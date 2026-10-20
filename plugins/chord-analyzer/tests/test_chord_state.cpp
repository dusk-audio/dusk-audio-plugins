// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#include "ChordAnalyzerState.hpp"
#include <cstdio>
#include <limits>
#include <thread>

using namespace duskaudio::chord;
namespace
{
int checks = 0, failures = 0;
void check(bool ok, const char* message)
{ ++checks; if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); } }
}

int main()
{
    InputState state;
    const InputValues defaults{{0, 0, 2, 1, 1}}, saved{{6, 1, 1, 0, 0}};
    const std::string fixture = "ChordAnalyzer2:1\nkeyRoot=6\nkeyMode=1\nsuggestionLevel=1\nshowInversions=0\nrespectSustain=0\n";
    check(state.values() == defaults, "default inputs match the shipping parameter table");
    check(encodeInputState(saved) == fixture, "saved schema contains exactly five named integer inputs");
    InputValues decoded{};
    check(decodeInputState(fixture, decoded) && decoded == saved, "independent nondefault fixture decodes");
    for (int root = 0; root < 12; ++root)
        for (int mode = 0; mode < 2; ++mode)
            for (int level = 0; level < 3; ++level)
                for (int inversions = 0; inversions < 2; ++inversions)
                    for (int sustain = 0; sustain < 2; ++sustain)
                    {
                        const InputValues values{{float(root), float(mode), float(level), float(inversions), float(sustain)}};
                        check(decodeInputState(encodeInputState(values), decoded) && decoded == values,
                              "every valid input combination round trips through the codec");
                        check(state.restore(values) && state.values() == values,
                              "every valid input combination publishes without bit overlap");
                    }
    auto rejects = [&](const std::string& invalid) {
        decoded = saved;
        check(!decodeInputState(invalid, decoded) && decoded == saved, "rejection leaves the destination untouched");
    };
    rejects(""); rejects(fixture.substr(0, fixture.size() - 1)); rejects(fixture + "junk\n");
    rejects("ChordAnalyzer2:2\n" + fixture.substr(fixture.find('\n') + 1));
    rejects(fixture + "detectedRoot=1\n"); rejects(fixture + "unknown=0\n");
    for (unsigned i = 0; i < inputParameterCount; ++i)
    {
        const auto key = std::string(parameterSpecs[i].symbol) + "=";
        const auto begin = fixture.find(key), end = fixture.find('\n', begin);
        rejects(fixture.substr(0, begin) + fixture.substr(end + 1));
        rejects(fixture + fixture.substr(begin, end + 1 - begin));
        for (const auto& value : std::array<std::string, 10>{{"", "nan", "inf", "0.5", "-1", "1junk", " 1", "1 ",
                                  "9999999999999999999999", std::to_string(int(parameterSpecs[i].maximum) + 1)}})
            rejects(fixture.substr(0, begin) + key + value + fixture.substr(end));
        for (float value : {-1.0f, 0.5f, parameterSpecs[i].maximum + 1, std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::quiet_NaN()})
        {
            auto invalid = saved; invalid[i] = value;
            state.restore(saved);
            check(!state.restore(invalid) && state.values() == saved, "invalid complete recalls never partially publish");
        }
        state.restore(defaults);
        check(state.set(i, parameterSpecs[i].maximum) && state.values()[i] == parameterSpecs[i].maximum,
              "every writable input can still be automated after a complete recall");
        auto expected = defaults; expected[i] = parameterSpecs[i].maximum;
        check(state.values() == expected, "individual automation preserves all sibling inputs");
    }
    check(!state.set(PARAM_DETECTED_ROOT, 1) && !state.set(PARAM_COUNT, 1), "host edits cannot write detection outputs");
    const InputValues a{{0, 1, 0, 1, 0}}, b{{11, 0, 2, 0, 1}};
    state.restore(a);
    std::atomic<bool> ready{false}, done{false};
    std::atomic<unsigned> observed{0};
    std::thread writer([&] {
        while (!ready.load(std::memory_order_acquire)) std::this_thread::yield();
        for (unsigned i = 0; i < 200000; ++i)
        {
            state.restore((i & 1) ? a : b);
            // Require reader progress even on a single-core runner; a writer
            // finishing in one timeslice must not make the check vacuous.
            if (i % 1000 == 0)
            {
                const auto previous = observed.load(std::memory_order_acquire);
                while (observed.load(std::memory_order_acquire) == previous) std::this_thread::yield();
            }
        }
        done.store(true, std::memory_order_release);
    });
    unsigned reads = 0, torn = 0;
    ready.store(true, std::memory_order_release);
    do {
        const auto values = state.values(); ++reads; torn += values != a && values != b;
        observed.store(reads, std::memory_order_release);
    }
    while (!done.load(std::memory_order_acquire));
    writer.join();
    check(reads > 1 && torn == 0, "concurrent complete recalls never expose mixed input sets");
    std::printf("Chord state: %d checks, %d failures; %u concurrent snapshots\n", checks, failures, reads);
    return failures ? 1 : 0;
}
