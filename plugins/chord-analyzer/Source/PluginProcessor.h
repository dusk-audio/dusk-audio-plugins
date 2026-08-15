#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../../shared/CrashLog.h"
#include "ChordAnalyzer.h"
#include "ChordRecorder.h"
#include <array>
#include <atomic>

//==============================================================================
class ChordAnalyzerProcessor : public juce::AudioProcessor,
                                public juce::AudioProcessorValueTreeState::Listener,
                                private juce::Timer
{
    // FIRST member, deliberately ahead of the public section. Members are
    // constructed in declaration order, so the crash handler is armed before
    // anything else in this class is built and released only after they are
    // gone (GH #172). Named per variant to match the supporters overlay, since
    // both plugins build from this one processor.
#if CHORD_ANALYZER_MIDI_MODE
    DuskCrashLog::ScopedRegistration crashLog_ { "Chord Analyzer MIDI", JucePlugin_VersionString };
#else
    DuskCrashLog::ScopedRegistration crashLog_ { "Chord Analyzer", JucePlugin_VersionString };
#endif

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

    // Recent chord history — rolling window of distinct detected chords
    // (newest last). Always-on, independent of the recording session.
    // Capped at maxHistorySize entries so memory is bounded.
    static constexpr int maxHistorySize = 32;
    std::vector<ChordInfo> getChordHistory() const;
    void clearChordHistory();

    //==========================================================================
    // Key access
    int getKeyRoot() const { return keyRoot.load(); }
    bool isMinorKey() const { return keyMinor.load(); }
    juce::String getKeyName() const;

    //==========================================================================
    // Recording controls (thread-safe). The recording session feeds the
    // MIDI drag-and-drop export — there is no longer a JSON file output
    // (dropped 2026-05-04, issue #71).
    void startRecording();
    void stopRecording();
    bool isRecording() const;
    void clearRecording();
    int getRecordedEventCount() const;

    // Atomic snapshot of the current recording — events plus the BPM that
    // was active when recording started (which is what each event's
    // startBeat / durationBeats were computed against). Read this rather
    // than the live host tempo so MIDI export stays consistent if the
    // user changes host tempo between stopping recording and dragging.
    RecordingSession getRecordingSession() const;

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
    std::array<std::atomic<uint64_t>, 2> activeNoteWords{
        std::atomic<uint64_t>{0}, std::atomic<uint64_t>{0} };

    struct PendingAnalysis
    {
        std::array<uint64_t, 2> noteWords{};
        double timeSec = 0.0;
    };
    static constexpr size_t analysisQueueCapacity = 64;
    std::array<PendingAnalysis, analysisQueueCapacity> analysisQueue{};
    std::atomic<size_t> analysisQueueWrite{0};
    std::atomic<size_t> analysisQueueRead{0};

    //==========================================================================
    // Current analysis (atomic for thread-safe UI access)
    std::atomic<bool> chordChangedFlag{false};
    ChordInfo currentChord;
    std::vector<ChordSuggestion> currentSuggestions;

    // Ring buffer (preallocated, no reallocation while publishing analysis).
    // historyHead is the next write slot; historyCount is the number of
    // valid entries (0..maxHistorySize). Logical order is oldest..newest
    // = chordHistory[(historyHead - historyCount + N) % N .. historyHead).
    std::array<ChordInfo, maxHistorySize> chordHistory{};
    int historyHead = 0;
    int historyCount = 0;
    mutable juce::SpinLock chordLock;

    //==========================================================================
    // Key state (atomic for thread safety)
    std::atomic<int> keyRoot{0};
    std::atomic<bool> keyMinor{false};
    std::atomic<int> suggestionLevel{2};  // 0=Basic, 1=+Inter, 2=All
    std::atomic<bool> showInversions{true};
    std::atomic<bool> respectSustain{true};

    // Last host BPM observed in processBlock — read by startRecording() so
    // recorded events get beat values aligned with the DAW's tempo. Falls
    // back to 120 if the host doesn't expose a BPM (offline render, etc).
    std::atomic<double> hostBPM{120.0};

    //==========================================================================
    // Sustain pedal (CC 64) state — audio-thread only, mutated in processMidiInput.
    bool sustainPedalDown = false;
    std::array<uint64_t, 2> sustainedReleasedWords{};

    //==========================================================================
    // Timing
    double currentSampleRate = 44100.0;
    // Accumulating wall-clock since prepareToPlay. Single-writer (the
    // audio thread); read from the message thread by stopRecording() so
    // the still-active chord can be closed at the actual stop moment.
    // std::atomic<double>::fetch_add isn't available until C++20, but
    // load+store is fine here because there's only one writer thread.
    std::atomic<double> currentTimeSec{0.0};
    std::atomic<bool> analysisDirty{true};

    //==========================================================================
    // Recording
    mutable juce::SpinLock recorderLock;

    //==========================================================================
    void processMidiInput(const juce::MidiBuffer& midi);
    // Clears every piece of note/pedal timeline state. Shared by prepareToPlay
    // (so a re-prepare starts clean) and releaseResources (so a stopped
    // instance cannot retain held notes or a stuck pedal).
    void resetNoteState() noexcept;
    bool activateNote(int note) noexcept;
    bool deactivateNote(int note) noexcept;
    std::array<uint64_t, 2> snapshotActiveNoteWords() const noexcept;
    static std::vector<int> notesFromWords(const std::array<uint64_t, 2>& words);
    std::vector<int> snapshotActiveNotes() const;
    bool enqueueAnalysisSnapshot(double timeSec) noexcept;
    bool dequeueAnalysisSnapshot(PendingAnalysis& snapshot) noexcept;
    void updateAnalysis();
    void updateAnalysis(const PendingAnalysis& snapshot);
    void stageDetectedChord(const ChordInfo& chord);   // message thread: stage host values
    void timerCallback() override;                     // message thread: analyze and publish

    //==========================================================================
    // Cached pointers to output parameters (populated in ctor).
    juce::AudioParameterChoice* detectedRootParam = nullptr;
    juce::AudioParameterChoice* detectedQualityParam = nullptr;
    juce::AudioParameterChoice* detectedBassParam = nullptr;
    juce::AudioParameterChoice* detectedInversionParam = nullptr;

    // Atomic snapshot of detection results, published to the host by the timer
    // (Reaper and others don't refresh their generic parameter display from
    // direct background-thread writes).
    std::atomic<int>  pendingRootIndex{0};
    std::atomic<int>  pendingQualityIndex{0};
    std::atomic<int>  pendingBassIndex{0};
    std::atomic<int>  pendingInversionIndex{0};
    std::atomic<bool> detectionDirty{false};

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordAnalyzerProcessor)
};
