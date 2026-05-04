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
    sessionStartCaptured = false;   // anchor on first recordChord() call
    currentSession.startTime = juce::Time::getCurrentTime();
}

void ChordRecorder::stopRecording(double currentTimeSec)
{
    if (!recording) return;

    // End any active chord at the actual stop moment. Previously this
    // used getRecordingDuration() which reads the last completed event's
    // endpoint — for the still-active chord that's effectively its own
    // start time, giving duration = 0 and dropping it via the < 0.05s
    // guard in endCurrentChord(). Convert the absolute plugin time the
    // caller hands us into recording-relative the same way recordChord()
    // does.
    if (hasActiveChord)
    {
        double relativeTime = currentTimeSec - sessionStartTime;
        if (relativeTime < 0) relativeTime = 0;
        endCurrentChord(relativeTime);
    }

    recording = false;
}

//==============================================================================
void ChordRecorder::recordChord(const ChordInfo& chord, double currentTimeSec)
{
    if (!recording) return;

    // Anchor the session to the first chord we see — currentTimeSec is the
    // plugin's wall-clock accumulator (started at prepareToPlay), not a
    // recording-relative value, so without this anchor every event lands
    // at its absolute plugin time and the exported MIDI clip pushes all
    // notes to the back end of the timeline.
    if (!sessionStartCaptured)
    {
        sessionStartTime = currentTimeSec;
        sessionStartCaptured = true;
    }

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
    sessionStartCaptured = false;
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
