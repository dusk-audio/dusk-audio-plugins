#pragma once

#include <algorithm>
#include <atomic>
#include <vector>

// Multi-tap delay line generating discrete early reflections.
// 24 taps per channel with exponentially-distributed delay times (8-80ms),
// inverse distance gain rolloff, and per-tap air absorption.
class EarlyReflections
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void process (const float* inputL, const float* inputR,
                  float* outputL, float* outputR, int numSamples);

    void setSize (float size);
    void setTimeScale (float scale);
    void setGainExponent (float exponent);
    void setAirAbsorptionFloor (float hz);
    void setAirAbsorptionCeiling (float hz);
    void setDecorrCoeff (float coeff);

private:
    static constexpr int kNumTaps = 24;
    static constexpr float kMinTimeMs = 8.0f;
    static constexpr float kMaxTimeMs = 80.0f;
    static constexpr float kMaxBufferMs = 120.0f;
    static constexpr float kTwoPi = 6.283185307179586f;

    // 12 positive / 12 negative per channel breaks comb filtering.
    static constexpr float kSignsL[kNumTaps] = {
         1, -1,  1,  1, -1,  1, -1, -1,
         1, -1, -1,  1,  1, -1,  1, -1,
        -1,  1,  1, -1,  1, -1, -1,  1
    };
    static constexpr float kSignsR[kNumTaps] = {
        -1,  1, -1,  1,  1, -1,  1, -1,
        -1,  1,  1, -1, -1,  1, -1,  1,
         1, -1, -1,  1, -1,  1,  1, -1
    };

    struct Tap
    {
        int delaySamples = 0;
        float gain = 0.0f;
        float lpCoeff = 0.0f;
        float lpState = 0.0f;
    };

    struct DecorrAllpass
    {
        std::vector<float> buffer;
        int writePos = 0;
        int length = 0;

        void init (int delaySamples)
        {
            length = delaySamples;
            buffer.assign (static_cast<size_t> (length), 0.0f);
            writePos = 0;
        }

        float process (float input, float g)
        {
            int readPos = ((writePos - length) % length + length) % length;
            float delayed = buffer[static_cast<size_t> (readPos)];
            float v = input - g * delayed;
            buffer[static_cast<size_t> (writePos)] = v;
            writePos = (writePos + 1) % length;
            return delayed + g * v;
        }

        void clear()
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            writePos = 0;
        }
    };

    std::vector<float> bufferL_;
    std::vector<float> bufferR_;
    int writePos_ = 0;
    int mask_ = 0;

    Tap tapsL_[kNumTaps] {};
    Tap tapsR_[kNumTaps] {};

    // Two cascaded allpasses per channel, different prime delays for L vs R
    DecorrAllpass decorr_L1_, decorr_L2_;
    DecorrAllpass decorr_R1_, decorr_R2_;
    std::atomic<float> decorrCoeff_ { 0.0f }; // 0 = bypassed

    float erSize_ = 1.0f;
    float timeScale_ = 1.0f;
    float gainExponent_ = 1.0f;
    float airAbsorptionFloorHz_ = 2000.0f;
    float airAbsorptionCeilingHz_ = 12000.0f;
    double sampleRate_ = 44100.0;
    bool prepared_ = false;
    std::atomic<bool> tapsNeedUpdate_ { false };

    void updateTaps();
};
