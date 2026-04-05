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

    // Model loading — call from message thread only.
    // Stages the model; it's swapped in on the next process() call.
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
    // Staged model: written by message thread, consumed by audio thread.
    // Protected by the atomic flag stagingReady_.
    std::unique_ptr<nam::DSP> stagedModel_;
    std::atomic<bool> stagingReady_ { false };

    // Active model: owned exclusively by the audio thread.
    std::unique_ptr<nam::DSP> activeModel_;
#endif

    float inputGain_ = 1.0f;
    float outputGain_ = 1.0f;
    double sampleRate_ = 48000.0;
    int maxBlockSize_ = 2048;

    juce::String modelName_;
    juce::File modelFile_;
    juce::String lastError_;
    std::atomic<bool> modelLoaded_ { false };

    std::vector<double> inputBuffer_;
    std::vector<double> outputBuffer_;
};
