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

void DattorroTank::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;
    float rateRatio = static_cast<float> (sampleRate / kBaseSampleRate);

    // Modulation headroom beyond the max scaled delay
    int maxModExcursion = 32;

    // Allocate all buffers
    auto prepareTank = [&] (Tank& tank)
    {
        int ap1Max = static_cast<int> (std::ceil (tank.ap1BaseDelay * rateRatio * sizeRangeMax_)) + maxModExcursion;
        int del1Max = static_cast<int> (std::ceil (tank.delay1BaseDelay * rateRatio * sizeRangeMax_)) + maxModExcursion;
        int ap2Max = static_cast<int> (std::ceil (tank.ap2BaseDelay * rateRatio * sizeRangeMax_)) + maxModExcursion;
        int del2Max = static_cast<int> (std::ceil (tank.delay2BaseDelay * rateRatio * sizeRangeMax_)) + maxModExcursion;

        tank.ap1Buffer.allocate (ap1Max);
        tank.delay1.allocate (del1Max + maxModExcursion);  // Extra headroom for noise jitter
        tank.ap2.allocate (ap2Max);
        tank.delay2.allocate (del2Max + maxModExcursion);

        // Density cascade allpasses
        for (int i = 0; i < kNumDensityAPs; ++i)
        {
            int dapMax = static_cast<int> (std::ceil (tank.densityAPBase[i] * rateRatio * sizeRangeMax_)) + 4;
            tank.densityAP[i].allocate (dapMax);
        }

        tank.damping.reset();
        tank.crossFeedState = 0.0f;
    };

    prepareTank (leftTank_);
    prepareTank (rightTank_);

    // Initialize LFO and PRNG state with different seeds per tank
    leftTank_.lfoPhase = 0.0f;
    leftTank_.lfoPRNG = 0x12345678u;
    leftTank_.noiseState = 0xDEADBEEFu;
    rightTank_.lfoPhase = 1.5707963f;  // 90° offset for stereo decorrelation
    rightTank_.lfoPRNG = 0x87654321u;
    rightTank_.noiseState = 0xCAFEBABEu;

    prepared_ = true;

    updateDelayLengths();
    updateDecayCoefficients();
    updateLFORates();
}

// -----------------------------------------------------------------------

void DattorroTank::process (const float* inputL, const float* inputR,
                            float* outputL, float* outputR, int numSamples)
{
    if (! prepared_)
        return;

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
            // LFO modulation with "Wander" drift (classic reverb technique)
            float mod = std::sin (tank.lfoPhase) * modDepthSamples_;
            float ap1ReadDelay = tank.ap1DelaySamples + mod;
            ap1ReadDelay = std::max (ap1ReadDelay, 1.0f);  // Never read ahead of write

            float ap1Delayed = tank.ap1Buffer.readInterpolated (ap1ReadDelay);
            float coeff1 = frozen_ ? 0.0f : decayDiff1_;
            float ap1In = tankIn + coeff1 * ap1Delayed;
            tank.ap1Buffer.write (ap1In);
            float ap1Out = ap1Delayed - coeff1 * ap1In;

            // Advance LFO with drift
            float drift = nextDrift (tank.lfoPRNG) * tank.lfoPhaseInc * 0.08f;
            tank.lfoPhase += tank.lfoPhaseInc + drift;
            if (tank.lfoPhase >= kTwoPi)
                tank.lfoPhase -= kTwoPi;
            else if (tank.lfoPhase < 0.0f)
                tank.lfoPhase += kTwoPi;

            // --- Delay 1 (with per-sample noise jitter) ---
            float jitter1 = nextDrift (tank.noiseState) * noiseModDepth_;
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

            // --- Decay gain ---
            // (Applied via the damping coefficients, which incorporate per-pass gain.
            //  No separate multiply needed — TwoBandDamping gLow/gHigh already
            //  encode the RT60-based attenuation per loop pass.)

            // --- Static allpass (decay diffusion 2) ---
            float coeff2 = frozen_ ? 0.0f : decayDiff2_;
            float ap2Out = tank.ap2.process (damped, coeff2);

            // --- Delay 2 (with per-sample noise jitter) ---
            float jitter2 = nextDrift (tank.noiseState) * noiseModDepth_;
            float del2Read = tank.delay2Samples + jitter2;
            del2Read = std::max (del2Read, 1.0f);
            float del2Out = tank.delay2.readInterpolated (del2Read);
            // Denormal prevention: tiny alternating bias
            float bias = ((tank.delay2.writePos ^ 1) & 1) ? +DspUtils::kDenormalPrevention
                                                           : -DspUtils::kDenormalPrevention;
            tank.delay2.write (ap2Out + bias);

            // Cross-feed output: end of this tank feeds the other tank's input
            tank.crossFeedState = del2Out;
        };

        // Save right tank's cross-feed state before left tank overwrites it
        float rightCrossFeed = rightTank_.crossFeedState;
        float leftCrossFeed = leftTank_.crossFeedState;

        processTank (leftTank_, rightCrossFeed);
        processTank (rightTank_, leftCrossFeed);

        // ------------------------------------------------------------------
        // Output: sum 7 signed taps from both tanks per channel.
        float outL = 0.0f;
        for (int t = 0; t < kNumOutputTaps; ++t)
            outL += readOutputTap (kLeftOutputTaps[t]) * kLeftOutputTaps[t].sign;

        float outR = 0.0f;
        for (int t = 0; t < kNumOutputTaps; ++t)
            outR += readOutputTap (kRightOutputTaps[t]) * kRightOutputTaps[t].sign;

        // Normalize 7-tap sum. The tank has much higher internal energy than
        // the FDN (2 loops vs 16 channels with Hadamard ÷4 normalization),
        // so we use a lower output scale to match FDN output levels.
        // DuskVerbEngine applies lateGainScale_ externally — do NOT apply here.
        constexpr float kOutputScale = 0.14285714f;  // 1/7 — average of 7 taps

        outputL[i] = std::clamp (outL * kOutputScale, -kSafetyClip, kSafetyClip);
        outputR[i] = std::clamp (outR * kOutputScale, -kSafetyClip, kSafetyClip);
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

void DattorroTank::setTrebleMultiply (float mult)
{
    trebleMultiply_ = std::max (mult, 0.1f);
    if (prepared_)
        updateDecayCoefficients();
}

void DattorroTank::setCrossoverFreq (float hz)
{
    crossoverFreq_ = hz;
    if (prepared_)
        updateDecayCoefficients();
}

void DattorroTank::setModDepth (float depth)
{
    // Map 0-1 knob range to 0-16 samples peak excursion (Dattorro: 8 samples typical)
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    modDepthSamples_ = depth * 16.0f * rateRatio;
    // Noise jitter scales with depth and sample rate
    noiseModDepth_ = depth * 12.0f * rateRatio;  // 12 samples peak at depth=1.0
}

void DattorroTank::setModRate (float hz)
{
    modRateHz_ = hz;
    if (prepared_)
        updateLFORates();
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

void DattorroTank::setFreeze (bool frozen)
{
    frozen_ = frozen;
}

void DattorroTank::setLateGainScale (float scale)
{
    lateGainScale_ = scale;
}

void DattorroTank::setSizeRange (float min, float max)
{
    sizeRangeMin_ = min;
    sizeRangeMax_ = max;
    if (prepared_)
    {
        updateDelayLengths();
        updateDecayCoefficients();
    }
}

void DattorroTank::clearBuffers()
{
    auto clearTank = [] (Tank& tank)
    {
        tank.ap1Buffer.clear();
        tank.delay1.clear();
        for (int i = 0; i < kNumDensityAPs; ++i)
            tank.densityAP[i].clear();
        tank.ap2.clear();
        tank.delay2.clear();
        tank.damping.reset();
        tank.crossFeedState = 0.0f;
    };

    clearTank (leftTank_);
    clearTank (rightTank_);
}

// -----------------------------------------------------------------------
// Internal update methods

void DattorroTank::updateDelayLengths()
{
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    float sizeScale = sizeRangeMin_ + (sizeRangeMax_ - sizeRangeMin_) * sizeParam_;

    auto updateTank = [&] (Tank& tank)
    {
        tank.ap1DelaySamples = static_cast<float> (tank.ap1BaseDelay) * rateRatio * sizeScale;
        tank.delay1Samples   = static_cast<float> (tank.delay1BaseDelay) * rateRatio * sizeScale;
        tank.delay2Samples   = static_cast<float> (tank.delay2BaseDelay) * rateRatio * sizeScale;

        // AP2 delay (integer, used by Allpass::process)
        tank.ap2.delaySamples = std::max (1, static_cast<int> (
            static_cast<float> (tank.ap2BaseDelay) * rateRatio * sizeScale));

        // Density cascade allpass delays (integer, scaled by rate + size)
        for (int i = 0; i < kNumDensityAPs; ++i)
            tank.densityAP[i].delaySamples = std::max (1, static_cast<int> (
                static_cast<float> (tank.densityAPBase[i]) * rateRatio * sizeScale));
    };

    updateTank (leftTank_);
    updateTank (rightTank_);
}

void DattorroTank::updateDecayCoefficients()
{
    float sr = static_cast<float> (sampleRate_);
    float crossoverCoeff = std::exp (-kTwoPi * crossoverFreq_ / sr);

    // In Dattorro topology, the decay gain is per-loop-pass (not per-delay-line).
    // Total loop length determines the effective delay for RT60 calculation.
    auto updateTankDamping = [&] (Tank& tank)
    {
        // Total loop length in samples (all elements including density cascade)
        float loopLength = tank.ap1DelaySamples
                         + tank.delay1Samples
                         + static_cast<float> (tank.ap2.delaySamples)
                         + tank.delay2Samples;
        for (int i = 0; i < kNumDensityAPs; ++i)
            loopLength += static_cast<float> (tank.densityAP[i].delaySamples);

        // Per-loop-pass gain: 10^(-3 * loopLength / (RT60 * sampleRate))
        // This gives -60dB attenuation after RT60 seconds.
        float gBase = std::pow (10.0f, -3.0f * loopLength / (decayTime_ * sr));

        // Bass sustain longer ("Bass Multiply")
        float gLow = std::pow (gBase, 1.0f / bassMultiply_);

        // Treble rolls off faster ("Treble Multiply")
        float gHigh = std::pow (gBase, 1.0f / trebleMultiply_);

        tank.damping.setCoefficients (gLow, gHigh, crossoverCoeff);
    };

    updateTankDamping (leftTank_);
    updateTankDamping (rightTank_);
}

void DattorroTank::updateLFORates()
{
    float sr = static_cast<float> (sampleRate_);

    // Asymmetric rates: left at base, right at golden ratio offset
    // to prevent correlated modulation between tanks.
    leftTank_.lfoPhaseInc  = kTwoPi * modRateHz_ / sr;
    rightTank_.lfoPhaseInc = kTwoPi * modRateHz_ * 1.1180339887f / sr;  // × sqrt(5)/2
}
