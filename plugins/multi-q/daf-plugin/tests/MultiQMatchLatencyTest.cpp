// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// #148: Match mode reports its correction latency from activation on.
//
// The Match correction runs through a convolver that delays the output by its
// latency (128 samples for the minimum-phase FIR the editor computes). A CLAP
// host reads the latency only at activation, and every activation rebuilds the
// convolver from the saved correction, which the audio thread adopts at the
// start of its first block. So the latency read at activation must already be
// the correction's, match what the first block plays, stay put while running,
// and survive further restarts. Clearing the correction returns it to 0 only
// once the convolver has faded out, so the reported value never runs behind
// what plays: after every block it equals the delay the next block renders.
//
// Runs the real plugin through DAF's PluginExporter, its host parameters and
// the editor's Match bridge. Match Apply is 0 %, so the correction is a unit
// impulse and the output is exactly the input, delayed by the convolver.
#include "../MultiQPlugin.cpp"
#include "src/DafPluginInternal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

namespace
{
int checks = 0, failures = 0;
void check(bool ok, const char* what, double measured, double expected)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s (measured %.3f, expected %.3f)\n", what, measured, expected);
    }
}

constexpr int kBlock = 256;
constexpr int kHop = duskaudio::MultiQMatch::CONV_HOP;   // the convolver's latency

class Plugin
{
public:
    explicit Plugin(double sr)
    {
        DAF_NAMESPACE::d_nextBufferSize = (uint32_t)kBlock;   // as a DAF wrapper does
        DAF_NAMESPACE::d_nextSampleRate = sr;
        plugin = std::make_unique<DAF_NAMESPACE::PluginExporter>(nullptr, nullptr, nullptr, nullptr);
        set(kParamEqType, 1.0f);       // Match
        set(kParamMatchApply, 0.0f);   // 0 %: a unit-impulse correction
        plugin->setSampleRate(sr, true);
        plugin->setBufferSize((uint32_t)kBlock, true);
        plugin->activate();
    }
    void set(uint32_t index, float value) { plugin->setParameterValue(index, value); }
    void reactivate() { plugin->deactivate(); plugin->activate(); }
    int latency() const { return (int)plugin->getLatency(); }
    void* instance() { return plugin->getInstancePointer(); }

    // One block of noise; returns the delay (samples) the block was rendered
    // with: the shift at which its output equals its input, or -1 if none.
    int block()
    {
        std::vector<float> in((size_t)kBlock), l, r;
        for (auto& v : in) v = noise();
        l = in; r = in;
        const float* ins[] = {l.data(), r.data()};
        float* outs[] = {l.data(), r.data()};
        plugin->run(ins, outs, (uint32_t)kBlock);
        history.insert(history.end(), in.begin(), in.end());
        const size_t start = history.size() - (size_t)kBlock;
        for (int shift : {0, kHop})
        {
            if ((size_t)shift > start) continue;   // too early in the render to tell
            double worst = 0.0;
            for (int i = 0; i < kBlock; ++i)
                worst = std::max(worst, (double)std::abs(l[(size_t)i] - history[start + (size_t)i - (size_t)shift]));
            if (worst < 1e-4) return shift;
        }
        return -1;
    }
    void blocks(int count) { for (int i = 0; i < count; ++i) block(); }

    // Learn a correction the way the editor does (both spectra, then compute).
    bool learn()
    {
        void* p = instance();
        multiQMatchStartLearnCurrent(p, true);    blocks(64);
        multiQMatchStartLearnReference(p, true);  blocks(64);
        multiQMatchStartLearnReference(p, false); blocks(2);
        const bool ok = multiQMatchCompute(p);
        return ok && multiQMatchHasCorrection(p);
    }
    // After an activation the convolver starts empty: what came before is silence.
    void restartHistory() { history.assign((size_t)kHop, 0.0f); }

private:
    float noise()
    {
        state = state * 1664525u + 1013904223u;
        return 0.1f * ((float)(state >> 8) / 8388608.0f - 1.0f);
    }
    std::unique_ptr<DAF_NAMESPACE::PluginExporter> plugin;
    std::vector<float> history;
    uint32_t state = 0x148u;
};

void learnedCorrection(double sr)
{
    Plugin plugin(sr);
    check(plugin.latency() == 0, "Match without a correction reports no latency", plugin.latency(), 0);
    check(plugin.learn(), "the editor bridge learns and computes a correction", 0, 1);
    plugin.block();
    const int running = plugin.latency();
    check(running == kHop, "the computed correction reports the convolver latency", running, kHop);
    for (int restart = 0; restart < 3; ++restart)
    {
        plugin.reactivate();
        plugin.restartHistory();
        const int atActivation = plugin.latency();
        const int firstDelay = plugin.block();
        const int secondDelay = plugin.block();
        const int afterRun = plugin.latency();
        std::printf("  sr=%.0f restart %d: latency at activation %d, rendered delay %d then %d, after a run %d\n",
                    sr, restart, atActivation, firstDelay, secondDelay, afterRun);
        check(atActivation == kHop, "the latency read at activation is the correction's", atActivation, kHop);
        check(firstDelay == atActivation && secondDelay == atActivation,
              "the first blocks render with the latency read at activation", firstDelay, atActivation);
        check(afterRun == atActivation, "running keeps the latency (no restart request)", afterRun, atActivation);
    }
}

void clearing(double sr)
{
    // After every block the reported latency must equal the delay the next
    // block renders: the convolver keeps its delay through its 10 ms fade-out.
    Plugin plugin(sr);
    plugin.learn();
    plugin.blocks(4);
    multiQMatchClear(plugin.instance());
    int reported = plugin.latency(), mismatches = 0, blocksAt128 = 0;
    for (int b = 0; b < 12; ++b)
    {
        const int delay = plugin.block();
        mismatches += delay != reported;
        blocksAt128 += delay == kHop;
        reported = plugin.latency();
    }
    std::printf("  sr=%.0f clear: %d blocks still at %d samples while fading, then %d; mismatched blocks %d\n",
                sr, blocksAt128, kHop, reported, mismatches);
    check(mismatches == 0, "after each block the latency equals the delay the next block plays", mismatches, 0);
    check(reported == 0, "a cleared correction reports no latency", reported, 0);
    plugin.reactivate();
    check(plugin.latency() == 0, "a cleared correction reports no latency after a restart", plugin.latency(), 0);
}

void switching(double sr)
{
    // Match off and on again, with the limiter, and with saturating bands that
    // Match leaves out.
    Plugin plugin(sr);
    plugin.learn();
    plugin.block();
    plugin.set(kParamEqType, 0.0f);   // Digital
    plugin.block();
    const int digitalRunning = plugin.latency();
    plugin.reactivate();
    const int digitalActivation = plugin.latency();
    plugin.set(kParamEqType, 1.0f);   // Match again
    plugin.block();
    const int matchRunning = plugin.latency();
    plugin.reactivate();
    const int matchActivation = plugin.latency();
    std::printf("  sr=%.0f Match off: %d running, %d at activation; on again: %d running, %d at activation\n",
                sr, digitalRunning, digitalActivation, matchRunning, matchActivation);
    check(digitalRunning == 0 && digitalActivation == 0, "turning Match off reports no latency", digitalActivation, 0);
    check(matchRunning == kHop && matchActivation == kHop, "turning Match on again reports the correction",
          matchActivation, kHop);

    const int lookahead = (int)std::max(1.0, sr * 0.001);   // the limiter's ~1 ms
    plugin.set(kParamLimiterEnabled, 1.0f);
    plugin.set(kParamHqEnabled, 1.0f);                       // 2x
    plugin.set(mqidx::satType(2), 1.0f);                     // bands 3 and 5 saturate,
    plugin.set(mqidx::satType(4), 1.0f);                     // which Match leaves out
    plugin.block();
    const int combinedRunning = plugin.latency();
    plugin.reactivate();
    const int combinedActivation = plugin.latency();
    plugin.block(); plugin.block();
    const int combinedAfterRun = plugin.latency();
    std::printf("  sr=%.0f Match + limiter + saturating bands at 2x: %d running, %d at activation, %d after a run\n",
                sr, combinedRunning, combinedActivation, combinedAfterRun);
    check(combinedActivation == kHop + lookahead && combinedRunning == combinedActivation
          && combinedAfterRun == combinedActivation,
          "Match with the limiter reports correction plus lookahead; Match leaves the bands' saturation out",
          combinedActivation, kHop + lookahead);
}
}

int main()
{
    std::printf("Multi-Q 2 Match latency\n");
    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        learnedCorrection(sr);
        clearing(sr);
        switching(sr);
    }
    std::printf("Multi-Q Match latency: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
