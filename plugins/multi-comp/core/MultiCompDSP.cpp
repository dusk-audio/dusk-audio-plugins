// Copyright (C) 2026 Dusk Audio , GNU GPL v3.0 or later (see repository LICENSE).
#include "MultiCompDSP.hpp"
#include "MultiCompBusControls.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace duskaudio
{

namespace
{
template <typename T>
void copyParameter(std::atomic<T>& destination, const std::atomic<T>& source) noexcept
{
    destination.store(source.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

// Global Lookahead belongs to the digital compressors (Digital, Multiband). The
// modes that follow a hardware unit ignore it: a detector lead over the audio
// has no counterpart there, so it adds neither delay nor latency.
float globalLookaheadForMode(MultiCompMode mode, float milliseconds) noexcept
{
    return mode == MultiCompMode::Digital || mode == MultiCompMode::Multiband
        ? std::clamp(milliseconds, 0.0f, 10.0f) : 0.0f;
}

// The Oversampling parameter's factor: 0 off, 1 2x, 2 4x.
int oversamplingFactorForSetting(int setting) noexcept
{
    return setting == 2 ? 4 : setting == 1 ? 2 : 1;
}

// The audio anti-alias oversampler's factor for a mode. Opto and vintage FET keep the mode rate of a 44.1 / 48 kHz
// host (fixedRateModeFactor()): their side chains are designed and fitted at the default 2x setting's mode rate
// there and depend on it (the FET loop since 2026-10-20). Studio VCA runs at the host rate at every setting: its
// reference has no oversampling, and its gain modulation (and what that aliases) is part of the measured sound.
// Every other mode runs the setting.
int audioFactorForMode(MultiCompMode mode, int settingFactor, double hostRate) noexcept
{
    if (mode == MultiCompMode::StudioVCA) return 1;
    return mode == MultiCompMode::Opto || mode == MultiCompMode::FET || mode == MultiCompMode::FetBlue
        ? fixedRateModeFactor(settingFactor, hostRate) : settingFactor;
}

// The curve of n next() calls. A smoother whose step leaves its value's bits unchanged has a fixed state and
// returns that value for good; a linear ramp that has arrived returns its target without moving. Either way the
// rest of the block is that value.
void fillRampCurve(SmoothedValue& smoother, float* curve, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const float before = smoother.value();
        curve[i] = smoother.next();
        std::uint32_t a, b;
        std::memcpy(&a, &before, sizeof a);
        std::memcpy(&b, &curve[i], sizeof b);
        if (a == b)
        {
            std::fill(curve + i + 1, curve + n, curve[i]);
            return;
        }
    }
}
void fillRampCurve(LinearRamp& ramp, float* curve, int n) noexcept
{
    if (ramp.arrived())
    {
        std::fill_n(curve, n, ramp.next());
        return;
    }
    for (int i = 0; i < n; ++i) curve[i] = ramp.next();
}

// The installed unit's first-cycle output peak SATURATES. Twelve dB of extra
// input moves it +8.83 dB from -30 to -18 dBFS but only +1.63 dB from -18 to
// -6 dBFS (Attack 0.00, 4 kHz reference peaks 0.9672 / 2.6765 / 3.2279). That
// is a ceiling signature, not a detector one, and it is the mechanism the
// vintage path was missing: our own peaks ran 6.69-7.01 dB high across the
// whole -6 dBFS row and +3.39/+6.62 dB at Attack 1.00 lower down.
//
// This is deliberately a LEVEL-domain soft ceiling and not the time-windowed
// output gain an earlier attempt used, because the two gates that constrain it
// overlap in time and cannot both be satisfied by any choice of window. The
// 4 kHz attack estimator fits from half a carrier period (0.125 ms) out to
// 20 ms, while the measured first-cycle peaks sit at 0.0208-0.2500 ms. A gain
// held across a fixed window multiplies the estimator's exponential by a
// non-exponential envelope, which moved the fitted tau by up to 26 percentage
// points on two rows -- proven by disabling the stage outright on diagnostic
// binary 28dd845b6adf, which restored the pre-stage 23.487 % worst exactly.
//
// A ceiling has the property the window cannot buy: it is inert on every
// sample already below the target, so it stops acting the moment the envelope
// has decayed past it. At -30 dBFS/Attack 1.00 that is 0.065 ms after onset,
// before the estimator's first fitted sample.
float fetStartupPeakTarget(float sourcePeakDbfs, float attackPosition,
                           int ratioIndex) noexcept
{
    // Reference whole-capture peaks, each entry fitted to the gated 1 kHz
    // dynamics campaign where that measurement exists and to the 4 kHz
    // high-carrier probe where it does not. `testFetStartupPeakSurface` asserts
    // the 1 kHz Attack 0.00/0.50/1.00 knots of the -6 dBFS row (and the ratio
    // rows at 0.50); nothing was ever captured at 1 kHz for Attack 0.25 or
    // 0.75, so those two take the 4 kHz numbers rather than an interpolation
    // between the knots either side. An earlier revision did interpolate them,
    // and the 4 kHz probe rejected it: Attack 0.75 came out 3.847 dB low with
    // its curve RMS up from 0.01808 to 0.04877, because the invented target
    // (1.3554) sat far under the measured one (2.5606).
    //
    // That leaves the row non-monotonic in Attack, and the non-monotonicity is
    // real rather than a fitting artefact: the installed unit's first-cycle
    // peak is CARRIER dependent wherever its own detector wins the race to the
    // first crest, so 1 kHz reads 2.4892 at Attack 0.50 where 4 kHz reads
    // 3.1495. A single carrier-independent ceiling cannot serve both, and the
    // gated carrier wins. The cost is recorded in the scorecard: the 4 kHz
    // Attack 1.00 row keeps a -3.394 dB residual, where the reference is
    // detector-bound (its peaks are 0.9113 at 1 kHz against 1.3908 at 4 kHz)
    // while ours is ceiling-bound and therefore nearly carrier independent.
    //
    // The -18 and -30 rows are 4 kHz throughout; no 1 kHz row was ever
    // captured at those drives.
    constexpr std::array<float, 3> sourceDbfs{{-30.0f, -18.0f, -6.0f}};
    constexpr std::array<float, 5> attackKnots{{
        0.0f, 0.25f, 0.5f, 0.75f, 1.0f}};
    // Stored knee-compensated, NOT as the raw reference peak: the soft knee
    // below passes `startupCeilingSlope` of the excess, so a table holding the
    // target itself lands high by exactly that fraction (measured: +0.444 to
    // +1.007 dB, core c9281fa8bc9b). Each entry is the C solving
    // `T = C + (P - C) * slope` for the measured reference peak T and our own
    // measured raw peak P, i.e. `C = (T - slope * P) / (1 - slope)`.
    constexpr std::array<std::array<float, 5>, 3> peaks{{
        {{0.9677f, 0.9350f, 0.8921f, 0.8034f, 0.5058f}},
        {{2.7337f, 2.3762f, 2.1709f, 1.8299f, 0.7835f}},
        {{2.7164f, 2.6670f, 2.3360f, 2.5606f, 0.7865f}},
    }};
    // Ratio scaling, measured at Attack 0.50 / -6 dBFS on the 1 kHz reference
    // (2.4892 / 2.1281 / 1.8324 / 1.4771 / 2.5832), knee-compensated the same
    // way and expressed against the 4:1 entry. The 8:1 entry is the reference
    // peak itself rather than a solved C, because our raw 8:1 peak (2.1056)
    // already sits BELOW the reference (2.1281): that row needs the ceiling to
    // stay inert, and solving it would have pulled the target under our own
    // peak and started clipping a row that was already within 0.092 dB. Held
    // flat in Attack because no other ratio row was captured, and faded out
    // towards -30 dBFS where no ratio row was captured at all.
    constexpr std::array<float, 5> ratioScale{{
        1.0f, 0.910999f, 0.772807f, 0.603782f, 1.029946f}};

    size_t sourceHigh = 1;
    if (sourcePeakDbfs >= sourceDbfs.back())
        sourceHigh = sourceDbfs.size() - 1;
    else
        while (sourceHigh < sourceDbfs.size() - 1
               && sourcePeakDbfs > sourceDbfs[sourceHigh])
            ++sourceHigh;
    const size_t sourceLow = sourceHigh - 1;
    const float sourceFraction = std::clamp(
        (sourcePeakDbfs - sourceDbfs[sourceLow])
            / (sourceDbfs[sourceHigh] - sourceDbfs[sourceLow]),
        0.0f, 1.0f);

    size_t attackHigh = 1;
    if (attackPosition >= attackKnots.back())
        attackHigh = attackKnots.size() - 1;
    else
        while (attackHigh < attackKnots.size() - 1
               && attackPosition > attackKnots[attackHigh])
            ++attackHigh;
    const size_t attackLow = attackHigh - 1;
    const float attackFraction = std::clamp(
        (attackPosition - attackKnots[attackLow])
            / (attackKnots[attackHigh] - attackKnots[attackLow]),
        0.0f, 1.0f);
    const auto interpolateAttack = [&](size_t sourceIndex) noexcept {
        return peaks[sourceIndex][attackLow]
            + attackFraction
                * (peaks[sourceIndex][attackHigh]
                    - peaks[sourceIndex][attackLow]);
    };
    const float low = interpolateAttack(sourceLow);
    const float high = interpolateAttack(sourceHigh);
    const float target = low + sourceFraction * (high - low);

    const float ratioDepth = std::clamp(
        (sourcePeakDbfs + 30.0f) / 24.0f, 0.0f, 1.0f);
    const float scale = 1.0f + ratioDepth
        * (ratioScale[static_cast<size_t>(std::clamp(ratioIndex, 0, 4))]
            - 1.0f);
    return target * scale;
}
}

void MultiCompDSP::prepare(double sr, int blockSize)
{
    busFadeControl = 0.0f;
    busFadeMeter.store(0.0f, std::memory_order_relaxed);
    sampleRate = std::isfinite(sr) && sr > 0.0 ? sr : 48000.0;
    busCompressionMeter.prepare(sampleRate);
    busMeterReading.store(0.0f, std::memory_order_relaxed);
    maxBlock = std::max(1, blockSize);
    const int settingOversampling = oversamplingFactorForSetting(params.oversampling.load(std::memory_order_relaxed));
    const int initialOversampling = audioFactorForMode(
        static_cast<MultiCompMode>(std::clamp(params.mode.load(std::memory_order_relaxed), 0, 7)),
        settingOversampling, sampleRate);
    modes.setOptoSettingFactor(settingOversampling);
    modes.prepare(sampleRate, maxBlock, initialOversampling);
    // MultiCompModes::prepare deliberately preserves state when its rate and
    // factor are unchanged. A host prepare is nevertheless a lifecycle reset
    // for the finite FET post-burst helper, including repeated prepare calls.
    modes.clearFetPostBurstRecovery();
    modes.clearBusState();
    if (vcaLaw::kLifecycleResetEnabled) modes.clearVcaState();
    vcaLifecycleChannels = 0;
    truePeakDetector.prepare();
    truePeakDetector.setQuality(MultiCompTruePeakDetector::Quality::Standard4x);
    for (auto& os : oversamplers) { os.setFactor(4); os.prepare(maxBlock); os.reset(); }
    for (auto& os : busExternalOversamplers) { os.setFactor(4); os.prepare(maxBlock); }
    optoLinkedDetectorUpsampler.reset();
    for (auto& os : optoOwnDetectorUpsamplers) os.reset();
    for (auto& os : busExternalOversamplers) os.reset();
    antiAliasLatency = static_cast<int>(std::lround(oversamplers[0].latency()));
    for (auto& os : oversamplers) os.setFactor(initialOversampling);
    optoLinkedDetectorUpsampler.setFactor(initialOversampling);
    for (auto& os : optoOwnDetectorUpsamplers) os.setFactor(initialOversampling);
    for (auto& os : busExternalOversamplers) os.setFactor(initialOversampling);
    for (auto& f : sidechainFilters) f.prepare(sampleRate);
    for (auto& f : sidechainTilt) f.prepare(sampleRate);
    for (auto& f : vcaScResponse) f.prepare(sampleRate);
    vcaScResponseActive = false;
    for (auto& f : vcaOutputVoicing) f.prepare(sampleRate);
    for (auto& f : vcaAudioCoupling) f.prepare(sampleRate, vcaLaw::kAudioCouplingHz);
    for (auto& f : vcaDetectorCoupling) f.prepare(sampleRate, vcaLaw::kDetectorCouplingHz);
    vcaDetectorCouplingActive = false;
    vcaAudioCouplingActive = false;
    vcaVoicingActive = false;
    for (auto& f : sidechainEQ) f.prepare(sampleRate);
    for (auto& band : bands) for (auto& v : band) v.assign(static_cast<size_t>(maxBlock), 0.0f);
    for (auto& band : sidechainBands) for (auto& v : band) v.assign(static_cast<size_t>(maxBlock), 0.0f);
    for (auto& v : processedSidechain) v.assign(static_cast<size_t>(maxBlock), 0.0f);
    for (auto& v : modeInput) v.assign(static_cast<size_t>(maxBlock), 0.0f);
    for (auto& v : vcaSidechain) v.assign(static_cast<size_t>(maxBlock), 0.0f);
    vcaOutputDbCurve.assign(static_cast<size_t>(maxBlock), 0.0f);
    vcaEnvelopes.assign(static_cast<size_t>(maxBlock * 4), 1.0f);
    vcaLawScratch.assign(static_cast<size_t>(2 * (maxBlock + 64)), 0.0f);
    vcaOutputGains.assign(static_cast<size_t>(maxBlock), 1.0f);
    vcaPhases.assign(static_cast<size_t>(maxBlock * 4), 0.0f);
    for (auto& v : vcaFeedScratch) v.assign(static_cast<size_t>(maxBlock), 0.0f);
    for (auto& v : audioPhaseBlock) v.assign(static_cast<size_t>(maxBlock * 4), 0.0f);
    for (auto& v : optoOwnPhaseBlock) v.assign(static_cast<size_t>(maxBlock * 4), 0.0f);
    for (auto& v : outputPhaseBlock) v.assign(static_cast<size_t>(maxBlock * 4), 0.0f);
    for (auto& v : fetSidechain) v.assign(static_cast<size_t>(maxBlock), 0.0f);
    for (auto& v : fetScPhases) v.assign(static_cast<size_t>(maxBlock * 4), 0.0f);
    for (auto& v : fetPartnerPhases) v.assign(static_cast<size_t>(maxBlock * 4), 0.0f);
    fetOutputDbCurve.assign(static_cast<size_t>(maxBlock), 0.0f);
    for (auto& v : busScPhases) v.assign(static_cast<size_t>(maxBlock * 4), 0.0f);
    busOutputDbCurve.assign(static_cast<size_t>(maxBlock), 0.0f);
    busMixCurve.assign(static_cast<size_t>(maxBlock), 0.0f);
    for (auto& v : studioVcaLevels) v.assign(static_cast<size_t>(maxBlock), 0.0f);
    for (auto& v : studioVcaGainCurves) v.assign(static_cast<size_t>(maxBlock), 1.0f);
    studioVcaMakeupDb.assign(static_cast<size_t>(maxBlock), 0.0f);
    studioVcaMakeupGain.assign(static_cast<size_t>(maxBlock), 1.0f);
    for (auto& v : optoOwnFeed) v.assign(static_cast<size_t>(maxBlock), 0.0f);
    optoLinkedPhaseBlock.assign(static_cast<size_t>(maxBlock * 4), 0.0f);
    optoGainKnob.assign(static_cast<size_t>(maxBlock), 0.0f);
    for (auto& v : optoStageFeeds) v.assign(static_cast<size_t>(maxBlock * 4), MultiCompModes::OptoStageFeed{});
    optoLinkedDetectorUpsampler.reserveBlock(maxBlock);
    for (auto& os : optoOwnDetectorUpsamplers) os.reserveBlock(maxBlock);
    for (auto& v : sanitizedInput) v.assign(static_cast<size_t>(maxBlock), 0.0f);
    for (auto& v : sanitizedSidechain) v.assign(static_cast<size_t>(maxBlock), 0.0f);
    for (auto& v : crossoverCurves) v.assign(static_cast<size_t>(maxBlock), 0.0f);
    multibandOutputGains.assign(static_cast<size_t>(maxBlock), 1.0f);
    dry.assign(static_cast<size_t>(maxBlock * kMaxChannels), 0.0f);
    bypassDry.assign(static_cast<size_t>(maxBlock * kMaxChannels), 0.0f);
    fetStartupInput.assign(
        static_cast<size_t>(maxBlock * kMaxChannels), 0.0f);
    mixCurve.assign(static_cast<size_t>(maxBlock), 1.0f);
    bypassCurve.assign(static_cast<size_t>(maxBlock), 0.0f);
    autoGainCurve.assign(static_cast<size_t>(maxBlock), 1.0f);
    listenCurve.assign(static_cast<size_t>(maxBlock), 0.0f);
    listenSource.assign(static_cast<size_t>(maxBlock), 0.0f);
    fadeCurve.assign(static_cast<size_t>(maxBlock), 1.0f);
    switchDuckCurve.assign(static_cast<size_t>(maxBlock), 1.0f);
    for (auto& line : delayedInput) line.assign(static_cast<size_t>(maxBlock), 0.0f);
    const size_t lookaheadSize = static_cast<size_t>(std::ceil(sampleRate * 0.01)) + 1u;
    for (auto& line : globalLookahead) line.assign(lookaheadSize, 0.0f);
    globalLookaheadWrite = {{0, 0}};
    previousOversampledSidechain = {{0.0f, 0.0f}};
    previousOversampledSidechainValid = {{false, false}};
    previousOptoOwnSidechain = {{0.0f, 0.0f}};
    previousOptoOwnSidechainValid = {{false, false}};
    for (auto& line : hostSidechainHistory) line.fill(0.0f);
    hostSidechainWrite = 0;
    fetStartupInputPeak = {{0.0f, 0.0f}};
    fetStartupActiveSamples = {{0, 0}};
    fetStartupSilentSamples = {{0, 0}};
    // The dry, bypass and listen histories reach the host latency with host
    // latency padding on: the deepest mode's base latency (anti-alias plus
    // Opto's output stage) instead of the anti-alias latency alone.
    const int deepestBase = getMaximumLatencyPaddingSamples();
    // Each lookahead is rounded on its own, so two of them can come to more
    // than 20 ms rounded up (221 + 221 at 22.05 kHz).
    const int longestLookahead = lookaheadSamples(10.0f);
    const size_t dryDelaySize = static_cast<size_t>(std::max(1, deepestBase + longestLookahead + 1));
    for (auto& line : dryPathDelay) line.assign(dryDelaySize, 0.0f);
    dryPathWrite = {{0, 0}};
    // Raw bypass history must cover the largest runtime latency combination:
    // global lookahead plus Digital lookahead, in addition to the deepest base.
    const size_t bypassDelaySize = static_cast<size_t>(std::max(1, deepestBase + 2 * longestLookahead + 1));
    for (auto& line : bypassDelay) line.assign(bypassDelaySize, 0.0f);
    bypassWrite = 0;
    for (auto& line : sidechainListenDelay) line.assign(bypassDelaySize, 0.0f);
    sidechainListenWrite = {{0, 0}};
    globalMixSmoother.prepare(sampleRate, 0.020f); globalMixSmoother.snap(globalMixTarget(currentMode()));
    autoGainMatcher.prepare(sampleRate);
    resetAutoGainMeasurement();
    autoGainSmoother.prepare(sampleRate, static_cast<float>(kAutoGainTransitionMs) * 0.001f); autoGainSmoother.snap(1.0f);
    manualMakeupScaleRamp.prepare(sampleRate, static_cast<float>(kAutoGainTransitionMs) * 0.001f);
    manualMakeupScaleRamp.snap(params.autoMakeup.load(std::memory_order_relaxed) ? 0.0f : 1.0f);
    busMixRamp.prepare(sampleRate, 0.020f); busMixRamp.snap(params.busMix.load(std::memory_order_relaxed) * 0.01f);
    digitalMixRamp.prepare(sampleRate, 0.020f); digitalMixRamp.snap(params.digitalMix.load(std::memory_order_relaxed) * 0.01f);
    bypassRamp.prepare(sampleRate, static_cast<float>(kBypassRampMs) * 0.001f); bypassRamp.snap(0.0f);
    sidechainListenRamp.prepare(sampleRate, static_cast<float>(kSidechainListenRampMs) * 0.001f);
    sidechainListenRamp.snap(params.globalSidechainListen.load(std::memory_order_relaxed) ? 1.0f : 0.0f);
    switchDuckFadeSamples = std::max(1, static_cast<int>(std::lround(sampleRate * kSwitchDuckFadeMs * 0.001)));
    resetSwitchDuck();
    bypassSettled = false; lastBypass = false; lastExternalSidechain = false;
    lastAutoMakeup = false; firstBlock = true; lastMode = -1; autoGainHoldSamples = 0;
    lastVcaSidechainTilt = false;
    lastVcaSidechainTiltEngaged = false;
    noiseState = 0x6d2b79f5u;
    multibandEnvelopes.fill(1.0f);
    prepareCrossovers();
    activeBandMask = 0;
    rebuildMultibandTopology(0x0f);
    for (auto& m : bandGR) m.store(0.0f, std::memory_order_relaxed);
    masterGR.store(0.0f, std::memory_order_relaxed);
    inputLevel.store(-60.0f, std::memory_order_relaxed);
    outputLevel.store(-60.0f, std::memory_order_relaxed);
    latencyPad.prepare(getMaximumLatencyPaddingSamples(), maxBlock);
    multibandDryPad.prepare(getMaximumLatencyPaddingSamples(), maxBlock);
}

void MultiCompDSP::reset()
{
    latencyPad.reset();
    multibandDryPad.reset();
    busCompressionMeter.reset();
    busMeterReading.store(0.0f, std::memory_order_relaxed);
    busFadeControl = 0.0f;
    busFadeMeter.store(0.0f, std::memory_order_relaxed);
    modes.reset();
    vcaLifecycleChannels = 0;
    truePeakDetector.prepare();
    for (auto& os : oversamplers) os.reset();
    optoLinkedDetectorUpsampler.reset();
    for (auto& os : optoOwnDetectorUpsamplers) os.reset();
    for (auto& os : busExternalOversamplers) os.reset();
    for (auto& f : sidechainFilters) f.reset();
    for (auto& f : sidechainTilt) f.reset();
    for (auto& f : vcaScResponse) f.reset();
    vcaScResponseActive = false;
    for (auto& f : vcaOutputVoicing) f.reset();
    for (auto& f : vcaAudioCoupling) f.reset();
    for (auto& f : vcaDetectorCoupling) f.reset();
    vcaDetectorCouplingActive = false;
    vcaAudioCouplingActive = false;
    vcaVoicingActive = false;
    for (auto& f : sidechainEQ) f.reset();
    resetCrossovers();
    for (auto& band : bands) for (auto& v : band) std::fill(v.begin(), v.end(), 0.0f);
    for (auto& band : sidechainBands) for (auto& v : band) std::fill(v.begin(), v.end(), 0.0f);
    std::fill(multibandOutputGains.begin(), multibandOutputGains.end(), 1.0f);
    for (auto& v : processedSidechain) std::fill(v.begin(), v.end(), 0.0f);
    for (auto& v : modeInput) std::fill(v.begin(), v.end(), 0.0f);
    for (auto& v : sanitizedInput) std::fill(v.begin(), v.end(), 0.0f);
    for (auto& v : sanitizedSidechain) std::fill(v.begin(), v.end(), 0.0f);
    std::fill(dry.begin(), dry.end(), 0.0f);
    std::fill(bypassDry.begin(), bypassDry.end(), 0.0f);
    std::fill(fetStartupInput.begin(), fetStartupInput.end(), 0.0f);
    for (auto& line : delayedInput) std::fill(line.begin(), line.end(), 0.0f);
    for (auto& line : globalLookahead) std::fill(line.begin(), line.end(), 0.0f);
    globalLookaheadWrite = {{0, 0}};
    previousOversampledSidechain = {{0.0f, 0.0f}};
    previousOversampledSidechainValid = {{false, false}};
    previousOptoOwnSidechain = {{0.0f, 0.0f}};
    previousOptoOwnSidechainValid = {{false, false}};
    for (auto& line : hostSidechainHistory) line.fill(0.0f);
    hostSidechainWrite = 0;
    fetStartupInputPeak = {{0.0f, 0.0f}};
    fetStartupActiveSamples = {{0, 0}};
    fetStartupSilentSamples = {{0, 0}};
    for (auto& line : dryPathDelay) std::fill(line.begin(), line.end(), 0.0f);
    dryPathWrite = {{0, 0}};
    for (auto& line : bypassDelay) std::fill(line.begin(), line.end(), 0.0f);
    bypassWrite = 0;
    for (auto& line : sidechainListenDelay) std::fill(line.begin(), line.end(), 0.0f);
    sidechainListenWrite = {{0, 0}};
    multibandEnvelopes.fill(1.0f);
    activeBandMask = 0;
    rebuildMultibandTopology(0x0f);
    globalMixSmoother.snap(globalMixTarget(currentMode()));
    autoGainMatcher.reset();
    resetAutoGainMeasurement();
    autoGainSmoother.snap(1.0f);
    manualMakeupScaleRamp.snap(params.autoMakeup.load(std::memory_order_relaxed) ? 0.0f : 1.0f);
    busMixRamp.snap(params.busMix.load(std::memory_order_relaxed) * 0.01f);
    digitalMixRamp.snap(params.digitalMix.load(std::memory_order_relaxed) * 0.01f);
    bypassRamp.snap(params.bypass.load(std::memory_order_relaxed) ? 1.0f : 0.0f);
    sidechainListenRamp.snap(params.globalSidechainListen.load(std::memory_order_relaxed) ? 1.0f : 0.0f);
    bypassSettled = params.bypass.load(std::memory_order_relaxed);
    lastBypass = bypassSettled;
    lastExternalSidechain = false;
    lastAutoMakeup = false;
    firstBlock = true;
    lastMode = -1;
    resetSwitchDuck();
    lastVcaSidechainTilt = false;
    lastVcaSidechainTiltEngaged = false;
    autoGainHoldSamples = 0;
    noiseState = 0x6d2b79f5u;
    for (auto& m : bandGR) m.store(0.0f, std::memory_order_relaxed);
    masterGR.store(0.0f, std::memory_order_relaxed);
}

void MultiCompDSP::setParameter(Parameter parameter, float value) noexcept
{
    // std::clamp passes NaN straight through, and several cases below convert
    // to int or feed a table index. A non-finite host value keeps the last
    // good setting instead.
    if (!std::isfinite(value))
        return;
    const bool b = value >= 0.5f;
    switch (parameter)
    {
        case Parameter::None: break;
        case Parameter::Mode: params.mode.store(std::clamp(static_cast<int>(value), 0, 7), std::memory_order_relaxed); break;
        case Parameter::Bypass: params.bypass.store(b, std::memory_order_relaxed); break;
        case Parameter::StereoLink: params.stereoLink.store(std::clamp(value, 0.0f, 100.0f), std::memory_order_relaxed); break;
        case Parameter::Mix: params.mix.store(std::clamp(value, 0.0f, 100.0f), std::memory_order_relaxed); break;
        case Parameter::SidechainHP: params.sidechainHP.store(std::clamp(value, 0.0f, 500.0f), std::memory_order_relaxed); break;
        case Parameter::TruePeakEnable: params.truePeakEnable.store(b, std::memory_order_relaxed); break;
        case Parameter::TruePeakQuality: params.truePeakQuality.store(std::clamp(static_cast<int>(value), 0, 1), std::memory_order_relaxed); break;
        case Parameter::ExternalSidechain: params.externalSidechain.store(b, std::memory_order_relaxed); break;
        case Parameter::AutoMakeup: params.autoMakeup.store(b, std::memory_order_relaxed); break;
        case Parameter::Distortion: params.distortion.store(std::clamp(static_cast<int>(value), 0, 3), std::memory_order_relaxed); break;
        case Parameter::DistortionAmount: params.distortionAmount.store(std::clamp(value, 0.0f, 100.0f), std::memory_order_relaxed); break;
        case Parameter::Oversampling: params.oversampling.store(std::clamp(static_cast<int>(value), 0, 2), std::memory_order_relaxed); break;
        case Parameter::GlobalLookahead: params.globalLookahead.store(std::clamp(value, 0.0f, 10.0f), std::memory_order_relaxed); break;
        case Parameter::OptoPeakReduction: params.optoPeakReduction.store(value, std::memory_order_relaxed); break;
        case Parameter::OptoGain: params.optoGain.store(value, std::memory_order_relaxed); break;
        case Parameter::OptoLimit: params.optoLimit.store(b, std::memory_order_relaxed); break;
        case Parameter::FetInput: params.fetInput.store(value, std::memory_order_relaxed); break;
        case Parameter::FetOutput: params.fetOutput.store(value, std::memory_order_relaxed); break;
        case Parameter::FetAttack: params.fetAttack.store(value, std::memory_order_relaxed); break;
        case Parameter::FetRelease: params.fetRelease.store(value, std::memory_order_relaxed); break;
        case Parameter::FetRatio: params.fetRatio.store(static_cast<int>(value), std::memory_order_relaxed); break;
        case Parameter::FetCurve: params.fetCurve.store(static_cast<int>(value), std::memory_order_relaxed); break;
        case Parameter::FetTransient: params.fetTransient.store(value, std::memory_order_relaxed); break;
        case Parameter::FetThreshold: params.fetThreshold.store(value, std::memory_order_relaxed); break;
        case Parameter::VcaThreshold: params.vcaThreshold.store(value, std::memory_order_relaxed); break;
        case Parameter::VcaRatio: params.vcaRatio.store(value, std::memory_order_relaxed); break;
        case Parameter::VcaAttack: params.vcaAttack.store(value, std::memory_order_relaxed); break;
        case Parameter::VcaRelease: params.vcaRelease.store(value, std::memory_order_relaxed); break;
        case Parameter::VcaOutput: params.vcaOutput.store(value, std::memory_order_relaxed); break;
        case Parameter::VcaOverEasy: params.vcaOverEasy.store(b, std::memory_order_relaxed); break;
        case Parameter::VcaClassicDetector: params.vcaClassicDetector.store(b, std::memory_order_relaxed); break;
        case Parameter::BusThreshold: params.busThreshold.store(value, std::memory_order_relaxed); break;
        case Parameter::BusRatio: params.busRatio.store(static_cast<int>(value), std::memory_order_relaxed); break;
        case Parameter::BusAttack: params.busAttack.store(static_cast<int>(value), std::memory_order_relaxed); break;
        case Parameter::BusRelease: params.busRelease.store(static_cast<int>(value), std::memory_order_relaxed); break;
        case Parameter::BusMakeup: params.busMakeup.store(value, std::memory_order_relaxed); break;
        case Parameter::BusHeadroom: params.busHeadroom.store(std::clamp(static_cast<int>(std::round(value)), 0, 6), std::memory_order_relaxed); break;
        case Parameter::BusFadeRate: params.busFadeRate.store(std::clamp(value, 1.0f, 60.0f), std::memory_order_relaxed); break;
        case Parameter::BusFade: params.busFade.store(b, std::memory_order_relaxed); break;
        case Parameter::BusMix: params.busMix.store(value, std::memory_order_relaxed); break;
        case Parameter::StudioVcaThreshold: params.studioVcaThreshold.store(value, std::memory_order_relaxed); break;
        case Parameter::StudioVcaRatio: params.studioVcaRatio.store(value, std::memory_order_relaxed); break;
        case Parameter::StudioVcaAttack: params.studioVcaAttack.store(value, std::memory_order_relaxed); break;
        case Parameter::StudioVcaRelease: params.studioVcaRelease.store(value, std::memory_order_relaxed); break;
        case Parameter::StudioVcaOutput: params.studioVcaOutput.store(value, std::memory_order_relaxed); break;
        case Parameter::StudioVcaInput: params.studioVcaInput.store(value, std::memory_order_relaxed); break;
        case Parameter::StudioVcaAutoRelease: params.studioVcaAutoRelease.store(b, std::memory_order_relaxed); break;
        case Parameter::FetBlueRatio: params.fetBlueRatio.store(std::clamp(static_cast<int>(std::round(value)), 0, 10), std::memory_order_relaxed); break;
        case Parameter::FetBlueScFilter: params.fetBlueScFilter.store(b, std::memory_order_relaxed); break;
        case Parameter::FetBlueHeadroom: params.fetBlueHeadroom.store(std::clamp(static_cast<int>(std::round(value)), 0, 6), std::memory_order_relaxed); break;
        case Parameter::DigitalThreshold: params.digitalThreshold.store(value, std::memory_order_relaxed); break;
        case Parameter::DigitalRatio: params.digitalRatio.store(value, std::memory_order_relaxed); break;
        case Parameter::DigitalKnee: params.digitalKnee.store(value, std::memory_order_relaxed); break;
        case Parameter::DigitalAttack: params.digitalAttack.store(value, std::memory_order_relaxed); break;
        case Parameter::DigitalRelease: params.digitalRelease.store(value, std::memory_order_relaxed); break;
        case Parameter::DigitalLookahead: params.digitalLookahead.store(value, std::memory_order_relaxed); break;
        case Parameter::DigitalMix: params.digitalMix.store(std::clamp(value, 0.0f, 100.0f), std::memory_order_relaxed); break;
        case Parameter::DigitalOutput: params.digitalOutput.store(value, std::memory_order_relaxed); break;
        case Parameter::DigitalAdaptive: params.digitalAdaptive.store(b, std::memory_order_relaxed); break;
        case Parameter::Crossover1: params.crossover1.store(value, std::memory_order_relaxed); break;
        case Parameter::Crossover2: params.crossover2.store(value, std::memory_order_relaxed); break;
        case Parameter::Crossover3: params.crossover3.store(value, std::memory_order_relaxed); break;
        case Parameter::EnvelopeCurve: params.envelopeCurve.store(std::clamp(static_cast<int>(value), 0, 1), std::memory_order_relaxed); break;
        case Parameter::GlobalSidechainListen: params.globalSidechainListen.store(b, std::memory_order_relaxed); break;
        case Parameter::MbMix: params.mbMix.store(std::clamp(value, 0.0f, 100.0f), std::memory_order_relaxed); break;
        case Parameter::MbOutput: params.mbOutput.store(std::clamp(value, -24.0f, 24.0f), std::memory_order_relaxed); break;
        case Parameter::NoiseEnable: params.noiseEnable.store(b, std::memory_order_relaxed); break;
        case Parameter::SaturationMode: params.saturationMode.store(std::clamp(static_cast<int>(value), 0, 2), std::memory_order_relaxed); break;
        case Parameter::ScLowFreq: params.scLowFreq.store(std::clamp(value, 60.0f, 500.0f), std::memory_order_relaxed); break;
        case Parameter::ScLowGain: params.scLowGain.store(std::clamp(value, -12.0f, 12.0f), std::memory_order_relaxed); break;
        case Parameter::ScHighFreq: params.scHighFreq.store(std::clamp(value, 2000.0f, 16000.0f), std::memory_order_relaxed); break;
        case Parameter::ScHighGain: params.scHighGain.store(std::clamp(value, -12.0f, 12.0f), std::memory_order_relaxed); break;
        case Parameter::StereoLinkMode: params.stereoLinkMode.store(std::clamp(static_cast<int>(value), 0, 2), std::memory_order_relaxed); break;
    }
}

void MultiCompDSP::setMultibandParameter(int band, MultibandParameter parameter, float value) noexcept
{
    if (band < 0 || band >= kMultiCompBands || !std::isfinite(value)) return;
    const size_t b = static_cast<size_t>(band);
    switch (parameter)
    {
        case MultibandParameter::Threshold: params.mbThreshold[b].store(value, std::memory_order_relaxed); break;
        case MultibandParameter::Ratio: params.mbRatio[b].store(value, std::memory_order_relaxed); break;
        case MultibandParameter::Attack: params.mbAttack[b].store(value, std::memory_order_relaxed); break;
        case MultibandParameter::Release: params.mbRelease[b].store(value, std::memory_order_relaxed); break;
        case MultibandParameter::Makeup: params.mbMakeup[b].store(value, std::memory_order_relaxed); break;
        case MultibandParameter::Bypass: params.mbBypass[b].store(value >= 0.5f, std::memory_order_relaxed); break;
        case MultibandParameter::Solo: params.mbSolo[b].store(value >= 0.5f, std::memory_order_relaxed); break;
        case MultibandParameter::Enabled: params.mbEnabled[b].store(value >= 0.5f, std::memory_order_relaxed); break;
    }
}

void MultiCompDSP::processBlock(const float* const* in, float* const* out, int nCh, int nSamples)
{
    processBlockExternal(in, nullptr, out, nCh, nSamples);
}

const float* const* MultiCompDSP::sanitizeChannels(
    const float* const* source, std::array<std::vector<float>, kMaxChannels>& scratch,
    const float* (&pointers)[kMaxChannels], int nCh, int nSamples) noexcept
{
    bool audio = true;
    for (int ch = 0; ch < nCh && audio; ++ch)
        audio = blockIsAllAudio(source[ch], nSamples);
    if (audio) return source;
    for (int ch = 0; ch < nCh; ++ch)
        copyWithoutNonAudio(source[ch], scratch[static_cast<size_t>(ch)].data(), nSamples);
    pointers[0] = scratch[0].data();
    pointers[1] = nCh > 1 ? scratch[1].data() : scratch[0].data();
    return pointers;
}

float MultiCompDSP::advanceFetStartupBlend(
    int& activeSamples, int fullCorrectionSamples,
    int correctionEndSamples) noexcept
{
    if (activeSamples < correctionEndSamples)
        ++activeSamples;
    if (activeSamples <= fullCorrectionSamples)
        return 1.0f;
    return std::clamp(static_cast<float>(correctionEndSamples - activeSamples)
                          / static_cast<float>(correctionEndSamples
                                               - fullCorrectionSamples),
                      0.0f, 1.0f);
}

void MultiCompDSP::processBlockExternal(const float* const* in, const float* const* sidechain,
                                        float* const* out, int nCh, int nSamples)
{
    if (in == nullptr || out == nullptr || nSamples <= 0 || nCh <= 0 || dry.empty()) return;
    nCh = std::min(nCh, kMaxChannels);
    // Samples [offset, offset + count) of this block as a block of their own.
    const auto processPart = [&](int offset, int count) {
        const float* inPart[kMaxChannels] = {in[0] + offset, nCh > 1 ? in[1] + offset : in[0] + offset};
        const float* scPart[kMaxChannels] = {nullptr, nullptr};
        if (sidechain != nullptr)
        {
            scPart[0] = sidechain[0] + offset;
            scPart[1] = nCh > 1 ? sidechain[1] + offset : sidechain[0] + offset;
        }
        float* outPart[kMaxChannels] = {out[0] + offset, nCh > 1 ? out[1] + offset : out[0] + offset};
        processBlockExternal(inPart, sidechain != nullptr ? scPart : nullptr, outPart, nCh, count);
    };
    if (nSamples > maxBlock)
    {
        for (int offset = 0; offset < nSamples; offset += maxBlock)
            processPart(offset, std::min(maxBlock, nSamples - offset));
        return;
    }
    // A switch that changes the audio path (switchChangesAudioPath) holds the old mode while its wet path fades out
    // (switchDuck*). Where the fade reaches silence inside this block, the block runs in two parts: the old mode
    // up to silence, the new one after it.
    const int requestedMode = std::clamp(params.mode.load(std::memory_order_relaxed), 0, 7);
    if (requestedMode == switchDuckHeldMode)
        switchDuckHeldMode = -1;   // switched back before silence: no switch, the fade turns round
    else if (switchDuckHeldMode < 0 && !firstBlock && lastMode >= 0 && requestedMode != lastMode
             && switchChangesAudioPath(lastMode, requestedMode))
    {
        if (switchDuckPosition > 0)
            switchDuckHeldMode = lastMode;              // audible: fade the old mode out first
        else
        {   // already silent: switch now, mute the realignment
            switchDuckMuted = kSwitchDuckMuteSamples;
            holdPaddingThroughSwitch(lastMode);
        }
    }
    if (switchDuckHeldMode >= 0 && switchDuckPosition < nSamples)
    {
        const int silentAt = switchDuckPosition;
        if (silentAt > 0) processPart(0, silentAt);
        // The switch happens at silence only: a part that reset (non-finite recovery) or saw the request
        // withdrawn ends audible, and the rest then runs as any block does.
        if (switchDuckHeldMode >= 0 && switchDuckPosition == 0)
        {
            holdPaddingThroughSwitch(switchDuckHeldMode);
            switchDuckHeldMode = -1;
            switchDuckMuted = kSwitchDuckMuteSamples;
        }
        processPart(silentAt, nSamples - silentAt);
        return;
    }
    ScopedFlushDenormals guard;
    // Read once: the sidechain guard below and the detector source further
    // down must agree on whether the external bus is live this block.
    const bool useExternalSidechain = params.externalSidechain.load(std::memory_order_relaxed) && sidechain != nullptr;
    // Stop non-finite or above-ceiling input before any state reads it (see blockIsAllAudio in
    // MultiCompHelpers.hpp). Both entry points latch: the main input reaches
    // every stage, and the external sidechain on its own poisons the shelf EQ,
    // the detector oversamplers and the envelopes. The sidechain is scanned
    // only when it is actually read.
    const float* sanitizedInputChannels[kMaxChannels] = {nullptr, nullptr};
    const float* sanitizedSidechainChannels[kMaxChannels] = {nullptr, nullptr};
    in = sanitizeChannels(in, sanitizedInput, sanitizedInputChannels, nCh, nSamples);
    if (useExternalSidechain)
        sidechain = sanitizeChannels(sidechain, sanitizedSidechain,
                                     sanitizedSidechainChannels, nCh, nSamples);
    float blockInputPeak = 0.0f;
    for (int ch = 0; ch < nCh; ++ch)
        blockInputPeak = blockAbsPeak(in[ch], nSamples, blockInputPeak);
    const MultiCompMode mode = static_cast<MultiCompMode>(switchDuckHeldMode >= 0 ? switchDuckHeldMode : requestedMode);
    // Observe other modes even through settled bypass and Multiband's early
    // return, so the next VCA block cannot reuse stale output-filter history.
    if (vcaLaw::kLifecycleResetEnabled)
    {
        if (mode != MultiCompMode::VCA)
            vcaLifecycleChannels = 0;
        else
        {
            if (vcaLifecycleChannels == 0)
            {
                modes.clearVcaState();
                for (auto& os : oversamplers) os.reset();
            }
            if (nCh == 1 && vcaLifecycleChannels != 1)
            {
                // Only the newly absent channel: active left histories and
                // the shared bypass write clock continue without a reset.
                modes.clearVcaState(1);
                oversamplers[1].reset();
                vcaOutputVoicing[1].reset();
                sidechainEQ[1].reset();
                std::fill(dryPathDelay[1].begin(), dryPathDelay[1].end(), 0.0f);
                dryPathWrite[1] = 0;
                std::fill(bypassDelay[1].begin(), bypassDelay[1].end(), 0.0f);
                std::fill(globalLookahead[1].begin(), globalLookahead[1].end(), 0.0f);
                globalLookaheadWrite[1] = 0;
                std::fill(sidechainListenDelay[1].begin(), sidechainListenDelay[1].end(), 0.0f);
                sidechainListenWrite[1] = 0;
                latencyPad.clearChannel(1);
                multibandDryPad.clearChannel(1);
            }
            vcaLifecycleChannels = nCh;
        }
    }
    if (mode != MultiCompMode::VCA) vcaVoicingActive = false;
    if (mode != MultiCompMode::VCA)
        vcaAudioCouplingActive = false;
    else if (vcaLaw::kAudioCouplingStrength != 0.0f)
    {
        if (!vcaAudioCouplingActive)
            for (auto& f : vcaAudioCoupling) f.reset();
        vcaAudioCouplingActive = true;
        // A reduced-channel block must not preserve the absent channel's
        // coupling history for a later stereo block.
        if (nCh == 1) vcaAudioCoupling[1].reset();
    }
    if (mode != MultiCompMode::Bus)
    {
        busCompressionMeter.reset();
        busMeterReading.store(0.0f, std::memory_order_relaxed);
    }
    if (mode != MultiCompMode::FET)
    {
        fetStartupInputPeak = {{0.0f, 0.0f}};
        fetStartupActiveSamples = {{0, 0}};
        fetStartupSilentSamples = {{0, 0}};
        modes.clearFetPostBurstRecovery();
    }
    else
    {
        if (nCh == 1) modes.clearFetPostBurstRecovery(1);
        for (int ch = 0; ch < nCh; ++ch)
            std::copy_n(in[ch], nSamples,
                fetStartupInput.data() + static_cast<size_t>(ch * maxBlock));
    }
    const int linkMode = std::clamp(params.stereoLinkMode.load(std::memory_order_relaxed), 0, 2);
    const int settingOversampling = oversamplingFactorForSetting(params.oversampling.load(std::memory_order_relaxed));
    const int actualOversampling = audioFactorForMode(mode, settingOversampling, sampleRate);
    modes.setOptoSettingFactor(settingOversampling);
    const float globalLookaheadMs = globalLookaheadForMode(
        mode, params.globalLookahead.load(std::memory_order_relaxed));
    const float digitalLookaheadMs = std::clamp(
        params.digitalLookahead.load(std::memory_order_relaxed), 0.0f, 10.0f);
    const int blockLatency = latencySamplesForMode(mode, globalLookaheadMs, digitalLookaheadMs);
    // With host latency padding the dry, bypass and listen paths are delayed
    // straight to the host latency (blockLatency + this), which every mode
    // shares, so a mode switch never moves them; the wet path gets the same
    // padding on its own, before Mix (padWetPath).
    const int hostPadding = hostLatencyPadding ? paddingForMode(static_cast<int>(mode)) : 0;
    const bool requestedBypass = params.bypass.load(std::memory_order_relaxed);
    const bool requestedSidechainListen = params.globalSidechainListen.load(std::memory_order_relaxed);
    sidechainListenRamp.setTarget(requestedSidechainListen ? 1.0f : 0.0f);
    if (requestedBypass != lastBypass)
    {
        lastBypass = requestedBypass;
        bypassSettled = false;
        bypassRamp.setTarget(requestedBypass ? 1.0f : 0.0f);
    }
    // `bypassSettled` only moves at the edge above and at the very end of this
    // block, so one capture serves the whole chain. Settled bypass still runs
    // every stage; it just emits the latency-aligned dry instead of the result.
    const bool settledBypass = requestedBypass && bypassSettled;
    const float* filteredSidechain[kMaxChannels] = {processedSidechain[0].data(), processedSidechain[1].data()};
    // Only faces that carry this control (Opto / Digital SC HP, VCA PULL/SC,
    // Bus SC FILTER) apply it. Elsewhere a value set on one of those faces acted
    // unseen after a mode switch.
    const bool modeHasSidechainHP = mode == MultiCompMode::Opto || mode == MultiCompMode::VCA
        || mode == MultiCompMode::Bus || mode == MultiCompMode::Digital;
    const float sidechainHP = modeHasSidechainHP ? params.sidechainHP.load(std::memory_order_relaxed) : 0.0f;
    // In VCA mode the SC HP control is the reference VCA compressor's PULL/SC switch: settings
    // at or above 1 Hz engage the reference's measured half-order tilt in
    // place of the high-pass (MultiCompVcaLaw.hpp SidechainResponse).
    const bool vcaSidechainTilt = mode == MultiCompMode::VCA;
    const bool vcaSidechainPathChanged = vcaSidechainTilt != lastVcaSidechainTilt;
    if (vcaSidechainPathChanged)
    {
        // The filter that was not running holds state from whenever it last
        // ran; clear it before it takes over so a mode switch does not start
        // from a stale sidechain.
        if (vcaSidechainTilt)
            for (auto& f : sidechainTilt) f.reset();
        else
            for (auto& f : sidechainFilters) f.reset();
        lastVcaSidechainTilt = vcaSidechainTilt;
    }
    // This section sees the selected raw L/R detector source before SC
    // tilt/EQ, independently of the wet audio pole and M/S routing.
    if (!vcaSidechainTilt)
        vcaDetectorCouplingActive = false;
    else if (vcaLaw::kDetectorCouplingStrength != 0.0f)
    {
        if (!vcaDetectorCouplingActive)
            for (auto& f : vcaDetectorCoupling) f.reset();
        vcaDetectorCouplingActive = true;
        if (nCh == 1) vcaDetectorCoupling[1].reset();
    }
    if (!vcaSidechainTilt)
        vcaScResponseActive = false;
    else if (vcaLaw::kScResponseStrength != 0.0f)
    {
        if (!vcaScResponseActive)
            for (auto& filter : vcaScResponse)
                filter.reset();
        vcaScResponseActive = true;
        if (nCh == 1)
            vcaScResponse[1].reset();
    }
    // This legacy engage-reset policy exists only for the exact-off
    // control. The live measured response runs continuously and ramps its
    // selection weight; resetting it on an SC edge contradicts the captures.
    if constexpr (vcaLaw::kScResponseStrength == 0.0f)
    {
        const bool vcaSidechainTiltEngaged = vcaSidechainTilt && sidechainHP >= 1.0f;
        if (vcaSidechainTiltEngaged && !lastVcaSidechainTiltEngaged
            && !vcaSidechainPathChanged)
        {
            // While PULL/SC is out the tilt is bypassed and retains its last
            // history. Clear that stale history once, on the edge back to IN,
            // before the first newly filtered sample. A mode entry already reset
            // it in the path-change block above.
            for (auto& f : sidechainTilt) f.reset();
        }
        lastVcaSidechainTiltEngaged = vcaSidechainTiltEngaged;
    }
    if (vcaSidechainTilt && vcaLaw::kScResponseStrength != 0.0f && nCh == 2)
    {
        // VCA stereo: the same three stages per channel (detector coupling, SC
        // response, shelf EQ), each run over the block on both channels at once.
        float* left = processedSidechain[0].data();
        float* right = processedSidechain[1].data();
        std::copy_n(useExternalSidechain ? sidechain[0] : in[0], nSamples, left);
        std::copy_n(useExternalSidechain ? sidechain[1] : in[1], nSamples, right);
        for (int ch = 0; ch < nCh; ++ch)
        {
            sidechainFilters[ch].setFrequency(sidechainHP, 0.707f);
            sidechainEQ[ch].setLowShelf(params.scLowFreq.load(std::memory_order_relaxed), params.scLowGain.load(std::memory_order_relaxed));
            sidechainEQ[ch].setHighShelf(params.scHighFreq.load(std::memory_order_relaxed), params.scHighGain.load(std::memory_order_relaxed));
        }
        vcaLaw::LowFrequencyHighPass::processPair(vcaDetectorCoupling[0], vcaDetectorCoupling[1], left, right,
                                                  nSamples, vcaLaw::kDetectorCouplingStrength);
        vcaLaw::SidechainResponse::processPair(vcaScResponse[0], vcaScResponse[1], left, right, nSamples,
                                               sidechainHP >= 1.0f);
        MultiCompSidechainEQ::processPair(sidechainEQ[0], sidechainEQ[1], left, right, nSamples);
    }
    else if (nCh == 2 && !vcaSidechainTilt && !(sidechainHP >= 1.0f))
    {
        // Stereo with neither the high-pass nor the VCA's tilt engaged: the per-channel loop below would run the
        // shelf EQ alone, so both channels run it at once (processPair performs process()'s float arithmetic).
        float* left = processedSidechain[0].data();
        float* right = processedSidechain[1].data();
        std::copy_n(useExternalSidechain ? sidechain[0] : in[0], nSamples, left);
        std::copy_n(useExternalSidechain ? sidechain[1] : in[1], nSamples, right);
        for (int ch = 0; ch < nCh; ++ch)
        {
            sidechainFilters[ch].setFrequency(sidechainHP, mode == MultiCompMode::Bus ? 0.5f : 0.707f);
            sidechainEQ[ch].setLowShelf(params.scLowFreq.load(std::memory_order_relaxed), params.scLowGain.load(std::memory_order_relaxed));
            sidechainEQ[ch].setHighShelf(params.scHighFreq.load(std::memory_order_relaxed), params.scHighGain.load(std::memory_order_relaxed));
        }
        MultiCompSidechainEQ::processPair(sidechainEQ[0], sidechainEQ[1], left, right, nSamples);
    }
    else
    for (int ch = 0; ch < nCh; ++ch)
    {
        const float* source = useExternalSidechain ? sidechain[ch] : in[ch];
        sidechainFilters[ch].setFrequency(sidechainHP, mode == MultiCompMode::Bus ? 0.5f : 0.707f);
        sidechainEQ[ch].setLowShelf(params.scLowFreq.load(std::memory_order_relaxed), params.scLowGain.load(std::memory_order_relaxed));
        sidechainEQ[ch].setHighShelf(params.scHighFreq.load(std::memory_order_relaxed), params.scHighGain.load(std::memory_order_relaxed));
        for (int i = 0; i < nSamples; ++i)
        {
            float sample = source[i];
            if (vcaSidechainTilt)
                sample = vcaDetectorCoupling[ch].process(sample, vcaLaw::kDetectorCouplingStrength);
            if (vcaSidechainTilt && vcaLaw::kScResponseStrength != 0.0f)
                sample = vcaScResponse[ch].process(sample, sidechainHP >= 1.0f);
            else if (sidechainHP >= 1.0f)
                sample = vcaSidechainTilt ? sidechainTilt[ch].process(sample)
                                          : sidechainFilters[ch].process(sample);
            processedSidechain[ch][static_cast<size_t>(i)] = sidechainEQ[ch].process(sample);
        }
    }
    // Multiband splits its key into bands after this point. On its internal key True Peak's copysign(peak, x)
    // waveform would put the high band's peaks into every lower band's detector (cymbals pumping the bass), so
    // that key stays as filtered, as the band audio was; an external key keeps it, as the JUCE version does.
    const bool useTruePeak = params.truePeakEnable.load(std::memory_order_relaxed)
        && (mode != MultiCompMode::Multiband || useExternalSidechain);
    truePeakDetector.setQuality(params.truePeakQuality.load(std::memory_order_relaxed) == 1
                                    ? MultiCompTruePeakDetector::Quality::High8x
                                    : MultiCompTruePeakDetector::Quality::Standard4x);
    if (useTruePeak)
    {
        for (int ch = 0; ch < nCh; ++ch)
            for (int i = 0; i < nSamples; ++i)
            {
                const float sample = processedSidechain[ch][static_cast<size_t>(i)];
                processedSidechain[ch][static_cast<size_t>(i)] =
                    std::copysign(truePeakDetector.processSample(sample, ch), sample);
            }
    }
    if (nCh == 1) filteredSidechain[1] = filteredSidechain[0];
    // Capture the latency-aligned dry, but never emit it here: `out` may alias
    // `in`, and every stage below still has to read the untouched input. The
    // settled-bypass output is written from `bypassDry` at the end instead.
    processLatencyHistory(in, nCh, nSamples, blockLatency + hostPadding);
    const float* processingIn[kMaxChannels] = {in[0], nCh > 1 ? in[1] : in[0]};
    const int globalLookaheadDelay = lookaheadSamples(globalLookaheadMs);
    prepareLookahead(in, processingIn, nCh, nSamples, globalLookaheadDelay);
    const int digitalDryDelay = mode == MultiCompMode::Digital ? lookaheadSamples(digitalLookaheadMs) : 0;
    // Opto's output stage runs 2x oversampled with a constant extra latency.
    const int optoStageDelay = mode == MultiCompMode::Opto ? optoStageLatencyHost(sampleRate) : 0;
    const int dryDelay = (mode == MultiCompMode::Multiband ? 0
        : antiAliasLatency + digitalDryDelay + optoStageDelay) + hostPadding;
    for (int ch = 0; ch < nCh; ++ch)
    {
        auto& line = dryPathDelay[static_cast<size_t>(ch)];
        const int size = static_cast<int>(line.size());
        delayLineBlock(line.data(), size, dryPathWrite[static_cast<size_t>(ch)], std::min(dryDelay, size - 1),
                       processingIn[ch], dry.data() + static_cast<size_t>(ch * maxBlock), nSamples);
    }
    // Multiband replaces this dry with its split-band sum and pads that after
    // the M/S decode below; the other modes feed the same input to its pad,
    // so entering Multiband reads the samples that came before, not silence.
    const bool isMulti = mode == MultiCompMode::Multiband;
    if (hostLatencyPadding && !isMulti)
        multibandDryPad.feed(processingIn, nCh, nSamples);
    // VCA coupling histories belong to physical L/R channels. Keep them
    // ahead of the optional M/S transform so changing the routing cannot
    // reinterpret stored low-frequency state. The dry copy above is unfiltered.
    const bool coupleVcaAudio = mode == MultiCompMode::VCA
        && vcaLaw::kAudioCouplingStrength != 0.0f;
    if (coupleVcaAudio)
    {
        for (int i = 0; i < nSamples; ++i)
        {
            const float left = vcaAudioCoupling[0].process(
                processingIn[0][i], vcaLaw::kAudioCouplingStrength);
            const float right = nCh > 1 ? vcaAudioCoupling[1].process(
                processingIn[1][i], vcaLaw::kAudioCouplingStrength) : left;
            modeInput[0][static_cast<size_t>(i)] = nCh > 1 && linkMode == 1
                ? (left + right) * 0.5f : left;
            modeInput[1][static_cast<size_t>(i)] = nCh > 1 && linkMode == 1
                ? (left - right) * 0.5f : right;
        }
        processingIn[0] = modeInput[0].data();
        processingIn[1] = modeInput[1].data();
    }
    if (!coupleVcaAudio && nCh > 1 && linkMode == 1)
    {
        for (int i = 0; i < nSamples; ++i)
        {
            modeInput[0][static_cast<size_t>(i)] = (processingIn[0][i] + processingIn[1][i]) * 0.5f;
            modeInput[1][static_cast<size_t>(i)] = (processingIn[0][i] - processingIn[1][i]) * 0.5f;
        }
        processingIn[0] = modeInput[0].data();
        processingIn[1] = modeInput[1].data();
    }
    const int modeIndex = static_cast<int>(mode);
    if (modeIndex != lastMode)
    {
        if (lastMode >= 0)
        {
            autoGainHoldSamples = std::max(1, static_cast<int>(sampleRate * kAutoGainTransitionMs * 0.001));
            // The entered mode starts from rest (MultiCompModes::clearModeState).
            modes.clearModeState(mode);
            if (mode == MultiCompMode::Multiband)
            {
                multibandEnvelopes.fill(1.0f);
                resetCrossovers();
            }
            // Multiband runs none of the anti-alias stages, the detector feeds or the host sidechain alignment: they
            // still hold the audio from before it, which the entered mode must not start from.
            if (lastMode == static_cast<int>(MultiCompMode::Multiband))
            {
                for (auto& os : oversamplers) os.reset();
                optoLinkedDetectorUpsampler.reset();
                for (auto& upsampler : optoOwnDetectorUpsamplers) upsampler.reset();
                for (auto& upsampler : busExternalOversamplers) upsampler.reset();
                for (auto& line : hostSidechainHistory) line.fill(0.0f);
            }
        }
        lastMode = modeIndex;
        autoGainMatcher.reset();
        resetAutoGainMeasurement();
        autoGainSmoother.setTarget(1.0f);
    }
    if (useExternalSidechain != lastExternalSidechain)
    {
        lastExternalSidechain = useExternalSidechain;
        if (!firstBlock)
            autoGainHoldSamples = std::max(1, static_cast<int>(sampleRate * kAutoGainTransitionMs * 0.001));
        autoGainMatcher.reset();
        resetAutoGainMeasurement();
        autoGainSmoother.setTarget(1.0f);
    }
    const bool autoMakeup = params.autoMakeup.load(std::memory_order_relaxed) && !useExternalSidechain;
    if (autoMakeup != lastAutoMakeup)
    {
        lastAutoMakeup = autoMakeup;
        if (!firstBlock)
            autoGainHoldSamples = std::max(1, static_cast<int>(sampleRate * kAutoGainTransitionMs * 0.001));
        autoGainMatcher.reset();
        resetAutoGainMeasurement();
        autoGainSmoother.setTarget(1.0f);
    }
    processRange(processingIn, filteredSidechain, out, nCh, nSamples,
                 useExternalSidechain, autoMakeup, mode, linkMode,
                 actualOversampling, digitalLookaheadMs);
    if (nCh > 1 && linkMode == 1)
    {
        for (int i = 0; i < nSamples; ++i)
        {
            const float mid = out[0][i], side = out[1][i];
            out[0][i] = mid + side;
            out[1][i] = mid - side;
            if (isMulti)
            {
                // Keep the crossover-rotated split sum, but put it in the same
                // L/R domain as the decoded wet path before mixing/matching.
                const size_t leftIndex = static_cast<size_t>(i);
                const size_t rightIndex = static_cast<size_t>(maxBlock + i);
                const float dryMid = dry[leftIndex], drySide = dry[rightIndex];
                dry[leftIndex] = dryMid + drySide;
                dry[rightIndex] = dryMid - drySide;
            }
        }
    }
    if (hostLatencyPadding && isMulti)
    {
        // Decoded first: the pad's history stays in the L/R domain it is fed in.
        float* dryChannels[kMaxChannels] = {dry.data(), dry.data() + maxBlock};
        multibandDryPad.process(dryChannels, nCh, nSamples, hostPadding);
    }
    if (mode == MultiCompMode::FET)
    {
        // The installed unit's first reconstructed cycle has a short output-
        // stage memory that depends on source peak, Attack, and Ratio. Apply
        // its measured surface here, after AA reconstruction: the current raw
        // input then leads the delayed output by `antiAliasLatency`, matching
        // the reference signal path without adding lookahead or changing PDC.
        // The correction is gone 0.5 ms after the aligned output onset. The
        // higher-carrier gate resolves and separately constrains the small
        // overlap with its first attack-shape samples.
        const float attackPosition = fetAttackPositionFromMilliseconds(
            params.fetAttack.load(std::memory_order_relaxed));
        const int ratioIndex = std::clamp(
            params.fetRatio.load(std::memory_order_relaxed), 0, 4);
        const float outputPosition = std::clamp(
            (params.fetOutput.load(std::memory_order_relaxed) + 20.0f) / 40.0f,
            0.0f, 1.0f);
        const float outputControlBlend = std::clamp(
            (0.90f - outputPosition) / (0.90f - 0.625915527f),
            0.0f, 1.0f);
        const float activeThreshold = decibelsToGain(-60.0f);
        const int silenceResetSamples = std::max(
            1, static_cast<int>(std::lround(0.002 * sampleRate)));
        const int fullCorrectionSamples = antiAliasLatency
            + std::max(1, static_cast<int>(std::lround(0.00025 * sampleRate)));
        const int correctionEndSamples = antiAliasLatency
            + std::max(2, static_cast<int>(std::lround(0.00050 * sampleRate)));
        const float peakRelease = std::exp(
            -1.0f / static_cast<float>(0.010 * sampleRate));
        constexpr float startupCeilingSlope = 0.10f;
        for (int ch = 0; ch < nCh; ++ch)
        {
            int i = 0;
            // Past the correction window the blend is 0 and stays 0 until a silence long enough to re-arm it; the
            // input peak is only read inside the window, and stays positive here until that reset zeroes it. So
            // only the silence count runs, without the peak's decay (exactly what reaches the output), until the
            // reset hands over to the full loop.
            if (fetStartupActiveSamples[static_cast<size_t>(ch)] >= correctionEndSamples)
            {
                const float* input = fetStartupInput.data() + static_cast<size_t>(ch * maxBlock);
                int& silent = fetStartupSilentSamples[static_cast<size_t>(ch)];
                for (; i < nSamples; ++i)
                {
                    if (std::abs(input[i]) > activeThreshold)
                        silent = 0;
                    else
                    {
                        silent = std::min(silent + 1, silenceResetSamples + 1);
                        if (silent >= silenceResetSamples)
                        {
                            fetStartupInputPeak[static_cast<size_t>(ch)] = 0.0f;
                            fetStartupActiveSamples[static_cast<size_t>(ch)] = 0;
                            ++i;
                            break;
                        }
                    }
                }
            }
            for (; i < nSamples; ++i)
            {
                const float inputMagnitude = std::abs(fetStartupInput[
                    static_cast<size_t>(ch * maxBlock + i)]);
                if (inputMagnitude > activeThreshold)
                {
                    if (fetStartupInputPeak[static_cast<size_t>(ch)] <= 0.0f
                        || fetStartupSilentSamples[static_cast<size_t>(ch)]
                            >= silenceResetSamples)
                    {
                        fetStartupInputPeak[static_cast<size_t>(ch)]
                            = inputMagnitude;
                        fetStartupActiveSamples[static_cast<size_t>(ch)] = 0;
                    }
                    else
                        fetStartupInputPeak[static_cast<size_t>(ch)] = std::max(
                            inputMagnitude,
                            fetStartupInputPeak[static_cast<size_t>(ch)]
                                * peakRelease);
                    fetStartupSilentSamples[static_cast<size_t>(ch)] = 0;
                }
                else if (fetStartupInputPeak[static_cast<size_t>(ch)] > 0.0f)
                {
                    fetStartupInputPeak[static_cast<size_t>(ch)] *= peakRelease;
                    fetStartupSilentSamples[static_cast<size_t>(ch)] = std::min(
                        fetStartupSilentSamples[static_cast<size_t>(ch)] + 1,
                        silenceResetSamples + 1);
                    if (fetStartupSilentSamples[static_cast<size_t>(ch)]
                        >= silenceResetSamples)
                    {
                        fetStartupInputPeak[static_cast<size_t>(ch)] = 0.0f;
                        fetStartupActiveSamples[static_cast<size_t>(ch)] = 0;
                    }
                }

                float startupBlend = 0.0f;
                if (fetStartupInputPeak[static_cast<size_t>(ch)] > 0.0f)
                {
                    // Saturate at the end of the correction window: the blend is
                    // already 0 there and stays 0, so counting past it changes
                    // nothing audible and only risks int overflow on a long
                    // sustained input.
                    int& activeSamples
                        = fetStartupActiveSamples[static_cast<size_t>(ch)];
                    startupBlend = advanceFetStartupBlend(
                        activeSamples, fullCorrectionSamples,
                        correctionEndSamples);
                }
                const float magnitude = std::abs(out[ch][i]);
                if (startupBlend > 0.0f && outputControlBlend > 0.0f
                    && magnitude > 0.0f)
                {
                    const float sourcePeakDbfs = gainToDecibels(std::max(
                        fetStartupInputPeak[static_cast<size_t>(ch)], 1.0e-12f));
                    const float startupCeiling = fetStartupPeakTarget(
                        sourcePeakDbfs, attackPosition, ratioIndex);
                    // Inert below the target -- that is the property the old
                    // time-windowed gain lacked. Only samples ABOVE the
                    // installed unit's measured first-cycle peak are touched,
                    // so the estimator window is left alone the instant the
                    // envelope has decayed past it.
                    if (magnitude > startupCeiling)
                    {
                        const float limited = startupCeiling
                            + (magnitude - startupCeiling)
                                * startupCeilingSlope;
                        out[ch][i] += outputControlBlend * startupBlend
                            * (std::copysign(limited, out[ch][i]) - out[ch][i]);
                    }
                }
            }
        }

        // The reference's base-rate output stage compresses only the largest
        // reconstructed startup peaks. Its steady ceiling sweep stays below
        // this knee, so static gain and harmonic measurements remain untouched.
        // Runs after the mid/side decode above: the hardware limits the
        // physical left/right outputs, so the ceiling must see decoded L/R
        // samples in both link modes (identical result for mono and for pure
        // mid or pure side content; differs only for mixed stereo material).
        constexpr float outputCeilingKnee = 6.39712761f;
        constexpr float outputCeilingSlope = 0.46720009f;
        for (int ch = 0; ch < nCh; ++ch)
            if (blockAbsPeak(out[ch], nSamples, 0.0f) > outputCeilingKnee)   // else no sample is touched
            for (int i = 0; i < nSamples; ++i)
            {
                const float magnitude = std::abs(out[ch][i]);
                if (magnitude > outputCeilingKnee)
                    out[ch][i] = std::copysign(
                        outputCeilingKnee
                            + (magnitude - outputCeilingKnee)
                                * outputCeilingSlope,
                        out[ch][i]);
            }
    }
    // The mode-switch duck shapes the wet path only, ahead of Mix (switchDuck*).
    if (advanceSwitchDuck(nSamples))
        for (int ch = 0; ch < nCh; ++ch)
            for (int i = 0; i < nSamples; ++i) out[ch][i] *= switchDuckCurve[static_cast<size_t>(i)];
    padWetPath(out, nCh, nSamples);
    const float targetMix = globalMixTarget(mode);
    globalMixSmoother.setTarget(targetMix);
    // The processed output carries the full reported latency (AA plus both
    // lookaheads). Keep the monitor history running even while Listen is off so
    // either direction of the crossfade starts between time-aligned signals.
    const int listenDelay = blockLatency + hostPadding;
    processSidechainListenHistory(filteredSidechain, nCh, nSamples, listenDelay);
    // The gain stages after the mode (Mix, auto makeup, noise, BUS fade,
    // Listen, the bypass crossfade) run as one pass per channel. Their ramps
    // advance once per sample, so their curves come first; each output sample
    // then takes the stages in the original order with the original arithmetic.
    fillRampCurve(globalMixSmoother, mixCurve.data(), nSamples);
    const auto applyMix = [&](int ch) noexcept {
        const float* dryChannel = dry.data() + static_cast<size_t>(ch * maxBlock);
        for (int i = 0; i < nSamples; ++i)
        {
            const float wet = mixCurve[static_cast<size_t>(i)];
            if (!isMulti || wet < 1.0f)
                out[ch][i] = out[ch][i] * wet + dryChannel[i] * (1.0f - wet);
        }
    };

    const bool measureAutoGain = autoMakeup && !requestedSidechainListen;
    if (!measureAutoGain || autoGainHoldSamples > 0)
    {
        autoGainMatcher.reset();
        resetAutoGainMeasurement();
        autoGainSmoother.setTarget(1.0f);
    }
    // The measurement reads the mixed output, so Mix goes first when it runs.
    const bool mixFirst = measureAutoGain;
    if (mixFirst)
        for (int ch = 0; ch < nCh; ++ch) applyMix(ch);
    if (!measureAutoGain)
    {
        // Without the measurement each sample only advances the smoother and the hold count.
        fillRampCurve(autoGainSmoother, autoGainCurve.data(), nSamples);
        autoGainHoldSamples = std::max(0, autoGainHoldSamples - nSamples);
    }
    else
    for (int i = 0; i < nSamples; ++i)
    {
        if (measureAutoGain)
        {
            if (autoGainHoldSamples <= 0)
            {
                double inPower = 0.0, outPower = 0.0;
                for (int ch = 0; ch < nCh; ++ch)
                {
                    const float drySample = dry[static_cast<size_t>(ch * maxBlock + i)];
                    inPower += static_cast<double>(drySample) * drySample;
                    outPower += static_cast<double>(out[ch][i]) * out[ch][i];
                }
                const double channelDivisor = static_cast<double>(std::max(1, nCh));
                // Carry one fixed measurement window across host calls. Target
                // cadence then follows sample position, not buffer boundaries.
                autoGainInputPower += inPower / channelDivisor;
                autoGainOutputPower += outPower / channelDivisor;
                if (++autoGainMeasurementCount == kAutoGainMeasurementWindow)
                {
                    const float sampleDivisor = static_cast<float>(autoGainMeasurementCount);
                    const float target = autoGainMatcher.update(
                        std::sqrt(static_cast<float>(autoGainInputPower) / sampleDivisor),
                        std::sqrt(static_cast<float>(autoGainOutputPower) / sampleDivisor),
                        autoGainMeasurementCount);
                    autoGainSmoother.setTarget(target);
                    resetAutoGainMeasurement();
                }
            }
        }
        autoGainCurve[static_cast<size_t>(i)] = autoGainSmoother.next();
        if (autoGainHoldSamples > 0) --autoGainHoldSamples;
    }
    const bool addNoise = params.noiseEnable.load(std::memory_order_relaxed)
        && mode != MultiCompMode::Digital && mode != MultiCompMode::Multiband;
    // The fade shapes only the wet path, so it runs before the Listen crossfade.
    // Listen holds the fade where it is but keeps applying the current gain, so
    // entering or leaving Listen never jumps a partly faded bus.
    const bool busFade = mode == MultiCompMode::Bus;
    if (busFade)
    {
        const bool fadeOut = params.busFade.load(std::memory_order_relaxed);
        const double seconds = std::clamp(static_cast<double>(params.busFadeRate.load(std::memory_order_relaxed)), 1.0, 60.0);
        // Fade travel pauses whenever the faded wet path is not what the user
        // is hearing: Listen, leaving BUS, and a settled bypass all hold the
        // position and keep applying the gain it reached. Unlike a detector
        // history this is a user-facing control, so it must not run on while
        // the plugin is out of circuit (MultiCompBusControlTests.cpp:308-314).
        const float step = requestedSidechainListen || settledBypass ? 0.0f
            : (fadeOut ? 1.0f : -1.0f) * busLaw::fadeStep(seconds, sampleRate);
        for (int i = 0; i < nSamples; ++i)
        {
            busFadeControl = std::clamp(busFadeControl + step, 0.0f, busLaw::fadeControlMaximum);
            fadeCurve[static_cast<size_t>(i)] = busLaw::fadeGain(busFadeControl);
        }
    }
    busFadeMeter.store(busFadeControl / busLaw::fadeControlMaximum, std::memory_order_relaxed);
    fillRampCurve(sidechainListenRamp, listenCurve.data(), nSamples);
    if (requestedSidechainListen)
    {
        busMeterReading.store(0.0f, std::memory_order_relaxed);
        masterGR.store(0.0f, std::memory_order_relaxed);
        for (auto& meter : bandGR) meter.store(0.0f, std::memory_order_relaxed);
    }
    // Bypass is the final gain-stage transition, so its 100% endpoint is the
    // same latency-aligned dry sample the settled branch below copies out.
    fillRampCurve(bypassRamp, bypassCurve.data(), nSamples);
    // A settled bypass copies the dry out afterwards instead of crossfading.
    const bool crossfadeBypass = !settledBypass && !bypassSettled;
    for (int ch = 0; ch < nCh; ++ch)
    {
        if (!mixFirst) applyMix(ch);
        float* channel = out[ch];
        for (int i = 0; i < nSamples; ++i) channel[i] *= autoGainCurve[static_cast<size_t>(i)];
        // The generator advances channel by channel, then sample by sample.
        if (addNoise)
            for (int i = 0; i < nSamples; ++i)
            {
                noiseState = noiseState * 1664525u + 1013904223u;
                const float unit = static_cast<float>((noiseState >> 8) & 0x00ffffffu) / 16777215.0f;
                channel[i] += (unit * 2.0f - 1.0f) * 0.0001f;
            }
        if (busFade)
            for (int i = 0; i < nSamples; ++i) channel[i] *= fadeCurve[static_cast<size_t>(i)];
        const float* listenSource = filteredSidechain[ch];
        const float* bypassChannel = bypassDry.data() + static_cast<size_t>(ch * maxBlock);
        for (int i = 0; i < nSamples; ++i)
        {
            const float listen = listenCurve[static_cast<size_t>(i)];
            float sample = channel[i] * (1.0f - listen) + listenSource[i] * listen;
            if (crossfadeBypass)
            {
                const float bypass = bypassCurve[static_cast<size_t>(i)];
                sample = sample * (1.0f - bypass) + bypassChannel[i] * bypass;
            }
            channel[i] = sample;
        }
    }
    // A settled bypass is a copy, not a crossfade against a curve that happens
    // to be exactly 1: multiplying would turn an overflowed processed sample
    // into NaN (inf * 0) and put it on the output the user asked to pass
    // through untouched. Scan the processed block first so the recovery guard
    // below still sees a fault the copy is about to hide.
    bool processedIsFinite = true;
    if (settledBypass)
    {
        for (int ch = 0; ch < nCh && processedIsFinite; ++ch)
            processedIsFinite = blockIsAllFinite(out[ch], nSamples);
        for (int ch = 0; ch < nCh; ++ch)
            std::copy_n(bypassDry.data() + static_cast<size_t>(ch * maxBlock), nSamples, out[ch]);
        // Gain reduction reads zero while bypassed, exactly as it did when this
        // path returned early. The BUS needle is not forced here: processRange
        // fed it zeros for this block, so it keeps gliding down rather than
        // snapping.
        masterGR.store(0.0f, std::memory_order_relaxed);
        for (auto& meter : bandGR) meter.store(0.0f, std::memory_order_relaxed);
        // Bypass is NOT a lifecycle boundary for the FET first-cycle helpers:
        // the chain keeps running while bypassed, so the startup peak/counters
        // and the post-burst recovery stay live like every other mode's state
        // and an un-bypass resumes warm instead of re-arming the first cycle.
    }
    if (requestedBypass && bypassRamp.value() >= 1.0f) bypassSettled = true;
    // Recovery. Before the input ceiling, finite input could overflow inside a
    // stage (measured with a 10 ms burst: the generic Studio FET that was mode 4
    // until 2026-10-27 from about 3e3 peak, Studio VCA from 1e13, FET from 1e20,
    // the other modes only near FLT_MAX). Input at or above 1024 no longer gets
    // this far, and since FET BLUE replaced Studio FET no sub-ceiling burst with
    // mode input gain on top does either (Studio FET's 1000 peak at +20 dB Input
    // peaks at 1.45 in FET BLUE); a fault that arises inside a stage still
    // would. Once one reaches the output it is latched in the same histories the
    // input guard protects.
    // Emit silence for the faulted block and clear every history exactly as a
    // host reset() would, so the next block starts from a defined state instead
    // of carrying NaN for ever. This sees only faults that reach the output: an
    // overflow a detector clamps back to a finite gain is not caught here.
    // Inert on finite output: nothing runs unless a sample is already
    // non-finite. reset() leaves firstBlock set, so return before it is
    // cleared; the next block then behaves as the first after a host reset.
    bool outputIsFinite = processedIsFinite;
    for (int ch = 0; ch < nCh && outputIsFinite; ++ch)
        outputIsFinite = blockIsAllFinite(out[ch], nSamples);
    if (!outputIsFinite)
    {
        // Behind a settled bypass `out` already holds the finite dry, and the
        // dry is what bypass promises: clear the faulted histories but keep
        // passing it rather than punching a hole in the signal.
        if (!settledBypass)
            for (int ch = 0; ch < nCh; ++ch)
                std::fill_n(out[ch], nSamples, 0.0f);
        reset();
        updateMeters(settledBypass ? blockInputPeak : 0.0f, out, nCh, nSamples);
        return;
    }
    updateMeters(blockInputPeak, out, nCh, nSamples);
    firstBlock = false;
}

// Host latency padding of the wet path, ahead of Mix: lastMode is the mode this
// part ran in; a ducked switch's held padding (holdPaddingThroughSwitch) runs
// first. The dry, bypass and listen paths reach the host latency on their own.
void MultiCompDSP::padWetPath(float* const* out, int nCh, int nSamples) noexcept
{
    if (!hostLatencyPadding) return;
    int done = 0;
    if (paddingHoldSamples > 0)
    {
        done = std::min(nSamples, paddingHoldSamples);
        latencyPad.process(out, nCh, done, paddingForMode(paddingHoldMode));
        paddingHoldSamples -= done;
    }
    if (done == nSamples) return;
    float* rest[kMaxChannels] = {out[0] + done, nCh > 1 ? out[1] + done : out[0] + done};
    latencyPad.process(rest, nCh, nSamples - done, getLatencyPaddingSamples());
}

// The padded output lags the DSP's by the old mode's padding, so at a ducked
// switch it still plays the last of the fade-out: changing the padding there
// would skip or replay those samples. The old padding stays until the deepest
// padding has passed, inside the switch's mute, where both delays read
// silence. Another switch in the meantime extends it from the same mode.
void MultiCompDSP::holdPaddingThroughSwitch(int previousMode) noexcept
{
    if (paddingHoldSamples == 0) paddingHoldMode = previousMode;
    paddingHoldSamples = getMaximumLatencyPaddingSamples();
}

int MultiCompDSP::paddingForMode(int mode) const noexcept
{
    return getMaximumLatencyPaddingSamples() - baseLatencyForMode(static_cast<MultiCompMode>(std::clamp(mode, 0, 7)));
}

void MultiCompDSP::processLatencyHistory(const float* const* in, int nCh,
                                         int nSamples, int delay) noexcept
{
    delay = std::clamp(delay, 0, static_cast<int>(bypassDelay[0].size()) - 1);
    const int size = static_cast<int>(bypassDelay[0].size());
    // One write position for both channels.
    int write = bypassWrite;
    for (int ch = 0; ch < nCh; ++ch)
    {
        write = bypassWrite;
        delayLineBlock(bypassDelay[static_cast<size_t>(ch)].data(), size, write, delay, in[ch],
                       bypassDry.data() + static_cast<size_t>(ch * maxBlock), nSamples);
    }
    bypassWrite = write;
}

void MultiCompDSP::processSidechainListenHistory(const float* const* sidechain,
                                                 int nCh, int nSamples,
                                                 int delay) noexcept
{
    for (int ch = 0; ch < nCh; ++ch)
    {
        auto& line = sidechainListenDelay[static_cast<size_t>(ch)];
        const int size = static_cast<int>(line.size());
        // The source is processedSidechain itself: delay out of a copy.
        std::copy_n(sidechain[ch], nSamples, listenSource.data());
        delayLineBlock(line.data(), size, sidechainListenWrite[static_cast<size_t>(ch)],
                       std::clamp(delay, 0, size - 1), listenSource.data(),
                       processedSidechain[static_cast<size_t>(ch)].data(), nSamples);
    }
}

float MultiCompDSP::delayHostSidechain(int channel, float value, int delay) noexcept
{
    auto& line = hostSidechainHistory[static_cast<size_t>(channel)];
    line[static_cast<size_t>(hostSidechainWrite)] = value;
    if (delay <= 0) return value;
    const int read = wrapRingIndex(hostSidechainWrite - delay, kHostSidechainRing);
    return line[static_cast<size_t>(read)];
}

MultiCompMode MultiCompDSP::currentMode() const noexcept
{
    return static_cast<MultiCompMode>(std::clamp(params.mode.load(std::memory_order_relaxed), 0, 7));
}

// Whether a switch between two modes changes the audio path ahead of the output: the anti-alias oversampler's
// factor at the current setting and rate, or whether Opto's output stage is in the path (it adds latency at every
// rate, 6 host samples at 44.1 / 48 kHz and 12 at 88.2 / 96 kHz, which the other modes take as wet-path padding).
// An instant switch swaps a running stage for a pad line and plays as a step: before 2026-10-26 VCA / FET <-> Opto
// stepped 6.6-7x the steady slope at 44.1 / 48 kHz and 12-29x at 88.2-176.4 kHz. Multiband never runs the
// oversampler, so its switches stay instant.
bool MultiCompDSP::switchChangesAudioPath(int fromMode, int toMode) const noexcept
{
    const auto multiband = static_cast<int>(MultiCompMode::Multiband);
    if (fromMode == multiband || toMode == multiband) return false;
    const int setting = oversamplingFactorForSetting(params.oversampling.load(std::memory_order_relaxed));
    if (audioFactorForMode(static_cast<MultiCompMode>(fromMode), setting, sampleRate)
        != audioFactorForMode(static_cast<MultiCompMode>(toMode), setting, sampleRate))
        return true;
    const auto opto = static_cast<int>(MultiCompMode::Opto);
    return (fromMode == opto) != (toMode == opto);
}

void MultiCompDSP::resetSwitchDuck() noexcept
{
    paddingHoldMode = -1;
    paddingHoldSamples = 0;
    switchDuckHeldMode = -1;
    switchDuckMuted = 0;
    switchDuckPosition = switchDuckFadeSamples;
}

// The block's wet gain into switchDuckCurve, advancing the duck: down while the old mode is held, held at 0 while
// muted, then up (a smoothstep of the position: no slope step at either end). False, and the curve untouched,
// while the duck is idle.
bool MultiCompDSP::advanceSwitchDuck(int nSamples) noexcept
{
    if (switchDuckHeldMode < 0 && switchDuckMuted == 0 && switchDuckPosition >= switchDuckFadeSamples) return false;
    const float inverseFade = 1.0f / static_cast<float>(switchDuckFadeSamples);
    for (int i = 0; i < nSamples; ++i)
    {
        if (switchDuckHeldMode >= 0) { if (switchDuckPosition > 0) --switchDuckPosition; }
        else if (switchDuckMuted > 0) --switchDuckMuted;
        else if (switchDuckPosition < switchDuckFadeSamples) ++switchDuckPosition;
        const float x = static_cast<float>(switchDuckPosition) * inverseFade;
        switchDuckCurve[static_cast<size_t>(i)] = x * x * (3.0f - 2.0f * x);
    }
    return true;
}

// Bus and Digital apply the MIX on their own faces inside the mode
// (busMixRamp, digitalMixRamp) and Multiband its MB Mix; the global Mix, which
// those faces do not show, stays out of them.
float MultiCompDSP::globalMixTarget(MultiCompMode mode) const noexcept
{
    const float percent = mode == MultiCompMode::Multiband ? params.mbMix.load(std::memory_order_relaxed)
        : (mode == MultiCompMode::Bus || mode == MultiCompMode::Digital) ? 100.0f
        : params.mix.load(std::memory_order_relaxed);
    return std::clamp(percent * 0.01f, 0.0f, 1.0f);
}

void MultiCompDSP::resetAutoGainMeasurement() noexcept
{
    autoGainInputPower = 0.0;
    autoGainOutputPower = 0.0;
    autoGainMeasurementCount = 0;
}

void MultiCompDSP::prepareLookahead(const float* const* in,
                                    const float* (&processingIn)[kMaxChannels],
                                    int nCh, int nSamples, int delay)
{
    if (globalLookahead[0].empty()) return;

    const int size = static_cast<int>(globalLookahead[0].size());
    const int clampedDelay = std::min(delay, size - 1);
    for (int ch = 0; ch < nCh; ++ch)
    {
        delayLineBlock(globalLookahead[static_cast<size_t>(ch)].data(), size,
                       globalLookaheadWrite[static_cast<size_t>(ch)], clampedDelay, in[ch],
                       delay > 0 ? delayedInput[static_cast<size_t>(ch)].data() : nullptr, nSamples);
        if (delay > 0)
            processingIn[ch] = delayedInput[static_cast<size_t>(ch)].data();
    }
}

void MultiCompDSP::syncModeParameters(MultiCompMode mode, float digitalLookaheadMs) noexcept
{
    switch (mode)
    {
        case MultiCompMode::Opto:
            copyParameter(modeParams.optoPeakReduction, params.optoPeakReduction);
            copyParameter(modeParams.optoGain, params.optoGain);
            copyParameter(modeParams.optoLimit, params.optoLimit);
            break;
        case MultiCompMode::FET:
        case MultiCompMode::FetBlue:
            copyParameter(modeParams.fetInput, params.fetInput);
            copyParameter(modeParams.fetOutput, params.fetOutput);
            copyParameter(modeParams.fetAttack, params.fetAttack);
            copyParameter(modeParams.fetRelease, params.fetRelease);
            copyParameter(modeParams.fetRatio, params.fetRatio);
            copyParameter(modeParams.fetCurve, params.fetCurve);
            copyParameter(modeParams.fetTransient, params.fetTransient);
            copyParameter(modeParams.fetThreshold, params.fetThreshold);
            copyParameter(modeParams.fetBlueRatio, params.fetBlueRatio);
            copyParameter(modeParams.fetBlueScFilter, params.fetBlueScFilter);
            copyParameter(modeParams.fetBlueHeadroom, params.fetBlueHeadroom);
            break;
        case MultiCompMode::VCA:
            copyParameter(modeParams.vcaThreshold, params.vcaThreshold);
            copyParameter(modeParams.vcaRatio, params.vcaRatio);
            copyParameter(modeParams.vcaAttack, params.vcaAttack);
            copyParameter(modeParams.vcaRelease, params.vcaRelease);
            copyParameter(modeParams.vcaOutput, params.vcaOutput);
            copyParameter(modeParams.vcaOverEasy, params.vcaOverEasy);
            copyParameter(modeParams.vcaClassicDetector, params.vcaClassicDetector);
            break;
        case MultiCompMode::Bus:
            copyParameter(modeParams.busHeadroom, params.busHeadroom);
            copyParameter(modeParams.busThreshold, params.busThreshold);
            copyParameter(modeParams.busRatio, params.busRatio);
            copyParameter(modeParams.busAttack, params.busAttack);
            copyParameter(modeParams.busRelease, params.busRelease);
            copyParameter(modeParams.busMakeup, params.busMakeup);
            break;
        case MultiCompMode::StudioVCA:
            copyParameter(modeParams.studioVcaThreshold, params.studioVcaThreshold);
            copyParameter(modeParams.studioVcaRatio, params.studioVcaRatio);
            copyParameter(modeParams.studioVcaAttack, params.studioVcaAttack);
            copyParameter(modeParams.studioVcaRelease, params.studioVcaRelease);
            copyParameter(modeParams.studioVcaOutput, params.studioVcaOutput);
            copyParameter(modeParams.studioVcaInput, params.studioVcaInput);
            copyParameter(modeParams.studioVcaAutoRelease, params.studioVcaAutoRelease);
            break;
        case MultiCompMode::Digital:
            copyParameter(modeParams.digitalThreshold, params.digitalThreshold);
            copyParameter(modeParams.digitalRatio, params.digitalRatio);
            copyParameter(modeParams.digitalKnee, params.digitalKnee);
            copyParameter(modeParams.digitalAttack, params.digitalAttack);
            copyParameter(modeParams.digitalRelease, params.digitalRelease);
            modeParams.digitalLookahead.store(digitalLookaheadMs, std::memory_order_relaxed);
            copyParameter(modeParams.digitalOutput, params.digitalOutput);
            copyParameter(modeParams.digitalAdaptive, params.digitalAdaptive);
            break;
        case MultiCompMode::Multiband:
            break;
    }
}

void MultiCompDSP::processRange(const float* const* in, const float* const* sidechain,
                                float* const* out, int nCh, int nSamples,
                                bool external, bool autoMakeup,
                                MultiCompMode mode, int linkMode, int actualOs,
                                float digitalLookaheadMs)
{
    const float linkAmount = std::clamp(params.stereoLink.load(std::memory_order_relaxed) * 0.01f, 0.0f, 1.0f);
    const auto distortionType = static_cast<DistortionType>(std::clamp(params.distortion.load(std::memory_order_relaxed), 0, 3));
    const float distortionAmount = std::clamp(params.distortionAmount.load(std::memory_order_relaxed) * 0.01f, 0.0f, 1.0f);
    for (auto& os : oversamplers) os.setFactor(actualOs);
    optoLinkedDetectorUpsampler.setFactor(actualOs);
    for (auto& os : optoOwnDetectorUpsamplers) os.setFactor(actualOs);
    for (auto& os : busExternalOversamplers) os.setFactor(actualOs);
    const int hostSidechainDelay = oversamplers[0].hostSidechainAlignmentSamples();
    modes.setRate(sampleRate, actualOs);
    if (mode == MultiCompMode::Bus)
        modes.setBusSidechainControls(
            params.sidechainHP.load(std::memory_order_relaxed),
            params.scLowFreq.load(std::memory_order_relaxed),
            params.scLowGain.load(std::memory_order_relaxed),
            params.scHighFreq.load(std::memory_order_relaxed),
            params.scHighGain.load(std::memory_order_relaxed));
    manualMakeupScaleRamp.setTarget(autoMakeup ? 0.0f : 1.0f);
    if (firstBlock) manualMakeupScaleRamp.snap(autoMakeup ? 0.0f : 1.0f);
    syncModeParameters(mode, digitalLookaheadMs);
    if (mode == MultiCompMode::Multiband)
    {
        previousOversampledSidechainValid = {{false, false}};
        previousOptoOwnSidechainValid = {{false, false}};
        // The detector stays ahead of the delayed audio even with an internal
        // key. The filtered feed is in L/R; Multiband routes it to M/S itself.
        processMultiband(in, sidechain, out, nCh, nSamples, linkMode);
        return;
    }
    float manualOutputDb = 0.0f;
    switch (mode)
    {
        case MultiCompMode::Opto: manualOutputDb = optoKnobToGainDb(params.optoGain.load(std::memory_order_relaxed)); break;
        case MultiCompMode::FET:
        case MultiCompMode::FetBlue: manualOutputDb = params.fetOutput.load(std::memory_order_relaxed); break;
        case MultiCompMode::VCA: manualOutputDb = params.vcaOutput.load(std::memory_order_relaxed); break;
        case MultiCompMode::Bus: manualOutputDb = params.busMakeup.load(std::memory_order_relaxed); break;
        case MultiCompMode::StudioVCA: manualOutputDb = params.studioVcaOutput.load(std::memory_order_relaxed); break;
        case MultiCompMode::Digital: manualOutputDb = params.digitalOutput.load(std::memory_order_relaxed); break;
        case MultiCompMode::Multiband: break;
    }
    busMixRamp.setTarget(std::clamp(params.busMix.load(std::memory_order_relaxed) * 0.01f, 0.0f, 1.0f));
    digitalMixRamp.setTarget(std::clamp(params.digitalMix.load(std::memory_order_relaxed) * 0.01f, 0.0f, 1.0f));
    if (mode == MultiCompMode::VCA)
        processVcaRange(in, sidechain, out, nCh, nSamples, linkAmount, linkMode, hostSidechainDelay,
                        manualOutputDb, distortionType, distortionAmount);
    if (mode == MultiCompMode::Opto)
        processOptoRange(in, sidechain, out, nCh, nSamples, external, linkAmount, linkMode, hostSidechainDelay,
                         manualOutputDb, distortionType, distortionAmount);
    if (mode == MultiCompMode::FET || mode == MultiCompMode::FetBlue)
        processFetRange(mode, in, sidechain, out, nCh, nSamples, external, linkAmount, linkMode, actualOs,
                        hostSidechainDelay, manualOutputDb, distortionType, distortionAmount);
    if (mode == MultiCompMode::StudioVCA)
        processStudioVcaRange(in, sidechain, out, nCh, nSamples, external, linkAmount, linkMode, hostSidechainDelay,
                              manualOutputDb, distortionType, distortionAmount);
    // BUS takes the block path as a linked stereo pair (the default layout).
    const bool busBlockPath = mode == MultiCompMode::Bus && linkMode == 0 && linkAmount > 0.0001f && nCh > 1;
    if (busBlockPath)
        processBusRange(in, sidechain, out, nSamples, external, linkAmount, actualOs, hostSidechainDelay,
                        manualOutputDb, distortionType, distortionAmount);
    const bool blockPath = mode == MultiCompMode::VCA || mode == MultiCompMode::Opto
        || mode == MultiCompMode::FET || mode == MultiCompMode::FetBlue || mode == MultiCompMode::StudioVCA
        || busBlockPath;
    for (int i = 0; i < nSamples && !blockPath; ++i)
    {
        const float scaledOutputDb = manualOutputDb * manualMakeupScaleRamp.next();
        switch (mode)
        {
            case MultiCompMode::Opto: modeParams.optoGain.store(optoGainDbToKnob(scaledOutputDb), std::memory_order_relaxed); break;
            case MultiCompMode::FET:
            case MultiCompMode::FetBlue: modeParams.fetOutput.store(scaledOutputDb, std::memory_order_relaxed); break;
            case MultiCompMode::VCA: modeParams.vcaOutput.store(scaledOutputDb, std::memory_order_relaxed); break;
            case MultiCompMode::Bus: modeParams.busMakeup.store(scaledOutputDb, std::memory_order_relaxed); break;
            case MultiCompMode::StudioVCA: modeParams.studioVcaOutput.store(scaledOutputDb, std::memory_order_relaxed); break;
            case MultiCompMode::Digital: modeParams.digitalOutput.store(scaledOutputDb, std::memory_order_relaxed); break;
            case MultiCompMode::Multiband: break;
        }
        const float localBusMix = mode == MultiCompMode::Bus ? busMixRamp.next() : 1.0f;
        const float localDigitalMix = mode == MultiCompMode::Digital ? digitalMixRamp.next() : 1.0f;
        const float rawSc0 = sidechain != nullptr ? sidechain[0][i] : in[0][i];
        const float rawSc1 = sidechain != nullptr ? sidechain[std::min(1, nCh - 1)][i] : in[std::min(1, nCh - 1)][i];
        // processBlock has already applied the HP and shelf EQ once. Keeping
        // this path as a pure read avoids doubling the detector filters.
        const float sc0 = rawSc0;
        const float sc1 = nCh > 1 ? rawSc1 : sc0;
        const float scLevel = std::max(std::abs(sc0), std::abs(sc1));
        // Native-calibrated host-rate controls (the OPTO detector feeds and the
        // vintage FET link control) read these: delayed by the extra lead of
        // the wide audio FIR so their measured timing against the audio holds.
        // Everything else keeps the undelayed host sample: the BUS external
        // and OPTO linked upsamplers align themselves, the VCA history read
        // carries the FIR delay, and the generic interpolated detectors keep
        // their host-sample step timing at every oversampling setting.
        const float alignedSc0 = delayHostSidechain(0, sc0, hostSidechainDelay);
        const float alignedSc1 = delayHostSidechain(1, sc1, hostSidechainDelay);
        hostSidechainWrite = advanceRingIndex(hostSidechainWrite, kHostSidechainRing);
        const float scSigned = std::abs(alignedSc0) >= std::abs(alignedSc1) ? alignedSc0 : alignedSc1;
        const bool link = linkMode == 0 && linkAmount > 0.0001f && nCh > 1;
        // Only a linked stereo pair blends toward the linked detector; Dual
        // Mono, Mid-Side and mono keep each channel's own feed.
        const float optoLinkWeight = link ? linkAmount : 0.0f;
        // OPTO can link inside the cell instead (kOptoLinkAtDriveDefault): the
        // cell then gets each channel's own detector and the link amount.
        const bool optoCellLink = link && mode == MultiCompMode::Opto && modes.optoLinksAtDrive();
        const float optoBlendWeight = optoCellLink ? 0.0f : optoLinkWeight;
        modes.setOptoLinkWeight(optoCellLink ? optoLinkWeight : 0.0f);
        // Match the audio FIR delay and native detector phase. Linear
        // interpolation loses 10.48 dB of external compression at 4x/20 kHz.
        // Every feed's history advances across linked, unlinked and mono
        // paths; a feed this mode does not read idles (LazyUpsampler).
        const bool readsBusFeed = mode == MultiCompMode::Bus;
        const bool readsOptoOwnFeed = mode == MultiCompMode::Opto && !external;
        const bool readsOptoLinkedFeed = mode == MultiCompMode::Opto && optoBlendWeight > 0.0f;
        std::array<std::array<float, 4>, 2> busExternalPhases{};
        if (readsBusFeed)
        {
            busExternalOversamplers[0].upsampleSample(sc0, busExternalPhases[0].data());
            busExternalOversamplers[1].upsampleSample(sc1, busExternalPhases[1].data());
        }
        else
        {
            busExternalOversamplers[0].idle(sc0);
            busExternalOversamplers[1].idle(sc1);
        }
        std::array<float, 4> optoLinkedPhases{};
        if (readsOptoLinkedFeed)
            optoLinkedDetectorUpsampler.upsampleSample(scSigned, optoLinkedPhases.data());
        else
            optoLinkedDetectorUpsampler.idle(scSigned);
        std::array<std::array<float, 4>, kMaxChannels> optoOwnPhases{};
        // The own feed follows the stereo mode: Mid-Side detects on the
        // encoded sidechain, like the audio it compresses.
        const bool midSideDetector = linkMode == 1 && nCh > 1;
        const float optoOwnFeed0 = midSideDetector ? (alignedSc0 + alignedSc1) * 0.5f : alignedSc0;
        const float optoOwnFeed1 = midSideDetector ? (alignedSc0 - alignedSc1) * 0.5f : alignedSc1;
        if (readsOptoOwnFeed)
        {
            optoOwnDetectorUpsamplers[0].upsampleSample(optoOwnFeed0, optoOwnPhases[0].data());
            optoOwnDetectorUpsamplers[1].upsampleSample(optoOwnFeed1, optoOwnPhases[1].data());
        }
        else
        {
            optoOwnDetectorUpsamplers[0].idle(optoOwnFeed0);
            optoOwnDetectorUpsamplers[1].idle(optoOwnFeed1);
        }
        if (optoCellLink)
        {
            // The cell ORs the two panel drives sample by sample, so the pair
            // advances together: channel 0 then channel 1 at every
            // oversampling phase, each on its own detector.
            std::array<float, 4> inputLeftPhases{}, inputRightPhases{};
            std::array<float, 4> outputLeftPhases{}, outputRightPhases{};
            oversamplers[0].upsampleSample(in[0][i], inputLeftPhases.data());
            oversamplers[1].upsampleSample(in[1][i], inputRightPhases.data());
            if (!previousOptoOwnSidechainValid[0] || !previousOptoOwnSidechainValid[1])
            {
                previousOptoOwnSidechain = {{sc0, sc1}};
                previousOptoOwnSidechainValid = {{true, true}};
            }
            for (int phase = 0; phase < actualOs; ++phase)
            {
                const size_t p = static_cast<size_t>(phase);
                const float leftDetector = !external ? optoOwnPhases[0][p]
                    : actualOs == 1 ? sc0 : interpolateOversampledSidechain(
                        previousOptoOwnSidechain[0], sc0, phase, actualOs);
                const float rightDetector = !external ? optoOwnPhases[1][p]
                    : actualOs == 1 ? sc1 : interpolateOversampledSidechain(
                        previousOptoOwnSidechain[1], sc1, phase, actualOs);
                const auto pair = modes.processOptoLightPair(
                    {{inputLeftPhases[p], inputRightPhases[p]}}, {{sc0, sc1}},
                    modeParams, external, {{leftDetector, rightDetector}});
                outputLeftPhases[p] = applyCoreDistortion(pair[0], distortionType, distortionAmount);
                outputRightPhases[p] = applyCoreDistortion(pair[1], distortionType, distortionAmount);
            }
            out[0][i] = oversamplers[0].downsampleSample(outputLeftPhases.data());
            out[1][i] = oversamplers[1].downsampleSample(outputRightPhases.data());
            previousOversampledSidechain = {{
                std::abs(sc0) * (1.0f - linkAmount) + scLevel * linkAmount,
                std::abs(sc1) * (1.0f - linkAmount) + scLevel * linkAmount}};
            previousOversampledSidechainValid = {{true, true}};
            previousOptoOwnSidechain = {{sc0, sc1}};
            continue;
        }
        for (int ch = 0; ch < nCh; ++ch)
        {
            const float input = in[ch][i];
            const float ownSc = ch == 0 ? sc0 : sc1;
            const float midSideSc = ch == 0 ? (sc0 + sc1) * 0.5f : (sc0 - sc1) * 0.5f;
            const float sc = link
                ? std::abs(ownSc) * (1.0f - linkAmount)
                    + scLevel * linkAmount
                : linkMode == 1 ? midSideSc : ownSc;
            // Interpolate causally from the previous native detector sample to
            // this one.  The carried endpoint makes the same detector curve
            // independent of where the host divides blocks.
            const size_t channelIndex = static_cast<size_t>(ch);
            if (!previousOversampledSidechainValid[channelIndex])
            {
                previousOversampledSidechain[channelIndex] = sc;
                previousOversampledSidechainValid[channelIndex] = true;
            }
            const float previousSc = previousOversampledSidechain[channelIndex];
            if (!previousOptoOwnSidechainValid[channelIndex])
            {
                previousOptoOwnSidechain[channelIndex] = ownSc;
                previousOptoOwnSidechainValid[channelIndex] = true;
            }
            const float previousOptoOwnSc = previousOptoOwnSidechain[channelIndex];
            const float localMix = mode == MultiCompMode::Digital ? localDigitalMix : localBusMix;
            // Internal OPTO always takes the explicit detector: the classic-FIR
            // own feed (or its linked blend), never the wide-FIR audio sample.
            // External keeps its interpolated sidechain path unchanged.
            const bool useOptoDetector = link || (mode == MultiCompMode::Opto && !external);
            if (actualOs == 1)
            {
                out[ch][i] = oversamplers[ch].processSample(input, [&](float sample) noexcept {
                    const float optoOwnDetector = external ? ownSc : optoOwnPhases[channelIndex][0];
                    const float optoDetector = optoOwnDetector
                        + (optoLinkedPhases[0] - optoOwnDetector) * optoBlendWeight;
                    return applyCoreDistortion(modes.process(
                                                   mode, sample, ch, sc, modeParams,
                                                   localMix, external, optoDetector,
                                                   useOptoDetector),
                                               distortionType, distortionAmount);
                });
            }
            else
            {
                int osPhase = 0;
                out[ch][i] = oversamplers[ch].processSample(input, [&](float sample) noexcept {
                    const int phase = osPhase++;
                    const float osSc = mode == MultiCompMode::Bus
                        ? (linkMode == 1 ? (busExternalPhases[0][phase] + (ch == 0 ? 1 : -1) * busExternalPhases[1][phase]) * 0.5f : busExternalPhases[ch][phase])
                        : mode == MultiCompMode::VCA ? sc : interpolateOversampledSidechain(
                        previousSc, sc, phase, actualOs);
                    const float optoOwnDetector = external
                        ? interpolateOversampledSidechain(
                              previousOptoOwnSc, ownSc, phase, actualOs)
                        : optoOwnPhases[channelIndex][static_cast<size_t>(phase)];
                    const float optoDetector = optoOwnDetector
                        + (optoLinkedPhases[static_cast<size_t>(phase)]
                           - optoOwnDetector) * optoBlendWeight;
                    return applyCoreDistortion(modes.process(
                                                   mode, sample, ch, osSc, modeParams,
                                                   localMix, external, optoDetector,
                                                   useOptoDetector),
                                               distortionType, distortionAmount);
                });
            }
            previousOversampledSidechain[channelIndex] = sc;
            previousOptoOwnSidechain[channelIndex] = ownSc;
        }
        if (mode == MultiCompMode::Bus)
            busCompressionMeter.process(bypassSettled ? 0.0f
                : -std::min(modes.gainReduction(mode, 0),
                    nCh > 1 ? modes.gainReduction(mode, 1) : modes.gainReduction(mode, 0)));
    }
    if (mode == MultiCompMode::VCA)
    {
        if (!vcaVoicingActive)
            for (auto& f : vcaOutputVoicing) f.reset();
        vcaVoicingActive = true;
        if (nCh == 2)
            vcaLaw::OutputVoicing::processPair(vcaOutputVoicing[0], vcaOutputVoicing[1], out[0], out[1], nSamples);
        else
            for (int ch = 0; ch < nCh; ++ch)
                for (int i = 0; i < nSamples; ++i)
                    out[ch][i] = vcaOutputVoicing[static_cast<size_t>(ch)].process(out[ch][i]);
    }
    if (mode == MultiCompMode::Bus)
        busMeterReading.store(busCompressionMeter.reading(), std::memory_order_relaxed);
    const float gr = std::min(modes.gainReduction(mode, 0), nCh > 1 ? modes.gainReduction(mode, 1) : modes.gainReduction(mode, 0));
    masterGR.store(gr, std::memory_order_relaxed);
    for (auto& meter : bandGR) meter.store(0.0f, std::memory_order_relaxed);
}

// VCA mode's share of processRange, a block at a time. The VCA is feed-forward
// (its gain follows the detector feed, never its own output), so the per-sample
// loop splits into three passes with the same arithmetic in the same order:
// the host-rate bookkeeping every mode keeps, each channel's cell gains, and
// the audio through the oversampler. Byte-identical to the per-sample loop.
void MultiCompDSP::processVcaRange(const float* const* in, const float* const* sidechain,
                                   float* const* out, int nCh, int nSamples, float linkAmount,
                                   int linkMode, int hostSidechainDelay, float manualOutputDb,
                                   DistortionType distortionType, float distortionAmount) noexcept
{
    if (nSamples <= 0) return;
    const bool link = linkMode == 0 && linkAmount > 0.0001f && nCh > 1;
    const bool midSideDetector = linkMode == 1 && nCh > 1;
    // The OPTO cell links only in OPTO mode.
    modes.setOptoLinkWeight(0.0f);
    // processBlock has already applied the HP and shelf EQ to the sidechain.
    const float* sc0 = sidechain != nullptr ? sidechain[0] : in[0];
    const float* sc1 = nCh > 1 ? (sidechain != nullptr ? sidechain[1] : in[1]) : sc0;
    for (int i = 0; i < nSamples; ++i)
        vcaOutputDbCurve[static_cast<size_t>(i)] = manualOutputDb * manualMakeupScaleRamp.next();
    idleHostRateFeeds(sc0, sc1, nSamples, hostSidechainDelay, midSideDetector);
    // Each channel's detector feed.
    for (int ch = 0; ch < nCh; ++ch)
    {
        float* sc = vcaSidechain[static_cast<size_t>(ch)].data();
        const float* own = ch == 0 ? sc0 : sc1;
        if (link)
            for (int i = 0; i < nSamples; ++i)
            {
                const float scLevel = std::max(std::abs(sc0[i]), std::abs(sc1[i]));
                sc[i] = std::abs(own[i]) * (1.0f - linkAmount) + scLevel * linkAmount;
            }
        else if (linkMode == 1)
            for (int i = 0; i < nSamples; ++i)
                sc[i] = ch == 0 ? (sc0[i] + sc1[i]) * 0.5f : (sc0[i] - sc1[i]) * 0.5f;
        else
            std::copy_n(own, nSamples, sc);
    }
    float lastSc[kMaxChannels] = {0.0f, 0.0f}, lastOwnSc[kMaxChannels] = {0.0f, 0.0f};
    for (int ch = 0; ch < nCh; ++ch)
    {
        lastSc[ch] = vcaSidechain[static_cast<size_t>(ch)][static_cast<size_t>(nSamples - 1)];
        lastOwnSc[ch] = (ch == 0 ? sc0 : sc1)[nSamples - 1];
    }
    modeParams.vcaOutput.store(vcaOutputDbCurve[static_cast<size_t>(nSamples - 1)], std::memory_order_relaxed);
    // The carried endpoints the other modes' interpolated detectors start from.
    for (int ch = 0; ch < nCh; ++ch)
    {
        previousOversampledSidechain[static_cast<size_t>(ch)] = lastSc[ch];
        previousOversampledSidechainValid[static_cast<size_t>(ch)] = true;
        previousOptoOwnSidechain[static_cast<size_t>(ch)] = lastOwnSc[ch];
        previousOptoOwnSidechainValid[static_cast<size_t>(ch)] = true;
    }
    const int factor = modes.oversamplingFactor();
    for (int ch = 0; ch < nCh; ++ch)
    {
        modes.vcaBlockEnvelopes(ch, vcaSidechain[static_cast<size_t>(ch)].data(), nSamples, modeParams,
                                vcaEnvelopes.data(), vcaLawScratch.data());
        for (int i = 0; i < nSamples; ++i)
            vcaOutputGains[static_cast<size_t>(i)] = modes.vcaOutputGainFor(vcaOutputDbCurve[static_cast<size_t>(i)]);
        const float* envelopes = vcaEnvelopes.data();
        const float* outputGains = vcaOutputGains.data();
        oversamplers[static_cast<size_t>(ch)].processBlock(in[ch], out[ch], nSamples, vcaPhases.data(),
            [&](int i, int phase, float sample) noexcept {
                return applyCoreDistortion(sample * envelopes[i * factor + phase] * outputGains[i],
                                           distortionType, distortionAmount);
            });
    }
}

// The host-rate bookkeeping of the per-sample loop for a mode that reads none of
// the detector feeds (VCA, FET, Studio VCA): the host sidechain history advances by its
// alignment delay (the aligned samples land in vcaFeedScratch[0] and [1]) and
// every detector feed keeps its history (LazyUpsampler; at 1x there is none to
// keep, and a stage restarts from rest when a factor steps it again). BUS reads its own
// feed and passes idleBusFeed = false.
void MultiCompDSP::idleHostRateFeeds(const float* sc0, const float* sc1, int nSamples, int hostSidechainDelay,
                                     bool midSideDetector, bool idleBusFeed) noexcept
{
    float* aligned0 = vcaFeedScratch[0].data();
    float* aligned1 = vcaFeedScratch[1].data();
    float* feed = vcaFeedScratch[2].data();
    int write = hostSidechainWrite;
    delayLineBlock(hostSidechainHistory[0].data(), kHostSidechainRing, write, hostSidechainDelay, sc0, aligned0, nSamples);
    write = hostSidechainWrite;
    delayLineBlock(hostSidechainHistory[1].data(), kHostSidechainRing, write, hostSidechainDelay, sc1, aligned1, nSamples);
    hostSidechainWrite = write;
    if (idleBusFeed)
    {
        busExternalOversamplers[0].idleBlock(sc0, nSamples);
        busExternalOversamplers[1].idleBlock(sc1, nSamples);
    }
    for (int i = 0; i < nSamples; ++i)
        feed[i] = std::abs(aligned0[i]) >= std::abs(aligned1[i]) ? aligned0[i] : aligned1[i];
    optoLinkedDetectorUpsampler.idleBlock(feed, nSamples);
    if (midSideDetector)
        for (int i = 0; i < nSamples; ++i) feed[i] = (aligned0[i] + aligned1[i]) * 0.5f;
    optoOwnDetectorUpsamplers[0].idleBlock(midSideDetector ? feed : aligned0, nSamples);
    if (midSideDetector)
        for (int i = 0; i < nSamples; ++i) feed[i] = (aligned0[i] - aligned1[i]) * 0.5f;
    optoOwnDetectorUpsamplers[1].idleBlock(midSideDetector ? feed : aligned1, nSamples);
}

// Studio VCA mode's share of processRange: the reference is a zero-latency digital compressor with no
// oversampling, so the mode runs at the host rate (audioFactorForMode() gives 1) and its anti-alias stage only adds
// the common latency. The per-sample loop's bookkeeping as blocks, each channel's detector level (the linked peak:
// a channel's own peak blended toward the louder channel's by the Stereo Link amount; the audio input, or the
// external sidechain when one is in use), each channel's gain from MultiCompModes::studioVcaGains() (one for both
// channels of a fully linked pair), then the audio times the input gain, that gain and the makeup. Every input read
// happens before the first output write, so `out` may alias `in` or `sidechain`.
void MultiCompDSP::processStudioVcaRange(const float* const* in, const float* const* sidechain,
                                         float* const* out, int nCh, int nSamples, bool external, float linkAmount,
                                         int linkMode, int hostSidechainDelay, float manualOutputDb,
                                         DistortionType distortionType, float distortionAmount) noexcept
{
    if (nSamples <= 0) return;
    const bool link = linkMode == 0 && linkAmount > 0.0001f && nCh > 1;
    const bool midSideDetector = linkMode == 1 && nCh > 1;
    // The OPTO cell links only in OPTO mode.
    modes.setOptoLinkWeight(0.0f);
    // processBlock has already applied the HP and shelf EQ to the sidechain.
    const float* sc0 = sidechain != nullptr ? sidechain[0] : in[0];
    const float* sc1 = nCh > 1 ? (sidechain != nullptr ? sidechain[1] : in[1]) : sc0;
    float* makeupDb = studioVcaMakeupDb.data();
    float* makeup = studioVcaMakeupGain.data();
    for (int i = 0; i < nSamples; ++i)
        makeupDb[i] = manualOutputDb * manualMakeupScaleRamp.next();
    for (int i = 0; i < nSamples; ++i)
        makeup[i] = i > 0 && makeupDb[i] == makeupDb[i - 1] ? makeup[i - 1] : decibelsToGain(makeupDb[i]);
    modeParams.studioVcaOutput.store(makeupDb[nSamples - 1], std::memory_order_relaxed);
    idleHostRateFeeds(sc0, sc1, nSamples, hostSidechainDelay, midSideDetector);
    // Each channel's detector level and gain. A fully linked pair whose gain computers agree (from a reset on, they
    // do) computes one gain for both. In the Mid-Side layout processBlock has encoded the audio, not the external
    // sidechain: encode it the same way (into the feed scratch, free once idleHostRateFeeds has run).
    const float* detector0 = external ? sc0 : in[0];
    const float* detector1 = nCh > 1 ? (external ? sc1 : in[1]) : detector0;
    if (external && midSideDetector)
    {
        float* mid = vcaFeedScratch[0].data();
        float* side = vcaFeedScratch[1].data();
        for (int i = 0; i < nSamples; ++i)
        {
            mid[i] = (sc0[i] + sc1[i]) * 0.5f;
            side[i] = (sc0[i] - sc1[i]) * 0.5f;
        }
        detector0 = mid;
        detector1 = side;
    }
    const bool fullLink = link && linkAmount >= 1.0f;
    const int computed = fullLink && modes.studioVcaChannelsAgree() ? 1 : nCh;
    for (int ch = 0; ch < computed; ++ch)
    {
        float* level = studioVcaLevels[static_cast<size_t>(ch)].data();
        const float* own = ch == 0 ? detector0 : detector1;
        if (fullLink)
            for (int i = 0; i < nSamples; ++i) level[i] = std::max(std::abs(detector0[i]), std::abs(detector1[i]));
        else if (link)
            for (int i = 0; i < nSamples; ++i)
            {
                const float mine = std::abs(own[i]);
                const float linked = std::max(std::abs(detector0[i]), std::abs(detector1[i]));
                level[i] = mine + (linked - mine) * linkAmount;
            }
        else
            for (int i = 0; i < nSamples; ++i) level[i] = std::abs(own[i]);
        modes.studioVcaGains(ch, level, nSamples, modeParams, studioVcaGainCurves[static_cast<size_t>(ch)].data());
    }
    if (computed < nCh) modes.studioVcaShareChannel();
    // The carried endpoints the other modes' interpolated detectors start from, as the per-sample loop leaves them.
    const int last = nSamples - 1;
    for (int ch = 0; ch < nCh; ++ch)
    {
        const size_t c = static_cast<size_t>(ch);
        const float own = ch == 0 ? sc0[last] : sc1[last];
        const float scLevel = std::max(std::abs(sc0[last]), std::abs(sc1[last]));
        previousOversampledSidechain[c] = link ? std::abs(own) * (1.0f - linkAmount) + scLevel * linkAmount
            : linkMode == 1 && nCh > 1 ? (ch == 0 ? (sc0[last] + sc1[last]) * 0.5f : (sc0[last] - sc1[last]) * 0.5f)
            : own;
        previousOversampledSidechainValid[c] = true;
        previousOptoOwnSidechain[c] = own;
        previousOptoOwnSidechainValid[c] = true;
    }
    const float inputGain = modes.studioVcaInputGain(modeParams);
    for (int ch = 0; ch < nCh; ++ch)
    {
        const float* gains = studioVcaGainCurves[static_cast<size_t>(std::min(ch, computed - 1))].data();
        oversamplers[static_cast<size_t>(ch)].processBlock(in[ch], out[ch], nSamples, audioPhaseBlock[0].data(),
            [&](int i, int /*phase*/, float sample) noexcept {
                return applyCoreDistortion(sample * inputGain * gains[i] * makeup[i], distortionType, distortionAmount);
            });
    }
}

// BUS mode's share of processRange for a linked stereo pair (the default
// layout), a block at a time: the per-sample loop's bookkeeping as blocks, the
// audio and the external-detector feed up the oversampler as blocks, the pair
// sample by sample in between (it feeds back on itself), then the audio down.
// Dual Mono, Mid-Side, an unlinked pair and mono keep the per-sample loop.
// Every sidechain read happens before the first output write, so `out` may
// alias `in` or `sidechain`. Byte-identical to the per-sample loop.
void MultiCompDSP::processBusRange(const float* const* in, const float* const* sidechain,
                                   float* const* out, int nSamples, bool external, float linkAmount,
                                   int factor, int hostSidechainDelay, float manualOutputDb,
                                   DistortionType distortionType, float distortionAmount) noexcept
{
    if (nSamples <= 0) return;
    // The OPTO cell links only in OPTO mode.
    modes.setOptoLinkWeight(0.0f);
    // processBlock has already applied the HP and shelf EQ to the sidechain.
    const float* sc0 = sidechain != nullptr ? sidechain[0] : in[0];
    const float* sc1 = sidechain != nullptr ? sidechain[1] : in[1];
    float* outputDb = busOutputDbCurve.data();
    float* mix = busMixCurve.data();
    for (int i = 0; i < nSamples; ++i)
        outputDb[i] = manualOutputDb * manualMakeupScaleRamp.next();
    for (int i = 0; i < nSamples; ++i)
        mix[i] = busMixRamp.next();
    idleHostRateFeeds(sc0, sc1, nSamples, hostSidechainDelay, false, false);
    float* scLeft = busScPhases[0].data();
    float* scRight = busScPhases[1].data();
    busExternalOversamplers[0].upsampleOwnBlock(sc0, nSamples, scLeft);
    busExternalOversamplers[1].upsampleOwnBlock(sc1, nSamples, scRight);
    float* inputLeft = audioPhaseBlock[0].data();
    float* inputRight = audioPhaseBlock[1].data();
    float* outputLeft = outputPhaseBlock[0].data();
    float* outputRight = outputPhaseBlock[1].data();
    oversamplers[0].upsampleBlock(in[0], nSamples, inputLeft);
    oversamplers[1].upsampleBlock(in[1], nSamples, inputRight);
    for (int i = 0; i < nSamples; ++i)
    {
        modeParams.busMakeup.store(outputDb[i], std::memory_order_relaxed);
        for (int phase = 0; phase < factor; ++phase)
        {
            const int k = i * factor + phase;
            modes.processBusPair(inputLeft[k], inputRight[k], scLeft[k], scRight[k], modeParams, mix[i],
                                 external, linkAmount, outputLeft[k], outputRight[k]);
            outputLeft[k] = applyCoreDistortion(outputLeft[k], distortionType, distortionAmount);
            outputRight[k] = applyCoreDistortion(outputRight[k], distortionType, distortionAmount);
        }
        busCompressionMeter.process(bypassSettled ? 0.0f
            : -std::min(modes.gainReduction(MultiCompMode::Bus, 0), modes.gainReduction(MultiCompMode::Bus, 1)));
    }
    // The generic per-channel path's carried endpoints stopped tracking the
    // input: its next sample re-seeds them instead of interpolating from an
    // arbitrarily old value when the link is switched off.
    previousOversampledSidechainValid = {{false, false}};
    previousOptoOwnSidechainValid = {{false, false}};
    oversamplers[0].downsampleBlock(outputLeft, nSamples, out[0]);
    oversamplers[1].downsampleBlock(outputRight, nSamples, out[1]);
}

// Vintage FET mode's share of processRange, a block at a time: the per-sample
// loop's bookkeeping as blocks, the audio up and down the oversampler as blocks,
// the cell sample by sample in between (it feeds back on itself). Unlinked, each
// channel on its own (the two cells share only pure knob memos); linked, the
// pair advances together as in the per-sample loop, each channel reading its
// partner's upsampled input. Every sidechain read happens before the first
// output write, so `out` may alias `in` or `sidechain`. Byte-identical to the
// per-sample loop.
void MultiCompDSP::processFetRange(MultiCompMode mode, const float* const* in, const float* const* sidechain,
                                   float* const* out, int nCh, int nSamples, bool external,
                                   float linkAmount, int linkMode, int factor, int hostSidechainDelay,
                                   float manualOutputDb, DistortionType distortionType,
                                   float distortionAmount) noexcept
{
    if (nSamples <= 0) return;
    // FET (FET BLACK) and FetBlue run one engine with two voices (MultiCompModes::selectFetVoice).
    modes.selectFetVoice(mode == MultiCompMode::FetBlue ? 1 : 0);
    const bool link = linkMode == 0 && linkAmount > 0.0001f && nCh > 1;
    const bool midSideDetector = linkMode == 1 && nCh > 1;
    // The OPTO cell links only in OPTO mode.
    modes.setOptoLinkWeight(0.0f);
    // processBlock has already applied the HP and shelf EQ to the sidechain.
    const float* sc0 = sidechain != nullptr ? sidechain[0] : in[0];
    const float* sc1 = nCh > 1 ? (sidechain != nullptr ? sidechain[1] : in[1]) : sc0;
    float* outputDb = fetOutputDbCurve.data();
    for (int i = 0; i < nSamples; ++i)
        outputDb[i] = manualOutputDb * manualMakeupScaleRamp.next();
    idleHostRateFeeds(sc0, sc1, nSamples, hostSidechainDelay, midSideDetector);
    const float* aligned0 = vcaFeedScratch[0].data();
    const float* aligned1 = vcaFeedScratch[1].data();
    const float lastOwnSc[kMaxChannels] = {sc0[nSamples - 1], sc1[nSamples - 1]};
    const auto distort = [distortionType, distortionAmount](float sample) noexcept {
        return applyCoreDistortion(sample, distortionType, distortionAmount);
    };

    if (modes.fetFeedbackActive(modeParams))
    {
        // Candidate C6 a block at a time (MultiCompModes::processFetC6Block): each channel's audio up the
        // oversampler, its detector feed and linked partner per oversampled phase (as the paths below compute
        // them), the loop on the lanes, the distortion, and the audio down.
        const int phases = nSamples * factor;
        float* audio[kMaxChannels] = {audioPhaseBlock[0].data(), audioPhaseBlock[1].data()};
        float* detector[kMaxChannels] = {fetScPhases[0].data(), fetScPhases[1].data()};
        float* partners[kMaxChannels] = {fetPartnerPhases[0].data(), fetPartnerPhases[1].data()};
        float* results[kMaxChannels] = {outputPhaseBlock[0].data(), outputPhaseBlock[1].data()};
        for (int ch = 0; ch < nCh; ++ch)
            oversamplers[static_cast<size_t>(ch)].upsampleBlock(in[ch], nSamples, audio[ch]);
        if (link)
        {
            // Each channel gets its partner's driven sample at the audio's own phase; an external sidechain
            // keeps its interpolated path (see the linked path below).
            for (int ch = 0; ch < 2; ++ch)
            {
                const size_t c = static_cast<size_t>(ch);
                const float* raw = ch == 0 ? sc0 : sc1;
                if (!previousOversampledSidechainValid[c])
                {
                    previousOversampledSidechain[c] = raw[0];
                    previousOversampledSidechainValid[c] = true;
                }
                for (int i = 0; i < nSamples; ++i)
                    for (int phase = 0; phase < factor; ++phase)
                    {
                        const int k = i * factor + phase;
                        detector[ch][k] = external
                            ? factor == 1 ? raw[i]
                                : interpolateOversampledSidechain(
                                    i == 0 ? previousOversampledSidechain[c] : raw[i - 1], raw[i], phase, factor)
                            : audio[ch][k];
                    }
            }
            for (int k = 0; k < phases; ++k)
            {
                partners[0][k] = linkAmount * detector[1][k];
                partners[1][k] = linkAmount * detector[0][k];
            }
            previousOversampledSidechain = {{sc0[nSamples - 1], sc1[nSamples - 1]}};
            previousOptoOwnSidechain = {{sc0[nSamples - 1], sc1[nSamples - 1]}};
            previousOptoOwnSidechainValid = {{true, true}};
        }
        else
        {
            // Each channel's detector feed (Mid-Side detects on the encoded sidechain), interpolated causally
            // from the previous native detector sample to this one.
            for (int ch = 0; ch < nCh; ++ch)
            {
                const size_t c = static_cast<size_t>(ch);
                float* sc = fetSidechain[c].data();
                for (int i = 0; i < nSamples; ++i)
                    sc[i] = linkMode == 1 ? (ch == 0 ? (sc0[i] + sc1[i]) * 0.5f : (sc0[i] - sc1[i]) * 0.5f)
                                          : (ch == 0 ? sc0[i] : sc1[i]);
                const float carriedSc = previousOversampledSidechainValid[c] ? previousOversampledSidechain[c] : sc[0];
                for (int i = 0; i < nSamples; ++i)
                    for (int phase = 0; phase < factor; ++phase)
                        detector[ch][i * factor + phase] = factor == 1 ? sc[i]
                            : interpolateOversampledSidechain(i == 0 ? carriedSc : sc[i - 1], sc[i], phase, factor);
                previousOversampledSidechain[c] = sc[nSamples - 1];
                previousOversampledSidechainValid[c] = true;
                previousOptoOwnSidechain[c] = lastOwnSc[c];
                previousOptoOwnSidechainValid[c] = true;
            }
        }
        modes.processFetC6Block(nCh, modeParams, external, nSamples, factor, outputDb, audio, detector,
                                link ? partners : nullptr, results);
        modeParams.fetOutput.store(outputDb[nSamples - 1], std::memory_order_relaxed);
        for (int ch = 0; ch < nCh; ++ch)
        {
            for (int k = 0; k < phases; ++k) results[ch][k] = distort(results[ch][k]);
            oversamplers[static_cast<size_t>(ch)].downsampleBlock(results[ch], nSamples, out[ch]);
        }
        return;
    }

    if (!link)
    {
        // Each channel's detector feed: Mid-Side detects on the encoded
        // sidechain, otherwise its own.
        for (int ch = 0; ch < nCh; ++ch)
        {
            float* sc = fetSidechain[static_cast<size_t>(ch)].data();
            for (int i = 0; i < nSamples; ++i)
                sc[i] = linkMode == 1 ? (ch == 0 ? (sc0[i] + sc1[i]) * 0.5f : (sc0[i] - sc1[i]) * 0.5f)
                                      : (ch == 0 ? sc0[i] : sc1[i]);
        }
        for (int ch = 0; ch < nCh; ++ch)
        {
            const size_t c = static_cast<size_t>(ch);
            const float* sc = fetSidechain[c].data();
            // Interpolated causally from the previous native detector sample
            // to this one; the carried endpoint makes it independent of where
            // the host divides blocks.
            const float carriedSc = previousOversampledSidechainValid[c] ? previousOversampledSidechain[c] : sc[0];
            oversamplers[c].processBlock(in[ch], out[ch], nSamples, audioPhaseBlock[0].data(),
                [&](int i, int phase, float sample) noexcept {
                    if (phase == 0)
                        modeParams.fetOutput.store(outputDb[i], std::memory_order_relaxed);
                    const float osSc = factor == 1 ? sc[i]
                        : interpolateOversampledSidechain(i == 0 ? carriedSc : sc[i - 1], sc[i], phase, factor);
                    return distort(modes.process(mode, sample, ch, osSc, modeParams,
                                                 1.0f, external, 0.0f, false));
                });
            previousOversampledSidechain[c] = sc[nSamples - 1];
            previousOversampledSidechainValid[c] = true;
            previousOptoOwnSidechain[c] = lastOwnSc[c];
            previousOptoOwnSidechainValid[c] = true;
        }
        return;
    }

    // Linked: the installed reference FET limiter's stereo link is an
    // arithmetic signed maximum, not a magnitude maximum or a power sum. Its
    // internal link control is evaluated once per host sample and held across
    // the oversampling phases; the audio and colour path remain fully
    // oversampled. Recomputing the maximum on interpolated sub-samples creates
    // a false time-order arm at 96 kHz: the equal-level quarter-cycle response
    // moves by 0.059 dB while its adjacent and complementary phases do not. A
    // full 1x control proved the rate coordinate (0.0065 dB worst); this held
    // control reproduces it without disabling audio oversampling (0.0099 dB
    // worst). The internal control was measured against the audio through the
    // classic FIR, so it reads the aligned host samples; external sidechains
    // keep their interpolated path. (Candidate C6 links inside its own loop
    // instead: the block path above.)
    float* inputLeft = audioPhaseBlock[0].data();
    float* inputRight = audioPhaseBlock[1].data();
    float* outputLeft = outputPhaseBlock[0].data();
    float* outputRight = outputPhaseBlock[1].data();
    oversamplers[0].upsampleBlock(in[0], nSamples, inputLeft);
    oversamplers[1].upsampleBlock(in[1], nSamples, inputRight);
    constexpr float fetLinkDetectorGain = 1.0285f;
    for (int i = 0; i < nSamples; ++i)
    {
        modeParams.fetOutput.store(outputDb[i], std::memory_order_relaxed);
        for (int ch = 0; ch < 2; ++ch)
            if (!previousOversampledSidechainValid[static_cast<size_t>(ch)])
            {
                previousOversampledSidechain[static_cast<size_t>(ch)] = ch == 0 ? sc0[i] : sc1[i];
                previousOversampledSidechainValid[static_cast<size_t>(ch)] = true;
            }
        const float nativeSignedMaximum = std::max(aligned0[i], aligned1[i]);
        const bool nativeLeftUsesLinkedDetector = external
            || aligned0[i] < nativeSignedMaximum - 1.0e-12f;
        const bool nativeRightUsesLinkedDetector = external
            || aligned1[i] < nativeSignedMaximum - 1.0e-12f;
        for (int phase = 0; phase < factor; ++phase)
        {
            const int k = i * factor + phase;
            const float phaseSc0 = external
                ? factor == 1 ? sc0[i]
                    : interpolateOversampledSidechain(previousOversampledSidechain[0], sc0[i], phase, factor)
                : inputLeft[k];
            const float phaseSc1 = external
                ? factor == 1 ? sc1[i]
                    : interpolateOversampledSidechain(previousOversampledSidechain[1], sc1[i], phase, factor)
                : inputRight[k];
            const float detectorSc0 = external ? phaseSc0 : aligned0[i];
            const float detectorSc1 = external ? phaseSc1 : aligned1[i];
            const float signedMaximum = std::max(detectorSc0, detectorSc1);
            const float leftSidechain = detectorSc0
                + (fetLinkDetectorGain * signedMaximum - detectorSc0) * linkAmount;
            const float rightSidechain = detectorSc1
                + (fetLinkDetectorGain * signedMaximum - detectorSc1) * linkAmount;
            outputLeft[k] = distort(modes.process(mode, inputLeft[k], 0, leftSidechain, modeParams,
                                                  1.0f, external, 0.0f, nativeLeftUsesLinkedDetector));
            outputRight[k] = distort(modes.process(mode, inputRight[k], 1, rightSidechain, modeParams,
                                                   1.0f, external, 0.0f, nativeRightUsesLinkedDetector));
        }
        previousOversampledSidechain = {{sc0[i], sc1[i]}};
        previousOptoOwnSidechain = {{sc0[i], sc1[i]}};
        previousOptoOwnSidechainValid = {{true, true}};
    }
    oversamplers[0].downsampleBlock(outputLeft, nSamples, out[0]);
    oversamplers[1].downsampleBlock(outputRight, nSamples, out[1]);
}

// OPTO mode's share of processRange, a block at a time: the per-sample loop's
// bookkeeping and detector feeds as blocks, the cell and everything ahead of the
// output stage sample by sample (it feeds back on itself), then the output stage
// and the downsampler over the block. Byte-identical to the per-sample loop.
void MultiCompDSP::processOptoRange(const float* const* in, const float* const* sidechain,
                                    float* const* out, int nCh, int nSamples, bool external,
                                    float linkAmount, int linkMode, int hostSidechainDelay,
                                    float manualOutputDb, DistortionType distortionType,
                                    float distortionAmount) noexcept
{
    if (nSamples <= 0) return;
    const int factor = modes.oversamplingFactor();
    const bool link = linkMode == 0 && linkAmount > 0.0001f && nCh > 1;
    const bool midSideDetector = linkMode == 1 && nCh > 1;
    // Only a linked stereo pair blends toward the linked detector; Dual
    // Mono, Mid-Side and mono keep each channel's own feed.
    const float optoLinkWeight = link ? linkAmount : 0.0f;
    // OPTO can link inside the cell instead (kOptoLinkAtDriveDefault): the
    // cell then gets each channel's own detector and the link amount.
    const bool optoCellLink = link && modes.optoLinksAtDrive();
    const float optoBlendWeight = optoCellLink ? 0.0f : optoLinkWeight;
    modes.setOptoLinkWeight(optoCellLink ? optoLinkWeight : 0.0f);
    // processBlock has already applied the HP and shelf EQ to the sidechain.
    const float* sc0 = sidechain != nullptr ? sidechain[0] : in[0];
    const float* sc1 = nCh > 1 ? (sidechain != nullptr ? sidechain[1] : in[1]) : sc0;
    for (int i = 0; i < nSamples; ++i)
        optoGainKnob[static_cast<size_t>(i)] = optoGainDbToKnob(manualOutputDb * manualMakeupScaleRamp.next());
    // The detector feeds: the BUS one idles, the linked and own ones are read
    // when this block uses them (LazyUpsampler).
    float* aligned0 = vcaFeedScratch[0].data();
    float* aligned1 = vcaFeedScratch[1].data();
    float* scSigned = vcaFeedScratch[2].data();
    int write = hostSidechainWrite;
    delayLineBlock(hostSidechainHistory[0].data(), kHostSidechainRing, write, hostSidechainDelay, sc0, aligned0, nSamples);
    write = hostSidechainWrite;
    delayLineBlock(hostSidechainHistory[1].data(), kHostSidechainRing, write, hostSidechainDelay, sc1, aligned1, nSamples);
    hostSidechainWrite = write;
    busExternalOversamplers[0].idleBlock(sc0, nSamples);
    busExternalOversamplers[1].idleBlock(sc1, nSamples);
    for (int i = 0; i < nSamples; ++i)
        scSigned[i] = std::abs(aligned0[i]) >= std::abs(aligned1[i]) ? aligned0[i] : aligned1[i];
    float* linkedPhases = optoLinkedPhaseBlock.data();
    if (optoBlendWeight > 0.0f)
        optoLinkedDetectorUpsampler.upsampleBlock(scSigned, nSamples, linkedPhases);
    else
    {
        optoLinkedDetectorUpsampler.idleBlock(scSigned, nSamples);
        std::fill_n(linkedPhases, nSamples * factor, 0.0f);
    }
    float* ownFeed0 = optoOwnFeed[0].data();
    float* ownFeed1 = optoOwnFeed[1].data();
    for (int i = 0; i < nSamples; ++i)
    {
        // The own feed follows the stereo mode: Mid-Side detects on the
        // encoded sidechain, like the audio it compresses.
        ownFeed0[i] = midSideDetector ? (aligned0[i] + aligned1[i]) * 0.5f : aligned0[i];
        ownFeed1[i] = midSideDetector ? (aligned0[i] - aligned1[i]) * 0.5f : aligned1[i];
    }
    float* ownPhases[kMaxChannels] = {optoOwnPhaseBlock[0].data(), optoOwnPhaseBlock[1].data()};
    if (!external)
    {
        optoOwnDetectorUpsamplers[0].upsampleBlock(ownFeed0, nSamples, ownPhases[0]);
        optoOwnDetectorUpsamplers[1].upsampleBlock(ownFeed1, nSamples, ownPhases[1]);
    }
    else
    {
        optoOwnDetectorUpsamplers[0].idleBlock(ownFeed0, nSamples);
        optoOwnDetectorUpsamplers[1].idleBlock(ownFeed1, nSamples);
    }
    for (int ch = 0; ch < nCh; ++ch)
        oversamplers[static_cast<size_t>(ch)].upsampleBlock(in[ch], nSamples, audioPhaseBlock[static_cast<size_t>(ch)].data());

    if (optoCellLink)
    {
        // The cell ORs the two panel drives sample by sample, so the pair
        // advances together: channel 0 then channel 1 at every
        // oversampling phase, each on its own detector.
        const float* audioLeft = audioPhaseBlock[0].data();
        const float* audioRight = audioPhaseBlock[1].data();
        auto* feedLeft = optoStageFeeds[0].data();
        auto* feedRight = optoStageFeeds[1].data();
        for (int i = 0; i < nSamples; ++i)
        {
            modeParams.optoGain.store(optoGainKnob[static_cast<size_t>(i)], std::memory_order_relaxed);
            if (!previousOptoOwnSidechainValid[0] || !previousOptoOwnSidechainValid[1])
            {
                previousOptoOwnSidechain = {{sc0[i], sc1[i]}};
                previousOptoOwnSidechainValid = {{true, true}};
            }
            for (int phase = 0; phase < factor; ++phase)
            {
                const int k = i * factor + phase;
                const float leftDetector = !external ? ownPhases[0][k]
                    : factor == 1 ? sc0[i] : interpolateOversampledSidechain(
                        previousOptoOwnSidechain[0], sc0[i], phase, factor);
                const float rightDetector = !external ? ownPhases[1][k]
                    : factor == 1 ? sc1[i] : interpolateOversampledSidechain(
                        previousOptoOwnSidechain[1], sc1[i], phase, factor);
                modes.optoPreStagePair({{audioLeft[k], audioRight[k]}}, {{sc0[i], sc1[i]}},
                                       modeParams, external, {{leftDetector, rightDetector}},
                                       feedLeft[k], feedRight[k]);
            }
            const float scLevel = std::max(std::abs(sc0[i]), std::abs(sc1[i]));
            previousOversampledSidechain = {{
                std::abs(sc0[i]) * (1.0f - linkAmount) + scLevel * linkAmount,
                std::abs(sc1[i]) * (1.0f - linkAmount) + scLevel * linkAmount}};
            previousOversampledSidechainValid = {{true, true}};
            previousOptoOwnSidechain = {{sc0[i], sc1[i]}};
        }
    }
    else
    {
        // Each channel on its own (the cell's link weight is 0), so channel
        // by channel.
        const bool useOptoDetector = link || !external;
        for (int ch = 0; ch < nCh; ++ch)
        {
            const size_t c = static_cast<size_t>(ch);
            const float* audio = audioPhaseBlock[c].data();
            auto* feeds = optoStageFeeds[c].data();
            for (int i = 0; i < nSamples; ++i)
            {
                modeParams.optoGain.store(optoGainKnob[static_cast<size_t>(i)], std::memory_order_relaxed);
                const float ownSc = ch == 0 ? sc0[i] : sc1[i];
                const float midSideSc = ch == 0 ? (sc0[i] + sc1[i]) * 0.5f : (sc0[i] - sc1[i]) * 0.5f;
                const float scLevel = std::max(std::abs(sc0[i]), std::abs(sc1[i]));
                const float sc = link
                    ? std::abs(ownSc) * (1.0f - linkAmount) + scLevel * linkAmount
                    : linkMode == 1 ? midSideSc : ownSc;
                if (!previousOversampledSidechainValid[c])
                {
                    previousOversampledSidechain[c] = sc;
                    previousOversampledSidechainValid[c] = true;
                }
                const float previousSc = previousOversampledSidechain[c];
                if (!previousOptoOwnSidechainValid[c])
                {
                    previousOptoOwnSidechain[c] = ownSc;
                    previousOptoOwnSidechainValid[c] = true;
                }
                const float previousOptoOwnSc = previousOptoOwnSidechain[c];
                for (int phase = 0; phase < factor; ++phase)
                {
                    const int k = i * factor + phase;
                    // At 1x the host sample itself; above, interpolated causally
                    // from the previous native detector sample to this one.
                    const float osSc = factor == 1 ? sc
                        : interpolateOversampledSidechain(previousSc, sc, phase, factor);
                    const float optoOwnDetector = !external ? ownPhases[c][k]
                        : factor == 1 ? ownSc
                        : interpolateOversampledSidechain(previousOptoOwnSc, ownSc, phase, factor);
                    const float optoDetector = optoOwnDetector
                        + (linkedPhases[k] - optoOwnDetector) * optoBlendWeight;
                    modes.optoPreStage(audio[k], ch, osSc, modeParams, external, optoDetector,
                                       useOptoDetector, feeds[k]);
                }
                previousOversampledSidechain[c] = sc;
                previousOptoOwnSidechain[c] = ownSc;
            }
        }
    }
    // The output stage, then the distortion and the downsampler.
    if (nCh == 2)
        modes.optoStageBlockPair(optoStageFeeds[0].data(), optoStageFeeds[1].data(), nSamples * factor,
                                 outputPhaseBlock[0].data(), outputPhaseBlock[1].data());
    for (int ch = 0; ch < nCh; ++ch)
    {
        const size_t c = static_cast<size_t>(ch);
        float* phases = outputPhaseBlock[c].data();
        if (nCh != 2)
            modes.optoStageBlock(ch, optoStageFeeds[c].data(), nSamples * factor, phases);
        for (int k = 0; k < nSamples * factor; ++k)
            phases[k] = applyCoreDistortion(phases[k], distortionType, distortionAmount);
        oversamplers[c].downsampleBlock(phases, nSamples, out[ch]);
    }
}

void MultiCompDSP::processMultiband(const float* const* input, const float* const* sidechain,
                                    float* const* output, int nCh, int nSamples, int linkMode)
{
    updateCrossoverTargets();
    std::array<float, 3> coefficientFrequencies{{crossoverRamps[0].value(),
                                                 crossoverRamps[1].value(),
                                                 crossoverRamps[2].value()}};
    // A 20 ms sample ramp removes the parameter step; coefficient designs are
    // refreshed every 8 samples to keep the control path bounded and reusable.
    for (int i = 0; i < nSamples; ++i)
    {
        std::array<float, 3> smoothed{};
        for (int boundary = 0; boundary < 3; ++boundary)
            smoothed[static_cast<size_t>(boundary)] = crossoverRamps[static_cast<size_t>(boundary)].next();
        if (i % kCrossoverCoefficientInterval == 0)
            coefficientFrequencies = smoothed;
        for (int boundary = 0; boundary < 3; ++boundary)
            crossoverCurves[static_cast<size_t>(boundary)][static_cast<size_t>(i)] = coefficientFrequencies[static_cast<size_t>(boundary)];
    }

    std::uint8_t desiredMask = 0;
    int desiredCount = 0;
    // Snapshot every atomic once per block, then repair that snapshot before
    // mutating topology so concurrent multi-parameter automation cannot yield
    // fewer than two active bands.
    for (int band = 0; band < kMultiCompBands; ++band)
        if (params.mbEnabled[static_cast<size_t>(band)].load(std::memory_order_relaxed))
        {
            desiredMask |= static_cast<std::uint8_t>(1u << band);
            ++desiredCount;
        }
    for (int band = 0; band < kMultiCompBands && desiredCount < 2; ++band)
        if ((desiredMask & static_cast<std::uint8_t>(1u << band)) == 0)
        {
            desiredMask |= static_cast<std::uint8_t>(1u << band);
            ++desiredCount;
        }
    if (desiredMask != activeBandMask)
        rebuildMultibandTopology(desiredMask);

    const auto distortionType = static_cast<DistortionType>(std::clamp(params.distortion.load(std::memory_order_relaxed), 0, 3));
    const float distortionAmount = std::clamp(params.distortionAmount.load(std::memory_order_relaxed) * 0.01f, 0.0f, 1.0f);
    std::array<float, kMultiCompBands> maxGr{{0, 0, 0, 0}};
    std::array<bool, kMultiCompBands> solos{};
    std::array<bool, kMultiCompBands> bypassed{};
    std::array<float, kMultiCompBands> thresholds{};
    std::array<float, kMultiCompBands> ratios{};
    std::array<float, kMultiCompBands> attacks{};
    std::array<float, kMultiCompBands> releases{};
    std::array<float, kMultiCompBands> makeupGains{};
    bool anySolo = false;
    for (int band = 0; band < kMultiCompBands; ++band)
    {
        const size_t bandIndex = static_cast<size_t>(band);
        solos[bandIndex] = params.mbSolo[bandIndex].load(std::memory_order_relaxed);
        bypassed[bandIndex] = params.mbBypass[bandIndex].load(std::memory_order_relaxed);
        thresholds[bandIndex] = params.mbThreshold[bandIndex].load(std::memory_order_relaxed);
        ratios[bandIndex] = std::max(1.0f, params.mbRatio[bandIndex].load(std::memory_order_relaxed));
        attacks[bandIndex] = std::max(0.0001f, params.mbAttack[bandIndex].load(std::memory_order_relaxed) * 0.001f);
        releases[bandIndex] = std::max(0.001f, params.mbRelease[bandIndex].load(std::memory_order_relaxed) * 0.001f);
        makeupGains[bandIndex] = decibelsToGain(params.mbMakeup[bandIndex].load(std::memory_order_relaxed));
        // Restored or automated state can retain Solo on a disabled band. An
        // inaudible band must not mute every band that is actually present.
        anySolo = anySolo || (solos[bandIndex] && (activeBandMask & (1u << band)) != 0);
    }
    const float mbOutputDb = params.mbOutput.load(std::memory_order_relaxed);
    if (manualMakeupScaleRamp.arrived())
        std::fill_n(multibandOutputGains.begin(), nSamples, decibelsToGain(mbOutputDb * manualMakeupScaleRamp.next()));
    else
        for (int i = 0; i < nSamples; ++i)
            multibandOutputGains[static_cast<size_t>(i)] = decibelsToGain(mbOutputDb * manualMakeupScaleRamp.next());
    // A reduced-channel callback must not retain the absent channel's split,
    // phase-compensation or band-envelope histories for a subsequent stereo
    // block: that channel resumes from rest.
    if (nCh == 1)
    {
        for (int boundary = 0; boundary < 3; ++boundary)
        {
            crossoverForBoundary(boundary, 1, false).reset();
            crossoverForBoundary(boundary, 1, true).reset();
            for (int band = 0; band < kMultiCompBands - 1; ++band)
            {
                multibandPhase[static_cast<size_t>(band)][static_cast<size_t>(boundary)][1].reset();
                sidechainMultibandPhase[static_cast<size_t>(band)][static_cast<size_t>(boundary)][1].reset();
            }
        }
        for (int band = 0; band < kMultiCompBands; ++band)
            multibandEnvelopes[static_cast<size_t>(band * kMaxChannels + 1)] = 1.0f;
    }
    for (int band = 0; band < kMultiCompBands; ++band)
        if ((activeBandMask & static_cast<std::uint8_t>(1u << band)) == 0)
            for (int ch = 0; ch < nCh; ++ch)
            {
                std::fill_n(bands[static_cast<size_t>(band)][static_cast<size_t>(ch)].begin(), nSamples, 0.0f);
                std::fill_n(sidechainBands[static_cast<size_t>(band)][static_cast<size_t>(ch)].begin(), nSamples, 0.0f);
            }
    for (int ch = 0; ch < nCh; ++ch)
    {
        for (int i = 0; i < nSamples; ++i)
        {
            for (int boundary = 0; boundary < 3; ++boundary)
            {
                const float frequency = crossoverCurves[static_cast<size_t>(boundary)][static_cast<size_t>(i)];
                crossoverForBoundary(boundary, ch, false).setFrequency(frequency);
                crossoverForBoundary(boundary, ch, true).setFrequency(frequency);
            }
            float running = input[ch][i];
            for (int stage = 0; stage < numActiveStages; ++stage)
            {
                float low = 0.0f, high = 0.0f;
                crossoverForBoundary(stageBoundaryIndices[static_cast<size_t>(stage)], ch, false)
                    .processStandard(running, low, high);
                const size_t band = static_cast<size_t>(enabledBandIndices[static_cast<size_t>(stage)]);
                for (int later = stage + 1; later < numActiveStages; ++later)
                {
                    float phaseLow, phaseHigh;
                    const size_t boundary = static_cast<size_t>(stageBoundaryIndices[static_cast<size_t>(later)]);
                    auto& phase = multibandPhase[band][boundary][static_cast<size_t>(ch)];
                    phase.setFrequency(crossoverCurves[boundary][static_cast<size_t>(i)]);
                    phase.processStandard(low, phaseLow, phaseHigh);
                    low = phaseLow + phaseHigh;
                }
                bands[band]
                     [static_cast<size_t>(ch)][static_cast<size_t>(i)] = low;
                running = high;
            }
            bands[static_cast<size_t>(topBandIndex)][static_cast<size_t>(ch)][static_cast<size_t>(i)] = running;
            if (sidechain != nullptr)
            {
                float scRunning = nCh > 1 && linkMode == 1
                    ? (sidechain[0][i] + (ch == 0 ? sidechain[1][i] : -sidechain[1][i])) * 0.5f
                    : sidechain[std::min(ch, nCh - 1)][i];
                for (int stage = 0; stage < numActiveStages; ++stage)
                {
                    float low = 0.0f, high = 0.0f;
                    crossoverForBoundary(stageBoundaryIndices[static_cast<size_t>(stage)], ch, true)
                        .processStandard(scRunning, low, high);
                    const size_t band = static_cast<size_t>(enabledBandIndices[static_cast<size_t>(stage)]);
                    for (int later = stage + 1; later < numActiveStages; ++later)
                    {
                        float phaseLow, phaseHigh;
                        const size_t boundary = static_cast<size_t>(stageBoundaryIndices[static_cast<size_t>(later)]);
                        auto& phase = sidechainMultibandPhase[band][boundary][static_cast<size_t>(ch)];
                        phase.setFrequency(crossoverCurves[boundary][static_cast<size_t>(i)]);
                        phase.processStandard(low, phaseLow, phaseHigh);
                        low = phaseLow + phaseHigh;
                    }
                    sidechainBands[band]
                                  [static_cast<size_t>(ch)][static_cast<size_t>(i)] = low;
                    scRunning = high;
                }
                sidechainBands[static_cast<size_t>(topBandIndex)][static_cast<size_t>(ch)][static_cast<size_t>(i)] = scRunning;
            }
        }
        // JUCE captures the uncompressed split-band sum as the dry reference.
        // It has the same all-pass phase rotation as the wet recombination,
        // avoiding crossover comb filtering during the multiband mix ramp.
        for (int i = 0; i < nSamples; ++i)
        {
            float splitDry = 0.0f;
            for (int band = 0; band < kMultiCompBands; ++band)
                if (!anySolo || solos[static_cast<size_t>(band)])
                    splitDry += bands[static_cast<size_t>(band)][static_cast<size_t>(ch)][static_cast<size_t>(i)];
            dry[static_cast<size_t>(ch * maxBlock + i)] = splitDry;
        }
        for (int band = 0; band < kMultiCompBands; ++band)
        {
            const size_t bandIndex = static_cast<size_t>(band);
            const bool solo = solos[bandIndex];
            const bool mutedBySolo = anySolo && !solo;
            float& envelope = multibandEnvelopes[static_cast<size_t>(band * kMaxChannels + ch)];
            if ((activeBandMask & static_cast<std::uint8_t>(1u << band)) == 0)
            {
                envelope = 1.0f;
                continue;
            }
            if (mutedBySolo)
            {
                std::fill(bands[bandIndex][static_cast<size_t>(ch)].begin(),
                          bands[bandIndex][static_cast<size_t>(ch)].begin() + nSamples, 0.0f);
                envelope = 1.0f;
                continue;
            }
            if (bypassed[bandIndex])
            {
                envelope = 1.0f;
                continue;
            }
            const float threshold = thresholds[bandIndex];
            const float ratio = ratios[bandIndex];
            const float attack = attacks[bandIndex];
            const float release = releases[bandIndex];
            const float makeupGain = makeupGains[bandIndex];
            for (int i = 0; i < nSamples; ++i)
            {
                const float own = sidechain != nullptr ? std::abs(sidechainBands[static_cast<size_t>(band)][static_cast<size_t>(ch)][static_cast<size_t>(i)]) : std::abs(bands[static_cast<size_t>(band)][static_cast<size_t>(ch)][static_cast<size_t>(i)]);
                // JUCE's multiband compressor processes each split channel's
                // detector independently; global stereo-link blending is not
                // applied to this path.
                const float detector = own;
                const float db = gainToDecibels(std::max(detector, 1.0e-5f));
                constexpr float kneeDb = 6.0f;
                const float kneeStart = threshold - kneeDb * 0.5f;
                float reduction = 0.0f;
                if (db > kneeStart)
                {
                    if (db < threshold + kneeDb * 0.5f)
                    {
                        const float x = db - kneeStart;
                        reduction = (1.0f - 1.0f / ratio) * (x * x) / (2.0f * kneeDb);
                    }
                    else
                    {
                        const float over = db - threshold;
                        reduction = over * (1.0f - 1.0f / ratio);
                    }
                }
                const float target = decibelsToGain(-reduction);
                const float c = std::exp(-1.0f / ((target < envelope ? attack : release) * sampleRate));
                envelope = c * envelope + (1.0f - c) * target;
                bands[static_cast<size_t>(band)][static_cast<size_t>(ch)][static_cast<size_t>(i)] *= envelope * makeupGain;
                maxGr[static_cast<size_t>(band)] = std::min(maxGr[static_cast<size_t>(band)], gainToDecibels(envelope));
            }
        }
        for (int i = 0; i < nSamples; ++i)
        {
            float sum = 0.0f;
            for (int band = 0; band < kMultiCompBands; ++band) sum += bands[static_cast<size_t>(band)][static_cast<size_t>(ch)][static_cast<size_t>(i)];
            sum *= multibandOutputGains[static_cast<size_t>(i)];
            float limited = sum;
            if (std::abs(limited) > 1.5f)
                limited = std::copysign(1.5f + 0.5f * std::tanh((std::abs(limited) - 1.5f) * 2.0f), limited);
            output[ch][i] = applyCoreDistortion(limited,
                                                distortionType, distortionAmount);
        }
    }
    for (int band = 0; band < kMultiCompBands; ++band) bandGR[static_cast<size_t>(band)].store(maxGr[static_cast<size_t>(band)], std::memory_order_relaxed);
    masterGR.store(*std::min_element(maxGr.begin(), maxGr.end()), std::memory_order_relaxed);
}

std::array<float, 3> MultiCompDSP::crossoverTargets() const noexcept
{
    const float f1 = std::clamp(params.crossover1.load(std::memory_order_relaxed), 20.0f, 500.0f);
    const float f2 = std::clamp(params.crossover2.load(std::memory_order_relaxed), f1 * 1.5f, 5000.0f);
    const float f3 = std::clamp(params.crossover3.load(std::memory_order_relaxed), f2 * 1.5f, 16000.0f);
    return {{f1, f2, f3}};
}

void MultiCompDSP::prepareCrossovers() noexcept
{
    const auto frequencies = crossoverTargets();
    for (int boundary = 0; boundary < 3; ++boundary)
    {
        crossoverRamps[static_cast<size_t>(boundary)].prepare(sampleRate, static_cast<float>(kCrossoverRampMs) * 0.001f);
        crossoverRamps[static_cast<size_t>(boundary)].snap(frequencies[static_cast<size_t>(boundary)]);
    }
    for (int ch = 0; ch < kMaxChannels; ++ch)
        for (int boundary = 0; boundary < 3; ++boundary)
        {
            crossoverForBoundary(boundary, ch, false).prepare(sampleRate, frequencies[static_cast<size_t>(boundary)]);
            crossoverForBoundary(boundary, ch, true).prepare(sampleRate, frequencies[static_cast<size_t>(boundary)]);
            for (int band = 0; band < kMultiCompBands - 1; ++band)
            {
                multibandPhase[static_cast<size_t>(band)][static_cast<size_t>(boundary)][static_cast<size_t>(ch)]
                    .prepare(sampleRate, frequencies[static_cast<size_t>(boundary)]);
                sidechainMultibandPhase[static_cast<size_t>(band)][static_cast<size_t>(boundary)][static_cast<size_t>(ch)]
                    .prepare(sampleRate, frequencies[static_cast<size_t>(boundary)]);
            }
        }
}

void MultiCompDSP::resetCrossovers() noexcept
{
    const auto frequencies = crossoverTargets();
    for (int boundary = 0; boundary < 3; ++boundary)
        crossoverRamps[static_cast<size_t>(boundary)].snap(frequencies[static_cast<size_t>(boundary)]);
    for (int ch = 0; ch < kMaxChannels; ++ch)
        for (int boundary = 0; boundary < 3; ++boundary)
        {
            auto& audio = crossoverForBoundary(boundary, ch, false);
            auto& detector = crossoverForBoundary(boundary, ch, true);
            audio.setFrequency(frequencies[static_cast<size_t>(boundary)]);
            detector.setFrequency(frequencies[static_cast<size_t>(boundary)]);
            audio.reset();
            detector.reset();
            for (int band = 0; band < kMultiCompBands - 1; ++band)
                for (auto* filters : {&multibandPhase, &sidechainMultibandPhase})
                {
                    auto& phase = (*filters)[static_cast<size_t>(band)][static_cast<size_t>(boundary)][static_cast<size_t>(ch)];
                    phase.setFrequency(frequencies[static_cast<size_t>(boundary)]);
                    phase.reset();
                }
        }
}

void MultiCompDSP::updateCrossoverTargets() noexcept
{
    const auto frequencies = crossoverTargets();
    for (int boundary = 0; boundary < 3; ++boundary)
        crossoverRamps[static_cast<size_t>(boundary)].setTarget(frequencies[static_cast<size_t>(boundary)]);
}

DuskCrossover& MultiCompDSP::crossoverForBoundary(int boundary, int channel, bool sidechain) noexcept
{
    const size_t ch = static_cast<size_t>(std::clamp(channel, 0, kMaxChannels - 1));
    if (boundary <= 0) return sidechain ? scCrossover1[ch] : crossover1[ch];
    if (boundary == 1) return sidechain ? scCrossover2[ch] : crossover2[ch];
    return sidechain ? scCrossover3[ch] : crossover3[ch];
}

void MultiCompDSP::rebuildMultibandTopology(std::uint8_t mask) noexcept
{
    std::array<bool, kMultiCompBands - 1> oldActivePools{};
    std::array<std::array<bool, kMultiCompBands - 1>, kMultiCompBands - 1> oldPhasePaths{};
    if (activeBandMask != 0)
        for (int stage = 0; stage < numActiveStages; ++stage)
        {
            oldActivePools[static_cast<size_t>(stageBoundaryIndices[static_cast<size_t>(stage)])] = true;
            for (int later = stage + 1; later < numActiveStages; ++later)
                oldPhasePaths[static_cast<size_t>(enabledBandIndices[static_cast<size_t>(stage)])]
                             [static_cast<size_t>(stageBoundaryIndices[static_cast<size_t>(later)])] = true;
        }

    activeBandMask = mask;
    numEnabledBands = 0;
    for (int band = 0; band < kMultiCompBands; ++band)
        if ((activeBandMask & static_cast<std::uint8_t>(1u << band)) != 0)
            enabledBandIndices[static_cast<size_t>(numEnabledBands++)] = band;
    if (numEnabledBands < 2)
    {
        activeBandMask = 0x0f;
        numEnabledBands = kMultiCompBands;
        for (int band = 0; band < kMultiCompBands; ++band)
            enabledBandIndices[static_cast<size_t>(band)] = band;
    }
    numActiveStages = numEnabledBands - 1;
    topBandIndex = enabledBandIndices[static_cast<size_t>(numEnabledBands - 1)];

    std::array<bool, kMultiCompBands - 1> newActivePools{};
    for (int stage = 0; stage < numActiveStages; ++stage)
    {
        const int boundary = enabledBandIndices[static_cast<size_t>(stage + 1)] - 1;
        stageBoundaryIndices[static_cast<size_t>(stage)] = boundary;
        newActivePools[static_cast<size_t>(boundary)] = true;
        const size_t band = static_cast<size_t>(enabledBandIndices[static_cast<size_t>(stage)]);
        for (int later = stage + 1; later < numActiveStages; ++later)
        {
            const size_t laterBoundary = static_cast<size_t>(enabledBandIndices[static_cast<size_t>(later + 1)] - 1);
            if (!oldPhasePaths[band][laterBoundary])
                for (int ch = 0; ch < kMaxChannels; ++ch)
                {
                    multibandPhase[band][laterBoundary][static_cast<size_t>(ch)].reset();
                    sidechainMultibandPhase[band][laterBoundary][static_cast<size_t>(ch)].reset();
                }
        }
    }
    for (int boundary = 0; boundary < kMultiCompBands - 1; ++boundary)
        if (newActivePools[static_cast<size_t>(boundary)] && !oldActivePools[static_cast<size_t>(boundary)])
            for (int ch = 0; ch < kMaxChannels; ++ch)
            {
                crossoverForBoundary(boundary, ch, false).reset();
                crossoverForBoundary(boundary, ch, true).reset();
            }
}

void MultiCompDSP::updateMeters(float inPeak, float* const* out, int nCh, int nSamples)
{
    float outPeak = 0.0f;
    for (int ch = 0; ch < nCh; ++ch)
        outPeak = blockAbsPeak(out[ch], nSamples, outPeak);
    inputLevel.store(inPeak > 1.0e-5f ? gainToDecibels(inPeak) : -60.0f, std::memory_order_relaxed);
    outputLevel.store(outPeak > 1.0e-5f ? gainToDecibels(outPeak) : -60.0f, std::memory_order_relaxed);
}

int MultiCompDSP::getLatencySamples() const noexcept
{
    const auto mode = static_cast<MultiCompMode>(std::clamp(params.mode.load(std::memory_order_relaxed), 0, 7));
    const float globalLookaheadMs = params.globalLookahead.load(std::memory_order_relaxed);
    const float digitalLookaheadMs = params.digitalLookahead.load(std::memory_order_relaxed);
    return latencySamplesForMode(mode, globalLookaheadMs, digitalLookaheadMs);
}

int MultiCompDSP::latencySamplesForMode(MultiCompMode mode, float globalLookaheadMs,
                                        float digitalLookaheadMs) const noexcept
{
    const int lookahead = lookaheadSamples(globalLookaheadForMode(mode, globalLookaheadMs));
    const int digital = mode == MultiCompMode::Digital
        ? lookaheadSamples(std::clamp(digitalLookaheadMs, 0.0f, 10.0f)) : 0;
    return baseLatencyForMode(mode) + lookahead + digital;
}

int MultiCompDSP::lookaheadSamples(float milliseconds) const noexcept
{
    return static_cast<int>(std::round(milliseconds * 0.001f * static_cast<float>(sampleRate)));
}

int MultiCompDSP::baseLatencyForMode(MultiCompMode mode) const noexcept
{
    // JUCE keeps PDC constant at the maximum anti-alias group delay.  The
    // multiband path is native-rate, so it contributes only lookahead here.
    const int antiAlias = mode == MultiCompMode::Multiband ? 0 : antiAliasLatency;
    // Opto's output stage is oversampled locally (MultiCompOptoStageOversampler.hpp).
    const int optoStage = mode == MultiCompMode::Opto ? optoStageLatencyHost(sampleRate) : 0;
    return antiAlias + optoStage;
}

int MultiCompDSP::getMaximumLatencyPaddingSamples() const noexcept
{
    // The deepest base is Opto's; Multiband, with none, takes all of it.
    return baseLatencyForMode(MultiCompMode::Opto);
}

int MultiCompDSP::getHostLatencySamples() const noexcept
{
    const auto mode = static_cast<MultiCompMode>(std::clamp(params.mode.load(std::memory_order_relaxed), 0, 7));
    return getLatencySamples() + getMaximumLatencyPaddingSamples() - baseLatencyForMode(mode);
}

int MultiCompDSP::getLatencyPaddingSamples() const noexcept
{
    // The mode the last block ran in, which is what the caller is padding.
    return paddingForMode(lastMode >= 0 ? lastMode : params.mode.load(std::memory_order_relaxed));
}

} // namespace duskaudio
