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
// 9b. Added tones and slash bass (issue #235)
//
// The matcher accepts a pattern whose intervals are a subset of the notes
// played, so every tone outside the pattern used to vanish from the name:
// C E G F# read as plain "C".
// =====================================================================
static void testAddedTones()
{
    std::cout << "\n--- Added Tones / Slash Bass ---\n";
    ChordAnalyzer a;

    // C major triad plus F# (C E G F#) — the reported chord
    auto addSharp11 = analyzeNotes(a, {60, 64, 66, 67});
    check("C E G F# = Cadd#11", addSharp11.isValid && addSharp11.name == "Cadd#11");

    // Same pitch classes voiced with F# in the bass — "C / F#"
    auto overSharp11 = analyzeNotes(a, {54, 60, 64, 67});
    check("C triad over F# names the #11", overSharp11.name == "Cadd#11");
    check("C triad over F# is a slash chord",
          overSharp11.slashBass && overSharp11.extensions == "/F#");

    // A perfect fifth alongside the tritone means #11, never b5
    auto dom7Sharp11 = analyzeNotes(a, {60, 64, 66, 67, 70});
    check("C E G Bb F# = C7(#11)", dom7Sharp11.isValid
                                    && dom7Sharp11.quality == ChordQuality::Dominant7
                                    && dom7Sharp11.name == "C7(#11)");

    auto maj7Sharp11 = analyzeNotes(a, {60, 64, 66, 67, 71});
    check("C E G B F# = Cmaj7(#11)", maj7Sharp11.name == "Cmaj7(#11)");

    // Same rule for the flat thirteenth against a natural fifth
    auto dom7Flat13 = analyzeNotes(a, {60, 64, 67, 68, 70});
    check("C E G Ab Bb = C7(b13)", dom7Flat13.name == "C7(b13)");

    // Genuine altered fifths still resolve to their own spelling
    auto realFlat5 = analyzeNotes(a, {60, 64, 66, 70});
    check("C E Gb Bb = C7b5", realFlat5.quality == ChordQuality::Dominant7Flat5
                               && realFlat5.name == "C7b5");
    auto realSharp5 = analyzeNotes(a, {60, 64, 68, 70});
    check("C E G# Bb = C7#5", realSharp5.name == "C7#5");
    auto dim = analyzeNotes(a, {60, 63, 66});
    check("C Eb Gb still = Cdim", dim.quality == ChordQuality::Diminished);

    // Altered ninths: these patterns were written with compound intervals
    // (13/15) that getIntervals never produces, so they could never match
    auto flat9 = analyzeNotes(a, {60, 64, 67, 70, 73});
    check("C E G Bb Db = C7b9", flat9.quality == ChordQuality::Dominant7Flat9
                                 && flat9.name == "C7b9");
    auto sharp9 = analyzeNotes(a, {60, 64, 67, 70, 75});
    check("C E G Bb Eb = C7#9", sharp9.quality == ChordQuality::Dominant7Sharp9
                                 && sharp9.name == "C7#9");

    // Exactly matched chords must gain no extra text
    check("C major gains no tension text", analyzeNotes(a, {60, 64, 67}).name == "C");
    check("Cadd9 gains no tension text",   analyzeNotes(a, {60, 64, 67, 74}).name == "Cadd9");
    check("Cadd11 gains no tension text",  analyzeNotes(a, {60, 64, 67, 65}).name == "Cadd11");
    check("Cmaj13 gains no tension text",
          analyzeNotes(a, {60, 64, 67, 71, 74, 77, 81}).name == "Cmaj13");

    // Classic inversions keep numbering their bass and still flag the slash
    auto firstInv = analyzeNotes(a, {64, 67, 72});
    check("C/E still 1st inversion", firstInv.inversion == 1
                                      && firstInv.slashBass
                                      && firstInv.extensions == "/E");
    check("Root position sets no slash", !analyzeNotes(a, {60, 64, 67}).slashBass);

    // A third outside the matched triad is a tension, not a dropped note
    check("C Eb E G = Cadd#9", analyzeNotes(a, {60, 63, 64, 67}).name == "Cadd#9");

    // Two patterns can tie on priority and size (add9 and add11 are both
    // {root,3,5,+1} at priority 16); chordPatterns declaration order breaks
    // the tie, and this pins the spelling that ordering is meant to produce.
    check("C E G D F = Cadd9(11)", analyzeNotes(a, {60, 64, 67, 74, 77}).name == "Cadd9(11)");
}

// =====================================================================
// 9c. Chord identity includes the bass
//
// ChordInfo::operator== gates display refresh, history and the exported
// detectedBass parameter. Leaving bassNote out of it meant moving only the
// bass never registered as a change.
// =====================================================================
static void testBassIdentity()
{
    std::cout << "\n--- Bass Is Part Of Identity ---\n";
    ChordAnalyzer a;

    auto overE = analyzeNotes(a, {64, 67, 72});   // C/E
    auto overG = analyzeNotes(a, {55, 60, 64});   // C/G

    check("C/E and C/G share a name",  overE.name == overG.name
                                        && overE.rootNote == overG.rootNote
                                        && overE.quality == overG.quality);
    check("C/E differs from C/G",      overE != overG);
    check("Same voicing compares equal", overE == analyzeNotes(a, {64, 67, 72}));
}

// =====================================================================
// 9d. Root choice: the bass decides an ambiguous spelling
//
// C E G A is both C6 and Am7. findRoot gives the bass a bonus, but the
// tie-break then preferred the higher pattern priority, so m7 always beat
// 6 and every root-position 6th chord came out as a 7th chord instead.
// =====================================================================
static void testSixthChords()
{
    std::cout << "\n--- Sixth Chords / Root Choice ---\n";
    ChordAnalyzer a;

    check("C E G A over C = C6",     analyzeNotes(a, {60, 64, 67, 69}).name == "C6");
    check("C Eb G A over C = Cm6",   analyzeNotes(a, {60, 63, 67, 69}).name == "Cm6");

    // The same pitch classes over their other root still read as the 7th chord
    auto am7 = analyzeNotes(a, {57, 60, 64, 67});
    check("A C E G over A = Am7",    am7.name == "Am7" && !am7.slashBass);
    auto am7b5 = analyzeNotes(a, {57, 60, 63, 67});
    check("A C Eb G over A = Am7b5", am7b5.name == "Am7b5");

    // Every root, both qualities
    bool allRoots = true;
    for (int r = 0; r < 12; ++r)
    {
        const juce::String root = ChordAnalyzer::pitchClassToName(r);
        if (analyzeNotes(a, {60 + r, 64 + r, 67 + r, 69 + r}).name != root + "6")  allRoots = false;
        if (analyzeNotes(a, {60 + r, 63 + r, 67 + r, 69 + r}).name != root + "m6") allRoots = false;
    }
    check("all 12 roots, major and minor 6ths", allRoots);

    // A two-note shape must never absorb a cluster, and whether a set can be
    // named at all must not depend on which of its notes is lowest. Which
    // root wins may legitimately change with the bass - that is the rule
    // above - so only namability and the power-chord exclusion are asserted.
    const int clusterPcs[4] = { 0, 1, 2, 5 };   // C C# D F
    bool anyPowerChord = false;
    bool allSameValidity = true;
    bool firstValid = false;

    for (int rot = 0; rot < 4; ++rot)
    {
        std::vector<int> notes;
        for (int i = 0; i < 4; ++i)
            notes.push_back((i < rot ? 72 : 60) + clusterPcs[i]);
        std::sort(notes.begin(), notes.end());

        const auto c = a.analyze(notes);
        if (rot == 0) firstValid = c.isValid;
        if (c.isValid != firstValid) allSameValidity = false;
        if (c.quality == ChordQuality::Power5) anyPowerChord = true;
    }

    check("cluster is never a power chord", !anyPowerChord);
    check("cluster namability is bass-independent", allSameValidity);

    // ...but a power chord with a single added tone is still named
    check("C G F# = C5add#11", analyzeNotes(a, {60, 66, 67}).name == "C5add#11");
    check("C G = C5",          analyzeNotes(a, {60, 67}).name == "C5");
}

// =====================================================================
// 9e. analyzeFacts is the real-time path
//
// The headless LV2 wrapper calls analyzeFacts from run(), which is the
// audio thread, and the plugin declares lv2:hardRTCapable. It must agree
// with analyze() on every field that wrapper publishes.
// =====================================================================
static void testAnalyzeFacts()
{
    std::cout << "\n--- analyzeFacts (real-time path) ---\n";
    ChordAnalyzer a;

    int checked = 0, mismatches = 0;

    for (int mask = 0; mask < 4096; ++mask)
    {
        std::vector<int> pcs;
        for (int i = 0; i < 12; ++i)
            if (mask & (1 << i))
                pcs.push_back(i);

        if (pcs.empty())
            continue;

        // every rotation, so the bass varies too
        for (size_t rot = 0; rot < pcs.size(); ++rot)
        {
            std::vector<int> notes;
            for (size_t i = 0; i < pcs.size(); ++i)
                notes.push_back((i < rot ? 72 : 60) + pcs[i]);
            std::sort(notes.begin(), notes.end());

            const ChordInfo  full = a.analyze(notes);
            const ChordFacts fast = a.analyzeFacts(notes.data(), (int) notes.size());
            ++checked;

            if (full.isValid   != fast.isValid   || full.rootNote  != fast.rootNote
             || full.bassNote  != fast.bassNote  || full.quality   != fast.quality
             || full.inversion != fast.inversion || full.slashBass != fast.slashBass)
                ++mismatches;
        }
    }

    std::cout << "       (" << checked << " voicings compared)\n";
    check("analyzeFacts agrees with analyze", mismatches == 0);

    // Degenerate inputs must not reach into the note pointer
    check("null notes are safe",  !a.analyzeFacts(nullptr, 0).isValid);
    const int one = 60;
    check("single note is not a chord", !a.analyzeFacts(&one, 1).isValid);
}

// =====================================================================
// 9f. Voicings without the fifth, and two-note intervals
//
// Every pattern used to require the perfect fifth, so the shell voicings
// that are standard on guitar and piano either went unnamed (C E Bb) or
// were read as nonsense (C E B came out as "E5addb13/C").
// =====================================================================
static void testNoFifthAndDyads()
{
    std::cout << "\n--- No-Fifth Voicings / Dyads ---\n";
    ChordAnalyzer a;

    // Shells: root, third, seventh
    check("C E Bb = C7(no5)",       analyzeNotes(a, {60, 64, 70}).name == "C7(no5)");
    check("C E B = Cmaj7(no5)",     analyzeNotes(a, {60, 64, 71}).name == "Cmaj7(no5)");
    check("C Eb Bb = Cm7(no5)",     analyzeNotes(a, {60, 63, 70}).name == "Cm7(no5)");
    check("C Eb B = CmMaj7(no5)",   analyzeNotes(a, {60, 63, 71}).name == "CmMaj7(no5)");
    check("C E D = Cadd9(no5)",     analyzeNotes(a, {60, 64, 74}).name == "Cadd9(no5)");

    // ...and the same with the ninth
    check("C E Bb D = C9(no5)",     analyzeNotes(a, {60, 64, 70, 74}).name == "C9(no5)");
    check("C E B D = Cmaj9(no5)",   analyzeNotes(a, {60, 64, 71, 74}).name == "Cmaj9(no5)");
    check("C Eb Bb D = Cm9(no5)",   analyzeNotes(a, {60, 63, 70, 74}).name == "Cm9(no5)");

    // The quality is the real one, so Roman numerals and the exported
    // detected-quality port stay correct without new enum values
    auto shell = analyzeNotes(a, {60, 64, 70});
    check("shell keeps its quality", shell.quality == ChordQuality::Dominant7 && shell.isValid);

    // A complete chord must still prefer the complete spelling
    check("C7 with a fifth is plain C7",       analyzeNotes(a, {60, 64, 67, 70}).name == "C7");
    check("Cmaj7 with a fifth is plain Cmaj7", analyzeNotes(a, {60, 64, 67, 71}).name == "Cmaj7");
    check("C9 with a fifth is plain C9",       analyzeNotes(a, {60, 64, 67, 70, 74}).name == "C9");

    // An incomplete shape must not outrank a complete one at another root.
    // At priority 28 maj9(no5) rooted on Ab beat C7 rooted on the bass, and
    // C E G Ab Bb came out as G#maj9(no5,b13)/C.
    check("C E G Ab Bb = C7(b13)", analyzeNotes(a, {60, 64, 67, 68, 70}).name == "C7(b13)");
    check("C Eb E G = Cadd#9",     analyzeNotes(a, {60, 63, 64, 67}).name == "Cadd#9");
    check("C E G D F = Cadd9(11)", analyzeNotes(a, {60, 64, 67, 74, 77}).name == "Cadd9(11)");

    // An omitted fifth joins the tension list rather than opening a second
    // parenthesis, so it never reads C7(no5)(13)
    check("C E Bb A = C7(no5,13)", analyzeNotes(a, {60, 64, 70, 81}).name == "C7(no5,13)");

    // Two notes are an interval, not a chord
    check("C E = M3 dyad",       analyzeNotes(a, {60, 64}).name == "C+E (M3)");
    check("C F# = tritone dyad", analyzeNotes(a, {60, 66}).name == "C+F# (tritone)");
    check("C Bb = m7 dyad",      analyzeNotes(a, {60, 70}).name == "C+A# (m7)");
    check("C C = octave dyad",   analyzeNotes(a, {60, 72}).name == "C+C (octave)");
    check("C C = exact unison",  analyzeNotes(a, {60, 60}).name == "C+C (unison)");
    check("three octaves of C",  analyzeNotes(a, {60, 72, 84}).name == "C+C (octave)");

    // Doubling a note must not cost the dyad its name: these are two pitch
    // classes spread over three notes, still a major third
    check("C E C doubled = M3",  analyzeNotes(a, {60, 64, 72}).name == "C+E (M3)");
    check("C C E doubled = M3",  analyzeNotes(a, {60, 72, 76}).name == "C+E (M3)");
    check("C E E doubled = M3",  analyzeNotes(a, {60, 64, 76}).name == "C+E (M3)");
    check("wide tritone",        analyzeNotes(a, {60, 78}).name == "C+F# (tritone)");
    check("dyad is not a chord", !analyzeNotes(a, {60, 64}).isValid);

    // The two dyads that do imply a chord keep their chord names
    check("C G is still C5",   analyzeNotes(a, {60, 67}).name == "C5");
    check("C F is still F5/C", analyzeNotes(a, {60, 65}).name == "F5");
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
    testAddedTones();
    testBassIdentity();
    testSixthChords();
    testAnalyzeFacts();
    testNoFifthAndDyads();
    testUtilities();

    std::cout << "\n=== Results: " << passed << " passed, " << failed << " failed ===\n";
    return failed > 0 ? 1 : 0;
}
