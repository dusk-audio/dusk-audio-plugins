#pragma once

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <string>
#include <array>

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
        // Brush articulations (for jazz/acoustic/singer-songwriter)
        BRUSH_SWIRL,      // Circular brush motion on snare
        BRUSH_SWEEP,      // Linear brush sweep
        BRUSH_TAP,        // Brush tap/accent
        BRUSH_SLAP,       // Brush slap on snare
        SIDE_STICK,       // Rim click / cross-stick
        NUM_ELEMENTS
    };

    struct MidiNote
    {
        int pitch;
        std::string name;
        DrumElement element;
        int defaultVelocity;
    };

    // Default General MIDI drum map
    // Note: These are defaults - user can customize via MidiNoteMap class
    const std::vector<MidiNote> drumMap = {
        {36, "Kick", KICK, 100},
        {35, "Kick 2", KICK, 95},
        {38, "Snare", SNARE, 90},
        {40, "Snare Rim", SNARE, 85},
        {37, "Side Stick", SIDE_STICK, 70},
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
        {70, "Shaker", SHAKER, 60},
        // Brush articulations - various mappings used by different drum plugins
        // Default to notes used by common drum samplers (adjust via MidiNoteMap)
        {25, "Brush Swirl", BRUSH_SWIRL, 65},     // Continuous brush swirl
        {26, "Brush Sweep", BRUSH_SWEEP, 70},     // Linear brush sweep
        {27, "Brush Tap", BRUSH_TAP, 80},         // Brush tap/accent
        {28, "Brush Slap", BRUSH_SLAP, 90}        // Brush slap
    };

    /**
     * MidiNoteMap - Configurable MIDI note mapping for each drum element
     *
     * Different drum plugins/samplers use different MIDI note assignments.
     * This class allows users to customize the mapping for their specific setup.
     * Common targets:
     * - General MIDI (GM) - Standard mapping
     * - Superior Drummer - Uses extended articulations
     * - Addictive Drums - Similar to GM with some variations
     * - EZdrummer - Toontrack mapping
     * - Steven Slate Drums - Custom mapping
     * - BFD - Custom extended mapping
     * - Abbey Road Drummer (Kontakt) - Custom mapping
     */
    class MidiNoteMap
    {
    public:
        MidiNoteMap()
        {
            // Initialize with GM defaults
            resetToDefaults();
        }

        // Get the MIDI note for a drum element
        int getNoteForElement(DrumElement element) const
        {
            if (element >= 0 && element < NUM_ELEMENTS)
                return noteMap[element];
            return 36; // Default to kick
        }

        // Set a custom MIDI note for a drum element
        void setNoteForElement(DrumElement element, int midiNote)
        {
            if (element >= 0 && element < NUM_ELEMENTS)
                noteMap[element] = juce::jlimit(0, 127, midiNote);
        }

        // Get element name for UI display
        static juce::String getElementName(DrumElement element)
        {
            switch (element)
            {
                case KICK:           return "Kick";
                case SNARE:          return "Snare";
                case HI_HAT_CLOSED:  return "Hi-Hat Closed";
                case HI_HAT_OPEN:    return "Hi-Hat Open";
                case HI_HAT_PEDAL:   return "Hi-Hat Pedal";
                case CRASH_1:        return "Crash 1";
                case CRASH_2:        return "Crash 2";
                case RIDE:           return "Ride";
                case RIDE_BELL:      return "Ride Bell";
                case TOM_FLOOR:      return "Floor Tom";
                case TOM_LOW:        return "Low Tom";
                case TOM_MID:        return "Mid Tom";
                case TOM_HIGH:       return "High Tom";
                case TAMBOURINE:     return "Tambourine";
                case COWBELL:        return "Cowbell";
                case CLAP:           return "Clap";
                case SHAKER:         return "Shaker";
                case BRUSH_SWIRL:    return "Brush Swirl";
                case BRUSH_SWEEP:    return "Brush Sweep";
                case BRUSH_TAP:      return "Brush Tap";
                case BRUSH_SLAP:     return "Brush Slap";
                case SIDE_STICK:     return "Side Stick";
                default:             return "Unknown";
            }
        }

        // Reset to General MIDI defaults
        void resetToDefaults()
        {
            noteMap[KICK] = 36;
            noteMap[SNARE] = 38;
            noteMap[HI_HAT_CLOSED] = 42;
            noteMap[HI_HAT_OPEN] = 46;
            noteMap[HI_HAT_PEDAL] = 44;
            noteMap[CRASH_1] = 49;
            noteMap[CRASH_2] = 57;
            noteMap[RIDE] = 51;
            noteMap[RIDE_BELL] = 53;
            noteMap[TOM_FLOOR] = 41;
            noteMap[TOM_LOW] = 43;
            noteMap[TOM_MID] = 45;
            noteMap[TOM_HIGH] = 48;
            noteMap[TAMBOURINE] = 54;
            noteMap[COWBELL] = 56;
            noteMap[CLAP] = 39;
            noteMap[SHAKER] = 70;
            noteMap[BRUSH_SWIRL] = 25;
            noteMap[BRUSH_SWEEP] = 26;
            noteMap[BRUSH_TAP] = 27;
            noteMap[BRUSH_SLAP] = 28;
            noteMap[SIDE_STICK] = 37;
        }

        // Load Superior Drummer 3 mapping
        void loadSuperiorDrummerMapping()
        {
            resetToDefaults();
            // Superior Drummer uses extended articulation notes
            noteMap[BRUSH_SWIRL] = 21;  // SD3 brush articulations
            noteMap[BRUSH_SWEEP] = 22;
            noteMap[BRUSH_TAP] = 23;
            noteMap[BRUSH_SLAP] = 24;
        }

        // Load EZdrummer/Addictive Drums mapping
        void loadEZdrummerMapping()
        {
            resetToDefaults();
            // Very close to GM, brush articulations on different notes
            noteMap[BRUSH_SWIRL] = 32;
            noteMap[BRUSH_SWEEP] = 33;
            noteMap[BRUSH_TAP] = 34;
            noteMap[BRUSH_SLAP] = 35;
        }

        // Load Steven Slate Drums mapping
        void loadSSDMapping()
        {
            resetToDefaults();
            // SSD has some different defaults
            noteMap[TOM_FLOOR] = 43;
            noteMap[TOM_LOW] = 45;
            noteMap[TOM_MID] = 47;
            noteMap[TOM_HIGH] = 50;
        }

        // Load BFD mapping
        void loadBFDMapping()
        {
            resetToDefaults();
            // BFD uses extended range for articulations
            noteMap[BRUSH_SWIRL] = 19;
            noteMap[BRUSH_SWEEP] = 20;
            noteMap[BRUSH_TAP] = 21;
            noteMap[BRUSH_SLAP] = 22;
        }

        // Serialize to ValueTree for save/load
        juce::ValueTree toValueTree() const
        {
            juce::ValueTree tree("MidiNoteMap");
            for (int i = 0; i < NUM_ELEMENTS; ++i)
            {
                tree.setProperty(juce::Identifier("note_" + juce::String(i)), noteMap[i], nullptr);
            }
            return tree;
        }

        // Load from ValueTree
        void fromValueTree(const juce::ValueTree& tree)
        {
            if (!tree.isValid() || tree.getType() != juce::Identifier("MidiNoteMap"))
            {
                resetToDefaults();
                return;
            }

            for (int i = 0; i < NUM_ELEMENTS; ++i)
            {
                auto propName = juce::Identifier("note_" + juce::String(i));
                if (tree.hasProperty(propName))
                {
                    noteMap[i] = juce::jlimit(0, 127, static_cast<int>(tree[propName]));
                }
            }
        }

    private:
        std::array<int, NUM_ELEMENTS> noteMap;
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
    // These define the core characteristics of how real drummers play each genre
    struct StyleHints
    {
        bool useRide = false;
        bool openHats = true;
        float ghostNoteProb = 0.1f;
        float fillFrequency = 0.1f;
        float syncopation = 0.2f;
        int primaryDivision = 16; // 8 or 16

        // Kick pattern characteristics
        bool fourOnFloor = false;        // Kick on every beat (house, disco)
        bool kickOnAnd = false;          // Kicks on "and" positions
        float kickSyncopation = 0.0f;    // How syncopated the kick is (0-1)

        // Snare characteristics
        bool halfTimeSnare = false;      // Snare only on beat 3 (half-time feel)
        bool rimClickInstead = false;    // Use rim click instead of snare (bossa, ballad)

        // Hi-hat characteristics
        int hatDivision = 8;             // 8 = 8ths, 16 = 16ths, 32 = 32nds (trap)
        float hatOpenProb = 0.1f;        // Probability of open hat
        bool hatAccentUpbeats = false;   // Accent upbeats (disco, funk)
        bool rollingHats = false;        // Trap-style rolling hi-hats

        // Feel
        float swingAmount = 0.0f;        // Default swing (0-0.5)
        float pushPull = 0.0f;           // Negative = rushed, positive = laid back
    };

    inline StyleHints getStyleHints(const juce::String& style)
    {
        StyleHints hints;

        if (style == "Rock")
        {
            // Classic rock: steady 8th note hats, solid backbeat, kick on 1 and 3
            // Think: AC/DC, Foo Fighters, classic rock
            hints.useRide = false;
            hints.openHats = true;
            hints.ghostNoteProb = 0.15f;      // Some ghost notes
            hints.fillFrequency = 0.15f;      // Regular fills
            hints.syncopation = 0.15f;        // Mostly straight
            hints.primaryDivision = 8;        // 8th note feel
            hints.fourOnFloor = false;
            hints.kickOnAnd = true;           // Kick on "and of 4" sometimes
            hints.kickSyncopation = 0.1f;
            hints.hatDivision = 8;            // Straight 8ths
            hints.hatOpenProb = 0.15f;        // Open on beat 4 upbeat
            hints.hatAccentUpbeats = false;
            hints.swingAmount = 0.0f;         // Straight
            hints.pushPull = -0.05f;          // Slightly pushed (energetic)
        }
        else if (style == "HipHop")
        {
            // Classic hip-hop: boom-bap style, syncopated kicks, ghost notes
            // Think: J Dilla, 90s hip-hop, Questlove
            hints.useRide = false;
            hints.openHats = false;
            hints.ghostNoteProb = 0.35f;      // Lots of ghost notes (key to the feel!)
            hints.fillFrequency = 0.05f;      // Minimal fills
            hints.syncopation = 0.5f;         // Very syncopated
            hints.primaryDivision = 16;       // 16th note feel
            hints.fourOnFloor = false;
            hints.kickOnAnd = true;           // Syncopated kicks
            hints.kickSyncopation = 0.4f;     // Heavy syncopation
            hints.hatDivision = 16;           // 16th hats or sparse 8ths
            hints.hatOpenProb = 0.05f;        // Minimal open hats
            hints.hatAccentUpbeats = false;
            hints.swingAmount = 0.15f;        // Slight swing (the J Dilla feel)
            hints.pushPull = 0.1f;            // Laid back pocket
        }
        else if (style == "Alternative")
        {
            // Alternative/Indie: dynamic, often uses ride, varied patterns
            // Think: Radiohead, Arctic Monkeys, The Black Keys
            hints.useRide = true;             // Often on ride
            hints.openHats = true;
            hints.ghostNoteProb = 0.2f;
            hints.fillFrequency = 0.12f;
            hints.syncopation = 0.25f;
            hints.primaryDivision = 8;
            hints.fourOnFloor = false;
            hints.kickOnAnd = true;
            hints.kickSyncopation = 0.2f;
            hints.hatDivision = 8;
            hints.hatOpenProb = 0.2f;
            hints.hatAccentUpbeats = false;
            hints.swingAmount = 0.0f;
            hints.pushPull = 0.0f;            // Neutral feel
        }
        else if (style == "R&B")
        {
            // R&B/Neo-Soul: 16th note feel, heavy ghost notes, laid back
            // Think: D'Angelo, Anderson .Paak, Erykah Badu
            hints.useRide = false;
            hints.openHats = true;
            hints.ghostNoteProb = 0.4f;       // Heavy ghost notes (essential!)
            hints.fillFrequency = 0.08f;
            hints.syncopation = 0.4f;
            hints.primaryDivision = 16;
            hints.fourOnFloor = false;
            hints.kickOnAnd = true;
            hints.kickSyncopation = 0.3f;
            hints.hatDivision = 16;           // 16th note hats
            hints.hatOpenProb = 0.12f;
            hints.hatAccentUpbeats = true;    // Accent upbeats
            hints.swingAmount = 0.1f;         // Slight swing
            hints.pushPull = 0.15f;           // Very laid back
        }
        else if (style == "Electronic")
        {
            // Electronic/House: four-on-floor, mechanical, open hats on upbeats
            // Think: Daft Punk, house music, EDM
            hints.useRide = false;
            hints.openHats = true;
            hints.ghostNoteProb = 0.0f;       // No ghost notes (mechanical)
            hints.fillFrequency = 0.02f;      // Minimal fills
            hints.syncopation = 0.1f;
            hints.primaryDivision = 16;
            hints.fourOnFloor = true;         // Kick on every beat!
            hints.kickOnAnd = false;
            hints.kickSyncopation = 0.0f;     // Very straight
            hints.hatDivision = 16;           // 16th or 8th hats
            hints.hatOpenProb = 0.5f;         // Open on upbeats (classic house)
            hints.hatAccentUpbeats = true;    // Upbeat accent
            hints.swingAmount = 0.0f;         // Dead straight
            hints.pushPull = 0.0f;            // Machine-like
        }
        else if (style == "Trap")
        {
            // Trap: rolling hi-hats, 808 kick patterns, sparse snare
            // Think: Metro Boomin, Travis Scott, modern hip-hop
            hints.useRide = false;
            hints.openHats = false;
            hints.ghostNoteProb = 0.0f;       // No ghost notes
            hints.fillFrequency = 0.0f;       // No fills (it's all about the pattern)
            hints.syncopation = 0.3f;
            hints.primaryDivision = 16;
            hints.fourOnFloor = false;
            hints.kickOnAnd = true;
            hints.kickSyncopation = 0.5f;     // Very syncopated kicks
            hints.halfTimeSnare = true;       // Snare on beat 3 only (half-time)
            hints.hatDivision = 32;           // Rolling 32nd note hats!
            hints.hatOpenProb = 0.0f;
            hints.rollingHats = true;         // Trap-style hat rolls
            hints.hatAccentUpbeats = false;
            hints.swingAmount = 0.0f;         // Straight
            hints.pushPull = 0.0f;
        }
        else if (style == "Songwriter")
        {
            // Singer-Songwriter/Ballad: simple, supportive, brushes/light touch
            // Think: acoustic performances, Ed Sheeran, John Mayer
            hints.useRide = false;
            hints.openHats = false;
            hints.ghostNoteProb = 0.05f;      // Minimal ghost notes
            hints.fillFrequency = 0.05f;      // Sparse fills
            hints.syncopation = 0.1f;
            hints.primaryDivision = 8;
            hints.fourOnFloor = false;
            hints.kickOnAnd = false;          // Simple kick pattern
            hints.kickSyncopation = 0.0f;
            hints.hatDivision = 8;            // Simple 8ths
            hints.hatOpenProb = 0.05f;
            hints.hatAccentUpbeats = false;
            hints.swingAmount = 0.0f;
            hints.pushPull = 0.05f;           // Slightly laid back
        }

        return hints;
    }
}