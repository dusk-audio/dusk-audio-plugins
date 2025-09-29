#pragma once

#include <JuceHeader.h>
#include <memory>
#include <array>
#include "DSP/ReverbProcessor.h"

// Reverb types matching Dragonfly's actual plugins
enum class ReverbType
{
    Room = 0,           // Dragonfly Room Reverb
    Hall,               // Dragonfly Hall Reverb
    Plate,              // Dragonfly Plate Reverb
    EarlyReflections,   // Dragonfly Early Reflections
    NumTypes
};

class StudioReverbAudioProcessor : public juce::AudioProcessor,
                                            public juce::AudioProcessorValueTreeState::Listener
{
public:
    StudioReverbAudioProcessor();
    ~StudioReverbAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    #endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int index) override {}
    const juce::String getProgramName(int index) override { return {}; }
    void changeProgramName(int index, const juce::String& newName) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Parameter IDs
    static constexpr const char* PARAM_REVERB_TYPE = "reverbType";
    static constexpr const char* PARAM_WET_DRY = "wetDry";
    static constexpr const char* PARAM_DECAY = "decay";
    static constexpr const char* PARAM_PREDELAY = "preDelay";
    static constexpr const char* PARAM_DAMPING = "damping";
    static constexpr const char* PARAM_ROOM_SIZE = "roomSize";
    static constexpr const char* PARAM_DIFFUSION = "diffusion";
    static constexpr const char* PARAM_LOW_CUT = "lowCut";
    static constexpr const char* PARAM_HIGH_CUT = "highCut";
    static constexpr const char* PARAM_EARLY_MIX = "earlyMix";
    static constexpr const char* PARAM_LATE_MIX = "lateMix";
    static constexpr const char* PARAM_MODULATION = "modulation";
    static constexpr const char* PARAM_OUTPUT_GAIN = "outputGain";

    // Get current reverb type
    ReverbType getCurrentReverbType() const;
    void setReverbType(ReverbType type);

    // Parameter tree
    juce::AudioProcessorValueTreeState apvts;

    // Parameter listener callback
    void parameterChanged(const juce::String& parameterID, float newValue) override;

    // Make processors accessible to editor
    std::array<std::unique_ptr<ReverbProcessor>, 4> reverbProcessors; // 4 reverb types

private:
    ReverbType currentReverbType{ReverbType::Room};

    // Processing buffers
    juce::AudioBuffer<float> wetBuffer;

    // Parameters
    std::atomic<float>* wetDryParam = nullptr;
    std::atomic<float>* decayParam = nullptr;
    std::atomic<float>* predelayParam = nullptr;
    std::atomic<float>* dampingParam = nullptr;
    std::atomic<float>* roomSizeParam = nullptr;
    std::atomic<float>* diffusionParam = nullptr;
    std::atomic<float>* lowCutParam = nullptr;
    std::atomic<float>* highCutParam = nullptr;
    std::atomic<float>* earlyMixParam = nullptr;
    std::atomic<float>* lateMixParam = nullptr;
    std::atomic<float>* modulationParam = nullptr;
    std::atomic<float>* outputGainParam = nullptr;

    // Smoothing for parameter changes
    juce::LinearSmoothedValue<float> wetDrySmoothed;
    juce::LinearSmoothedValue<float> outputGainSmoothed;

    // Sample rate
    double currentSampleRate{44100.0};
    int currentBlockSize{512};

    // Create parameter layout
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Update reverb parameters
    void updateReverbParameters();

    // Switch reverb type with crossfade
    void switchReverbType(ReverbType newType);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StudioReverbAudioProcessor)
};