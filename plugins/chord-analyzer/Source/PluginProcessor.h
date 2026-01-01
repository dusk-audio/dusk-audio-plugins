#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "ChordAnalyzer.h"
#include "ChordRecorder.h"

//==============================================================================
class ChordAnalyzerProcessor : public juce::AudioProcessor,
                                public juce::AudioProcessorValueTreeState::Listener
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
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return true; }
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

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordAnalyzerProcessor)
};
