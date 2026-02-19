#pragma once

#include "TwoBandDamping.h"

#include <vector>

class FDNReverb
{
public:
    FDNReverb();

    void prepare (double sampleRate, int maxBlockSize);
    void process (const float* inputL, const float* inputR,
                  float* outputL, float* outputR, int numSamples);

    void setDecayTime (float seconds);
    void setBassMultiply (float mult);
    void setTrebleMultiply (float mult);
    void setCrossoverFreq (float hz);
    void setModDepth (float depth);
    void setModRate (float hz);
    void setSize (float size);
    void setFreeze (bool frozen);

    void setBaseDelays (const int* delays);
    void setOutputTaps (const int* lt, const int* rt,
                        const float* ls, const float* rs);
    void setLateGainScale (float scale);
    void setSizeRange (float min, float max);

private:
    static constexpr int N = 16;
    static constexpr double kBaseSampleRate = 44100.0;
    static constexpr float kTwoPi = 6.283185307179586f;
    static constexpr float kOutputScale = 0.353553f; // 1/sqrt(8) — normalizes 8-tap sum
    static constexpr float kOutputGain  = 2.0f;      // +6dB compensation after tanh
    static constexpr int kNumOutputTaps = 8;

    // Worst-case base delay across all algorithms (for buffer allocation)
    static constexpr int kMaxBaseDelay = 3251;

    // Mutable delay and tap configuration (initialized to Hall defaults)
    int baseDelays_[N];
    int leftTaps_[8];
    int rightTaps_[8];
    float leftSigns_[8];
    float rightSigns_[8];

    float lateGainScale_ = 1.0f;
    float sizeRangeMin_ = 0.5f;
    float sizeRangeMax_ = 1.5f;

    struct DelayLine
    {
        std::vector<float> buffer;
        int writePos = 0;
        int mask = 0;
    };

    DelayLine delayLines_[N];
    TwoBandDamping dampFilter_[N];
    float lfoPhase_[N] {};
    float lfoPhaseInc_[N] {};
    float delayLength_[N] {};

    double sampleRate_ = 44100.0;
    float decayTime_ = 1.0f;
    float bassMultiply_ = 1.0f;
    float trebleMultiply_ = 0.5f;
    float crossoverFreq_ = 1000.0f;
    float modDepth_ = 0.5f;
    float modRateHz_ = 1.0f;
    float modDepthSamples_ = 2.0f;
    float sizeParam_ = 1.0f;
    bool frozen_ = false;
    bool prepared_ = false;

    void updateDelayLengths();
    void updateDecayCoefficients();
    void updateLFORates();
};
