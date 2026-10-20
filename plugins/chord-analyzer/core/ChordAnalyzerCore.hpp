// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once

#include <cstdint>
#include <set>
#include <vector>

namespace duskaudio::chord
{
enum class ChordQuality
{
    Major,
    Minor,
    Diminished,
    Augmented,
    Dominant7,
    Major7,
    Minor7,
    MinorMajor7,
    Diminished7,
    HalfDiminished7,
    Augmented7,
    AugmentedMajor7,
    Sus2,
    Sus4,
    Dominant7Sus4,
    Add9,
    Add11,
    Major6,
    Minor6,
    Major9,
    Minor9,
    Dominant9,
    Major11,
    Minor11,
    Dominant11,
    Major13,
    Minor13,
    Dominant13,
    Power5,
    Dominant7Flat5,
    Dominant7Sharp5,
    Dominant7Flat9,
    Dominant7Sharp9,
    Unknown
};

enum class HarmonicFunction
{
    Tonic,           // I, vi, iii
    Subdominant,     // IV, ii
    Dominant,        // V, vii
    SecondaryDom,    // V/x chords
    Borrowed,        // Modal interchange (bVII, bVI, etc.)
    Chromatic,       // Outside the key
    Unknown
};

enum class SuggestionCategory
{
    Basic,          // Common progressions (I-IV-V-I)
    Intermediate,   // Secondary dominants, borrowed chords
    Advanced        // Modal interchange, tritone subs, chromatic mediants
};

// Inversion numbering. An inversion is the ordinal of the bass tone in the
// chord's stacked-thirds spelling, judged against the pattern that matched:
//
//   0  root      1  third      2  fifth      3  seventh
//   4  ninth     5  eleventh   6  thirteenth
//
// The interval alone cannot decide it - 3 semitones above the root is the minor
// third of an m7 and the #9 of a 7#9 - so the numbering always goes through the
// matched pattern.
//
// A bass the matched pattern does not spell (an added tension, or a foreign
// bass such as the F# under a C triad) is no inversion at all. It gets its own
// value, sorted after the six real ordinals so the host parameter that renders
// these can append one choice for it.
inline constexpr int kInversionSlashBass = 7;

struct ChordFacts
{
    int rootNote = -1;              // Root pitch class (0-11, C=0)
    int bassNote = -1;              // Lowest note pitch class
    ChordQuality quality = ChordQuality::Unknown;
    int inversion = 0;              // 0..6 by degree, kInversionSlashBass otherwise
    bool slashBass = false;         // Bass is not the root
    bool isValid = false;
    float confidence = 0.0f;
    int patternIndex = -1;          // matched entry, -1 when nothing fitted
    std::uint32_t intervals = 0;    // interval bit mask from the root

    bool operator==(const ChordFacts& o) const
    {
        return rootNote == o.rootNote && bassNote == o.bassNote
            && quality == o.quality && inversion == o.inversion
            && isValid == o.isValid;
    }
    bool operator!=(const ChordFacts& o) const { return !(*this == o); }
};

struct SuggestionFacts
{
    int rootNote = -1;                                  // 0-11, C=0; -1 when unset
    ChordQuality quality = ChordQuality::Unknown;
    SuggestionCategory category = SuggestionCategory::Basic;
    float commonality = 0.5f;                           // 0.0-1.0, how common the move is
};

// Numeric recognition and suggestions, shared by desktop and headless products.
// Immutable tables are built before main. Construction and query methods build
// no strings, allocate no memory and take no locks. UI formatting stays outside
// this boundary; the legacy formatter derives from it to use this same model.
class ChordAnalyzerCore
{
public:
    ChordAnalyzerCore() noexcept = default;
    void reset() noexcept { keyRoot = 0; minorKey = false; }
    ChordFacts analyzeFacts(const int* midiNotes, int numNotes) const noexcept;
    void setKey(int rootNote, bool isMinor);
    int getKeyRoot() const { return keyRoot; }
    bool isMinorKey() const { return minorKey; }
    HarmonicFunction getHarmonicFunction(int chordRoot, ChordQuality quality) const;
    static constexpr int maxSuggestions = 16;
    int getSuggestionFacts(const ChordFacts& currentChord, SuggestionCategory maxLevel,
                           SuggestionFacts* out, int maxOut) const noexcept;
    static std::uint16_t patternPitchClasses(int patternIndex) noexcept;

protected:
    int keyRoot = 0;        // C
    bool minorKey = false;

    //==========================================================================
    // Interval pattern matching
    struct ChordPattern
    {
        std::set<int> intervals;    // Semitone intervals from root
        ChordQuality quality;
        int priority;               // Higher = preferred match

        // Set on the shapes that deliberately leave the fifth out. The fifth
        // is the most disposable chord tone, so these voicings are everywhere
        // in practice, but the name has to say the fifth is absent or it would
        // claim a note that is not sounding.
        bool omitsFifth = false;
    };

    static const std::vector<ChordPattern> chordPatterns;

    //==========================================================================
    // Each pattern reduced to bit masks once, at static-init time, so analysis
    // touches no heap container. Interval masks carry bits 0-21 (the compound
    // 14, 17 and 21 included); pitch masks are folded to 12 bits.
    struct PatternMask
    {
        std::uint32_t intervals;
        std::uint16_t pitches;
        int           pitchCount;

        // Inversion ordinal of each pitch class in this pattern's spelling (see
        // kInversionSlashBass above for the numbering), -1 for a pitch class the
        // pattern does not spell. Resolved once, at static-init time, because
        // the same interval means different degrees in different patterns.
        std::int8_t   degrees[12];
    };

    static const std::vector<PatternMask> kPatternMasks;
    static std::vector<PatternMask> buildPatternMasks();

    //==========================================================================
    // Analysis helpers. All operate on masks and allocate nothing.
    static int findRoot(std::uint16_t pitchMask, int bassPitch) noexcept;
    static std::uint32_t intervalsFrom(std::uint16_t pitchMask, int root) noexcept;
    static bool patternMatches(int patternIndex, std::uint32_t intervals) noexcept;
    static int countPitchClasses(std::uint16_t mask) noexcept;
    static int matchPattern(std::uint32_t intervals) noexcept;   // -1 if none
    static int calculateInversion(int bassPitch, int root, int patternIndex) noexcept;
    static float calculateConfidence(int patternIndex, std::uint32_t intervals) noexcept;

    int getScaleDegree(int chordRoot) const noexcept;
    bool isChromatic(int chordRoot) const noexcept;

    enum class RootSpelling { KeyAware, Sharp, Flat };

    struct SuggestionSeed
    {
        int rootNote;                   // pitch class 0-11
        ChordQuality quality;
        SuggestionCategory category;
        float commonality;
        const char* romanNumeral;       // string literal, never owned
        const char* reason;             // string literal, never owned
        RootSpelling spelling;
        const char* nameSuffix;         // overrides qualityToSuffix when set
    };

    // Each appends to out[] starting at count and returns the new count,
    // never writing past maxOut.
    static int emitSeed(SuggestionSeed* out, int count, int maxOut,
                        const SuggestionSeed& seed) noexcept;
    int collectSuggestions(SuggestionSeed* out, int maxOut, int currentDegree,
                           ChordQuality quality, SuggestionCategory maxLevel) const noexcept;
    int addBasicSuggestions(SuggestionSeed* out, int count, int maxOut,
                            int currentDegree, ChordQuality quality) const noexcept;
    int addIntermediateSuggestions(SuggestionSeed* out, int count, int maxOut,
                                   int currentDegree, ChordQuality quality) const noexcept;
    int addAdvancedSuggestions(SuggestionSeed* out, int count, int maxOut,
                               int currentDegree, ChordQuality quality) const noexcept;

    int rootPitchClassForDegree(int degree) const noexcept;
};
} // namespace duskaudio::chord
