#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <string>

/**
 * General MIDI Drum Mapping
 * Defines standard drum kit note mappings
 */
namespace DrumMapping
{
    enum DrumElement
    {
        KICK = 0,
        SNARE,
        HI_HAT_CLOSED,
        HI_HAT_OPEN,
        HI_HAT_PEDAL,
        CRASH_1,
        CRASH_2,
        RIDE,
        RIDE_BELL,
        TOM_FLOOR,
        TOM_LOW,
        TOM_MID,
        TOM_HIGH,
        TAMBOURINE,
        COWBELL,
        CLAP,
        SHAKER,
        NUM_ELEMENTS
    };

    struct MidiNote
    {
        int pitch;
        std::string name;
        DrumElement element;
        int defaultVelocity;
    };

    // General MIDI drum map
    const std::vector<MidiNote> drumMap = {
        {36, "Kick", KICK, 100},
        {35, "Kick 2", KICK, 95},
        {38, "Snare", SNARE, 90},
        {40, "Snare Rim", SNARE, 85},
        {37, "Side Stick", SNARE, 70},
        {42, "Hi-Hat Closed", HI_HAT_CLOSED, 80},
        {46, "Hi-Hat Open", HI_HAT_OPEN, 85},
        {44, "Hi-Hat Pedal", HI_HAT_PEDAL, 60},
        {49, "Crash 1", CRASH_1, 100},
        {57, "Crash 2", CRASH_2, 95},
        {51, "Ride", RIDE, 85},
        {53, "Ride Bell", RIDE_BELL, 90},
        {41, "Tom Floor", TOM_FLOOR, 85},
        {43, "Tom Low", TOM_LOW, 85},
        {45, "Tom Mid", TOM_MID, 85},
        {47, "Tom Mid High", TOM_MID, 85},
        {48, "Tom High", TOM_HIGH, 85},
        {50, "Tom High 2", TOM_HIGH, 85},
        {54, "Tambourine", TAMBOURINE, 70},
        {56, "Cowbell", COWBELL, 75},
        {39, "Clap", CLAP, 85},
        {70, "Shaker", SHAKER, 60}
    };

    // Pattern complexity levels
    enum Complexity
    {
        SIMPLE = 1,      // Basic kick & snare
        BASIC = 3,       // Add hi-hats
        MODERATE = 5,    // Add variations
        COMPLEX = 7,     // Add ghost notes
        INTENSE = 10     // Full kit, fills
    };

    // Helper functions
    inline int getNoteForElement(DrumElement element)
    {
        for (const auto& note : drumMap)
        {
            if (note.element == element)
                return note.pitch;
        }
        return 36; // Default to kick
    }

    inline std::vector<int> getNotesForElement(DrumElement element)
    {
        std::vector<int> notes;
        for (const auto& note : drumMap)
        {
            if (note.element == element)
                notes.push_back(note.pitch);
        }
        return notes;
    }

    inline int getDefaultVelocity(int pitch)
    {
        for (const auto& note : drumMap)
        {
            if (note.pitch == pitch)
                return note.defaultVelocity;
        }
        return 80; // Default velocity
    }

    // Style-specific pattern hints
    struct StyleHints
    {
        bool useRide = false;
        bool openHats = true;
        float ghostNoteProb = 0.1f;
        float fillFrequency = 0.1f;
        float syncopation = 0.2f;
        int primaryDivision = 16; // 8 or 16
    };

    inline StyleHints getStyleHints(const juce::String& style)
    {
        StyleHints hints;

        if (style == "Rock")
        {
            hints.useRide = false;
            hints.openHats = true;
            hints.ghostNoteProb = 0.15f;
            hints.fillFrequency = 0.15f;
            hints.syncopation = 0.2f;
            hints.primaryDivision = 8;
        }
        else if (style == "HipHop")
        {
            hints.useRide = false;
            hints.openHats = false;
            hints.ghostNoteProb = 0.25f;
            hints.fillFrequency = 0.05f;
            hints.syncopation = 0.4f;
            hints.primaryDivision = 16;
        }
        else if (style == "Jazz")
        {
            hints.useRide = true;
            hints.openHats = false;
            hints.ghostNoteProb = 0.3f;
            hints.fillFrequency = 0.1f;
            hints.syncopation = 0.3f;
            hints.primaryDivision = 16;
        }
        else if (style == "Electronic")
        {
            hints.useRide = false;
            hints.openHats = true;
            hints.ghostNoteProb = 0.05f;
            hints.fillFrequency = 0.02f;
            hints.syncopation = 0.1f;
            hints.primaryDivision = 16;
        }
        else if (style == "R&B")
        {
            hints.useRide = false;
            hints.openHats = true;
            hints.ghostNoteProb = 0.2f;
            hints.fillFrequency = 0.08f;
            hints.syncopation = 0.35f;
            hints.primaryDivision = 16;
        }
        else if (style == "Alternative")
        {
            hints.useRide = true;
            hints.openHats = true;
            hints.ghostNoteProb = 0.18f;
            hints.fillFrequency = 0.12f;
            hints.syncopation = 0.25f;
            hints.primaryDivision = 8;
        }

        return hints;
    }
}