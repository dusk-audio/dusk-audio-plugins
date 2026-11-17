// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// wheelmod_test — unit gate for core/WheelMod.hpp (Oracle Wheel Mod, issue #303)
// and for the parameters appended for it.
//
// Every expected number is a measurement of the reference model (2026-11-17,
// instrument-host renders) or a law derived from one; WheelMod.hpp says where each
// comes from. Tolerances were fixed before the first run. Each family carries a
// FAILURE CONTROL: a deliberately wrong input the same check must reject, so a
// check that cannot fail is caught here rather than trusted.
//
//   [params]   appended at the end, 1.1.0 index order frozen (symbol hash), core and
//              shell tables agree, defaults = section off
//   [start]    LFO starts at phase 0: saw 0, triangle at its peak, square high
//   [rate]     cycle period = 1 / rate (0.05 %), log knob law vs the reference points (1 %)
//   [shapes]   every switch combination vs the reference's measured extremes (+-10 cents)
//   [depth]    wheel law linear; per-step depths for frequency, pulse width, filter (1 %)
//   [limits]   depth = lower + (upper - lower) x wheel
//   [mix]      linear crossfade LFO <-> noise / S&H
//   [noise]    RMS, mean, skew, kurtosis (three sample rates), spectrum vs the reference
//   [sh]       S&H: uniform, +-801 cents, one step per LFO cycle at the saw reset
//   [shared]   free-running, block-segmentation invariant, deterministic
//   [off]      no destination: every frame +0.0f and active() false (the voices'
//              byte-identity guarantee)
//   [fade]     a destination switch fades over 5 ms; switched off it returns to exact 0
//   [smooth]   wheel moves glide (8 ms one-pole), snap lands at once
//   [robust]   non-finite settings never reach the output
//
// Test code allocates; the module under test never does.

#include "WheelMod.hpp"
#include "MultiSynthDSP.hpp"
#include "MultiSynthParams.hpp"
#include "DuskFft.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace msynth;

namespace
{
int g_fail = 0, g_pass = 0;

void check(bool ok, const char* tag, const char* fmt, ...)
{
    std::printf("%s [%s] ", ok ? "PASS" : "FAIL", tag);
    va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    va_end(ap);
    std::printf("\n");
    (ok ? g_pass : g_fail)++;
}
// A failure control: the check must REJECT this input. Reported as PASS when it does.
void control(bool rejected, const char* tag, const char* what)
{
    std::printf("%s [%s] control rejected: %s\n", rejected ? "PASS" : "FAIL", tag, what);
    (rejected ? g_pass : g_fail)++;
}

bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }
bool nearRel(double a, double b, double rel) { return std::fabs(a - b) <= rel * std::fabs(b); }

uint32_t bits(float f) { uint32_t u; std::memcpy(&u, &f, sizeof u); return u; }

WheelModSettings fullWheelB(bool saw, bool tri, bool sq)
{
    WheelModSettings s;
    s.saw = saw; s.tri = tri; s.square = sq;
    s.freqB = true;
    return s;
}

// --------------------------------------------------------------------------
void testParams()
{
    const char* T = "params";
    // 1.1.0 shipped 228 parameters; their symbol order is what host automation,
    // sessions and .scpreset files key on. FNV-1a 64 of "symbol\n" over that
    // order, computed from the 1.1.0 table (and identical to the order in a real
    // 1.1.0 user preset file from issue #303).
    constexpr int kParams110 = 228;
    constexpr uint64_t kHash110 = 0xa109e17990c4d27eull;
    uint64_t h = 0xcbf29ce484222325ull;
    for (int i = 0; i < kParams110; ++i)
    {
        for (const char* c = kParamDefs[i].symbol; *c; ++c) { h ^= (uint8_t)*c; h *= 0x100000001b3ull; }
        h ^= (uint8_t)'\n'; h *= 0x100000001b3ull;
    }
    check(h == kHash110, T, "first %d symbols in 1.1.0 order (hash 0x%016llx)", kParams110, (unsigned long long)h);
    check((int)pWheelLfoRate == kParams110 && (int)pModOsc3Filter == kParams110 - 1, T,
          "wheel block appended right after the 1.1.0 table (first index %d)", (int)pWheelLfoRate);
    check((int)kNumParams == kParams110 + 13 && (int)kNumCoreParams == (int)kNumParams, T,
          "13 parameters appended, core %d == shell %d", (int)kNumParams, (int)kNumCoreParams);

    // Core name table, shell table and the module agree, symbol by symbol.
    struct Want { int idx; const char* sym; float def; };
    const Want want[] = {
        { pWheelLfoRate, "wheelLfoRate", 0.8866f }, { pWheelLfoSaw, "wheelLfoSaw", 0.0f },
        { pWheelLfoTri, "wheelLfoTri", 1.0f },      { pWheelLfoSquare, "wheelLfoSquare", 0.0f },
        { pWheelSourceMix, "wheelSourceMix", 0.0f },{ pWheelNoiseSH, "wheelNoiseSH", 0.0f },
        { pWheelLowerLimit, "wheelLowerLimit", 0.0f }, { pWheelUpperLimit, "wheelUpperLimit", 1.0f },
        { pWheelFreqA, "wheelFreqA", 0.0f },        { pWheelFreqB, "wheelFreqB", 0.0f },
        { pWheelPwA, "wheelPwA", 0.0f },            { pWheelPwB, "wheelPwB", 0.0f },
        { pWheelFilter, "wheelFilter", 0.0f },
    };
    MultiSynthDSP fresh;
    bool allOk = true;
    for (const Want& w : want)
    {
        const bool ok = MultiSynthDSP::paramIndexForName(w.sym) == w.idx
                     && std::strcmp(kParamDefs[w.idx].symbol, w.sym) == 0
                     && kParamDefs[w.idx].def == w.def
                     && fresh.getParameter(w.idx) == w.def;
        if (!ok) std::printf("      mismatch at %s (index %d)\n", w.sym, w.idx);
        allOk &= ok;
    }
    check(allOk, T, "core names, core defaults and shell table agree for all 13");
    check(kParamDefs[pWheelLfoRate].min == WheelMod::kRateMinHz && kParamDefs[pWheelLfoRate].max == WheelMod::kRateMaxHz
          && kParamDefs[pWheelLfoRate].kind == PK_LOG, T, "rate parameter is logarithmic %.2f..%.1f Hz (the knob law)",
          kParamDefs[pWheelLfoRate].min, kParamDefs[pWheelLfoRate].max);
    // Defaults switch nothing on: the engine-default settings are inactive.
    WheelModSettings def;
    check(!def.freqA && !def.freqB && !def.pwA && !def.pwB && !def.filter, T,
          "module defaults route to no destination");
    WheelMod m; m.prepare(48000.0); m.set(def, 1.0f, false);
    check(!m.active(), T, "defaults leave the section inactive, even at full wheel");
}

// --------------------------------------------------------------------------
void testStart()
{
    const char* T = "start";
    struct C { bool saw, tri, sq; double want; const char* name; };
    // Phase 0 at reset: saw 0, triangle at its +0.5 peak, square high.
    const C cases[] = {
        { true, false, false, 0.0, "saw" },
        { false, true, false, 0.036526 + 0.5 * 1.00660, "triangle peak" },
        { false, false, true, 0.684297, "square high" },
    };
    for (const C& c : cases)
    {
        WheelMod m; m.prepare(48000.0); m.set(fullWheelB(c.saw, c.tri, c.sq), 1.0f, true);
        const WheelModFrame f = m.process();
        check(near(f.freqBCents, c.want * WheelMod::kFreqCentsPerUnit, 0.01), T, "%s: first sample %.2f c (want %.2f)",
              c.name, f.freqBCents, c.want * WheelMod::kFreqCentsPerUnit);
    }
}

// --------------------------------------------------------------------------
double measuredPeriod(double sr, float rate, double seconds)
{
    WheelMod m; m.prepare(sr);
    WheelModSettings s; s.rateHz = rate; m.set(s, 0.0f, true);
    const long n = (long)(seconds * sr);
    long first = -1, last = -1; int wraps = 0;
    double prev = m.lfoPhase();
    for (long i = 0; i < n; ++i)
    {
        m.process();
        const double ph = m.lfoPhase();
        if (ph < prev) { if (first < 0) first = i; last = i; ++wraps; }
        prev = ph;
    }
    return wraps > 1 ? (double)(last - first) / (double)(wraps - 1) / sr : -1.0;
}

void testRate()
{
    const char* T = "rate";
    for (double sr : { 44100.0, 48000.0, 192000.0 })
        for (float r : { WheelMod::kRateMinHz, 0.8866f, 4.8197f, WheelMod::kRateMaxHz })
        {
            const double secs = std::max(20.0, 3.2 / r);
            const double per = measuredPeriod(sr, r, secs);
            check(nearRel(per, 1.0 / r, 5e-4), T, "%6.0f Hz: rate %.4f Hz -> period %.5f s (want %.5f)", sr, r, per, 1.0 / r);
        }
    control(!nearRel(measuredPeriod(48000.0, 0.8866f * 1.01f, 20.0), 1.0 / 0.8866, 5e-4), T,
            "a 1 % fast LFO fails the period check");
    // Knob law: a logarithmic parameter from kRateMinHz to kRateMaxHz at knob
    // positions 0 / .25 / .5 / .75 / 1 against the reference: 0.0300 (33.33 s
    // period), 0.1628, 0.883, 4.795, 26.3 Hz.
    const double refHz[5] = { 0.0300, 0.16279, 0.883, 4.795, 26.3 };
    for (int k = 0; k <= 4; ++k)
    {
        const double x = k / 4.0;
        const double hz = WheelMod::kRateMinHz * std::pow((double)WheelMod::kRateMaxHz / WheelMod::kRateMinHz, x);
        check(nearRel(hz, refHz[k], 0.01), T, "knob %3d -> %.4f Hz (reference %.4f, %+.2f %%)", k * 25, hz, refHz[k],
              100.0 * (hz / refHz[k] - 1.0));
    }
    control(!nearRel(0.03 * std::pow(26.2 / 0.03 * 1.05, 0.75), 4.795, 0.01), T,
            "a 5 % wider knob range misses the knob-75 point");
}

// --------------------------------------------------------------------------
struct Extremes { double lo = 1e30, hi = -1e30; };
Extremes runExtremes(const WheelModSettings& s, float wheel, double seconds, double sr = 48000.0)
{
    WheelMod m; m.prepare(sr); m.set(s, wheel, true);
    Extremes e;
    const long n = (long)(seconds * sr);
    for (long i = 0; i < n; ++i)
    {
        const double v = m.process().freqBCents;
        e.lo = std::min(e.lo, v); e.hi = std::max(e.hi, v);
    }
    return e;
}

void testShapes()
{
    const char* T = "shapes";
    // Reference renders: LFO knob 25 (0.163 Hz), wheel 127, osc B frequency, cents
    // re the unmodulated note; min and max of the zero-crossing frequency track.
    struct C { bool saw, tri, sq; double lo, hi; const char* name; };
    const C cases[] = {
        { true,  false, false,    1.6, 1175.2, "saw" },
        { false, true,  false, -548.4,  633.9, "triangle" },
        { false, false, true,    -0.4,  807.6, "square" },
        { true,  false, true,   523.5, 1149.4, "saw + square" },
        { false, true,  true,  -545.6, 1909.4, "triangle + square" },
        { true,  true,  false,  385.0, 2502.0, "saw + triangle" },
        { true,  true,  true,   286.8, 2294.5, "all three" },
    };
    for (const C& c : cases)
    {
        WheelModSettings s = fullWheelB(c.saw, c.tri, c.sq); s.rateHz = 0.16309f;
        const Extremes e = runExtremes(s, 1.0f, 1.0 / 0.16309 + 0.5);
        check(near(e.lo, c.lo, 10.0) && near(e.hi, c.hi, 10.0), T, "%-18s %8.1f .. %7.1f c (reference %8.1f .. %7.1f)",
              c.name, e.lo, e.hi, c.lo, c.hi);
    }
    {
        WheelModSettings s = fullWheelB(false, false, false); s.rateHz = 0.16309f;
        const Extremes e = runExtremes(s, 1.0f, 7.0);
        check(e.lo == 0.0 && e.hi == 0.0, T, "no shape selected: no LFO at full wheel (reference: flat)");
    }
    // Polarity: square and saw unipolar (never below 0), triangle bipolar.
    {
        WheelModSettings s = fullWheelB(true, false, false); s.rateHz = 5.0f;
        const Extremes a = runExtremes(s, 1.0f, 2.0);
        s = fullWheelB(false, false, true); s.rateHz = 5.0f;
        const Extremes b = runExtremes(s, 1.0f, 2.0);
        s = fullWheelB(false, true, false); s.rateHz = 5.0f;
        const Extremes c = runExtremes(s, 1.0f, 2.0);
        check(a.lo >= 0.0 && b.lo >= 0.0 && c.lo < -500.0 && c.hi > 500.0, T,
              "saw and square unipolar (min %.1f, %.1f c), triangle bipolar (%.0f .. %.0f c)", a.lo, b.lo, c.lo, c.hi);
    }
    // Control: summing the single shapes (what "add the shapes" would naively
    // mean) misses the reference's saw + square by far.
    control(!near(0.0 + 0.0, 523.5, 10.0) || !near(1175.06 * 1.0 + 804.09, 1149.4, 10.0), T,
            "a plain sum of saw and square (0 .. 1979 c) misses the measured 523 .. 1149 c");
}

// --------------------------------------------------------------------------
void testDepth()
{
    const char* T = "depth";
    // Linear wheel law: every output scales with the wheel exactly.
    WheelModSettings s = fullWheelB(true, false, false); s.rateHz = 0.5f;
    s.freqA = s.pwA = s.pwB = s.filter = true;
    WheelMod full, half; full.prepare(48000.0); half.prepare(48000.0);
    full.set(s, 1.0f, true); half.set(s, 64.0f / 127.0f, true);
    double worst = 0.0;
    for (int i = 0; i < 96000; ++i)
    {
        const WheelModFrame a = full.process(), b = half.process();
        const double k = 64.0 / 127.0;
        worst = std::max(worst, std::fabs(b.freqACents - k * a.freqACents) / 1175.0);
        worst = std::max(worst, std::fabs(b.pwB - k * a.pwB));
        worst = std::max(worst, std::fabs(b.filterOct - k * a.filterOct) / 12.9);
    }
    check(worst < 1e-6, T, "wheel 64/127 scales every destination by 64/127 (worst %.2g of full scale)", worst);

    // Per-step depths (reference, wheel CC steps of 1/127): triangle frequency
    // p-p 1182.3 c at CC 127 (9.31 c per step), square PW +0.2021 (A) / +0.2010 (B)
    // at CC 32, square filter 1.1127 oct at CC 16, saw filter 1.625 oct at CC 16.
    auto pp = [](WheelModSettings st, float wheel, int which) {
        WheelMod m; m.prepare(48000.0); st.rateHz = 0.5f; m.set(st, wheel, true);
        double lo = 1e30, hi = -1e30;
        for (int i = 0; i < 2 * 96000 + 10; ++i)
        {
            const WheelModFrame f = m.process();
            const double v = which == 0 ? f.freqBCents : which == 1 ? f.pwA : which == 2 ? f.pwB : f.filterOct;
            lo = std::min(lo, v); hi = std::max(hi, v);
        }
        return hi - lo;
    };
    WheelModSettings tri = fullWheelB(false, true, false);
    const double triPerStep = pp(tri, 1.0f, 0) / 127.0;
    check(nearRel(triPerStep, 1182.3 / 127.0, 0.01), T, "triangle frequency %.3f c per wheel step (reference %.3f)",
          triPerStep, 1182.3 / 127.0);
    WheelModSettings sq; sq.tri = false; sq.square = true; sq.pwA = sq.pwB = sq.filter = true;
    const double pwA32 = pp(sq, 32.0f / 127.0f, 1), pwB32 = pp(sq, 32.0f / 127.0f, 2);
    check(nearRel(pwA32, 0.2021, 0.01) && nearRel(pwB32, 0.2010, 0.01), T,
          "square PW at wheel 32: A %+.4f (ref +0.2021), B %+.4f (ref +0.2010)", pwA32, pwB32);
    const double fSq16 = pp(sq, 16.0f / 127.0f, 3);
    check(nearRel(fSq16, 1.1127, 0.01), T, "square filter at wheel 16: %.4f oct (reference 1.1127)", fSq16);
    WheelModSettings saw; saw.tri = false; saw.saw = true; saw.filter = true;
    const double fSaw16 = pp(saw, 16.0f / 127.0f, 3);
    check(nearRel(fSaw16, 1.625, 0.01), T, "saw filter at wheel 16: %.4f oct (reference 1.6087 p0.5-p99.5 = 1.625 p-p)", fSaw16);
    WheelModSettings sqF = fullWheelB(false, false, true);
    const double sqFull = pp(sqF, 1.0f, 0);
    check(nearRel(sqFull, 804.09, 0.005), T, "square frequency at full wheel %.2f c (reference +804.1)", sqFull);
    control(!nearRel(pp(tri, 1.0f, 0) * 1.02 / 127.0, 1182.3 / 127.0, 0.01), T,
            "a 2 % deeper frequency route fails the per-step check");
}

// --------------------------------------------------------------------------
void testLimits()
{
    const char* T = "limits";
    auto level = [](float lower, float upper, float wheel) {
        WheelModSettings s = fullWheelB(false, false, true); s.lowerLimit = lower; s.upperLimit = upper;
        WheelMod m; m.prepare(48000.0); m.set(s, wheel, true);
        return (double)m.process().freqBCents;   // phase 0: square high
    };
    const double full = level(0.0f, 1.0f, 1.0f);
    check(near(level(0.0f, 1.0f, 0.0f), 0.0, 0.0), T, "lower 0, wheel 0: no modulation");
    check(nearRel(level(0.5f, 1.0f, 0.0f), 0.5 * full, 1e-6), T, "lower 0.5, wheel 0: half depth (reference: half)");
    check(nearRel(level(0.0f, 0.5f, 1.0f), 0.5 * full, 1e-6), T, "upper 0.5, wheel 1: half depth (reference: half)");
    check(nearRel(level(0.2f, 0.6f, 0.25f), (0.2 + 0.4 * 0.25) * full, 1e-6), T, "lower 0.2, upper 0.6, wheel 0.25: 0.30");
    check(nearRel(level(0.8f, 0.2f, 0.5f), 0.5 * full, 1e-6) && nearRel(level(0.8f, 0.2f, 1.0f), 0.2 * full, 1e-6), T,
          "lower above upper: the wheel turns the depth DOWN (0.8 -> 0.2)");
    control(!nearRel(level(0.5f, 1.0f, 0.0f), 0.0, 1e-6), T, "a lower limit that is ignored at wheel 0 is caught");
}

// --------------------------------------------------------------------------
void testMix()
{
    const char* T = "mix";
    for (bool sh : { false, true })
    {
        WheelModSettings s = fullWheelB(false, true, false); s.rateHz = 3.0f; s.sampleAndHold = sh;
        WheelMod a, b, c; a.prepare(48000.0); b.prepare(48000.0); c.prepare(48000.0);
        s.sourceMix = 0.0f; a.set(s, 1.0f, true);
        s.sourceMix = 1.0f; b.set(s, 1.0f, true);
        s.sourceMix = 0.25f; c.set(s, 1.0f, true);
        double worst = 0.0, worstSq = 0.0;
        for (int i = 0; i < 4 * 48000; ++i)
        {
            const double x0 = a.process().freqBCents, x1 = b.process().freqBCents, xm = c.process().freqBCents;
            worst = std::max(worst, std::fabs(xm - (0.75 * x0 + 0.25 * x1)));
            worstSq = std::max(worstSq, std::fabs(xm - ((1 - 0.0625) * x0 + 0.0625 * x1)));   // control: m^2 law
        }
        check(worst < 1e-3, T, "%s: mix 0.25 = 0.75 x LFO + 0.25 x %s (worst %.2g c)", sh ? "S&H" : "noise",
              sh ? "S&H" : "noise", worst);
        control(worstSq > 1.0, T, "a squared mix law is distinguishable");
        // Reference cross-check: triangle RMS at mix 0.25 is 0.75 x the mix-0 RMS
        // (reference 256.1 c against 341.9 c).
    }
    // Endpoints are exact: mix 0 is the LFO alone, mix 1 the noise alone.
    WheelModSettings s = fullWheelB(false, true, false); s.sourceMix = 1.0f;
    WheelMod m; m.prepare(48000.0); m.set(s, 1.0f, true);
    bool exact = true;
    for (int i = 0; i < 48000; ++i)
    {
        const WheelModFrame f = m.process();
        exact &= m.lastBusValue() == m.lastNoiseValue();
        (void)f;
    }
    check(exact, T, "mix 1 is the noise alone, bit for bit");
}

// --------------------------------------------------------------------------
// Welch PSD (Hann, 50 % overlap) of x at rate fs; returns the one-sided density
// per bin (arbitrary scale: every comparison below is a ratio).
std::vector<double> welch(const std::vector<float>& x, int nfft, std::vector<double>& freqs, double fs)
{
    duskaudio::FFTr2 fft; fft.prepare(nfft);
    std::vector<float> re((size_t)nfft), im((size_t)nfft), win((size_t)nfft);
    for (int i = 0; i < nfft; ++i) win[(size_t)i] = (float)(0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * i / nfft));
    std::vector<double> p((size_t)(nfft / 2 + 1), 0.0);
    int segs = 0;
    for (size_t s = 0; s + (size_t)nfft <= x.size(); s += (size_t)(nfft / 2))
    {
        double mean = 0.0;
        for (int i = 0; i < nfft; ++i) mean += x[s + (size_t)i];
        mean /= nfft;
        for (int i = 0; i < nfft; ++i) { re[(size_t)i] = (float)((x[s + (size_t)i] - mean) * win[(size_t)i]); im[(size_t)i] = 0.0f; }
        fft.forward(re.data(), im.data());
        for (int k = 0; k <= nfft / 2; ++k) p[(size_t)k] += (double)re[(size_t)k] * re[(size_t)k] + (double)im[(size_t)k] * im[(size_t)k];
        ++segs;
    }
    freqs.resize(p.size());
    for (size_t k = 0; k < p.size(); ++k) { freqs[k] = (double)k * fs / nfft; p[k] /= std::max(1, segs); }
    return p;
}
double bandMean(const std::vector<double>& f, const std::vector<double>& p, double lo, double hi)
{
    double s = 0.0; int n = 0;
    for (size_t k = 1; k < f.size(); ++k) if (f[k] >= lo && f[k] < hi) { s += p[k]; ++n; }
    return n ? s / n : 0.0;
}

struct Moments { double mean, rms, skew, exkurt; };
Moments moments(const std::vector<float>& v)
{
    double s1 = 0, s2 = 0, s3 = 0, s4 = 0;
    for (float x : v) s1 += x;
    const double mu = s1 / (double)v.size();
    for (float x : v) { const double d = x - mu; s2 += d * d; s3 += d * d * d; s4 += d * d * d * d; }
    const double n = (double)v.size(), var = s2 / n;
    return { mu, std::sqrt(var), (s3 / n) / std::pow(var, 1.5), (s4 / n) / (var * var) - 3.0 };
}

// Shaped noise in cents on osc B at full wheel (mix 1), decimated by boxcar
// averages of `dec` samples, plus the moments of the full-rate stream.
std::vector<float> noiseCents(double sr, double seconds, int dec, Moments* mom)
{
    WheelModSettings s = fullWheelB(false, true, false); s.sourceMix = 1.0f;
    WheelMod m; m.prepare(sr); m.set(s, 1.0f, true);
    const long n = (long)(seconds * sr);
    std::vector<float> out; out.reserve((size_t)(n / dec + 1));
    double acc = 0.0, r1 = 0.0, r2 = 0.0, r3 = 0.0, r4 = 0.0; int k = 0;
    for (long i = 0; i < n; ++i)
    {
        const double c = m.process().freqBCents;
        r1 += c; r2 += c * c; r3 += c * c * c; r4 += c * c * c * c;   // raw moments, |mean| << RMS
        acc += c;
        if (++k == dec) { out.push_back((float)(acc / dec)); acc = 0.0; k = 0; }
    }
    if (mom)
    {
        const double N = (double)n, mu = r1 / N, e2 = r2 / N, e3 = r3 / N, e4 = r4 / N;
        const double var = e2 - mu * mu;
        const double m3 = e3 - 3 * mu * e2 + 2 * mu * mu * mu;
        const double m4 = e4 - 4 * mu * e3 + 6 * mu * mu * e2 - 3 * mu * mu * mu * mu;
        *mom = { mu, std::sqrt(var), m3 / std::pow(var, 1.5), m4 / (var * var) - 3.0 };
    }
    return out;
}

void testNoise()
{
    const char* T = "noise";
    // Moments (reference: 71.94 c RMS, mean +0.24 c, skew -0.825, excess kurtosis
    // +1.92; the half-wheel render has the same skew and kurtosis).
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        Moments mo{};
        noiseCents(sr, 300.0, 1 << 30, &mo);
        check(nearRel(mo.rms, 71.94, 0.03) && std::fabs(mo.mean) < 2.0 && near(mo.skew, -0.825, 0.05) && near(mo.exkurt, 1.92, 0.25),
              T, "%6.0f Hz: RMS %.2f c (71.94), mean %+.2f c, skew %.3f (-0.825), excess kurtosis %.2f (1.92)",
              sr, mo.rms, mo.mean, mo.skew, mo.exkurt);
    }
    // Control: a Gaussian of the same RMS (what "Gaussian-like" would have built).
    {
        Xorshift r; r.seed(7);
        std::vector<float> g(4000000);
        for (float& v : g) { double a = 0; for (int j = 0; j < 12; ++j) a += r.nextFloat(); v = (float)((a - 6.0) * 71.94); }
        const Moments mo = moments(g);
        control(!near(mo.skew, -0.825, 0.05), T, "a symmetric Gaussian noise fails the skew check");
    }

    // Spectrum vs the reference, dB re the 4-16 Hz level. Reference = A7 carrier,
    // zero-crossing estimator, 60 s (bands >= 2 Hz; >= 128 Hz corrected for the
    // estimator's sinc^3 roll-off), and the mean of the A4 / A6 / A7 renders for
    // the bands below 2 Hz. Half-octave bands centred on fc.
    struct B { double fc, ref, tol; };
    const B bands[] = {
        { 0.25, 1.74, 1.0 }, { 0.5, 1.41, 1.0 }, { 1.0, 0.20, 1.0 }, { 2.0, 0.26, 0.75 },
        { 32, -0.23, 0.75 }, { 48, -0.78, 0.75 }, { 64, -1.36, 0.75 }, { 96, -2.85, 0.75 }, { 128, -4.47, 0.75 },
        { 180, -7.36, 0.75 }, { 256, -11.34, 0.75 }, { 362, -16.01, 0.75 }, { 512, -21.15, 0.75 },
    };
    const double sr = 48000.0, secs = 1200.0;
    // HF: boxcar 16 -> 3 kHz, 8192-point segments; the boxcar response is divided out.
    std::vector<double> fH, fL;
    std::vector<float> hf = noiseCents(sr, secs, 16, nullptr);
    std::vector<double> pH = welch(hf, 8192, fH, sr / 16);
    for (size_t k = 1; k < pH.size(); ++k)
    {
        const double a = 3.14159265358979 * fH[k] / sr;
        const double hBox = std::sin(16 * a) / (16 * std::sin(a));
        pH[k] /= hBox * hBox;
    }
    // LF: boxcar 256 -> 187.5 Hz, 4096-point segments (0.046 Hz bins).
    std::vector<float> lf(hf.size() / 16);
    for (size_t i = 0; i < lf.size(); ++i) { double a = 0; for (int j = 0; j < 16; ++j) a += hf[i * 16 + (size_t)j]; lf[i] = (float)(a / 16); }
    std::vector<double> pL = welch(lf, 4096, fL, sr / 256);
    const double refH = bandMean(fH, pH, 4, 16), refL = bandMean(fL, pL, 4, 16);
    bool allOk = true;
    std::vector<double> got;
    for (const B& b : bands)
    {
        const bool low = b.fc < 2.5;
        const auto& f = low ? fL : fH; const auto& p = low ? pL : pH;
        const double db = 10.0 * std::log10(bandMean(f, p, b.fc / std::pow(2.0, 0.25), b.fc * std::pow(2.0, 0.25)) / (low ? refL : refH));
        got.push_back(db);
        const bool ok = near(db, b.ref, b.tol);
        allOk &= ok;
        std::printf("      %7.2f Hz  %+7.2f dB  (reference %+7.2f, tol %.2f)%s\n", b.fc, db, b.ref, b.tol, ok ? "" : "  <-- out");
    }
    check(allOk, T, "deviation spectrum within tolerance in all %d bands (0.25 .. 512 Hz)", (int)(sizeof bands / sizeof bands[0]));
    // Control: plain white noise (no shaping) through the same analysis.
    {
        Xorshift r; r.seed(11);
        std::vector<float> w(hf.size());
        for (float& v : w) v = r.nextBipolar();
        std::vector<double> fw; std::vector<double> pw = welch(w, 8192, fw, sr / 16);
        const double db = 10.0 * std::log10(bandMean(fw, pw, 256 / std::pow(2.0, 0.25), 256 * std::pow(2.0, 0.25)) / bandMean(fw, pw, 4, 16));
        control(!near(db, -11.34, 0.75), T, "unshaped white noise fails the 256 Hz band");
    }
}

// --------------------------------------------------------------------------
void testSH()
{
    const char* T = "sh";
    WheelModSettings s = fullWheelB(false, true, false); s.sourceMix = 1.0f; s.sampleAndHold = true; s.rateHz = 26.2f;
    WheelMod m; m.prepare(48000.0); m.set(s, 1.0f, true);
    std::vector<float> levels;
    float prevV = m.process().freqBCents; double prevPh = m.lfoPhase();
    long changes = 0, wraps = 0;
    for (long i = 0; i < (long)(600.0 * 48000.0); ++i)
    {
        const float v = m.process().freqBCents;
        const double ph = m.lfoPhase();
        // The value read on this sample was taken BEFORE this sample's advance, so a
        // change shows on the sample after the wrap.
        if (v != prevV) { ++changes; levels.push_back(v); }
        if (ph < prevPh) ++wraps;
        prevV = v; prevPh = ph;
    }
    const Moments mo = moments(levels);
    double lo = 1e30, hi = -1e30, lag = 0.0;
    for (float v : levels) { lo = std::min(lo, (double)v); hi = std::max(hi, (double)v); }
    for (size_t i = 1; i < levels.size(); ++i) lag += (levels[i] - mo.mean) * (levels[i - 1] - mo.mean);
    lag /= (double)(levels.size() - 1) * mo.rms * mo.rms;
    check(std::labs(changes - wraps) <= 1, T, "one step per LFO cycle (%ld steps, %ld cycles)", changes, wraps);
    check(near(hi, 801.0, 8.0) && near(lo, -801.0, 8.0), T, "range %.1f .. %.1f c (reference about +-801)", lo, hi);
    check(near(mo.exkurt, -1.2, 0.1) && std::fabs(mo.mean) < 20.0 && std::fabs(lag) < 0.05, T,
          "uniform: excess kurtosis %.3f (reference -1.20), mean %+.1f c, lag-1 corr %.3f", mo.exkurt, mo.mean, lag);
    // Step timing: with the saw in the mix the S&H steps land on the saw reset only
    // (reference: S&H + saw at mix 0.5 shows one jump per cycle, none mid-cycle).
    WheelModSettings t = fullWheelB(true, false, false); t.sourceMix = 0.5f; t.sampleAndHold = true; t.rateHz = 2.0f;
    WheelMod n; n.prepare(48000.0); n.set(t, 1.0f, true);
    float pv = n.process().freqBCents; int jumps = 0;
    const double rampStep = 0.5 * 1175.06 * 2.0 / 48000.0;
    for (int i = 0; i < 10 * 48000 + 6000; ++i)   // 20 resets, none near the end
    {
        const float v = n.process().freqBCents;
        if (std::fabs(v - pv - rampStep) > 1.0) ++jumps;
        pv = v;
    }
    check(jumps == 20, T, "saw + S&H at mix 0.5: %d jumps in 20 cycles (only the saw resets)", jumps);
    control(!near(mo.exkurt, 1.92, 0.1), T, "the S&H is not the shaped noise (kurtosis differs)");
}

// --------------------------------------------------------------------------
void testShared()
{
    const char* T = "shared";
    WheelModSettings s = fullWheelB(true, true, false); s.sourceMix = 0.4f; s.rateHz = 6.0f;
    s.freqA = s.pwA = s.pwB = s.filter = true;
    WheelMod a, b; a.prepare(48000.0); b.prepare(48000.0);
    a.set(s, 0.7f, true); b.set(s, 0.7f, true);
    Xorshift r; r.seed(3);
    bool same = true; long i = 0;
    while (i < 10 * 48000)
    {
        const int seg = 1 + r.nextInt(4096);
        b.set(s, 0.7f, false);   // the engine re-sends the settings every block
        for (int k = 0; k < seg && i < 10 * 48000; ++k, ++i)
        {
            const WheelModFrame x = a.process(), y = b.process();
            same &= std::memcmp(&x, &y, sizeof x) == 0;
        }
    }
    check(same, T, "free-running and block-size invariant: random 1..4096 blocks match one block, bit for bit");
    // Free-running across "notes": the module has no note input at all, so the
    // phase cannot be retriggered; reset() is the only thing that restarts it.
    WheelMod c; c.prepare(48000.0); c.set(s, 0.7f, true);
    for (int k = 0; k < 12345; ++k) c.process();
    WheelMod d; d.prepare(48000.0); d.set(s, 0.7f, true);
    c.reset(); c.set(s, 0.7f, true);
    bool resetSame = true;
    for (int k = 0; k < 48000; ++k) { const WheelModFrame x = c.process(), y = d.process(); resetSame &= std::memcmp(&x, &y, sizeof x) == 0; }
    check(resetSame, T, "reset() restarts phase and streams deterministically");
    // Control: the same comparison sees an instance that is 12345 samples ahead.
    WheelMod e; e.prepare(48000.0); e.set(s, 0.7f, true);
    WheelMod g; g.prepare(48000.0); g.set(s, 0.7f, true);
    for (int k = 0; k < 12345; ++k) e.process();
    bool differs = false;
    for (int k = 0; k < 48000; ++k) { const WheelModFrame x = e.process(), y = g.process(); differs |= std::memcmp(&x, &y, sizeof x) != 0; }
    control(differs, T, "an instance at another phase does not compare equal");
}

// --------------------------------------------------------------------------
void testOff()
{
    const char* T = "off";
    // Everything but the destinations at its most active: all shapes, S&H, half
    // mix, both limits, the wheel moving every block.
    WheelModSettings s; s.saw = s.tri = s.square = true; s.sampleAndHold = true; s.sourceMix = 0.5f;
    s.lowerLimit = 0.7f; s.upperLimit = 0.2f; s.rateHz = 26.2f;
    WheelMod m; m.prepare(48000.0);
    bool allZero = true, inactive = true;
    for (int blk = 0; blk < 2000; ++blk)
    {
        s.sampleAndHold = (blk / 100) % 2 == 0;
        m.set(s, (float)(blk % 128) / 127.0f, false);
        inactive &= !m.active();
        for (int k = 0; k < 256; ++k)
        {
            const WheelModFrame f = m.process();
            allZero &= bits(f.freqACents) == 0 && bits(f.freqBCents) == 0 && bits(f.pwA) == 0
                    && bits(f.pwB) == 0 && bits(f.filterOct) == 0;
        }
    }
    check(allZero && inactive, T, "no destination: every frame field is +0.0f and active() is false (%d samples)", 2000 * 256);
    s.filter = true; m.set(s, 1.0f, false);
    bool moved = false;
    for (int k = 0; k < 4800; ++k) moved |= m.process().filterOct != 0.0f;
    control(moved && m.active(), T, "one destination on makes the frame non-zero and the section active");
}

// --------------------------------------------------------------------------
void testFade()
{
    const char* T = "fade";
    const double sr = 48000.0;
    WheelModSettings s = fullWheelB(false, false, true); s.freqB = false;   // square: constant +804 c in the first half
    s.rateHz = 0.1f;
    WheelMod m; m.prepare(sr); m.set(s, 1.0f, true);
    for (int k = 0; k < 100; ++k) m.process();
    s.freqB = true; m.set(s, 1.0f, false);
    const int fadeN = (int)std::ceil(WheelMod::kDestFadeSeconds * sr);
    float prev = 0.0f, maxStep = 0.0f; int reached = -1;
    for (int k = 0; k < 2 * fadeN; ++k)
    {
        const float v = m.process().freqBCents;
        maxStep = std::max(maxStep, std::fabs(v - prev)); prev = v;
        if (reached < 0 && v >= 804.0f) reached = k;
    }
    // Smoothstep over N samples: steepest step 1.5 x full / N.
    check(reached >= fadeN - 4 && reached <= fadeN + 1 && maxStep <= 1.5f * 804.1f / fadeN * 1.05f, T,
          "switched on: reaches +804 c after %d samples (%d = 5 ms), largest step %.2f c", reached, fadeN, maxStep);
    s.freqB = false; m.set(s, 1.0f, false);
    for (int k = 0; k < fadeN + 2; ++k) m.process();
    const WheelModFrame f = m.process();
    check(bits(f.freqBCents) == 0 && !m.active(), T, "switched off: exact +0.0f and inactive after the 5 ms fade");
    control(maxStep < 804.0f, T, "an instant switch (step 804 c) would exceed the bound");
}

// --------------------------------------------------------------------------
void testSmooth()
{
    const char* T = "smooth";
    const double sr = 48000.0;
    WheelModSettings s = fullWheelB(false, false, true); s.rateHz = 0.05f;   // square high for 10 s
    WheelMod m; m.prepare(sr); m.set(s, 0.0f, true);
    m.process();
    m.set(s, 1.0f, false);
    const int tauN = (int)(kParamSmoothTau * sr);
    float v = 0.0f;
    for (int k = 0; k < tauN; ++k) v = m.process().freqBCents;
    check(nearRel(v / 804.09, 1.0 - std::exp(-1.0), 0.02), T, "wheel 0 -> 1: %.1f %% of the step after one 8 ms time constant (63.2)",
          100.0 * v / 804.09);
    WheelMod n; n.prepare(sr); n.set(s, 0.0f, true); n.process();
    n.set(s, 1.0f, true);
    check(nearRel(n.process().freqBCents, 804.09, 1e-4), T, "snap lands the new depth on the next sample");
    control(!nearRel(v / 804.09, 1.0, 0.02), T, "an unsmoothed wheel would be at 100 % already");
}

// --------------------------------------------------------------------------
void testRobust()
{
    const char* T = "robust";
    WheelModSettings s = fullWheelB(true, true, true); s.freqA = s.pwA = s.pwB = s.filter = true;
    WheelMod m; m.prepare(48000.0); m.set(s, 1.0f, true);
    const float bad[] = { NAN, INFINITY, -INFINITY, 1e30f, -1e30f };
    bool finite = true;
    for (float b : bad)
    {
        WheelModSettings t = s; t.rateHz = b; t.sourceMix = b; t.lowerLimit = b; t.upperLimit = b;
        m.set(t, b, false);
        for (int k = 0; k < 4800; ++k)
        {
            const WheelModFrame f = m.process();
            finite &= std::isfinite(f.freqACents) && std::isfinite(f.freqBCents) && std::isfinite(f.pwA)
                   && std::isfinite(f.pwB) && std::isfinite(f.filterOct);
        }
    }
    check(finite, T, "non-finite and huge settings never reach the output");
    // The noise is bounded whatever the stream does: the cubic's input is clamped.
    const double x = WheelMod::kNoiseInputClamp;
    const double yMax = std::fabs(WheelMod::kFlA) + x * (WheelMod::kFlB + x * (std::fabs(WheelMod::kFlC) + x * WheelMod::kFlD));
    const double bound = WheelMod::kNoiseSigma * yMax * (1.0 + WheelMod::kNoiseShelfK);
    check(bound < 1.5, T, "noise bounded to %.3f bus units (%.0f c at full wheel)", bound, bound * WheelMod::kFreqCentsPerUnit);
}

} // namespace

int main()
{
    testParams();
    testStart();
    testRate();
    testShapes();
    testDepth();
    testLimits();
    testMix();
    testNoise();
    testSH();
    testShared();
    testOff();
    testFade();
    testSmooth();
    testRobust();
    std::printf("\nwheelmod_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
