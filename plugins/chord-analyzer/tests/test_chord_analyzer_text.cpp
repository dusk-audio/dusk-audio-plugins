// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// #183: freeze the shipping formatter independently before removing JUCE.
#include <string>
#include <string_view>
#include "AllocationProbe.hpp"
#if defined(CHORD_ANALYZER_LEGACY_REFERENCE)
#include "../Source/ChordAnalyzer.h"
using Model = ChordAnalyzer;
using Chord = ChordInfo;
static std::string utf8(const juce::String& s) { return s.toStdString(); }
static int parseNote(std::string_view s) { return Model::nameToNote(juce::String::fromUTF8(s.data(), int(s.size()))); }
#else
#include "../core/ChordAnalyzerText.hpp"
using namespace duskaudio::chord;
using Model = ChordAnalyzerText;
using Chord = ChordDescription;
static std::string_view utf8(const std::string& s) { return s; }
static int parseNote(std::string_view s) { return Model::nameToNote(s); }
#endif

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace
{
struct Hash
{
    std::uint64_t value = UINT64_C(14695981039346656037);
    void word(std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i) { value ^= (v >> (8 * i)) & 255u; value *= UINT64_C(1099511628211); }
    }
    template<class Text> void text(const Text& source)
    {
        const auto s = utf8(source);
        word(static_cast<std::uint32_t>(s.size()));
        for (char c : s) { value ^= static_cast<unsigned char>(c); value *= UINT64_C(1099511628211); }
    }
    void floating(float v) { std::uint32_t bits; std::memcpy(&bits, &v, sizeof(bits)); word(bits); }
    void chord(const Chord& c)
    {
        text(c.name); text(c.romanNumeral); text(c.extensions);
        word(static_cast<unsigned>(c.function)); word(static_cast<unsigned>(c.rootNote));
        word(static_cast<unsigned>(c.bassNote)); word(static_cast<unsigned>(c.quality));
        word(static_cast<unsigned>(c.inversion)); word(c.slashBass); word(c.isValid); floating(c.confidence);
        word(static_cast<unsigned>(c.midiNotes.size()));
        for (int n : c.midiNotes) word(static_cast<unsigned>(n));
    }
};
int failures = 0;
void check(bool ok, const char* message)
{
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
int encode(const Chord& c, bool inversions)
{
#if defined(CHORD_ANALYZER_LEGACY_REFERENCE)
    return Model::encodeLabel(c, inversions);
#else
    return Model::encodeLabel(c, c.midiNotes.data(), int(c.midiNotes.size()), inversions);
#endif
}
}

int main(int argc, char** argv)
{
    const bool record = argc == 2 && std::strcmp(argv[1], "--record") == 0;
    if (argc > 1 && !record) return 2;
    Model model;
    Hash analysis, suggestions, codec, utilities;
    int analyses = 0, queries = 0, labels = 0, roundTripFailures = 0;
    for (int code = -1024; code <= Model::maxLabelCode + 1024; ++code)
    {
        const auto label = Model::decodeLabel(code);
        const int parsed = Model::labelToCode(label);
        codec.word(static_cast<unsigned>(code)); codec.text(label); codec.word(static_cast<unsigned>(parsed));
        if (Model::decodeLabel(parsed) != label) ++roundTripFailures;
        ++labels;
    }
    for (int key = 0; key < 12; ++key)
        for (bool minor : {false, true})
        {
            model.setKey(key, minor);
            analysis.text(model.getKeyName());
            const auto observe = [&](const std::vector<int>& notes)
            {
                const auto chord = model.analyze(notes);
                analysis.chord(chord);
                for (bool inversions : {false, true})
                {
                    const int code = encode(chord, inversions);
                    analysis.word(static_cast<unsigned>(code)); analysis.text(Model::decodeLabel(code));
                }
                ++analyses;
            };
            observe({});
            for (int n = 0; n < 128; ++n)
            {
                observe({n}); observe({n, n});
                if (n + 12 < 128) observe({n + 12, n});
            }
            observe({67, 64, 60, 60});
            std::vector<int> maximum;
            for (int n = 127; n >= 0; --n) maximum.push_back(n);
            observe(maximum);
            for (int mask = 1; mask < 4096; ++mask)
                for (int bass = 0; bass < 12; ++bass)
                {
                    if ((mask & (1 << bass)) == 0) continue;
                    std::vector<int> notes{36 + bass};
                    for (int pc = 0; pc < 12; ++pc)
                        if (pc != bass && (mask & (1 << pc)) != 0) notes.push_back(60 + pc);
                    observe(notes);
                }
            for (int root = -1; root < 12; ++root)
                for (int quality = 0; quality <= int(ChordQuality::Unknown); ++quality)
                    for (auto level : {SuggestionCategory::Basic, SuggestionCategory::Intermediate, SuggestionCategory::Advanced})
                    {
                        Chord chord;
                        chord.isValid = root >= 0; chord.rootNote = root; chord.quality = static_cast<ChordQuality>(quality);
                        const auto out = model.getSuggestions(chord, level);
                        suggestions.word(static_cast<unsigned>(out.size()));
                        for (const auto& s : out)
                        {
                            suggestions.text(s.romanNumeral); suggestions.text(s.chordName); suggestions.text(s.reason);
                            suggestions.word(static_cast<unsigned>(s.category)); suggestions.floating(s.commonality);
                        }
                        ++queries;
                    }
        }
    for (bool flats : {false, true})
    {
        for (int n = -128; n < 256; ++n) utilities.text(Model::noteToName(n, flats));
        for (int n = -24; n < 36; ++n) utilities.text(Model::pitchClassToName(n, flats));
    }
    for (int q = -1; q <= int(ChordQuality::Unknown) + 1; ++q)
    {
        utilities.text(Model::qualityToString(static_cast<ChordQuality>(q)));
        utilities.text(Model::qualityToSuffix(static_cast<ChordQuality>(q)));
    }
    for (int f = -1; f <= int(HarmonicFunction::Unknown) + 1; ++f)
        utilities.text(Model::functionToString(static_cast<HarmonicFunction>(f)));
    for (const char* s : {"", "-", "C", " c ", "C#4", "Db", "bb4", "B#", "Fb", "E#", "Cs", "cs9", " gS9 ",
                           "\tA#\r\n", "H", "Cgarbage", "G/B", "50000", "\xc2\xb0", "C\xe2\x99\xaf"})
        utilities.word(static_cast<unsigned>(parseNote(s)));
    model.setKey(11, true); model.reset();
    check(utf8(model.getKeyName()) == "C Major", "reset restores the text model's key state");
    auto tonic = model.analyze({60, 64, 67});
    check(tonic != model.analyze({64, 67, 72}), "description identity distinguishes a changed bass");
    auto sameIdentity = tonic;
    sameIdentity.inversion = 6;
    sameIdentity.extensions = "changed";
    check(tonic == sameIdentity, "description identity preserves the shipping four-field comparison");
    sameIdentity.name += "changed";
    check(tonic != sameIdentity, "description identity distinguishes a changed name");
    const std::array<Chord, 7> encodeCases{
        model.analyze({}), model.analyze({60}), model.analyze({60, 64}), model.analyze({60, 72}),
        model.analyze({60, 60}), model.analyze({60, 61, 62}), model.analyze({64, 67, 72})};
    check(ChordAllocationProbe::positiveControl(), "allocation probe detects an actual request");
    std::uint64_t encodingSum = 0;
    ChordAllocationProbe::watchAllocations = true;
    for (const auto& c : encodeCases)
        for (bool inversions : {false, true}) encodingSum += static_cast<unsigned>(encode(c, inversions));
    ChordAllocationProbe::watchAllocations = false;
    check(encodingSum != 0 && ChordAllocationProbe::allocationCalls == 0,
          "numeric label encoding allocates nothing across all naming branches");
#if !defined(CHORD_ANALYZER_LEGACY_REFERENCE)
    const int note = 60;
    check(Model::encodeLabel(tonic, nullptr, 128, true) == 0
              && Model::encodeLabel(tonic, &note, -1, true) == 0,
          "raw-note label encoder rejects null storage and negative counts");
#endif
    check(analyses == 598824 && queries == 31824 && labels == Model::maxLabelCode + 2049,
          "complete voicing/key, suggestion and encoded-label domains were visited");
    check(roundTripFailures == 0, "every label, including out-of-range probes, survives inverse conversion");
    std::printf("Chord text: %d analyses, %d suggestion queries, %d label probes, %d inverse failures\n",
                analyses, queries, labels, roundTripFailures);
    std::printf("analysis=%016llx suggestions=%016llx codec=%016llx utilities=%016llx\n",
                static_cast<unsigned long long>(analysis.value), static_cast<unsigned long long>(suggestions.value),
                static_cast<unsigned long long>(codec.value), static_cast<unsigned long long>(utilities.value));
    if (!record)
    {
        // Independently captured from frozen 7a6d4bb7 with normal and shipping
        // fast-math flags. UTF-8 bytes and floating-point bits are both frozen.
        check(analysis.value == UINT64_C(0xb5bc452e3752362f), "all descriptions/encodings match the shipping formatter");
        check(suggestions.value == UINT64_C(0x959ed30c4b170715), "suggestion spelling, reasons, tiers and order match shipping");
        check(codec.value == UINT64_C(0x22f0a9b7cf047b10), "all encoded labels and canonical inverses match shipping");
        check(utilities.value == UINT64_C(0xb9be7d745a57957d), "note/quality/function utilities retain their shipping results");
    }
    std::printf("Chord text: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
