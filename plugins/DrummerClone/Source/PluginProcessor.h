#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include "DrumMapping.h"
#include "TransientDetector.h"
#include "MidiGrooveExtractor.h"
#include "GrooveTemplateGenerator.h"
#include "GrooveFollower.h"
#include "DrummerEngine.h"

// Step sequencer data structure (matches StepSequencer.h)
struct StepSequencerPattern
{
    static constexpr int NumLanes = 8;
    static constexpr int NumSteps = 16;

    struct Step
    {
        bool active = false;
        float velocity = 0.8f;
    };

    std::array<std::array<Step, NumSteps>, NumLanes> pattern;
    bool enabled = false;  // Whether to use step sequencer override
};

//==============================================================================
/**
 * DrummerClone - A MIDI Effect VST3 that clones Logic Pro's Drummer functionality
 * Features Follow Mode to sync with input audio/MIDI
 */
class DrummerCloneAudioProcessor : public juce::AudioProcessor,
                                   public juce::AudioProcessorValueTreeState::Listener,
                                   public juce::Timer
{
public:
    //==============================================================================
    DrummerCloneAudioProcessor();
    ~DrummerCloneAudioProcessor() override;

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
    // Parameter handling
    void parameterChanged(const juce::String& parameterID, float newValue) override;

    // Timer callback for UI sync
    void timerCallback() override;

    // Get current parameters
    juce::AudioProcessorValueTreeState& getValueTreeState() { return parameters; }

    // Follow Mode data access
    const GrooveTemplate& getCurrentGroove() const { return currentGroove; }
    float getGrooveLockPercentage() const { return grooveLockPercentage; }
    bool isFollowModeActive() const { return followModeActive; }

    // MIDI CC control
    bool isSectionControlledByMidi() const { return midiSectionActive; }
    double getTimeSinceLastMidiSection() const { return timeSinceLastMidiSection; }

    // Step sequencer pattern (thread-safe accessors)
    void setStepSequencerPattern(const StepSequencerPattern& pattern);
    void setStepSequencerEnabled(bool enabled);
    bool isStepSequencerEnabled() const;
    StepSequencerPattern getStepSequencerPattern() const;

private:
    //==============================================================================
    // Core components
    juce::AudioProcessorValueTreeState parameters;

    // Follow Mode components
    TransientDetector transientDetector;
    MidiGrooveExtractor midiGrooveExtractor;
    GrooveTemplateGenerator grooveTemplateGenerator;
    GrooveFollower grooveFollower;

    // Buffers
    juce::AudioBuffer<float> audioInputBuffer;

    // MIDI Generation
    DrummerEngine drummerEngine;
    juce::MidiBuffer incomingMidiBuffer;
    juce::MidiBuffer generatedMidiBuffer;
    std::vector<juce::MidiMessage> midiRingBuffer;

    // State
    double currentSampleRate = 44100.0;
    int currentSamplesPerBlock = 512;
    double currentBPM = 120.0;
    double ppqPosition = 0.0;
    bool isPlaying = false;

    // Follow Mode state
    bool followModeActive = false;
    bool followSourceIsAudio = false; // false = MIDI, true = Audio
    float followSensitivity = 0.5f;
    GrooveTemplate currentGroove;
    float grooveLockPercentage = 0.0f;

    // Generation state
    std::atomic<bool> needsRegeneration{true};
    int lastGeneratedBar = -1;

    // MIDI CC control state
    bool lastMidiSectionChange = false;
    bool midiSectionActive = false;       // true when section is being controlled via MIDI
    double timeSinceLastMidiSection = 0.0; // seconds since last MIDI section change

    // Step sequencer pattern (protected by spinlock for thread safety)
    mutable juce::SpinLock stepSeqPatternLock;
    StepSequencerPattern stepSeqPattern;

    // Parameter IDs
    static constexpr const char* PARAM_COMPLEXITY = "complexity";
    static constexpr const char* PARAM_LOUDNESS = "loudness";
    static constexpr const char* PARAM_SWING = "swing";
    static constexpr const char* PARAM_FOLLOW_ENABLED = "followEnabled";
    static constexpr const char* PARAM_FOLLOW_SOURCE = "followSource";
    static constexpr const char* PARAM_FOLLOW_SENSITIVITY = "followSensitivity";
    static constexpr const char* PARAM_STYLE = "style";
    static constexpr const char* PARAM_DRUMMER = "drummer";

    // Helper methods
    void updatePlayheadInfo(juce::AudioPlayHead* playHead);
    void processFollowMode(const juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi);
    void processMidiInput(const juce::MidiBuffer& midiMessages);
    void pruneOldMidiEvents();
    void generateDrumPattern();
    bool isBarBoundary(double ppq, double bpm);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrummerCloneAudioProcessor)
};