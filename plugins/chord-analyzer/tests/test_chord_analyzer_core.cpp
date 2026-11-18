// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// #183: numeric core preservation against the shipping JUCE model.
#if defined(CHORD_ANALYZER_LEGACY_REFERENCE)
#include "../Source/ChordAnalyzer.h"
using Model = ChordAnalyzer;
#else
#include "../core/ChordAnalyzerCore.hpp"
using namespace duskaudio::chord;
using Model = ChordAnalyzerCore;
#endif

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace
{
bool watchAllocations = false;
std::size_t allocationCalls = 0;
void* allocate(std::size_t bytes)
{
    if (watchAllocations) ++allocationCalls;
    if (void* memory = std::malloc(bytes == 0 ? 1 : bytes)) return memory;
    throw std::bad_alloc();
}
struct Hash
{
    std::uint64_t value = UINT64_C(14695981039346656037);
    void word(std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i) { value ^= (v >> (8 * i)) & 255u; value *= UINT64_C(1099511628211); }
    }
    void floating(float v) { std::uint32_t bits; std::memcpy(&bits, &v, sizeof(bits)); word(bits); }
    void facts(const ChordFacts& f)
    {
        word(f.rootNote); word(f.bassNote); word(static_cast<unsigned>(f.quality));
        word(f.inversion); word(f.slashBass); word(f.isValid); floating(f.confidence);
        word(f.patternIndex); word(f.intervals);
    }
};
int failures = 0;
void check(bool condition, const char* message)
{
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
}

void* operator new(std::size_t bytes) { return allocate(bytes); }
void* operator new[](std::size_t bytes) { return allocate(bytes); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int main(int argc, char** argv)
{
    const bool record = argc == 2 && std::strcmp(argv[1], "--record") == 0;
    if (argc > 1 && !record) return 2;
    // Prove the allocation probe catches a real request. The volatile indirect
    // call prevents a compiler's new/delete elision from hiding the control.
    watchAllocations = true;
    void* (*volatile request)(std::size_t) = ::operator new;
    void* memory = request(8);
    ::operator delete(memory);
    watchAllocations = false;
    check(allocationCalls == 1, "allocation positive control is observed");
    allocationCalls = 0;

    Hash analysis, suggestions, harmonic, patterns;
    int voicings = 0, queries = 0, boundsFailures = 0;
    watchAllocations = true;
    Model model;
    analysis.facts(model.analyzeFacts(nullptr, 0));
    for (int mask = 1; mask < 4096; ++mask)
        for (int bass = 0; bass < 12; ++bass)
        {
            if ((mask & (1 << bass)) == 0) continue;
            int notes[12], count = 1;
            notes[0] = 36 + bass;
            for (int pc = 0; pc < 12; ++pc)
                if (pc != bass && (mask & (1 << pc)) != 0) notes[count++] = 60 + pc;
            analysis.word(mask); analysis.word(bass);
            analysis.facts(model.analyzeFacts(notes, count));
            ++voicings;
        }
    // Pattern indices are part of the detected-label contract, including the
    // unknown/out-of-range result. Freeze them independently of recognition.
    for (int index = -1; index < 64; ++index)
    {
        patterns.word(index); patterns.word(Model::patternPitchClasses(index));
    }
    for (int key = 0; key < 12; ++key)
        for (bool minor : {false, true})
        {
            model.setKey(key, minor);
            for (int root = -1; root < 12; ++root)
                for (int quality = 0; quality <= static_cast<int>(ChordQuality::Unknown); ++quality)
                {
                    ChordFacts facts;
                    facts.isValid = root >= 0; facts.rootNote = root;
                    facts.quality = static_cast<ChordQuality>(quality);
                    harmonic.word(static_cast<unsigned>(model.getHarmonicFunction(root, facts.quality)));
                    for (auto level : {SuggestionCategory::Basic, SuggestionCategory::Intermediate,
                                       SuggestionCategory::Advanced})
                        for (int capacity : {0, 1, 4, Model::maxSuggestions})
                        {
                            std::array<SuggestionFacts, Model::maxSuggestions + 2> out;
                            for (auto& value : out) { value.rootNote = -901; value.commonality = -10.0f; }
                            const int count = model.getSuggestionFacts(facts, level, out.data() + 1, capacity);
                            if (count < 0 || count > capacity) ++boundsFailures;
                            for (int i = 0; i < int(out.size()); ++i)
                                if ((i == 0 || i > count) && (out[i].rootNote != -901 || out[i].commonality != -10.0f
                                    || out[i].quality != ChordQuality::Unknown || out[i].category != SuggestionCategory::Basic))
                                    ++boundsFailures;
                            suggestions.word(count);
                            for (int i = 0; i < count && i < capacity; ++i)
                            {
                                const auto& value = out[i + 1];
                                suggestions.word(value.rootNote); suggestions.word(static_cast<unsigned>(value.quality));
                                suggestions.word(static_cast<unsigned>(value.category)); suggestions.floating(value.commonality);
                            }
                            ++queries;
                        }
                }
        }
#if !defined(CHORD_ANALYZER_LEGACY_REFERENCE)
    model.setKey(11, true);
    model.reset();
    check(model.getKeyRoot() == 0 && !model.isMinorKey(), "reset clears all owned key state");
#endif
    watchAllocations = false;
    check(allocationCalls == 0, "numeric model construction and all queries allocate nothing");
    check(boundsFailures == 0, "bounded suggestions preserve every output guard");
    check(voicings == 24576 && queries == 127296, "complete pitch/bass and key/quality/level/capacity domains were visited");
    std::printf("Chord core: %d voicings, %d suggestion queries, %zu allocation calls, %d bounds failures\n",
                voicings, queries, allocationCalls, boundsFailures);
    std::printf("analysis=%016llx suggestions=%016llx harmonic=%016llx patterns=%016llx\n",
                static_cast<unsigned long long>(analysis.value), static_cast<unsigned long long>(suggestions.value),
                static_cast<unsigned long long>(harmonic.value), static_cast<unsigned long long>(patterns.value));
    if (!record)
    {
        // Frozen from independent shipping source a59703ee before extraction.
        // Both its normal and shipping fast-math builds produced these hashes.
        check(analysis.value == UINT64_C(0xe0b7a752f99f391c), "analysis matches the shipping model over every pitch/bass set");
        check(suggestions.value == UINT64_C(0xfd8510f44f87e065), "suggestions match shipping contents, order and truncation");
        check(harmonic.value == UINT64_C(0xae072c5cff6a4fb5), "harmonic functions match every shipping key/root/quality result");
        check(patterns.value == UINT64_C(0x637a2e486708f584), "pattern index masks retain the shipping label contract");
    }
    std::printf("Chord core: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
