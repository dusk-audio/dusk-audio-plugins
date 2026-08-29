#include "ChordAnalyzer.h"
#include <algorithm>
#include <cmath>

//==============================================================================
// Chord patterns - interval sets from root (in semitones)
// Priority determines which pattern wins when multiple match. Patterns that
// tie on both priority and size fall back to DECLARATION ORDER, so this list
// is ordered by preferred spelling: Major before Minor before Diminished, and
// the lower tension first (add9 before add11, so C E G D F reads Cadd9(11)).
// Reordering entries within a priority band changes chord names.
const std::vector<ChordAnalyzer::ChordPattern> ChordAnalyzer::chordPatterns = {
    // Power chord (2 notes)
    {{0, 7}, ChordQuality::Power5, "5", 1},

    // Triads (3 notes)
    {{0, 4, 7}, ChordQuality::Major, "", 10},
    {{0, 3, 7}, ChordQuality::Minor, "m", 10},
    {{0, 3, 6}, ChordQuality::Diminished, "dim", 10},
    {{0, 4, 8}, ChordQuality::Augmented, "aug", 10},
    {{0, 2, 7}, ChordQuality::Sus2, "sus2", 9},
    {{0, 5, 7}, ChordQuality::Sus4, "sus4", 9},

    // Sixth chords
    {{0, 4, 7, 9}, ChordQuality::Major6, "6", 15},
    {{0, 3, 7, 9}, ChordQuality::Minor6, "m6", 15},

    // Seventh chords (4 notes)
    {{0, 4, 7, 11}, ChordQuality::Major7, "maj7", 20},
    {{0, 4, 7, 10}, ChordQuality::Dominant7, "7", 20},
    {{0, 3, 7, 10}, ChordQuality::Minor7, "m7", 20},
    {{0, 3, 7, 11}, ChordQuality::MinorMajor7, "mMaj7", 20},
    {{0, 3, 6, 10}, ChordQuality::HalfDiminished7, "m7b5", 20},
    {{0, 3, 6, 9}, ChordQuality::Diminished7, "dim7", 20},
    {{0, 4, 8, 10}, ChordQuality::Augmented7, "aug7", 20},
    {{0, 4, 8, 11}, ChordQuality::AugmentedMajor7, "augMaj7", 20},
    {{0, 5, 7, 10}, ChordQuality::Dominant7Sus4, "7sus4", 19},

    // Altered dominants
    {{0, 4, 6, 10}, ChordQuality::Dominant7Flat5, "7b5", 21},
    {{0, 4, 8, 10}, ChordQuality::Dominant7Sharp5, "7#5", 21},
    {{0, 1, 4, 7, 10}, ChordQuality::Dominant7Flat9, "7b9", 25},
    {{0, 3, 4, 7, 10}, ChordQuality::Dominant7Sharp9, "7#9", 25},

    // Add chords
    {{0, 4, 7, 14}, ChordQuality::Add9, "add9", 16},
    {{0, 4, 7, 17}, ChordQuality::Add11, "add11", 16},

    // Extended chords (5+ notes)
    {{0, 4, 7, 10, 14}, ChordQuality::Dominant9, "9", 30},
    {{0, 4, 7, 11, 14}, ChordQuality::Major9, "maj9", 30},
    {{0, 3, 7, 10, 14}, ChordQuality::Minor9, "m9", 30},

    {{0, 4, 7, 10, 14, 17}, ChordQuality::Dominant11, "11", 35},
    {{0, 4, 7, 11, 14, 17}, ChordQuality::Major11, "maj11", 35},
    {{0, 3, 7, 10, 14, 17}, ChordQuality::Minor11, "m11", 35},

    {{0, 4, 7, 10, 14, 17, 21}, ChordQuality::Dominant13, "13", 40},
    {{0, 4, 7, 11, 14, 17, 21}, ChordQuality::Major13, "maj13", 40},
    {{0, 3, 7, 10, 14, 17, 21}, ChordQuality::Minor13, "m13", 40},

    // Simplified extended chords (without all tensions)
    {{0, 4, 7, 10, 21}, ChordQuality::Dominant13, "13", 28},  // 13 without 9/11
    {{0, 4, 7, 10, 17}, ChordQuality::Dominant11, "11", 28},  // 11 without 9

    // Voicings that drop the fifth. Standard practice on guitar and piano -
    // the fifth adds no colour, so it is the first tone to go - and the shell
    // voicing (root, third, seventh) is the backbone of jazz comping.
    //
    // These sit BELOW every pattern that contains a fifth, not just below
    // their own full counterparts. findRoot compares priorities across
    // candidate roots, so a high-priority incomplete shape wins at the wrong
    // root: at 28, maj9(no5) rooted on Ab beat a plain C7 rooted on the bass
    // and C E G Ab Bb came out as G#maj9(no5,b13)/C. Ranked here only against
    // each other, they win when, and only when, no fifth is sounding.
    {{0, 4, 14},         ChordQuality::Add9,        "add9",   3, true},
    {{0, 4, 10},         ChordQuality::Dominant7,   "7",      5, true},
    {{0, 4, 11},         ChordQuality::Major7,      "maj7",   5, true},
    {{0, 3, 10},         ChordQuality::Minor7,      "m7",     5, true},
    {{0, 3, 11},         ChordQuality::MinorMajor7, "mMaj7",  5, true},
    {{0, 4, 10, 14},     ChordQuality::Dominant9,   "9",      7, true},
    {{0, 4, 11, 14},     ChordQuality::Major9,      "maj9",   7, true},
    {{0, 3, 10, 14},     ChordQuality::Minor9,      "m9",     7, true},
};

//==============================================================================
ChordAnalyzer::ChordAnalyzer()
{
}

//==============================================================================
ChordFacts ChordAnalyzer::analyzeFacts(const int* midiNotes, int numNotes) const noexcept
{
    ChordFacts facts;

    if (midiNotes == nullptr || numNotes <= 0)
        return facts;

    // Fold to a pitch-class mask and find the lowest sounding note. Neither
    // needs the notes sorted or copied.
    std::uint16_t pitchMask = 0;
    int lowest = midiNotes[0];

    for (int i = 0; i < numNotes; ++i)
    {
        pitchMask = static_cast<std::uint16_t>(pitchMask | (1u << (((midiNotes[i] % 12) + 12) % 12)));
        lowest = std::min(lowest, midiNotes[i]);
    }

    facts.bassNote = ((lowest % 12) + 12) % 12;

    if (countPitchClasses(pitchMask) < 2)
    {
        // A single pitch class is a note, not a chord
        facts.rootNote = facts.bassNote;
        return facts;
    }

    facts.rootNote = findRoot(pitchMask, facts.bassNote);
    facts.intervals = intervalsFrom(pitchMask, facts.rootNote);
    facts.patternIndex = matchPattern(facts.intervals);

    if (facts.patternIndex < 0)
    {
        facts.confidence = 0.3f;
        return facts;
    }

    facts.quality = chordPatterns[static_cast<size_t>(facts.patternIndex)].quality;
    facts.inversion = calculateInversion(facts.bassNote, facts.rootNote);
    facts.slashBass = (facts.bassNote != facts.rootNote);
    facts.confidence = calculateConfidence(facts.patternIndex, facts.intervals);
    facts.isValid = true;

    return facts;
}

//==============================================================================
ChordInfo ChordAnalyzer::analyze(const std::vector<int>& midiNotes)
{
    ChordInfo result;
    result.midiNotes = midiNotes;
    std::sort(result.midiNotes.begin(), result.midiNotes.end());

    if (midiNotes.empty())
    {
        result.name = "-";
        result.romanNumeral = "-";
        return result;
    }

    const ChordFacts facts = analyzeFacts(midiNotes.data(), static_cast<int>(midiNotes.size()));

    result.rootNote  = facts.rootNote;
    result.bassNote  = facts.bassNote;
    result.quality   = facts.quality;
    result.inversion = facts.inversion;
    result.slashBass = facts.slashBass;
    result.confidence = facts.confidence;
    result.isValid   = facts.isValid;

    if (midiNotes.size() == 1)
    {
        // Single note - just show the note name
        result.name = noteToName(midiNotes[0]);
        result.romanNumeral = "-";
        return result;
    }

    if (facts.patternIndex < 0)
    {
        // Two notes are an interval, not a chord, so name the interval rather
        // than giving up with "C?". The dyads that do imply a chord, the
        // fourth and the fifth, match a pattern above and never reach here.
        std::uint16_t pitchMask = 0;
        for (int note : midiNotes)
            pitchMask = static_cast<std::uint16_t>(pitchMask | (1u << (((note % 12) + 12) % 12)));

        if (countPitchClasses(pitchMask) == 2)
        {
            int upper = result.bassNote;
            for (int pc = 0; pc < 12; ++pc)
            {
                if (pc != result.bassNote && (pitchMask & (1u << pc)) != 0)
                {
                    upper = pc;
                    break;
                }
            }

            result.name = pitchClassToName(result.bassNote) + "+" + pitchClassToName(upper)
                        + " (" + intervalName(((upper - result.bassNote) + 12) % 12) + ")";
            result.romanNumeral = "-";
            return result;
        }

        // No known chord shape fits these notes
        result.name = pitchClassToName(result.rootNote) + "?";
        result.romanNumeral = "?";
        return result;
    }

    // Build chord name. Any sounding tone the matched pattern does not account
    // for is spelled out instead of dropped, so C E G F# reads "Cadd#11" and
    // not "C" (issue #235).
    result.name = pitchClassToName(result.rootNote)
                + qualityToSuffix(result.quality)
                + describeAddedTones(facts.patternIndex, facts.intervals);

    // Slash notation for any bass note other than the root. This covers the
    // classic inversions and also an upper-structure bass such as the #11,
    // which calculateInversion cannot number but must still be shown.
    if (result.slashBass)
        result.extensions = "/" + pitchClassToName(result.bassNote);

    result.romanNumeral = buildRomanNumeral(result.rootNote, result.quality);
    result.function = getHarmonicFunction(result.rootNote, result.quality);

    return result;
}

//==============================================================================
void ChordAnalyzer::setKey(int rootNote, bool isMinor)
{
    keyRoot = rootNote % 12;
    minorKey = isMinor;
}

juce::String ChordAnalyzer::getKeyName() const
{
    return pitchClassToName(keyRoot) + (minorKey ? " Minor" : " Major");
}

//==============================================================================
std::vector<ChordAnalyzer::PatternMask> ChordAnalyzer::buildPatternMasks()
{
    std::vector<PatternMask> masks;
    masks.reserve(chordPatterns.size());

    for (const auto& pattern : chordPatterns)
    {
        PatternMask m { 0u, 0u, 0 };

        for (int interval : pattern.intervals)
        {
            m.intervals |= (1u << interval);
            m.pitches = static_cast<std::uint16_t>(m.pitches | (1u << (interval % 12)));
        }

        m.pitchCount = countPitchClasses(m.pitches);
        masks.push_back(m);
    }

    return masks;
}

// Built once at static-init time, after chordPatterns, so no analysis call
// ever has to allocate this table.
const std::vector<ChordAnalyzer::PatternMask> ChordAnalyzer::kPatternMasks
    = ChordAnalyzer::buildPatternMasks();

int ChordAnalyzer::countPitchClasses(std::uint16_t mask) noexcept
{
    int n = 0;
    for (; mask != 0; mask &= static_cast<std::uint16_t>(mask - 1))
        ++n;
    return n;
}

std::uint32_t ChordAnalyzer::intervalsFrom(std::uint16_t pitchMask, int root) noexcept
{
    std::uint32_t intervals = 1u;   // the root is always interval 0

    for (int pc = 0; pc < 12; ++pc)
    {
        if ((pitchMask & (1u << pc)) == 0)
            continue;

        const int interval = ((pc - root) + 12) % 12;
        intervals |= (1u << interval);

        // Also state compound intervals, so the extended patterns can match
        if (interval == 2) intervals |= (1u << 14);   // 9th
        if (interval == 5) intervals |= (1u << 17);   // 11th
        if (interval == 9) intervals |= (1u << 21);   // 13th
    }

    return intervals;
}

bool ChordAnalyzer::patternMatches(int patternIndex, std::uint32_t intervals) noexcept
{
    const PatternMask& m = kPatternMasks[static_cast<size_t>(patternIndex)];

    // Every tone the pattern requires must be sounding (extra notes allowed)
    if ((intervals & m.intervals) != m.intervals)
        return false;

    // A b5 or #5 spelling *replaces* the fifth, so it cannot describe a chord
    // that also sounds a perfect fifth - there the altered tone is a #11 or a
    // b13. Without this, C E G Bb F# matched C7b5 and the natural fifth was
    // thrown away.
    const bool soundsFifth  = (intervals   & (1u << 7)) != 0;
    const bool patternFifth = (m.intervals & (1u << 7)) != 0;
    const bool patternAltFifth = (m.intervals & ((1u << 6) | (1u << 8))) != 0;

    if (soundsFifth && ! patternFifth && patternAltFifth)
        return false;

    return true;
}

int ChordAnalyzer::findRoot(std::uint16_t pitchMask, int bassPitch) noexcept
{
    const int soundingTones = countPitchClasses(pitchMask);
    int lowestPitch = 0;
    while (lowestPitch < 12 && (pitchMask & (1u << lowestPitch)) == 0)
        ++lowestPitch;

    if (soundingTones < 2)
        return lowestPitch;

    // Try each note as potential root and score the result
    int bestRoot = lowestPitch;
    int bestPriority = -1;
    float bestScore = 0.0f;
    bool bestIsBass = false;
    bool found = false;          // a match scoring <= 0 is still a match

    for (int candidateRoot = 0; candidateRoot < 12; ++candidateRoot)
    {
        if ((pitchMask & (1u << candidateRoot)) == 0)
            continue;

        const std::uint32_t intervals = intervalsFrom(pitchMask, candidateRoot);

        // Try to match a pattern
        for (size_t i = 0; i < chordPatterns.size(); ++i)
        {
            // Check if intervals match pattern (allowing extra notes)
            if (! patternMatches(static_cast<int>(i), intervals))
                continue;

            // Penalty for sounding notes the pattern does not explain.
            // Counted in pitch classes: intervalsFrom also states a 9th as
            // 14, an 11th as 17 and a 13th as 21, and charging those
            // restatements as extra notes penalised exactly the chords
            // that have them. C E G A scored 19.5 as C6 but 20 as Am7,
            // purely because the synthetic 21 made C6 look impure.
            const int patternTones = kPatternMasks[i].pitchCount;
            const int extraNotes   = soundingTones - patternTones;

            // A shape smaller than a triad may carry at most one added
            // tone. The two-note power chord fits almost anything, and
            // without this it "identifies" dense chromatic clusters as
            // F5(b13,13) and the like. One extra still allows the real
            // C5add#11 voicing. Bass-independent, so the same notes read
            // the same way whichever one of them is lowest - previously a
            // cluster was named or not purely according to its bass.
            if (patternTones < 3 && extraNotes > 1)
                continue;

            const int priority = chordPatterns[i].priority;
            float score = static_cast<float>(priority);

            // Bonus for bass note being the root
            const bool isBass = (bassPitch == candidateRoot);
            if (isBass)
                score += 5.0f;

            score -= static_cast<float>(extraNotes) * 0.5f;

            // Rank on score, then on the bass, then on pattern priority.
            // The bass has to outrank priority: C E G A over a C bass is
            // C6, and only the tie-break tells it from Am7's first
            // inversion, which scores the same but is not in the bass.
            const bool better = ! found
                || score > bestScore
                || (score == bestScore && isBass && ! bestIsBass)
                || (score == bestScore && isBass == bestIsBass && priority > bestPriority);

            if (better)
            {
                found = true;
                bestIsBass = isBass;
                bestScore = score;
                bestPriority = priority;
                bestRoot = candidateRoot;
            }
        }
    }

    return bestRoot;
}

int ChordAnalyzer::matchPattern(std::uint32_t intervals) noexcept
{
    int best = -1;

    for (size_t i = 0; i < chordPatterns.size(); ++i)
    {
        if (! patternMatches(static_cast<int>(i), intervals))
            continue;

        // Prefer patterns that match more notes (more specific)
        // But also consider priority. A pattern that ties on both keeps the
        // incumbent, so an exact tie resolves to whichever is declared first
        // in chordPatterns - see the ordering contract on that table.
        if (best < 0
            || chordPatterns[i].priority > chordPatterns[static_cast<size_t>(best)].priority
            || (chordPatterns[i].priority == chordPatterns[static_cast<size_t>(best)].priority
                && chordPatterns[i].intervals.size() > chordPatterns[static_cast<size_t>(best)].intervals.size()))
        {
            best = static_cast<int>(i);
        }
    }

    return best;
}

const char* ChordAnalyzer::tensionLabel(int semitonesFromRoot)
{
    // Folded to a pitch class so a compound interval names the same tension as
    // its simple form (14 and 2 are both a 9th, 17 and 5 both an 11th).
    // String literals, so naming a tension costs no allocation.
    switch (semitonesFromRoot % 12)
    {
        case 1:  return "b9";
        case 2:  return "9";
        case 3:  return "#9";
        case 4:  return "3";
        case 5:  return "11";
        case 6:  return "#11";
        case 7:  return "5";
        case 8:  return "b13";
        case 9:  return "13";
        case 10: return "b7";
        case 11: return "maj7";
        default: return nullptr;   // 0 is the root, always covered by the pattern
    }
}

const char* ChordAnalyzer::intervalName(int semitones)
{
    switch (((semitones % 12) + 12) % 12)
    {
        case 0:  return "octave";
        case 1:  return "m2";
        case 2:  return "M2";
        case 3:  return "m3";
        case 4:  return "M3";
        case 5:  return "P4";
        case 6:  return "tritone";
        case 7:  return "P5";
        case 8:  return "m6";
        case 9:  return "M6";
        case 10: return "m7";
        default: return "M7";
    }
}

juce::String ChordAnalyzer::describeAddedTones(int patternIndex, std::uint32_t intervals)
{
    // Compare as pitch classes: intervalsFrom states a 9th as both 2 and 14,
    // and the patterns use the compound form, so a raw difference would report
    // every add9/add11/13 chord tone as an extra note.
    const PatternMask& m = kPatternMasks[static_cast<size_t>(patternIndex)];

    // At most one extra per pitch class, plus the "no5" marker.
    const char* extras[13];
    int numExtras = 0;

    // Announce a missing fifth first, so it reads C9(no5,#11) rather than
    // splitting the parenthesis into two groups.
    const bool omitsFifth = chordPatterns[static_cast<size_t>(patternIndex)].omitsFifth;
    if (omitsFifth)
        extras[numExtras++] = "no5";

    for (int interval = 0; interval < 12; ++interval)
    {
        if ((intervals & (1u << interval)) == 0 || (m.pitches & (1u << interval)) != 0)
            continue;

        if (const char* label = tensionLabel(interval))
            extras[numExtras++] = label;
    }

    if (numExtras == 0)
        return {};

    // A triad carrying one extra tone reads as an add chord ("Cadd#11");
    // anything richer takes parenthesised tensions ("C7(#11)", "C9(b13)").
    // An absent fifth is never an "add", so those always take the parens.
    if (! omitsFifth && numExtras == 1
        && chordPatterns[static_cast<size_t>(patternIndex)].intervals.size() <= 3)
        return juce::String("add") + extras[0];

    juce::String text("(");
    for (int i = 0; i < numExtras; ++i)
    {
        if (i > 0)
            text << ",";
        text << extras[i];
    }
    text << ")";

    return text;
}

int ChordAnalyzer::calculateInversion(int bassPitch, int root) noexcept
{
    if (bassPitch == root)
        return 0;

    const int bassInterval = ((bassPitch - root) + 12) % 12;

    if (bassInterval == 3 || bassInterval == 4) return 1;  // Third in bass
    if (bassInterval == 7) return 2;  // Fifth in bass
    if (bassInterval == 10 || bassInterval == 11) return 3;  // Seventh in bass

    return 0;
}

float ChordAnalyzer::calculateConfidence(int patternIndex, std::uint32_t intervals) noexcept
{
    // Score against the pattern that actually matched. Comparing in pitch
    // classes keeps a compound pattern tone (9th written as 14) from counting
    // its own chord tone as an extra note.
    const PatternMask& m = kPatternMasks[static_cast<size_t>(patternIndex)];

    int matchedIntervals = 0;
    int extraIntervals = 0;

    for (int interval = 0; interval < 12; ++interval)
    {
        if ((intervals & (1u << interval)) == 0)
            continue;

        if ((m.pitches & (1u << interval)) != 0)
            matchedIntervals++;
        else if (interval != 0)
            extraIntervals++;
    }

    const float patternMatch = static_cast<float>(matchedIntervals) / static_cast<float>(m.pitchCount);
    const float penalty = static_cast<float>(extraIntervals) * 0.1f;

    return juce::jlimit(0.0f, 1.0f, patternMatch - penalty);
}

//==============================================================================
// Roman numeral generation
int ChordAnalyzer::getScaleDegree(int chordRoot) const
{
    int interval = (chordRoot - keyRoot + 12) % 12;

    if (minorKey)
    {
        // Natural minor scale: 0, 2, 3, 5, 7, 8, 10
        static const std::map<int, int> minorDegrees = {
            {0, 1}, {2, 2}, {3, 3}, {5, 4}, {7, 5}, {8, 6}, {10, 7}
        };
        auto it = minorDegrees.find(interval);
        if (it != minorDegrees.end()) return it->second;
    }
    else
    {
        // Major scale: 0, 2, 4, 5, 7, 9, 11
        static const std::map<int, int> majorDegrees = {
            {0, 1}, {2, 2}, {4, 3}, {5, 4}, {7, 5}, {9, 6}, {11, 7}
        };
        auto it = majorDegrees.find(interval);
        if (it != majorDegrees.end()) return it->second;
    }

    // Chromatic - find closest degree
    if (interval == 1) return 2;   // b2
    if (interval == 3 && !minorKey) return 3;   // b3 in major
    if (interval == 4 && minorKey) return 3;    // #3 in minor
    if (interval == 6) return 4;   // #4/b5
    if (interval == 8 && !minorKey) return 6;   // b6 in major
    if (interval == 9 && minorKey) return 6;    // #6 in minor
    if (interval == 10 && !minorKey) return 7;  // b7 in major
    if (interval == 11 && minorKey) return 7;   // #7 in minor

    return 1;  // Default to tonic
}

bool ChordAnalyzer::isChromatic(int chordRoot) const
{
    int interval = (chordRoot - keyRoot + 12) % 12;

    if (minorKey)
    {
        // Natural minor scale degrees
        static const std::set<int> minorScale = {0, 2, 3, 5, 7, 8, 10};
        return minorScale.find(interval) == minorScale.end();
    }
    else
    {
        // Major scale degrees
        static const std::set<int> majorScale = {0, 2, 4, 5, 7, 9, 11};
        return majorScale.find(interval) == majorScale.end();
    }
}

juce::String ChordAnalyzer::getAccidental(int chordRoot) const
{
    int interval = (chordRoot - keyRoot + 12) % 12;

    if (minorKey)
    {
        // Check for alterations relative to natural minor
        if (interval == 1) return "b";   // b2
        if (interval == 4) return "#";   // #3 (major third)
        if (interval == 6) return "#";   // #4
        if (interval == 9) return "#";   // #6
        if (interval == 11) return "#";  // #7
    }
    else
    {
        // Check for alterations relative to major
        if (interval == 1) return "b";   // b2
        if (interval == 3) return "b";   // b3
        if (interval == 6) return "#";   // #4
        if (interval == 8) return "b";   // b6
        if (interval == 10) return "b";  // b7
    }

    return "";
}

juce::String ChordAnalyzer::degreeToRoman(int degree, bool uppercase) const
{
    static const juce::String upperNumerals[] = {"I", "II", "III", "IV", "V", "VI", "VII"};
    static const juce::String lowerNumerals[] = {"i", "ii", "iii", "iv", "v", "vi", "vii"};

    if (degree < 1 || degree > 7) return "?";

    return uppercase ? upperNumerals[degree - 1] : lowerNumerals[degree - 1];
}

juce::String ChordAnalyzer::buildRomanNumeral(int chordRoot, ChordQuality quality) const
{
    int degree = getScaleDegree(chordRoot);
    juce::String accidental = getAccidental(chordRoot);

    // Determine if uppercase (major quality) or lowercase (minor quality)
    bool uppercase = true;
    switch (quality)
    {
        case ChordQuality::Minor:
        case ChordQuality::Minor7:
        case ChordQuality::Minor6:
        case ChordQuality::Minor9:
        case ChordQuality::Minor11:
        case ChordQuality::Minor13:
        case ChordQuality::MinorMajor7:
        case ChordQuality::Diminished:
        case ChordQuality::Diminished7:
        case ChordQuality::HalfDiminished7:
            uppercase = false;
            break;
        default:
            uppercase = true;
            break;
    }

    juce::String numeral = accidental + degreeToRoman(degree, uppercase);

    // Add quality suffix
    switch (quality)
    {
        case ChordQuality::Diminished:
            numeral += juce::String::fromUTF8("\xC2\xB0");  // degree sign
            break;
        case ChordQuality::Augmented:
            numeral += "+";
            break;
        case ChordQuality::Dominant7:
            numeral += "7";
            break;
        case ChordQuality::Major7:
            numeral += "M7";
            break;
        case ChordQuality::Minor7:
            numeral += "7";
            break;
        case ChordQuality::HalfDiminished7:
            numeral += juce::String::fromUTF8("\xC3\xB8") + "7";  // slashed o
            break;
        case ChordQuality::Diminished7:
            numeral += juce::String::fromUTF8("\xC2\xB0") + "7";
            break;
        case ChordQuality::Sus2:
            numeral += "sus2";
            break;
        case ChordQuality::Sus4:
            numeral += "sus4";
            break;
        case ChordQuality::Major9:
        case ChordQuality::Minor9:
        case ChordQuality::Dominant9:
            numeral += "9";
            break;
        case ChordQuality::Major11:
        case ChordQuality::Minor11:
        case ChordQuality::Dominant11:
            numeral += "11";
            break;
        case ChordQuality::Major13:
        case ChordQuality::Minor13:
        case ChordQuality::Dominant13:
            numeral += "13";
            break;
        default:
            break;
    }

    return numeral;
}

//==============================================================================
HarmonicFunction ChordAnalyzer::getHarmonicFunction(int chordRoot, ChordQuality quality) const
{
    if (isChromatic(chordRoot))
    {
        // Check for borrowed chords
        int interval = (chordRoot - keyRoot + 12) % 12;
        if (interval == 10)  // bVII
            return HarmonicFunction::Borrowed;
        if (interval == 8)   // bVI
            return HarmonicFunction::Borrowed;
        if (interval == 3 && !minorKey)  // bIII in major
            return HarmonicFunction::Borrowed;

        return HarmonicFunction::Chromatic;
    }

    int degree = getScaleDegree(chordRoot);

    // Check for secondary dominants
    if (quality == ChordQuality::Dominant7 || quality == ChordQuality::Major)
    {
        if (degree == 2 || degree == 3 || degree == 6)
        {
            // These could be secondary dominants
            return HarmonicFunction::SecondaryDom;
        }
    }

    switch (degree)
    {
        case 1:  // I
        case 6:  // vi
            return HarmonicFunction::Tonic;
        case 3:  // iii
            return HarmonicFunction::Tonic;
        case 2:  // ii
        case 4:  // IV
            return HarmonicFunction::Subdominant;
        case 5:  // V
        case 7:  // vii
            return HarmonicFunction::Dominant;
        default:
            return HarmonicFunction::Unknown;
    }
}

//==============================================================================
juce::String ChordAnalyzer::getRootNameInKey(int degree) const
{
    // Calculate the pitch class for this scale degree
    static const int majorIntervals[] = {0, 2, 4, 5, 7, 9, 11};
    static const int minorIntervals[] = {0, 2, 3, 5, 7, 8, 10};

    if (degree < 1 || degree > 7) return "?";

    int interval = minorKey ? minorIntervals[degree - 1] : majorIntervals[degree - 1];
    int pitchClass = (keyRoot + interval) % 12;

    // Use the key-aware spelling method
    return getSpellingForKey(pitchClass);
}

juce::String ChordAnalyzer::getSpellingForKey(int pitchClass) const
{
    // Determine correct enharmonic spelling based on key signature
    // This ensures scale degrees use the musically correct note names
    //
    // Flat keys (use flats for accidentals): F, Bb, Eb, Ab, Db, Gb/Cb
    // Sharp keys (use sharps for accidentals): G, D, A, E, B, F#/C#
    // C major/A minor: convention uses sharps for accidentals

    // Define the correct note names for each key (major keys)
    // Format: array of 12 note names indexed by pitch class
    static const char* keySpellings[12][12] = {
        // C major: C D E F G A B (no accidentals, sharps for chromatic)
        {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"},
        // Db major: Db Eb F Gb Ab Bb C (5 flats)
        {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "Cb"},
        // D major: D E F# G A B C# (2 sharps)
        {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"},
        // Eb major: Eb F G Ab Bb C D (3 flats)
        {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"},
        // E major: E F# G# A B C# D# (4 sharps)
        {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"},
        // F major: F G A Bb C D E (1 flat)
        {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"},
        // Gb major: Gb Ab Bb Cb Db Eb F (6 flats)
        {"C", "Db", "D", "Eb", "Fb", "F", "Gb", "G", "Ab", "A", "Bb", "Cb"},
        // G major: G A B C D E F# (1 sharp)
        {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"},
        // Ab major: Ab Bb C Db Eb F G (4 flats)
        {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"},
        // A major: A B C# D E F# G# (3 sharps)
        {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"},
        // Bb major: Bb C D Eb F G A (2 flats)
        {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"},
        // B major: B C# D# E F# G# A# (5 sharps)
        {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}
    };

    pitchClass = pitchClass % 12;
    if (pitchClass < 0) pitchClass += 12;

    int keyIndex = keyRoot % 12;
    return keySpellings[keyIndex][pitchClass];
}

std::vector<ChordSuggestion> ChordAnalyzer::getSuggestions(const ChordInfo& currentChord,
                                                            SuggestionCategory maxLevel) const
{
    std::vector<ChordSuggestion> suggestions;

    if (!currentChord.isValid || currentChord.rootNote < 0)
        return suggestions;

    int currentDegree = getScaleDegree(currentChord.rootNote);

    // Always add basic suggestions
    addBasicSuggestions(suggestions, currentDegree, currentChord.quality);

    if (maxLevel >= SuggestionCategory::Intermediate)
        addIntermediateSuggestions(suggestions, currentDegree, currentChord.quality);

    if (maxLevel >= SuggestionCategory::Advanced)
        addAdvancedSuggestions(suggestions, currentDegree, currentChord.quality);

    return suggestions;
}

void ChordAnalyzer::addBasicSuggestions(std::vector<ChordSuggestion>& suggestions,
                                         int currentDegree, ChordQuality /*quality*/) const
{
    // Common progressions based on current chord
    auto addSuggestion = [&](int degree, const juce::String& roman, ChordQuality q,
                             const juce::String& reason, float commonality)
    {
        ChordSuggestion s;
        s.romanNumeral = roman;
        s.chordName = getRootNameInKey(degree) + qualityToSuffix(q);
        s.category = SuggestionCategory::Basic;
        s.reason = reason;
        s.commonality = commonality;
        suggestions.push_back(s);
    };

    if (minorKey)
    {
        // Minor key progressions
        switch (currentDegree)
        {
            case 1:  // i -> iv, V, VII, III
                addSuggestion(4, "iv", ChordQuality::Minor, "Classic i-iv motion", 0.9f);
                addSuggestion(5, "V", ChordQuality::Major, "Dominant resolution setup", 0.95f);
                addSuggestion(7, "VII", ChordQuality::Major, "Subtonic chord", 0.7f);
                break;
            case 2:  // ii° -> V, i
                addSuggestion(5, "V", ChordQuality::Major, "ii-V progression", 0.9f);
                addSuggestion(1, "i", ChordQuality::Minor, "Return to tonic", 0.7f);
                break;
            case 3:  // III -> VI, iv
                addSuggestion(6, "VI", ChordQuality::Major, "Relative motion", 0.8f);
                addSuggestion(4, "iv", ChordQuality::Minor, "Subdominant function", 0.7f);
                break;
            case 4:  // iv -> V, i, VII
                addSuggestion(5, "V", ChordQuality::Major, "Subdominant to dominant", 0.9f);
                addSuggestion(1, "i", ChordQuality::Minor, "Plagal motion", 0.8f);
                addSuggestion(7, "VII", ChordQuality::Major, "Subtonic approach", 0.6f);
                break;
            case 5:  // V -> i, VI
                addSuggestion(1, "i", ChordQuality::Minor, "Perfect cadence", 1.0f);
                addSuggestion(6, "VI", ChordQuality::Major, "Deceptive cadence", 0.7f);
                break;
            case 6:  // VI -> VII, III, iv
                addSuggestion(7, "VII", ChordQuality::Major, "Step up to subtonic", 0.8f);
                addSuggestion(3, "III", ChordQuality::Major, "Mediant motion", 0.6f);
                break;
            case 7:  // VII -> III, i
                addSuggestion(3, "III", ChordQuality::Major, "Resolve up by fifth", 0.8f);
                addSuggestion(1, "i", ChordQuality::Minor, "Return to tonic", 0.9f);
                break;
        }
    }
    else
    {
        // Major key progressions
        switch (currentDegree)
        {
            case 1:  // I -> IV, V, vi, ii
                addSuggestion(4, "IV", ChordQuality::Major, "Classic I-IV motion", 0.9f);
                addSuggestion(5, "V", ChordQuality::Major, "Dominant preparation", 0.95f);
                addSuggestion(6, "vi", ChordQuality::Minor, "Relative minor", 0.8f);
                break;
            case 2:  // ii -> V, vii°
                addSuggestion(5, "V", ChordQuality::Major, "Classic ii-V", 0.95f);
                addSuggestion(7, "vii°", ChordQuality::Diminished, "Leading tone chord", 0.5f);
                break;
            case 3:  // iii -> vi, IV
                addSuggestion(6, "vi", ChordQuality::Minor, "Descending thirds", 0.8f);
                addSuggestion(4, "IV", ChordQuality::Major, "Subdominant function", 0.7f);
                break;
            case 4:  // IV -> V, I, ii
                addSuggestion(5, "V", ChordQuality::Major, "Subdominant to dominant", 0.95f);
                addSuggestion(1, "I", ChordQuality::Major, "Plagal cadence", 0.8f);
                addSuggestion(2, "ii", ChordQuality::Minor, "Subdominant variation", 0.6f);
                break;
            case 5:  // V -> I, vi
                addSuggestion(1, "I", ChordQuality::Major, "Perfect cadence", 1.0f);
                addSuggestion(6, "vi", ChordQuality::Minor, "Deceptive cadence", 0.7f);
                break;
            case 6:  // vi -> IV, ii, V
                addSuggestion(4, "IV", ChordQuality::Major, "Common pop progression", 0.9f);
                addSuggestion(2, "ii", ChordQuality::Minor, "Subdominant motion", 0.8f);
                addSuggestion(5, "V", ChordQuality::Major, "Skip to dominant", 0.6f);
                break;
            case 7:  // vii° -> I, iii
                addSuggestion(1, "I", ChordQuality::Major, "Resolve to tonic", 0.95f);
                addSuggestion(3, "iii", ChordQuality::Minor, "Resolve to mediant", 0.5f);
                break;
        }
    }
}

void ChordAnalyzer::addIntermediateSuggestions(std::vector<ChordSuggestion>& suggestions,
                                                int currentDegree, ChordQuality quality) const
{
    auto addSuggestion = [&](const juce::String& roman, const juce::String& name,
                             const juce::String& reason, float commonality)
    {
        ChordSuggestion s;
        s.romanNumeral = roman;
        s.chordName = name;
        s.category = SuggestionCategory::Intermediate;
        s.reason = reason;
        s.commonality = commonality;
        suggestions.push_back(s);
    };

    // Secondary dominants
    if (currentDegree == 1)
    {
        // V/V (secondary dominant of V)
        int vOfV = (keyRoot + 2) % 12;  // D in C major
        addSuggestion("V/V", pitchClassToName(vOfV) + "7",
                      "Secondary dominant to V", 0.7f);
    }

    if (currentDegree == 2 || currentDegree == 5)
    {
        // V/vi (secondary dominant of vi)
        int vOfVi = (keyRoot + 4) % 12;  // E in C major
        addSuggestion("V/vi", pitchClassToName(vOfVi) + "7",
                      "Secondary dominant to vi", 0.6f);
    }

    // Borrowed chords (modal interchange)
    if (!minorKey)
    {
        // bVII from mixolydian - use flats for flat numeral
        int bVII = (keyRoot + 10) % 12;
        addSuggestion("bVII", pitchClassToName(bVII, true),
                      "Borrowed from parallel minor", 0.65f);

        // iv from parallel minor
        int iv = (keyRoot + 5) % 12;
        addSuggestion("iv", pitchClassToName(iv) + "m",
                      "Minor iv from parallel", 0.6f);
    }
    else
    {
        // IV from parallel major (Picardy motion)
        int IV = (keyRoot + 5) % 12;
        addSuggestion("IV", pitchClassToName(IV),
                      "Borrowed from parallel major", 0.6f);
    }

    // Applied chords based on quality
    if (quality == ChordQuality::Dominant7)
    {
        // Tritone substitution target - use flats for flat numeral
        int tritone = (keyRoot + 6) % 12;
        addSuggestion("bII7", pitchClassToName(tritone, true) + "7",
                      "Tritone substitution", 0.5f);
    }
}

void ChordAnalyzer::addAdvancedSuggestions(std::vector<ChordSuggestion>& suggestions,
                                            int currentDegree, ChordQuality /*quality*/) const
{
    auto addSuggestion = [&](const juce::String& roman, const juce::String& name,
                             const juce::String& reason, float commonality)
    {
        ChordSuggestion s;
        s.romanNumeral = roman;
        s.chordName = name;
        s.category = SuggestionCategory::Advanced;
        s.reason = reason;
        s.commonality = commonality;
        suggestions.push_back(s);
    };

    // Chromatic mediants
    if (currentDegree == 1)
    {
        // bVI (chromatic mediant) - use flats for flat numeral
        int bVI = (keyRoot + 8) % 12;
        addSuggestion("bVI", pitchClassToName(bVI, true),
                      "Chromatic mediant - dramatic shift", 0.4f);

        // bIII (chromatic mediant) - use flats for flat numeral
        int bIII = (keyRoot + 3) % 12;
        addSuggestion("bIII", pitchClassToName(bIII, true),
                      "Chromatic mediant - upward", 0.35f);
    }

    // Neapolitan
    if (currentDegree == 4 || currentDegree == 2)
    {
        // Neapolitan chord - use flats for flat numeral
        int neapolitan = (keyRoot + 1) % 12;
        addSuggestion("bII", pitchClassToName(neapolitan, true),
                      "Neapolitan chord - pre-dominant", 0.4f);
    }

    // Augmented 6th approach
    if (currentDegree == 5)
    {
        // Italian augmented 6th - built on b6 scale degree
        // In C major: Ab-C-F# (resolves to G)
        int flatSix = (keyRoot + 8) % 12;
        addSuggestion("It+6", pitchClassToName(flatSix, true) + " It+6",
                      "Italian augmented 6th - chromatic approach", 0.3f);
    }

    // Coltrane changes suggestion
    if (currentDegree == 1)
    {
        // bVI maj7 - use flats for flat numeral
        int majThirdDown = (keyRoot + 8) % 12;  // Ab in C
        addSuggestion("bVI maj7", pitchClassToName(majThirdDown, true) + "maj7",
                      "Coltrane changes - major third cycle", 0.25f);
    }
}

//==============================================================================
// Static utility functions
juce::String ChordAnalyzer::noteToName(int midiNote, bool useFlats)
{
    int pitchClass = midiNote % 12;
    int octave = (midiNote / 12) - 1;
    return pitchClassToName(pitchClass, useFlats) + juce::String(octave);
}

juce::String ChordAnalyzer::pitchClassToName(int pitchClass, bool useFlats)
{
    static const char* sharpNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char* flatNames[] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};

    pitchClass = pitchClass % 12;
    if (pitchClass < 0) pitchClass += 12;

    return useFlats ? flatNames[pitchClass] : sharpNames[pitchClass];
}

int ChordAnalyzer::nameToNote(const juce::String& name)
{
    juce::String upper = name.toUpperCase().trim();
    if (upper.isEmpty()) return -1;

    int base = -1;
    switch (upper[0])
    {
        case 'C': base = 0; break;
        case 'D': base = 2; break;
        case 'E': base = 4; break;
        case 'F': base = 5; break;
        case 'G': base = 7; break;
        case 'A': base = 9; break;
        case 'B': base = 11; break;
        default: return -1;
    }

    // Check for accidentals
    if (upper.length() > 1)
    {
        if (upper[1] == '#' || upper[1] == 'S') base++;
        else if (upper[1] == 'B' || upper[1] == 'b') base--;
    }

    return (base + 12) % 12;
}

juce::String ChordAnalyzer::qualityToString(ChordQuality quality)
{
    switch (quality)
    {
        case ChordQuality::Major: return "Major";
        case ChordQuality::Minor: return "Minor";
        case ChordQuality::Diminished: return "Diminished";
        case ChordQuality::Augmented: return "Augmented";
        case ChordQuality::Dominant7: return "Dominant 7th";
        case ChordQuality::Major7: return "Major 7th";
        case ChordQuality::Minor7: return "Minor 7th";
        case ChordQuality::MinorMajor7: return "Minor-Major 7th";
        case ChordQuality::Diminished7: return "Diminished 7th";
        case ChordQuality::HalfDiminished7: return "Half-Diminished 7th";
        case ChordQuality::Augmented7: return "Augmented 7th";
        case ChordQuality::AugmentedMajor7: return "Augmented Major 7th";
        case ChordQuality::Sus2: return "Suspended 2nd";
        case ChordQuality::Sus4: return "Suspended 4th";
        case ChordQuality::Dominant7Sus4: return "Dominant 7th Sus4";
        case ChordQuality::Add9: return "Add 9";
        case ChordQuality::Add11: return "Add 11";
        case ChordQuality::Major6: return "Major 6th";
        case ChordQuality::Minor6: return "Minor 6th";
        case ChordQuality::Major9: return "Major 9th";
        case ChordQuality::Minor9: return "Minor 9th";
        case ChordQuality::Dominant9: return "Dominant 9th";
        case ChordQuality::Major11: return "Major 11th";
        case ChordQuality::Minor11: return "Minor 11th";
        case ChordQuality::Dominant11: return "Dominant 11th";
        case ChordQuality::Major13: return "Major 13th";
        case ChordQuality::Minor13: return "Minor 13th";
        case ChordQuality::Dominant13: return "Dominant 13th";
        case ChordQuality::Power5: return "Power Chord";
        case ChordQuality::Dominant7Flat5: return "Dominant 7th Flat 5";
        case ChordQuality::Dominant7Sharp5: return "Dominant 7th Sharp 5";
        case ChordQuality::Dominant7Flat9: return "Dominant 7th Flat 9";
        case ChordQuality::Dominant7Sharp9: return "Dominant 7th Sharp 9";
        default: return "Unknown";
    }
}

juce::String ChordAnalyzer::qualityToSuffix(ChordQuality quality)
{
    switch (quality)
    {
        case ChordQuality::Major: return "";
        case ChordQuality::Minor: return "m";
        case ChordQuality::Diminished: return "dim";
        case ChordQuality::Augmented: return "aug";
        case ChordQuality::Dominant7: return "7";
        case ChordQuality::Major7: return "maj7";
        case ChordQuality::Minor7: return "m7";
        case ChordQuality::MinorMajor7: return "mMaj7";
        case ChordQuality::Diminished7: return "dim7";
        case ChordQuality::HalfDiminished7: return "m7b5";
        case ChordQuality::Augmented7: return "aug7";
        case ChordQuality::AugmentedMajor7: return "augMaj7";
        case ChordQuality::Sus2: return "sus2";
        case ChordQuality::Sus4: return "sus4";
        case ChordQuality::Dominant7Sus4: return "7sus4";
        case ChordQuality::Add9: return "add9";
        case ChordQuality::Add11: return "add11";
        case ChordQuality::Major6: return "6";
        case ChordQuality::Minor6: return "m6";
        case ChordQuality::Major9: return "maj9";
        case ChordQuality::Minor9: return "m9";
        case ChordQuality::Dominant9: return "9";
        case ChordQuality::Major11: return "maj11";
        case ChordQuality::Minor11: return "m11";
        case ChordQuality::Dominant11: return "11";
        case ChordQuality::Major13: return "maj13";
        case ChordQuality::Minor13: return "m13";
        case ChordQuality::Dominant13: return "13";
        case ChordQuality::Power5: return "5";
        case ChordQuality::Dominant7Flat5: return "7b5";
        case ChordQuality::Dominant7Sharp5: return "7#5";
        case ChordQuality::Dominant7Flat9: return "7b9";
        case ChordQuality::Dominant7Sharp9: return "7#9";
        default: return "?";
    }
}

juce::String ChordAnalyzer::functionToString(HarmonicFunction func)
{
    switch (func)
    {
        case HarmonicFunction::Tonic: return "Tonic";
        case HarmonicFunction::Subdominant: return "Subdominant";
        case HarmonicFunction::Dominant: return "Dominant";
        case HarmonicFunction::SecondaryDom: return "Secondary Dominant";
        case HarmonicFunction::Borrowed: return "Borrowed";
        case HarmonicFunction::Chromatic: return "Chromatic";
        default: return "Unknown";
    }
}
