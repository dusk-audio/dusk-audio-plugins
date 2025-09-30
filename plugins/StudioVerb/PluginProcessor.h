/*
  ==============================================================================

    Studio Verb - Professional Reverb Plugin
    Copyright (c) 2024 Luna CO. Audio

    A high-quality reverb processor with four distinct algorithms:
    Room, Hall, Plate, and Early Reflections

    Developed by Luna CO. Audio
    https://lunaco.audio

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <random>
#include <memory>

//==============================================================================
// Forward declarations
class ReverbEngine;
class ReverbEngineEnhanced;

//==============================================================================
/**
    Main audio processor class for Studio Verb
*/
class StudioVerbAudioProcessor : public juce::AudioProcessor,
                                  public juce::AudioProcessorValueTreeState::Listener
{
public:
    //==============================================================================
    StudioVerbAudioProcessor();
    ~StudioVerbAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Parameter IDs
    static constexpr const char* ALGORITHM_ID = "algorithm";
    static constexpr const char* SIZE_ID = "size";
    static constexpr const char* DAMP_ID = "damp";
    static constexpr const char* PREDELAY_ID = "predelay";
    static constexpr const char* MIX_ID = "mix";
    static constexpr const char* WIDTH_ID = "width";  // Task 10: Added width control
    static constexpr const char* PRESET_ID = "preset";

    // Algorithm types
    enum Algorithm
    {
        Room = 0,
        Hall,
        Plate,
        EarlyReflections,
        NumAlgorithms
    };

    // Preset structure
    struct Preset
    {
        juce::String name;
        Algorithm algorithm;
        float size;
        float damp;
        float predelay;
        float mix;
    };

    // Get parameters
    juce::AudioProcessorValueTreeState& getValueTreeState() { return parameters; }

    // Load preset
    void loadPreset(int presetIndex);

    // Get preset names for current algorithm
    juce::StringArray getPresetNamesForAlgorithm(Algorithm algo) const;

    // Get factory presets
    const std::vector<Preset>& getFactoryPresets() const { return factoryPresets; }

    // Parameter listener
    void parameterChanged(const juce::String& parameterID, float newValue) override;

private:
    //==============================================================================
    // Parameters
    juce::AudioProcessorValueTreeState parameters;

    // Current settings
    std::atomic<Algorithm> currentAlgorithm { Room };
    std::atomic<float> currentSize { 0.5f };
    std::atomic<float> currentDamp { 0.5f };
    std::atomic<float> currentPredelay { 0.0f };
    std::atomic<float> currentMix { 0.5f };
    std::atomic<float> currentWidth { 0.5f };  // Task 10: Width parameter

    // Reverb engine (Task 3: Integrated Enhanced FDN Engine)
    std::unique_ptr<ReverbEngineEnhanced> reverbEngine;

    // Preset management (Task 4: Added user preset support)
    std::vector<Preset> factoryPresets;
    std::vector<Preset> userPresets;
    int currentPresetIndex = 0;

    // User preset methods
    void saveUserPreset(const juce::String& name);
    void deleteUserPreset(int index);

    // Helper functions
    void initializeParameters();
    void initializePresets();
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Thread safety
    juce::CriticalSection processLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StudioVerbAudioProcessor)
};