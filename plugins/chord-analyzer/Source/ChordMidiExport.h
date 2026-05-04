#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "ChordAnalyzer.h"
#include "ChordRecorder.h"

#include <vector>

// MIDI export helpers for the Chord Analyzer's drag-and-drop feature.
// Builds a temporary .mid file that the editor passes to
// FileDragAndDropContainer::performExternalDragDropOfFiles, which lets
// users drag chords / progressions straight into a DAW track.
//
// Replaced the JSON file export that issue #71 retired in favour of a
// DAW-native interchange format.
namespace ChordMidiExport
{
    // Pulses per quarter note for everything we emit. 480 is a common
    // DAW resolution (Ableton, Bitwig, Reaper all import cleanly at this).
    constexpr short kPPQ = 480;

    namespace detail
    {
        // Absolute path of the directory we drop temp .mid files into.
        // Each export gets a UUID suffix so concurrent plugin instances
        // can't overwrite each other's in-flight drag payloads.
        inline juce::File getTempDir()
        {
            auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("DuskAudio_ChordAnalyzer");
            dir.createDirectory();
            return dir;
        }

        // Sanitise a chord name into something that's safe as a filename
        // on every platform we ship to.
        inline juce::String sanitiseFilename (const juce::String& s)
        {
            return s.replaceCharacters ("/\\:*?\"<>|", "         ").trim();
        }

        // Emit a single-track MidiMessageSequence containing a tempo meta-
        // event followed by the supplied note-on / note-off pairs.
        inline juce::MidiMessageSequence buildSequence (
            const std::vector<std::pair<juce::MidiMessage, juce::MidiMessage>>& noteOnOffs,
            double tempoBPM)
        {
            juce::MidiMessageSequence seq;

            // Tempo meta-event at tick 0 — controls the timeline interpretation.
            auto tempoMeta = juce::MidiMessage::tempoMetaEvent (
                                static_cast<int> (60'000'000.0 / juce::jmax (1.0, tempoBPM)));
            tempoMeta.setTimeStamp (0.0);
            seq.addEvent (tempoMeta);

            for (const auto& pair : noteOnOffs)
            {
                seq.addEvent (pair.first);
                seq.addEvent (pair.second);
            }

            seq.updateMatchedPairs();
            return seq;
        }

        // Write a single-track MidiFile to the supplied file. Returns the
        // file on success (i.e. the same path back), or an invalid
        // juce::File if the write failed so callers can fall back gracefully.
        inline juce::File writeMidiFile (const juce::File& dest,
                                         const juce::MidiMessageSequence& seq)
        {
            juce::MidiFile mf;
            mf.setTicksPerQuarterNote (kPPQ);
            mf.addTrack (seq);

            juce::FileOutputStream out (dest);
            if (! out.openedOk())
                return {};
            if (! mf.writeTo (out))
                return {};
            return dest;
        }
    }

    // Build a one-bar MIDI clip of `chord` at 120 BPM and write it to a
    // temp .mid file. The clip places all of the chord's notes on at
    // tick 0 with duration = 1 bar (4 beats), velocity 100. Returns the
    // path to the temp file, or an invalid juce::File on write failure.
    inline juce::File writeChordToTempFile (const ChordInfo& chord)
    {
        if (! chord.isValid || chord.midiNotes.empty())
            return {};

        constexpr double tempoBPM = 120.0;
        constexpr int    onTick   = 0;
        constexpr int    offTick  = kPPQ * 4;   // one bar
        constexpr int    velocity = 100;

        std::vector<std::pair<juce::MidiMessage, juce::MidiMessage>> notes;
        notes.reserve (chord.midiNotes.size());
        for (int n : chord.midiNotes)
        {
            auto on  = juce::MidiMessage::noteOn  (1, n, (juce::uint8) velocity);
            auto off = juce::MidiMessage::noteOff (1, n);
            on .setTimeStamp ((double) onTick);
            off.setTimeStamp ((double) offTick);
            notes.emplace_back (on, off);
        }

        auto seq    = detail::buildSequence (notes, tempoBPM);
        auto fname  = "chord_" + detail::sanitiseFilename (chord.name)
                    + "_" + juce::Uuid().toString() + ".mid";
        auto target = detail::getTempDir().getChildFile (fname);
        return detail::writeMidiFile (target, seq);
    }

    // Build a multi-bar MIDI clip from a recorded session. Each event
    // lands at its `startBeat` for `durationBeats` (falling back to
    // seconds-as-beats at 120 BPM when beat data is missing). Tempo
    // meta-event at tick 0 carries the session's BPM.
    inline juce::File writeSessionToTempFile (const std::vector<RecordedChordEvent>& events,
                                              double tempoBPM)
    {
        if (events.empty())
            return {};

        const double bpm = tempoBPM > 0.0 ? tempoBPM : 120.0;
        constexpr int velocity = 100;

        std::vector<std::pair<juce::MidiMessage, juce::MidiMessage>> notes;

        for (const auto& ev : events)
        {
            if (! ev.chord.isValid || ev.chord.midiNotes.empty())
                continue;

            // Prefer beat-based timing when present; otherwise compute from
            // seconds at the resolved tempo so fall-back sessions still
            // align to the tempo meta event we'll emit.
            const double beatsPerSec = bpm / 60.0;
            const double startBeats  = ev.startBeat   > 0.0 ? ev.startBeat
                                                            : ev.startTimeSec * beatsPerSec;
            const double durBeats    = ev.durationBeats > 0.0 ? ev.durationBeats
                                                              : juce::jmax (0.25, ev.durationSec * beatsPerSec);

            const double onTick  = startBeats * kPPQ;
            const double offTick = (startBeats + durBeats) * kPPQ;

            for (int n : ev.chord.midiNotes)
            {
                auto on  = juce::MidiMessage::noteOn  (1, n, (juce::uint8) velocity);
                auto off = juce::MidiMessage::noteOff (1, n);
                on .setTimeStamp (onTick);
                off.setTimeStamp (offTick);
                notes.emplace_back (on, off);
            }
        }

        if (notes.empty())
            return {};

        auto seq    = detail::buildSequence (notes, bpm);
        auto target = detail::getTempDir().getChildFile (
                          "chord_progression_" + juce::Uuid().toString() + ".mid");
        return detail::writeMidiFile (target, seq);
    }
}
