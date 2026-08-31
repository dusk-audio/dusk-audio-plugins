// Copyright (C) 2026 Dusk Audio , GNU GPL v3.0 or later (see repository LICENSE).
#include "MultiCompDSP.hpp"

#include <algorithm>
#include <cmath>

namespace duskaudio
{

namespace
{
template <typename T>
void copyParameter(std::atomic<T>& destination, const std::atomic<T>& source) noexcept
{
    destination.store(source.load(std::memory_order_relaxed), std::memory_order_relaxed);
}

float fetAttackPositionFromPlain(float legacyMilliseconds) noexcept
{
    const float proportion = std::clamp(
        (legacyMilliseconds - 0.02f) / (80.0f - 0.02f), 0.0f, 1.0f);
    return std::pow(proportion, 0.3f);
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
    sampleRate = std::isfinite(sr) && sr > 0.0 ? sr : 48000.0;
    maxBlock = std::max(1, blockSize);
    const int oversamplingSetting = params.oversampling.load(std::memory_order_relaxed);
    const int initialOversampling = oversamplingSetting == 2 ? 4
                                  : oversamplingSetting == 1 ? 2 : 1;
    modes.prepare(sampleRate, maxBlock, initialOversampling);
    // MultiCompModes::prepare deliberately preserves state when its rate and
    // factor are unchanged. A host prepare is nevertheless a lifecycle reset
    // for the finite FET post-burst helper, including repeated prepare calls.
    modes.clearFetPostBurstRecovery();
    truePeakDetector.prepare();
    truePeakDetector.setQuality(MultiCompTruePeakDetector::Quality::Standard4x);
    for (auto& os : oversamplers) { os.setFactor(4); os.prepare(maxBlock); os.reset(); }
    optoLinkedDetectorOversampler.setFactor(4);
    optoLinkedDetectorOversampler.prepare(maxBlock);
    optoLinkedDetectorOversampler.reset();
    antiAliasLatency = static_cast<int>(std::lround(oversamplers[0].latency()));
    for (auto& os : oversamplers) os.setFactor(initialOversampling);
    optoLinkedDetectorOversampler.setFactor(initialOversampling);
    for (auto& f : sidechainFilters) f.prepare(sampleRate);
    for (auto& f : sidechainEQ) f.prepare(sampleRate);
    for (auto& band : bands) for (auto& v : band) v.assign(static_cast<size_t>(maxBlock), 0.0f);
    for (auto& band : sidechainBands) for (auto& v : band) v.assign(static_cast<size_t>(maxBlock), 0.0f);
    for (auto& v : processedSidechain) v.assign(static_cast<size_t>(maxBlock), 0.0f);
    for (auto& v : modeInput) v.assign(static_cast<size_t>(maxBlock), 0.0f);
    for (auto& v : crossoverCurves) v.assign(static_cast<size_t>(maxBlock), 0.0f);
    dry.assign(static_cast<size_t>(maxBlock * kMaxChannels), 0.0f);
    bypassDry.assign(static_cast<size_t>(maxBlock * kMaxChannels), 0.0f);
    fetStartupInput.assign(
        static_cast<size_t>(maxBlock * kMaxChannels), 0.0f);
    mixCurve.assign(static_cast<size_t>(maxBlock), 1.0f);
    bypassCurve.assign(static_cast<size_t>(maxBlock), 0.0f);
    autoGainCurve.assign(static_cast<size_t>(maxBlock), 1.0f);
    for (auto& line : delayedInput) line.assign(static_cast<size_t>(maxBlock), 0.0f);
    const size_t lookaheadSize = static_cast<size_t>(std::ceil(sampleRate * 0.01)) + 1u;
    for (auto& line : globalLookahead) line.assign(lookaheadSize, 0.0f);
    globalLookaheadWrite = {{0, 0}};
    previousOversampledSidechain = {{0.0f, 0.0f}};
    previousOversampledSidechainValid = {{false, false}};
    previousOptoOwnSidechain = {{0.0f, 0.0f}};
    previousOptoOwnSidechainValid = {{false, false}};
    previousBusSidechain = {{0.0f, 0.0f}};
    previousBusSidechainValid = {{false, false}};
    fetStartupInputPeak = {{0.0f, 0.0f}};
    fetStartupActiveSamples = {{0, 0}};
    fetStartupSilentSamples = {{0, 0}};
    const size_t dryDelaySize = static_cast<size_t>(std::max(1, antiAliasLatency + static_cast<int>(std::ceil(sampleRate * 0.01)) + 1));
    for (auto& line : dryPathDelay) line.assign(dryDelaySize, 0.0f);
    dryPathWrite = {{0, 0}};
    // Raw bypass history must cover the largest runtime latency combination:
    // global lookahead plus Digital lookahead, in addition to the AA maximum.
    const size_t bypassDelaySize = static_cast<size_t>(std::max(1, antiAliasLatency
        + static_cast<int>(std::ceil(sampleRate * 0.02)) + 1));
    for (auto& line : bypassDelay) line.assign(bypassDelaySize, 0.0f);
    bypassWrite = 0;
    for (auto& line : sidechainListenDelay) line.assign(bypassDelaySize, 0.0f);
    sidechainListenWrite = {{0, 0}};
    globalMixSmoother.prepare(sampleRate, 0.020f); globalMixSmoother.snap(params.mix.load(std::memory_order_relaxed) * 0.01f);
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
    bypassSettled = false; lastBypass = false; lastExternalSidechain = false;
    lastAutoMakeup = false; firstBlock = true; lastMode = -1; autoGainHoldSamples = 0;
    noiseState = 0x6d2b79f5u;
    multibandEnvelopes.fill(1.0f);
    prepareCrossovers();
    activeBandMask = 0;
    rebuildMultibandTopology(0x0f);
    for (auto& m : bandGR) m.store(0.0f, std::memory_order_relaxed);
    masterGR.store(0.0f, std::memory_order_relaxed);
    inputLevel.store(-60.0f, std::memory_order_relaxed);
    outputLevel.store(-60.0f, std::memory_order_relaxed);
}

void MultiCompDSP::reset()
{
    modes.reset();
    truePeakDetector.prepare();
    for (auto& os : oversamplers) os.reset();
    optoLinkedDetectorOversampler.reset();
    for (auto& f : sidechainFilters) f.reset();
    for (auto& f : sidechainEQ) f.reset();
    resetCrossovers();
    for (auto& band : bands) for (auto& v : band) std::fill(v.begin(), v.end(), 0.0f);
    for (auto& band : sidechainBands) for (auto& v : band) std::fill(v.begin(), v.end(), 0.0f);
    for (auto& v : processedSidechain) std::fill(v.begin(), v.end(), 0.0f);
    for (auto& v : modeInput) std::fill(v.begin(), v.end(), 0.0f);
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
    previousBusSidechain = {{0.0f, 0.0f}};
    previousBusSidechainValid = {{false, false}};
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
    globalMixSmoother.snap(params.mix.load(std::memory_order_relaxed) * 0.01f);
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
    autoGainHoldSamples = 0;
    noiseState = 0x6d2b79f5u;
    for (auto& m : bandGR) m.store(0.0f, std::memory_order_relaxed);
    masterGR.store(0.0f, std::memory_order_relaxed);
}

void MultiCompDSP::setParameter(Parameter parameter, float value) noexcept
{
    const bool b = value >= 0.5f;
    switch (parameter)
    {
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
        case Parameter::BusMix: params.busMix.store(value, std::memory_order_relaxed); break;
        case Parameter::StudioVcaThreshold: params.studioVcaThreshold.store(value, std::memory_order_relaxed); break;
        case Parameter::StudioVcaRatio: params.studioVcaRatio.store(value, std::memory_order_relaxed); break;
        case Parameter::StudioVcaAttack: params.studioVcaAttack.store(value, std::memory_order_relaxed); break;
        case Parameter::StudioVcaRelease: params.studioVcaRelease.store(value, std::memory_order_relaxed); break;
        case Parameter::StudioVcaOutput: params.studioVcaOutput.store(value, std::memory_order_relaxed); break;
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
    if (band < 0 || band >= kMultiCompBands) return;
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
    if (nSamples > maxBlock)
    {
        int offset = 0;
        while (offset < nSamples)
        {
            const int count = std::min(maxBlock, nSamples - offset);
            const float* inChunk[kMaxChannels] = {in[0] + offset, nCh > 1 ? in[1] + offset : in[0] + offset};
            const float* scChunk[kMaxChannels] = {nullptr, nullptr};
            if (sidechain != nullptr)
            {
                scChunk[0] = sidechain[0] + offset;
                scChunk[1] = nCh > 1 ? sidechain[1] + offset : sidechain[0] + offset;
            }
            float* outChunk[kMaxChannels] = {out[0] + offset, nCh > 1 ? out[1] + offset : out[0] + offset};
            processBlockExternal(inChunk, sidechain != nullptr ? scChunk : nullptr, outChunk, nCh, count);
            offset += count;
        }
        return;
    }
    ScopedFlushDenormals guard;
    float blockInputPeak = 0.0f;
    for (int ch = 0; ch < nCh; ++ch)
        for (int i = 0; i < nSamples; ++i)
            blockInputPeak = std::max(blockInputPeak, std::abs(in[ch][i]));
    const MultiCompMode mode = static_cast<MultiCompMode>(
        std::clamp(params.mode.load(std::memory_order_relaxed), 0, 7));
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
    const int oversamplingSetting = params.oversampling.load(std::memory_order_relaxed);
    const int actualOversampling = oversamplingSetting == 2 ? 4 : oversamplingSetting == 1 ? 2 : 1;
    const float globalLookaheadMs = std::clamp(
        params.globalLookahead.load(std::memory_order_relaxed), 0.0f, 10.0f);
    const float digitalLookaheadMs = std::clamp(
        params.digitalLookahead.load(std::memory_order_relaxed), 0.0f, 10.0f);
    const int blockLatency = latencySamplesForMode(mode, globalLookaheadMs, digitalLookaheadMs);
    const bool requestedBypass = params.bypass.load(std::memory_order_relaxed);
    const bool requestedSidechainListen = params.globalSidechainListen.load(std::memory_order_relaxed);
    sidechainListenRamp.setTarget(requestedSidechainListen ? 1.0f : 0.0f);
    if (requestedBypass != lastBypass)
    {
        lastBypass = requestedBypass;
        bypassSettled = false;
        bypassRamp.setTarget(requestedBypass ? 1.0f : 0.0f);
    }
    const bool useExternalSidechain = params.externalSidechain.load(std::memory_order_relaxed) && sidechain != nullptr;
    const float* filteredSidechain[kMaxChannels] = {processedSidechain[0].data(), processedSidechain[1].data()};
    const float sidechainHP = params.sidechainHP.load(std::memory_order_relaxed);
    for (int ch = 0; ch < nCh; ++ch)
    {
        const float* source = useExternalSidechain ? sidechain[ch] : in[ch];
        sidechainFilters[ch].setFrequency(sidechainHP);
        sidechainEQ[ch].setLowShelf(params.scLowFreq.load(std::memory_order_relaxed), params.scLowGain.load(std::memory_order_relaxed));
        sidechainEQ[ch].setHighShelf(params.scHighFreq.load(std::memory_order_relaxed), params.scHighGain.load(std::memory_order_relaxed));
        for (int i = 0; i < nSamples; ++i)
        {
            float sample = source[i];
            if (sidechainHP >= 1.0f)
                sample = sidechainFilters[ch].process(sample);
            processedSidechain[ch][static_cast<size_t>(i)] = sidechainEQ[ch].process(sample);
        }
    }
    const bool useTruePeak = params.truePeakEnable.load(std::memory_order_relaxed);
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
    processLatencyHistory(in, out, nCh, nSamples, blockLatency, requestedBypass && bypassSettled);
    if (requestedBypass && bypassSettled)
    {
        fetStartupInputPeak = {{0.0f, 0.0f}};
        fetStartupActiveSamples = {{0, 0}};
        fetStartupSilentSamples = {{0, 0}};
        modes.clearFetPostBurstRecovery();
        previousBusSidechainValid = {{false, false}};
        processSidechainListenHistory(filteredSidechain, nCh, nSamples, blockLatency);
        for (int i = 0; i < nSamples; ++i) (void)sidechainListenRamp.next();
        masterGR.store(0.0f, std::memory_order_relaxed);
        for (auto& meter : bandGR) meter.store(0.0f, std::memory_order_relaxed);
        updateMeters(blockInputPeak, out, nCh, nSamples);
        return;
    }
    const float* processingIn[kMaxChannels] = {in[0], nCh > 1 ? in[1] : in[0]};
    const int globalLookaheadDelay = static_cast<int>(std::round(
        globalLookaheadMs * 0.001f * static_cast<float>(sampleRate)));
    prepareLookahead(in, processingIn, nCh, nSamples, globalLookaheadDelay);
    const int digitalDryDelay = mode == MultiCompMode::Digital
        ? static_cast<int>(std::round(digitalLookaheadMs * 0.001f * static_cast<float>(sampleRate))) : 0;
    const int dryDelay = mode == MultiCompMode::Multiband ? 0 : antiAliasLatency + digitalDryDelay;
    for (int ch = 0; ch < nCh; ++ch)
    {
        auto& line = dryPathDelay[static_cast<size_t>(ch)];
        int& write = dryPathWrite[static_cast<size_t>(ch)];
        const int size = static_cast<int>(line.size());
        for (int i = 0; i < nSamples; ++i)
        {
            const int read = (write - std::min(dryDelay, size - 1) + size) % size;
            const float source = processingIn[ch][i];
            line[static_cast<size_t>(write)] = source;
            dry[static_cast<size_t>(ch * maxBlock + i)] = dryDelay > 0 ? line[static_cast<size_t>(read)] : source;
            write = (write + 1) % size;
        }
    }
    if (nCh > 1 && linkMode == 1)
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
            autoGainHoldSamples = std::max(1, static_cast<int>(sampleRate * kAutoGainTransitionMs * 0.001));
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
    const bool isMulti = mode == MultiCompMode::Multiband;
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
        const float attackPosition = fetAttackPositionFromPlain(
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
            for (int i = 0; i < nSamples; ++i)
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
    const float targetMix = std::clamp((isMulti ? params.mbMix.load(std::memory_order_relaxed) : params.mix.load(std::memory_order_relaxed)) * 0.01f, 0.0f, 1.0f);
    globalMixSmoother.setTarget(targetMix);
    // The processed output carries the full reported latency (AA plus both
    // lookaheads). Keep the monitor history running even while Listen is off so
    // either direction of the crossfade starts between time-aligned signals.
    const int listenDelay = blockLatency;
    processSidechainListenHistory(filteredSidechain, nCh, nSamples, listenDelay);
    for (int i = 0; i < nSamples; ++i)
        mixCurve[static_cast<size_t>(i)] = globalMixSmoother.next();
    // `dry` is one shared scratch line; copy the source per channel again before
    // mixing. This is safe for in-place processing and avoids process-time allocs.
    for (int ch = 0; ch < nCh; ++ch)
    {
        float* dryChannel = dry.data() + static_cast<size_t>(ch * maxBlock);
        for (int i = 0; i < nSamples; ++i)
        {
            const float wet = mixCurve[static_cast<size_t>(i)];
            if (!isMulti || wet < 1.0f)
                out[ch][i] = out[ch][i] * wet + dryChannel[i] * (1.0f - wet);
        }
    }

    const bool measureAutoGain = autoMakeup && !requestedSidechainListen;
    if (!measureAutoGain || autoGainHoldSamples > 0)
    {
        autoGainMatcher.reset();
        resetAutoGainMeasurement();
        autoGainSmoother.setTarget(1.0f);
    }
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
    for (int ch = 0; ch < nCh; ++ch)
        for (int i = 0; i < nSamples; ++i) out[ch][i] *= autoGainCurve[static_cast<size_t>(i)];
    if (params.noiseEnable.load(std::memory_order_relaxed) && mode != MultiCompMode::Digital && mode != MultiCompMode::Multiband)
    {
        for (int ch = 0; ch < nCh; ++ch)
            for (int i = 0; i < nSamples; ++i)
            {
                noiseState = noiseState * 1664525u + 1013904223u;
                const float unit = static_cast<float>((noiseState >> 8) & 0x00ffffffu) / 16777215.0f;
                out[ch][i] += (unit * 2.0f - 1.0f) * 0.0001f;
            }
    }
    for (int i = 0; i < nSamples; ++i)
    {
        const float listen = sidechainListenRamp.next();
        for (int ch = 0; ch < nCh; ++ch)
        {
            out[ch][i] = out[ch][i] * (1.0f - listen)
                       + filteredSidechain[ch][i] * listen;
        }
    }
    if (requestedSidechainListen)
    {
        masterGR.store(0.0f, std::memory_order_relaxed);
        for (auto& meter : bandGR) meter.store(0.0f, std::memory_order_relaxed);
    }
    // Bypass is the final gain-stage transition, so its 100% endpoint is the
    // same latency-aligned dry sample emitted by the settled-bypass fast path.
    for (int i = 0; i < nSamples; ++i)
        bypassCurve[static_cast<size_t>(i)] = bypassRamp.next();
    if (!bypassSettled)
    {
        for (int ch = 0; ch < nCh; ++ch)
        {
            const float* bypassChannel = bypassDry.data() + static_cast<size_t>(ch * maxBlock);
            for (int i = 0; i < nSamples; ++i)
            {
                const float bypass = bypassCurve[static_cast<size_t>(i)];
                out[ch][i] = out[ch][i] * (1.0f - bypass) + bypassChannel[i] * bypass;
            }
        }
    }
    if (requestedBypass && bypassRamp.value() >= 1.0f) bypassSettled = true;
    updateMeters(blockInputPeak, out, nCh, nSamples);
    firstBlock = false;
}

void MultiCompDSP::processLatencyHistory(const float* const* in, float* const* out,
                                         int nCh, int nSamples, int delay, bool emit) noexcept
{
    delay = std::clamp(delay, 0, static_cast<int>(bypassDelay[0].size()) - 1);
    const int size = static_cast<int>(bypassDelay[0].size());
    for (int i = 0; i < nSamples; ++i)
    {
        for (int ch = 0; ch < nCh; ++ch)
        {
            auto& line = bypassDelay[static_cast<size_t>(ch)];
            const int read = (bypassWrite - delay + size) % size;
            line[static_cast<size_t>(bypassWrite)] = in[ch][i];
            const float delayed = line[static_cast<size_t>(read)];
            bypassDry[static_cast<size_t>(ch * maxBlock + i)] = delayed;
            if (emit) out[ch][i] = delayed;
        }
        bypassWrite = (bypassWrite + 1) % size;
    }
}

void MultiCompDSP::processSidechainListenHistory(const float* const* sidechain,
                                                 int nCh, int nSamples,
                                                 int delay) noexcept
{
    for (int ch = 0; ch < nCh; ++ch)
    {
        auto& line = sidechainListenDelay[static_cast<size_t>(ch)];
        int& write = sidechainListenWrite[static_cast<size_t>(ch)];
        const int size = static_cast<int>(line.size());
        const int clampedDelay = std::clamp(delay, 0, size - 1);
        for (int i = 0; i < nSamples; ++i)
        {
            const int read = (write - clampedDelay + size) % size;
            const float sample = sidechain[ch][i];
            line[static_cast<size_t>(write)] = sample;
            processedSidechain[static_cast<size_t>(ch)][static_cast<size_t>(i)] =
                clampedDelay > 0 ? line[static_cast<size_t>(read)] : sample;
            write = (write + 1) % size;
        }
    }
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
    for (int ch = 0; ch < nCh; ++ch)
    {
        auto& line = globalLookahead[static_cast<size_t>(ch)];
        int& write = globalLookaheadWrite[static_cast<size_t>(ch)];
        for (int i = 0; i < nSamples; ++i)
        {
            const int read = (write - std::min(delay, size - 1) + size) % size;
            line[static_cast<size_t>(write)] = in[ch][i];
            if (delay > 0)
                delayedInput[static_cast<size_t>(ch)][static_cast<size_t>(i)] = line[static_cast<size_t>(read)];
            write = (write + 1) % size;
        }
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
        case MultiCompMode::StudioFET:
            copyParameter(modeParams.fetInput, params.fetInput);
            copyParameter(modeParams.fetOutput, params.fetOutput);
            copyParameter(modeParams.fetAttack, params.fetAttack);
            copyParameter(modeParams.fetRelease, params.fetRelease);
            copyParameter(modeParams.fetRatio, params.fetRatio);
            copyParameter(modeParams.fetCurve, params.fetCurve);
            copyParameter(modeParams.fetTransient, params.fetTransient);
            copyParameter(modeParams.fetThreshold, params.fetThreshold);
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
    optoLinkedDetectorOversampler.setFactor(actualOs);
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
        previousBusSidechainValid = {{false, false}};
        for (int i = 0; i < nSamples; ++i) (void)manualMakeupScaleRamp.next();
        processMultiband(in, external ? sidechain : nullptr, out, nCh, nSamples);
        return;
    }
    float manualOutputDb = 0.0f;
    switch (mode)
    {
        case MultiCompMode::Opto: manualOutputDb = optoKnobToGainDb(params.optoGain.load(std::memory_order_relaxed)); break;
        case MultiCompMode::FET:
        case MultiCompMode::StudioFET: manualOutputDb = params.fetOutput.load(std::memory_order_relaxed); break;
        case MultiCompMode::VCA: manualOutputDb = params.vcaOutput.load(std::memory_order_relaxed); break;
        case MultiCompMode::Bus: manualOutputDb = params.busMakeup.load(std::memory_order_relaxed); break;
        case MultiCompMode::StudioVCA: manualOutputDb = params.studioVcaOutput.load(std::memory_order_relaxed); break;
        case MultiCompMode::Digital: manualOutputDb = params.digitalOutput.load(std::memory_order_relaxed); break;
        case MultiCompMode::Multiband: break;
    }
    busMixRamp.setTarget(std::clamp(params.busMix.load(std::memory_order_relaxed) * 0.01f, 0.0f, 1.0f));
    digitalMixRamp.setTarget(std::clamp(params.digitalMix.load(std::memory_order_relaxed) * 0.01f, 0.0f, 1.0f));
    const bool linkedBusPathActive = mode == MultiCompMode::Bus
        && linkMode == 0 && linkAmount > 0.0001f && nCh > 1;
    if (!linkedBusPathActive)
        previousBusSidechainValid = {{false, false}};
    for (int i = 0; i < nSamples; ++i)
    {
        const float scaledOutputDb = manualOutputDb * manualMakeupScaleRamp.next();
        switch (mode)
        {
            case MultiCompMode::Opto: modeParams.optoGain.store(optoGainDbToKnob(scaledOutputDb), std::memory_order_relaxed); break;
            case MultiCompMode::FET:
            case MultiCompMode::StudioFET: modeParams.fetOutput.store(scaledOutputDb, std::memory_order_relaxed); break;
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
        const float scSigned = std::abs(sc0) >= std::abs(sc1) ? sc0 : sc1;
        const bool link = linkMode == 0 && linkAmount > 0.0001f && nCh > 1;
        std::array<float, 4> optoLinkedPhases{};
        int linkedPhase = 0;
        (void)optoLinkedDetectorOversampler.processSample(
            scSigned, [&](float sample) noexcept {
                optoLinkedPhases[static_cast<size_t>(linkedPhase++)] = sample;
                return sample;
            });
        if (mode == MultiCompMode::Bus && link)
        {
            std::array<float, 4> inputLeftPhases{}, inputRightPhases{};
            std::array<float, 4> outputLeftPhases{}, outputRightPhases{};
            oversamplers[0].upsampleSample(in[0][i], inputLeftPhases.data());
            oversamplers[1].upsampleSample(in[1][i], inputRightPhases.data());
            for (int ch = 0; ch < 2; ++ch)
                if (!previousBusSidechainValid[static_cast<size_t>(ch)])
                {
                    previousBusSidechain[static_cast<size_t>(ch)] = ch == 0 ? sc0 : sc1;
                    previousBusSidechainValid[static_cast<size_t>(ch)] = true;
                }
            for (int phase = 0; phase < actualOs; ++phase)
            {
                const float phaseSc0 = actualOs == 1 ? sc0
                    : interpolateOversampledSidechain(
                        previousBusSidechain[0], sc0, phase, actualOs);
                const float phaseSc1 = actualOs == 1 ? sc1
                    : interpolateOversampledSidechain(
                        previousBusSidechain[1], sc1, phase, actualOs);
                modes.processBusPair(
                    inputLeftPhases[static_cast<size_t>(phase)],
                    inputRightPhases[static_cast<size_t>(phase)],
                    phaseSc0, phaseSc1, modeParams, localBusMix,
                    external, linkAmount,
                    outputLeftPhases[static_cast<size_t>(phase)],
                    outputRightPhases[static_cast<size_t>(phase)]);
                outputLeftPhases[static_cast<size_t>(phase)] = applyCoreDistortion(
                    outputLeftPhases[static_cast<size_t>(phase)],
                    distortionType, distortionAmount);
                outputRightPhases[static_cast<size_t>(phase)] = applyCoreDistortion(
                    outputRightPhases[static_cast<size_t>(phase)],
                    distortionType, distortionAmount);
            }
            out[0][i] = oversamplers[0].downsampleSample(outputLeftPhases.data());
            out[1][i] = oversamplers[1].downsampleSample(outputRightPhases.data());
            previousBusSidechain = {{sc0, sc1}};
            // The generic per-channel path is skipped this sample, so its
            // carried endpoints stop tracking the input.  Invalidate them here
            // and let the next generic sample re-seed instead of interpolating
            // from an arbitrarily old value when the link is switched off.
            previousOversampledSidechainValid = {{false, false}};
            previousOptoOwnSidechainValid = {{false, false}};
            continue;
        }
        if (mode == MultiCompMode::FET && link)
        {
            // The installed 1176's stereo link is an arithmetic signed
            // maximum, not a magnitude maximum or a power sum. Its internal
            // link control is evaluated once per host sample and held across
            // the oversampling phases; the audio and colour path remain fully
            // oversampled. Recomputing the maximum on interpolated sub-samples
            // creates a false time-order arm at 96 kHz: the equal-level
            // quarter-cycle response moves by 0.059 dB while its adjacent and
            // complementary phases do not. A full 1x control proved the rate
            // coordinate (0.0065 dB worst); this held control reproduces it
            // without disabling audio oversampling (0.0099 dB worst).
            std::array<float, 4> inputLeftPhases{}, inputRightPhases{};
            std::array<float, 4> outputLeftPhases{}, outputRightPhases{};
            oversamplers[0].upsampleSample(in[0][i], inputLeftPhases.data());
            oversamplers[1].upsampleSample(in[1][i], inputRightPhases.data());
            for (int ch = 0; ch < 2; ++ch)
                if (!previousOversampledSidechainValid[static_cast<size_t>(ch)])
                {
                    previousOversampledSidechain[static_cast<size_t>(ch)]
                        = ch == 0 ? sc0 : sc1;
                    previousOversampledSidechainValid[static_cast<size_t>(ch)] = true;
                }
            const float nativeSignedMaximum = std::max(sc0, sc1);
            const bool nativeLeftUsesLinkedDetector = external
                || sc0 < nativeSignedMaximum - 1.0e-12f;
            const bool nativeRightUsesLinkedDetector = external
                || sc1 < nativeSignedMaximum - 1.0e-12f;
            for (int phase = 0; phase < actualOs; ++phase)
            {
                const float phaseSc0 = external
                    ? actualOs == 1 ? sc0
                        : interpolateOversampledSidechain(
                            previousOversampledSidechain[0], sc0,
                            phase, actualOs)
                    : inputLeftPhases[static_cast<size_t>(phase)];
                const float phaseSc1 = external
                    ? actualOs == 1 ? sc1
                        : interpolateOversampledSidechain(
                            previousOversampledSidechain[1], sc1,
                            phase, actualOs)
                    : inputRightPhases[static_cast<size_t>(phase)];
                // External sidechains retain their existing interpolated path;
                // only the measured internal link has a native-rate control.
                const float detectorSc0 = external ? phaseSc0 : sc0;
                const float detectorSc1 = external ? phaseSc1 : sc1;
                const float signedMaximum = std::max(detectorSc0, detectorSc1);
                constexpr float fetLinkDetectorGain = 1.0285f;
                const float leftSidechain = detectorSc0
                    + (fetLinkDetectorGain * signedMaximum - detectorSc0)
                        * linkAmount;
                const float rightSidechain = detectorSc1
                    + (fetLinkDetectorGain * signedMaximum - detectorSc1)
                        * linkAmount;
                outputLeftPhases[static_cast<size_t>(phase)] = applyCoreDistortion(
                    modes.process(
                        mode, inputLeftPhases[static_cast<size_t>(phase)], 0,
                        leftSidechain, modeParams, localBusMix, external, 0.0f,
                        nativeLeftUsesLinkedDetector),
                    distortionType, distortionAmount);
                outputRightPhases[static_cast<size_t>(phase)] = applyCoreDistortion(
                    modes.process(
                        mode, inputRightPhases[static_cast<size_t>(phase)], 1,
                        rightSidechain, modeParams, localBusMix, external, 0.0f,
                        nativeRightUsesLinkedDetector),
                    distortionType, distortionAmount);
            }
            out[0][i] = oversamplers[0].downsampleSample(outputLeftPhases.data());
            out[1][i] = oversamplers[1].downsampleSample(outputRightPhases.data());
            previousOversampledSidechain = {{sc0, sc1}};
            previousOptoOwnSidechain = {{sc0, sc1}};
            previousOptoOwnSidechainValid = {{true, true}};
            previousBusSidechainValid = {{false, false}};
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
            const bool fetOwnDetectorIsLinkedMaximum
                = std::abs(ownSc) >= scLevel - 1.0e-12f;
            const bool modeUsesLinkedDetector = mode == MultiCompMode::FET
                ? link && (external || !fetOwnDetectorIsLinkedMaximum) : link;
            if (actualOs == 1)
            {
                out[ch][i] = oversamplers[ch].processSample(input, [&](float sample) noexcept {
                    const float optoOwnDetector = external ? ownSc : sample;
                    const float optoDetector = optoOwnDetector
                        + (optoLinkedPhases[0] - optoOwnDetector) * linkAmount;
                    const float modeSidechain = mode == MultiCompMode::FET && link
                        ? external ? sc
                        : fetOwnDetectorIsLinkedMaximum ? std::abs(sample)
                        : std::abs(sample) * (1.0f - linkAmount)
                            + std::abs(optoLinkedPhases[0]) * linkAmount
                        : sc;
                    return applyCoreDistortion(modes.process(
                                                   mode, sample, ch, modeSidechain, modeParams,
                                                   localMix, external, optoDetector,
                                                   modeUsesLinkedDetector),
                                               distortionType, distortionAmount);
                });
            }
            else
            {
                int osPhase = 0;
                out[ch][i] = oversamplers[ch].processSample(input, [&](float sample) noexcept {
                    const int phase = osPhase++;
                    const float osSc = interpolateOversampledSidechain(
                        previousSc, sc, phase, actualOs);
                    const float optoOwnDetector = external
                        ? interpolateOversampledSidechain(
                              previousOptoOwnSc, ownSc, phase, actualOs)
                        : sample;
                    const float optoDetector = optoOwnDetector
                        + (optoLinkedPhases[static_cast<size_t>(phase)]
                           - optoOwnDetector) * linkAmount;
                    const float modeSidechain = mode == MultiCompMode::FET && link
                        ? external ? osSc
                        : fetOwnDetectorIsLinkedMaximum ? std::abs(sample)
                        : std::abs(sample) * (1.0f - linkAmount)
                            + std::abs(optoLinkedPhases[static_cast<size_t>(phase)])
                                * linkAmount
                        : osSc;
                    return applyCoreDistortion(modes.process(
                                                   mode, sample, ch, modeSidechain, modeParams,
                                                   localMix, external, optoDetector,
                                                   modeUsesLinkedDetector),
                                               distortionType, distortionAmount);
                });
            }
            previousOversampledSidechain[channelIndex] = sc;
            previousOptoOwnSidechain[channelIndex] = ownSc;
        }
    }
    const float gr = std::min(modes.gainReduction(mode, 0), nCh > 1 ? modes.gainReduction(mode, 1) : modes.gainReduction(mode, 0));
    masterGR.store(gr, std::memory_order_relaxed);
    for (auto& meter : bandGR) meter.store(0.0f, std::memory_order_relaxed);
}

void MultiCompDSP::processMultiband(const float* const* input, const float* const* sidechain,
                                    float* const* output, int nCh, int nSamples)
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
        anySolo = anySolo || solos[bandIndex];
    }
    const float mbOutputGain = decibelsToGain(params.mbOutput.load(std::memory_order_relaxed));
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
                bands[static_cast<size_t>(enabledBandIndices[static_cast<size_t>(stage)])]
                     [static_cast<size_t>(ch)][static_cast<size_t>(i)] = low;
                running = high;
            }
            bands[static_cast<size_t>(topBandIndex)][static_cast<size_t>(ch)][static_cast<size_t>(i)] = running;
            if (sidechain != nullptr)
            {
                float scRunning = sidechain[std::min(ch, nCh - 1)][i];
                for (int stage = 0; stage < numActiveStages; ++stage)
                {
                    float low = 0.0f, high = 0.0f;
                    crossoverForBoundary(stageBoundaryIndices[static_cast<size_t>(stage)], ch, true)
                        .processStandard(scRunning, low, high);
                    sidechainBands[static_cast<size_t>(enabledBandIndices[static_cast<size_t>(stage)])]
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
            sum *= mbOutputGain;
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
    if (activeBandMask != 0)
        for (int stage = 0; stage < numActiveStages; ++stage)
            oldActivePools[static_cast<size_t>(stageBoundaryIndices[static_cast<size_t>(stage)])] = true;

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
        for (int i = 0; i < nSamples; ++i)
            outPeak = std::max(outPeak, std::abs(out[ch][i]));
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
    const int lookahead = static_cast<int>(std::round(std::clamp(globalLookaheadMs, 0.0f, 10.0f)
        * 0.001f * static_cast<float>(sampleRate)));
    const int digital = mode == MultiCompMode::Digital
        ? static_cast<int>(std::round(std::clamp(digitalLookaheadMs, 0.0f, 10.0f)
            * 0.001f * static_cast<float>(sampleRate))) : 0;
    // JUCE keeps PDC constant at the maximum anti-alias group delay.  The
    // multiband path is native-rate, so it contributes only lookahead here.
    const int antiAlias = mode == MultiCompMode::Multiband ? 0 : antiAliasLatency;
    return antiAlias + lookahead + digital;
}

} // namespace duskaudio
