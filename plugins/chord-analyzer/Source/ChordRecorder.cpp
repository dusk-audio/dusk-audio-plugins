#include "ChordRecorder.h"

//==============================================================================
ChordRecorder::ChordRecorder()
{
}

//==============================================================================
void ChordRecorder::startRecording()
{
    if (recording) return;

    clearSession();
    recording = true;
    sessionStartTime = 0.0;
    currentSession.startTime = juce::Time::getCurrentTime();
}

void ChordRecorder::stopRecording()
{
    if (!recording) return;

    // End any active chord
    if (hasActiveChord)
    {
        endCurrentChord(getRecordingDuration());
    }

    recording = false;
}

//==============================================================================
void ChordRecorder::recordChord(const ChordInfo& chord, double currentTimeSec)
{
    if (!recording) return;

    // Calculate relative time
    double relativeTime = currentTimeSec - sessionStartTime;
    if (relativeTime < 0) relativeTime = 0;

    // Check if this is a new chord
    if (!hasActiveChord || chord != lastChord)
    {
        // End the previous chord if there was one
        if (hasActiveChord)
        {
            endCurrentChord(relativeTime);
        }

        // Start a new chord if it's valid
        if (chord.isValid && !chord.name.isEmpty() && chord.name != "-")
        {
            lastChord = chord;
            lastChordStartTime = relativeTime;
            hasActiveChord = true;
        }
        else
        {
            hasActiveChord = false;
        }
    }
}

void ChordRecorder::endCurrentChord(double endTimeSec)
{
    if (!hasActiveChord) return;

    double duration = endTimeSec - lastChordStartTime;
    if (duration < 0.05) return;  // Ignore very short chords

    RecordedChordEvent event;
    event.chord = lastChord;
    event.startTimeSec = lastChordStartTime;
    event.durationSec = duration;

    // Calculate beat-based timing if tempo is set
    if (currentSession.tempoBPM > 0)
    {
        double beatsPerSecond = currentSession.tempoBPM / 60.0;
        event.startBeat = lastChordStartTime * beatsPerSecond;
        event.durationBeats = duration * beatsPerSecond;
    }

    currentSession.events.push_back(event);
    hasActiveChord = false;
}

//==============================================================================
void ChordRecorder::clearSession()
{
    currentSession = RecordingSession();
    sessionStartTime = 0.0;
    lastChord = ChordInfo();
    lastChordStartTime = 0.0;
    hasActiveChord = false;
}

void ChordRecorder::setSessionName(const juce::String& name)
{
    currentSession.name = name;
}

void ChordRecorder::setTempo(double bpm)
{
    currentSession.tempoBPM = bpm;
}

void ChordRecorder::setKey(int root, bool minor)
{
    currentSession.keyRoot = root;
    currentSession.isMinor = minor;
}

double ChordRecorder::getRecordingDuration() const
{
    if (currentSession.events.empty())
        return 0.0;

    const auto& last = currentSession.events.back();
    return last.startTimeSec + last.durationSec;
}
