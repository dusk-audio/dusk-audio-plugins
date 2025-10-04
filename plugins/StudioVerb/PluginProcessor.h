/*
  ==============================================================================

    Studio Verb - Professional Reverb Plugin
    Copyright (c) 2024 Luna Co. Audio

    A high-quality reverb processor with four distinct algorithms:
    Room, Hall, Plate, and Early Reflections

    Developed by Luna Co. Audio
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
    static constexpr const char* WIDTH_ID = "width";
    static constexpr const char* PRESET_ID = "preset";

    // Advanced parameters
    static constexpr const char* LOW_RT60_ID = "lowRT60";
    static constexpr const char* MID_RT60_ID = "midRT60";
    static constexpr const char* HIGH_RT60_ID = "highRT60";
    static constexpr const char* INFINITE_ID = "infinite";
    static constexpr const char* OVERSAMPLING_ID = "oversampling";
    static constexpr const char* ROOM_SHAPE_ID = "roomShape";
    static constexpr const char* VINTAGE_ID = "vintage";
    static constexpr const char* PREDELAY_BEATS_ID = "predelayBeats";
    static constexpr const char* MOD_RATE_ID = "modRate";
    static constexpr const char* MOD_DEPTH_ID = "modDepth";
    static constexpr const char* COLOR_MODE_ID = "colorMode";
    static constexpr const char* BASS_MULT_ID = "bassMult";
    static constexpr const char* BASS_XOVER_ID = "bassXover";
    static constexpr const char* NOISE_AMOUNT_ID = "noiseAmount";
    static constexpr const char* QUALITY_ID = "quality";

    // Algorithm types
    enum Algorithm
    {
        Room = 0,
        Hall,
        Plate,
        EarlyReflections,
        Gated,          // Non-linear gated reverb
        Reverse,        // Non-linear reverse reverb
        ConcertHall,    // Large diffuse hall with longer decay
        BrightChamber,  // Reflective chamber with high-frequency emphasis
        DarkHall,       // Warm smooth hall with reduced highs
        Sanctuary,      // Ethereal non-realistic space
        TightRoom,      // Small room with quick reflections
        Shimmer,        // Upward pitch-shifted reverb tail
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
        float width = 0.5f;
        float lowRT60 = 2.0f;
        float midRT60 = 2.0f;
        float highRT60 = 1.5f;
        bool infinite = false;
        int oversampling = 0;
        int roomShape = 0;
        int predelayBeats = 0;
        float vintage = 0.0f;
        float modRate = 0.5f;
        float modDepth = 0.5f;
        int colorMode = 2;  // Default to "Now" (clean)
        float bassMult = 1.0f;
        float bassXover = 150.0f;
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
    std::atomic<float> currentWidth { 0.5f };

    // Advanced parameters
    std::atomic<float> currentLowRT60 { 2.0f };
    std::atomic<float> currentMidRT60 { 2.0f };
    std::atomic<float> currentHighRT60 { 1.5f };
    std::atomic<bool> currentInfinite { false };
    std::atomic<int> currentOversampling { 0 };  // 0=off, 1=2x, 2=4x
    std::atomic<int> currentRoomShape { 0 };
    std::atomic<float> currentVintage { 0.0f };
    std::atomic<int> currentPredelayBeats { 0 };  // 0=off, 1=1/16, 2=1/8, etc.
    std::atomic<float> currentModRate { 0.5f };   // Modulation rate (0.1-5.0 Hz)
    std::atomic<float> currentModDepth { 0.5f };  // Modulation depth (0-1)
    std::atomic<int> currentColorMode { 2 };      // 0=1970s, 1=1980s, 2=Now (default)
    std::atomic<float> currentBassMult { 1.0f };  // Bass decay multiplier (0.5-2.0)
    std::atomic<float> currentBassXover { 150.0f }; // Bass crossover frequency (50-500 Hz)
    std::atomic<float> currentNoiseAmount { 0.5f }; // Vintage noise amount (0-1)
    std::atomic<int> currentQuality { 1 };  // Quality: 0=Eco(16ch), 1=High(32ch)

    // Store manual predelay separately to preserve it during tempo sync
    std::atomic<float> manualPredelay { 0.0f };

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