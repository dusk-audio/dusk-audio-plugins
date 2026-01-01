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

//==============================================================================
juce::String ChordRecorder::escapeJSON(const juce::String& str) const
{
    juce::String result;
    for (int i = 0; i < str.length(); ++i)
    {
        juce::juce_wchar c = str[i];
        switch (c)
        {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
        }
    }
    return result;
}

juce::String ChordRecorder::chordInfoToJSON(const ChordInfo& chord) const
{
    juce::String json;
    json += "    {\n";
    json += "      \"name\": \"" + escapeJSON(chord.name) + "\",\n";
    json += "      \"romanNumeral\": \"" + escapeJSON(chord.romanNumeral) + "\",\n";
    json += "      \"function\": \"" + escapeJSON(ChordAnalyzer::functionToString(chord.function)) + "\",\n";
    json += "      \"quality\": \"" + escapeJSON(ChordAnalyzer::qualityToString(chord.quality)) + "\",\n";
    json += "      \"rootNote\": " + juce::String(chord.rootNote) + ",\n";
    json += "      \"rootName\": \"" + escapeJSON(ChordAnalyzer::pitchClassToName(chord.rootNote)) + "\",\n";

    // MIDI notes array
    json += "      \"midiNotes\": [";
    for (size_t i = 0; i < chord.midiNotes.size(); ++i)
    {
        if (i > 0) json += ", ";
        json += juce::String(chord.midiNotes[i]);
    }
    json += "],\n";

    json += "      \"inversion\": " + juce::String(chord.inversion);

    if (!chord.extensions.isEmpty())
    {
        json += ",\n      \"extensions\": \"" + escapeJSON(chord.extensions) + "\"";
    }

    json += "\n    }";
    return json;
}

juce::String ChordRecorder::eventToJSON(const RecordedChordEvent& event) const
{
    juce::String json;
    json += "  {\n";
    json += "    \"startTimeSec\": " + juce::String(event.startTimeSec, 3) + ",\n";
    json += "    \"durationSec\": " + juce::String(event.durationSec, 3) + ",\n";
    json += "    \"startBeat\": " + juce::String(event.startBeat, 3) + ",\n";
    json += "    \"durationBeats\": " + juce::String(event.durationBeats, 3) + ",\n";
    json += "    \"chord\":\n";
    json += chordInfoToJSON(event.chord) + "\n";
    json += "  }";
    return json;
}

juce::String ChordRecorder::exportToJSON() const
{
    juce::String json;

    json += "{\n";
    json += "  \"session\": {\n";
    json += "    \"name\": \"" + escapeJSON(currentSession.name) + "\",\n";
    json += "    \"timestamp\": \"" + currentSession.startTime.toISO8601(true) + "\",\n";
    json += "    \"tempoBPM\": " + juce::String(currentSession.tempoBPM, 1) + ",\n";
    json += "    \"key\": {\n";
    json += "      \"root\": " + juce::String(currentSession.keyRoot) + ",\n";
    json += "      \"rootName\": \"" + escapeJSON(ChordAnalyzer::pitchClassToName(currentSession.keyRoot)) + "\",\n";
    json += "      \"mode\": \"" + juce::String(currentSession.isMinor ? "minor" : "major") + "\"\n";
    json += "    },\n";
    json += "    \"totalEvents\": " + juce::String(currentSession.events.size()) + ",\n";

    // Calculate total duration
    double totalDuration = 0.0;
    if (!currentSession.events.empty())
    {
        const auto& last = currentSession.events.back();
        totalDuration = last.startTimeSec + last.durationSec;
    }
    json += "    \"totalDurationSec\": " + juce::String(totalDuration, 3) + "\n";
    json += "  },\n";

    // Events array
    json += "  \"progression\": [\n";
    for (size_t i = 0; i < currentSession.events.size(); ++i)
    {
        json += eventToJSON(currentSession.events[i]);
        if (i < currentSession.events.size() - 1)
            json += ",";
        json += "\n";
    }
    json += "  ],\n";

    // Summary - just the chord names and Roman numerals
    json += "  \"summary\": {\n";
    json += "    \"chordNames\": [";
    for (size_t i = 0; i < currentSession.events.size(); ++i)
    {
        if (i > 0) json += ", ";
        json += "\"" + escapeJSON(currentSession.events[i].chord.name) + "\"";
    }
    json += "],\n";

    json += "    \"romanNumerals\": [";
    for (size_t i = 0; i < currentSession.events.size(); ++i)
    {
        if (i > 0) json += ", ";
        json += "\"" + escapeJSON(currentSession.events[i].chord.romanNumeral) + "\"";
    }
    json += "]\n";
    json += "  }\n";

    json += "}\n";

    return json;
}

bool ChordRecorder::exportToFile(const juce::File& file) const
{
    juce::String json = exportToJSON();
    return file.replaceWithText(json);
}
