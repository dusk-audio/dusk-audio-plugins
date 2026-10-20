// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#include "ChordAnalyzerCore.hpp"
#include <algorithm>
#include <cassert>

namespace duskaudio::chord
{
//==============================================================================
// Chord patterns - interval sets from root (in semitones)
// Priority determines which pattern wins when multiple match. Patterns that
// tie on both priority and size fall back to DECLARATION ORDER, so this list
// is ordered by preferred spelling: Major before Minor before Diminished, and
// the lower tension first (add9 before add11, so C E G D F reads Cadd9(11)).
// Reordering entries within a priority band changes chord names.
const std::vector<ChordAnalyzerCore::ChordPattern> ChordAnalyzerCore::chordPatterns = {
    // Power chord (2 notes)
    {{0, 7}, ChordQuality::Power5, 1},

    // Triads (3 notes)
    {{0, 4, 7}, ChordQuality::Major, 10},
    {{0, 3, 7}, ChordQuality::Minor, 10},
    {{0, 3, 6}, ChordQuality::Diminished, 10},
    {{0, 4, 8}, ChordQuality::Augmented, 10},
    {{0, 2, 7}, ChordQuality::Sus2, 9},
    {{0, 5, 7}, ChordQuality::Sus4, 9},

    // Sixth chords
    {{0, 4, 7, 9}, ChordQuality::Major6, 15},
    {{0, 3, 7, 9}, ChordQuality::Minor6, 15},

    // Seventh chords (4 notes)
    {{0, 4, 7, 11}, ChordQuality::Major7, 20},
    {{0, 4, 7, 10}, ChordQuality::Dominant7, 20},
    {{0, 3, 7, 10}, ChordQuality::Minor7, 20},
    {{0, 3, 7, 11}, ChordQuality::MinorMajor7, 20},
    {{0, 3, 6, 10}, ChordQuality::HalfDiminished7, 20},
    {{0, 3, 6, 9}, ChordQuality::Diminished7, 20},
    {{0, 4, 8, 10}, ChordQuality::Augmented7, 20},
    {{0, 4, 8, 11}, ChordQuality::AugmentedMajor7, 20},
    {{0, 5, 7, 10}, ChordQuality::Dominant7Sus4, 19},

    // Altered dominants
    {{0, 4, 6, 10}, ChordQuality::Dominant7Flat5, 21},
    {{0, 4, 8, 10}, ChordQuality::Dominant7Sharp5, 21},
    {{0, 1, 4, 7, 10}, ChordQuality::Dominant7Flat9, 25},
    {{0, 3, 4, 7, 10}, ChordQuality::Dominant7Sharp9, 25},

    // Add chords
    {{0, 4, 7, 14}, ChordQuality::Add9, 16},
    {{0, 4, 7, 17}, ChordQuality::Add11, 16},

    // Extended chords (5+ notes)
    {{0, 4, 7, 10, 14}, ChordQuality::Dominant9, 30},
    {{0, 4, 7, 11, 14}, ChordQuality::Major9, 30},
    {{0, 3, 7, 10, 14}, ChordQuality::Minor9, 30},

    {{0, 4, 7, 10, 14, 17}, ChordQuality::Dominant11, 35},
    {{0, 4, 7, 11, 14, 17}, ChordQuality::Major11, 35},
    {{0, 3, 7, 10, 14, 17}, ChordQuality::Minor11, 35},

    {{0, 4, 7, 10, 14, 17, 21}, ChordQuality::Dominant13, 40},
    {{0, 4, 7, 11, 14, 17, 21}, ChordQuality::Major13, 40},
    {{0, 3, 7, 10, 14, 17, 21}, ChordQuality::Minor13, 40},

    // Simplified extended chords (without all tensions)
    {{0, 4, 7, 10, 21}, ChordQuality::Dominant13, 28},  // 13 without 9/11
    {{0, 4, 7, 10, 17}, ChordQuality::Dominant11, 28},  // 11 without 9

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
    //
    // Inside the block, a four-tone shape has to outrank a three-tone one
    // ROOTED ON THE BASS, because findRoot hands the bass candidate five
    // points and charges half a point for each sounding tone the shape does
    // not explain. A three-tone shape on the bass with one tone left over
    // therefore scores its priority + 4.5, so at 7 the four-tone shapes were
    // half a point short of add9(no5) at 3: Bb C E D over the Bb read as
    // A#add9(no5,#11), the Bb owning the name and the E demoted to a #11,
    // rather than as the C9 shell it is (issue #277).
    //
    // 8 clears add9(no5). It does not clear the three-tone shells at 5, which
    // would need 10, and that is deliberate: at 10 these shapes tie the triads
    // and beat one at another root, which is the failure the paragraph above
    // describes. Chromatic clusters that pit a shell on the bass against a
    // four-tone shape elsewhere therefore still take the shell.
    {{0, 4, 14},         ChordQuality::Add9,   3, true},
    {{0, 4, 10},         ChordQuality::Dominant7,      5, true},
    {{0, 4, 11},         ChordQuality::Major7,   5, true},
    {{0, 3, 10},         ChordQuality::Minor7,     5, true},
    {{0, 3, 11},         ChordQuality::MinorMajor7,  5, true},
    {{0, 4, 10, 14},     ChordQuality::Dominant9,      8, true},
    {{0, 4, 11, 14},     ChordQuality::Major9,   8, true},
    {{0, 3, 10, 14},     ChordQuality::Minor9,     8, true},
};

namespace
{
    // Inversion ordinal of one pattern tone: which degree of the stacked-thirds
    // spelling it is (see kInversionSlashBass in ChordAnalyzer.h). The interval
    // on its own does not say - 3 semitones is the minor third of an m7 and the
    // #9 of a 7#9, 9 semitones is the sixth of a 6 chord and the diminished
    // seventh of a dim7 - so the whole pattern being described is passed in.
    //
    // The compound spellings (14, 17, 21) fold to the same ordinal as their
    // simple forms, so a 9th in the bass numbers the same whether the pattern
    // states it as 2 (sus2) or as 14 (every extended chord).
    //
    // Returns -1 for an interval no pattern is allowed to contain;
    // buildPatternMasks asserts on it rather than letting it pass silently.
    int degreeOrdinalOf(int interval, const std::set<int>& patternIntervals)
    {
        switch (interval)
        {
            case 0:  return 0;                        // root
            case 1:  return 4;                        // b9
            case 2:  return 4;                        // 9, stated simple by sus2
            case 3:  // minor third, unless a major third is also spelled: then #9
                return patternIntervals.count(4) != 0 ? 4 : 1;
            case 4:  return 1;                        // major third
            case 5:  return 5;                        // 11th, stated simple by sus4
            case 6:  return 2;                        // b5. No pattern spells a #11 here
            case 7:  return 2;                        // perfect fifth
            case 8:  return 2;                        // #5. No pattern spells a b13 here
            case 9:  // diminished seventh of a fully diminished chord, else the 6th
                return (patternIntervals.count(3) != 0 && patternIntervals.count(6) != 0)
                           ? 3 : 6;
            case 10: return 3;                        // minor seventh
            case 11: return 3;                        // major seventh
            case 14: return 4;                        // 9th
            case 17: return 5;                        // 11th
            case 21: return 6;                        // 13th
            default: return -1;
        }
    }
}

ChordFacts ChordAnalyzerCore::analyzeFacts(const int* midiNotes, int numNotes) const noexcept
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
    facts.inversion = calculateInversion(facts.bassNote, facts.rootNote, facts.patternIndex);
    facts.slashBass = (facts.bassNote != facts.rootNote);
    facts.confidence = calculateConfidence(facts.patternIndex, facts.intervals);
    facts.isValid = true;

    return facts;
}

void ChordAnalyzerCore::setKey(int rootNote, bool isMinor)
{
    keyRoot = rootNote % 12;
    minorKey = isMinor;
}

std::vector<ChordAnalyzerCore::PatternMask> ChordAnalyzerCore::buildPatternMasks()
{
    std::vector<PatternMask> masks;
    masks.reserve(chordPatterns.size());

    for (const auto& pattern : chordPatterns)
    {
        PatternMask m { 0u, 0u, 0, { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 } };

        for (int interval : pattern.intervals)
        {
            m.intervals |= (1u << interval);

            const int pitchClass = interval % 12;
            m.pitches = static_cast<std::uint16_t>(m.pitches | (1u << pitchClass));

            const int degree = degreeOrdinalOf(interval, pattern.intervals);

            // Every tone of every pattern has to number, and no pattern may
            // spell one pitch class as two different degrees - the ordinal is
            // looked up by pitch class, so a collision would have no answer.
            assert(degree >= 0);
            assert(m.degrees[pitchClass] < 0 || m.degrees[pitchClass] == degree);

            if (degree >= 0)
                m.degrees[pitchClass] = static_cast<std::int8_t>(degree);
        }

        m.pitchCount = countPitchClasses(m.pitches);
        masks.push_back(m);
    }

    return masks;
}

int ChordAnalyzerCore::countPitchClasses(std::uint16_t mask) noexcept
{
    int n = 0;
    for (; mask != 0; mask &= static_cast<std::uint16_t>(mask - 1))
        ++n;
    return n;
}

std::uint32_t ChordAnalyzerCore::intervalsFrom(std::uint16_t pitchMask, int root) noexcept
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

bool ChordAnalyzerCore::patternMatches(int patternIndex, std::uint32_t intervals) noexcept
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

int ChordAnalyzerCore::findRoot(std::uint16_t pitchMask, int bassPitch) noexcept
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

            // Bonus for bass note being the root. The no-fifth block at the
            // top of chordPatterns is spaced against this five, so read the
            // note there before changing it.
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

int ChordAnalyzerCore::matchPattern(std::uint32_t intervals) noexcept
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

int ChordAnalyzerCore::calculateInversion(int bassPitch, int root, int patternIndex) noexcept
{
    if (bassPitch == root)
        return 0;

    // Nothing matched, so there is no spelling to number the bass against.
    if (patternIndex < 0 || patternIndex >= static_cast<int>(kPatternMasks.size()))
        return 0;

    const int bassInterval = ((bassPitch - root) + 12) % 12;
    const int degree = kPatternMasks[static_cast<size_t>(patternIndex)].degrees[bassInterval];

    // A bass the pattern does not spell is an added tension or a foreign bass,
    // not an inversion: C E G under an F# is Cadd#11/F#, and calling that root
    // position (which every non-chord-tone bass used to report) told the host
    // the exact opposite of what is sounding. Issue #273.
    return degree >= 0 ? degree : kInversionSlashBass;
}

std::uint16_t ChordAnalyzerCore::patternPitchClasses(int patternIndex) noexcept
{
    if (patternIndex < 0 || patternIndex >= static_cast<int>(kPatternMasks.size()))
        return 0;

    return kPatternMasks[static_cast<size_t>(patternIndex)].pitches;
}

float ChordAnalyzerCore::calculateConfidence(int patternIndex, std::uint32_t intervals) noexcept
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

    return std::clamp(patternMatch - penalty, 0.0f, 1.0f);
}

int ChordAnalyzerCore::getScaleDegree(int chordRoot) const noexcept
{
    const int interval = (((chordRoot - keyRoot) % 12) + 12) % 12;

    // Degree of each interval, 0 for an interval the scale does not contain.
    // Natural minor is 0, 2, 3, 5, 7, 8, 10; major is 0, 2, 4, 5, 7, 9, 11.
    // constexpr arrays rather than the function-local std::maps these were:
    // getSuggestionFacts runs on the audio thread in the headless LV2, and a
    // function-local static container allocates and takes a guard lock the
    // first time it is touched.
    static constexpr int minorDegrees[12] = { 1, 0, 2, 3, 0, 4, 0, 5, 6, 0, 7, 0 };
    static constexpr int majorDegrees[12] = { 1, 0, 2, 0, 3, 4, 0, 5, 0, 6, 0, 7 };

    const int degree = minorKey ? minorDegrees[interval] : majorDegrees[interval];
    if (degree != 0)
        return degree;

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

bool ChordAnalyzerCore::isChromatic(int chordRoot) const noexcept
{
    const int interval = (((chordRoot - keyRoot) % 12) + 12) % 12;

    // One bit per scale degree, for the same reason getScaleDegree above uses
    // a plain array: no static container to build on first use.
    constexpr unsigned int minorScale = (1u << 0) | (1u << 2) | (1u << 3) | (1u << 5)
                                      | (1u << 7) | (1u << 8) | (1u << 10);
    constexpr unsigned int majorScale = (1u << 0) | (1u << 2) | (1u << 4) | (1u << 5)
                                      | (1u << 7) | (1u << 9) | (1u << 11);

    return (((minorKey ? minorScale : majorScale) >> interval) & 1u) == 0u;
}

HarmonicFunction ChordAnalyzerCore::getHarmonicFunction(int chordRoot, ChordQuality quality) const
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

int ChordAnalyzerCore::rootPitchClassForDegree(int degree) const noexcept
{
    // Pitch class of a scale degree in the current key.
    static constexpr int majorIntervals[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static constexpr int minorIntervals[7] = { 0, 2, 3, 5, 7, 8, 10 };

    if (degree < 1 || degree > 7)
        return keyRoot % 12;

    const int interval = minorKey ? minorIntervals[degree - 1] : majorIntervals[degree - 1];
    return (keyRoot + interval) % 12;
}

int ChordAnalyzerCore::emitSeed(SuggestionSeed* out, int count, int maxOut,
                            const SuggestionSeed& seed) noexcept
{
    if (count >= maxOut)
        return count;               // caller sized the buffer; drop, never write past it

    out[count] = seed;
    return count + 1;
}

int ChordAnalyzerCore::collectSuggestions(SuggestionSeed* out, int maxOut, int currentDegree,
                                      ChordQuality quality, SuggestionCategory maxLevel) const noexcept
{
    int count = addBasicSuggestions(out, 0, maxOut, currentDegree, quality);

    if (maxLevel >= SuggestionCategory::Intermediate)
        count = addIntermediateSuggestions(out, count, maxOut, currentDegree, quality);

    if (maxLevel >= SuggestionCategory::Advanced)
        count = addAdvancedSuggestions(out, count, maxOut, currentDegree, quality);

    return count;
}

int ChordAnalyzerCore::getSuggestionFacts(const ChordFacts& currentChord,
                                      SuggestionCategory maxLevel,
                                      SuggestionFacts* out,
                                      int maxOut) const noexcept
{
    if (out == nullptr || maxOut <= 0)
        return 0;

    if (!currentChord.isValid || currentChord.rootNote < 0)
        return 0;

    // On the stack, not the heap: this runs on the audio thread in the
    // headless LV2 wrapper.
    SuggestionSeed seeds[maxSuggestions];
    const int count = collectSuggestions(seeds, maxSuggestions,
                                         getScaleDegree(currentChord.rootNote),
                                         currentChord.quality, maxLevel);

    const int written = count < maxOut ? count : maxOut;

    for (int i = 0; i < written; ++i)
    {
        out[i].rootNote    = seeds[i].rootNote;
        out[i].quality     = seeds[i].quality;
        out[i].category    = seeds[i].category;
        out[i].commonality = seeds[i].commonality;
    }

    return written;
}

int ChordAnalyzerCore::addBasicSuggestions(SuggestionSeed* out, int count, int maxOut,
                                        int currentDegree, ChordQuality /*quality*/) const noexcept
{
    // Common progressions based on current chord. The root is a scale degree,
    // so it is spelled by the key signature.
    auto addSuggestion = [&](int degree, const char* roman, ChordQuality q,
                             const char* reason, float commonality)
    {
        count = emitSeed(out, count, maxOut,
                         { rootPitchClassForDegree(degree), q, SuggestionCategory::Basic,
                           commonality, roman, reason, RootSpelling::KeyAware, nullptr });
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

    return count;
}

int ChordAnalyzerCore::addIntermediateSuggestions(SuggestionSeed* out, int count, int maxOut,
                                               int currentDegree, ChordQuality quality) const noexcept
{
    auto addSuggestion = [&](const char* roman, int rootNote, ChordQuality q,
                             RootSpelling spelling, const char* reason, float commonality)
    {
        count = emitSeed(out, count, maxOut,
                         { rootNote, q, SuggestionCategory::Intermediate,
                           commonality, roman, reason, spelling, nullptr });
    };

    // Secondary dominants
    if (currentDegree == 1)
    {
        // V/V (secondary dominant of V)
        addSuggestion("V/V", (keyRoot + 2) % 12, ChordQuality::Dominant7, RootSpelling::Sharp,
                      "Secondary dominant to V", 0.7f);
    }

    if (currentDegree == 2 || currentDegree == 5)
    {
        // V/vi (secondary dominant of vi)
        addSuggestion("V/vi", (keyRoot + 4) % 12, ChordQuality::Dominant7, RootSpelling::Sharp,
                      "Secondary dominant to vi", 0.6f);
    }

    // Borrowed chords (modal interchange)
    if (!minorKey)
    {
        // bVII from mixolydian - use flats for flat numeral
        addSuggestion("bVII", (keyRoot + 10) % 12, ChordQuality::Major, RootSpelling::Flat,
                      "Borrowed from parallel minor", 0.65f);

        // iv from parallel minor
        addSuggestion("iv", (keyRoot + 5) % 12, ChordQuality::Minor, RootSpelling::Sharp,
                      "Minor iv from parallel", 0.6f);
    }
    else
    {
        // IV from parallel major (Picardy motion)
        addSuggestion("IV", (keyRoot + 5) % 12, ChordQuality::Major, RootSpelling::Sharp,
                      "Borrowed from parallel major", 0.6f);
    }

    // Applied chords based on quality
    if (quality == ChordQuality::Dominant7)
    {
        // Tritone substitution target - use flats for flat numeral
        addSuggestion("bII7", (keyRoot + 6) % 12, ChordQuality::Dominant7, RootSpelling::Flat,
                      "Tritone substitution", 0.5f);
    }

    return count;
}

int ChordAnalyzerCore::addAdvancedSuggestions(SuggestionSeed* out, int count, int maxOut,
                                           int currentDegree, ChordQuality /*quality*/) const noexcept
{
    auto addSuggestion = [&](const char* roman, int rootNote, ChordQuality q,
                             const char* nameSuffix, const char* reason, float commonality)
    {
        // Every advanced suggestion is spelled with flats: they are all named
        // by a flat Roman numeral.
        count = emitSeed(out, count, maxOut,
                         { rootNote, q, SuggestionCategory::Advanced,
                           commonality, roman, reason, RootSpelling::Flat, nameSuffix });
    };

    // Chromatic mediants
    if (currentDegree == 1)
    {
        // bVI (chromatic mediant)
        addSuggestion("bVI", (keyRoot + 8) % 12, ChordQuality::Major, nullptr,
                      "Chromatic mediant - dramatic shift", 0.4f);

        // bIII (chromatic mediant)
        addSuggestion("bIII", (keyRoot + 3) % 12, ChordQuality::Major, nullptr,
                      "Chromatic mediant - upward", 0.35f);
    }

    // Neapolitan
    if (currentDegree == 4 || currentDegree == 2)
    {
        addSuggestion("bII", (keyRoot + 1) % 12, ChordQuality::Major, nullptr,
                      "Neapolitan chord - pre-dominant", 0.4f);
    }

    // Augmented 6th approach
    if (currentDegree == 5)
    {
        // Italian augmented 6th - built on b6 scale degree. In C major that is
        // Ab-C-F#, which is no quality in the table, so the name carries the
        // tag and the quality stays Unknown: a host port can still show the
        // root, and shows "-" where a quality would go.
        addSuggestion("It+6", (keyRoot + 8) % 12, ChordQuality::Unknown, " It+6",
                      "Italian augmented 6th - chromatic approach", 0.3f);
    }

    // Coltrane changes suggestion
    if (currentDegree == 1)
    {
        addSuggestion("bVI maj7", (keyRoot + 8) % 12, ChordQuality::Major7, nullptr,
                      "Coltrane changes - major third cycle", 0.25f);
    }

    return count;
}

// Built once at static-init time, after chordPatterns, so no analysis call
// ever has to allocate this table.
const std::vector<ChordAnalyzerCore::PatternMask> ChordAnalyzerCore::kPatternMasks
    = ChordAnalyzerCore::buildPatternMasks();
} // namespace duskaudio::chord
