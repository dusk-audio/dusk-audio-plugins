// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// #148: soloing a band never changes Multi-Q 2's latency or its output timing.
//
// With Oversampling at 2x or 4x, every band with saturation on delays the
// output by its oversamplers' round trip (62.5 or 65.75 samples). A solo
// silences the other bands; if their delay went with them, the reported
// latency would change on every solo click (a CLAP host would restart the
// plugin each time) or the soloed output would move in time against the rest
// of the session. The original Multi-Q keeps one latency whatever is soloed,
// and so must Multi-Q 2:
//   * the reported latency is the same with and without solo, in every mode
//     and setting, at activation and while running (no restart request);
//   * a soloed band (plain or delta) lines up with the unsoloed output to the
//     sample, including the fractional part of the oversamplers' delay;
//   * engaging and releasing solo stays a click-free crossfade, also when a
//     silenced band saturates and boosts or cuts, and so does switching a
//     saturating band on or off;
//   * phase invert, pan and delta solo of a saturating band act on the band's
//     effect lined up with its delay, as they do without oversampling.
//
// Runs the real plugin through DAF's PluginExporter, its host parameters and
// the editor's solo bridge.
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
        std::fprintf(stderr, "FAIL: %s (measured %.6f, expected %.6f)\n", what, measured, expected);
    }
}

constexpr int kBlock = 256;
constexpr double kPi = 3.14159265358979323846;

struct Setup
{
    int eqType = 0;                        // 0 Digital, 1 Match, 2 British, 3 Tube
    int oversampling = 0;                  // 0 Off, 1 2x, 2 4x
    std::vector<int> saturating;           // UI bands (2..7) with Tape saturation
    float drive = 0.0f;                    // 0: the waveshaper is an identity
    bool limiter = false;
    int boostBand = -1;                    // UI band with a +6 dB bell, -1 none
    int midBand = -1;                      // UI band routed to Mid, -1 none
    bool edges = true;                     // HPF (band 1) and LPF (band 8) on
    int offBand = -1;                      // UI band (2..7) switched off, -1 none
    int invertBand = -1;                   // UI band with Phase Invert on, -1 none
    int panBand = -1;                      // UI band panned half right, -1 none
    int eqBand = -1;                       // UI band with the bell below, -1 none
    float eqFreq = 1000.0f, eqGain = 0.0f, eqQ = 0.71f;   // Hz, dB
};

class Plugin
{
public:
    Plugin(double sr, const Setup& s)
    {
        DAF_NAMESPACE::d_nextBufferSize = (uint32_t)kBlock;   // as a DAF wrapper does
        DAF_NAMESPACE::d_nextSampleRate = sr;
        plugin = std::make_unique<DAF_NAMESPACE::PluginExporter>(nullptr, nullptr, nullptr, nullptr);
        set(kParamEqType, (float)s.eqType);
        set(kParamHqEnabled, (float)s.oversampling);
        set(kParamLimiterEnabled, s.limiter ? 1.0f : 0.0f);
        for (int band : s.saturating)
        {
            set(mqidx::satType(band - 1), 1.0f);
            set(mqidx::satDrive(band - 1), s.drive);
        }
        if (s.boostBand > 0) set(mqidx::gain(s.boostBand - 1), 6.0f);
        if (s.midBand > 0) set(mqidx::routing(s.midBand - 1), 4.0f);   // Mid
        set(mqidx::enabled(0), s.edges ? 1.0f : 0.0f);
        set(mqidx::enabled(7), s.edges ? 1.0f : 0.0f);
        if (s.offBand > 0) set(mqidx::enabled(s.offBand - 1), 0.0f);
        if (s.invertBand > 0) set(mqidx::phaseInvert(s.invertBand - 1), 1.0f);
        if (s.panBand > 0) set(mqidx::pan(s.panBand - 1), 0.5f);
        if (s.eqBand > 0)
        {
            set(mqidx::freq(s.eqBand - 1), s.eqFreq);
            set(mqidx::gain(s.eqBand - 1), s.eqGain);
            set(mqidx::q(s.eqBand - 1), s.eqQ);
        }
        plugin->setSampleRate(sr, true);
        plugin->setBufferSize((uint32_t)kBlock, true);
        plugin->activate();
    }
    void set(uint32_t index, float value) { plugin->setParameterValue(index, value); }
    void solo(int uiBand, bool delta) { multiQSetSolo(plugin->getInstancePointer(), uiBand - 1, delta); }
    void unsolo() { multiQSetSolo(plugin->getInstancePointer(), -1, false); }
    void reactivate() { plugin->deactivate(); plugin->activate(); }
    int latency() const { return (int)plugin->getLatency(); }
    void process(float* l, float* r, int n)
    {
        const float* in[] = {l, r};
        float* out[] = {l, r};
        plugin->run(in, out, (uint32_t)n);
    }
    void silence(int blocks)
    {
        std::vector<float> l((size_t)kBlock, 0.0f), r((size_t)kBlock, 0.0f);
        for (int b = 0; b < blocks; ++b) process(l.data(), r.data(), kBlock);
    }

private:
    std::unique_ptr<DAF_NAMESPACE::PluginExporter> plugin;
};

struct Signal { std::vector<float> l, r; };

// Independent low-passed noise per channel at about -30 dBFS: Left, Right, Mid
// and Side all carry signal, and the saturation stays close to linear.
Signal noise(double sr, double seconds)
{
    const size_t n = (size_t)(seconds * sr);
    Signal s{std::vector<float>(n), std::vector<float>(n)};
    uint32_t state = 0x5017u;
    const double a = std::exp(-2.0 * kPi * 4000.0 / sr);
    double l1 = 0, l2 = 0, r1 = 0, r2 = 0;
    for (size_t i = 0; i < n; ++i)
    {
        state = state * 1664525u + 1013904223u; const double wl = (double)(state >> 8) / 16777216.0 - 0.5;
        state = state * 1664525u + 1013904223u; const double wr = (double)(state >> 8) / 16777216.0 - 0.5;
        l1 = a * l1 + (1 - a) * wl; l2 = a * l2 + (1 - a) * l1;
        r1 = a * r1 + (1 - a) * wr; r2 = a * r2 + (1 - a) * r1;
        s.l[i] = (float)(0.12 * l2); s.r[i] = (float)(0.12 * r2);
    }
    return s;
}

Signal tone(double sr, double seconds, double peakDb, double hz = 1000.0)
{
    const size_t n = (size_t)(seconds * sr);
    Signal s{std::vector<float>(n), std::vector<float>(n)};
    const double g = std::pow(10.0, peakDb / 20.0);
    for (size_t i = 0; i < n; ++i)
        s.l[i] = s.r[i] = (float)(g * std::sin(2.0 * kPi * hz * (double)i / sr));
    return s;
}

// Renders `in`; the solo (band > 0) is engaged from the first block, or at
// sample `soloAt` if that is positive, and released at `releaseAt` if positive.
Signal render(double sr, const Setup& s, const Signal& in, int band = -1, bool delta = false,
              long soloAt = 0, long releaseAt = -1, int* latencyChanges = nullptr)
{
    Plugin plugin(sr, s);
    if (band > 0 && soloAt == 0) { plugin.solo(band, delta); plugin.reactivate(); }
    const int atStart = plugin.latency();
    Signal out = in;
    int changes = 0;
    for (size_t pos = 0; pos < in.l.size(); pos += kBlock)
    {
        if (band > 0 && soloAt > 0 && (long)pos == soloAt) plugin.solo(band, delta);
        if (releaseAt > 0 && (long)pos == releaseAt) plugin.unsolo();
        const int n = (int)std::min<size_t>(kBlock, in.l.size() - pos);
        plugin.process(out.l.data() + pos, out.r.data() + pos, n);
        changes += plugin.latency() != atStart;
    }
    if (latencyChanges) *latencyChanges = changes;
    return out;
}

// Delay (samples) of y against x over the settled middle of the render: the
// whole-sample lag with the largest cross-correlation within +-8, refined by
// the least-squares fit of y[i + lag] = x[i - t] ~ x[i] - t x'[i]. A parabola
// through the correlation peak would do for broadband noise but reads a few
// thousandths of a sample off on a narrowband signal such as one bell's
// contribution, whose correlation peak is flat; the fit has no such bias.
double lagOf(const Signal& x, const Signal& y, double sr)
{
    const int from = (int)(0.25 * sr), to = (int)(x.l.size()) - 16;
    auto corr = [&](int lag) {
        double c = 0.0;
        for (int i = from; i < to; ++i)
            c += (double)x.l[(size_t)i] * y.l[(size_t)(i + lag)] + (double)x.r[(size_t)i] * y.r[(size_t)(i + lag)];
        return c;
    };
    int best = -8; double bestC = corr(-8);
    for (int lag = -7; lag <= 8; ++lag) { const double c = corr(lag); if (c > bestC) { bestC = c; best = lag; } }
    double num = 0.0, den = 0.0;
    for (const auto* ch : {&x.l, &x.r})
    {
        const auto& xs = *ch;
        const auto& ys = (ch == &x.l) ? y.l : y.r;
        for (int i = from; i < to; ++i)
        {
            const double slope = 0.5 * ((double)xs[(size_t)(i + 1)] - xs[(size_t)(i - 1)]);
            num += ((double)ys[(size_t)(i + best)] - xs[(size_t)i]) * slope;
            den += slope * slope;
        }
    }
    return best - (den > 0.0 ? num / den : 0.0);
}

double maxDiff(const Signal& a, const Signal& b, double sr)
{
    double worst = 0.0;
    for (size_t i = (size_t)(0.25 * sr); i < a.l.size(); ++i)
        worst = std::max({worst, (double)std::abs(a.l[i] - b.l[i]), (double)std::abs(a.r[i] - b.r[i])});
    return worst;
}

Signal minus(const Signal& a, const Signal& b)
{
    Signal d = a;
    for (size_t i = 0; i < d.l.size(); ++i) { d.l[i] -= b.l[i]; d.r[i] -= b.r[i]; }
    return d;
}

void latencyIgnoresSolo(double sr)
{
    // Every character, oversampling setting, saturation layout and limiter
    // setting; every band soloed plainly and as delta, engaged while running
    // and present at activation.
    int configurations = 0, mismatches = 0;
    for (int eqType : {0, 1, 2, 3})
        for (int os : {0, 1, 2})
            for (const std::vector<int>& sat : {std::vector<int>{}, std::vector<int>{3}, std::vector<int>{2, 4, 6}})
                for (bool limiter : {false, true})
                {
                    Setup s; s.eqType = eqType; s.oversampling = os; s.saturating = sat; s.limiter = limiter;
                    Plugin plugin(sr, s);
                    plugin.silence(2);
                    const int reference = plugin.latency();
                    ++configurations;
                    for (bool delta : {false, true})
                        for (int band = 1; band <= 8; ++band)
                        {
                            plugin.solo(band, delta);
                            plugin.silence(2);
                            const int running = plugin.latency();
                            plugin.reactivate();
                            const int activation = plugin.latency();
                            plugin.silence(1);
                            const int afterRun = plugin.latency();
                            mismatches += running != reference || activation != reference || afterRun != reference;
                            plugin.unsolo();
                            plugin.silence(1);
                            mismatches += plugin.latency() != reference;
                        }
                }
    std::printf("  sr=%.0f latency with solo: %d settings x 16 solos, %d differ from the unsoloed latency\n",
                sr, configurations, mismatches);
    check(mismatches == 0, "solo never changes the reported latency (no restart request)", mismatches, 0);
}

void timing(double sr, int os, const std::vector<int>& saturating, int midBand)
{
    // The bands are flat (0 dB, drive 0, HPF and LPF off) except a +6 dB bell
    // on band 4, so a solo removes nothing audible but the silenced bands'
    // processing, and their delay must stay: the soloed output must equal the
    // unsoloed one. Soloing the bell compares with the full render; soloing a
    // flat saturating band (3), which silences the bell, with the render
    // without the bell.
    Setup s; s.oversampling = os; s.saturating = saturating; s.boostBand = 4; s.midBand = midBand; s.edges = false;
    Setup flat = s; flat.boostBand = -1;
    const Signal in = noise(sr, 0.6);
    const Signal full = render(sr, s, in), fullFlat = render(sr, flat, in);
    const Signal soloBell = render(sr, s, in, 4), soloFlat = render(sr, s, in, 3);
    const double worstLag = std::max(std::abs(lagOf(full, soloBell, sr)), std::abs(lagOf(fullFlat, soloFlat, sr)));
    const double worstDiff = std::max(maxDiff(full, soloBell, sr), maxDiff(fullFlat, soloFlat, sr));
    // Delta solo of band 4: its contribution, compared with the unsoloed
    // output minus the same render without the bell.
    const Signal contribution = minus(full, fullFlat);
    const Signal delta = render(sr, s, in, 4, true);
    const double deltaLag = std::abs(lagOf(contribution, delta, sr));
    const double deltaDiff = maxDiff(contribution, delta, sr);
    std::printf("  sr=%.0f %dx, saturating bands %zu%s: solo lag %.1e samples, max diff %.1e; delta lag %.1e, max diff %.1e\n",
                sr, os == 2 ? 4 : 2, saturating.size(), midBand > 0 ? " (one routed Mid)" : "",
                worstLag, worstDiff, deltaLag, deltaDiff);
    check(worstLag < 0.001 && worstDiff < 1e-5, "a soloed band lines up with the unsoloed output to the sample",
          worstLag, 0.0);
    check(deltaLag < 0.001 && deltaDiff < 1e-5, "a delta-soloed band lines up with its contribution to the full output",
          deltaLag, 0.0);
}

void bandReference(double sr, int os)
{
    // Phase invert, pan and delta solo act on a band's effect: the band's
    // output against its input delayed as its saturation delays it. On a flat
    // saturating band at drive 0 the effect is nothing, so inverting or
    // panning it changes nothing and its delta solo is silent. Measured
    // against its undelayed input instead, the effect would be the delay
    // itself, and these would comb-filter the signal.
    Setup s; s.oversampling = os; s.saturating = {2, 3, 5}; s.edges = false;
    Setup inverted = s; inverted.invertBand = 3;
    Setup panned = s; panned.panBand = 3;
    const Signal in = noise(sr, 0.6);
    const Signal plain = render(sr, s, in);
    const double invertDiff = maxDiff(plain, render(sr, inverted, in), sr);
    const double panDiff = maxDiff(plain, render(sr, panned, in), sr);
    const Signal silence{std::vector<float>(in.l.size(), 0.0f), std::vector<float>(in.r.size(), 0.0f)};
    const double deltaLevel = maxDiff(silence, render(sr, s, in, 3, true), sr);
    std::printf("  sr=%.0f %dx, flat saturating band: phase invert changes the output by %.1e, pan by %.1e; "
                "its delta solo peaks at %.1e\n", sr, os == 2 ? 4 : 2, invertDiff, panDiff, deltaLevel);
    check(invertDiff < 1e-5 && panDiff < 1e-5 && deltaLevel < 1e-5,
          "phase invert, pan and delta solo act on a saturating band's effect, lined up with its delay",
          std::max({invertDiff, panDiff, deltaLevel}), 0.0);
}

double maxStep(const Signal& y, size_t from, size_t to)
{
    double worst = 0.0;
    for (size_t i = std::max<size_t>(from, 1); i < to; ++i)
        worst = std::max({worst, (double)std::abs(y.l[i] - y.l[i - 1]), (double)std::abs(y.r[i] - y.r[i - 1])});
    return worst;
}

// Largest |a - b| over samples [from, to), both channels.
double offBy(const Signal& a, const Signal& b, size_t from, size_t to)
{
    double worst = 0.0;
    for (size_t i = from; i < std::min(to, a.l.size()); ++i)
        worst = std::max({worst, (double)std::abs(a.l[i] - b.l[i]), (double)std::abs(a.r[i] - b.r[i])});
    return worst;
}

// Switch times on block boundaries, and the sample from which a switch has
// fully passed: the 3 ms crossfade, the 128 samples a band that runs again
// holds silent, and an oversampler's 128-sample memory for each saturating
// band in series after it, with 1 ms margin.
long blockAt(double sr, double seconds) { return (long)(seconds * sr) / kBlock * kBlock; }
size_t settledAfter(double sr, long at, int saturatingBands)
{
    return (size_t)at + (size_t)std::lround(0.004 * sr) + (size_t)(128 * (saturatingBands + 1));
}

void crossfade(double sr)
{
    // Saturating bands with real drive at 2x; solo band 3 on a tone, then
    // release it. Neither switch may click, and once each has passed the
    // output must equal a render soloed from the start, then one never
    // soloed: the timing never moved and the solo left nothing behind.
    Setup s; s.oversampling = 1; s.saturating = {2, 4, 6}; s.drive = 0.6f;
    const Signal in = tone(sr, 0.8, -12.0);
    const long engage = blockAt(sr, 0.3), release = blockAt(sr, 0.55);
    int changes = 0;
    const Signal y = render(sr, s, in, 3, false, engage, release, &changes);
    const Signal soloed = render(sr, s, in, 3), unsoloed = render(sr, s, in);
    const size_t ms = (size_t)std::lround(0.001 * sr);
    const double steady = std::max(maxStep(y, (size_t)(0.1 * sr), (size_t)(0.2 * sr)),
                                   maxStep(y, (size_t)(0.4 * sr), (size_t)(0.5 * sr)));   // unsoloed, soloed
    const double atEngage = maxStep(y, (size_t)engage - ms, settledAfter(sr, engage, 3));
    const double atRelease = maxStep(y, (size_t)release - ms, settledAfter(sr, release, 3));
    const double during = offBy(y, soloed, (size_t)engage + ms, (size_t)engage + 2 * ms);
    const double settledSoloed = offBy(y, soloed, settledAfter(sr, engage, 3), (size_t)release);
    const double settledReleased = offBy(y, unsoloed, settledAfter(sr, release, 3), y.l.size());
    std::printf("  sr=%.0f solo switch: largest step %.4f at engage, %.4f at release (steady %.4f); "
                "%.1e from a render soloed throughout during the fade, %.1e once settled; "
                "after release %.1e from one never soloed; latency changes %d\n",
                sr, atEngage, atRelease, steady, during, settledSoloed, settledReleased, changes);
    check(atEngage <= 1.5 * steady && atRelease <= 1.5 * steady, "engaging and releasing solo does not click",
          std::max(atEngage, atRelease), 1.5 * steady);
    check(during > 1e-3, "the solo is a crossfade, not a switch", during, 1e-3);
    check(settledSoloed < 1e-5, "after the crossfade the soloed output is exactly the soloed-throughout render",
          settledSoloed, 0.0);
    check(settledReleased < 1e-5, "after release the output is exactly the never-soloed render", settledReleased, 0.0);
    check(changes == 0, "switching solo while running never changes the latency", changes, 0);
}

void enableSwitch(double sr)
{
    // Switching a saturating band on (which adds its delay to the latency) and
    // off while running stays a click-free crossfade, and once it has passed
    // the output equals a render with the band on, then off, throughout.
    Setup on; on.oversampling = 1; on.saturating = {3}; on.drive = 0.6f;
    Setup off = on; off.offBand = 3;
    const Signal in = tone(sr, 0.8, -12.0);
    const long switchOn = blockAt(sr, 0.3), switchOff = blockAt(sr, 0.55);
    Plugin plugin(sr, off);
    Signal y = in;
    for (size_t pos = 0; pos < in.l.size(); pos += kBlock)
    {
        if ((long)pos == switchOn) plugin.set(mqidx::enabled(2), 1.0f);
        if ((long)pos == switchOff) plugin.set(mqidx::enabled(2), 0.0f);
        plugin.process(y.l.data() + pos, y.r.data() + pos, (int)std::min<size_t>(kBlock, in.l.size() - pos));
    }
    const Signal bandOn = render(sr, on, in), bandOff = render(sr, off, in);
    const size_t ms = (size_t)std::lround(0.001 * sr);
    const double steady = maxStep(y, (size_t)(0.1 * sr), (size_t)(0.2 * sr));
    const double atOn = maxStep(y, (size_t)switchOn - ms, settledAfter(sr, switchOn, 1));
    const double atOff = maxStep(y, (size_t)switchOff - ms, settledAfter(sr, switchOff, 1));
    const double settledOn = offBy(y, bandOn, settledAfter(sr, switchOn, 1), (size_t)switchOff);
    const double settledOff = offBy(y, bandOff, settledAfter(sr, switchOff, 1), y.l.size());
    std::printf("  sr=%.0f saturating band switched on and off: largest step %.4f on, %.4f off (steady %.4f); "
                "once settled %.1e from the band on throughout, %.1e from it off\n",
                sr, atOn, atOff, steady, settledOn, settledOff);
    check(atOn <= 1.5 * steady && atOff <= 1.5 * steady, "switching a saturating band on or off does not click",
          std::max(atOn, atOff), 1.5 * steady);
    check(settledOn < 1e-5 && settledOff < 1e-5, "after the switch the output is exactly the steady render",
          std::max(settledOn, settledOff), 0.0);
}

void flatBandsUnchanged(double sr, int os)
{
    // All six parametric bands saturate at drive 0 and stay flat, so each
    // contributes only its delay, which its reference reproduces exactly.
    // Soloing band 4 mid-stream and releasing it must then leave the output
    // as it is without solo, before, during and after both crossfades: a
    // reference that starts must render like one that never stopped, and a
    // band that runs again must hold none of what its oversampler had before.
    Setup s; s.oversampling = os; s.saturating = {2, 3, 4, 5, 6, 7}; s.edges = false;
    const Signal in = noise(sr, 0.9);
    const long engage = blockAt(sr, 0.3), release = blockAt(sr, 0.6);
    const Signal y = render(sr, s, in, 4, false, engage, release);
    const double diff = offBy(y, render(sr, s, in), (size_t)(0.25 * sr), y.l.size());
    std::printf("  sr=%.0f %dx, six flat saturating bands at drive 0, band 4 soloed and released: "
                "%.1e from the render without solo\n", sr, os == 2 ? 4 : 2, diff);
    check(diff < 1e-5, "a solo of flat saturating bands leaves the output unchanged through both switches",
          diff, 0.0);
}

void shapingBandClick(double sr, int os, float gain, float q, float drive)
{
    // All six parametric bands saturate; band 4 also boosts or cuts, at the
    // tone's 200 Hz (low, so a click stands far above the tone's own sample
    // steps). Soloing band 3 silences band 4, so its reference must take
    // over from a band whose own oversampler carries its filtered, saturated
    // signal, and on release the band must take over from the reference
    // again. Any seam between the two signals is a click a few round trips
    // later, through the references that follow. Neither switch may step
    // further than 1.5 times the louder of the two steady states does (the
    // solo removes band 4's boost or cut, so the soloed tone is a different
    // level).
    Setup s; s.oversampling = os; s.saturating = {2, 3, 4, 5, 6, 7}; s.drive = drive;
    s.eqBand = 4; s.eqFreq = 200.0f; s.eqGain = gain; s.eqQ = q;
    const Signal in = tone(sr, 0.9, -10.0, 200.0);
    const long engage = blockAt(sr, 0.3), release = blockAt(sr, 0.6);
    const Signal y = render(sr, s, in, 3, false, engage, release);
    const Signal unsoloed = render(sr, s, in);
    const size_t ms = (size_t)std::lround(0.001 * sr);
    const double steady = std::max(maxStep(y, (size_t)(0.1 * sr), (size_t)(0.2 * sr)),
                                   maxStep(y, (size_t)(0.4 * sr), (size_t)(0.5 * sr)));
    const double atEngage = maxStep(y, (size_t)engage - ms, settledAfter(sr, engage, 6));
    const double atRelease = maxStep(y, (size_t)release - ms, settledAfter(sr, release, 6));
    const double settledReleased = offBy(y, unsoloed, settledAfter(sr, release, 6) + 10 * ms, y.l.size());
    std::printf("  sr=%.0f %dx, six saturating bands (drive %.1f), band 4 %+.0f dB Q %.2f silenced by a solo: "
                "largest step x%.2f at engage, x%.2f at release; 10 ms after settling %.1e from never soloed\n",
                sr, os == 2 ? 4 : 2, drive, gain, q, atEngage / steady, atRelease / steady, settledReleased);
    check(atEngage <= 1.5 * steady && atRelease <= 1.5 * steady,
          "a solo that silences a saturating band which also boosts or cuts does not click",
          std::max(atEngage, atRelease) / steady, 1.5);
}
}

int main()
{
    std::printf("Multi-Q 2 latency and timing with solo\n");
    for (double sr : {44100.0, 48000.0, 96000.0})
    {
        latencyIgnoresSolo(sr);
        for (int os : {1, 2})
        {
            timing(sr, os, {3}, -1);
            timing(sr, os, {2, 3, 5, 7}, -1);
            timing(sr, os, {2, 3, 5, 7}, 5);
            bandReference(sr, os);
            flatBandsUnchanged(sr, os);
            shapingBandClick(sr, os, 6.0f, 0.71f, 0.3f);
            shapingBandClick(sr, os, -6.0f, 0.71f, 0.3f);
            shapingBandClick(sr, os, 12.0f, 4.0f, 0.3f);
            shapingBandClick(sr, os, 3.0f, 0.71f, 0.0f);
        }
        crossfade(sr);
        enableSwitch(sr);
    }
    std::printf("Multi-Q solo latency: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
