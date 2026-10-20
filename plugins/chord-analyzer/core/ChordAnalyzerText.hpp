// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once

#include "ChordAnalyzerCore.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace duskaudio::chord
{
struct ChordDescription : ChordFacts
{
    std::string name;
    std::string romanNumeral;
    HarmonicFunction function = HarmonicFunction::Unknown;
    std::vector<int> midiNotes;
    std::string extensions;

    bool operator==(const ChordDescription& other) const
    {
        return name == other.name && rootNote == other.rootNote
            && quality == other.quality && bassNote == other.bassNote;
    }
    bool operator!=(const ChordDescription& other) const { return !(*this == other); }
};

struct ChordSuggestionText
{
    std::string romanNumeral;
    std::string chordName;
    SuggestionCategory category = SuggestionCategory::Basic;
    std::string reason;
    float commonality = 0.5f;
};

// Shared UTF-8 naming for desktop and DAF consumers. String/vector-returning
// methods allocate and belong on the UI/host-control thread. The numeric core
// and raw-note encodeLabel remain allocation-free. formatLabel and labelToCode
// also use bounded scratch storage; string/vector-returning methods are not RT-safe.
class ChordAnalyzerText : public ChordAnalyzerCore
{
public:
    ChordDescription analyze(const std::vector<int>& midiNotes);
    std::string getKeyName() const;
    std::vector<ChordSuggestionText> getSuggestions(const ChordFacts& chord,
        SuggestionCategory maxLevel = SuggestionCategory::Advanced) const;
    static std::string noteToName(int midiNote, bool useFlats = false);
    static std::string pitchClassToName(int pitchClass, bool useFlats = false);
    static int nameToNote(std::string_view name);
    static std::string qualityToString(ChordQuality quality);
    static std::string qualityToSuffix(ChordQuality quality);
    static std::string functionToString(HarmonicFunction function);

    // Retained shipping code layout: none, single note, dyad, unknown root,
    // then root/bass/pitch-mask codes. Adapters must not renumber this domain.
    static constexpr int maxLabelCode = 295340;
    static int encodeLabel(const ChordFacts& chord, const int* notes, int count,
                           bool showInversions) noexcept;
    static std::string decodeLabel(int code);
    // Includes the trailing NUL. Formatting fails without truncation; a usable
    // destination is empty on failure. Invalid codes retain the legacy "-".
    static constexpr std::size_t labelTextCapacity = 128;
    static bool formatLabel(int code, char* text, std::size_t capacity) noexcept;
    // Canonical-label inverse: codes with identical text share a representative.
    // Parsers accept ASCII note spellings/whitespace; legacy adapters retain
    // their own Unicode input normalization before entering this boundary.
    static int labelToCode(std::string_view text) noexcept;

private:
    static const char* qualitySuffix(ChordQuality quality) noexcept;
    static int addedToneLabels(int patternIndex, std::uint32_t intervals,
                              const char** extras) noexcept;
    static const char* tensionLabel(int semitonesFromRoot);
    static const char* intervalName(int semitones);
    static std::string describeAddedTones(int patternIndex, std::uint32_t intervals);
    std::string getAccidental(int chordRoot) const;
    std::string degreeToRoman(int degree, bool uppercase) const;
    std::string buildRomanNumeral(int chordRoot, ChordQuality quality) const;
    std::string spellSeed(const SuggestionSeed& seed) const;
    std::string getSpellingForKey(int pitchClass) const;
};
} // namespace duskaudio::chord
