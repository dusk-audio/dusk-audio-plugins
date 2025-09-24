#pragma once

#include <JuceHeader.h>
#include <memory>
#include <array>

class ImprovedTapeEmulation;

class TapeMachineAudioProcessor : public juce::AudioProcessor
{
public:
    TapeMachineAudioProcessor();
    ~TapeMachineAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    // Level metering
    float getInputLevelL() const { return inputLevelL.load(); }
    float getInputLevelR() const { return inputLevelR.load(); }
    float getOutputLevelL() const { return outputLevelL.load(); }
    float getOutputLevelR() const { return outputLevelR.load(); }

    // Transport state for reel animation
    bool isProcessing() const { return isProcessingAudio.load(); }

    enum TapeMachine
    {
        StuderA800 = 0,
        AmpexATR102,
        Blend
    };

    enum TapeSpeed
    {
        Speed_7_5_IPS = 0,
        Speed_15_IPS,
        Speed_30_IPS
    };

    enum TapeType
    {
        Ampex456 = 0,
        GP9,
        BASF911
    };

private:
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::unique_ptr<ImprovedTapeEmulation> tapeEmulationLeft;
    std::unique_ptr<ImprovedTapeEmulation> tapeEmulationRight;

    // Add bias parameter for improved tape emulation
    std::atomic<float>* biasParam = nullptr;

    juce::dsp::Oversampling<float> oversampling;

    juce::dsp::ProcessorChain<
        juce::dsp::Gain<float>,
        juce::dsp::StateVariableTPTFilter<float>,
        juce::dsp::StateVariableTPTFilter<float>,
        juce::dsp::Gain<float>
    > processorChainLeft, processorChainRight;

    juce::dsp::DelayLine<float> wowFlutterDelayLeft{48000};
    juce::dsp::DelayLine<float> wowFlutterDelayRight{48000};

    juce::Random noiseGenerator;

    float wowPhase = 0.0f;
    float flutterPhase = 0.0f;
    float currentSampleRate = 44100.0f;

    std::atomic<float>* tapeMachineParam = nullptr;
    std::atomic<float>* tapeSpeedParam = nullptr;
    std::atomic<float>* tapeTypeParam = nullptr;
    std::atomic<float>* inputGainParam = nullptr;
    std::atomic<float>* saturationParam = nullptr;
    std::atomic<float>* highpassFreqParam = nullptr;
    std::atomic<float>* lowpassFreqParam = nullptr;
    std::atomic<float>* noiseAmountParam = nullptr;
    std::atomic<float>* noiseEnabledParam = nullptr;
    std::atomic<float>* wowFlutterParam = nullptr;
    std::atomic<float>* outputGainParam = nullptr;

    void updateFilters();
    float processTapeSaturation(float input, float saturation, TapeMachine machine, TapeType tape);
    std::pair<float, float> processWowFlutter(float inputL, float inputR, float amount);

    // Level metering
    std::atomic<float> inputLevelL { 0.0f };
    std::atomic<float> inputLevelR { 0.0f };
    std::atomic<float> outputLevelL { 0.0f };
    std::atomic<float> outputLevelR { 0.0f };
    std::atomic<bool> isProcessingAudio { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapeMachineAudioProcessor)
};