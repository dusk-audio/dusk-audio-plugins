#include "EarlyReflections.h"
#include "DspUtils.h"

#include <algorithm>
#include <cmath>

void EarlyReflections::prepare (double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;

    // Buffer for max tap time (80ms) at current sample rate
    int maxSamples = static_cast<int> (std::ceil (kMaxTimeMs * 0.001f
                                                  * static_cast<float> (sampleRate))) + 1;
    int bufSize = DspUtils::nextPowerOf2 (maxSamples);

    bufferL_.assign (static_cast<size_t> (bufSize), 0.0f);
    bufferR_.assign (static_cast<size_t> (bufSize), 0.0f);
    writePos_ = 0;
    mask_ = bufSize - 1;

    updateTaps();
    prepared_ = true;
}

void EarlyReflections::process (const float* inputL, const float* inputR,
                                float* outputL, float* outputR, int numSamples)
{
    if (! prepared_)
        return;

    if (tapsNeedUpdate_.exchange (false, std::memory_order_acquire))
        updateTaps();

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
    // Clamp to [0.1, 1.0] — buffer is sized for kMaxTimeMs at scale 1.0
    timeScale_ = std::clamp (scale, 0.1f, 1.0f);
    if (prepared_)
        tapsNeedUpdate_.store (true, std::memory_order_release);
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

        // Inverse distance law: gain ∝ 1/distance ∝ 1/time
        float normL = timeMsL / kMinTimeMs;
        float normR = timeMsR / kMinTimeMs;
        tapsL_[i].gain = 1.0f / normL;
        tapsR_[i].gain = 1.0f / normR;

        // Air absorption: one-pole lowpass per tap
        // Cutoff sweeps from 12kHz (earliest) to 2kHz (latest)
        float cutoffL = 12000.0f * std::pow (2000.0f / 12000.0f, tL);
        float cutoffR = 12000.0f * std::pow (2000.0f / 12000.0f, tR);
        tapsL_[i].lpCoeff = std::exp (-kTwoPi * cutoffL / sr);
        tapsR_[i].lpCoeff = std::exp (-kTwoPi * cutoffR / sr);

        tapsL_[i].lpState = 0.0f;
        tapsR_[i].lpState = 0.0f;
    }

    // Normalize tap gains so each channel sums to 1.0.
    // Without this, 16 inverse-distance-law taps sum to ~5.7x gain.
    float sumL = 0.0f, sumR = 0.0f;
    for (int i = 0; i < kNumTaps; ++i)
    {
        sumL += tapsL_[i].gain;
        sumR += tapsR_[i].gain;
    }
    if (sumL > 0.0f)
        for (int i = 0; i < kNumTaps; ++i)
            tapsL_[i].gain /= sumL;
    if (sumR > 0.0f)
        for (int i = 0; i < kNumTaps; ++i)
            tapsR_[i].gain /= sumR;
}
