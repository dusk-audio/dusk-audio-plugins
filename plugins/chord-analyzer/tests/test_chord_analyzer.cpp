/*
    Chord Analyzer unit tests — verifies chord recognition, inversions,
    Roman numeral generation, harmonic functions, and edge cases.
*/

#include "../Source/ChordAnalyzer.h"
#include <iostream>
#include <iomanip>
#include <vector>

static int passed = 0, failed = 0;

static void check(const char* name, bool condition)
{
    if (condition) {
        std::cout << "\033[32m[PASS]\033[0m " << name << "\n";
        ++passed;
    } else {
        std::cout << "\033[31m[FAIL]\033[0m " << name << "\n";
        ++failed;
    }
}

// Helper: build a chord from MIDI notes
static ChordInfo analyzeNotes(ChordAnalyzer& a, std::initializer_list<int> notes)
{
    return a.analyze(std::vector<int>(notes));
}

// =====================================================================
// 1. Basic triads
// =====================================================================
static void testTriads()
{
    std::cout << "\n--- Basic Triads ---\n";
    ChordAnalyzer a;

    // C major (C E G)
    auto c = analyzeNotes(a, {60, 64, 67});
    check("C major detected",  c.isValid && c.quality == ChordQuality::Major);
    check("C major root = C",  c.rootNote == 0);
    check("C major name",      c.name.startsWith("C"));

    // C minor (C Eb G)
    auto cm = analyzeNotes(a, {60, 63, 67});
    check("C minor detected",  cm.isValid && cm.quality == ChordQuality::Minor);

    // C diminished (C Eb Gb)
    auto cd = analyzeNotes(a, {60, 63, 66});
    check("C dim detected",    cd.isValid && cd.quality == ChordQuality::Diminished);

    // C augmented (C E G#)
    auto ca = analyzeNotes(a, {60, 64, 68});
    check("C aug detected",    ca.isValid && ca.quality == ChordQuality::Augmented);

    // D major (D F# A)
    auto d = analyzeNotes(a, {62, 66, 69});
    check("D major detected",  d.isValid && d.quality == ChordQuality::Major);
    check("D major root = D",  d.rootNote == 2);

    // F# minor (F# A C#)
    auto fsm = analyzeNotes(a, {66, 69, 73});
    check("F# minor detected", fsm.isValid && fsm.quality == ChordQuality::Minor);
    check("F# minor root = F#", fsm.rootNote == 6);

    // Bb major (Bb D F)
    auto bb = analyzeNotes(a, {58, 62, 65});
    check("Bb major detected", bb.isValid && bb.quality == ChordQuality::Major);
    check("Bb major root = Bb", bb.rootNote == 10);

    // Ab minor (Ab Cb Eb)
    auto abm = analyzeNotes(a, {56, 59, 63});
    check("Ab minor detected", abm.isValid && abm.quality == ChordQuality::Minor);
    check("Ab minor root = Ab", abm.rootNote == 8);
}

// =====================================================================
// 2. Seventh chords
// =====================================================================
static void testSevenths()
{
    std::cout << "\n--- Seventh Chords ---\n";
    ChordAnalyzer a;

    // C major 7 (C E G B)
    auto cmaj7 = analyzeNotes(a, {60, 64, 67, 71});
    check("Cmaj7 detected",    cmaj7.isValid && cmaj7.quality == ChordQuality::Major7);

    // C dominant 7 (C E G Bb)
    auto c7 = analyzeNotes(a, {60, 64, 67, 70});
    check("C7 detected",       c7.isValid && c7.quality == ChordQuality::Dominant7);

    // C minor 7 (C Eb G Bb)
    auto cm7 = analyzeNotes(a, {60, 63, 67, 70});
    check("Cm7 detected",      cm7.isValid && cm7.quality == ChordQuality::Minor7);

    // C diminished 7 (C Eb Gb Bbb/A)
    auto cdim7 = analyzeNotes(a, {60, 63, 66, 69});
    check("Cdim7 detected",    cdim7.isValid && cdim7.quality == ChordQuality::Diminished7);

    // C half-diminished 7 (C Eb Gb Bb)
    auto cm7b5 = analyzeNotes(a, {60, 63, 66, 70});
    check("Cm7b5 detected",    cm7b5.isValid && cm7b5.quality == ChordQuality::HalfDiminished7);

    // G dominant 7 (G B D F)
    auto g7 = analyzeNotes(a, {55, 59, 62, 65});
    check("G7 detected",       g7.isValid && g7.quality == ChordQuality::Dominant7);
    check("G7 root = G",       g7.rootNote == 7);

    // A minor 7 (A C E G)
    auto am7 = analyzeNotes(a, {57, 60, 64, 67});
    check("Am7 detected",      am7.isValid && am7.quality == ChordQuality::Minor7);
    check("Am7 root = A",      am7.rootNote == 9);
}

// =====================================================================
// 3. Sus and add chords
// =====================================================================
static void testSusAdd()
{
    std::cout << "\n--- Sus and Add Chords ---\n";
    ChordAnalyzer a;

    // Csus2 (C D G)
    auto csus2 = analyzeNotes(a, {60, 62, 67});
    check("Csus2 detected",    csus2.isValid && csus2.quality == ChordQuality::Sus2);

    // Csus4 (C F G)
    auto csus4 = analyzeNotes(a, {60, 65, 67});
    check("Csus4 detected",    csus4.isValid && csus4.quality == ChordQuality::Sus4);

    // C power chord (C G)
    auto c5 = analyzeNotes(a, {60, 67});
    check("C5 power detected", c5.isValid && c5.quality == ChordQuality::Power5);
}

// =====================================================================
// 4. Inversions
// =====================================================================
static void testInversions()
{
    std::cout << "\n--- Inversions ---\n";
    ChordAnalyzer a;

    // C major root position (C E G)
    auto root = analyzeNotes(a, {60, 64, 67});
    check("C root position",   root.inversion == 0);

    // C major 1st inversion (E G C)
    auto first = analyzeNotes(a, {52, 55, 60});
    check("C 1st inversion",   first.isValid && first.quality == ChordQuality::Major
                                && first.rootNote == 0 && first.inversion == 1);

    // C major 2nd inversion (G C E)
    auto second = analyzeNotes(a, {55, 60, 64});
    check("C 2nd inversion",   second.isValid && second.quality == ChordQuality::Major
                                && second.rootNote == 0 && second.inversion == 2);

    // G7 3rd inversion (F G B D) — bass note F
    auto third = analyzeNotes(a, {53, 55, 59, 62});
    check("G7 3rd inversion",  third.isValid && third.quality == ChordQuality::Dominant7
                               && third.rootNote == 7 && third.inversion == 3);
}

// =====================================================================
// 5. Roman numerals in C major
// =====================================================================
static void testRomanNumeralsMajor()
{
    std::cout << "\n--- Roman Numerals (C Major) ---\n";
    ChordAnalyzer a;
    a.setKey(0, false);  // C major

    // I = C major
    auto I = analyzeNotes(a, {60, 64, 67});
    check("C in C major = I",          I.romanNumeral == "I");

    // ii = D minor
    auto ii = analyzeNotes(a, {62, 65, 69});
    check("Dm in C major = ii",        ii.romanNumeral == "ii");

    // iii = E minor
    auto iii = analyzeNotes(a, {64, 67, 71});
    check("Em in C major = iii",       iii.romanNumeral == "iii");

    // IV = F major
    auto IV = analyzeNotes(a, {65, 69, 72});
    check("F in C major = IV",         IV.romanNumeral == "IV");

    // V = G major
    auto V = analyzeNotes(a, {55, 59, 62});
    check("G in C major = V",          V.romanNumeral == "V");

    // vi = A minor
    auto vi = analyzeNotes(a, {57, 60, 64});
    check("Am in C major = vi",        vi.romanNumeral == "vi");

    // V7 = G dominant 7
    auto V7 = analyzeNotes(a, {55, 59, 62, 65});
    check("G7 in C major = V7",        V7.romanNumeral == "V7");
}

// =====================================================================
// 6. Roman numerals in A minor
// =====================================================================
static void testRomanNumeralsMinor()
{
    std::cout << "\n--- Roman Numerals (A Minor) ---\n";
    ChordAnalyzer a;
    a.setKey(9, true);  // A minor

    // i = A minor
    auto i = analyzeNotes(a, {57, 60, 64});
    check("Am in A minor = i",         i.romanNumeral == "i");

    // III = C major
    auto III = analyzeNotes(a, {60, 64, 67});
    check("C in A minor = III",        III.romanNumeral == "III");

    // iv = D minor
    auto iv = analyzeNotes(a, {62, 65, 69});
    check("Dm in A minor = iv",        iv.romanNumeral == "iv");

    // V = E major (harmonic minor dominant)
    auto V = analyzeNotes(a, {64, 68, 71});
    check("E in A minor = V",          V.romanNumeral == "V");

    // VI = F major
    auto VI = analyzeNotes(a, {65, 69, 72});
    check("F in A minor = VI",         VI.romanNumeral == "VI");

    // VII = G major
    auto VII = analyzeNotes(a, {55, 59, 62});
    check("G in A minor = VII",        VII.romanNumeral == "VII");
}

// =====================================================================
// 7. Harmonic functions
// =====================================================================
static void testHarmonicFunctions()
{
    std::cout << "\n--- Harmonic Functions ---\n";
    ChordAnalyzer a;
    a.setKey(0, false);  // C major

    // I = Tonic
    auto I = analyzeNotes(a, {60, 64, 67});
    check("I = Tonic",    I.function == HarmonicFunction::Tonic);

    // IV = Subdominant
    auto IV = analyzeNotes(a, {65, 69, 72});
    check("IV = Subdominant", IV.function == HarmonicFunction::Subdominant);

    // V = Dominant
    auto V = analyzeNotes(a, {55, 59, 62});
    check("V = Dominant",  V.function == HarmonicFunction::Dominant);

    // ii = Subdominant
    auto ii = analyzeNotes(a, {62, 65, 69});
    check("ii = Subdominant", ii.function == HarmonicFunction::Subdominant);

    // vi = Tonic
    auto vi = analyzeNotes(a, {57, 60, 64});
    check("vi = Tonic",    vi.function == HarmonicFunction::Tonic);
}

// =====================================================================
// 8. Edge cases
// =====================================================================
static void testEdgeCases()
{
    std::cout << "\n--- Edge Cases ---\n";
    ChordAnalyzer a;

    // Empty input
    auto empty = a.analyze({});
    check("Empty = invalid",   !empty.isValid);

    // Single note
    auto single = a.analyze({60});
    check("Single note = invalid", !single.isValid);

    // Two notes (power chord: C G)
    auto two = analyzeNotes(a, {48, 55});
    check("Two notes = power chord", two.isValid && two.quality == ChordQuality::Power5);

    // Octave-doubled triad (C E G C)
    auto doubled = analyzeNotes(a, {48, 52, 55, 60});
    check("Doubled triad = Major", doubled.isValid && doubled.quality == ChordQuality::Major
                                    && doubled.rootNote == 0);

    // Wide voicing (C3 G4 E5)
    auto wide = analyzeNotes(a, {48, 67, 76});
    check("Wide voicing = Major", wide.isValid && wide.quality == ChordQuality::Major
                                   && wide.rootNote == 0);

    // Dense cluster (C Db D Eb) — should either detect or gracefully fail
    auto cluster = a.analyze({60, 61, 62, 63});
    check("Cluster doesn't crash", true);  // Just verify no crash

    // Many notes (10+ polyphony)
    auto many = a.analyze({48, 52, 55, 60, 64, 67, 72, 76, 79, 84});
    check("10-note polyphony doesn't crash", true);
}

// =====================================================================
// 9. Confidence scores
// =====================================================================
static void testConfidence()
{
    std::cout << "\n--- Confidence Scores ---\n";
    ChordAnalyzer a;

    // Perfect triad should have high confidence
    auto cmaj = analyzeNotes(a, {60, 64, 67});
    check("Triad confidence > 0.5", cmaj.confidence > 0.5f);

    // Perfect seventh chord
    auto cmaj7 = analyzeNotes(a, {60, 64, 67, 71});
    check("7th chord confidence > 0.5", cmaj7.confidence > 0.5f);
}

// =====================================================================
// 10. Static utility functions
// =====================================================================
static void testUtilities()
{
    std::cout << "\n--- Utility Functions ---\n";

    check("noteToName(60) = C",  ChordAnalyzer::noteToName(60).startsWith("C"));
    check("noteToName(69) = A",  ChordAnalyzer::noteToName(69).startsWith("A"));
    check("pitchClassToName(0) = C", ChordAnalyzer::pitchClassToName(0) == "C");
    check("pitchClassToName(7) = G", ChordAnalyzer::pitchClassToName(7) == "G");
    check("qualityToString(Major)", ChordAnalyzer::qualityToString(ChordQuality::Major) == "Major");
    check("functionToString(Tonic)", ChordAnalyzer::functionToString(HarmonicFunction::Tonic) == "Tonic");
}

// =====================================================================
int main()
{
    std::cout << "=== Chord Analyzer Unit Tests ===\n";

    testTriads();
    testSevenths();
    testSusAdd();
    testInversions();
    testRomanNumeralsMajor();
    testRomanNumeralsMinor();
    testHarmonicFunctions();
    testEdgeCases();
    testConfidence();
    testUtilities();

    std::cout << "\n=== Results: " << passed << " passed, " << failed << " failed ===\n";
    return failed > 0 ? 1 : 0;
}
