// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// #148: Multi-Q 2 reports the latency it renders, from activation on.
//
// Oversampled band saturation (Digital), the oversampled British and Tube cores
// and the limiter lookahead delay the output. A CLAP host reads the latency only
// at activation and restarts the plugin whenever the plugin reports a new value
// while running. So the latency read at activation must equal the delay measured
// in the render (within the half sample an integer latency allows), processing
// with unchanged settings must not move it, and after a settings change the
// value reported while running must survive the host's restart. A saturating
// band routed to one channel, or to Mid or Side, must delay both channels as
// the latency says, or the channels drift apart.
//
// Runs the real plugin through DAF's PluginExporter and its host parameters.
#include "../MultiQPlugin.cpp"
#include "src/DafPluginInternal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
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
    float drive = -1.0f;              // saturation drive; < 0 keeps the default
    int routing[2] = {0, 0};          // bands 3, 5: 0 Global, 1 Stereo, 2 Left, 3 Right, 4 Mid, 5 Side
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
        {
            plugin->setParameterValue(mqidx::satType(bands[i]), i < s.saturatingBands ? 1.0f : 0.0f);
            if (s.drive >= 0.0f) plugin->setParameterValue(mqidx::satDrive(bands[i]), s.drive);
            plugin->setParameterValue(mqidx::routing(bands[i]), (float)s.routing[i]);
        }
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
std::vector<float> stimulus(double sr, uint32_t seed = 12345u)
{
    std::vector<float> x((size_t)sr);
    uint32_t state = seed;
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

struct Render { std::vector<float> left, right; int atActivation = -1, afterRun = -1; };

// `stereo`: the right channel carries its own noise, so Mid and Side differ
// from Left and Right; otherwise both channels carry the same.
Render render(double sr, const Settings& s, bool stereo = false)
{
    constexpr int block = 256;
    Plugin plugin(sr, block, s);
    Render r;
    r.atActivation = plugin.latency();
    r.left = stimulus(sr);
    r.right = stereo ? stimulus(sr, 54321u) : r.left;
    const int n = (int)r.left.size();
    for (int pos = 0; pos < n; pos += block)
        plugin.process(r.left.data() + pos, r.right.data() + pos, std::min(block, n - pos));
    r.afterRun = plugin.latency();
    return r;
}

constexpr double kInfinite = std::numeric_limits<double>::infinity();

// Delay of y against x (samples, sub-sample by a parabola through the
// cross-correlation peak), searched over 0..400 samples. A non-finite sample
// among those it reads makes the delay infinite: a NaN never wins the
// comparison, so the search would otherwise settle on a lag regardless.
double delayOf(const std::vector<float>& x, const std::vector<float>& y, double sr)
{
    const int from = (int)(0.2 * sr), to = (int)(0.9 * sr);
    for (int i = from; i < to + 401; ++i)
        if ((i < to && !std::isfinite(x[(size_t)i])) || !std::isfinite(y[(size_t)i])) return kInfinite;
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

// Largest difference between two renders, over both channels; infinite at a
// non-finite sample, which std::max would otherwise pass over.
double maxDiff(const Render& a, const Render& b)
{
    double worst = 0.0;
    for (size_t i = 0; i < a.left.size(); ++i)
    {
        if (!std::isfinite(a.left[i]) || !std::isfinite(b.left[i]) || !std::isfinite(a.right[i])
            || !std::isfinite(b.right[i]))
            return kInfinite;
        worst = std::max({worst, (double)std::abs(a.left[i] - b.left[i]), (double)std::abs(a.right[i] - b.right[i])});
    }
    return worst;
}

void routingKeepsChannelsTogether(double sr, int oversampling)
{
    // Flat saturating bands at drive 0 only delay. Routed to one channel, or to
    // Mid or Side, a band must still delay both channels by its round trip, as
    // the latency reports, so its render equals the render with the same bands
    // routed Stereo. Delaying only what it saturates would leave the other
    // channel (or the Side under a Mid band) early, and the channels apart.
    struct Case { const char* name; int bands, first, second; };
    const Case cases[] = {{"Left", 1, 2, 1},        {"Right", 1, 3, 1},       {"Mid", 1, 4, 1},
                          {"Side", 1, 5, 1},        {"Left + Right", 2, 2, 3}, {"Left + Left", 2, 2, 2},
                          {"Mid + Side", 2, 4, 5}};
    Settings flat;
    const Render dry = render(sr, flat, true);
    for (const Case& c : cases)
    {
        Settings s;
        s.oversampling = oversampling;
        s.saturatingBands = c.bands;
        s.drive = 0.0f;
        s.routing[0] = c.first;
        s.routing[1] = c.second;
        Settings stereo = s;
        stereo.routing[0] = stereo.routing[1] = 1;
        const Render routed = render(sr, s, true), reference = render(sr, stereo, true);
        const double diff = maxDiff(routed, reference);
        const double delayL = delayOf(dry.left, routed.left, sr), delayR = delayOf(dry.right, routed.right, sr);
        std::printf("  sr=%.0f %dx, saturating band%s routed %-12s latency %3d; rendered delay L %7.3f, R %7.3f; "
                    "%.1e from the same bands routed Stereo\n", sr, oversampling == 2 ? 4 : 2,
                    c.bands > 1 ? "s" : " ", c.name, routed.atActivation, delayL, delayR, diff);
        check(diff < 1e-5 && std::abs(delayL - routed.atActivation) <= 0.6 && std::abs(delayR - routed.atActivation) <= 0.6,
              "a routed saturating band delays both channels by the reported latency", std::max(delayL, delayR),
              routed.atActivation);
    }
}

void helpersRejectNonFinite()
{
    // A NaN never wins std::max or a correlation comparison, so a reduction
    // built on them would read a render with a NaN as a match or as a delay.
    // Each must report a non-finite sample as infinite.
    const double sr = 44100.0;
    Render clean;
    clean.left = clean.right = stimulus(sr);
    Render bad = clean;
    bad.left[(size_t)(0.5 * sr)] = std::numeric_limits<float>::quiet_NaN();
    const double delay = delayOf(clean.left, bad.left, sr), diff = maxDiff(clean, bad);
    std::printf("  reductions over a NaN: delay %g, render difference %g\n", delay, diff);
    check(std::isinf(delay) && std::isinf(diff), "every reduction reports a non-finite sample as infinite", 0.0, 1.0);
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
        routingKeepsChannelsTogether(sr, 1);
        routingKeepsChannelsTogether(sr, 2);
    }
    helpersRejectNonFinite();
    std::printf("Multi-Q latency: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
