#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>

#if DUSKAMP_NAM_SUPPORT
#include "NAM/dsp.h"
#include "NAM/get_dsp.h"
#endif

class NAMProcessor
{
public:
    NAMProcessor() = default;
    ~NAMProcessor();

    void prepare(double sampleRate, int maxBlockSize);
    void process(float* buffer, int numSamples);
    void reset();

    // Thread-safe model loading (call from message thread)
    bool loadModel(const juce::File& file);
    void clearModel();

    bool hasModel() const;
    juce::String getModelName() const;
    juce::File getModelFile() const;

    void setInputLevel(float dB);
    void setOutputLevel(float dB);

    juce::String getLastError() const { return lastError_; }

private:
#if DUSKAMP_NAM_SUPPORT
    // Model swap: message thread writes pendingModel_, sets pendingReady_ flag.
    // Audio thread checks the flag and swaps.
    std::unique_ptr<nam::DSP> pendingModel_;
    std::unique_ptr<nam::DSP> activeModel_;
    std::unique_ptr<nam::DSP> retiredModel_;  // deferred delete
    std::atomic<bool> pendingReady_ { false };
    std::atomic<bool> pendingClear_ { false };
#endif

    float inputGain_ = 1.0f;
    float outputGain_ = 1.0f;
    double sampleRate_ = 44100.0;
    int maxBlockSize_ = 512;

    juce::String modelName_;
    juce::File modelFile_;
    juce::String lastError_;
    std::atomic<bool> modelLoaded_ { false };

    std::vector<double> inputBuffer_;
    std::vector<double> outputBuffer_;
};
