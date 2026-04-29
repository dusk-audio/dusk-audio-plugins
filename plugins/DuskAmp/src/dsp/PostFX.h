// SPDX-License-Identifier: GPL-3.0-or-later

// PostFX.h — Delay (Digital/Analog/Tape) + Dattorro plate reverb

#pragma once

#include <vector>
#include <algorithm>

class PostFX
{
public:
    enum class DelayType { Digital = 0, Analog = 1, Tape = 2 };

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    // Delay
    void setDelayEnabled (bool on);
    void setDelayType (int type);  // 0=Digital, 1=Analog, 2=Tape
    void setDelayTime (float ms);
    void setDelayFeedback (float fb01);
    void setDelayMix (float mix01);

    // Reverb
    void setReverbEnabled (bool on);
    void setReverbMix (float mix01);
    void setReverbDecay (float decay01);
    void setReverbPreDelay (float ms);
    void setReverbDamping (float damping01);
    void setReverbSize (float size01);

    void process (float* left, float* right, int numSamples);

private:
    double sampleRate_ = 44100.0;

    // === DELAY ===
    bool delayEnabled_ = false;
    DelayType delayType_ = DelayType::Digital;
    std::vector<float> delayBufL_, delayBufR_;
    int delayWritePos_ = 0;
    int delaySamples_ = 0;
    float delayFeedback_ = 0.3f;
    float delayMix_ = 0.2f;

    // Feedback filter (hi-cut in delay feedback loop)
    float delayFbFilterStateL_ = 0.0f;
    float delayFbFilterStateR_ = 0.0f;
    float delayFbFilterCoeff_ = 0.7f;

    // Analog delay: LFO for chorus/modulation on read position
    float lfoPhase_ = 0.0f;
    static constexpr float kLFOFreq = 0.6f; // Hz — subtle chorus

    // Tape delay: wow/flutter LFO (slower, random-ish)
    float wowPhase_ = 0.0f;
    float flutterPhase_ = 0.0f;
    static constexpr float kWowFreq = 0.4f;
    static constexpr float kFlutterFreq = 6.5f;

    void updateDelayFbFilterCoeff();
    float readDelayInterp (const std::vector<float>& buf, float readPos) const;

    // === DATTORRO PLATE REVERB ===
    // Based on "Effect Design Part 1" (Jon Dattorro, JAES 1997)
    // Topology: input diffusers → crossed-feedback tank with allpass + delay
    bool reverbEnabled_ = false;
    float reverbMix_ = 0.15f;
    float reverbDecay_ = 0.5f;
    float reverbDamping_ = 0.5f;
    float reverbSize_ = 0.5f;

    // Pre-delay
    std::vector<float> preDelayBuf_;
    int preDelayWritePos_ = 0;
    int preDelaySamples_ = 0;

    // Input diffusers (4 allpass filters in series)
    struct AllpassFilter
    {
        std::vector<float> buffer;
        int writePos = 0;
        int delaySamples = 0;

        void allocate (int maxSamples)
        {
            buffer.assign (static_cast<size_t> (maxSamples), 0.0f);
            writePos = 0;
        }

        void clear()
        {
            std::fill (buffer.begin(), buffer.end(), 0.0f);
            writePos = 0;
        }

        float process (float input, float coeff)
        {
            int bufSize = static_cast<int> (buffer.size());
            if (bufSize == 0) return input;
            int readPos = ((writePos - delaySamples) % bufSize + bufSize) % bufSize;
            float delayed = buffer[static_cast<size_t> (readPos)];
            float output = delayed - coeff * input;
            buffer[static_cast<size_t> (writePos)] = input + coeff * output;
            writePos = (writePos + 1) % bufSize;
            return output;
        }
    };

    // Input diffusers (4x)
    AllpassFilter inputDiffuser_[4];
    static constexpr float kInputDiffCoeff1 = 0.75f;
    static constexpr float kInputDiffCoeff2 = 0.625f;

    // Tank: two parallel delay lines with allpass + damping, cross-fed
    struct TankHalf
    {
        AllpassFilter apf;        // Decay allpass
        std::vector<float> delay; // Main delay line
        int delayWritePos = 0;
        int delaySamples = 0;
        float dampState = 0.0f;   // One-pole damping filter
    };

    TankHalf tankL_, tankR_;
    float tankDecay_ = 0.5f;
    float dampCoeff_ = 0.5f;
    static constexpr float kDecayDiffCoeff = 0.7f;

    // Dattorro prime delay lengths (in samples at 29761 Hz reference rate)
    // Scaled to actual sample rate in prepare()
    void initDattorroDelays();
    void updateDattorroParams();

    int scaleDelay (int referenceSamples) const;
};
