#pragma once

#include <juce_core/juce_core.h>
#include "../core/ChordAnalyzerCore.hpp"
#include <vector>

// Keep the shipping source API and enum ordinals while using one numeric model.
using ChordQuality = duskaudio::chord::ChordQuality;
using HarmonicFunction = duskaudio::chord::HarmonicFunction;
using SuggestionCategory = duskaudio::chord::SuggestionCategory;
using ChordFacts = duskaudio::chord::ChordFacts;
using SuggestionFacts = duskaudio::chord::SuggestionFacts;
inline constexpr int kInversionSlashBass = duskaudio::chord::kInversionSlashBass;

struct ChordInfo
{
    juce::String name;              // e.g., "Cmaj7", "Dm", "G7"
    juce::String romanNumeral;      // e.g., "I", "ii", "V7"
    HarmonicFunction function = HarmonicFunction::Unknown;
    std::vector<int> midiNotes;     // MIDI note numbers (sorted)
    int rootNote = -1;              // Root pitch class (0-11, C=0)
    int bassNote = -1;              // Lowest note pitch class
    ChordQuality quality = ChordQuality::Unknown;
    juce::String extensions;        // Any additional text
    int inversion = 0;              // 0..6 by degree, kInversionSlashBass otherwise
    bool slashBass = false;         // Bass is not the root - display slash notation
    bool isValid = false;
    float confidence = 0.0f;        // 0.0-1.0 confidence score

    bool operator==(const ChordInfo& other) const
    {
        // bassNote is part of the identity: C/E and C/G share a name, root and
        // quality, but they display differently and publish a different
        // detectedBass. Omitting it left both stale until some other note moved.
        return name == other.name && rootNote == other.rootNote
            && quality == other.quality && bassNote == other.bassNote;
    }

    bool operator!=(const ChordInfo& other) const
    {
        return !(*this == other);
    }
};

struct ChordSuggestion
{
    juce::String romanNumeral;
    juce::String chordName;         // Actual chord name in current key
    SuggestionCategory category;
    juce::String reason;            // Why this suggestion makes sense
    float commonality = 0.5f;       // How common this progression is (0.0-1.0)
};

// JUCE display adapter. Recognition, key state and numeric suggestions belong
// to the shared core; strings, history/UI structures and label formatting stay
// here so old sessions and existing editor/host-parameter text do not change.
class ChordAnalyzer : public duskaudio::chord::ChordAnalyzerCore
{
public:
    ChordAnalyzer() = default;
    ChordInfo analyze(const std::vector<int>& midiNotes);
    juce::String getKeyName() const;
    juce::String getRomanNumeral(const ChordInfo& chord) const;
    std::vector<ChordSuggestion> getSuggestions(const ChordInfo& currentChord,
        SuggestionCategory maxLevel = SuggestionCategory::Advanced) const;
    static juce::String noteToName(int midiNote, bool useFlats = false);
    static juce::String pitchClassToName(int pitchClass, bool useFlats = false);
    static int nameToNote(const juce::String& name);
    static juce::String qualityToString(ChordQuality quality);
    static juce::String qualityToSuffix(ChordQuality quality);
    static juce::String functionToString(HarmonicFunction func);

    // Host-parameter encoding of the label the editor displays.
    //
    // The "Detected Chord" parameter has to build its text out of the
    // parameter VALUE alone. Hosts call getText() with arbitrary values and
    // cache what comes back, so a stringFromValue that peeked at live
    // processor state would hand the host a string belonging to some other
    // value. encodeLabel packs everything the label depends on into one small
    // integer; decodeLabel rebuilds the string from that integer and nothing
    // else, through the same naming helpers analyze() uses.
    //
    // Value layout (the bases live in ChordAnalyzer.cpp):
    //   0                no chord, "-"
    //   1 .. 128         single note, MIDI 0..127 (that label carries an octave)
    //   129 .. 416       two sounding pitch classes, "C+E (M3)", "C+C (octave)"
    //   417 .. 428       nothing matched, "C?" .. "B?"
    //   429 .. 295340    matched pattern: root, sounding intervals, bass
    //
    // 295340 needs 19 bits, comfortably inside the 24-bit mantissa a float
    // NormalisableRange has to round-trip.
    static constexpr int maxLabelCode = 295340;

    // showInversions mirrors the editor's Show Inversions toggle: the slash
    // bass belongs to the label only while that toggle is on.
    static int encodeLabel(const ChordInfo& chord, bool showInversions) noexcept;
    static juce::String decodeLabel(int code);

private:
    static const char* tensionLabel(int semitonesFromRoot);
    static const char* intervalName(int semitones);
    static juce::String describeAddedTones(int patternIndex, std::uint32_t intervals);
    juce::String getAccidental(int chordRoot) const;
    juce::String degreeToRoman(int degree, bool uppercase) const;
    juce::String buildRomanNumeral(int chordRoot, ChordQuality quality) const;
    juce::String spellSeed(const SuggestionSeed& seed) const;
    juce::String getSpellingForKey(int pitchClass) const;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordAnalyzer)
};
