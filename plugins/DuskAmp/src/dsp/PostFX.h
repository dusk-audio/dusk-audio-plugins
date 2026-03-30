#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cmath>
#include <algorithm>

class PostFX
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void setDelayEnabled (bool on);
    void setDelayTime (float ms);
    void setDelayFeedback (float fb01);
    void setDelayMix (float mix01);
    void setReverbEnabled (bool on);
    void setReverbMix (float mix01);
    void setReverbDecay (float decay01);

    void process (float* left, float* right, int numSamples);

private:
    double sampleRate_ = 44100.0;

    // Delay
    bool delayEnabled_ = false;
    std::vector<float> delayBufL_, delayBufR_;
    int delayWritePos_ = 0;
    int delaySamples_ = 0;
    float delayFeedback_ = 0.3f;
    float delayMix_ = 0.2f;

    // Feedback filter (hi-cut in delay feedback loop)
    float delayFbFilterStateL_ = 0.0f;
    float delayFbFilterStateR_ = 0.0f;
    float delayFbFilterCoeff_ = 0.7f; // ~4kHz rolloff

    // Reverb
    bool reverbEnabled_ = false;
    juce::dsp::Reverb reverb_;
    juce::dsp::Reverb::Parameters reverbParams_;
    float reverbMix_ = 0.15f;
    juce::AudioBuffer<float> reverbBuffer_; // Pre-allocated for audio-thread use

    static constexpr int kMaxDelaySamples = 96000 * 2; // 2 seconds at 96kHz

    void updateDelayFbFilterCoeff();
};
