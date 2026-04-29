#include "DattorroTank.h"
#include "DspUtils.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// -----------------------------------------------------------------------
// DelayLine helpers

void DattorroTank::DelayLine::allocate (int maxSamples)
{
    int bufSize = DspUtils::nextPowerOf2 (maxSamples + 1);
    buffer.assign (static_cast<size_t> (bufSize), 0.0f);
    mask = bufSize - 1;
    writePos = 0;
}

void DattorroTank::DelayLine::clear()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

float DattorroTank::DelayLine::readInterpolated (float delaySamples) const
{
    float readPos = static_cast<float> (writePos) - delaySamples;
    int intIdx = static_cast<int> (std::floor (readPos));
    float frac = readPos - static_cast<float> (intIdx);
    return DspUtils::cubicHermite (buffer.data(), mask, intIdx, frac);
}

// -----------------------------------------------------------------------
// Allpass helpers

void DattorroTank::Allpass::allocate (int maxSamples)
{
    int bufSize = DspUtils::nextPowerOf2 (maxSamples + 1);
    buffer.assign (static_cast<size_t> (bufSize), 0.0f);
    mask = bufSize - 1;
    writePos = 0;
}

void DattorroTank::Allpass::clear()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

// -----------------------------------------------------------------------
// Constructor

DattorroTank::DattorroTank()
{
    leftTank_.ap1BaseDelay  = kLeftAP1Base;
    leftTank_.delay1BaseDelay = kLeftDel1Base;
    leftTank_.ap2BaseDelay  = kLeftAP2Base;
    leftTank_.delay2BaseDelay = kLeftDel2Base;

    rightTank_.ap1BaseDelay  = kRightAP1Base;
    rightTank_.delay1BaseDelay = kRightDel1Base;
    rightTank_.ap2BaseDelay  = kRightAP2Base;
    rightTank_.delay2BaseDelay = kRightDel2Base;

    // Density cascade base delays
    for (int i = 0; i < kNumDensityAPs; ++i)
    {
        leftTank_.densityAPBase[i]  = kLeftDensityAPBase[i];
        rightTank_.densityAPBase[i] = kRightDensityAPBase[i];
    }
}

// -----------------------------------------------------------------------

void DattorroTank::setHallScale (bool enable)
{
    if (enable)
    {
        leftTank_.ap1BaseDelay     = kLeftAP1BaseHall;
        leftTank_.delay1BaseDelay  = kLeftDel1BaseHall;
        leftTank_.ap2BaseDelay     = kLeftAP2BaseHall;
        leftTank_.delay2BaseDelay  = kLeftDel2BaseHall;
        rightTank_.ap1BaseDelay    = kRightAP1BaseHall;
        rightTank_.delay1BaseDelay = kRightDel1BaseHall;
        rightTank_.ap2BaseDelay    = kRightAP2BaseHall;
        rightTank_.delay2BaseDelay = kRightDel2BaseHall;
        for (int i = 0; i < kNumDensityAPs; ++i)
        {
            leftTank_.densityAPBase[i]  = kLeftDensityAPBaseHall[i];
            rightTank_.densityAPBase[i] = kRightDensityAPBaseHall[i];
        }
    }
    else
    {
        leftTank_.ap1BaseDelay     = kLeftAP1Base;
        leftTank_.delay1BaseDelay  = kLeftDel1Base;
        leftTank_.ap2BaseDelay     = kLeftAP2Base;
        leftTank_.delay2BaseDelay  = kLeftDel2Base;
        rightTank_.ap1BaseDelay    = kRightAP1Base;
        rightTank_.delay1BaseDelay = kRightDel1Base;
        rightTank_.ap2BaseDelay    = kRightAP2Base;
        rightTank_.delay2BaseDelay = kRightDel2Base;
        for (int i = 0; i < kNumDensityAPs; ++i)
        {
            leftTank_.densityAPBase[i]  = kLeftDensityAPBase[i];
            rightTank_.densityAPBase[i] = kRightDensityAPBase[i];
        }
    }
    if (prepared_)
    {
        updateDelayLengths();
        updateDecayCoefficients();
    }
}

// -----------------------------------------------------------------------

void DattorroTank::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;
    float rateRatio = static_cast<float> (sampleRate / kBaseSampleRate);

    // Modulation headroom beyond the max scaled delay (scale with sample rate)
    const int maxModExcursion = static_cast<int> (std::ceil (32.0 * sampleRate / kBaseSampleRate));

    // Track allocation ceiling for runtime setSizeRange() bounds checking
    sizeRangeAllocatedMax_ = std::max (sizeRangeAllocatedMax_, std::max (sizeRangeMax_, 1.5f));

    // Allocate all buffers
    auto prepareTank = [&] (Tank& tank)
    {
        constexpr float kMaxDelayScale = 4.0f;  // Must match setDelayScale() clamp
        float maxScale = sizeRangeAllocatedMax_ * kMaxDelayScale;
        int ap1Max = static_cast<int> (std::ceil (tank.ap1BaseDelay * rateRatio * maxScale)) + maxModExcursion;
        int del1Max = static_cast<int> (std::ceil (tank.delay1BaseDelay * rateRatio * maxScale)) + maxModExcursion;
        int ap2Max = static_cast<int> (std::ceil (tank.ap2BaseDelay * rateRatio * maxScale)) + maxModExcursion;
        int del2Max = static_cast<int> (std::ceil (tank.delay2BaseDelay * rateRatio * maxScale)) + maxModExcursion;

        tank.ap1Buffer.allocate (ap1Max);
        tank.delay1.allocate (del1Max + maxModExcursion);  // Extra headroom for noise jitter
        tank.ap2.allocate (ap2Max);
        tank.delay2.allocate (del2Max + maxModExcursion);

        // Density cascade allpasses. updateDelayLengths() scales these by
        // delayScale_ too, so the max allocation must include it to avoid
        // buffer underruns when delayScale_ > 1.
        for (int i = 0; i < kNumDensityAPs; ++i)
        {
            int dapMax = static_cast<int> (std::ceil (
                tank.densityAPBase[i] * rateRatio * sizeRangeAllocatedMax_ * kMaxDelayScale)) + 4;
            tank.densityAP[i].allocate (dapMax);
            // Lexicon spin-and-wander on each density AP — same fix as
            // SixAPTank got. Breaks the AP's modal phase-locking which
            // otherwise leaks its delay period into the tail as an audible
            // 26-30 ms ring on plate presets (verified by render-tool
            // measurement on Vintage Vocal Plate / Bright Drum Plate).
            tank.densityAP[i].jitterDepthFraction = 0.015f;  // 1.5 % wander
        }

        tank.damping.prepare (static_cast<float> (sampleRate));
        tank.damping.reset();
        tank.crossFeedState = 0.0f;
    };

    prepareTank (leftTank_);
    prepareTank (rightTank_);

    // Initialize LFO and noise PRNG state with different seeds per tank so
    // L/R modulators trace independent paths.
    leftTank_.lfo.prepare        (static_cast<float> (sampleRate), 0x12345678u);
    leftTank_.noiseState  = 0xDEADBEEFu;
    rightTank_.lfo.prepare       (static_cast<float> (sampleRate), 0x87654321u);
    rightTank_.noiseState = 0xCAFEBABEu;

    // Per-density-AP jitter LFOs — distinct seeds per stage and per L/R tank
    // so every AP wanders independently. Rate is set later in updateJitterDepth
    // (called from updateDelayLengths) based on actual delaySamples.
    for (int i = 0; i < kNumDensityAPs; ++i)
    {
        const std::uint32_t lSeed = 0xBADBEEFu + static_cast<std::uint32_t> (i * 31337);
        const std::uint32_t rSeed = 0xC0FFEEu  + static_cast<std::uint32_t> (i * 27449);
        leftTank_ .densityAP[i].jitterLFO.prepare (static_cast<float> (sampleRate), lSeed);
        rightTank_.densityAP[i].jitterLFO.prepare (static_cast<float> (sampleRate), rSeed);
    }

    prepared_ = true;

    updateDelayLengths();
    updateDecayCoefficients();
    updateLFORates();

    // Re-apply mod depth scaled for the new sample rate
    setModDepth (lastModDepthRaw_);

    // Re-apply noise mod depth and structural HF damping for new sample rate
    if (lastNoiseModDepthRaw_ >= 0.0f)
        setNoiseModDepth (lastNoiseModDepthRaw_);
    if (lastStructHFHz_ > 0.0f)
        setStructuralHFDamping (lastStructHFHz_);

    // Recompute soft-onset coefficient for the new sample rate
    if (softOnsetMs_ > 0.0f)
        setSoftOnsetMs (softOnsetMs_);

    // Clear all stateful trackers (structural HF damping state, terminal
    // decay RMS history). Without this, a host re-prepare would start with
    // empty delay buffers but retain the previous run's tracker state.
    structHFStateL_ = 0.0f;
    structHFStateR_ = 0.0f;
    leftTank_.currentRMS = 0.0f;
    leftTank_.peakRMS = 0.0f;
    leftTank_.terminalDecayActive = false;
    leftTank_.savedAP1Mod = 0.0f;
    rightTank_.currentRMS = 0.0f;
    rightTank_.peakRMS = 0.0f;
    rightTank_.terminalDecayActive = false;
    rightTank_.savedAP1Mod = 0.0f;
    softOnsetEnvL_ = (softOnsetMs_ > 0.0f) ? 0.0f : 1.0f;
    limiterEnv_ = 0.0f;
    // Sample-rate-invariant terminal decay smoothing coefficients
    constexpr float kRmsTauMs = 45.0f;    // RMS window ~45ms/tracking
    constexpr float kPeakTauMs = 2270.0f; // Peak decay ~2.27s hold decay
    float sr = static_cast<float> (sampleRate_);
    rmsAlpha_ = std::exp (-1000.0f / (kRmsTauMs * sr));
    peakDecayAlpha_ = std::exp (-1000.0f / (kPeakTauMs * sr));

    // Recompute limiter release coefficient for new sample rate
    if (limiterThreshold_ > 0.0f)
    {
        float releaseSamples = limiterReleaseMs_ * 0.001f * static_cast<float> (sampleRate_);
        limiterReleaseCoeff_ = std::exp (-1.0f / std::max (releaseSamples, 1.0f));
    }
}

// -----------------------------------------------------------------------

void DattorroTank::process (const float* inputL, const float* inputR,
                            float* outputL, float* outputR, int numSamples)
{
    if (! prepared_)
        return;

    // Drive-style saturation: amount=0 → clean (threshold 1.0, no audible
    // effect on quiet tail); amount=1 → threshold 0.4 (aggressive, every loop
    // pass touches the soft knee). Ceiling stays at 2.0 so the softClip's
    // upper asymptote remains stable across drive settings.
    const float satThreshold = 1.0f - saturationAmount_ * 0.6f;
    const float satCeiling   = 2.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        // Mono sum of stereo input (Dattorro tank is internally mono,
        // stereo comes from decorrelated output tapping)
        float input = (inputL[i] + inputR[i]) * 0.5f;

        if (frozen_)
            input = 0.0f;

        // ------------------------------------------------------------------
        // Process both tanks. Each receives the other's cross-feed state.
        // Order: left first, then right. The one-sample delay in cross-feed
        // is intentional (Dattorro's figure-8 topology).

        auto processTank = [&] (Tank& tank, float otherCrossFeed)
        {
            // Tank input: new audio + cross-fed signal from the other tank
            float tankIn = input + otherCrossFeed;

            // --- Modulated allpass (decay diffusion 1) ---
            // Random-walk LFO replaces the previous std::sin(phase) + drift
            // chain. The smoothed-noise modulator never settles into a
            // perceptible periodic warble — gives the "expensive shimmer" of
            // high-end random-hall hardware. When frozen, hold the last
            // modulation offset so the read head doesn't snap (avoids click).
            float currentMod = frozen_ ? tank.savedAP1Mod : tank.lfo.next();
            if (! frozen_)
                tank.savedAP1Mod = currentMod;
            float ap1ReadDelay = tank.ap1DelaySamples + currentMod;
            ap1ReadDelay = std::max (ap1ReadDelay, 1.0f);  // Never read ahead of write

            float ap1Delayed = tank.ap1Buffer.readInterpolated (ap1ReadDelay);
            float coeff1 = frozen_ ? 0.0f : decayDiff1_;
            float ap1In = tankIn + coeff1 * ap1Delayed;
            tank.ap1Buffer.write (ap1In);
            float ap1Out = ap1Delayed - coeff1 * ap1In;

            // --- Delay 1 (with per-sample noise jitter) ---
            // Skip jitter when frozen to keep tail static (no PRNG state advance).
            float effectiveNoiseMod = (independentNoiseModDepth_ >= 0.0f)
                                    ? independentNoiseModDepth_ : noiseModDepth_;
            float jitter1 = frozen_ ? 0.0f : nextDrift (tank.noiseState) * effectiveNoiseMod;
            float del1Read = tank.delay1Samples + jitter1;
            del1Read = std::max (del1Read, 1.0f);
            float del1Out = tank.delay1.readInterpolated (del1Read);
            tank.delay1.write (ap1Out);

            // --- Density cascade: 3 allpasses to multiply echo density ---
            float dense = del1Out;
            if (! frozen_)
            {
                for (int d = 0; d < kNumDensityAPs; ++d)
                    dense = tank.densityAP[d].process (dense, densityDiffCoeff_);
            }

            // --- Two-band damping ---
            float damped = frozen_ ? dense : tank.damping.process (dense);

            // --- Structural HF damping ---
            if (structHFCoeff_ > 0.0f && ! frozen_)
            {
                float& hfState = (&tank == &leftTank_) ? structHFStateL_ : structHFStateR_;
                hfState = (1.0f - structHFCoeff_) * damped + structHFCoeff_ * hfState;
                damped = hfState;
            }

            // --- Static allpass (decay diffusion 2) ---
            float coeff2 = frozen_ ? 0.0f : decayDiff2_;
            float ap2Out = tank.ap2.process (damped, coeff2);

            // --- Delay 2 (with per-sample noise jitter) ---
            float jitter2 = frozen_ ? 0.0f : nextDrift (tank.noiseState) * effectiveNoiseMod;
            float del2Read = tank.delay2Samples + jitter2;
            del2Read = std::max (del2Read, 1.0f);
            float del2Out = tank.delay2.readInterpolated (del2Read);
            // Denormal prevention: tiny alternating bias
            float bias = frozen_ ? 0.0f
                                 : (((tank.delay2.writePos ^ 1) & 1)
                                        ? +DspUtils::kDenormalPrevention
                                        : -DspUtils::kDenormalPrevention);
            tank.delay2.write (ap2Out + bias);

            // Terminal decay: extra damping when tail is far below peak.
            // Uses sample-rate-invariant smoothing (rmsAlpha_/peakDecayAlpha_
            // computed in prepare()) and linear-domain ratio test.
            // Terminal decay HARD-DISABLED — see VocalPlatePreset.cpp for rationale.
            if (false /* terminalDecayFactor_ < 1.0f */ && ! frozen_)
            {
                float sampleEnergy = del2Out * del2Out;
                tank.currentRMS = tank.currentRMS * rmsAlpha_ + sampleEnergy * (1.0f - rmsAlpha_);
                if (tank.currentRMS > tank.peakRMS) tank.peakRMS = tank.currentRMS;
                else tank.peakRMS *= peakDecayAlpha_;
                // Linear-domain ratio test: peak/current > threshold (avoids per-sample log10)
                float ratio = tank.peakRMS / std::max (tank.currentRMS, 1e-20f);
                tank.terminalDecayActive = (ratio > terminalLinearThreshold_) && (tank.peakRMS > 1e-12f);
                if (tank.terminalDecayActive)
                    del2Out *= terminalDecayFactor_;
            }

            // Cross-feed output: end of this tank feeds the other tank's input.
            // softClip on the cross-feed write delivers analog-style soft
            // saturation that engages only when transients drive the loop above
            // ±1.0 (quiet tail samples pass through linearly, so RT60 is
            // preserved). Hard kSafetyClip stays as the stability backstop.
            tank.crossFeedState = std::clamp (DspUtils::softClip (del2Out, satThreshold, satCeiling),
                                              -kSafetyClip, kSafetyClip);
        };

        // Save right tank's cross-feed state before left tank overwrites it
        float rightCrossFeed = rightTank_.crossFeedState;
        float leftCrossFeed = leftTank_.crossFeedState;

        processTank (leftTank_, rightCrossFeed);
        processTank (rightTank_, leftCrossFeed);

        // ------------------------------------------------------------------
        // Output: sum 7 signed taps from both tanks per channel.
        const OutputTap* lTaps = useCustomTaps_ ? customLeftTaps_ : kLeftOutputTaps;
        const OutputTap* rTaps = useCustomTaps_ ? customRightTaps_ : kRightOutputTaps;

        float outL = 0.0f;
        for (int t = 0; t < kNumOutputTaps; ++t)
            outL += readOutputTap (lTaps[t]) * lTaps[t].sign * lTaps[t].gain;

        float outR = 0.0f;
        for (int t = 0; t < kNumOutputTaps; ++t)
            outR += readOutputTap (rTaps[t]) * rTaps[t].sign * rTaps[t].gain;

        // Normalize 7-tap sum. The tank has much higher internal energy than
        // the FDN (2 loops vs 16 channels with Hadamard ÷4 normalization),
        // so we use a lower output scale to match FDN output levels.
        constexpr float kOutputScale = 0.14285714f;  // 1/7 — average of 7 taps
        const float outputGain = kOutputScale * lateGainScale_;

        float scaledL = outL * outputGain;
        float scaledR = outR * outputGain;

        // Soft output onset ramp: smooths the initial transient spike from early taps.
        // Ramps from 0→1 linearly over softOnsetMs_ after reset/preset change.
        if (softOnsetEnvL_ < 1.0f)
        {
            scaledL *= softOnsetEnvL_;
            scaledR *= softOnsetEnvL_;
            softOnsetEnvL_ = std::min (softOnsetEnvL_ + softOnsetCoeff_, 1.0f);
        }

        // Peak limiter: fast-attack / slow-release envelope follower with gain reduction.
        // Reduces transient peaks while preserving RMS level (lowers crest factor).
        // Attack: instant (0 samples). Release: ~50ms one-pole decay.
        // When peak exceeds limiterThreshold_, gain is reduced to keep output at threshold.
        if (limiterThreshold_ > 0.0f)
        {
            float peakLR = std::max (std::abs (scaledL), std::abs (scaledR));

            // Envelope: instant attack, slow release
            if (peakLR > limiterEnv_)
                limiterEnv_ = peakLR;  // Instant attack
            else
                limiterEnv_ = limiterReleaseCoeff_ * limiterEnv_
                            + (1.0f - limiterReleaseCoeff_) * peakLR;  // Slow release

            // Gain reduction: when envelope > threshold, reduce gain
            if (limiterEnv_ > limiterThreshold_)
            {
                float gain = limiterThreshold_ / limiterEnv_;
                scaledL *= gain;
                scaledR *= gain;
            }
        }

        outputL[i] = std::clamp (scaledL, -kSafetyClip, kSafetyClip);
        outputR[i] = std::clamp (scaledR, -kSafetyClip, kSafetyClip);
    }
}

// -----------------------------------------------------------------------
// Output tap reading

float DattorroTank::readOutputTap (const OutputTap& tap) const
{
    // Map buffer index to the actual delay buffer:
    // 0=leftDelay1, 1=leftDelay2, 2=leftAP2,
    // 3=rightDelay1, 4=rightDelay2, 5=rightAP2
    const DelayLine* delayBuf = nullptr;
    float totalDelay = 0.0f;

    switch (tap.bufferIndex)
    {
        case 0: delayBuf = &leftTank_.delay1;  totalDelay = leftTank_.delay1Samples;  break;
        case 1: delayBuf = &leftTank_.delay2;  totalDelay = leftTank_.delay2Samples;  break;
        case 2:
        {
            // Read from AP2's internal buffer at a fractional position
            const auto& ap = leftTank_.ap2;
            int tapOffset = static_cast<int> (tap.positionFrac * static_cast<float> (ap.delaySamples));
            tapOffset = std::max (tapOffset, 1);
            return ap.buffer[static_cast<size_t> ((ap.writePos - tapOffset) & ap.mask)];
        }
        case 3: delayBuf = &rightTank_.delay1; totalDelay = rightTank_.delay1Samples; break;
        case 4: delayBuf = &rightTank_.delay2; totalDelay = rightTank_.delay2Samples; break;
        case 5:
        {
            const auto& ap = rightTank_.ap2;
            int tapOffset = static_cast<int> (tap.positionFrac * static_cast<float> (ap.delaySamples));
            tapOffset = std::max (tapOffset, 1);
            return ap.buffer[static_cast<size_t> ((ap.writePos - tapOffset) & ap.mask)];
        }
        default: return 0.0f;
    }

    // Read from delay line at fractional position
    float tapDelay = tap.positionFrac * totalDelay;
    tapDelay = std::max (tapDelay, 1.0f);
    return delayBuf->readInterpolated (tapDelay);
}

// -----------------------------------------------------------------------
// Parameter setters

void DattorroTank::setDecayTime (float seconds)
{
    decayTime_ = std::max (seconds, 0.1f);
    if (prepared_)
        updateDecayCoefficients();
}

void DattorroTank::setBassMultiply (float mult)
{
    bassMultiply_ = std::max (mult, 0.1f);
    if (prepared_)
        updateDecayCoefficients();
}

void DattorroTank::setMidMultiply (float mult)
{
    midMultiply_ = std::clamp (mult, 0.1f, 4.0f);
    if (prepared_)
        updateDecayCoefficients();
}

void DattorroTank::setSaturation (float amount)
{
    saturationAmount_ = std::clamp (amount, 0.0f, 1.0f);
    // Wired into the per-sample softClip in process() — no recomputation needed.
}

void DattorroTank::setTrebleMultiply (float mult)
{
    trebleMultiply_ = std::max (mult, 0.1f);
    if (prepared_)
        updateDecayCoefficients();
}

void DattorroTank::setCrossoverFreq (float hz)
{
    crossoverFreq_ = std::min (std::max (hz, 1.0f), highCrossoverFreq_ - 10.0f);
    if (prepared_)
        updateDecayCoefficients();
}

void DattorroTank::setHighCrossoverFreq (float hz)
{
    highCrossoverFreq_ = std::max (hz, crossoverFreq_ + 10.0f);
    if (prepared_)
        updateDecayCoefficients();
}

void DattorroTank::setAirDampingScale (float scale)
{
    airDampingScale_ = std::max (scale, 0.01f);
    if (prepared_)
        updateDecayCoefficients();
}

void DattorroTank::setModDepth (float depth)
{
    // Cache the original requested value so prepare() can replay it at a new
    // sample rate without losing precision from clamping.
    lastModDepthRaw_ = depth;

    // Clamp depth so modDepthSamples_ cannot exceed the ±32-sample modulation
    // headroom reserved in prepare()'s buffer allocation.
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    float maxDepth = 32.0f / (16.0f * std::max (rateRatio, 1.0f));
    float clampedDepth = std::clamp (depth, 0.0f, maxDepth);
    // Map 0-1 knob range to 0-16 samples peak excursion (Dattorro: 8 samples typical)
    modDepthSamples_ = clampedDepth * 16.0f * rateRatio;
    leftTank_.lfo.setDepth  (modDepthSamples_);
    rightTank_.lfo.setDepth (modDepthSamples_);
    // Noise jitter uses a SQUARED depth mapping (was linear ×12). At full
    // depth=1.0 the maximum is unchanged (12 samples) so plate/chamber
    // presets that need aggressive jitter to suppress modal ringing still
    // get it. At low depths (e.g. Blade Runner 224 at 0.03) the squared
    // curve drops noise from 0.36 samples to 0.011 — eliminating the
    // wandering LR-correlation jitter that was 2.3× too noisy vs the
    // Arturia LX-24 reference (stddev 0.066 vs target 0.028).
    noiseModDepth_ = clampedDepth * clampedDepth * 12.0f * rateRatio;
}

void DattorroTank::setModRate (float hz)
{
    modRateHz_ = hz;
    if (prepared_)
        updateLFORates();
}

void DattorroTank::setLimiter (float thresholdDb, float releaseMs)
{
    if (thresholdDb <= -60.0f || thresholdDb >= 0.0f)
    {
        limiterThreshold_ = 0.0f;  // Disabled
        return;
    }
    limiterThreshold_ = std::pow (10.0f, thresholdDb / 20.0f);
    limiterReleaseMs_ = releaseMs;
    if (prepared_)
    {
        float releaseSamples = releaseMs * 0.001f * static_cast<float> (sampleRate_);
        limiterReleaseCoeff_ = std::exp (-1.0f / std::max (releaseSamples, 1.0f));
    }
}

void DattorroTank::setSize (float size)
{
    sizeParam_ = std::clamp (size, 0.0f, 1.0f);
    if (prepared_)
    {
        updateDelayLengths();
        updateDecayCoefficients();
    }
}

void DattorroTank::setSoftOnsetMs (float ms)
{
    float newMs = std::max (ms, 0.0f);
    bool changed = (newMs > 0.0f) != (softOnsetMs_ > 0.0f)
                || std::abs (newMs - softOnsetMs_) > 0.01f;
    softOnsetMs_ = newMs;

    if (prepared_ && softOnsetMs_ > 0.0f)
    {
        // Per-sample increment for linear ramp from 0→1 over softOnsetMs
        float samples = softOnsetMs_ * 0.001f * static_cast<float> (sampleRate_);
        softOnsetCoeff_ = 1.0f / std::max (samples, 1.0f);
        // Only reset ramp when value actually changes (not on every processBlock call)
        if (changed)
            softOnsetEnvL_ = 0.0f;
    }
    else
    {
        softOnsetCoeff_ = 0.0f;  // Disabled
        softOnsetEnvL_ = 1.0f;   // Full gain immediately
    }
}

void DattorroTank::setDelayScale (float scale)
{
    delayScale_ = std::clamp (scale, 0.25f, 4.0f);
    if (prepared_)
    {
        updateDelayLengths();
        updateDecayCoefficients();
    }
}

void DattorroTank::setNoiseModDepth (float samples)
{
    // Independent noise jitter, decoupled from LFO modDepth.
    // When set (>= 0), this overrides the modDepth-coupled noise jitter.
    // Critical for algorithms with low modDepth (e.g., Chamber=0.05) that
    // still need aggressive jitter to suppress modal resonances.
    lastNoiseModDepthRaw_ = samples;
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    // Clamp to the modulation headroom reserved in prepare() (32 samples at base rate)
    float maxAllowed = 32.0f * rateRatio;
    independentNoiseModDepth_ = std::clamp (samples * rateRatio, -1.0f, maxAllowed);
}

void DattorroTank::setOutputTaps (const OutputTap* left, const OutputTap* right)
{
    if (left && right)
    {
        for (int i = 0; i < kNumOutputTaps; ++i)
        {
            customLeftTaps_[i] = left[i];
            customRightTaps_[i] = right[i];
        }
        useCustomTaps_ = true;
    }
    else
    {
        useCustomTaps_ = false;
    }
}

void DattorroTank::applyTapGains (const float* leftGains, const float* rightGains)
{
    if (leftGains && rightGains)
    {
        // If custom taps aren't already active, initialize from defaults
        // so buffer indices and positions are valid before applying gains.
        if (! useCustomTaps_)
        {
            for (int i = 0; i < kNumOutputTaps; ++i)
            {
                customLeftTaps_[i] = kLeftOutputTaps[i];
                customRightTaps_[i] = kRightOutputTaps[i];
            }
        }

        for (int i = 0; i < kNumOutputTaps; ++i)
        {
            customLeftTaps_[i].gain = leftGains[i];
            customRightTaps_[i].gain = rightGains[i];
        }
        useCustomTaps_ = true;
    }
}

void DattorroTank::setTankDiffusion (float amount)
{
    // Scale around baseline: knob 0 → 0.5×, knob 0.5 → 1.0× (= 0.55 baseline),
    // knob 0.85 (typical preset) → 1.095×, knob 1.0 → 1.2×. Hard cap at 0.85
    // keeps the AP comfortably inside its stable range.
    float a = std::clamp (amount, 0.0f, 1.0f);
    float scale = 0.5f + a * 0.7f;
    densityDiffCoeff_ = std::clamp (kDensityDiffBaseline_ * scale, 0.0f, 0.85f);
}

void DattorroTank::setFreeze (bool frozen)
{
    bool wasTransition = (frozen != frozen_);
    frozen_ = frozen;
    if (wasTransition)
    {
        structHFStateL_ = 0.0f;
        structHFStateR_ = 0.0f;
        leftTank_.currentRMS = 0.0f;
        leftTank_.peakRMS = 0.0f;
        leftTank_.terminalDecayActive = false;
        rightTank_.currentRMS = 0.0f;
        rightTank_.peakRMS = 0.0f;
        rightTank_.terminalDecayActive = false;
    }
}

void DattorroTank::setLateGainScale (float scale)
{
    lateGainScale_ = std::max (scale, 0.0f);
}

void DattorroTank::setSizeRange (float min, float max)
{
    float newMin = std::max (min, 0.0f);
    float newMax = std::max (max, newMin);
    if (prepared_)
    {
        newMin = std::min (newMin, sizeRangeAllocatedMax_);
        newMax = std::min (newMax, sizeRangeAllocatedMax_);
    }
    sizeRangeMin_ = newMin;
    sizeRangeMax_ = std::max (newMax, sizeRangeMin_);
    if (prepared_)
    {
        updateDelayLengths();
        updateDecayCoefficients();
    }
}

void DattorroTank::setDecayBoost (float boost)
{
    decayBoost_ = std::clamp (boost, 0.3f, 2.0f);
    if (prepared_)
        updateDecayCoefficients();
}

void DattorroTank::setStructuralHFDamping (float hz)
{
    lastStructHFHz_ = hz;
    if (hz <= 0.0f)
    {
        structHFCoeff_ = 0.0f;
        structHFStateL_ = 0.0f;
        structHFStateR_ = 0.0f;
        return;
    }
    structHFCoeff_ = std::exp (-kTwoPi * hz / static_cast<float> (sampleRate_));
}

void DattorroTank::setTerminalDecay (float thresholdDB, float factor)
{
    terminalDecayThresholdDB_ = -std::abs (thresholdDB);
    terminalDecayFactor_ = std::clamp (factor, 0.0f, 1.0f);
    terminalLinearThreshold_ = std::pow (10.0f, -terminalDecayThresholdDB_ * 0.1f);
}

void DattorroTank::clearBuffers()
{
    auto clearTank = [this] (Tank& tank, uint32_t seed)
    {
        tank.ap1Buffer.clear();
        tank.delay1.clear();
        for (int i = 0; i < kNumDensityAPs; ++i)
            tank.densityAP[i].clear();
        tank.ap2.clear();
        tank.delay2.clear();
        tank.damping.reset();
        tank.crossFeedState = 0.0f;
        tank.savedAP1Mod = 0.0f;  // keep from the code-review fix
        tank.currentRMS = 0.0f;
        tank.peakRMS = 0.0f;
        tank.terminalDecayActive = false;
        tank.lfo.prepare (static_cast<float> (sampleRate_), seed);
        tank.lfo.setRate (modRateHz_);
        tank.lfo.setDepth (modDepthSamples_);
        tank.noiseState = seed * 2654435761u;
    };

    clearTank (leftTank_, 0x12345678u);
    clearTank (rightTank_, 0x87654321u);
    // Reset soft onset ramp (starts from 0 if enabled, 1 if disabled)
    softOnsetEnvL_ = (softOnsetMs_ > 0.0f) ? 0.0f : 1.0f;
    limiterEnv_ = 0.0f;
    structHFStateL_ = 0.0f;
    structHFStateR_ = 0.0f;
}

// -----------------------------------------------------------------------
// Internal update methods

void DattorroTank::updateDelayLengths()
{
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    float sizeScale = sizeRangeMin_ + (sizeRangeMax_ - sizeRangeMin_) * sizeParam_;
    float totalScale = sizeScale * delayScale_;  // Combined size + per-algorithm delay scaling

    auto updateTank = [&] (Tank& tank)
    {
        tank.ap1DelaySamples = static_cast<float> (tank.ap1BaseDelay) * rateRatio * totalScale;
        tank.delay1Samples   = static_cast<float> (tank.delay1BaseDelay) * rateRatio * totalScale;
        tank.delay2Samples   = static_cast<float> (tank.delay2BaseDelay) * rateRatio * totalScale;

        // AP2 delay (integer, used by Allpass::process)
        tank.ap2.delaySamples = std::max (1, static_cast<int> (
            static_cast<float> (tank.ap2BaseDelay) * rateRatio * totalScale));

        // Density cascade allpass delays (integer, scaled by rate + size + delayScale)
        // After updating delaySamples we MUST refresh the jitter LFO depth+rate
        // so the spin-and-wander modulation tracks the new delay length.
        const float sr = static_cast<float> (sampleRate_);
        for (int i = 0; i < kNumDensityAPs; ++i)
        {
            tank.densityAP[i].delaySamples = std::max (1, static_cast<int> (
                static_cast<float> (tank.densityAPBase[i]) * rateRatio * totalScale));
            tank.densityAP[i].updateJitterDepth (sr);
        }
    };

    updateTank (leftTank_);
    updateTank (rightTank_);
}

void DattorroTank::updateDecayCoefficients()
{
    float sr = static_cast<float> (sampleRate_);
    float lowXoverCoeff = std::exp (-kTwoPi * crossoverFreq_ / sr);
    float highXoverCoeff = std::exp (-kTwoPi * highCrossoverFreq_ / sr);

    // NOTE: SixAPTank uses a size-dependent AP energy-storage factor for
    // its 6-AP cascade. We tried it here too but it OVER-compensated short-
    // decay plate presets (Bright Drum Plate went from -2 % to -20 % off).
    // Dattorro has only 3 density APs and a different decay-diff structure,
    // so its empirical storage factor is much closer to 1.0. Use the simple
    // sum (storage factor = 1.0) — measured to be the best fit across the
    // 4 plate presets that depend on this engine.
    auto updateTankDamping = [&] (Tank& tank)
    {
        float loopLength = tank.ap1DelaySamples
                         + tank.delay1Samples
                         + static_cast<float> (tank.ap2.delaySamples)
                         + tank.delay2Samples;
        for (int i = 0; i < kNumDensityAPs; ++i)
            loopLength += static_cast<float> (tank.densityAP[i].delaySamples);

        float gBase = std::pow (10.0f, -3.0f * loopLength / (decayTime_ * sr));
        gBase = std::clamp (std::pow (gBase, decayBoost_), 0.001f, 0.9999f);
        // Per-band gains using true 3-band multipliers. The legacy
        // airDampingScale_ is now folded into trebleMultiply for back-compat —
        // call setMidMultiply explicitly for true mid-band control.
        float gLow = std::clamp (std::pow (gBase, 1.0f / bassMultiply_), 0.001f, 0.9999f);
        float gMid = std::clamp (std::pow (gBase, 1.0f / midMultiply_), 0.001f, 0.9999f);
        float gHi  = std::clamp (std::pow (gBase, 1.0f / (trebleMultiply_ * airDampingScale_)), 0.001f, 0.9999f);

        tank.damping.setCoefficients (gLow, gMid, gHi, lowXoverCoeff, highXoverCoeff);
    };

    updateTankDamping (leftTank_);
    updateTankDamping (rightTank_);
}

void DattorroTank::updateLFORates()
{
    // Both tanks at the SAME rate. Originally we used asymmetric L/R rates
    // (×√5/2 offset) to "prevent correlated modulation paths," but this
    // turned out to be the dominant source of LR-correlation jitter — over
    // time the two LFOs drift in and out of phase, causing the late-field
    // L/R correlation to wander. Measured against Arturia LX-24 BladeRunner
    // (LR stddev 0.028), our wandering of 0.066 is audibly less stable
    // than a true random late field.
    //
    // The LFOs already use distinct PRNG seeds (0x12345678u vs 0x87654321u
    // in prepare()), so the per-sample modulation paths are decorrelated
    // even at the same rate. Setting equal rates keeps the average
    // modulation magnitude in phase between tanks while letting the
    // per-sample noise produce natural micro-variance.
    leftTank_.lfo.setRate  (modRateHz_);
    rightTank_.lfo.setRate (modRateHz_);
}
