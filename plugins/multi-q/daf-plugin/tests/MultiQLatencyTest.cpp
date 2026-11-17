// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// #148: Multi-Q 2 reports the latency it renders, from activation on.
//
// Oversampled band saturation (Digital), the oversampled British and Tube cores
// and the limiter lookahead delay the output. A CLAP host reads the latency only
// at activation and restarts the plugin whenever the plugin reports a new value
// while running. So the latency read at activation must equal the delay measured
// in the render (within the half sample an integer latency allows), processing
// with unchanged settings must not move it, and after a settings change the
// value reported while running must survive the host's restart.
//
// Runs the real plugin through DAF's PluginExporter and its host parameters.
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

enum EqType { Digital = 0, British = 2, Tube = 3 };

struct Settings
{
    int eqType = Digital;
    int oversampling = 0;             // 0 = Off, 1 = 2x, 2 = 4x
    int saturatingBands = 0;          // Digital: bands 3, 5 get Tape saturation
    bool limiter = false;
};

class Plugin
{
public:
    Plugin(double sr, int block, const Settings& s)
    {
        // As a DAF wrapper does before it creates the plugin.
        DAF_NAMESPACE::d_nextBufferSize = (uint32_t)block;
        DAF_NAMESPACE::d_nextSampleRate = sr;
        plugin = std::make_unique<DAF_NAMESPACE::PluginExporter>(nullptr, nullptr, nullptr, nullptr);
        apply(s);
        plugin->setSampleRate(sr, true);
        plugin->setBufferSize((uint32_t)block, true);
        plugin->activate();
    }
    void apply(const Settings& s)
    {
        plugin->setParameterValue(kParamEqType, (float)s.eqType);
        plugin->setParameterValue(kParamHqEnabled, (float)s.oversampling);
        plugin->setParameterValue(kParamLimiterEnabled, s.limiter ? 1.0f : 0.0f);
        const int bands[] = {2, 4};   // UI bands 3 and 5
        for (int i = 0; i < 2; ++i)
            plugin->setParameterValue(mqidx::satType(bands[i]), i < s.saturatingBands ? 1.0f : 0.0f);
    }
    void reactivate() { plugin->deactivate(); plugin->activate(); }
    void process(float* left, float* right, int frames)
    {
        const float* in[] = {left, right};
        float* out[] = {left, right};
        plugin->run(in, out, (uint32_t)frames);
    }
    int latency() const { return (int)plugin->getLatency(); }

private:
    std::unique_ptr<DAF_NAMESPACE::PluginExporter> plugin;
};

// One second of low-passed noise at about -30 dBFS: low enough that the
// saturation stays close to linear, band-limited so every oversampling filter
// passes it unchanged and only its delay shows.
std::vector<float> stimulus(double sr)
{
    std::vector<float> x((size_t)sr);
    uint32_t state = 12345u;
    const double a = std::exp(-2.0 * 3.141592653589793 * 3000.0 / sr);
    double y1 = 0.0, y2 = 0.0;
    for (auto& v : x)
    {
        state = state * 1664525u + 1013904223u;
        const double white = (double)(state >> 8) / 16777216.0 - 0.5;
        y1 = a * y1 + (1.0 - a) * white;
        y2 = a * y2 + (1.0 - a) * y1;
        v = (float)(0.15 * y2);
    }
    return x;
}

struct Render { std::vector<float> left; int atActivation = -1, afterRun = -1; };

Render render(double sr, const Settings& s)
{
    constexpr int block = 256;
    Plugin plugin(sr, block, s);
    Render r;
    r.atActivation = plugin.latency();
    r.left = stimulus(sr);
    std::vector<float> right = r.left;
    const int n = (int)r.left.size();
    for (int pos = 0; pos < n; pos += block)
        plugin.process(r.left.data() + pos, right.data() + pos, std::min(block, n - pos));
    r.afterRun = plugin.latency();
    return r;
}

// Delay of y against x (samples, sub-sample by a parabola through the
// cross-correlation peak), searched over 0..400 samples.
double delayOf(const std::vector<float>& x, const std::vector<float>& y, double sr)
{
    const int from = (int)(0.2 * sr), to = (int)(0.9 * sr);
    auto corr = [&](int lag) {
        double c = 0.0;
        for (int i = from; i < to; ++i) c += (double)x[(size_t)i] * y[(size_t)(i + lag)];
        return c;
    };
    int best = 0;
    double bestC = corr(0);
    for (int lag = 1; lag <= 400; ++lag)
    {
        const double c = corr(lag);
        if (c > bestC) { bestC = c; best = lag; }
    }
    if (best == 0) return 0.0;
    const double cm = corr(best - 1), cp = corr(best + 1);
    const double denom = cm - 2.0 * bestC + cp;
    return best + (denom != 0.0 ? 0.5 * (cm - cp) / denom : 0.0);
}

void latencyMatchesRender(double sr, const char* name, Settings s)
{
    Settings reference = s;
    reference.oversampling = 0;
    reference.limiter = false;
    const Render ref = render(sr, reference);
    const Render out = render(sr, s);
    const double delay = delayOf(ref.left, out.left, sr);
    std::printf("  sr=%.0f %-22s latency at activation %3d, after a run %3d; rendered delay %7.3f samples\n",
                sr, name, out.atActivation, out.afterRun, delay);
    check(ref.atActivation == 0 && ref.afterRun == 0, "no oversampling and no limiter report no latency",
          ref.atActivation, 0.0);
    check(std::abs(out.atActivation - delay) <= 0.6, "the latency read at activation is the rendered delay",
          out.atActivation, delay);
    check(out.afterRun == out.atActivation, "processing with unchanged settings keeps the latency (no restart)",
          out.afterRun, out.atActivation);
}

void settingsChangeSurvivesRestart(double sr)
{
    // Turning saturation on while running moves the latency once; the value a
    // host then reads after restarting the plugin is the same, and stays.
    constexpr int block = 256;
    Settings s;
    s.oversampling = 1;
    Plugin plugin(sr, block, s);
    std::vector<float> l(block, 0.0f), r(block, 0.0f);
    plugin.process(l.data(), r.data(), block);
    const int before = plugin.latency();
    s.saturatingBands = 1;
    plugin.apply(s);
    plugin.process(l.data(), r.data(), block);
    const int whileRunning = plugin.latency();
    plugin.reactivate();
    const int afterRestart = plugin.latency();
    plugin.process(l.data(), r.data(), block);
    const int afterRun = plugin.latency();
    std::printf("  sr=%.0f saturation switched on: %d -> %d while running, %d after restart, %d after a run\n",
                sr, before, whileRunning, afterRestart, afterRun);
    check(before == 0 && whileRunning > 0, "switching saturation on reports the new latency", whileRunning, 1.0);
    check(afterRestart == whileRunning && afterRun == whileRunning,
          "the restarted plugin reports the same latency, so the host restarts once", afterRestart, whileRunning);
}
}

int main()
{
    std::printf("Multi-Q 2 latency\n");
    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        Settings s;
        s.oversampling = 1;                          latencyMatchesRender(sr, "Digital 2x, no sat", s);
        s.saturatingBands = 1;                       latencyMatchesRender(sr, "Digital 2x, 1 sat band", s);
        s.saturatingBands = 2;                       latencyMatchesRender(sr, "Digital 2x, 2 sat bands", s);
        s.oversampling = 2; s.saturatingBands = 0;   latencyMatchesRender(sr, "Digital 4x, no sat", s);
        s.saturatingBands = 1;                       latencyMatchesRender(sr, "Digital 4x, 1 sat band", s);
        s.saturatingBands = 2;                       latencyMatchesRender(sr, "Digital 4x, 2 sat bands", s);
        Settings b; b.eqType = British;
        b.oversampling = 1;                          latencyMatchesRender(sr, "British 2x", b);
        b.oversampling = 2;                          latencyMatchesRender(sr, "British 4x", b);
        Settings t; t.eqType = Tube;
        t.oversampling = 1;                          latencyMatchesRender(sr, "Tube 2x", t);
        t.oversampling = 2;                          latencyMatchesRender(sr, "Tube 4x", t);
        Settings l; l.limiter = true;                latencyMatchesRender(sr, "Limiter", l);
        l.oversampling = 1; l.saturatingBands = 2;   latencyMatchesRender(sr, "Limiter + 2x, 2 sat", l);
        settingsChangeSurvivesRestart(sr);
    }
    std::printf("Multi-Q latency: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
