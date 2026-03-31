#include "QuadTank.h"
#include "DspUtils.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// -----------------------------------------------------------------------
// DelayLine helpers (same as DattorroTank)

void QuadTank::DelayLine::allocate (int maxSamples)
{
    int bufSize = DspUtils::nextPowerOf2 (maxSamples + 1);
    buffer.assign (static_cast<size_t> (bufSize), 0.0f);
    mask = bufSize - 1;
    writePos = 0;
}

void QuadTank::DelayLine::clear()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

float QuadTank::DelayLine::readInterpolated (float delaySamples) const
{
    float readPos = static_cast<float> (writePos) - delaySamples;
    int intIdx = static_cast<int> (std::floor (readPos));
    float frac = readPos - static_cast<float> (intIdx);
    return DspUtils::cubicHermite (buffer.data(), mask, intIdx, frac);
}

// -----------------------------------------------------------------------
void QuadTank::Allpass::allocate (int maxSamples)
{
    int bufSize = DspUtils::nextPowerOf2 (maxSamples + 1);
    buffer.assign (static_cast<size_t> (bufSize), 0.0f);
    mask = bufSize - 1;
    writePos = 0;
}

void QuadTank::Allpass::clear()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

// -----------------------------------------------------------------------
QuadTank::QuadTank()
{
    for (int t = 0; t < kNumTanks; ++t)
    {
        tanks_[t].ap1BaseDelay     = kTankConfigs[t].ap1Base;
        tanks_[t].delay1BaseDelay  = kTankConfigs[t].del1Base;
        tanks_[t].ap2BaseDelay     = kTankConfigs[t].ap2Base;
        tanks_[t].delay2BaseDelay  = kTankConfigs[t].del2Base;
        for (int i = 0; i < kNumDensityAPs; ++i)
            tanks_[t].densityAPBase[i] = kTankConfigs[t].densityAPBase[i];
    }
}

// -----------------------------------------------------------------------
void QuadTank::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;
    float rateRatio = static_cast<float> (sampleRate / kBaseSampleRate);
    const int maxModExcursion = static_cast<int> (std::ceil (32.0 * sampleRate / 44100.0));

    for (int t = 0; t < kNumTanks; ++t)
    {
        auto& tank = tanks_[t];

        int ap1Max = static_cast<int> (std::ceil (tank.ap1BaseDelay * rateRatio * sizeRangeMax_)) + maxModExcursion;
        int del1Max = static_cast<int> (std::ceil (tank.delay1BaseDelay * rateRatio * sizeRangeMax_)) + maxModExcursion;
        int ap2Max = static_cast<int> (std::ceil (tank.ap2BaseDelay * rateRatio * sizeRangeMax_)) + maxModExcursion;
        int del2Max = static_cast<int> (std::ceil (tank.delay2BaseDelay * rateRatio * sizeRangeMax_)) + maxModExcursion;

        tank.ap1Buffer.allocate (ap1Max);
        tank.delay1.allocate (del1Max + maxModExcursion);
        tank.ap2.allocate (ap2Max);
        tank.delay2.allocate (del2Max + maxModExcursion);

        for (int i = 0; i < kNumDensityAPs; ++i)
        {
            int dapMax = static_cast<int> (std::ceil (tank.densityAPBase[i] * rateRatio * sizeRangeMax_)) + 4;
            tank.densityAP[i].allocate (dapMax);
        }

        tank.damping.reset();
        tank.crossFeedState = 0.0f;
    }

    // Initialize LFO and PRNG with unique seeds per tank (90° phase offsets)
    static constexpr uint32_t kLFOSeeds[kNumTanks]   = { 0x12345678u, 0x87654321u, 0xABCDEF01u, 0x13579BDFu };
    static constexpr uint32_t kNoiseSeeds[kNumTanks]  = { 0xDEADBEEFu, 0xCAFEBABEu, 0xFEEDFACEu, 0xBAADF00Du };
    static constexpr float    kPhaseOffsets[kNumTanks] = { 0.0f, 1.5707963f, 3.1415927f, 4.7123890f };

    for (int t = 0; t < kNumTanks; ++t)
    {
        tanks_[t].lfoPhase  = kPhaseOffsets[t];
        tanks_[t].lfoPRNG   = kLFOSeeds[t];
        tanks_[t].noiseState = kNoiseSeeds[t];
    }

    prepared_ = true;

    updateDelayLengths();
    updateDecayCoefficients();
    updateLFORates();
    setModDepth (lastModDepthRaw_);
}

// -----------------------------------------------------------------------
void QuadTank::process (const float* inputL, const float* inputR,
                        float* outputL, float* outputR, int numSamples)
{
    if (! prepared_)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        float input = (inputL[i] + inputR[i]) * 0.5f;
        if (frozen_)
            input = 0.0f;

        // Save all cross-feed states before processing
        float cf[kNumTanks];
        for (int t = 0; t < kNumTanks; ++t)
            cf[t] = tanks_[t].crossFeedState;

        // Process each tank with circular cross-coupling (0←3, 1←0, 2←1, 3←2)
        for (int t = 0; t < kNumTanks; ++t)
        {
            auto& tank = tanks_[t];
            float otherCrossFeed = cf[(t + kNumTanks - 1) % kNumTanks];
            float tankIn = input + otherCrossFeed;

            // --- Modulated allpass (decay diffusion 1) ---
            float mod = std::sin (tank.lfoPhase) * modDepthSamples_;
            float ap1ReadDelay = tank.ap1DelaySamples + mod;
            ap1ReadDelay = std::max (ap1ReadDelay, 1.0f);

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

            // --- Delay 1 with noise jitter ---
            float jitter1 = nextDrift (tank.noiseState) * noiseModDepth_;
            float del1Read = tank.delay1Samples + jitter1;
            del1Read = std::max (del1Read, 1.0f);
            float del1Out = tank.delay1.readInterpolated (del1Read);
            tank.delay1.write (ap1Out);

            // --- Density cascade: 3 allpasses ---
            float dense = del1Out;
            if (! frozen_)
            {
                for (int d = 0; d < kNumDensityAPs; ++d)
                    dense = tank.densityAP[d].process (dense, densityDiffCoeff_);
            }

            // --- Two-band damping ---
            float damped = frozen_ ? dense : tank.damping.process (dense);

            // --- Static allpass (decay diffusion 2) ---
            float coeff2 = frozen_ ? 0.0f : decayDiff2_;
            float ap2Out = tank.ap2.process (damped, coeff2);

            // --- Delay 2 with noise jitter ---
            float jitter2 = nextDrift (tank.noiseState) * noiseModDepth_;
            float del2Read = tank.delay2Samples + jitter2;
            del2Read = std::max (del2Read, 1.0f);
            float del2Out = tank.delay2.readInterpolated (del2Read);
            float bias = ((tank.delay2.writePos ^ 1) & 1) ? +DspUtils::kDenormalPrevention
                                                            : -DspUtils::kDenormalPrevention;
            tank.delay2.write (ap2Out + bias);

            // Cross-feed: feeds next tank
            tank.crossFeedState = del2Out;
        }

        // ------------------------------------------------------------------
        // Output: sum 14 signed taps from all 4 tanks per channel
        float outL = 0.0f;
        for (int t = 0; t < kNumOutputTaps; ++t)
            outL += readOutputTap (kLeftOutputTaps[t]) * kLeftOutputTaps[t].sign;

        float outR = 0.0f;
        for (int t = 0; t < kNumOutputTaps; ++t)
            outR += readOutputTap (kRightOutputTaps[t]) * kRightOutputTaps[t].sign;

        // Normalize output tap sum. With alternating signs on highly correlated
        // taps (same tank, adjacent delay positions), effective independence is
        // ~8 taps, not 48 — 1/sqrt(48) severely over-attenuates.
        // 0.35 ≈ 1/sqrt(8) compensates for sign cancellation and the slow
        // energy buildup of long-loop tanks vs shorter-loop FDN/DattorroTank.
        constexpr float kOutputScale = 0.35f;  // ~1/sqrt(8)

        outputL[i] = std::clamp (outL * kOutputScale, -kSafetyClip, kSafetyClip);
        outputR[i] = std::clamp (outR * kOutputScale, -kSafetyClip, kSafetyClip);
    }
}

// -----------------------------------------------------------------------
float QuadTank::readOutputTap (const OutputTap& tap) const
{
    // Buffer index mapping:
    //   0-3:  Delay1 from tanks 0-3
    //   4-7:  Delay2 from tanks 0-3
    //   8-11: AP2 from tanks 0-3
    int tankIdx = tap.bufferIndex % kNumTanks;
    int bufType = tap.bufferIndex / kNumTanks;  // 0=del1, 1=del2, 2=ap2

    const auto& tank = tanks_[tankIdx];

    if (bufType == 2)
    {
        // Read from AP2 internal buffer at fractional position
        const auto& ap = tank.ap2;
        int tapOffset = static_cast<int> (tap.positionFrac * static_cast<float> (ap.delaySamples));
        tapOffset = std::max (tapOffset, 1);
        return ap.buffer[static_cast<size_t> ((ap.writePos - tapOffset) & ap.mask)];
    }

    const DelayLine* delayBuf = (bufType == 0) ? &tank.delay1 : &tank.delay2;
    float totalDelay = (bufType == 0) ? tank.delay1Samples : tank.delay2Samples;

    float tapDelay = tap.positionFrac * totalDelay;
    tapDelay = std::max (tapDelay, 1.0f);
    return delayBuf->readInterpolated (tapDelay);
}

// -----------------------------------------------------------------------
void QuadTank::setDecayTime (float seconds)
{
    decayTime_ = std::max (seconds, 0.1f);
    if (prepared_) updateDecayCoefficients();
}

void QuadTank::setBassMultiply (float mult)
{
    bassMultiply_ = std::max (mult, 0.1f);
    if (prepared_) updateDecayCoefficients();
}

void QuadTank::setTrebleMultiply (float mult)
{
    trebleMultiply_ = std::max (mult, 0.1f);
    if (prepared_) updateDecayCoefficients();
}

void QuadTank::setCrossoverFreq (float hz)
{
    crossoverFreq_ = hz;
    if (prepared_) updateDecayCoefficients();
}

void QuadTank::setModDepth (float depth)
{
    lastModDepthRaw_ = depth;
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    modDepthSamples_ = depth * 16.0f * rateRatio;
    noiseModDepth_ = depth * 12.0f * rateRatio;  // Match DattorroTank (12 samples peak)
}

void QuadTank::setModRate (float hz)
{
    modRateHz_ = hz;
    if (prepared_) updateLFORates();
}

void QuadTank::setSize (float size)
{
    sizeParam_ = std::clamp (size, 0.0f, 1.0f);
    if (prepared_)
    {
        updateDelayLengths();
        updateDecayCoefficients();
    }
}

void QuadTank::setFreeze (bool frozen) { frozen_ = frozen; }
void QuadTank::setLateGainScale (float scale) { lateGainScale_ = scale; }

void QuadTank::setHighCrossoverFreq (float hz)
{
    highCrossoverFreq_ = std::max (hz, 100.0f);
    if (prepared_)
        updateDecayCoefficients();
}

void QuadTank::setAirDampingScale (float scale)
{
    airDampingScale_ = std::max (scale, 0.1f);
    if (prepared_)
        updateDecayCoefficients();
}

void QuadTank::setSizeRange (float min, float max)
{
    sizeRangeMin_ = min;
    sizeRangeMax_ = max;
    if (prepared_)
    {
        updateDelayLengths();
        updateDecayCoefficients();
    }
}

void QuadTank::clearBuffers()
{
    for (int t = 0; t < kNumTanks; ++t)
    {
        auto& tank = tanks_[t];
        tank.ap1Buffer.clear();
        tank.delay1.clear();
        for (int i = 0; i < kNumDensityAPs; ++i)
            tank.densityAP[i].clear();
        tank.ap2.clear();
        tank.delay2.clear();
        tank.damping.reset();
        tank.crossFeedState = 0.0f;
    }
}

// -----------------------------------------------------------------------
void QuadTank::updateDelayLengths()
{
    float rateRatio = static_cast<float> (sampleRate_ / kBaseSampleRate);
    float sizeScale = sizeRangeMin_ + (sizeRangeMax_ - sizeRangeMin_) * sizeParam_;

    for (int t = 0; t < kNumTanks; ++t)
    {
        auto& tank = tanks_[t];
        tank.ap1DelaySamples = static_cast<float> (tank.ap1BaseDelay) * rateRatio * sizeScale;
        tank.delay1Samples   = static_cast<float> (tank.delay1BaseDelay) * rateRatio * sizeScale;
        tank.delay2Samples   = static_cast<float> (tank.delay2BaseDelay) * rateRatio * sizeScale;

        tank.ap2.delaySamples = std::max (1, static_cast<int> (
            static_cast<float> (tank.ap2BaseDelay) * rateRatio * sizeScale));

        for (int i = 0; i < kNumDensityAPs; ++i)
            tank.densityAP[i].delaySamples = std::max (1, static_cast<int> (
                static_cast<float> (tank.densityAPBase[i]) * rateRatio * sizeScale));
    }
}

void QuadTank::updateDecayCoefficients()
{
    float sr = static_cast<float> (sampleRate_);
    float lowCrossoverCoeff = std::exp (-kTwoPi * crossoverFreq_ / sr);
    float highCrossoverCoeff = std::exp (-kTwoPi * highCrossoverFreq_ / sr);

    for (int t = 0; t < kNumTanks; ++t)
    {
        auto& tank = tanks_[t];

        float loopLength = tank.ap1DelaySamples
                         + tank.delay1Samples
                         + static_cast<float> (tank.ap2.delaySamples)
                         + tank.delay2Samples;
        for (int i = 0; i < kNumDensityAPs; ++i)
            loopLength += static_cast<float> (tank.densityAP[i].delaySamples);

        float gBase = std::pow (10.0f, -3.0f * loopLength / (decayTime_ * sr));
        float gLow  = std::pow (gBase, 1.0f / bassMultiply_);
        float gMid  = gBase;  // mid band decays at natural rate
        float gHigh = std::pow (gBase, 1.0f / (trebleMultiply_ * airDampingScale_));

        tank.damping.setCoefficients (gLow, gMid, gHigh, lowCrossoverCoeff, highCrossoverCoeff);
    }
}

void QuadTank::updateLFORates()
{
    float sr = static_cast<float> (sampleRate_);
    // 4 asymmetric rates using irrational multipliers to prevent correlation
    static constexpr float kRateMultipliers[kNumTanks] = {
        1.0f, 1.1180339887f, 0.8944271910f, 1.2360679775f  // 1, √5/2, 2/√5, (1+√5)/2/φ
    };
    for (int t = 0; t < kNumTanks; ++t)
        tanks_[t].lfoPhaseInc = kTwoPi * modRateHz_ * kRateMultipliers[t] / sr;
}
