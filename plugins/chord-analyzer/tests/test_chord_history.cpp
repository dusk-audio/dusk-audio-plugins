// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#include "../core/ChordHistory.hpp"
#include <cstdio>
#include <deque>

int main()
{
    using namespace duskaudio::chord;
    ChordAnalyzerText model;
    ChordHistory history;
    std::deque<ChordDescription> expected;
    int failures = 0, comparisons = 0;
    const auto equal = [](const ChordDescription& a, const ChordDescription& b)
    {
        return a.name == b.name && a.romanNumeral == b.romanNumeral && a.extensions == b.extensions
            && a.function == b.function && a.rootNote == b.rootNote && a.bassNote == b.bassNote
            && a.quality == b.quality && a.inversion == b.inversion && a.slashBass == b.slashBass
            && a.isValid == b.isValid && a.confidence == b.confidence && a.midiNotes == b.midiNotes;
    };
    const auto compare = [&]
    {
        const auto actual = history.snapshot();
        bool correct = actual.size() == expected.size();
        if (correct) for (std::size_t i = 0; i < actual.size(); ++i) correct &= equal(actual[i], expected[i]);
        if (!correct && ++failures <= 5) std::fprintf(stderr, "FAIL: complete oldest-to-newest history differs from deque oracle\n");
        ++comparisons;
    };
    const auto append = [&](const ChordDescription& chord)
    {
        bool added = false;
        // Independent deque oracle, with the explicit shipping identity fields.
        bool duplicate = false;
        if (!expected.empty())
        {
            const auto& last = expected.back();
            duplicate = last.name == chord.name && last.rootNote == chord.rootNote
                     && last.quality == chord.quality && last.bassNote == chord.bassNote;
        }
        if (chord.isValid && !chord.name.empty() && chord.name != "-" && !duplicate)
        {
            expected.push_back(chord);
            if (expected.size() > 32) expected.pop_front();
            added = true;
        }
        if (history.append(chord) != added) ++failures;
        compare();
    };
    compare();
    for (int i = 0; i < 97; ++i)
    {
        const int root = i % 12;
        const auto chord = model.analyze({48+root,52+root,55+root});
        append(chord); append(chord); append(model.analyze({}));
    }
    history.clear(); expected.clear(); compare();
    const auto tonic = model.analyze({60,64,67});
    append(tonic); append(model.analyze({64,67,72}));
    auto same = model.analyze({64,67,72}); same.romanNumeral = "different"; same.inversion = 6;
    append(same); // fields outside the retained identity must not create a duplicate chip
    same.name = "-"; append(same);
    same.name = ""; append(same);
    same.name = "invalid"; same.isValid = false; append(same);
    std::uint32_t random = 0x183005u;
    for (int i = 0; i < 10000; ++i)
    {
        random = random * 1664525u + 1013904223u;
        if (i % 257 == 0) { history.clear(); expected.clear(); compare(); }
        const int root = int((random >> 8) % 12);
        auto chord = model.analyze({48+root,52+root,55+root});
        if (random % 11 == 0) chord.isValid = false;
        if (random % 17 == 0) chord.bassNote = (root + 4) % 12;
        append(chord);
    }
    history.clear(); expected.clear(); compare();
    std::printf("Chord history: %d full comparisons, %d failures\n", comparisons, failures);
    return failures == 0 ? 0 : 1;
}
