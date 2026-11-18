#pragma once

#include "ChordAnalyzer.h"
#include "../core/ChordRecorder.hpp"
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
    using Event = RecordedChordEvent;
    using Name = juce::String;
    using Time = juce::Time;
    juce::String name = "Untitled";
    juce::Time startTime;
    double tempoBPM = 120.0;
    int keyRoot = 0;
    bool isMinor = false;
    std::vector<RecordedChordEvent> events;
};

//==============================================================================
// Thin legacy type/clock adapter. Recording logic is shared with DAF consumers.
class ChordRecorder : public duskaudio::chord::BasicChordRecorder<ChordInfo, RecordingSession>
{
    using Base = duskaudio::chord::BasicChordRecorder<ChordInfo, RecordingSession>;
public:
    ChordRecorder();
    void startRecording();

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordRecorder)
};
