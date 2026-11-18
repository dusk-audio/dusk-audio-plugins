// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once
#include "ChordAnalyzerText.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace duskaudio::chord
{
struct RecordedChordEvent
{
    ChordDescription chord;
    double startTimeSec = 0.0;
    double durationSec = 0.0;
    double startBeat = 0.0;
    double durationBeats = 0.0;
};

struct RecordingSession
{
    using Event = RecordedChordEvent;
    using Name = std::string;
    using Time = std::int64_t; // Unix milliseconds, supplied by the caller
    Name name = "Untitled";
    Time startTime = 0;
    double tempoBPM = 120.0;
    int keyRoot = 0;
    bool isMinor = false;
    std::vector<Event> events;
};

// Non-real-time, single-owner recording state. Text and event vectors may
// allocate. The legacy adapter uses its existing JUCE chord/session types;
// native consumers use the framework-free types above. The state machine is
// shared without copying or mirroring an entire session on every chord change.
template<class Chord, class Session>
class BasicChordRecorder
{
public:
    void startRecording(std::int64_t startTimeMilliseconds)
    {
        if (recording) return;
        clearSession();
        // Initial capacity, not a recording limit. Growth is off the audio thread.
        currentSession.events.reserve(1024);
        recording = true;
        sessionStartTime = 0.0;
        sessionStartCaptured = false;
        currentSession.startTime = typename Session::Time(startTimeMilliseconds);
    }

    void stopRecording(double currentTimeSec)
    {
        if (!recording) return;
        if (hasActiveChord)
        {
            double relativeTime = currentTimeSec - sessionStartTime;
            if (relativeTime < 0) relativeTime = 0;
            endCurrentChord(relativeTime);
        }
        recording = false;
    }

    bool isRecording() const { return recording; }

    void recordChord(const Chord& chord, double currentTimeSec)
    {
        if (!recording) return;
        // The audio clock may have been running long before recording began.
        // Match the legacy anchor: the first observed chord, even if invalid.
        if (!sessionStartCaptured)
        {
            sessionStartTime = currentTimeSec;
            sessionStartCaptured = true;
        }
        double relativeTime = currentTimeSec - sessionStartTime;
        if (relativeTime < 0) relativeTime = 0;

        if (!hasActiveChord || chord != lastChord)
        {
            if (hasActiveChord) endCurrentChord(relativeTime);
            if (chord.isValid && chord.name != "" && chord.name != "-")
            {
                lastChord = chord;
                lastChordStartTime = relativeTime;
                hasActiveChord = true;
            }
            else hasActiveChord = false;
        }
    }

    // endTimeSec is already relative to the first observed chord.
    void endCurrentChord(double endTimeSec)
    {
        if (!hasActiveChord) return;
        const double duration = endTimeSec - lastChordStartTime;
        if (duration < 0.05) return;

        typename Session::Event event;
        event.chord = lastChord;
        event.startTimeSec = lastChordStartTime;
        event.durationSec = duration;
        if (currentSession.tempoBPM > 0)
        {
            const double beatsPerSecond = currentSession.tempoBPM / 60.0;
            event.startBeat = lastChordStartTime * beatsPerSecond;
            event.durationBeats = duration * beatsPerSecond;
        }
        currentSession.events.push_back(event);
        hasActiveChord = false;
    }

    // Deliberately preserve recording status, as the shipping Clear action does.
    void clearSession()
    {
        currentSession = Session();
        sessionStartTime = 0.0;
        sessionStartCaptured = false;
        lastChord = Chord();
        lastChordStartTime = 0.0;
        hasActiveChord = false;
    }
    const Session& getSession() const { return currentSession; }
    void setSessionName(const typename Session::Name& name) { currentSession.name = name; }
    void setTempo(double bpm) { currentSession.tempoBPM = bpm; }
    void setKey(int root, bool minor) { currentSession.keyRoot = root; currentSession.isMinor = minor; }
    const std::vector<typename Session::Event>& getEvents() const { return currentSession.events; }
    int getEventCount() const { return static_cast<int>(currentSession.events.size()); }
    double getRecordingDuration() const
    {
        if (currentSession.events.empty()) return 0.0;
        const auto& last = currentSession.events.back();
        return last.startTimeSec + last.durationSec;
    }

private:
    bool recording = false;
    Session currentSession;
    double sessionStartTime = 0.0;
    bool sessionStartCaptured = false;
    Chord lastChord;
    double lastChordStartTime = 0.0;
    bool hasActiveChord = false;
};

using ChordRecorder = BasicChordRecorder<ChordDescription, RecordingSession>;
} // namespace duskaudio::chord
