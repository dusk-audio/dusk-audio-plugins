#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "ChordAnalyzer.h"
#include "ChordRecorder.h"

//==============================================================================
class ChordAnalyzerProcessor : public juce::AudioProcessor,
                                public juce::AudioProcessorValueTreeState::Listener,
                                private juce::Timer
{
public:
    ChordAnalyzerProcessor();
    ~ChordAnalyzerProcessor() override;

    //==========================================================================
    // AudioProcessor overrides
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    // MIDI effect characteristics
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }  // MIDI pass-through
    bool isMidiEffect() const override
    {
       #if CHORD_ANALYZER_MIDI_MODE
        return true;
       #else
        return false;
       #endif
    }
    double getTailLengthSeconds() const override { return 0.0; }

    //==========================================================================
    // Editor
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==========================================================================
    // Plugin info
    const juce::String getName() const override;
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    //==========================================================================
    // State
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==========================================================================
    // Parameter handling
    juce::AudioProcessorValueTreeState& getAPVTS() { return parameters; }
    void parameterChanged(const juce::String& parameterID, float newValue) override;

    //==========================================================================
    // Thread-safe chord access for UI
    ChordInfo getCurrentChord() const;
    bool hasChordChanged();
    std::vector<ChordSuggestion> getCurrentSuggestions() const;
    std::vector<int> getActiveNotes() const;

    //==========================================================================
    // Key access
    int getKeyRoot() const { return keyRoot.load(); }
    bool isMinorKey() const { return keyMinor.load(); }
    juce::String getKeyName() const;

    //==========================================================================
    // Recording controls (thread-safe)
    void startRecording();
    void stopRecording();
    bool isRecording() const;
    void clearRecording();
    juce::String exportRecordingToJSON() const;
    int getRecordedEventCount() const;

    //==========================================================================
    // Parameter IDs
    static constexpr const char* PARAM_KEY_ROOT = "keyRoot";
    static constexpr const char* PARAM_KEY_MODE = "keyMode";
    static constexpr const char* PARAM_SUGGESTION_LEVEL = "suggestionLevel";
    static constexpr const char* PARAM_SHOW_INVERSIONS = "showInversions";
    static constexpr const char* PARAM_RESPECT_SUSTAIN = "respectSustain";

    // Output (read-only) parameters — populated by the processor so headless
    // hosts (e.g. Zynthian, generic Reaper view) can display detection results
    // without instantiating the editor.
    static constexpr const char* PARAM_DETECTED_ROOT      = "detectedRoot";
    static constexpr const char* PARAM_DETECTED_QUALITY   = "detectedQuality";
    static constexpr const char* PARAM_DETECTED_BASS      = "detectedBass";
    static constexpr const char* PARAM_DETECTED_INVERSION = "detectedInversion";

private:
    juce::AudioProcessorValueTreeState parameters;

    ChordAnalyzer analyzer;
    ChordRecorder recorder;

    //==========================================================================
    // Active notes tracking
    std::vector<int> activeNotes;
    mutable juce::SpinLock notesLock;

    //==========================================================================
    // Current analysis (atomic for thread-safe UI access)
    std::atomic<bool> chordChangedFlag{false};
    ChordInfo currentChord;
    std::vector<ChordSuggestion> currentSuggestions;
    mutable juce::SpinLock chordLock;

    //==========================================================================
    // Key state (atomic for thread safety)
    std::atomic<int> keyRoot{0};
    std::atomic<bool> keyMinor{false};
    std::atomic<int> suggestionLevel{2};  // 0=Basic, 1=+Inter, 2=All
    std::atomic<bool> showInversions{true};
    std::atomic<bool> respectSustain{true};

    //==========================================================================
    // Sustain pedal (CC 64) state — audio-thread only, mutated in processMidiInput.
    bool sustainPedalDown = false;
    std::vector<int> sustainedReleasedNotes;  // notes released while pedal was down

    //==========================================================================
    // Timing
    double currentSampleRate = 44100.0;
    double currentTimeSec = 0.0;
    double lastAnalysisTime = 0.0;
    static constexpr double analysisIntervalSec = 0.05;  // 50ms debounce

    //==========================================================================
    // Recording
    mutable juce::SpinLock recorderLock;

    //==========================================================================
    void processMidiInput(const juce::MidiBuffer& midi);
    void updateAnalysis();
    void stageDetectedChord(const ChordInfo& chord);   // audio thread: store atomic snapshot
    void timerCallback() override;                     // message thread: publish to host params

    //==========================================================================
    // Cached pointers to output parameters (populated in ctor).
    juce::AudioParameterChoice* detectedRootParam = nullptr;
    juce::AudioParameterChoice* detectedQualityParam = nullptr;
    juce::AudioParameterChoice* detectedBassParam = nullptr;
    juce::AudioParameterChoice* detectedInversionParam = nullptr;

    // Atomic snapshot of detection results, written from the audio thread and
    // published to the host on the message thread (Reaper and others don't
    // refresh their parameter display from audio-thread setValueNotifyingHost).
    std::atomic<int>  pendingRootIndex{0};
    std::atomic<int>  pendingQualityIndex{0};
    std::atomic<int>  pendingBassIndex{0};
    std::atomic<int>  pendingInversionIndex{0};
    std::atomic<bool> detectionDirty{false};

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordAnalyzerProcessor)
};
