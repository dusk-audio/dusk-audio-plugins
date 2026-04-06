#pragma once

#include "AlgorithmConfig.h"
#include "DspUtils.h"

#include <algorithm>
#include <atomic>
#include <vector>

// Multi-tap delay line generating discrete early reflections.
//
// Two modes:
// 1. **Generated** (default): 24 taps per channel with exponentially-distributed
//    delay times (8-80ms), inverse distance gain rolloff, and per-tap air absorption.
// 2. **Custom** (when setCustomTaps is called): uses a fixed mono-panned tap table
//    from AlgorithmConfig. Each tap outputs to L or R only (never both).
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

    // Load a custom mono-panned tap table from AlgorithmConfig.
    // When numTaps > 0, the custom table overrides the generated tap formula.
    // Pass numTaps=0 to revert to generated mode.
    // preDelayMs: DV's current pre-delay in ms. VV-extracted tap times are absolute
    // (include VV's pre-delay), so we subtract DV's pre-delay to compensate since
    // the ER engine receives already-pre-delayed input. Taps that would go negative
    // are clamped to 1 sample.
    void setCustomTaps (const CustomERTap* taps, int numTaps, float preDelayMs = 0.0f);

private:
    static constexpr int kNumTaps = 24;
    static constexpr float kMinTimeMs = 8.0f;
    static constexpr float kMaxTimeMs = 80.0f;
    static constexpr float kMaxBufferMs = 120.0f; // Buffer headroom for erTimeScale up to 1.5
    static constexpr float kTwoPi = 6.283185307179586f;

    // Polarity signs per tap — 12 positive, 12 negative per channel.
    // Breaks up comb filtering from closely-spaced all-positive taps.
    // L/R patterns differ for stereo decorrelation.
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

    // Mono-panned custom tap (used in custom mode).
    struct MonoTap
    {
        int delaySamples = 0;
        float gain = 0.0f;     // Amplitude (sign included)
        int channel = 0;       // 0 = left, 1 = right
        float lpCoeff = 0.0f;  // Air absorption per tap
        float lpState = 0.0f;
    };

    // Post-tap decorrelation allpass (Schroeder)
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

    // Generated mode taps
    Tap tapsL_[kNumTaps] {};
    Tap tapsR_[kNumTaps] {};

    // Custom mode taps
    MonoTap customTaps_[kMaxCustomERTaps] {};
    int numCustomTaps_ = 0;
    bool useCustomTaps_ = false;

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
    void updateCustomTaps();
};
