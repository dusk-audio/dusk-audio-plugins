#pragma once

#include "DspUtils.h"

#include <atomic>
#include <vector>

// Multi-tap delay line generating discrete early reflections.
// 16 taps per channel with exponentially-distributed delay times (5-80ms),
// inverse distance gain rolloff, and per-tap air absorption filtering.
// Left and right channels use different tap patterns for stereo decorrelation.
class EarlyReflections
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void process (const float* inputL, const float* inputR,
                  float* outputL, float* outputR, int numSamples);

    void setSize (float size);
    void setTimeScale (float scale);

private:
    static constexpr int kNumTaps = 16;
    static constexpr float kMinTimeMs = 5.0f;
    static constexpr float kMaxTimeMs = 80.0f;
    static constexpr float kTwoPi = 6.283185307179586f;

    struct Tap
    {
        int delaySamples = 0;
        float gain = 0.0f;
        float lpCoeff = 0.0f;
        float lpState = 0.0f;
    };

    std::vector<float> bufferL_;
    std::vector<float> bufferR_;
    int writePos_ = 0;
    int mask_ = 0;

    Tap tapsL_[kNumTaps] {};
    Tap tapsR_[kNumTaps] {};

    float erSize_ = 1.0f;
    float timeScale_ = 1.0f;
    double sampleRate_ = 44100.0;
    bool prepared_ = false;
    std::atomic<bool> tapsNeedUpdate_ { false };

    void updateTaps();
};
