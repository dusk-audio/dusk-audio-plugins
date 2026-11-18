// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once
#include <juce_core/juce_core.h>
#include "ChordAnalyzer.h"
#include "ChordRecorder.h"
#include "../core/ChordMidiExport.hpp"

// Shipping JUCE file/UUID adapter. MIDI event construction and SMF encoding are
// shared with the framework-free DAF export path; public editor calls stay intact.
namespace ChordMidiExport
{
    constexpr short kPPQ = duskaudio::chord::midi::ppq;
    namespace detail
    {
        inline juce::File getTempDir()
        {
            auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("DuskAudio_ChordAnalyzer");
            dir.createDirectory();
            return dir;
        }
        inline juce::String sanitiseFilename(const juce::String& name)
        {
            return name.replaceCharacters("/\\:*?\"<>|", "         ").trim();
        }
        inline juce::File writeMidiFile(const juce::File& destination,
                                       const duskaudio::chord::midi::Bytes& bytes)
        {
            if (bytes.empty()) return {};
            juce::FileOutputStream out(destination);
            if (!out.openedOk() || !out.write(bytes.data(), bytes.size())) return {};
            out.flush();
            return out.getStatus().wasOk() ? destination : juce::File{};
        }
    }
    inline juce::File writeChordToTempFile(const ChordInfo& chord)
    {
        auto bytes = duskaudio::chord::midi::chordBytes(chord);
        if (bytes.empty()) return {};
        const auto filename = "chord_" + detail::sanitiseFilename(chord.name)
                            + "_" + juce::Uuid().toString() + ".mid";
        return detail::writeMidiFile(detail::getTempDir().getChildFile(filename), bytes);
    }
    inline juce::File writeSessionToTempFile(const std::vector<RecordedChordEvent>& events,
                                            double tempoBPM)
    {
        auto bytes = duskaudio::chord::midi::sessionBytes(events, tempoBPM);
        if (bytes.empty()) return {};
        const auto filename = "chord_progression_" + juce::Uuid().toString() + ".mid";
        return detail::writeMidiFile(detail::getTempDir().getChildFile(filename), bytes);
    }
}
