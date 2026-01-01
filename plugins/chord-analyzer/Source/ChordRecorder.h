#pragma once

#include "ChordAnalyzer.h"
#include <juce_core/juce_core.h>
#include <vector>

//==============================================================================
// Recorded chord event with timing
struct RecordedChordEvent
{
    ChordInfo chord;
    double startTimeSec = 0.0;
    double durationSec = 0.0;
    double startBeat = 0.0;
    double durationBeats = 0.0;
};

//==============================================================================
// Recording session metadata
struct RecordingSession
{
    juce::String name = "Untitled";
    juce::Time startTime;
    double tempoBPM = 120.0;
    int keyRoot = 0;
    bool isMinor = false;
    std::vector<RecordedChordEvent> events;
};

//==============================================================================
class ChordRecorder
{
public:
    ChordRecorder();

    //==========================================================================
    // Recording control
    void startRecording();
    void stopRecording();
    bool isRecording() const { return recording; }

    //==========================================================================
    // Chord recording
    void recordChord(const ChordInfo& chord, double currentTimeSec);
    void endCurrentChord(double endTimeSec);

    //==========================================================================
    // Session management
    void clearSession();
    const RecordingSession& getSession() const { return currentSession; }
    void setSessionName(const juce::String& name);
    void setTempo(double bpm);
    void setKey(int root, bool minor);

    //==========================================================================
    // Get recorded events
    const std::vector<RecordedChordEvent>& getEvents() const { return currentSession.events; }
    int getEventCount() const { return static_cast<int>(currentSession.events.size()); }
    double getRecordingDuration() const;

    //==========================================================================
    // Export
    juce::String exportToJSON() const;
    bool exportToFile(const juce::File& file) const;

private:
    bool recording = false;
    RecordingSession currentSession;
    double sessionStartTime = 0.0;

    // Current chord tracking
    ChordInfo lastChord;
    double lastChordStartTime = 0.0;
    bool hasActiveChord = false;

    //==========================================================================
    // JSON helpers
    juce::String chordInfoToJSON(const ChordInfo& chord) const;
    juce::String eventToJSON(const RecordedChordEvent& event) const;
    juce::String escapeJSON(const juce::String& str) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordRecorder)
};
