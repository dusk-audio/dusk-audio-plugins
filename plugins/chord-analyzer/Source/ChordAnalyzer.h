#pragma once

#include <juce_core/juce_core.h>
#include <vector>
#include <set>
#include <map>
#include <cstdint>

//==============================================================================
// Chord quality enumeration
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

//==============================================================================
// Harmonic function enumeration
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

//==============================================================================
// Suggestion category
enum class SuggestionCategory
{
    Basic,          // Common progressions (I-IV-V-I)
    Intermediate,   // Secondary dominants, borrowed chords
    Advanced        // Modal interchange, tritone subs, chromatic mediants
};

//==============================================================================
// Numeric analysis result: no strings, no heap, safe to compute on an audio
// thread. The headless LV2 wrapper publishes only these fields.
struct ChordFacts
{
    int rootNote = -1;              // Root pitch class (0-11, C=0)
    int bassNote = -1;              // Lowest note pitch class
    ChordQuality quality = ChordQuality::Unknown;
    int inversion = 0;              // 0=root, 1=first, 2=second, 3=seventh
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

//==============================================================================
// Chord information structure
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
    int inversion = 0;              // 0=root, 1=first, 2=second, etc.
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

//==============================================================================
// Chord suggestion structure
struct ChordSuggestion
{
    juce::String romanNumeral;
    juce::String chordName;         // Actual chord name in current key
    SuggestionCategory category;
    juce::String reason;            // Why this suggestion makes sense
    float commonality = 0.5f;       // How common this progression is (0.0-1.0)
};

//==============================================================================
// Main chord analyzer class
class ChordAnalyzer
{
public:
    ChordAnalyzer();

    //==========================================================================
    // Main analysis function
    ChordInfo analyze(const std::vector<int>& midiNotes);

    // Numeric-only analysis. Allocates nothing and builds no strings, so it is
    // safe to call from an audio callback - the headless LV2 wrapper does.
    // analyze() is this plus the display strings.
    ChordFacts analyzeFacts(const int* midiNotes, int numNotes) const noexcept;

    //==========================================================================
    // Key context
    void setKey(int rootNote, bool isMinor);
    int getKeyRoot() const { return keyRoot; }
    bool isMinorKey() const { return minorKey; }
    juce::String getKeyName() const;

    //==========================================================================
    // Get Roman numeral for chord in current key
    juce::String getRomanNumeral(const ChordInfo& chord) const;

    // Get harmonic function
    HarmonicFunction getHarmonicFunction(int chordRoot, ChordQuality quality) const;

    //==========================================================================
    // Get chord suggestions based on current chord
    std::vector<ChordSuggestion> getSuggestions(const ChordInfo& currentChord,
                                                 SuggestionCategory maxLevel = SuggestionCategory::Advanced) const;

    //==========================================================================
    // Static utilities
    static juce::String noteToName(int midiNote, bool useFlats = false);
    static juce::String pitchClassToName(int pitchClass, bool useFlats = false);
    static int nameToNote(const juce::String& name);
    static juce::String qualityToString(ChordQuality quality);
    static juce::String qualityToSuffix(ChordQuality quality);
    static juce::String functionToString(HarmonicFunction func);

private:
    int keyRoot = 0;        // C
    bool minorKey = false;

    //==========================================================================
    // Interval pattern matching
    struct ChordPattern
    {
        std::set<int> intervals;    // Semitone intervals from root
        ChordQuality quality;
        juce::String suffix;
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
    static int calculateInversion(int bassPitch, int root) noexcept;
    static float calculateConfidence(int patternIndex, std::uint32_t intervals) noexcept;

    //==========================================================================
    // Naming of tones the matched pattern does not account for
    static const char* tensionLabel(int semitonesFromRoot);   // nullptr if none
    static const char* intervalName(int semitones);           // "M3", "tritone", ...
    static juce::String describeAddedTones(int patternIndex, std::uint32_t intervals);

    //==========================================================================
    // Roman numeral helpers
    int getScaleDegree(int chordRoot) const;
    bool isChromatic(int chordRoot) const;
    juce::String getAccidental(int chordRoot) const;
    juce::String degreeToRoman(int degree, bool uppercase) const;
    juce::String buildRomanNumeral(int chordRoot, ChordQuality quality) const;

    //==========================================================================
    // Suggestion generation
    juce::String getRootNameInKey(int degree) const;
    juce::String getSpellingForKey(int pitchClass) const;
    void addBasicSuggestions(std::vector<ChordSuggestion>& suggestions, int currentDegree, ChordQuality quality) const;
    void addIntermediateSuggestions(std::vector<ChordSuggestion>& suggestions, int currentDegree, ChordQuality quality) const;
    void addAdvancedSuggestions(std::vector<ChordSuggestion>& suggestions, int currentDegree, ChordQuality quality) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordAnalyzer)
};
