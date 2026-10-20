// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#include "ChordAnalyzerText.hpp"
#include <string_view>
#include <algorithm>
#include <utility>

namespace duskaudio::chord
{
namespace
{
    std::string_view trimAscii(std::string_view text) noexcept
    {
        const auto space = [](char c) { return c == ' ' || (c >= '\t' && c <= '\r'); };
        while (!text.empty() && space(text.front())) text.remove_prefix(1);
        while (!text.empty() && space(text.back())) text.remove_suffix(1);
        return text;
    }
    bool startsWith(std::string_view text, std::string_view prefix) noexcept
    {
        return text.substr(0, prefix.size()) == prefix;
    }
}

//==============================================================================
ChordDescription ChordAnalyzerText::analyze(const std::vector<int>& midiNotes)
{
    ChordDescription result;
    result.midiNotes = midiNotes;
    std::sort(result.midiNotes.begin(), result.midiNotes.end());

    if (midiNotes.empty())
    {
        result.name = "-";
        result.romanNumeral = "-";
        return result;
    }

    const ChordFacts facts = analyzeFacts(midiNotes.data(), static_cast<int>(midiNotes.size()));

    static_cast<ChordFacts&>(result) = facts;

    if (midiNotes.size() == 1)
    {
        // Single note - just show the note name
        result.name = noteToName(midiNotes[0]);
        result.romanNumeral = "-";
        return result;
    }

    if (facts.patternIndex < 0)
    {
        // Two pitch classes are an interval, not a chord, so name the interval
        // rather than giving up with "C?". The dyads that do imply a chord, the
        // fourth and the fifth, match a pattern above and never reach here.
        // Counted in pitch classes, not notes, so a doubled dyad still reads as
        // its interval: C4 E4 C5 is a major third, not three unnamed notes.
        std::uint16_t pitchMask = 0;
        for (int note : midiNotes)
            pitchMask = static_cast<std::uint16_t>(pitchMask | (1u << (((note % 12) + 12) % 12)));

        const int distinctPitches = countPitchClasses(pitchMask);

        if (distinctPitches <= 2)
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

            // One pitch class is the same note doubled: an octave, or a unison
            // when every note is literally the same pitch.
            const char* interval = nullptr;

            if (distinctPitches < 2)
            {
                const auto range = std::minmax_element(result.midiNotes.begin(),
                                                       result.midiNotes.end());
                interval = (*range.first == *range.second) ? "unison" : "octave";
            }
            else
            {
                interval = intervalName(((upper - result.bassNote) + 12) % 12);
            }

            result.name = pitchClassToName(result.bassNote) + "+" + pitchClassToName(upper)
                        + " (" + interval + ")";
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
// Label encoding for the "Detected Chord" host parameter. See ChordAnalyzerText.hpp
// for why the decoder has to be a pure function of the code.
namespace
{
    // One block of codes per branch analyze() can take when it builds
    // ChordDescription::name, sized exactly to what that branch needs.
    constexpr int kLabelNone        = 0;
    constexpr int kLabelSingleBase  = 1;                              // 128 MIDI notes
    constexpr int kLabelDyadBase    = kLabelSingleBase + 128;         // 12 x 12 x unison flag
    constexpr int kLabelUnknownBase = kLabelDyadBase + 12 * 12 * 2;   // 12 roots
    constexpr int kLabelChordBase   = kLabelUnknownBase + 12;         // root x bass x mask

    // Sounding intervals above the root, one bit each for 1..11 semitones. The
    // root is always sounding (findRoot only ever returns a sounding pitch
    // class), so interval 0 is implied and costs no bit.
    constexpr int kLabelMaskBits    = 11;
    constexpr int kLabelMaskCount   = 1 << kLabelMaskBits;

    static_assert(kLabelChordBase + 12 * 12 * kLabelMaskCount - 1
                      == ChordAnalyzerText::maxLabelCode,
                  "maxLabelCode must match the label code layout");

    std::uint16_t pitchMaskOf(const int* midiNotes, int count) noexcept
    {
        std::uint16_t mask = 0;
        for (int i = 0; i < count; ++i)
            mask = static_cast<std::uint16_t>(mask | (1u << (((midiNotes[i] % 12) + 12) % 12)));
        return mask;
    }
}

int ChordAnalyzerText::encodeLabel(const ChordFacts& chord, const int* notes, int numNotes, bool showInversions) noexcept
{
    // Branches in the same order as analyze(), on the same conditions, so the
    // code always describes the string analyze() produced. isValid stands in
    // for "a pattern matched": ChordFacts sets it only on that path, and sets
    // it whenever that path is taken.
    if (notes == nullptr || numNotes <= 0)
        return kLabelNone;

    if (numNotes == 1)
        return kLabelSingleBase + std::clamp(notes[0], 0, 127);

    const std::uint16_t pitchMask = pitchMaskOf(notes, numNotes);
    const int bass = ((chord.bassNote % 12) + 12) % 12;
    const int root = ((chord.rootNote % 12) + 12) % 12;

    if (! chord.isValid)
    {
        const int distinctPitches = countPitchClasses(pitchMask);

        if (distinctPitches > 2)
            return kLabelUnknownBase + root;

        int upper = bass;
        for (int pc = 0; pc < 12; ++pc)
        {
            if (pc != bass && (pitchMask & (1u << pc)) != 0)
            {
                upper = pc;
                break;
            }
        }

        // One pitch class is a doubled note: an octave, or a unison when every
        // note is literally the same pitch.
        bool unison = false;
        if (distinctPitches < 2)
        {
            const auto range = std::minmax_element(notes, notes + numNotes);
            unison = (*range.first == *range.second);
        }

        return kLabelDyadBase + (bass * 12 + upper) * 2 + (unison ? 1 : 0);
    }

    int intervalMask = 0;
    for (int pc = 0; pc < 12; ++pc)
    {
        if ((pitchMask & (1u << pc)) == 0)
            continue;

        const int interval = ((pc - root) + 12) % 12;
        if (interval != 0)
            intervalMask |= (1 << (interval - 1));
    }

    // Bass interval 0 means "no slash", which is also how a toggled-off Show
    // Inversions reads: the editor drops the suffix, so the code must too.
    const int bassInterval = (showInversions && chord.slashBass)
                                 ? ((bass - root) + 12) % 12 : 0;

    return kLabelChordBase + (root * 12 + bassInterval) * kLabelMaskCount + intervalMask;
}

std::string ChordAnalyzerText::decodeLabel(int code)
{
    // Hosts probe getText() with any value in range, so every code out of the
    // encoder's image still has to produce something harmless.
    if (code <= kLabelNone || code > maxLabelCode)
        return "-";

    if (code < kLabelDyadBase)
        return noteToName(code - kLabelSingleBase);

    if (code < kLabelUnknownBase)
    {
        int payload = code - kLabelDyadBase;
        const bool unison = (payload % 2) != 0;
        payload /= 2;
        const int bass  = payload / 12;
        const int upper = payload % 12;

        const char* interval = (bass == upper) ? (unison ? "unison" : "octave")
                                               : intervalName(((upper - bass) + 12) % 12);

        return pitchClassToName(bass) + "+" + pitchClassToName(upper)
             + " (" + interval + ")";
    }

    if (code < kLabelChordBase)
        return pitchClassToName(code - kLabelUnknownBase) + "?";

    int payload = code - kLabelChordBase;
    const int intervalMask = payload % kLabelMaskCount;
    payload /= kLabelMaskCount;
    const int bassInterval = payload % 12;
    const int root         = payload / 12;

    // Rebuild the sounding pitch classes, then run the same matching and
    // naming analyze() runs. No second copy of the pattern table.
    std::uint16_t pitchMask = static_cast<std::uint16_t>(1u << root);
    for (int bit = 0; bit < kLabelMaskBits; ++bit)
        if ((intervalMask & (1 << bit)) != 0)
            pitchMask = static_cast<std::uint16_t>(pitchMask | (1u << ((root + bit + 1) % 12)));

    const std::uint32_t intervals = intervalsFrom(pitchMask, root);
    const int patternIndex = matchPattern(intervals);

    if (patternIndex < 0)
        return pitchClassToName(root) + "?";   // only reachable from a probed code

    std::string label = pitchClassToName(root)
                       + qualityToSuffix(chordPatterns[static_cast<size_t>(patternIndex)].quality)
                       + describeAddedTones(patternIndex, intervals);

    if (bassInterval != 0)
        label += "/" + pitchClassToName((root + bassInterval) % 12);

    return label;
}

// Hosts may ask for a value from any label they previously rendered. Several
// codes have the same text (e.g. C5 is a single note and a power chord), so this
// returns a canonical representative with the same label, not the original code.
int ChordAnalyzerText::labelToCode(std::string_view source)
{
    const auto text = trimAscii(source);
    if (text == "-") return kLabelNone;

    int root = -1;
    std::size_t rootLength = 0;
    for (int pitch = 0; pitch < 12; ++pitch)
    {
        const auto name = pitchClassToName(pitch);
        if (name.size() > rootLength && startsWith(text, name))
        {
            root = pitch;
            rootLength = name.size();
        }
    }
    if (root < 0) return -1;

    for (int note = root; note < 128; note += 12)
        if (noteToName(note) == text) return kLabelSingleBase + note;

    if (text.find('+') != std::string_view::npos)
    {
        for (int code = kLabelDyadBase; code < kLabelUnknownBase; ++code)
            if (decodeLabel(code) == text) return code;
        return -1;
    }
    if (text == pitchClassToName(root) + "?") return kLabelUnknownBase + root;

    int bassInterval = 0;
    const auto slash = text.rfind('/');
    if (slash != std::string_view::npos)
    {
        int bass = -1;
        for (int pitch = 0; pitch < 12; ++pitch)
            if (text.substr(slash + 1) == pitchClassToName(pitch)) bass = pitch;
        if (bass < 0) return -1;
        bassInterval = (bass - root + 12) % 12;
    }
    const auto body = text.substr(rootLength, slash == std::string_view::npos ? slash : slash - rootLength);

    for (std::size_t pattern = 0; pattern < chordPatterns.size(); ++pattern)
    {
        const auto suffix = qualityToSuffix(chordPatterns[pattern].quality);
        if (!startsWith(body, suffix)) continue;
        const auto extra = body.substr(suffix.size());
        auto pitches = kPatternMasks[pattern].pitches;
        const auto addTension = [&pitches](std::string_view token)
        {
            for (int interval = 1; interval < 12; ++interval)
                if (token == tensionLabel(interval))
                {
                    pitches = static_cast<std::uint16_t>(pitches | (1u << interval));
                    return true;
                }
            return false;
        };
        bool valid = true;
        if (!extra.empty())
        {
            if (startsWith(extra, "add")) valid = addTension(extra.substr(3));
            else if (extra.front() == '(' && extra.back() == ')')
            {
                auto remaining = extra.substr(1, extra.size() - 2);
                while (!remaining.empty())
                {
                    const auto comma = remaining.find(',');
                    const auto token = remaining.substr(0, comma);
                    if (token != "no5") valid = addTension(token) && valid;
                    if (comma == std::string_view::npos) break;
                    remaining.remove_prefix(comma + 1);
                }
            }
            else valid = false;
        }
        if (!valid) continue;
        const int code = kLabelChordBase + (root * 12 + bassInterval) * kLabelMaskCount + (pitches >> 1);
        // Reuse the authoritative decoder to reject ambiguous suffix matches,
        // malformed extras and spellings it could never have emitted.
        if (decodeLabel(code) == text) return code;
    }
    return -1;
}

//==============================================================================

std::string ChordAnalyzerText::getKeyName() const
{
    return pitchClassToName(keyRoot) + (minorKey ? " Minor" : " Major");
}

//==============================================================================

const char* ChordAnalyzerText::tensionLabel(int semitonesFromRoot)
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

const char* ChordAnalyzerText::intervalName(int semitones)
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

std::string ChordAnalyzerText::describeAddedTones(int patternIndex, std::uint32_t intervals)
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
        return std::string("add") + extras[0];

    std::string text("(");
    for (int i = 0; i < numExtras; ++i)
    {
        if (i > 0)
            text += ",";
        text += extras[i];
    }
    text += ")";

    return text;
}

//==============================================================================
// Roman numeral generation

std::string ChordAnalyzerText::getAccidental(int chordRoot) const
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

std::string ChordAnalyzerText::degreeToRoman(int degree, bool uppercase) const
{
    static const std::string upperNumerals[] = {"I", "II", "III", "IV", "V", "VI", "VII"};
    static const std::string lowerNumerals[] = {"i", "ii", "iii", "iv", "v", "vi", "vii"};

    if (degree < 1 || degree > 7) return "?";

    return uppercase ? upperNumerals[degree - 1] : lowerNumerals[degree - 1];
}

std::string ChordAnalyzerText::buildRomanNumeral(int chordRoot, ChordQuality quality) const
{
    int degree = getScaleDegree(chordRoot);
    std::string accidental = getAccidental(chordRoot);

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

    std::string numeral = accidental + degreeToRoman(degree, uppercase);

    // Add quality suffix
    switch (quality)
    {
        case ChordQuality::Diminished:
            numeral += std::string("\xC2\xB0");  // degree sign
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
            numeral += std::string("\xC3\xB8") + "7";  // slashed o
            break;
        case ChordQuality::Diminished7:
            numeral += std::string("\xC2\xB0") + "7";
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
std::string ChordAnalyzerText::getSpellingForKey(int pitchClass) const
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

//==============================================================================
// Suggestion naming from the numeric core's bounded seeds.

std::string ChordAnalyzerText::spellSeed(const SuggestionSeed& seed) const
{
    std::string name;

    switch (seed.spelling)
    {
        case RootSpelling::KeyAware: name = getSpellingForKey(seed.rootNote);        break;
        case RootSpelling::Flat:     name = pitchClassToName(seed.rootNote, true);   break;
        case RootSpelling::Sharp:
        default:                     name = pitchClassToName(seed.rootNote, false);  break;
    }

    return name + (seed.nameSuffix != nullptr ? std::string(seed.nameSuffix)
                                              : qualityToSuffix(seed.quality));
}

std::vector<ChordSuggestionText> ChordAnalyzerText::getSuggestions(const ChordFacts& currentChord,
                                                            SuggestionCategory maxLevel) const
{
    std::vector<ChordSuggestionText> suggestions;

    if (!currentChord.isValid || currentChord.rootNote < 0)
        return suggestions;

    SuggestionSeed seeds[maxSuggestions];
    const int count = collectSuggestions(seeds, maxSuggestions,
                                         getScaleDegree(currentChord.rootNote),
                                         currentChord.quality, maxLevel);

    suggestions.reserve(static_cast<size_t>(count));

    for (int i = 0; i < count; ++i)
    {
        ChordSuggestionText s;
        s.romanNumeral = seeds[i].romanNumeral;
        s.chordName    = spellSeed(seeds[i]);
        s.category     = seeds[i].category;
        s.reason       = seeds[i].reason;
        s.commonality  = seeds[i].commonality;
        suggestions.push_back(std::move(s));
    }

    return suggestions;
}

//==============================================================================
// Static utility functions
std::string ChordAnalyzerText::noteToName(int midiNote, bool useFlats)
{
    int pitchClass = midiNote % 12;
    int octave = (midiNote / 12) - 1;
    return pitchClassToName(pitchClass, useFlats) + std::to_string(octave);
}

std::string ChordAnalyzerText::pitchClassToName(int pitchClass, bool useFlats)
{
    static const char* sharpNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char* flatNames[] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};

    pitchClass = pitchClass % 12;
    if (pitchClass < 0) pitchClass += 12;

    return useFlats ? flatNames[pitchClass] : sharpNames[pitchClass];
}

int ChordAnalyzerText::nameToNote(std::string_view name)
{
    const auto text = trimAscii(name);
    if (text.empty()) return -1;
    const auto upper = [](char c) { return c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : c; };
    int base = -1;
    switch (upper(text[0]))
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
    if (text.size() > 1)
    {
        const char accidental = upper(text[1]);
        if (accidental == '#' || accidental == 'S') ++base;
        else if (accidental == 'B') --base;
    }
    return (base + 12) % 12;
}

std::string ChordAnalyzerText::qualityToString(ChordQuality quality)
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

std::string ChordAnalyzerText::qualityToSuffix(ChordQuality quality)
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

std::string ChordAnalyzerText::functionToString(HarmonicFunction func)
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

} // namespace duskaudio::chord
