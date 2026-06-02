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
    // Rising-onset envelope: tap gains ramp UP to a peak at onsetRiseMs then
    // decay (vs the default inverse-distance rolloff that peaks at the first
    // tap). Reproduces VVV's gentle ~15 ms attack swell instead of an
    // instantaneous first-tap spike. 0 = off → legacy rolloff → bit-identical.
    void setOnsetRiseMs (float ms);
    void setAirAbsorptionFloor (float hz);
    void setAirAbsorptionCeiling (float hz);
    void setDecorrCoeff (float coeff);

    // Zero the multi-tap delay buffers, per-tap lowpass states, and the
    // decorrelating allpass buffers. Tap geometry (delay, gain, lpCoeff) is
    // preserved — only signal-carrying state is reset. Used by the
    // processor's preset-swap path so an idle engine can be brought back
    // online without leaking stale audio through the ER stage.
    void clear()
    {
        std::fill (bufferL_.begin(), bufferL_.end(), 0.0f);
        std::fill (bufferR_.begin(), bufferR_.end(), 0.0f);
        writePos_ = 0;
        for (int i = 0; i < kNumTaps; ++i)
        {
            tapsL_[i].lpState = 0.0f;
            tapsR_[i].lpState = 0.0f;
        }
        decorr_L1_.clear();
        decorr_L2_.clear();
        decorr_R1_.clear();
        decorr_R2_.clear();
    }

    // Copy SIGNAL state from another EarlyReflections (assumed prepared at the
    // same sample rate, so buffer sizes match). Tap geometry (delay/gain/
    // lpCoeff) stays at this instance's values — only the multi-tap delay
    // line, per-tap lowpass states, and decorrelating allpass buffers are
    // overwritten. Used by the preset-swap path so the new engine's ER taps
    // fire from real input history at sample 0 instead of from silence (the
    // 8-80 ms tap delay range otherwise produces audible discrete step
    // onsets that the equal-power crossfade can't fully mask).
    void copySignalStateFrom (const EarlyReflections& other)
    {
        if (bufferL_.size() == other.bufferL_.size())
        {
            bufferL_ = other.bufferL_;
            bufferR_ = other.bufferR_;
            writePos_ = other.writePos_;
        }
        for (int i = 0; i < kNumTaps; ++i)
        {
            tapsL_[i].lpState = other.tapsL_[i].lpState;
            tapsR_[i].lpState = other.tapsR_[i].lpState;
        }
        decorr_L1_.copyStateFrom (other.decorr_L1_);
        decorr_L2_.copyStateFrom (other.decorr_L2_);
        decorr_R1_.copyStateFrom (other.decorr_R1_);
        decorr_R2_.copyStateFrom (other.decorr_R2_);
    }

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

        void copyStateFrom (const DecorrAllpass& other)
        {
            if (buffer.size() == other.buffer.size())
            {
                buffer = other.buffer;
                writePos = other.writePos;
            }
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
    float onsetRiseMs_ = 0.0f;   // 0 = legacy rolloff; >0 = rising-onset peak
    float airAbsorptionFloorHz_ = 2000.0f;
    float airAbsorptionCeilingHz_ = 12000.0f;
    double sampleRate_ = 44100.0;
    bool prepared_ = false;
    std::atomic<bool> tapsNeedUpdate_ { false };

    void updateTaps();
};
