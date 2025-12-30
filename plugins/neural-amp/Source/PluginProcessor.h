#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <atomic>

class NAMProcessor;
class CabinetProcessor;

class NeuralAmpAudioProcessor : public juce::AudioProcessor
{
public:
    NeuralAmpAudioProcessor();
    ~NeuralAmpAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    // Model/IR loading
    bool loadNAMModel(const juce::File& modelFile);
    bool loadCabinetIR(const juce::File& irFile);

    juce::String getModelName() const;
    juce::String getModelInfo() const;
    juce::String getIRName() const;
    bool isModelLoaded() const;
    bool isIRLoaded() const;

    // Metering
    float getInputLevel() const { return inputLevel.load(); }
    float getOutputLevel() const { return outputLevel.load(); }

private:
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::unique_ptr<NAMProcessor> namProcessor;
    std::unique_ptr<CabinetProcessor> cabinetProcessor;

    // DSP components
    juce::dsp::Gain<float> inputGain;
    juce::dsp::Gain<float> outputGain;

    // Noise gate
    juce::dsp::NoiseGate<float> noiseGate;

    // Tone stack (3-band EQ) - use ProcessorDuplicator for stereo processing
    using IIRFilter = juce::dsp::IIR::Filter<float>;
    using IIRCoefs = juce::dsp::IIR::Coefficients<float>;
    juce::dsp::ProcessorDuplicator<IIRFilter, IIRCoefs> bassFilter;
    juce::dsp::ProcessorDuplicator<IIRFilter, IIRCoefs> midFilter;
    juce::dsp::ProcessorDuplicator<IIRFilter, IIRCoefs> trebleFilter;

    // Output filters - use ProcessorDuplicator for stereo processing
    juce::dsp::ProcessorDuplicator<IIRFilter, IIRCoefs> lowCutFilter;
    juce::dsp::ProcessorDuplicator<IIRFilter, IIRCoefs> highCutFilter;

    // Parameter pointers
    std::atomic<float>* inputGainParam = nullptr;
    std::atomic<float>* outputGainParam = nullptr;
    std::atomic<float>* gateThresholdParam = nullptr;
    std::atomic<float>* gateEnabledParam = nullptr;
    std::atomic<float>* bassParam = nullptr;
    std::atomic<float>* midParam = nullptr;
    std::atomic<float>* trebleParam = nullptr;
    std::atomic<float>* lowCutParam = nullptr;
    std::atomic<float>* highCutParam = nullptr;
    std::atomic<float>* cabEnabledParam = nullptr;
    std::atomic<float>* cabMixParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;

    // Current sample rate
    double currentSampleRate = 44100.0;

    // Metering
    std::atomic<float> inputLevel{0.0f};
    std::atomic<float> outputLevel{0.0f};

    // Model/IR file paths for state saving
    juce::String currentModelPath;
    juce::String currentIRPath;

    void updateFilters();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NeuralAmpAudioProcessor)
};
