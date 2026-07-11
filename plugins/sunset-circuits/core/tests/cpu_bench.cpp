// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// cpu_bench — offline worst-case CPU profiler for the Sunset Circuits core.
//
//   cpu_bench [scenario] [seconds]
//
// Measures engine cost as a percentage of real time at 48 kHz with 512-frame
// blocks. Only the processBlock() loop is timed: prepare(), note-on, and a
// warm-up pass (to reach steady state — voices ramped, FX delay lines filled)
// are all excluded. A figure of 100% means "exactly real time"; lower is
// faster than real time, so headroom = 100/pct voices' worth of the same load.
//
// Scenarios (run all with no argument):
//   prism    (a) worst case: 8-voice Prism FM + full FX + 4x oversampling
//   cosmos   (b) 6-voice Cosmos, 8x unison (poly auto-reduced) + chorus + FX, 2x
//   oracle   (c) typical:   4-voice Oracle pad + reverb, 2x
//   acid     (d) Acid sequencer + FX, 2x

#include "MultiSynthDSP.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using msynth::MultiSynthDSP;

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int    kBlockSize  = 512;

void set(MultiSynthDSP& s, const char* name, float v)
{
    const int idx = MultiSynthDSP::paramIndexForName(name);
    if (idx < 0) { std::fprintf(stderr, "cpu_bench: unknown param '%s'\n", name); std::exit(2); }
    s.setParameter(idx, v);
}

struct Result { const char* label; double pct; double perBlockUs; double worstBlockUs; };

// Run one scenario. `setup` configures params; `notes` are held for the whole
// render; `arpLike` scenarios generate their own notes so a single held root is
// enough. Returns CPU as a percentage of real time over `seconds` of audio.
Result run(const char* label, double seconds,
           void (*setup)(MultiSynthDSP&),
           const std::vector<int>& notes)
{
    MultiSynthDSP synth;
    synth.prepare(kSampleRate, kBlockSize);
    setup(synth);
    synth.setTempo(120.0, true);
    for (int n : notes) synth.noteOn(n, 1.0f);

    std::vector<float> bufL((size_t)kBlockSize), bufR((size_t)kBlockSize);

    // Warm-up: 0.5 s to reach steady state (envelopes open, FX lines fill, the
    // oversampled voice path is re-prepared on the first block). Untimed.
    const int warmBlocks = (int)(0.5 * kSampleRate / kBlockSize);
    for (int b = 0; b < warmBlocks; ++b)
        synth.processBlock(bufL.data(), bufR.data(), kBlockSize);

    const int timedBlocks = (int)(seconds * kSampleRate / kBlockSize);
    double worstUs = 0.0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < timedBlocks; ++b)
    {
        const auto b0 = std::chrono::steady_clock::now();
        synth.processBlock(bufL.data(), bufR.data(), kBlockSize);
        const auto b1 = std::chrono::steady_clock::now();
        const double us = std::chrono::duration<double, std::micro>(b1 - b0).count();
        if (us > worstUs) worstUs = us;
    }
    const auto t1 = std::chrono::steady_clock::now();

    const double wallSec  = std::chrono::duration<double>(t1 - t0).count();
    const double audioSec = (double)timedBlocks * kBlockSize / kSampleRate;
    const double pct      = wallSec / audioSec * 100.0;
    const double perBlock = wallSec / timedBlocks * 1e6;
    return { label, pct, perBlock, worstUs };
}

//==============================================================================
// Scenario setups.

// (a) worst case: 8-voice Prism (4-op FM) + drive + chorus + delay + reverb, 4x.
void setupPrism(MultiSynthDSP& s)
{
    set(s, "mode", (float)msynth::SynthMode::Prism);
    set(s, "oversampling", 2);          // index 2 == 4x
    set(s, "driveOn", 1);  set(s, "chorusOn", 1);
    set(s, "delayOn", 1);  set(s, "reverbOn", 1);
    set(s, "prismFB", 0.6f);            // op feedback = extra math per sample
}

// (b) 6-voice Cosmos, 8x unison (poly auto-reduces to 2 -> 16 osc voices) +
// chorus + full FX, 2x.
void setupCosmos(MultiSynthDSP& s)
{
    set(s, "mode", (float)msynth::SynthMode::Cosmos);
    set(s, "oversampling", 1);          // index 1 == 2x
    set(s, "unisonVoices", 8);
    set(s, "cosmosChorus", 3);          // dual chorus (I+II)
    set(s, "chorusOn", 1); set(s, "driveOn", 1);
    set(s, "delayOn", 1);  set(s, "reverbOn", 1);
}

// (c) typical: 4-voice Oracle pad + reverb, 2x.
void setupOracle(MultiSynthDSP& s)
{
    set(s, "mode", (float)msynth::SynthMode::Oracle);
    set(s, "oversampling", 1);          // 2x
    set(s, "reverbOn", 1);
}

// (d) Acid: diode-ladder mono + 16-step sequencer running + drive + delay, 2x.
void setupAcid(MultiSynthDSP& s)
{
    set(s, "mode", (float)msynth::SynthMode::Acid);
    set(s, "oversampling", 1);          // 2x
    set(s, "arpOn", 1);                 // enables the pattern sequencer in mode 5
    set(s, "driveOn", 1); set(s, "delayOn", 1); set(s, "reverbOn", 1);
}
} // namespace

int main(int argc, char** argv)
{
    const std::string which = argc > 1 ? argv[1] : "all";
    const double seconds = argc > 2 ? std::atof(argv[2]) : 10.0;

    std::printf("Sunset Circuits CPU profile  (48 kHz, 512-frame blocks, %.0f s steady-state)\n",
                seconds);
    std::printf("%-42s  %8s  %10s  %10s\n", "scenario", "%rt", "us/block", "worst us");
    std::printf("%-42s  %8s  %10s  %10s\n",
                "------------------------------------------", "-----", "--------", "--------");

    struct Scenario { const char* key; const char* label; void (*setup)(MultiSynthDSP&); std::vector<int> notes; };
    const std::vector<Scenario> scenarios = {
        { "prism",  "(a) 8-voice Prism FM + full FX + 4x OS",       setupPrism,  { 48, 52, 55, 59, 60, 64, 67, 71 } },
        { "cosmos", "(b) 6-voice Cosmos 8x unison + chorus + FX 2x", setupCosmos, { 48, 52, 55, 59, 60, 64 } },
        { "oracle", "(c) 4-voice Oracle pad + reverb 2x OS",         setupOracle, { 48, 52, 55, 59 } },
        { "acid",   "(d) Acid sequencer + FX 2x OS",                 setupAcid,   { 36 } },
    };

    // budget-per-block at 512/48k for reference
    const double blockBudgetUs = (double)kBlockSize / kSampleRate * 1e6;
    std::vector<Result> results;
    for (const auto& sc : scenarios)
    {
        if (which != "all" && which != sc.key) continue;
        results.push_back(run(sc.label, seconds, sc.setup, sc.notes));
        const Result& r = results.back();
        std::printf("%-42s  %7.2f%%  %9.1f  %9.1f\n", r.label, r.pct, r.perBlockUs, r.worstBlockUs);
    }
    std::printf("\n(block budget at 512/48kHz = %.1f us; %%rt < 100 means faster than real time)\n",
                blockBudgetUs);
    return 0;
}
