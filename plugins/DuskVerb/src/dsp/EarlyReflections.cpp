#include "EarlyReflections.h"
#include "DspUtils.h"

#include <algorithm>
#include <cmath>

void EarlyReflections::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;

    // Buffer for max tap time at any erTimeScale (up to 1.5) and erSize
    int maxSamples = static_cast<int> (std::ceil (kMaxBufferMs * 0.001f
                                                  * static_cast<float> (sampleRate))) + 1;
    int bufSize = DspUtils::nextPowerOf2 (maxSamples);

    bufferL_.assign (static_cast<size_t> (bufSize), 0.0f);
    bufferR_.assign (static_cast<size_t> (bufSize), 0.0f);
    writePos_ = 0;
    mask_ = bufSize - 1;

    // Decorrelation allpass delays: different primes for L vs R
    // At 44.1kHz base rate: L = {139, 193}, R = {167, 211}
    double rateScale = sampleRate / 44100.0;
    decorr_L1_.init (std::max (1, static_cast<int> (139 * rateScale)));
    decorr_L2_.init (std::max (1, static_cast<int> (193 * rateScale)));
    decorr_R1_.init (std::max (1, static_cast<int> (167 * rateScale)));
    decorr_R2_.init (std::max (1, static_cast<int> (211 * rateScale)));

    if (useCustomTaps_)
        updateCustomTaps();
    else
        updateTaps();

    prepared_ = true;
}

void EarlyReflections::process (const float* inputL, const float* inputR,
                                float* outputL, float* outputR, int numSamples)
{
    if (! prepared_)
        return;

    if (tapsNeedUpdate_.exchange (false, std::memory_order_acquire))
    {
        if (useCustomTaps_)
            updateCustomTaps();
        else
            updateTaps();
    }

    float localDecorrCoeff = decorrCoeff_.load (std::memory_order_acquire);

    if (useCustomTaps_)
    {
        // Custom mono-panned tap mode
        for (int i = 0; i < numSamples; ++i)
        {
            bufferL_[static_cast<size_t> (writePos_)] = inputL[i];
            bufferR_[static_cast<size_t> (writePos_)] = inputR[i];

            float outL = 0.0f, outR = 0.0f;

            for (int t = 0; t < numCustomTaps_; ++t)
            {
                auto& tap = customTaps_[t];

                // Read from the channel's buffer at the tap delay
                int readPos = static_cast<int> ((static_cast<unsigned> (writePos_)
                            - static_cast<unsigned> (tap.delaySamples))
                            & static_cast<unsigned> (mask_));

                // Each tap reads from its assigned channel's input buffer
                float raw = (tap.channel == 0)
                    ? bufferL_[static_cast<size_t> (readPos)]
                    : bufferR_[static_cast<size_t> (readPos)];

                float tapVal = raw * tap.gain;

                // Air absorption LP per tap
                tap.lpState = (1.0f - tap.lpCoeff) * tapVal
                            + tap.lpCoeff * tap.lpState
                            + DspUtils::kDenormalPrevention;

                // Output to assigned channel only (mono-panned)
                if (tap.channel == 0)
                    outL += tap.lpState;
                else
                    outR += tap.lpState;
            }

            // Post-tap decorrelation (same as generated mode)
            if (localDecorrCoeff > 0.0f)
            {
                outL = decorr_L1_.process (outL, localDecorrCoeff);
                outL = decorr_L2_.process (outL, localDecorrCoeff);
                outR = decorr_R1_.process (outR, localDecorrCoeff);
                outR = decorr_R2_.process (outR, localDecorrCoeff);
            }

            outputL[i] = outL;
            outputR[i] = outR;

            writePos_ = (writePos_ + 1) & mask_;
        }
        return;
    }

    // Generated tap mode (original code path)
    for (int i = 0; i < numSamples; ++i)
    {
        // Store input into internal delay buffers
        bufferL_[static_cast<size_t> (writePos_)] = inputL[i];
        bufferR_[static_cast<size_t> (writePos_)] = inputR[i];

        float outL = 0.0f, outR = 0.0f;

        for (int t = 0; t < kNumTaps; ++t)
        {
            // Left tap: read, apply gain, filter for air absorption
            int readL = static_cast<int> ((static_cast<unsigned> (writePos_)
                       - static_cast<unsigned> (tapsL_[t].delaySamples))
                       & static_cast<unsigned> (mask_));
            float tapL = bufferL_[static_cast<size_t> (readL)] * tapsL_[t].gain;
            tapsL_[t].lpState = (1.0f - tapsL_[t].lpCoeff) * tapL
                              + tapsL_[t].lpCoeff * tapsL_[t].lpState
                              + DspUtils::kDenormalPrevention;
            outL += tapsL_[t].lpState;

            // Right tap: different delay pattern for stereo width
            int readR = static_cast<int> ((static_cast<unsigned> (writePos_)
                       - static_cast<unsigned> (tapsR_[t].delaySamples))
                       & static_cast<unsigned> (mask_));
            float tapR = bufferR_[static_cast<size_t> (readR)] * tapsR_[t].gain;
            tapsR_[t].lpState = (1.0f - tapsR_[t].lpCoeff) * tapR
                              + tapsR_[t].lpCoeff * tapsR_[t].lpState
                              + DspUtils::kDenormalPrevention;
            outR += tapsR_[t].lpState;
        }

        // Post-tap decorrelation: cascaded Schroeder allpasses with different
        // prime delays per channel create phase differences for stereo widening
        if (localDecorrCoeff > 0.0f)
        {
            outL = decorr_L1_.process (outL, localDecorrCoeff);
            outL = decorr_L2_.process (outL, localDecorrCoeff);
            outR = decorr_R1_.process (outR, localDecorrCoeff);
            outR = decorr_R2_.process (outR, localDecorrCoeff);
        }

        outputL[i] = outL;
        outputR[i] = outR;

        writePos_ = (writePos_ + 1) & mask_;
    }
}

void EarlyReflections::setSize (float size)
{
    erSize_ = std::clamp (size, 0.0f, 1.0f);
    if (prepared_)
        tapsNeedUpdate_.store (true, std::memory_order_release);
}

void EarlyReflections::setTimeScale (float scale)
{
    // Clamp to [0.1, 1.5] — buffer is sized for kMaxTimeMs * 1.5
    timeScale_ = std::clamp (scale, 0.1f, 1.5f);
    if (prepared_)
        tapsNeedUpdate_.store (true, std::memory_order_release);
}

void EarlyReflections::setGainExponent (float exponent)
{
    gainExponent_ = std::clamp (exponent, 0.0f, 2.0f);
    if (prepared_)
        tapsNeedUpdate_.store (true, std::memory_order_release);
}

void EarlyReflections::setAirAbsorptionFloor (float hz)
{
    airAbsorptionFloorHz_ = std::clamp (hz, 1000.0f, 12000.0f);
    if (prepared_)
        tapsNeedUpdate_.store (true, std::memory_order_release);
}

void EarlyReflections::setAirAbsorptionCeiling (float hz)
{
    airAbsorptionCeilingHz_ = std::clamp (hz, 8000.0f, 20000.0f);
    if (prepared_)
        tapsNeedUpdate_.store (true, std::memory_order_release);
}

void EarlyReflections::setDecorrCoeff (float coeff)
{
    decorrCoeff_.store (std::clamp (coeff, 0.0f, 0.7f), std::memory_order_release);
}

void EarlyReflections::setCustomTaps (const CustomERTap* taps, int numTaps)
{
    if (numTaps <= 0 || taps == nullptr)
    {
        useCustomTaps_ = false;
        numCustomTaps_ = 0;
        if (prepared_)
            tapsNeedUpdate_.store (true, std::memory_order_release);
        return;
    }

    numCustomTaps_ = std::min (numTaps, kMaxCustomERTaps);

    float sr = static_cast<float> (sampleRate_);
    float maxDelaySamples = static_cast<float> (mask_);

    for (int i = 0; i < numCustomTaps_; ++i)
    {
        customTaps_[i].delaySamples = std::clamp (
            static_cast<int> (taps[i].delayMs * 0.001f * sr),
            1, static_cast<int> (maxDelaySamples));
        customTaps_[i].gain = taps[i].amplitude;
        customTaps_[i].channel = (taps[i].channel == 0) ? 0 : 1;
        customTaps_[i].lpState = 0.0f;
    }

    // Normalize so absolute gains sum to 1.0 (matching generated ER normalization).
    // This ensures erLevelScale controls the final level identically in both modes.
    float absSum = 0.0f;
    for (int i = 0; i < numCustomTaps_; ++i)
        absSum += std::abs (customTaps_[i].gain);
    if (absSum > 0.0f)
        for (int i = 0; i < numCustomTaps_; ++i)
            customTaps_[i].gain /= absSum;

    useCustomTaps_ = true;

    if (prepared_)
        updateCustomTaps();
}

void EarlyReflections::updateCustomTaps()
{
    // Apply air absorption coefficients based on tap delay.
    // Earlier taps get higher cutoff (brighter), later taps get lower cutoff (darker).
    float sr = static_cast<float> (sampleRate_);

    // Find min/max delay for normalization
    float minDelay = 1e10f, maxDelay = 0.0f;
    for (int i = 0; i < numCustomTaps_; ++i)
    {
        float d = static_cast<float> (customTaps_[i].delaySamples);
        minDelay = std::min (minDelay, d);
        maxDelay = std::max (maxDelay, d);
    }
    float range = std::max (maxDelay - minDelay, 1.0f);

    for (int i = 0; i < numCustomTaps_; ++i)
    {
        float t = (static_cast<float> (customTaps_[i].delaySamples) - minDelay) / range;
        float cutoff = airAbsorptionCeilingHz_
                     * std::pow (airAbsorptionFloorHz_ / airAbsorptionCeilingHz_, t);
        customTaps_[i].lpCoeff = std::exp (-kTwoPi * cutoff / sr);
    }
}

void EarlyReflections::updateTaps()
{
    static_assert (kNumTaps > 1, "kNumTaps must be > 1 to avoid division by zero");
    // sizeScale ranges from 0.3 (small room) to 1.0 (large hall)
    float sizeScale = (0.3f + 0.7f * erSize_) * timeScale_;
    float sr = static_cast<float> (sampleRate_);
    float timeRatio = kMaxTimeMs / kMinTimeMs; // 16x range

    for (int i = 0; i < kNumTaps; ++i)
    {
        // --- Left channel: exponential distribution of tap times ---
        float tL = static_cast<float> (i) / static_cast<float> (kNumTaps - 1);
        float timeMsL = kMinTimeMs * std::pow (timeRatio, tL) * sizeScale;

        // --- Right channel: shifted index for stereo decorrelation ---
        // Offset by 0.37 taps to create a different delay pattern
        float tR = (static_cast<float> (i) + 0.37f)
                 / (static_cast<float> (kNumTaps - 1) + 0.37f);
        float timeMsR = kMinTimeMs * std::pow (timeRatio, tR) * sizeScale;

        tapsL_[i].delaySamples = std::max (1, static_cast<int> (timeMsL * 0.001f * sr));
        tapsR_[i].delaySamples = std::max (1, static_cast<int> (timeMsR * 0.001f * sr));

        // Distance attenuation with configurable exponent:
        // 1.0 = inverse distance (default), 0.5 = sqrt (gentler), 0.0 = flat
        float normL = timeMsL / kMinTimeMs;
        float normR = timeMsR / kMinTimeMs;
        float attenL = (gainExponent_ > 0.0f) ? std::pow (normL, gainExponent_) : 1.0f;
        float attenR = (gainExponent_ > 0.0f) ? std::pow (normR, gainExponent_) : 1.0f;
        tapsL_[i].gain = kSignsL[i] / attenL;
        tapsR_[i].gain = kSignsR[i] / attenR;

        // Air absorption: one-pole lowpass per tap
        // Cutoff sweeps from airAbsorptionCeilingHz_ (earliest) to airAbsorptionFloorHz_ (latest)
        float cutoffL = airAbsorptionCeilingHz_ * std::pow (airAbsorptionFloorHz_ / airAbsorptionCeilingHz_, tL);
        float cutoffR = airAbsorptionCeilingHz_ * std::pow (airAbsorptionFloorHz_ / airAbsorptionCeilingHz_, tR);
        tapsL_[i].lpCoeff = std::exp (-kTwoPi * cutoffL / sr);
        tapsR_[i].lpCoeff = std::exp (-kTwoPi * cutoffR / sr);
    }

    // Normalize so absolute gains sum to 1.0 (preserving signs).
    float sumL = 0.0f, sumR = 0.0f;
    for (int i = 0; i < kNumTaps; ++i)
    {
        sumL += std::abs (tapsL_[i].gain);
        sumR += std::abs (tapsR_[i].gain);
    }
    if (sumL > 0.0f)
        for (int i = 0; i < kNumTaps; ++i)
            tapsL_[i].gain /= sumL;
    if (sumR > 0.0f)
        for (int i = 0; i < kNumTaps; ++i)
            tapsR_[i].gain /= sumR;
}
