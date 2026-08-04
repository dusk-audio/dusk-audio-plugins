// TapeEchoDSP.cpp — vintage three-head tape echo emulation core (framework-free C++17).

#include "TapeEchoDSP.hpp"
#include "DuskDenormals.hpp"   // ScopedFlushDenormals (SSE + ARM64 FPCR)

#include <algorithm>

namespace duskaudio
{

namespace
{
    // 4-point, 3rd-order Hermite. Interpolates between x0 and x1 at `frac`.
    inline float hermite(float frac, float xm1, float x0, float x1, float x2) noexcept
    {
        const float c = 0.5f * (x1 - xm1);
        const float v = x0 - x1;
        const float w = c + v;
        const float a = w + v + 0.5f * (x2 - x0);
        const float b = w + a;
        return (((a * frac - b) * frac + c) * frac) + x0;
    }

    inline int nextPowerOfTwo(int n) noexcept
    {
        int p = 1;
        while (p < n)
            p <<= 1;
        return p;
    }

    // Mode matrix: which read heads and whether the spring tank is active.
    struct ModeConfig { float h1, h2, h3, reverb; };

    constexpr ModeConfig kModeTable[TapeEchoDSP::kNumModes] =
    {
        { 1, 0, 0, 0 },   // 1:  Head 1
        { 0, 1, 0, 0 },   // 2:  Head 2
        { 0, 0, 1, 0 },   // 3:  Head 3
        { 0, 0.724f, 0.724f, 0 }, // 4: Heads 2 + 3
        { 1, 0, 0, 1 },   // 5:  Head 1 + Reverb
        { 0, 1, 0, 1 },   // 6:  Head 2 + Reverb
        { 0, 0, 1, 1 },   // 7:  Head 3 + Reverb
        { 0.724f, 0.724f, 0, 1 }, // 8: Heads 1 + 2 + Reverb
        { 0, 0.724f, 0.724f, 1 }, // 9: Heads 2 + 3 + Reverb
        { 0.724f, 0, 0.724f, 1 }, // 10: Heads 1 + 3 + Reverb
        { 0.596f, 0.596f, 0.596f, 1 }, // 11: all heads + Reverb
        { 0, 0, 0, 1 },   // 12: Reverb only
    };

    // Hosted single-head trims. Multi-head attenuation lives in the mode table
    // above and keeps the summed programs near the single-head loudness.
    constexpr float kHeadTrim[3] = { 1.0f, 1.122f, 0.958f };

    // Transport motion. Flutter frequency follows tape speed; its depth grows
    // toward the slow end of the motor range. The second harmonic gives the
    // measured capstan cycle its slightly non-sinusoidal shape.
    constexpr float kWowHz       = 0.5f;
    constexpr float kFlutterMaxHz= 3.857f;
    constexpr float kWowDepth    = 0.0008f;
    constexpr float kNoiseDepth  = 0.0004f;

    // Scrape flutter. The reference's flutter band (6-100 Hz) is stochastic,
    // not a tone: band noise centred near 6 Hz, not a harmonic of the loop
    // rate. Modelled as white noise through a resonant band-pass plus a
    // one-pole skirt, so the resulting speed deviation (which differentiates
    // the delay modulation, i.e. tilts +6 dB/oct) peaks just above 6 Hz and
    // then falls away instead of running flat to Nyquist.
    constexpr float kFlutterNoiseHz    = 6.0f;
    constexpr float kFlutterNoiseQ     = 2.0f;
    constexpr float kFlutterNoiseLPHz  = 9.0f;
    constexpr float kFlutterNoiseDepth = 0.00515f;
    // Age coupling. Measured reference flutter grows 1.00 / 1.76 / 3.36 over
    // Loop Age 0 / 0.5 / 1.0. The common `wf` factor below already contributes
    // (1 + 0.20*age), so this polynomial carries the remainder.
    constexpr float kFlutterAgeLin = 0.589f;
    constexpr float kFlutterAgeSq  = 1.214f;

    constexpr float kTwoPi = 6.28318530717958647692f;

    // clap-validator exercises fractional sample rates down to 1 kHz. Keep
    // every bilinear-transform design safely below Nyquist; this is a no-op
    // throughout the plug-in's normal supported audio-rate range.
    inline float safeBiquadFrequency(double sampleRate, float frequency) noexcept
    {
        return std::max(
            1.0f,
            std::min(frequency, 0.45f * (float)sampleRate));
    }

    // DCBlocker's default pole (R = 0.9975) is FIXED, so its corner rides the
    // sample rate: ~19 Hz at 48 kHz but ~38 Hz at 96 kHz and ~76 Hz at 192 kHz,
    // audibly thinning the low end on high-rate sessions. Driving it from a
    // cutoff instead makes the corner track fs. This value is the exact cutoff
    // that reproduces R = 0.9975 at 48 kHz (-48000*ln(0.9975)/2pi), so the
    // calibrated 48 kHz response is preserved bit-for-bit.
    constexpr float kDcBlockerCutoffHz = 19.122506f;
}

float TapeEchoDSP::leadingHeadRatioForMode(int mode1to12) noexcept
{
    const auto& mode = kModeTable[clampInt(mode1to12, 1, kNumModes) - 1];
    if (mode.h1 > 0.0f) return kHeadRatio[0];
    if (mode.h2 > 0.0f) return kHeadRatio[1];
    if (mode.h3 > 0.0f) return kHeadRatio[2];
    return 1.0f;
}

//==============================================================================
// Oversampled preamp — halfband taps now shared (plugins/shared-dpf/dsp/
// DuskOversampler.hpp, duskaudio::hbtaps::kA/kB; scipy remez, identical values).
//==============================================================================
float TapeEchoDSP::preampOversampled(Channel& ch, float x, float drive) noexcept
{
    // 4x oversampled saturation: upsample (2 cascaded 2x halfbands), shape,
    // decimate through the same filters. Zero-stuffed interpolation needs
    // the x2 gain on each upsampling stage.
    for (int p = 0; p < 2; ++p)
    {
        ch.upA.push(p == 0 ? x : 0.0f);
        const float a = 2.0f * ch.upA.out(hbtaps::kA);
        for (int q = 0; q < 2; ++q)
        {
            ch.upB.push(q == 0 ? a : 0.0f);
            const float b = 2.0f * ch.upB.out(hbtaps::kB);
            ch.downB.push(preampShape(b * drive));
        }
        ch.downA.push(ch.downB.out(hbtaps::kB));
    }
    return ch.downA.out(hbtaps::kA);
}

//==============================================================================
// (ShelfFilter coefficient design moved to shared Biquad::shelfSlope1 — see
//  plugins/shared-dpf/dsp/DuskFilters.hpp. Bit-identical float op-order.)
//==============================================================================

//==============================================================================
// SpringReverb
//==============================================================================
void SpringReverb::Spring::prepare(double fs, float lengthSeconds, float fbAmount,
                                   float lfoHz, float apCoeff)
{
    len = std::max(16, (int)std::lround(lengthSeconds * fs));
    buf.assign((size_t)len + 8, 0.0f);
    // The return transducer and mounting add a short path beyond the nominal
    // one-way spring length. The 7.9 ms return offset complements the 2.2 ms
    // one-way pickup calibration below, keeping the measured recurrence period
    // unchanged while placing the first arrival correctly.
    const int feedbackLen =
        len + std::max(1, (int)std::lround(0.0079 * fs));
    feedbackBuf.assign((size_t)feedbackLen, 0.0f);
    writeIdx = 0;
    feedbackWriteIdx = 0;
    feedback = fbAmount;
    lfoPhase = 0.0f;
    lfoInc   = kTwoPi * lfoHz / (float)fs;
    // A few samples, rate-scaled — capped so the modulation excursion stays
    // inside the +8 buffer guard band at any sample rate (6 < 8).
    lfoDepth = std::min(
        1.54f * (1.5f + 0.00005f * (float)fs), 6.0f);
    for (auto& ap : chain)
        ap.a = apCoeff;
    damping.setCutoff(17000.0f, fs);
    feedbackHighPass.setCoeffs(Biquad::highPass(
        fs, safeBiquadFrequency(fs, 51.0f), 0.70710678f));
    highDamping.setCoeffs(Biquad::lowPass(
        fs, safeBiquadFrequency(fs, 8000.0f), 0.70710678f));
    airDamping.setCoeffs(Biquad::shelfSlope1(
        fs, safeBiquadFrequency(fs, 6500.0f), -2.75f, true));
    upperModeDamping.setCoeffs(Biquad::peak(
        fs, safeBiquadFrequency(fs, 8000.0f), -0.70f, 3.0f));
    reset();
}

void SpringReverb::Spring::reset()
{
    std::fill(buf.begin(), buf.end(), 0.0f);
    std::fill(feedbackBuf.begin(), feedbackBuf.end(), 0.0f);
    for (auto& ap : chain)
        ap.z = 0.0f;
    damping.reset();
    feedbackHighPass.reset();
    highDamping.reset();
    airDamping.reset();
    upperModeDamping.reset();
    lfoPhase = 0.0f;
    writeIdx = 0;
    feedbackWriteIdx = 0;
}

float SpringReverb::Spring::process(float x) noexcept
{
    // Modulated read position (linear interpolation is plenty: the
    // modulation is a fraction of a millisecond, moving at < 1 Hz).
    lfoPhase += lfoInc;
    if (lfoPhase > kTwoPi)
        lfoPhase -= kTwoPi;

    const float delay = (float)len - 4.0f + lfoDepth * std::sin(lfoPhase);
    const float sz = (float)buf.size();
    // Robust wrap into [0, sz): a single "+= sz" leaves rp negative when delay
    // exceeds sz (possible at high fs if the guard margin is ever outgrown).
    float rp = (float)writeIdx - delay;
    rp -= std::floor(rp / sz) * sz;
    if (rp >= sz) rp -= sz; // fp edge guard

    const int   i0   = (int)rp;
    const float frac = rp - (float)i0;
    const int   i1   = (i0 + 1 < (int)buf.size()) ? i0 + 1 : 0;
    float y = buf[(size_t)i0] + frac * (buf[(size_t)i1] - buf[(size_t)i0]);

    // Dispersive allpass chain: the "boing".
    for (auto& ap : chain)
        y = ap.process(y);

    y = highDamping.process(damping.process(y));

    // A spring's pickup hears the first one-way wave, while feedback returns
    // through the opposite direction of travel. Keeping that return delay
    // separate preserves the initial arrival but makes the recurrence period
    // a physical round trip instead of another one-way transit.
    const float feedbackReturn =
        upperModeDamping.process(airDamping.process(
            feedbackHighPass.process(
                feedbackBuf[(size_t)feedbackWriteIdx])));
    feedbackBuf[(size_t)feedbackWriteIdx] = y;
    if (++feedbackWriteIdx >= (int)feedbackBuf.size())
        feedbackWriteIdx = 0;

    buf[(size_t)writeIdx] = x + feedback * feedbackReturn;
    if (++writeIdx >= (int)buf.size())
        writeIdx = 0;

    return y;
}

void SpringReverb::prepare(double sampleRate, float detune)
{
    // Four unequal springs build the tank's dense dispersive tail.
    // Length-compensated feedback keeps the low-band decay close across the
    // four unequal paths. The high damping corner preserves the measured
    // upper-mid spring tail while still shortening the extreme top end.
    constexpr float kPickupAdvanceSeconds = 0.0022f;
    springs[0].prepare(sampleRate, (0.0200f - kPickupAdvanceSeconds) * detune,
                       0.8980f, 0.31f, 0.62f);
    springs[1].prepare(sampleRate, (0.0215f - kPickupAdvanceSeconds) * detune,
                       0.8900f, 0.47f, 0.66f);
    springs[2].prepare(sampleRate, (0.0230f - kPickupAdvanceSeconds) * detune,
                       0.8820f, 0.38f, 0.60f);
    springs[3].prepare(sampleRate, (0.0245f - kPickupAdvanceSeconds) * detune,
                       0.8740f, 0.53f, 0.68f);
    dcBlock.setSampleRate(sampleRate, kDcBlockerCutoffHz);
    pickupTap8  = std::max(1, (int)std::lround(0.008 * sampleRate));
    pickupTap18 = std::max(1, (int)std::lround(0.018 * sampleRate));
    pickupTap28 = std::max(1, (int)std::lround(0.028 * sampleRate));
    pickupSpring3Tap85 =
        std::max(1, (int)std::lround(0.0085 * sampleRate));
    pickupSpring3Tap20 =
        std::max(1, (int)std::lround(0.020 * sampleRate));
    pickupSpring3Tap285 =
        std::max(1, (int)std::lround(0.0285 * sampleRate));
    const size_t pickupSize =
        (size_t)std::max(pickupTap28, pickupSpring3Tap285) + 1;
    pickupBuf.assign(pickupSize, 0.0f);
    pickupSpring3Buf.assign(pickupSize, 0.0f);
    pickupWriteIdx = 0;
    outputDiffusionBuf.assign(
        (size_t)std::max(1, (int)std::lround(0.0013 * sampleRate)),
        0.0f);
    outputDiffusionWriteIdx = 0;
    inputHP.setCutoff(140.0f, sampleRate);
    inputLP.setCutoff(4200.0f, sampleRate);
    outputHP.setCoeffs(Biquad::highPass(
        sampleRate, safeBiquadFrequency(sampleRate, 400.0f), 0.70710678f));
    outputVoiceLP.setCoeffs(Biquad::lowPass(
        sampleRate, safeBiquadFrequency(sampleRate, 1800.0f), 0.70710678f));
    for (size_t i = 0; i < outputCeilingLP.size(); ++i)
    {
        const float theta =
            ((2.0f * (float)i + 1.0f) * kDuskPi)
            / (4.0f * (float)outputCeilingLP.size());
        const float q = 1.0f / (2.0f * std::cos(theta));
        outputCeilingLP[i].setCoeffs(Biquad::lowPass(
            sampleRate, safeBiquadFrequency(sampleRate, 4900.0f), q));
    }
    outputLowContour.setCoeffs(Biquad::peak(
        sampleRate, safeBiquadFrequency(sampleRate, 127.0f),
        -5.0f, 2.0f));
    outputBody.setCoeffs(Biquad::peak(
        sampleRate, safeBiquadFrequency(sampleRate, 1000.0f),
        0.5f, 0.70f));
    outputPresence.setCoeffs(Biquad::peak(
        sampleRate, safeBiquadFrequency(sampleRate, 5000.0f),
        5.0f, 3.0f));
    pickupImagePhaseInc = kTwoPi
        * safeBiquadFrequency(sampleRate, 12000.0f)
        / (float)sampleRate;
    reset();
}

void SpringReverb::reset()
{
    for (auto& s : springs)
        s.reset();
    std::fill(pickupBuf.begin(), pickupBuf.end(), 0.0f);
    std::fill(pickupSpring3Buf.begin(), pickupSpring3Buf.end(), 0.0f);
    pickupWriteIdx = 0;
    std::fill(outputDiffusionBuf.begin(), outputDiffusionBuf.end(), 0.0f);
    outputDiffusionWriteIdx = 0;
    inputHP.reset();
    inputLP.reset();
    dcBlock.reset();
    outputHP.reset();
    outputVoiceLP.reset();
    for (auto& lp : outputCeilingLP)
        lp.reset();
    outputLowContour.reset();
    outputBody.reset();
    outputPresence.reset();
    pickupImagePhase = 0.057f;
}

float SpringReverb::process(float in) noexcept
{
    const float voiced = inputLP.process(inputHP.process(in));
    float wet = 0.0f;
    float spring3Wet = 0.0f;
    for (size_t i = 0; i < springs.size(); ++i)
    {
        const float springWet = springs[i].process(voiced);
        wet += springWet;
        if (i == 3)
            spring3Wet = springWet;
    }

    // Secondary pickup modes bridge the quiet intervals between primary
    // round trips. They are feed-forward only: the calibrated decay and
    // stability of each spring loop remain unchanged.
    const auto pickupAt = [this](const std::vector<float>& buffer,
                                 int delay) noexcept
    {
        int index = pickupWriteIdx - delay;
        if (index < 0)
            index += (int)buffer.size();
        return buffer[(size_t)index];
    };
    pickupBuf[(size_t)pickupWriteIdx] = wet;
    pickupSpring3Buf[(size_t)pickupWriteIdx] = spring3Wet;
    wet = 0.90f * wet
        + 0.45f * pickupAt(pickupBuf, pickupTap8)
        - 0.18f * pickupAt(pickupBuf, pickupTap18)
        + 0.09f * pickupAt(pickupBuf, pickupTap28)
        + 0.45f * (pickupAt(pickupSpring3Buf, pickupSpring3Tap85)
                   - pickupAt(pickupSpring3Buf, pickupTap8))
        - 0.18f * (pickupAt(pickupSpring3Buf, pickupSpring3Tap20)
                   - pickupAt(pickupSpring3Buf, pickupTap18))
        + 0.09f * (pickupAt(pickupSpring3Buf, pickupSpring3Tap285)
                   - pickupAt(pickupSpring3Buf, pickupTap28));
    if (++pickupWriteIdx >= (int)pickupBuf.size())
        pickupWriteIdx = 0;

    // A short all-pass at the pickup diffuses coherent reflection clusters
    // without changing the tank's magnitude response or calibrated decay.
    constexpr float kOutputDiffusion = 0.70f;
    const float diffusionDelay =
        outputDiffusionBuf[(size_t)outputDiffusionWriteIdx];
    const float diffusedWet = diffusionDelay - kOutputDiffusion * wet;
    outputDiffusionBuf[(size_t)outputDiffusionWriteIdx] =
        wet + kOutputDiffusion * diffusedWet;
    if (++outputDiffusionWriteIdx >= (int)outputDiffusionBuf.size())
        outputDiffusionWriteIdx = 0;
    wet = diffusedWet;

    // Output trim is calibrated per channel. The wet path is mono and panned
    // later; measuring an L/R average here would overstate the spring level
    // because the former dual-mono tanks partially cancelled at center.
    float voicedWet = outputHP.process(dcBlock.process(0.96f * wet));
    voicedWet = outputVoiceLP.process(voicedWet);
    for (auto& lp : outputCeilingLP)
        voicedWet = lp.process(voicedWet);
    voicedWet = outputLowContour.process(voicedWet);
    const float springReturn =
        outputPresence.process(outputBody.process(voicedWet));

    // The pickup return carries a very quiet clock image of the tank signal.
    // Keeping it feed-forward preserves the calibrated decay and adds only the
    // measured upper-air sidebands around the 12 kHz carrier.
    const float pickupImage =
        0.00028f * springReturn * std::cos(pickupImagePhase);
    pickupImagePhase += pickupImagePhaseInc;
    if (pickupImagePhase >= kTwoPi)
        pickupImagePhase -= kTwoPi;
    return springReturn + pickupImage;
}

//==============================================================================
// TapeEchoDSP
//==============================================================================
constexpr float TapeEchoDSP::kHeadRatio[3];

float TapeEchoDSP::delayMsForRepeatRate(float v01) noexcept
{
    const float x  = clamp01(v01);
    const float x2 = x * x;
    // Monotonic endpoint-constrained fit to hosted head-1 arrivals at five
    // motor positions. A linear knob law was over 10 ms early at midpoint.
    const float slow01 = clamp01(
        1.0f - 2.14728717f * x2
             + 0.90911783f * x2 * x
             + 0.23816934f * x2 * x2);
    return kMinDelayMs + slow01 * (kMaxDelayMs - kMinDelayMs);
}

float TapeEchoDSP::repeatRateForDelayMs(float delayMs) noexcept
{
    const float target = clampF(delayMs, kMinDelayMs, kMaxDelayMs);
    float lo = 0.0f;
    float hi = 1.0f;
    for (int i = 0; i < 24; ++i)
    {
        const float mid = 0.5f * (lo + hi);
        if (delayMsForRepeatRate(mid) > target)
            lo = mid;
        else
            hi = mid;
    }
    return 0.5f * (lo + hi);
}

void TapeEchoDSP::prepare(double sampleRate, int /*maxBlockSize*/)
{
    fs = sampleRate;

    // Noise-modulator rate compensation; see kNoiseCalibrationFs in the header.
    // Exactly 1.0f at 48 kHz, so the calibrated render path is bit-identical.
    noiseRateComp = std::sqrt((float)(fs / kNoiseCalibrationFs));

    // Longest possible read: head 3 at the slowest motor speed, plus wow
    // headroom, plus interpolation guard.
    const float maxDelaySec = (kMaxDelayMs * 0.001f) * kHeadRatio[2] * 1.05f;
    const int   needed      = (int)std::ceil(maxDelaySec * fs) + 8;
    const int   tapeLen     = nextPowerOfTwo(needed);
    mask            = tapeLen - 1;
    maxDelaySamples = (float)(tapeLen - 8);
    writeIdx        = 0;

    for (auto& ch : channels)
    {
        ch.tape.assign((size_t)tapeLen, 0.0f);
        ch.preampDC.setSampleRate(fs, kDcBlockerCutoffHz);
        ch.recordHP.setCoeffs(Biquad::highPass(
            fs, safeBiquadFrequency(fs, 115.0f), 0.70710678f));
        const float initialMs = delayMsForRepeatRate(
            pRepeatRate.load(std::memory_order_relaxed));
        const float initialSlow01 = clamp01(
            (initialMs - kMinDelayMs) / (kMaxDelayMs - kMinDelayMs));
        const float initialVoicing = 1.05f + 0.08f * initialSlow01
            + 0.48f * initialSlow01 * (1.0f - initialSlow01);
        ch.speedLP.setCoeffs(Biquad::lowPass(
            fs, safeBiquadFrequency(
                fs, 7200.0f * kMinDelayMs / initialMs * initialVoicing),
            0.70710678f));
        constexpr float kButterworthQ[4] =
            { 0.50979558f, 0.60134489f, 0.89997622f, 2.56291545f };
        for (int i = 0; i < 4; ++i)
            ch.antiAliasLP[(size_t)i].setCoeffs(Biquad::lowPass(
                fs, safeBiquadFrequency(
                    fs, 11000.0f
                        + 2000.0f * std::pow(1.0f - initialSlow01, 3.0f)),
                kButterworthQ[i]));
        // The direct monitor path retains full midrange level while its
        // coupling network and output amplifier gently soften the extremes.
        // These shelves are outside the echo/reverb sends.
        ch.dryLowShelf.setCoeffs(Biquad::shelf(
            fs, safeBiquadFrequency(fs, 46.3316f),
            -6.17579f, 0.492074f, /*high=*/false));
        ch.dryHighShelf.setCoeffs(Biquad::shelf(
            fs, safeBiquadFrequency(fs, 13762.47f),
            -6.13329f, 0.492299f, /*high=*/true));
    }
    spring.prepare(fs, 1.0f);

    noiseLP.setCutoff(2.0f, fs);
    flutterBand.set(kFlutterNoiseHz, kFlutterNoiseQ, fs);
    flutterBandLP.setCutoff(kFlutterNoiseLPHz, fs);
    wowInc     = kTwoPi * kWowHz     / (float)fs;
    flutterInc = kTwoPi * kFlutterMaxHz / (float)fs;
    // The record VU owns the needle ballistics. Attack and release are split so
    // the return can follow the reference more closely without making the rise
    // too eager. The UI deliberately adds no second smoothing stage.
    constexpr float kVuAttackSeconds = 0.225f;
    constexpr float kVuReleaseSeconds = 0.2f;
    meterVuAttackCoeff = 1.0f - std::exp(
        -1.0f / (kVuAttackSeconds * (float)fs));
    meterVuReleaseCoeff = 1.0f - std::exp(
        -1.0f / (kVuReleaseSeconds * (float)fs));
    meterPeakDecayPerSample = std::exp(-1.0f / (0.3f * (float)fs));
    recordEnvelopeAttack =
        1.0f - std::exp(-1.0f / (0.00005f * (float)fs));
    recordEnvelopeRelease = std::exp(-1.0f / (0.12f * (float)fs));
    meterVu = 0.0f;
    meterPeak = 0.0f;
    recordVu.store(0.0f, std::memory_order_relaxed);
    recordPeak.store(0.0f, std::memory_order_relaxed);

    delaySmoother.prepare(fs, 0.35f);        // motor/capstan inertia
    intensitySmoother.prepare(fs, 0.03f);
    for (auto& g : headGain)
        g.prepare(fs, 0.015f);
    reverbSendSmoother.prepare(fs, 0.015f);
    echoLevelSmoother.prepare(fs, 0.02f);
    reverbLevelSmoother.prepare(fs, 0.02f);
    dryLevelSmoother.prepare(fs, 0.02f);
    outputVolumeSmoother.prepare(fs, 0.02f);
    echoPanSmoother.prepare(fs, 0.02f);
    reverbPanSmoother.prepare(fs, 0.02f);
    inputSendSmoother.prepare(fs, 0.01f);
    wetSoloSmoother.prepare(fs, 0.01f);
    mixSmoother.prepare(fs, 0.02f);
    driveSmoother.prepare(fs, 0.02f);
    wowFlutterSmoother.prepare(fs, 0.05f);
    powerSmoother.prepare(fs, 0.03f);
    ageSmoother.prepare(fs, 0.10f);
    hissVoice.setCutoff(4500.0f, fs);
    wobbleLP.setCutoff(5.0f, fs);

    // Snap smoothers to current parameter values so prepare() never glides.
    const auto& m = kModeTable[pMode.load(std::memory_order_relaxed) - 1];
    const float t = delayMsForRepeatRate(
        pRepeatRate.load(std::memory_order_relaxed));
    delaySmoother.snap(t * 0.001f * (float)fs);
    intensitySmoother.snap(pIntensity.load(std::memory_order_relaxed));
    headGain[0].snap(m.h1);
    headGain[1].snap(m.h2);
    headGain[2].snap(m.h3);
    reverbSendSmoother.snap(m.reverb);
    echoLevelSmoother.snap(pEchoLevel.load(std::memory_order_relaxed));
    reverbLevelSmoother.snap(pReverbLevel.load(std::memory_order_relaxed));
    dryLevelSmoother.snap(pDryLevel.load(std::memory_order_relaxed));
    outputVolumeSmoother.snap(
        pOutputVolume.load(std::memory_order_relaxed));
    echoPanSmoother.snap(pEchoPan.load(std::memory_order_relaxed));
    reverbPanSmoother.snap(pReverbPan.load(std::memory_order_relaxed));
    inputSendSmoother.snap(pInputSend.load(std::memory_order_relaxed));
    wetSoloSmoother.snap(pWetSolo.load(std::memory_order_relaxed));
    mixSmoother.snap(pMix.load(std::memory_order_relaxed));
    driveSmoother.snap(pInputGain.load(std::memory_order_relaxed));
    wowFlutterSmoother.snap(pWowFlutter.load(std::memory_order_relaxed));
    powerSmoother.snap(1.0f - pBypass.load(std::memory_order_relaxed));
    ageSmoother.snap(pTapeAge.load(std::memory_order_relaxed));
    lastPlaybackCutoff = -1.0f;
    lastAntiAliasCutoff = -1.0f;

    lastBass = lastTreble = -999.0f; // force shelf recompute
    reset();
}

void TapeEchoDSP::reset()
{
    for (auto& ch : channels)
    {
        std::fill(ch.tape.begin(), ch.tape.end(), 0.0f);
        ch.preampDC.reset();
        ch.upA.reset(); ch.downA.reset();
        ch.upB.reset(); ch.downB.reset();
        ch.recordHP.reset();
        ch.speedLP.reset();
        for (auto& lp : ch.antiAliasLP)
            lp.reset();
        ch.bassShelf.reset();
        ch.trebleShelf.reset();
        ch.dryLowShelf.reset();
        ch.dryHighShelf.reset();
        ch.springCleanDelay.fill(0.0f);
        ch.springCleanDelayWriteIdx = 0;
        ch.recordEnvelope = 0.0f;
        ch.loopEnvelope = 0.0f;
        ch.magneticEnvelope = 0.0f;
    }
    spring.reset();
    noiseLP.reset();
    flutterBand.reset();
    flutterBandLP.reset();
    hissVoice.reset();
    wobbleLP.reset();
    writeIdx = 0;
    wowPhase = flutterPhase = 0.0f;
    meterVu = 0.0f;
    meterPeak = 0.0f;
    recordVu.store(0.0f, std::memory_order_relaxed);
    recordPeak.store(0.0f, std::memory_order_relaxed);
    // Initial cartridge position is deterministic. Hosted captures place the
    // first head-1 splice at about 7.0 s over most of the motor range and
    // 9.1 s at the extreme fast end (after renderer pre-roll).
    spliceSamplesToHead1 = 7.58f * (float)fs;
    spliceClockStarted = false;
    lastClearRequest =
        pClearRequest.load(std::memory_order_relaxed);
}

float TapeEchoDSP::readTape(const std::vector<float>& buf, float delaySamples) const noexcept
{
    const float rp   = (float)writeIdx - delaySamples;
    int         i0   = (int)std::floor(rp);
    const float frac = rp - (float)i0;

    i0 &= mask; // power-of-two wrap (two's complement handles negative i0)
    const int im1 = (i0 - 1) & mask;
    const int i1  = (i0 + 1) & mask;
    const int i2  = (i0 + 2) & mask;

    return hermite(frac, buf[(size_t)im1], buf[(size_t)i0],
                         buf[(size_t)i1],  buf[(size_t)i2]);
}

void TapeEchoDSP::refreshBlockRateControls()
{
    const uint32_t clearRequest =
        pClearRequest.load(std::memory_order_relaxed);
    if (clearRequest != lastClearRequest)
    {
        // POWER off clears the circulating tape and spring tank. reset() only
        // clears preallocated state, so it is allocation-free on this thread.
        reset();
        lastClearRequest = clearRequest;
    }

    // Snapshot atomics once per block; smoothers handle the rest per sample.
    const auto& m = kModeTable[pMode.load(std::memory_order_relaxed) - 1];
    headGain[0].setTarget(m.h1);
    headGain[1].setTarget(m.h2);
    headGain[2].setTarget(m.h3);
    reverbSendSmoother.setTarget(m.reverb);

    const float rate = pRepeatRate.load(std::memory_order_relaxed);
    if (!spliceClockStarted)
    {
        const float initialSpliceSeconds =
            7.58f + 2.07f * std::pow(rate, 6.0f);
        spliceSamplesToHead1 =
            initialSpliceSeconds * (float)fs;
        spliceClockStarted = true;
    }
    const float tMs  = delayMsForRepeatRate(rate);
    delaySmoother.setTarget(tMs * 0.001f * (float)fs);

    intensitySmoother.setTarget(pIntensity.load(std::memory_order_relaxed));
    echoLevelSmoother.setTarget(pEchoLevel.load(std::memory_order_relaxed));
    reverbLevelSmoother.setTarget(pReverbLevel.load(std::memory_order_relaxed));
    dryLevelSmoother.setTarget(pDryLevel.load(std::memory_order_relaxed));
    outputVolumeSmoother.setTarget(
        pOutputVolume.load(std::memory_order_relaxed));
    echoPanSmoother.setTarget(pEchoPan.load(std::memory_order_relaxed));
    reverbPanSmoother.setTarget(pReverbPan.load(std::memory_order_relaxed));
    inputSendSmoother.setTarget(pInputSend.load(std::memory_order_relaxed));
    wetSoloSmoother.setTarget(pWetSolo.load(std::memory_order_relaxed));
    mixSmoother.setTarget(pMix.load(std::memory_order_relaxed));
    driveSmoother.setTarget(pInputGain.load(std::memory_order_relaxed));
    wowFlutterSmoother.setTarget(pWowFlutter.load(std::memory_order_relaxed));
    powerSmoother.setTarget(1.0f - pBypass.load(std::memory_order_relaxed));
    ageSmoother.setTarget(pTapeAge.load(std::memory_order_relaxed));

    // Playback bandwidth is proportional to tape speed. Worn tape narrows it
    // further. This replaces the fixed-frequency poles that made slow and fast
    // repeats incorrectly share the same spectrum.
    {
        const float age = ageSmoother.value();
        const float slow01 = clamp01(
            (tMs - kMinDelayMs) / (kMaxDelayMs - kMinDelayMs));
        const float voicing = 1.05f + 0.08f * slow01
                            + 0.48f * slow01 * (1.0f - slow01);
        const float cutoff = safeBiquadFrequency(
            fs, 7200.0f * kMinDelayMs / tMs * voicing
              * (1.0f - 0.32f * age));
        if (cutoff != lastPlaybackCutoff)
        {
            lastPlaybackCutoff = cutoff;
            for (int c = 0; c < kMaxChannels; ++c)
                channels[(size_t)c].speedLP.setCoeffs(
                    Biquad::lowPass(fs, cutoff, 0.70710678f));
        }
        const float antiAliasCutoff = safeBiquadFrequency(
            fs,
            (11000.0f + 2000.0f * std::pow(1.0f - slow01, 3.0f))
            * (1.0f - 0.10f * age));
        if (antiAliasCutoff != lastAntiAliasCutoff)
        {
            lastAntiAliasCutoff = antiAliasCutoff;
            constexpr float kButterworthQ[4] =
                { 0.50979558f, 0.60134489f, 0.89997622f, 2.56291545f };
            for (int c = 0; c < kMaxChannels; ++c)
                for (int i = 0; i < 4; ++i)
                    channels[(size_t)c].antiAliasLP[(size_t)i].setCoeffs(
                        Biquad::lowPass(fs, antiAliasCutoff, kButterworthQ[i]));
        }
    }

    // Echo-path tone shelves: recompute only when the knobs actually moved.
    const float bass   = pBass.load(std::memory_order_relaxed);
    const float treble = pTreble.load(std::memory_order_relaxed);
    if (bass != lastBass || treble != lastTreble)
    {
        lastBass   = bass;
        lastTreble = treble;
        // Configure ALL channels (like the age-retune block above), not just the
        // current numChannels: a later mono→stereo switch without a knob move
        // would otherwise leave channels[1]'s shelves at their passthrough
        // default while channels[0] is shelved — an L/R mismatch.
        for (int c = 0; c < kMaxChannels; ++c)
        {
            const float bassAmount = std::abs(bass);
            const float bassGainDb = std::copysign(
                bassAmount * (11.31f + 6.06f * bassAmount), bass);
            const float bassFrequency =
                67.55f + 86.70f * bassAmount;
            const float bassQ =
                1.074f - 0.580f * bassAmount;
            channels[(size_t)c].bassShelf.setCoeffs(
                Biquad::shelf(
                    fs, safeBiquadFrequency(fs, bassFrequency),
                    bassGainDb, bassQ, /*high=*/false));

            const float trebleAmount = std::abs(treble);
            const float trebleGainDb = std::copysign(
                trebleAmount * (4.375f + 13.09f * trebleAmount),
                treble);
            // The passive boost and cut arms have different turnover laws.
            // Hosted impulse sweeps place the half-travel corners at 991 Hz
            // (boost) and 1441 Hz (cut), converging near 3 kHz at full travel.
            const float trebleFrequency = treble >= 0.0f
                ? 2850.8f * std::pow(trebleAmount, 1.525f)
                : 3440.8f * std::pow(trebleAmount, 1.255f);
            const float trebleQ =
                0.545f - 0.102f * trebleAmount;
            channels[(size_t)c].trebleShelf.setCoeffs(
                Biquad::shelf(
                    fs, safeBiquadFrequency(
                        fs, std::max(trebleFrequency, 20.0f)),
                    trebleGainDb, trebleQ, /*high=*/true));
        }
    }
}

void TapeEchoDSP::processBlock(const float* const* inputs, float* const* outputs,
                                int numChannels, int numSamples) noexcept
{
    if (numSamples <= 0 || mask == 0)
        return;

    ScopedFlushDenormals ftz;

    numChannels = clampInt(numChannels, 1, kMaxChannels);
    refreshBlockRateControls();

    constexpr float kVuSineCalibration = 1.110720735f;

    for (int n = 0; n < numSamples; ++n)
    {
        //--- shared per-sample control signals (one motor, one tape) ----------
        const float nominalT1 = delaySmoother.next();
        const float nominalMs = nominalT1 * (1000.0f / (float)fs);
        const float slow01 = clamp01(
            (nominalMs - kMinDelayMs) / (kMaxDelayMs - kMinDelayMs));
        // The recurring transport cycle speeds up with tape velocity: hosted
        // 3150 Hz captures measured 2.00 Hz at midpoint and 3.857 Hz at fast.
        flutterInc = kTwoPi * kFlutterMaxHz
                   * (kMinDelayMs / nominalMs) / (float)fs;

        wowPhase += wowInc;
        if (wowPhase > kTwoPi)     wowPhase     -= kTwoPi;
        flutterPhase += flutterInc;
        if (flutterPhase > kTwoPi) flutterPhase -= kTwoPi;

        // The reference transport always moves, even in its freshest state.
        // The user control adds an intentionally wider creative range.
        const float age = ageSmoother.next();
        const float wf  = 1.0f + 1.5f * wowFlutterSmoother.next() + 0.20f * age;
        // slow playback-level wobble (worn pinch roller / dropout precursor);
        // exactly 1.0 at age 0.
        const float wobble =
            1.0f + age * 0.11f
                 * wobbleLP.process(ageRand() * noiseRateComp);
        // hiss recorded onto the tape: regenerates with intensity like the
        // hardware. Voiced dark, exactly 0.0 at age 0.
        const float hissL =
            age * 0.0000079f
                * hissVoice.process(ageRand() * noiseRateComp);
        const float flutterDepth = 0.00237f + 0.00242f * slow01;
        const float flutterWave = std::sin(flutterPhase)
                                + 0.088f * std::sin(2.0f * flutterPhase);
        // Scrape flutter: band noise around 6 Hz. Age worsens it steeply on the
        // reference, far faster than the loop-rate wow it rides on.
        const float flutterNoiseGain =
            kFlutterNoiseDepth
            * (1.0f + age * (kFlutterAgeLin + kFlutterAgeSq * age));
        const float flutterNoise =
            flutterNoiseGain
            * flutterBandLP.process(
                  flutterBand.process(flutterRand() * noiseRateComp));
        const float mod = wf * (kWowDepth * std::sin(wowPhase)
                              + flutterDepth * flutterWave
                              + flutterNoise
                              + kNoiseDepth
                                    * noiseLP.process(frand() * noiseRateComp));

        // The oversampled preamp delays the tape feed by a fixed group delay;
        // subtract it AFTER the head-ratio scaling so all three heads stay at
        // their exact mechanical times (subtracting from T would scale the
        // compensation by 1.9x/2.75x on the far heads).
        const float t1 = nominalT1 * (1.0f + mod);
        const float d1 = clampF(t1                 - kPreampLatencySamples, 4.0f, maxDelaySamples);
        const float d2 = clampF(t1 * kHeadRatio[1] - kPreampLatencySamples, 4.0f, maxDelaySamples);
        const float d3 = clampF(t1 * kHeadRatio[2] - kPreampLatencySamples, 4.0f, maxDelaySamples);

        const float g1 = headGain[0].next() * kHeadTrim[0];
        const float g2 = headGain[1].next() * kHeadTrim[1];
        const float g3 = headGain[2].next() * kHeadTrim[2];

        // Intensity mapped past unity loop gain: > ~0.75 the loop exceeds
        // unity for small signals and the in-loop tape saturation clamps it
        // into stable, warm self-oscillation.
        const float fbGain =
            feedbackGainFromControl(intensitySmoother.next());
        const float revSend   = reverbSendSmoother.next();
        // Level controls use measured analog-style tapers. The effect-volume
        // curve includes a linear term, so low settings remain useful.
        const float echoRaw   = echoLevelSmoother.next();
        const float echoLvl   = echoGainFromControl(echoRaw);
        const float revRaw    = reverbLevelSmoother.next();
        const float revLvl    = echoGainFromControl(revRaw) * revSend;
        const float dryLvl    = dryLevelSmoother.next();
        const float mix       = mixSmoother.next();
        // Unity-overlap balance law: both paths are unity at the 50% default,
        // preserving the calibrated parallel mix (and old sessions that do not
        // contain kParamMix). Moving toward either endpoint fades only the
        // opposite path; 0% is dry-only and 100% is wet-only.
        const float dryMixGain = std::min(1.0f, 2.0f * (1.0f - mix));
        const float wetMixGain = std::min(1.0f, 2.0f * mix);
        const float outputVolume = outputVolumeSmoother.next();
        // Hosted output trim is linear in decibels: -20 dB at zero, unity at
        // midpoint, and +20 dB at maximum.
        const float outputGain = std::pow(
            10.0f, 2.0f * outputVolume - 1.0f);
        const float driveKnob = driveSmoother.next();
        const float inputGain = inputGainFromControl(driveKnob);
        // Equivalent-level hosted sweeps at multiple knob positions collapse
        // onto one transfer curve: the input control is a gain taper feeding
        // a fixed record-stage nonlinearity, not a second distortion control.
        constexpr float kInputStageDrive = 2.65f;
        constexpr float kInputStageTrim  = 0.459f;
        const float power     = powerSmoother.next(); // 0 = bypassed

        //--- mono effect path -------------------------------------------------
        // The hosted unit sums its stereo input before both wet paths. A
        // left-only impulse produces sample-identical L/R wet signals at
        // center, while duplicated stereo material drives the nonlinear
        // record stage about 6 dB harder. The 0.5 average here preserves the
        // transfer calibration made with duplicated stereo stimuli; the
        // measured pan matrix below restores the corresponding x2 wet gain.
        const float inL = inputs[0][n];
        const float inR = numChannels > 1 ? inputs[1][n] : inL;
        const float inputSend = inputSendSmoother.next();
        const float monoInput =
            power * (numChannels > 1 ? 0.5f * (inL + inR) : inL);
        Channel& ch = channels[0];
        const float drivenInput = monoInput * inputGain;
        const float tapeInput = inputSend * drivenInput;

        // The hosted record path has a broad tape-compression knee above
        // nominal level and asymptotically reaches about 4.5 dB of gain
        // reduction. Most of that reduction happens before the saturator
        // (limiting harmonic growth); the balance is clean record gain.
        const float tapeMagnitude =
            tapeInput < 0.0f ? -tapeInput : tapeInput;
        if (tapeMagnitude > ch.recordEnvelope)
            ch.recordEnvelope += recordEnvelopeAttack
                               * (tapeMagnitude - ch.recordEnvelope);
        else
            ch.recordEnvelope *= recordEnvelopeRelease;
        const float over = std::max(ch.recordEnvelope - 0.12f, 0.0f);
        const float reductionDb = 4.5f * (1.0f - std::exp(-2.7f * over));
        const float compression =
            std::pow(10.0f, -reductionDb * (1.0f / 20.0f));
        // Below the nominal tape threshold, gain reduction is clean and
        // leaves the measured harmonic curve intact. Above it, a growing
        // fraction moves ahead of the shaper so THD approaches the
        // measured high-level plateau instead of climbing without bound.
        const float preCompression01 = clamp01(
            (ch.recordEnvelope - 0.45f) * (1.0f / 0.75f));
        const float preExponent =
            1.2f * std::pow(preCompression01, 0.8f);
        const float postExponent =
            std::max(1.0f - 0.3f * preExponent, 0.6f);
        const float preDriveGain =
            std::pow(compression, preExponent);
        const float highLevel01 = clamp01(
            (ch.recordEnvelope - 0.65f) * (1.0f / 0.59f));
        const float highLevelTrim = std::pow(
            10.0f, -1.1f * std::sqrt(highLevel01) * (1.0f / 20.0f));
        const float postDriveGain =
            std::pow(compression, postExponent) * highLevelTrim;

        // FET preamp front-end, saturated at 4x to keep fold-back
        // products out of the echo passband.
        const float pre = ch.preampDC.process(
                              preampOversampled(
                                  ch, tapeInput * preDriveGain,
                                  kInputStageDrive))
                          * kInputStageTrim * postDriveGain;
        // The spring send is taken entirely ahead of the record-stage shaper
        // and its compressor. A drive ladder measured through the hosted
        // spring (impulse and pink burst, 48 dB of input range, reverb level
        // fixed) shows its tank gain constant to 0.00 dB: the hosted spring
        // send is linear. A partly shaped send instead compressed by up to
        // 3.9 dB across that ladder, which is what made the sparse-impulse and
        // dense-program level errors disagree. Only the preamp-latency
        // alignment below is kept, so the first arrival is unchanged.
        float cleanReadPos =
            (float)ch.springCleanDelayWriteIdx - kPreampLatencySamples;
        if (cleanReadPos < 0.0f)
            cleanReadPos += (float)ch.springCleanDelay.size();
        const int cleanRead0 = (int)cleanReadPos;
        const int cleanRead1 =
            (cleanRead0 + 1) % (int)ch.springCleanDelay.size();
        const float cleanReadFrac = cleanReadPos - (float)cleanRead0;
        const float delayedTapeInput =
            ch.springCleanDelay[(size_t)cleanRead0]
            + cleanReadFrac
                * (ch.springCleanDelay[(size_t)cleanRead1]
                   - ch.springCleanDelay[(size_t)cleanRead0]);
        // Input Send is the original Echo/Normal "dub" switch. It interrupts
        // the tape record feed, but not the independent spring-reverb path.
        ch.springCleanDelay[(size_t)ch.springCleanDelayWriteIdx] = drivenInput;
        if (++ch.springCleanDelayWriteIdx >=
            (int)ch.springCleanDelay.size())
            ch.springCleanDelayWriteIdx = 0;
        // Send trim, at measured parameter-matched parity. The former 0.60
        // shaped / 0.40 clean blend had a small-signal gain of 1.1385 x
        // tapeInput and read 1.60 dB hot against the hosted tank once its own
        // compression was taken out of the reading, which puts the linear
        // send at 0.9470 x tapeInput.
        constexpr float kCleanSpringSendGain =
            0.77855f * kInputStageDrive * kInputStageTrim;
        const float springPre =
            kCleanSpringSendGain * delayedTapeInput;

        // Three playback heads off the shared tape.
        // The joined ends of the physical tape create a recurring dropout.
        // Its loop period is 9.23 s at the fastest motor setting and scales
        // inversely with tape speed. A narrow join is followed by a broader
        // loss region; age determines how audible both become.
        const float speedScale = nominalMs / kMinDelayMs;
        float spliceDepthDb;
        if (age <= 0.5f)
            spliceDepthDb = 1.34f + 4.0f * age;
        else
        {
            const float old01 = (age - 0.5f) * 2.0f;
            spliceDepthDb =
                3.34f + 9.82f * std::pow(old01, 1.5f);
        }
        const auto spliceGain = [&](float samplesToEvent) noexcept
        {
            const float secondsToEvent =
                samplesToEvent / (float)fs;
            const float narrow =
                std::exp(-0.5f * std::pow(
                    secondsToEvent / 0.0075f, 2.0f));
            const float broad =
                std::exp(-0.5f * std::pow(
                    secondsToEvent / (0.080f * speedScale), 2.0f));
            const float shape = 0.65f * narrow + 0.35f * broad;
            return std::pow(10.0f, -spliceDepthDb * shape * 0.05f);
        };
        const float h1 = readTape(ch.tape, d1)
                       * spliceGain(spliceSamplesToHead1);
        const float h2 = readTape(ch.tape, d2)
                       * spliceGain(
                           spliceSamplesToHead1 + (d2 - d1));
        const float h3 = readTape(ch.tape, d3)
                       * spliceGain(
                           spliceSamplesToHead1 + (d3 - d1));
        const float headSum = (h1 * g1 + h2 * g2 + h3 * g3) * wobble;

        // Record chain: program + feedback -> head EQ -> magnetic
        // saturation -> tape. Everything here is inside the loop, so
        // repeats darken and compress cumulatively.
        const float loopDrive = hardKnee(
            fbGain * softClip(headSum),
            kLoopCeilingKnee, kLoopCeiling);
        // Meter point: after Input Volume, with regeneration injected just
        // before detection. This deliberately ignores Input Send.
        const float recordMeterSignal = drivenInput + power * loopDrive;
        const float recordMeterMagnitude = std::abs(recordMeterSignal);
        // Average-responding VU, calibrated so a sine's average rectified value
        // reads its RMS amplitude. The peak lamp uses the unsmoothed magnitude.
        const float vuCoeff = recordMeterMagnitude > meterVu
                            ? meterVuAttackCoeff
                            : meterVuReleaseCoeff;
        meterVu += vuCoeff * (recordMeterMagnitude - meterVu);
        meterPeak = std::max(
            recordMeterMagnitude,
            meterPeak * meterPeakDecayPerSample);
        // The low-flux gate below is a function of record flux, and the record
        // head sees program AND regeneration. Gating it on the program alone
        // left a runaway loop pinned in the low-flux region of the tape curve,
        // where the low-level harmonic term is still fully active, folds the
        // transfer back on itself and caps the tape at 0.130 - the whole
        // measured 11 dB self-oscillation shortfall. The same flux driven from
        // the input reached the matched plateau, because the input envelope
        // opened the gate. Only that gate is moved onto total flux: the hot /
        // very-hot Chebyshev terms and the magnetic makeup below stay on the
        // program envelope, because they were fitted as program-level makeup
        // laws. Feeding them total flux as well measured 0.5 dB worse on the
        // factory octave-band gate (2.66 against a 2.50 limit) - they are
        // harmonic generators, and opening them on program-plus-regeneration
        // put content in bands the reference does not have.
        const float loopMagnitude = loopDrive < 0.0f ? -loopDrive : loopDrive;
        if (loopMagnitude > ch.loopEnvelope)
            ch.loopEnvelope += recordEnvelopeAttack
                             * (loopMagnitude - ch.loopEnvelope);
        else
            ch.loopEnvelope *= recordEnvelopeRelease;
        // Referred back through the record-amp small-signal gain so the
        // measured gate thresholds - all calibrated in input units against the
        // program envelope - keep their meaning. Exactly zero with the
        // feedback control at minimum, so the harmonic calibration is
        // untouched there.
        constexpr float kRecordDriveToInput =
            1.0f / (kInputStageDrive * kInputStageTrim);
        const float fluxEnvelope =
            ch.recordEnvelope + ch.loopEnvelope * kRecordDriveToInput;

        const float recorded = ch.recordHP.process(
            ch.speedLP.process(
                pre + hissL + loopDrive));
        // The tape's low-level magnetisation curve is more strongly curved
        // than the record preamp, but its slope remains positive at overload.
        // This bounded cubic term raises the odd-harmonic ladder without
        // changing small-signal loop gain or the separately calibrated spring
        // send.
        const float recorded2 = recorded * recorded;
        // At low flux the measured odd harmonics fall much more slowly than a
        // cubic polynomial can produce. A regularized, scale-covariant p=2.15
        // term restores that quiet harmonic floor, then fades out before the
        // main magnetic curve reaches its already-matched -18 dB operating
        // point.
        const float lowFloor01 = clamp01(
            (fluxEnvelope - 0.02f) * (1.0f / 0.07f));
        const float lowFloorSmooth =
            lowFloor01 * lowFloor01 * (3.0f - 2.0f * lowFloor01);
        const float lowFloorFade = 1.0f - lowFloorSmooth;
        const float lowFloor = 1.325f * lowFloorFade * recorded
            * std::pow(recorded2 + 0.006f * 0.006f, 0.575f);
        const float magneticInput =
            recorded * (1.0f - 5.0f * recorded2
                        / (1.0f + 8.0f * recorded2))
            - lowFloor;
        const float baseTape = softClip(magneticInput);
        const float baseMagnitude = std::abs(baseTape);
        if (baseMagnitude > ch.magneticEnvelope)
            ch.magneticEnvelope = baseMagnitude;
        else
            ch.magneticEnvelope *= recordEnvelopeRelease;

        // At high record flux the hosted tape adds a steep odd-harmonic
        // ladder while its fundamental has already reached a level plateau.
        // Chebyshev terms supply those missing harmonics without changing the
        // steady-state fundamental. Their envelope-gated, normalized sum is
        // bounded to roughly thirteen percent of the tape signal and remains
        // zero through the already-matched nominal range.
        const float magneticAmplitude =
            std::max(ch.magneticEnvelope, 1.0e-4f);
        const float z = clampF(
            baseTape / magneticAmplitude, -1.0f, 1.0f);
        const float z2 = z * z;
        const float z3 = z2 * z;
        const float z5 = z3 * z2;
        const float z7 = z5 * z2;
        const float hot01 = clamp01(
            (ch.recordEnvelope - 0.26f) * (1.0f / 0.21f));
        const float hot =
            hot01 * hot01 * (3.0f - 2.0f * hot01);
        const float veryHot01 = clamp01(
            (ch.recordEnvelope - 0.52f) * (1.0f / 0.16f));
        const float veryHot =
            veryHot01 * veryHot01 * (3.0f - 2.0f * veryHot01);
        const float t3 = 4.0f * z3 - 3.0f * z;
        const float t5 = 16.0f * z5 - 20.0f * z3 + 5.0f * z;
        const float t7 =
            64.0f * z7 - 112.0f * z5 + 56.0f * z3 - 7.0f * z;
        const float c3 = -0.0983f * hot + 0.0209f * veryHot;
        const float c5 = -0.0195f * hot - 0.0255f * veryHot;
        const float c7 = 0.0f;
        float toTape = baseTape + magneticAmplitude
            * (c3 * t3 + c5 * t5 + c7 * t7);
        // Restore the measured level plateau without undoing the magnetic
        // curvature. The slowly tracked envelope makes this a program-level
        // makeup law (not a sample-by-sample waveshaper), so harmonic ratios
        // stay intact. It is inactive below ordinary musical peaks.
        const float magneticMakeup01 = clamp01(
            (ch.recordEnvelope - 0.18f) * (1.0f / 0.82f));
        const float magneticMakeupDb =
            1.8f * std::sqrt(std::sqrt(magneticMakeup01))
            + 0.46f * hot + 0.06f * veryHot;
        toTape *= std::pow(10.0f, magneticMakeupDb * (1.0f / 20.0f));
        for (auto& lp : ch.antiAliasLP)
            toTape = lp.process(toTape);
        ch.tape[(size_t)writeIdx] = toTape;

        // Echo output path only: bass/treble shelves (dry and reverb
        // are unaffected, matching the hardware layout).
        const float echoWet =
            ch.trebleShelf.process(ch.bassShelf.process(headSum));

        // Spring tank is fed from the same mono preamp signal.
        const float rev = spring.process(springPre * revSend);

        // Both wet-path pan controls are measured linear amplitude laws.
        // In stereo, the x2 factor pairs with the input average above:
        // center (0.5/0.5) preserves the calibrated wet level, while a hard
        // pan is 6.02 dB louder in its destination channel.
        const float echoPan = echoPanSmoother.next();
        const float reverbPan = reverbPanSmoother.next();
        const float dryEnable = 1.0f - wetSoloSmoother.next();
        for (int c = 0; c < numChannels; ++c)
        {
            const float in = c == 0 ? inL : inR;
            Channel& outputChannel = channels[(size_t)c];
            const float dry = outputChannel.dryHighShelf.process(
                outputChannel.dryLowShelf.process(in));
            const float echoPanGain = numChannels == 1
                ? 1.0f : 2.0f * (c == 0 ? 1.0f - echoPan : echoPan);
            const float reverbPanGain = numChannels == 1
                ? 1.0f : 2.0f * (c == 0 ? 1.0f - reverbPan : reverbPan);
            const float dryPath = dryEnable * dryLvl * inputGain * dry;
            const float wetPath = echoLvl * echoWet * echoPanGain
                                + revLvl * rev * reverbPanGain;
            const float mixed = dryMixGain * dryPath + wetMixGain * wetPath;
            // POWER off: clean passthrough. The wet state is cleared on the
            // rising bypass edge when POWER is switched off, with the clear
            // applied in the next block. Re-engaging starts with an empty
            // tape loop.
            outputs[c][n] = in + power * (outputGain * mixed - in);

        }

        writeIdx = (writeIdx + 1) & mask;
        spliceSamplesToHead1 -= 1.0f;
        const float spliceTailSamples =
            0.8f * speedScale * (float)fs;
        if (spliceSamplesToHead1 < -spliceTailSamples)
        {
            const float loopPeriodSamples =
                9.23f * speedScale * (float)fs;
            spliceSamplesToHead1 += loopPeriodSamples;
        }
    }

    recordVu.store(kVuSineCalibration * meterVu, std::memory_order_relaxed);
    recordPeak.store(meterPeak, std::memory_order_relaxed);
}

} // namespace duskaudio
