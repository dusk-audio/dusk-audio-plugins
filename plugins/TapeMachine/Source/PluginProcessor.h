#pragma once

#include <JuceHeader.h>
#include <memory>
#include <array>

class ImprovedTapeEmulation;
class WowFlutterProcessor;  // Forward declaration for shared wow/flutter

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

    // Level metering - use relaxed ordering for UI reads (no synchronization needed)
    // Audio thread writes, UI thread reads - eventual consistency is fine for metering
    float getInputLevelL() const { return inputLevelL.load(std::memory_order_relaxed); }
    float getInputLevelR() const { return inputLevelR.load(std::memory_order_relaxed); }
    float getOutputLevelL() const { return outputLevelL.load(std::memory_order_relaxed); }
    float getOutputLevelR() const { return outputLevelR.load(std::memory_order_relaxed); }

    // Transport state for reel animation
    bool isProcessing() const { return isProcessingAudio.load(std::memory_order_relaxed); }

    // Mono/stereo state for VU meter display
    bool isMonoTrack() const { return isMonoInput.load(std::memory_order_relaxed); }

    enum TapeMachine
    {
        StuderA800 = 0,
        AmpexATR102
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
        BASF911,
        Type250
    };

private:
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::unique_ptr<ImprovedTapeEmulation> tapeEmulationLeft;
    std::unique_ptr<ImprovedTapeEmulation> tapeEmulationRight;

    // Shared wow/flutter processor for stereo coherence (real tape motor affects both channels identically)
    std::unique_ptr<WowFlutterProcessor> sharedWowFlutter;

    // Add bias parameter for improved tape emulation
    std::atomic<float>* biasParam = nullptr;
    std::atomic<float>* calibrationParam = nullptr;

    // Oversampling with 2x/4x selection using FIR equiripple filters
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler2x;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler4x;
    std::atomic<float>* oversamplingParam = nullptr;
    int currentOversamplingFactor = 4;  // Default to 4x

    // For recreating oversamplers when settings change
    double lastPreparedSampleRate = 0.0;
    int lastPreparedBlockSize = 0;
    int lastOversamplingChoice = -1;

    // Crossfade state for smooth oversampling transitions
    bool oversamplingTransitionActive = false;
    int oversamplingTransitionSamples = 0;
    static constexpr int OVERSAMPLING_CROSSFADE_SAMPLES = 512;  // ~10ms at 48kHz

    juce::dsp::ProcessorChain<
        juce::dsp::Gain<float>,
        juce::dsp::StateVariableTPTFilter<float>,
        juce::dsp::StateVariableTPTFilter<float>,
        juce::dsp::Gain<float>
    > processorChainLeft, processorChainRight;

    float currentSampleRate = 44100.0f;
    float currentOversampledRate = 176400.0f;  // Default value; computed dynamically in prepareToPlay()

    std::atomic<float>* tapeMachineParam = nullptr;
    std::atomic<float>* tapeSpeedParam = nullptr;
    std::atomic<float>* tapeTypeParam = nullptr;
    std::atomic<float>* inputGainParam = nullptr;
    std::atomic<float>* highpassFreqParam = nullptr;
    std::atomic<float>* lowpassFreqParam = nullptr;
    std::atomic<float>* noiseAmountParam = nullptr;
    std::atomic<float>* noiseEnabledParam = nullptr;
    std::atomic<float>* wowAmountParam = nullptr;
    std::atomic<float>* flutterAmountParam = nullptr;
    std::atomic<float>* outputGainParam = nullptr;
    std::atomic<float>* autoCompParam = nullptr;

    void updateFilters();

    // Level metering (RMS-based for VU accuracy)
    std::atomic<float> inputLevelL { 0.0f };
    std::atomic<float> inputLevelR { 0.0f };
    std::atomic<float> outputLevelL { 0.0f };
    std::atomic<float> outputLevelR { 0.0f };
    std::atomic<bool> isProcessingAudio { false };
    std::atomic<bool> isMonoInput { false };  // True when on a mono track

    // RMS integration for VU-accurate metering (300ms time constant)
    float rmsInputL = 0.0f;
    float rmsInputR = 0.0f;
    float rmsOutputL = 0.0f;
    float rmsOutputR = 0.0f;

    // Filter frequency tracking (instance variables instead of statics)
    float lastHpFreq = -1.0f;
    float lastLpFreq = -1.0f;

    // Smoothed parameters to prevent zipper noise
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedSaturation;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedNoiseAmount;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedWow;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedFlutter;

    // Filter bypass states
    bool bypassHighpass = true;
    bool bypassLowpass = true;

    // Preset management
    int currentPresetIndex = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TapeMachineAudioProcessor)
};