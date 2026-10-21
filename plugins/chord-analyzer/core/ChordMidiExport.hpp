// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once
#include "ChordRecorder.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <utility>
#include <vector>

namespace duskaudio::chord::midi
{
inline constexpr short ppq = 480;
using Bytes = std::vector<std::uint8_t>;
namespace detail
{
struct NoteEvent { double tick; std::uint8_t pitch; bool on; };
double resolvedTempo(double bpm) noexcept;
bool appendNotes(std::vector<NoteEvent>& out, const std::vector<int>& notes,
                 double startTick, double endTick);
Bytes encode(std::vector<NoteEvent> notes, double tempoBPM);
}

// All export operations may allocate and belong off the audio thread.
// Templates let legacy JUCE structs and framework-free consumers share the
// exact event construction without copying/mirroring an entire session.
template<class Chord>
Bytes chordBytes(const Chord& chord)
{
    if (!chord.isValid || chord.midiNotes.empty()) return {};
    std::vector<detail::NoteEvent> notes;
    if (!detail::appendNotes(notes, chord.midiNotes, 0.0, ppq * 4.0)) return {};
    return detail::encode(std::move(notes), 120.0);
}

template<class Event>
Bytes sessionBytes(const std::vector<Event>& events, double tempoBPM)
{
    if (events.empty()) return {};
    const double bpm = detail::resolvedTempo(tempoBPM);
    std::vector<detail::NoteEvent> notes;
    for (const auto& event : events)
    {
        if (!event.chord.isValid || event.chord.midiNotes.empty()) continue;
        const double beatsPerSecond = bpm / 60.0;
        const double start = event.startBeat > 0.0 ? event.startBeat : event.startTimeSec * beatsPerSecond;
        const double duration = event.durationBeats > 0.0 ? event.durationBeats
                              : std::max(0.25, event.durationSec * beatsPerSecond);
        if (!detail::appendNotes(notes, event.chord.midiNotes, start * ppq, (start + duration) * ppq)) return {};
    }
    return notes.empty() ? Bytes{} : detail::encode(std::move(notes), bpm);
}

// Explicit destinations are replaced. Empty clips and write failures return
// false. Temporary exports use exclusively created per-export directories so
// concurrent instances/processes cannot truncate each other's drag payloads.
bool writeFile(const std::filesystem::path& destination, const Bytes& bytes) noexcept;
std::filesystem::path temporaryChordFile(const ChordDescription& chord) noexcept;
std::filesystem::path temporarySessionFile(const RecordingSession& session) noexcept;
// Temporary files remain valid after the call, including after an editor closes:
// a DAW may read a drag payload asynchronously. Callers own any later cleanup.
} // namespace duskaudio::chord::midi
