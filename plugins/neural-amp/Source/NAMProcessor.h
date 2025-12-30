#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>
#include <atomic>

// Forward declare NAM types
namespace nam {
    class DSP;
    struct dspData;
}

class NAMProcessor
{
public:
    NAMProcessor();
    ~NAMProcessor();

    bool loadModel(const juce::File& modelFile);
    void prepare(double sampleRate, int samplesPerBlock);
    void process(juce::AudioBuffer<float>& buffer);
    void reset();

    // Model info
    juce::String getModelName() const;
    juce::String getModelInfo() const;
    bool isModelLoaded() const { return modelLoaded.load(); }

private:
    std::unique_ptr<nam::DSP> namModel;
    std::unique_ptr<nam::dspData> modelData;

    double currentSampleRate = 48000.0;
    double modelSampleRate = 48000.0;
    int maxBlockSize = 512;

    std::atomic<bool> modelLoaded{false};

    juce::String modelName{"No Model"};
    juce::String modelGear;
    juce::String modelTone;

    // Resampling for sample rate mismatch
    std::unique_ptr<juce::ResamplingAudioSource> resampler;
    std::unique_ptr<juce::AudioBuffer<float>> resampleBuffer;
    bool needsResampling = false;
    double resampleRatio = 1.0;

    // Output normalization based on model loudness
    float outputNormalization = 1.0f;

    // Processing buffer (NAM is mono)
    std::vector<float> inputBuffer;
    std::vector<float> outputBuffer;

    void extractModelMetadata();
};
