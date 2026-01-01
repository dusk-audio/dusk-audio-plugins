#include "ChordAnalyzer.h"
#include <algorithm>
#include <cmath>

//==============================================================================
// Chord patterns - interval sets from root (in semitones)
// Priority determines which pattern wins when multiple match
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
    {{0, 4, 7, 10, 13}, ChordQuality::Dominant7Flat9, "7b9", 25},
    {{0, 4, 7, 10, 15}, ChordQuality::Dominant7Sharp9, "7#9", 25},

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
};

//==============================================================================
ChordAnalyzer::ChordAnalyzer()
{
}

//==============================================================================
ChordInfo ChordAnalyzer::analyze(const std::vector<int>& midiNotes)
{
    ChordInfo result;
    result.midiNotes = midiNotes;

    if (midiNotes.empty())
    {
        result.name = "-";
        result.romanNumeral = "-";
        return result;
    }

    if (midiNotes.size() == 1)
    {
        // Single note - just show the note name
        result.name = noteToName(midiNotes[0]);
        result.romanNumeral = "-";
        result.rootNote = midiNotes[0] % 12;
        result.bassNote = result.rootNote;
        return result;
    }

    // Sort notes for consistent analysis
    std::vector<int> sortedNotes = midiNotes;
    std::sort(sortedNotes.begin(), sortedNotes.end());

    // Get bass note
    result.bassNote = sortedNotes[0] % 12;

    // Find the root
    result.rootNote = findRoot(sortedNotes);

    if (result.rootNote < 0)
    {
        result.name = "?";
        result.romanNumeral = "?";
        return result;
    }

    // Get intervals from root
    auto intervals = getIntervals(sortedNotes, result.rootNote);

    // Match pattern
    int priority = 0;
    result.quality = matchPattern(intervals, priority);

    if (result.quality == ChordQuality::Unknown)
    {
        // Try simpler patterns by removing some notes
        // Sometimes extra notes don't fit known patterns
        result.name = pitchClassToName(result.rootNote) + "?";
        result.romanNumeral = "?";
        result.confidence = 0.3f;
        return result;
    }

    // Build chord name
    result.name = pitchClassToName(result.rootNote) + qualityToSuffix(result.quality);

    // Calculate inversion
    result.inversion = calculateInversion(sortedNotes, result.rootNote);

    // Add inversion notation if not root position
    if (result.inversion > 0)
    {
        result.extensions = "/" + pitchClassToName(result.bassNote);
    }

    // Get Roman numeral and function
    result.romanNumeral = buildRomanNumeral(result.rootNote, result.quality);
    result.function = getHarmonicFunction(result.rootNote, result.quality);

    // Calculate confidence
    result.confidence = calculateConfidence(intervals, result.quality);
    result.isValid = true;

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
int ChordAnalyzer::findRoot(const std::vector<int>& notes) const
{
    if (notes.empty()) return -1;

    // Get unique pitch classes
    std::set<int> pitchClasses;
    for (int note : notes)
        pitchClasses.insert(note % 12);

    std::vector<int> uniquePitches(pitchClasses.begin(), pitchClasses.end());

    if (uniquePitches.size() < 2)
        return uniquePitches[0];

    // Try each note as potential root and score the result
    int bestRoot = uniquePitches[0];
    int bestPriority = -1;
    float bestScore = 0.0f;

    for (int candidateRoot : uniquePitches)
    {
        auto intervals = getIntervals(uniquePitches, candidateRoot);

        // Try to match a pattern
        for (const auto& pattern : chordPatterns)
        {
            // Check if intervals match pattern (allowing extra notes)
            bool matches = true;
            for (int interval : pattern.intervals)
            {
                if (intervals.find(interval) == intervals.end())
                {
                    matches = false;
                    break;
                }
            }

            if (matches)
            {
                float score = static_cast<float>(pattern.priority);

                // Bonus for bass note being the root
                if (notes[0] % 12 == candidateRoot)
                    score += 5.0f;

                // Bonus for matching interval count closely
                int extraNotes = static_cast<int>(intervals.size()) - static_cast<int>(pattern.intervals.size());
                score -= static_cast<float>(extraNotes) * 0.5f;

                if (score > bestScore || (score == bestScore && pattern.priority > bestPriority))
                {
                    bestScore = score;
                    bestPriority = pattern.priority;
                    bestRoot = candidateRoot;
                }
            }
        }
    }

    return bestRoot;
}

std::set<int> ChordAnalyzer::getIntervals(const std::vector<int>& notes, int root) const
{
    std::set<int> intervals;
    intervals.insert(0);  // Root is always 0

    for (int note : notes)
    {
        int pitchClass = note % 12;
        int interval = (pitchClass - root + 12) % 12;
        intervals.insert(interval);

        // Also add compound intervals for extended chords
        if (interval == 2) intervals.insert(14);  // 9th
        if (interval == 5) intervals.insert(17);  // 11th
        if (interval == 9) intervals.insert(21);  // 13th
    }

    return intervals;
}

ChordQuality ChordAnalyzer::matchPattern(const std::set<int>& intervals, int& outPriority) const
{
    ChordQuality bestMatch = ChordQuality::Unknown;
    int bestPriority = -1;
    size_t bestMatchSize = 0;

    for (const auto& pattern : chordPatterns)
    {
        // Check if all pattern intervals are present
        bool matches = true;
        for (int interval : pattern.intervals)
        {
            if (intervals.find(interval) == intervals.end())
            {
                matches = false;
                break;
            }
        }

        if (matches)
        {
            // Prefer patterns that match more notes (more specific)
            // But also consider priority
            if (pattern.priority > bestPriority ||
                (pattern.priority == bestPriority && pattern.intervals.size() > bestMatchSize))
            {
                bestMatch = pattern.quality;
                bestPriority = pattern.priority;
                bestMatchSize = pattern.intervals.size();
            }
        }
    }

    outPriority = bestPriority;
    return bestMatch;
}

int ChordAnalyzer::calculateInversion(const std::vector<int>& notes, int root) const
{
    if (notes.empty()) return 0;

    int bassNote = notes[0] % 12;
    if (bassNote == root) return 0;

    // Find which chord tone is in the bass
    std::set<int> intervals;
    for (int note : notes)
        intervals.insert((note % 12 - root + 12) % 12);

    int bassInterval = (bassNote - root + 12) % 12;

    if (bassInterval == 3 || bassInterval == 4) return 1;  // Third in bass
    if (bassInterval == 7) return 2;  // Fifth in bass
    if (bassInterval == 10 || bassInterval == 11) return 3;  // Seventh in bass

    return 0;
}

float ChordAnalyzer::calculateConfidence(const std::set<int>& intervals, ChordQuality matched) const
{
    if (matched == ChordQuality::Unknown) return 0.0f;

    // Find the matching pattern
    for (const auto& pattern : chordPatterns)
    {
        if (pattern.quality == matched)
        {
            // Check how closely we match
            int matchedIntervals = 0;
            int extraIntervals = 0;

            for (int interval : intervals)
            {
                if (interval < 12)  // Only count basic intervals
                {
                    if (pattern.intervals.find(interval) != pattern.intervals.end())
                        matchedIntervals++;
                    else if (interval != 0)
                        extraIntervals++;
                }
            }

            float patternMatch = static_cast<float>(matchedIntervals) / static_cast<float>(pattern.intervals.size());
            float penalty = static_cast<float>(extraIntervals) * 0.1f;

            return juce::jlimit(0.0f, 1.0f, patternMatch - penalty);
        }
    }

    return 0.5f;
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
            return minorKey ? HarmonicFunction::Tonic : HarmonicFunction::Tonic;
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

    return pitchClassToName(pitchClass);
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
        // bVII from mixolydian
        int bVII = (keyRoot + 10) % 12;
        addSuggestion("bVII", pitchClassToName(bVII),
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
        // Tritone substitution target
        int tritone = (keyRoot + 6) % 12;
        addSuggestion("bII7", pitchClassToName(tritone) + "7",
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
        // bVI (chromatic mediant)
        int bVI = (keyRoot + 8) % 12;
        addSuggestion("bVI", pitchClassToName(bVI),
                      "Chromatic mediant - dramatic shift", 0.4f);

        // bIII (chromatic mediant)
        int bIII = (keyRoot + 3) % 12;
        addSuggestion("bIII", pitchClassToName(bIII),
                      "Chromatic mediant - upward", 0.35f);
    }

    // Neapolitan
    if (currentDegree == 4 || currentDegree == 2)
    {
        int neapolitan = (keyRoot + 1) % 12;
        addSuggestion("bII", pitchClassToName(neapolitan),
                      "Neapolitan chord - pre-dominant", 0.4f);
    }

    // Augmented 6th approach
    if (currentDegree == 5)
    {
        // Italian augmented 6th setup
        addSuggestion("It+6", "It+6",
                      "Italian augmented 6th - chromatic approach", 0.3f);
    }

    // Coltrane changes suggestion
    if (currentDegree == 1)
    {
        int majThirdDown = (keyRoot + 8) % 12;  // Ab in C
        addSuggestion("bVI maj7", pitchClassToName(majThirdDown) + "maj7",
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
