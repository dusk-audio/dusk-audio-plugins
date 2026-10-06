// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// RingOutDSPTest.cpp — regression tests for the Ring Out engine, framework-free.
//
//   grid          the FREQ / CUT / Q step rules and clamps
//   text          table and edit-command text forms, strict rejection
//   notch         a filter cuts what it says, where it says; globals apply
//   invariance    block size never changes the output
//   detect        a growing pure tone gets a notch at its frequency, then deeper
//   reject        harmonic-rich tones and noise alone place nothing
//   add / timer   ADD stops after one filter; SETUP switches itself off at 60 s
//   loop          a simulated unstable acoustic loop is stabilised (the control
//                 run without processing is shown to run away)
//
// Build: see plugins/ring-out/daf-plugin/CMakeLists.txt (RingOutCoreTest).

#include "RingOutDSP.hpp"
#include "RingOutFilterTable.hpp"
#include "DuskFilters.hpp"
#include "DuskLogFreqAxis.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using duskaudio::RingOutDSP;
namespace ro = duskaudio::ringout;

static int failures = 0;

#define CHECK(cond, ...)                                                         \
    do {                                                                         \
        if (!(cond))                                                             \
        {                                                                        \
            ++failures;                                                          \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__);                     \
            std::printf(__VA_ARGS__);                                            \
            std::printf("\n");                                                   \
        }                                                                        \
    } while (0)

static bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

//------------------------------------------------------------------------------
static void testGrid()
{
    CHECK(near(ro::stepFreq(24.0f, +1), 25.0f, 1e-4f), "24 Hz steps up by 1 Hz");
    CHECK(near(ro::stepFreq(24.0f, -1), 24.0f, 1e-4f), "24 Hz is the floor");
    CHECK(near(ro::stepFreq(499.0f, +1), 500.0f, 1e-4f), "499 -> 500");
    CHECK(near(ro::stepFreq(500.0f, +1), 510.0f, 1e-4f), "500 steps up by 10");
    CHECK(near(ro::stepFreq(510.0f, -1), 500.0f, 1e-4f), "510 steps down by 10");
    CHECK(near(ro::stepFreq(500.0f, -1), 499.0f, 1e-4f), "500 steps down by 1");
    CHECK(near(ro::stepFreq(1000.0f, +1), 1100.0f, 1e-4f), "1 kHz steps up by 100");
    CHECK(near(ro::stepFreq(1000.0f, -1), 990.0f, 1e-4f), "1 kHz steps down by 10");
    CHECK(near(ro::stepFreq(20000.0f, +1), 20000.0f, 1e-4f), "20 kHz is the ceiling");
    CHECK(near(ro::stepFreq(66.3f, +1), 67.3f, 1e-3f), "a detected 66.3 Hz nudges to 67.3, not to a grid");
    CHECK(near(ro::snapFreq(1234.56f), 1234.6f, 1e-3f), "frequency keeps a tenth of a hertz");
    CHECK(near(ro::stepCut(0.0f, +1), 0.0f, 1e-5f), "cut cannot go above 0 dB");
    CHECK(near(ro::stepCut(0.0f, -1), -0.1f, 1e-5f), "cut steps by 0.1 dB");
    CHECK(near(ro::stepCut(-20.0f, -1), -20.0f, 1e-5f), "cut floor is -20 dB");
    CHECK(near(ro::stepQ(20.0f, +1), 20.0f, 1e-5f), "Q ceiling is 20");
    CHECK(near(ro::stepQ(0.5f, -1), 0.5f, 1e-5f), "Q floor is 0.5");
    CHECK(near(ro::stepQ(2.5f, +1), 2.6f, 1e-5f), "Q steps by 0.1");
}

static void testAxisLabels()
{
    duskdaf::LogFreqAxis axis { 24.0f, 20000.0f };
    char b[16];
    const auto label = [&](float f) { duskdaf::LogFreqAxis::label(f, b, (int)sizeof(b)); return std::string(b); };
    CHECK(label(24.0f) == "24", "24 Hz -> '%s'", b);
    CHECK(label(100.0f) == "100", "100 Hz -> '%s'", b);
    CHECK(label(1000.0f) == "1k", "1 kHz -> '%s'", b);
    CHECK(label(1200.0f) == "1k2", "1.2 kHz -> '%s'", b);
    CHECK(label(12500.0f) == "12k5", "12.5 kHz -> '%s'", b);
    CHECK(label(1999.0f) == "2k", "1999 Hz rounds with carry -> '%s'", b);
    CHECK(label(1050.0f) == "1k1", "1050 Hz -> '%s'", b);
    CHECK(near(axis.fromNorm(axis.toNorm(440.0f)), 440.0f, 0.05f), "axis mapping round-trips");
    float grid[16];
    const int n = axis.gridLines(grid, 16);
    CHECK(n == 9 && near(grid[0], 50.0f, 1e-3f) && near(grid[n - 1], 20000.0f, 1e-3f),
          "1-2-5 grid inside 24 Hz..20 kHz has %d rules, %.0f..%.0f", n, grid[0], grid[n > 0 ? n - 1 : 0]);
}

//------------------------------------------------------------------------------
static void testText()
{
    ro::FilterTable t;
    t.add(ro::Filter{ true, 66.3f, -10.7f, 5.8f });
    t.add(ro::Filter{ false, 901.0f, -8.0f, 5.0f });
    t.add(ro::Filter{ true, 12500.0f, -20.0f, 0.7f });

    char buf[ro::kTableTextCapacity];
    CHECK(ro::serializeTable(t, buf, (int)sizeof(buf)), "serialise");
    CHECK(std::strcmp(buf, "1,66.3,-10.7,5.8;0,901,-8,5;1,12500,-20,0.7") == 0,
          "text form is compact and locale-free: '%s'", buf);

    ro::FilterTable back;
    CHECK(ro::parseTable(buf, back), "parse own output");
    CHECK(ro::tablesEqual(t, back), "round trip");

    ro::FilterTable e;
    CHECK(ro::parseTable("", e) && e.count == 0, "empty text is the empty table");
    CHECK(ro::parseTable("  ", e) && e.count == 0, "whitespace is the empty table");
    CHECK(ro::parseTable("1,66.3,-10.7,5.8;", e) && e.count == 1, "trailing separator tolerated");

    ro::FilterTable bad;
    bad.add(ro::Filter{ true, 100.0f, -1.0f, 1.0f });
    const char* rejects[] = {
        "1,66.3,-10.7",          // missing Q
        "1,66.3,-10.7,nan",      // not a number
        "2,66,-1,1",             // on flag must be 0/1
        "1,10,-1,1",             // below 24 Hz
        "1,30000,-1,1",          // above 20 kHz
        "1,100,-25,1",           // below -20 dB
        "1,100,1,1",             // a boost
        "1,100,-1,0.2",          // Q below 0.5
        "1,1e3,-1,1",            // no exponents
        "1;66;-1;1",             // wrong separators
        "x",
    };
    for (const char* r : rejects)
    {
        ro::FilterTable copy = bad;
        CHECK(!ro::parseTable(r, copy), "reject '%s'", r);
        CHECK(ro::tablesEqual(copy, bad), "rejected text leaves the table untouched ('%s')", r);
    }

    // Twenty-one entries cannot be a table.
    std::string many;
    for (int i = 0; i < 21; ++i) { if (i) many += ';'; many += "1,100,-1,1"; }
    CHECK(!ro::parseTable(many.c_str(), e), "21 entries rejected");
    std::string twenty;
    for (int i = 0; i < 20; ++i) { if (i) twenty += ';'; twenty += "1,100,-1,1"; }
    CHECK(ro::parseTable(twenty.c_str(), e) && e.count == 20, "20 entries accepted");

    // Edit commands.
    ro::EditCommand c;
    CHECK(ro::parseEditCommand("set,3,1,66.3,-10.7,5.8", c) && c.kind == ro::EditCommand::kSet
          && c.slot == 3 && c.filter.on && near(c.filter.freqHz, 66.3f, 1e-3f), "parse set");
    CHECK(ro::parseEditCommand("add,0,24,0,2.5", c) && c.kind == ro::EditCommand::kAdd && !c.filter.on, "parse add");
    CHECK(ro::parseEditCommand("del,19", c) && c.kind == ro::EditCommand::kDelete && c.slot == 19, "parse del");
    CHECK(ro::parseEditCommand("clear", c) && c.kind == ro::EditCommand::kClear, "parse clear");
    CHECK(ro::parseEditCommand("addstop", c) && c.kind == ro::EditCommand::kAddStop, "parse addstop");
    {
        ro::FilterTable untouched; untouched.add(ro::defaultFilter());
        CHECK(ro::applyEditCommand(untouched, c) == -1 && untouched.count == 1, "addstop is not a table edit");
    }
    CHECK(!ro::parseEditCommand("del,20", c), "slot 20 does not exist");
    CHECK(!ro::parseEditCommand("set,0,1,100,-3", c), "incomplete set rejected");
    CHECK(!ro::parseEditCommand("frob", c), "unknown verb rejected");
    CHECK(!ro::parseEditCommand("clear please", c), "trailing text rejected");

    ro::EditCommand f;
    f.kind = ro::EditCommand::kSet; f.slot = 7; f.filter = ro::Filter{ true, 1234.5f, -3.2f, 10.0f };
    char cb[128];
    CHECK(ro::formatEditCommand(f, cb, (int)sizeof(cb)) && std::strcmp(cb, "set,7,1,1234.5,-3.2,10") == 0,
          "format set: '%s'", cb);
    ro::EditCommand g;
    CHECK(ro::parseEditCommand(cb, g) && g.slot == 7 && ro::filtersEqual(g.filter, f.filter), "set round trip");

    // Applying.
    ro::FilterTable a;
    CHECK(ro::applyEditCommand(a, g) == -1, "set on an empty table touches nothing");
    ro::EditCommand add; add.kind = ro::EditCommand::kAdd; add.filter = ro::defaultFilter();
    CHECK(ro::applyEditCommand(a, add) == 0 && a.count == 1, "add -> slot 0");
    CHECK(ro::applyEditCommand(a, add) == 1 && a.count == 2, "add -> slot 1");
    ro::EditCommand del; del.kind = ro::EditCommand::kDelete; del.slot = 0;
    a.f[1].freqHz = 500.0f;
    CHECK(ro::applyEditCommand(a, del) == 0 && a.count == 1 && near(a.f[0].freqHz, 500.0f, 1e-4f),
          "delete closes the gap");
    ro::EditCommand clr; clr.kind = ro::EditCommand::kClear;
    CHECK(ro::applyEditCommand(a, clr) == 0 && a.count == 0, "clear");
    CHECK(ro::applyEditCommand(a, clr) == -1, "clear on empty is a no-op");
}

//------------------------------------------------------------------------------
// Steady-state RMS of a sine through the engine, relative to the input, in dB.
static float measureGainDb(RingOutDSP& dsp, double sr, float freq, float seconds = 1.0f, int block = 256)
{
    const int total = (int)(seconds * sr);
    std::vector<float> inL((size_t)block), inR((size_t)block), outL((size_t)block), outR((size_t)block);
    double sumIn = 0.0, sumOut = 0.0;
    const int measureFrom = total * 3 / 4;
    double phase = 0.0;
    for (int done = 0; done < total; done += block)
    {
        const int n = std::min(block, total - done);
        for (int i = 0; i < n; ++i)
        {
            const float v = 0.5f * (float)std::sin(phase);
            phase += 2.0 * 3.14159265358979323846 * freq / sr;
            inL[(size_t)i] = inR[(size_t)i] = v;
        }
        const float* ins[2] = { inL.data(), inR.data() };
        float* outs[2] = { outL.data(), outR.data() };
        dsp.processBlock(ins, outs, 2, n);
        if (done >= measureFrom)
            for (int i = 0; i < n; ++i)
            {
                sumIn += (double)inL[(size_t)i] * inL[(size_t)i];
                sumOut += (double)outL[(size_t)i] * outL[(size_t)i];
            }
    }
    return (float)(10.0 * std::log10((sumOut + 1e-30) / (sumIn + 1e-30)));
}

static void testNotch()
{
    const double sr = 48000.0;
    RingOutDSP dsp;
    dsp.prepare(sr, 256);
    ro::FilterTable t;
    t.add(ro::Filter{ true, 1000.0f, -12.0f, 5.0f });
    dsp.setTable(t);

    const float at1k = measureGainDb(dsp, sr, 1000.0f);
    CHECK(near(at1k, -12.0f, 0.3f), "-12 dB notch at 1 kHz measures %.2f dB", at1k);
    const float predicted = (float)RingOutDSP::responseDb(t, 1.0f, 0.0f, sr, 1000.0);
    CHECK(near(predicted, -12.0f, 0.05f), "response curve predicts %.2f dB at centre", predicted);
    const float at2k = measureGainDb(dsp, sr, 2000.0f);
    CHECK(at2k > -1.0f, "one octave away the notch is nearly gone: %.2f dB", at2k);
    const float predicted2k = (float)RingOutDSP::responseDb(t, 1.0f, 0.0f, sr, 2000.0);
    CHECK(near(predicted2k, at2k, 0.3f), "curve %.2f dB vs measured %.2f dB at 2 kHz", predicted2k, at2k);

    // Off filter: flat.
    t.f[0].on = false;
    dsp.setTable(t);
    const float off = measureGainDb(dsp, sr, 1000.0f);
    CHECK(near(off, 0.0f, 0.05f), "a switched-off filter is flat: %.2f dB", off);
    t.f[0].on = true;
    dsp.setTable(t);

    // Global AMP offsets every cut; it can flatten but never boost.
    dsp.setGlobalAmpDb(-6.0f);
    const float deeper = measureGainDb(dsp, sr, 1000.0f);
    CHECK(near(deeper, -18.0f, 0.4f), "AMP -6 dB deepens the notch to %.2f dB", deeper);
    dsp.setGlobalAmpDb(24.0f);
    const float flat = measureGainDb(dsp, sr, 1000.0f);
    CHECK(near(flat, 0.0f, 0.1f), "AMP +24 dB flattens a -12 dB notch to %.2f dB, never a boost", flat);
    dsp.setGlobalAmpDb(0.0f);

    // Global Q multiplies every Q: a smaller multiplier widens the notch, so a tone
    // off-centre loses more.
    const float narrowSide = measureGainDb(dsp, sr, 1150.0f);
    dsp.setGlobalQ(0.5f);
    const float wideSide = measureGainDb(dsp, sr, 1150.0f);
    CHECK(wideSide < narrowSide - 1.0f, "Global Q 0.5 widens: %.2f dB -> %.2f dB at 1150 Hz", narrowSide, wideSide);
    dsp.setGlobalQ(1.0f);

    // Output gain.
    dsp.setGainOutDb(-6.0f);
    const float g = measureGainDb(dsp, sr, 4000.0f);
    CHECK(near(g, -6.0f, 0.15f), "GAIN OUT -6 dB: %.2f dB", g);
    dsp.setGainOutDb(0.0f);

    // Bypass: identity, filters and gain out of circuit.
    dsp.setGainOutDb(-6.0f);
    dsp.setBypass(true);
    const float byp = measureGainDb(dsp, sr, 1000.0f);
    CHECK(near(byp, 0.0f, 1e-3f), "bypass is identity: %.3f dB", byp);
    dsp.setBypass(false);
    dsp.setGainOutDb(0.0f);

    // Mono processing.
    {
        RingOutDSP mono;
        mono.prepare(sr, 256);
        mono.setTable(t);
        std::vector<float> in(256), out(256);
        double sumIn = 0, sumOut = 0; double ph = 0;
        for (int b = 0; b < 400; ++b)
        {
            for (int i = 0; i < 256; ++i) { in[(size_t)i] = 0.5f * (float)std::sin(ph); ph += 2.0 * 3.14159265358979323846 * 1000.0 / sr; }
            const float* ins[1] = { in.data() }; float* outs[1] = { out.data() };
            mono.processBlock(ins, outs, 1, 256);
            if (b > 300) for (int i = 0; i < 256; ++i) { sumIn += in[(size_t)i] * in[(size_t)i]; sumOut += out[(size_t)i] * out[(size_t)i]; }
        }
        const float m = (float)(10.0 * std::log10(sumOut / sumIn));
        CHECK(near(m, -12.0f, 0.3f), "mono path notches too: %.2f dB", m);
    }
}

//------------------------------------------------------------------------------
static void testBlockSizeInvariance()
{
    const double sr = 48000.0;
    ro::FilterTable t;
    t.add(ro::Filter{ true, 120.0f, -9.0f, 3.0f });
    t.add(ro::Filter{ true, 2345.0f, -15.0f, 8.0f });
    t.add(ro::Filter{ false, 5000.0f, -5.0f, 2.5f });
    t.add(ro::Filter{ true, 8800.0f, -3.0f, 12.0f });

    std::mt19937 rng(7);
    std::uniform_real_distribution<float> dist(-0.3f, 0.3f);
    const int total = 48000;
    std::vector<float> src((size_t)total);
    for (float& v : src) v = dist(rng);

    auto render = [&](int block)
    {
        RingOutDSP dsp;
        dsp.prepare(sr, block);
        dsp.setTable(t);
        dsp.setGainOutDb(-3.0f);
        std::vector<float> out((size_t)total), outR((size_t)total);
        for (int done = 0; done < total; done += block)
        {
            const int n = std::min(block, total - done);
            const float* ins[2] = { src.data() + done, src.data() + done };
            float* outs[2] = { out.data() + done, outR.data() + done };
            dsp.processBlock(ins, outs, 2, n);
        }
        return out;
    };
    // 100 and 37 are not multiples of the 32-sample update grid, so sub-blocks
    // straddle host blocks; the GAIN OUT ramp from 0 to -3 dB runs during the
    // first 50 ms of every render and must advance identically in all of them.
    const std::vector<float> a = render(64), b = render(480), c = render(4096), d = render(100), e = render(37);
    float maxDiff = 0.0f;
    for (int i = 0; i < total; ++i)
    {
        maxDiff = std::max(maxDiff, std::fabs(a[(size_t)i] - b[(size_t)i]));
        maxDiff = std::max(maxDiff, std::fabs(a[(size_t)i] - c[(size_t)i]));
        maxDiff = std::max(maxDiff, std::fabs(a[(size_t)i] - d[(size_t)i]));
        maxDiff = std::max(maxDiff, std::fabs(a[(size_t)i] - e[(size_t)i]));
    }
    CHECK(maxDiff <= 1e-6f, "block size changes the output by up to %.3g", maxDiff);
}

// A filter deleted while audio runs fades out in place; the filters that move
// down a row keep their slot, so nothing steps and nothing sweeps.
static void testDeleteCrossfade()
{
    const double sr = 48000.0;
    RingOutDSP dsp;
    dsp.prepare(sr, 64);
    ro::FilterTable t;
    t.add(ro::Filter{ true, 1000.0f, -20.0f, 8.0f });
    t.add(ro::Filter{ true, 3000.0f, -6.0f, 5.0f });
    dsp.setTable(t);

    const int block = 64;
    std::vector<float> in((size_t)block), out((size_t)block);
    double phase = 0.0;
    float maxJump = 0.0f, prev = 0.0f;
    double sumLate = 0.0; int nLate = 0;
    const int total = (int)(1.5 * sr);
    bool deleted = false;
    for (int done = 0; done < total; done += block)
    {
        // Block starts are multiples of 64, so test for "reached", not "equal".
        if (!deleted && done >= (int)(0.75 * sr))
        {
            deleted = true;
            ro::EditCommand del; del.kind = ro::EditCommand::kDelete; del.slot = 0;
            CHECK(dsp.applyEdit(del) == 0, "delete slot 0 mid-stream");
        }
        for (int i = 0; i < block; ++i)
        {
            in[(size_t)i] = 0.5f * (float)std::sin(phase);
            phase += 2.0 * 3.14159265358979323846 * 1000.0 / sr;
        }
        const float* ins[1] = { in.data() }; float* outs[1] = { out.data() };
        dsp.processBlock(ins, outs, 1, block);
        for (int i = 0; i < block; ++i)
        {
            const float y = out[(size_t)i];
            if (done > 0 || i > 0) maxJump = std::max(maxJump, std::fabs(y - prev));
            prev = y;
            if (done >= (int)(1.25 * sr)) { sumLate += (double)y * y; ++nLate; }
        }
    }
    // A 0.5-amplitude 1 kHz sine at 48 kHz moves at most 0.0654 per sample; a
    // coefficient step at the delete would show as a far larger jump.
    CHECK(maxJump < 0.085f, "delete is click-free: max sample step %.4f", maxJump);
    const float lateDb = (float)(10.0 * std::log10(sumLate / (double)nLate / 0.125));
    CHECK(lateDb > -1.0f && lateDb < 0.3f, "after the delete the 1 kHz tone is back to %.2f dB", lateDb);

    // The surviving filter still does its job from whatever slot it holds.
    const float at3k = measureGainDb(dsp, sr, 3000.0f);
    CHECK(near(at3k, -6.0f, 0.4f), "the moved-down 3 kHz filter still cuts %.2f dB", at3k);
}

//------------------------------------------------------------------------------
// Pink-ish noise: white noise through a one-pole low-pass, scaled to a peak level.
struct NoiseSource
{
    std::mt19937 rng { 12345 };
    std::uniform_real_distribution<float> dist { -1.0f, 1.0f };
    float lp = 0.0f;
    float next(float gain)
    {
        lp += 0.08f * (dist(rng) - lp);
        return gain * lp * 3.0f;
    }
};

static float dbToLin(float db) { return std::pow(10.0f, db * 0.05f); }

// Runs `seconds` of (noise + tone generator) through the engine.
template <typename ToneFn>
static void runTone(RingOutDSP& dsp, double sr, float seconds, float noiseDb, ToneFn&& tone, int block = 256)
{
    NoiseSource noise;
    const int total = (int)(seconds * sr);
    std::vector<float> in((size_t)block), out((size_t)block), outR((size_t)block);
    int n = 0;
    for (int done = 0; done < total; done += block)
    {
        const int count = std::min(block, total - done);
        for (int i = 0; i < count; ++i, ++n)
            in[(size_t)i] = noise.next(dbToLin(noiseDb)) + tone((double)n / sr);
        const float* ins[2] = { in.data(), in.data() };
        float* outs[2] = { out.data(), outR.data() };
        dsp.processBlock(ins, outs, 2, count);
    }
}

static void testDetect()
{
    const double sr = 48000.0;
    RingOutDSP dsp;
    dsp.prepare(sr, 256);
    dsp.setSense(RingOutDSP::kSenseLow);
    dsp.setSetup(true);

    // A pure tone that grows from -60 to -6 dBFS over 0.6 s and holds, over
    // quiet programme noise.
    const float f0 = 1234.0f;
    auto tone = [&](double t)
    {
        const float db = t < 0.6 ? -60.0f + (float)(t / 0.6) * 54.0f : -6.0f;
        return dbToLin(db) * (float)std::sin(2.0 * 3.14159265358979323846 * f0 * t);
    };
    runTone(dsp, sr, 0.7f, -45.0f, tone);

    ro::FilterTable t;
    dsp.getTable(t);
    CHECK(t.count >= 1, "a growing pure tone gets a filter (count %d)", t.count);
    if (t.count >= 1)
    {
        CHECK(near(t.f[0].freqHz, f0, 6.0f), "filter placed at %.1f Hz for a %.0f Hz tone", t.f[0].freqHz, f0);
        CHECK(t.f[0].on, "placed filter is on");
        CHECK(t.f[0].cutDb <= -6.0f, "initial cut is at least 6 dB (%.1f)", t.f[0].cutDb);
        CHECK(t.f[0].q >= 3.0f && t.f[0].q <= 8.0f, "initial Q within the engine's range (%.1f)", t.f[0].q);
    }
    CHECK(t.count <= 2, "one tone does not scatter filters (count %d)", t.count);
    const float firstCut = t.count >= 1 ? t.f[0].cutDb : 0.0f;

    // The tone keeps ringing (this harness has no loop to close): the engine
    // deepens the same filter rather than adding another.
    runTone(dsp, sr, 1.0f, -45.0f, tone);
    dsp.getTable(t);
    CHECK(t.count >= 1 && t.f[0].cutDb <= firstCut - 3.0f,
          "persistent tone deepens the filter: %.1f -> %.1f dB", firstCut, t.count ? t.f[0].cutDb : 0.0f);
    CHECK(t.count <= 2, "deepening does not multiply filters (count %d)", t.count);

    const RingOutDSP::Status st = dsp.status();
    CHECK(st.setupActive, "SETUP still armed inside its minute");
    CHECK(st.engagementCount >= 2, "engagements counted (%u)", st.engagementCount);
    CHECK(st.tableVersion == dsp.tableVersion(), "status carries the table version");
}

// Runs a steady 1 kHz sine, applies `mutate` at 0.75 s, and reports the largest
// sample-to-sample step over the whole run plus the level of the final quarter
// second in dB relative to the input. A 0.5-amplitude 1 kHz sine at 48 kHz moves
// at most 0.0654 per sample on its own.
template <typename Fn>
static void runStepProbe(RingOutDSP& dsp, Fn&& mutate, float& maxJumpOut, float& lateDbOut)
{
    const double sr = 48000.0;
    const int block = 64;
    std::vector<float> in((size_t)block), out((size_t)block);
    double phase = 0.0;
    float maxJump = 0.0f, prev = 0.0f;
    double sumLateOut = 0.0, sumLateIn = 0.0;
    const int total = (int)(1.5 * sr);
    bool mutated = false;
    for (int done = 0; done < total; done += block)
    {
        if (!mutated && done >= (int)(0.75 * sr)) { mutated = true; mutate(); }
        for (int i = 0; i < block; ++i)
        {
            in[(size_t)i] = 0.5f * (float)std::sin(phase);
            phase += 2.0 * 3.14159265358979323846 * 1000.0 / sr;
        }
        const float* ins[1] = { in.data() }; float* outs[1] = { out.data() };
        dsp.processBlock(ins, outs, 1, block);
        for (int i = 0; i < block; ++i)
        {
            const float y = out[(size_t)i];
            if (done > 0 || i > 0) maxJump = std::max(maxJump, std::fabs(y - prev));
            prev = y;
            if (done >= (int)(1.25 * sr))
            {
                sumLateOut += (double)y * y;
                sumLateIn += (double)in[(size_t)i] * in[(size_t)i];
            }
        }
    }
    maxJumpOut = maxJump;
    // Relative to the input over the same window, so a window that is not a
    // whole number of periods cancels out.
    lateDbOut = (float)(10.0 * std::log10((sumLateOut + 1e-30) / (sumLateIn + 1e-30)));
}

// A FREQ step of +100 Hz at 1 kHz is the same filter moved, not a new one: the
// slot glides, nothing fades out or ramps in.
static void testFreqStepKeepsSlot()
{
    RingOutDSP dsp;
    dsp.prepare(48000.0, 64);
    ro::FilterTable t;
    t.add(ro::Filter{ true, 1000.0f, -20.0f, 8.0f });
    dsp.setTable(t);
    float maxJump = 0.0f, lateDb = 0.0f;
    runStepProbe(dsp, [&]
    {
        ro::EditCommand set; set.kind = ro::EditCommand::kSet; set.slot = 0;
        set.filter = ro::Filter{ true, ro::stepFreq(1000.0f, +1), -20.0f, 8.0f };
        CHECK(dsp.applyEdit(set) == 0 && near(set.filter.freqHz, 1100.0f, 1e-3f), "FREQ + at 1 kHz -> 1100 Hz");
    }, maxJump, lateDb);
    CHECK(maxJump < 0.085f, "a 100 Hz FREQ step mid-tone is click-free: max sample step %.4f", maxJump);
    // The notch now centred at 1100 Hz still reaches 1 kHz with its skirt; the
    // level must match the static response there, i.e. the slot glided and
    // settled rather than a second slot ramping in.
    ro::FilterTable moved;
    moved.add(ro::Filter{ true, 1100.0f, -20.0f, 8.0f });
    const float expected = (float)RingOutDSP::responseDb(moved, 1.0f, 0.0f, 48000.0, 1000.0);
    CHECK(near(lateDb, expected, 0.5f), "after the step 1 kHz sits at %.2f dB (static response %.2f dB)", lateDb, expected);
}

// Bypass crossfades to a bit-exact dry path and back without a click; the
// filters stay warm while bypassed so un-bypass replays no stale tail.
static void testBypassCrossfade()
{
    {
        RingOutDSP dsp;
        dsp.prepare(48000.0, 64);
        ro::FilterTable t;
        t.add(ro::Filter{ true, 1000.0f, -20.0f, 8.0f });
        dsp.setTable(t);
        float maxJump = 0.0f, lateDb = 0.0f;
        runStepProbe(dsp, [&] { dsp.setBypass(true); }, maxJump, lateDb);
        CHECK(maxJump < 0.085f, "engaging bypass is click-free: max sample step %.4f", maxJump);
        CHECK(near(lateDb, 0.0f, 1e-3f), "bypassed output is the dry input: %.4f dB", lateDb);
    }
    {
        RingOutDSP dsp;
        dsp.prepare(48000.0, 64);
        ro::FilterTable t;
        t.add(ro::Filter{ true, 1000.0f, -20.0f, 8.0f });
        dsp.setTable(t);
        dsp.setBypass(true);
        float maxJump = 0.0f, lateDb = 0.0f;
        runStepProbe(dsp, [&] { dsp.setBypass(false); }, maxJump, lateDb);
        CHECK(maxJump < 0.085f, "releasing bypass is click-free: max sample step %.4f", maxJump);
        CHECK(near(lateDb, -20.0f, 0.3f), "after un-bypass the notch is fully in: %.2f dB", lateDb);
    }
}

static void testTimersAndEdges()
{
    {
        // Armed at the constructor's 48 kHz, then activated at 96 kHz: still a minute.
        RingOutDSP dsp;
        dsp.setSetup(true);
        dsp.prepare(96000.0, 256);
        CHECK(near(dsp.status().setupRemainingSeconds, 60.0f, 0.05f),
              "SETUP countdown survives a rate change: %.2f s left", dsp.status().setupRemainingSeconds);
    }
    {
        // After the minute runs out, the next 1 is a rising edge again.
        RingOutDSP dsp;
        dsp.prepare(48000.0, 4096);
        dsp.setSetup(true);
        std::vector<float> z(4096, 0.0f), o(4096), o2(4096);
        const float* ins[2] = { z.data(), z.data() }; float* outs[2] = { o.data(), o2.data() };
        for (int b = 0; b < 720; ++b) dsp.processBlock(ins, outs, 2, 4096);
        CHECK(!dsp.status().setupActive && dsp.status().setupExpired, "expired after a minute");
        dsp.setSetup(true);
        CHECK(dsp.status().setupActive && !dsp.status().setupExpired,
              "a host that still holds 1 re-arms with its next write of 1");
    }
    {
        // An ADD search nobody ends gives up after kAddSeconds, and the next
        // start is a fresh one.
        RingOutDSP dsp;
        dsp.prepare(48000.0, 4096);
        dsp.setAdd(true);
        std::vector<float> z(4096, 0.0f), o(4096), o2(4096);
        const float* ins[2] = { z.data(), z.data() }; float* outs[2] = { o.data(), o2.data() };
        const int blocks = (int)((ro::kAddSeconds + 1.0f) * 48000.0f / 4096.0f);
        for (int b = 0; b < blocks; ++b) dsp.processBlock(ins, outs, 2, 4096);
        CHECK(!dsp.status().addSearching, "ADD gives up after %.0f s", ro::kAddSeconds);
        dsp.setAdd(true);
        CHECK(dsp.status().addSearching, "a new ADD after the timeout starts a new search");
    }
    {
        // Meters never read below the declared parameter floor.
        RingOutDSP dsp;
        dsp.prepare(48000.0, 512);
        std::vector<float> z(512, 0.0f), o(512), o2(512);
        const float* ins[2] = { z.data(), z.data() }; float* outs[2] = { o.data(), o2.data() };
        for (int b = 0; b < 200; ++b) dsp.processBlock(ins, outs, 2, 512);
        CHECK(dsp.inputPeakDb(0) >= RingOutDSP::kDbFloor && dsp.outputPeakDb(1) >= RingOutDSP::kDbFloor,
              "silence meters at the floor: %.1f / %.1f dB", dsp.inputPeakDb(0), dsp.outputPeakDb(1));
    }
    {
        // After reset() (activate, a rate change) the notches are in force
        // from the first block: no ramp-in that would leave the PA bare.
        RingOutDSP dsp;
        dsp.prepare(48000.0, 64);
        ro::FilterTable t;
        t.add(ro::Filter{ true, 1000.0f, -20.0f, 8.0f });
        dsp.setTable(t);
        dsp.reset();
        std::vector<float> in(64), out(64);
        double phase = 0.0, sumIn = 0.0, sumOut = 0.0;
        for (int b = 0; b < 8; ++b)   // the first ~10 ms
        {
            for (int i = 0; i < 64; ++i) { in[(size_t)i] = 0.5f * (float)std::sin(phase); phase += 2.0 * 3.14159265358979323846 * 1000.0 / 48000.0; }
            const float* ins[1] = { in.data() }; float* outs[1] = { out.data() };
            dsp.processBlock(ins, outs, 1, 64);
            if (b >= 4)   // past the filter's own transient
                for (int i = 0; i < 64; ++i) { sumIn += in[(size_t)i] * in[(size_t)i]; sumOut += out[(size_t)i] * out[(size_t)i]; }
        }
        const float early = (float)(10.0 * std::log10(sumOut / sumIn));
        CHECK(early < -12.0f, "notches are in force right after reset: %.1f dB within the first 10 ms", early);
    }
    {
        // A user filter wider than the engine's floor is never narrowed back.
        RingOutDSP dsp;
        dsp.prepare(48000.0, 256);
        ro::FilterTable t;
        t.add(ro::Filter{ true, 1000.0f, ro::kAutoCutFloor, ro::kQMin });
        dsp.setTable(t);
        dsp.setSetup(true);
        runTone(dsp, 48000.0, 1.0f, -45.0f,
                [](double tt) { return 0.3f * (float)std::sin(2.0 * 3.14159265358979323846 * 1000.0 * tt); });
        dsp.getTable(t);
        CHECK(t.count == 1 && near(t.f[0].q, ro::kQMin, 1e-4f),
              "a Q 0.5 user filter stays at Q 0.5 (now %.1f)", t.count ? t.f[0].q : 0.0f);
    }
    {
        // A filter already at the floor has nothing left to give: the table
        // version does not churn while the tone rings on.
        RingOutDSP dsp;
        dsp.prepare(48000.0, 256);
        ro::FilterTable t;
        t.add(ro::Filter{ true, 1000.0f, ro::kAutoCutFloor, ro::kAutoQFloor });
        dsp.setTable(t);
        const unsigned v = dsp.tableVersion();
        dsp.setSetup(true);
        runTone(dsp, 48000.0, 1.5f, -45.0f,
                [](double tt) { return 0.3f * (float)std::sin(2.0 * 3.14159265358979323846 * 1000.0 * tt); });
        CHECK(dsp.tableVersion() == v && dsp.status().engagementCount == 0,
              "no version bump for an engage that changes nothing (version %u -> %u, engagements %u)",
              v, dsp.tableVersion(), dsp.status().engagementCount);
    }
}

// The bottom of the band: a 66 Hz ring (the reference manual's own example) sits
// at bin 5 or 6 of a 4096-point frame, where a scan floor set for the widest
// neighbourhood would skip it; 30 Hz sits between bins 2 and 3.
static void testDetectLow()
{
    {
        RingOutDSP dsp;
        dsp.prepare(48000.0, 256);
        dsp.setSense(RingOutDSP::kSenseLow);
        dsp.setSetup(true);
        auto tone = [](double tt)
        {
            const float db = tt < 0.6 ? -55.0f + (float)(tt / 0.6) * 47.0f : -8.0f;
            return dbToLin(db) * (float)std::sin(2.0 * 3.14159265358979323846 * 30.0 * tt);
        };
        runTone(dsp, 48000.0, 0.8f, -45.0f, tone);
        ro::FilterTable t;
        dsp.getTable(t);
        CHECK(t.count >= 1, "a 30 Hz ring is detected (count %d)", t.count);
        if (t.count >= 1)
            CHECK(near(t.f[0].freqHz, 30.0f, 5.0f), "placed at %.1f Hz for a 30 Hz tone", t.f[0].freqHz);
    }
    const double sr = 48000.0;
    RingOutDSP dsp;
    dsp.prepare(sr, 256);
    dsp.setSense(RingOutDSP::kSenseLow);
    dsp.setSetup(true);
    const float f0 = 66.0f;
    auto tone = [&](double t)
    {
        const float db = t < 0.6 ? -55.0f + (float)(t / 0.6) * 47.0f : -8.0f;
        return dbToLin(db) * (float)std::sin(2.0 * 3.14159265358979323846 * f0 * t);
    };
    runTone(dsp, sr, 0.8f, -45.0f, tone);
    ro::FilterTable t;
    dsp.getTable(t);
    CHECK(t.count >= 1, "a 66 Hz ring is detected (count %d)", t.count);
    if (t.count >= 1)
    {
        CHECK(near(t.f[0].freqHz, f0, 5.0f), "placed at %.1f Hz for a %.0f Hz tone", t.f[0].freqHz, f0);
        CHECK(near(t.f[0].q, 3.0f, 1e-3f), "low-frequency filter takes the widest engine Q (%.1f)", t.f[0].q);
    }
    CHECK(t.count <= 2, "one low tone does not scatter filters (count %d)", t.count);
}

// With every slot taken the engine must not carve an unrelated notch deeper.
static void testTableFull()
{
    const double sr = 48000.0;
    RingOutDSP dsp;
    dsp.prepare(sr, 256);
    dsp.setSense(RingOutDSP::kSenseHigh);
    ro::FilterTable t;
    for (int i = 0; i < ro::kMaxFilters; ++i)
        t.add(ro::Filter{ true, 100.0f * std::pow(1.17f, (float)i), -6.0f, 8.0f });   // 100 Hz .. ~2 kHz
    dsp.setTable(t);
    dsp.setSetup(true);

    // A ring far from every filter: nothing may change.
    auto far = [](double tt) { return 0.3f * (float)std::sin(2.0 * 3.14159265358979323846 * 8000.0 * tt); };
    runTone(dsp, sr, 1.0f, -45.0f, far);
    ro::FilterTable after;
    dsp.getTable(after);
    CHECK(ro::tablesEqual(after, t), "a full table leaves unrelated filters alone");
    CHECK(dsp.status().tableFull, "the status reports the full table");
    CHECK(dsp.status().engagementCount == 0, "no engagement was counted (%u)", dsp.status().engagementCount);
    {
        // The report describes a listening engine: it clears when SETUP stops.
        dsp.setSetup(false);
        std::vector<float> z(256, 0.0f), o(256), o2(256);
        const float* zi[2] = { z.data(), z.data() }; float* zo[2] = { o.data(), o2.data() };
        dsp.processBlock(zi, zo, 2, 256);
        CHECK(!dsp.status().tableFull, "table-full report clears once the engine is idle");
        dsp.setSetup(true);
    }

    // A ring inside filter 10's band: that filter deepens.
    const float f10 = t.f[10].freqHz;
    auto inside = [&](double tt) { return 0.3f * (float)std::sin(2.0 * 3.14159265358979323846 * (double)f10 * tt); };
    runTone(dsp, sr, 1.0f, -45.0f, inside);
    dsp.getTable(after);
    CHECK(after.count == ro::kMaxFilters && after.f[10].cutDb <= t.f[10].cutDb - 3.0f,
          "a ring inside a filter's band deepens it: %.1f -> %.1f dB", t.f[10].cutDb, after.f[10].cutDb);
    // Deleting a filter makes room again and says so.
    ro::EditCommand del; del.kind = ro::EditCommand::kDelete; del.slot = 0;
    CHECK(dsp.applyEdit(del) == 0, "delete one of twenty");
    CHECK(!dsp.status().tableFull, "DEL clears the full-table flag");
    dsp.resetFilters();
    std::vector<float> z(256, 0.0f), o(256), o2(256);
    const float* ins[2] = { z.data(), z.data() }; float* outs[2] = { o.data(), o2.data() };
    dsp.processBlock(ins, outs, 2, 256);
    CHECK(!dsp.status().tableFull, "RESET clears the full-table flag");
}

static void testHighSense()
{
    const double sr = 44100.0;
    RingOutDSP dsp;
    dsp.prepare(sr, 128);
    dsp.setSense(RingOutDSP::kSenseHigh);
    dsp.setSetup(true);
    const float f0 = 315.0f;
    auto tone = [&](double t)
    {
        const float db = t < 0.4 ? -50.0f + (float)(t / 0.4) * 30.0f : -20.0f;
        return dbToLin(db) * (float)std::sin(2.0 * 3.14159265358979323846 * f0 * t);
    };
    runTone(dsp, sr, 0.5f, -50.0f, tone, 128);
    ro::FilterTable t;
    dsp.getTable(t);
    CHECK(t.count >= 1, "High sensitivity catches a quieter, slower tone at 44.1 kHz (count %d)", t.count);
    if (t.count >= 1)
        CHECK(near(t.f[0].freqHz, f0, 6.0f), "placed at %.1f Hz for %.0f Hz", t.f[0].freqHz, f0);
}

static void testReject()
{
    const double sr = 48000.0;
    {
        // Sustained sawtooth: a musical note has harmonics, feedback does not.
        RingOutDSP dsp;
        dsp.prepare(sr, 256);
        dsp.setSense(RingOutDSP::kSenseHigh);
        dsp.setSetup(true);
        auto saw = [](double t)
        {
            const double ph = std::fmod(t * 220.0, 1.0);
            return (float)(0.25 * (2.0 * ph - 1.0));
        };
        runTone(dsp, sr, 1.5f, -45.0f, saw);
        ro::FilterTable t;
        dsp.getTable(t);
        CHECK(t.count == 0, "a sustained sawtooth places no filter (count %d)", t.count);
    }
    {
        // Noise alone.
        RingOutDSP dsp;
        dsp.prepare(sr, 256);
        dsp.setSense(RingOutDSP::kSenseHigh);
        dsp.setSetup(true);
        runTone(dsp, sr, 1.5f, -20.0f, [](double) { return 0.0f; });
        ro::FilterTable t;
        dsp.getTable(t);
        CHECK(t.count == 0, "noise alone places no filter (count %d)", t.count);
    }
    {
        // A tone that decays (a plucked note) is not feedback.
        RingOutDSP dsp;
        dsp.prepare(sr, 256);
        dsp.setSense(RingOutDSP::kSenseLow);
        dsp.setSetup(true);
        auto pluck = [](double t)
        {
            const double env = std::exp(-t * 6.0);
            return (float)(0.5 * env * std::sin(2.0 * 3.14159265358979323846 * 440.0 * t));
        };
        runTone(dsp, sr, 1.0f, -45.0f, pluck);
        ro::FilterTable t;
        dsp.getTable(t);
        CHECK(t.count == 0, "a decaying pure tone places no filter (count %d)", t.count);
    }
    {
        // Engine idle: nothing happens however much it rings.
        RingOutDSP dsp;
        dsp.prepare(sr, 256);
        runTone(dsp, sr, 1.0f, -45.0f, [](double t) { return 0.5f * (float)std::sin(2.0 * 3.14159265358979323846 * 1000.0 * t); });
        ro::FilterTable t;
        dsp.getTable(t);
        CHECK(t.count == 0, "no SETUP, no ADD: no filter (count %d)", t.count);
    }
}

//------------------------------------------------------------------------------
static void testAddAndTimer()
{
    const double sr = 48000.0;
    {
        RingOutDSP dsp;
        dsp.prepare(sr, 256);
        dsp.setAdd(true);
        auto ring =[](double t) { return 0.3f * (float)std::sin(2.0 * 3.14159265358979323846 * 2500.0 * t); };
        runTone(dsp, sr, 0.6f, -45.0f, ring);
        ro::FilterTable t;
        dsp.getTable(t);
        RingOutDSP::Status st = dsp.status();
        CHECK(t.count == 1, "ADD places exactly one filter (count %d)", t.count);
        CHECK(!st.addSearching && st.addSatisfied, "ADD stops searching once a filter is engaged");
        const float cut = t.count ? t.f[0].cutDb : 0.0f;
        runTone(dsp, sr, 1.0f, -45.0f, ring);
        dsp.getTable(t);
        CHECK(t.count == 1 && near(t.f[0].cutDb, cut, 1e-4f), "a satisfied ADD leaves the table alone");
        dsp.setAdd(false);
        st = dsp.status();
        CHECK(!st.addSearching && !st.addSatisfied, "releasing ADD clears both flags");
        dsp.setAdd(true);
        runTone(dsp, sr, 0.6f, -45.0f, ring);
        dsp.getTable(t);
        CHECK(t.count == 1 && t.f[0].cutDb <= cut - 3.0f,
              "a second ADD on the same tone deepens rather than duplicating (%.1f -> %.1f)", cut, t.f[0].cutDb);
    }
    {
        RingOutDSP dsp;
        dsp.prepare(sr, 4096);
        dsp.setSetup(true);
        CHECK(dsp.status().setupActive, "SETUP arms");
        CHECK(near(dsp.status().setupRemainingSeconds, 60.0f, 0.01f), "60 s on the clock");
        std::vector<float> silence(4096, 0.0f), out(4096), outR(4096);
        const float* ins[2] = { silence.data(), silence.data() };
        float* outs[2] = { out.data(), outR.data() };
        const int blocks = (int)(59.0 * sr / 4096.0);
        for (int b = 0; b < blocks; ++b) dsp.processBlock(ins, outs, 2, 4096);
        CHECK(dsp.status().setupActive, "still armed at 59 s");
        for (int b = 0; b < 30; ++b) dsp.processBlock(ins, outs, 2, 4096);
        RingOutDSP::Status st = dsp.status();
        CHECK(!st.setupActive && st.setupExpired, "SETUP switches itself off after a minute");
        dsp.setSetup(false);
        CHECK(!dsp.status().setupExpired, "lowering SETUP clears the expiry flag");
        dsp.setSetup(true);
        CHECK(dsp.status().setupActive && !dsp.status().setupExpired, "re-arms on the next rising edge");
        dsp.setSetup(false);
        CHECK(!dsp.status().setupActive, "SETUP off stops the engine");
    }
    {
        // Table plumbing.
        RingOutDSP dsp;
        const unsigned v0 = dsp.tableVersion();
        ro::EditCommand add; add.kind = ro::EditCommand::kAdd; add.filter = ro::Filter{ true, 300.0f, -4.0f, 3.0f };
        CHECK(dsp.applyEdit(add) == 0, "edit add -> slot 0");
        CHECK(dsp.tableVersion() == v0 + 1, "version bumps on change");
        ro::EditCommand bad; bad.kind = ro::EditCommand::kDelete; bad.slot = 5;
        CHECK(dsp.applyEdit(bad) == -1 && dsp.tableVersion() == v0 + 1, "a no-op edit does not bump the version");
        ro::FilterTable t;
        dsp.getTable(t);
        CHECK(t.count == 1 && near(t.f[0].freqHz, 300.0f, 1e-4f), "getTable sees the edit");
        dsp.setTable(t);
        CHECK(dsp.tableVersion() == v0 + 1, "setTable with an equal table does not bump");
        dsp.resetFilters();
        dsp.getTable(t);
        CHECK(t.count == 0 && dsp.tableVersion() == v0 + 2, "resetFilters clears and bumps");
        ro::FilterTable invalid; invalid.count = 1; invalid.f[0] = ro::Filter{ true, 5.0f, 0.0f, 1.0f };
        dsp.setTable(invalid);
        dsp.getTable(t);
        CHECK(t.count == 0, "an invalid table is refused");
    }
}

//------------------------------------------------------------------------------
// Acoustic loop: microphone = programme + g * room(delay(plugin output)). The room
// is a resonance at 1 kHz; g puts the loop 2 dB over unity there, so without the
// plugin the loop runs away. With SETUP armed it must settle.
static float runLoop(bool process, ro::FilterTable* tableOut, float* firstHalfPeak)
{
    const double sr = 48000.0;
    RingOutDSP dsp;
    dsp.prepare(sr, 64);
    dsp.setSense(RingOutDSP::kSenseLow);
    dsp.setBypass(!process);
    if (process) dsp.setSetup(true);

    duskaudio::Biquad room;
    room.setCoeffs(duskaudio::Biquad::bandPassConstantPeak(sr, 1000.0f, 8.0f));
    const float g = dbToLin(2.0f);
    const int delaySamples = (int)(0.020 * sr);
    std::vector<float> delayLine((size_t)delaySamples, 0.0f);
    int delayPos = 0;

    NoiseSource noise;
    const float programme = dbToLin(-50.0f);
    const int block = 64;
    const int total = (int)(6.0 * sr);
    std::vector<float> in((size_t)block), out((size_t)block);
    float peakLate = 0.0f, peakEarly = 0.0f;
    for (int done = 0; done < total; done += block)
    {
        // The block is shorter than the loop delay, so the whole block's feed
        // is already in the line: read it ahead of writing this block back.
        for (int i = 0; i < block; ++i)
        {
            const float fed = delayLine[(size_t)((delayPos + i) % delaySamples)];
            in[(size_t)i] = noise.next(programme) + g * room.process(fed);
        }
        const float* ins[1] = { in.data() };
        float* outs[1] = { out.data() };
        dsp.processBlock(ins, outs, 1, block);
        for (int i = 0; i < block; ++i)
        {
            // Clip like a real amplifier so the control run cannot overflow.
            const float y = std::max(-4.0f, std::min(4.0f, out[(size_t)i]));
            delayLine[(size_t)delayPos] = y;
            delayPos = (delayPos + 1) % delaySamples;
            const float a = std::fabs(y);
            if (done >= total * 2 / 3) peakLate = std::max(peakLate, a);
            else peakEarly = std::max(peakEarly, a);
        }
    }
    if (tableOut) dsp.getTable(*tableOut);
    if (firstHalfPeak) *firstHalfPeak = peakEarly;
    return peakLate;
}

static void testClosedLoop()
{
    float early = 0.0f;
    const float runaway = runLoop(false, nullptr, &early);
    CHECK(runaway >= 1.0f, "control: the unprocessed loop runs away (late peak %.3f)", runaway);

    ro::FilterTable t;
    const float settled = runLoop(true, &t, &early);
    CHECK(settled < 0.25f, "SETUP stabilises the loop: late peak %.3f (early %.3f)", settled, early);
    CHECK(t.count >= 1 && t.count <= 4, "a few filters, not a comb (count %d)", t.count);
    bool nearResonance = false;
    for (int i = 0; i < t.count; ++i)
        if (t.f[i].on && std::fabs(t.f[i].freqHz - 1000.0f) < 60.0f) nearResonance = true;
    CHECK(nearResonance, "a filter sits at the ringing frequency (first at %.1f Hz)", t.count ? t.f[0].freqHz : 0.0f);
}

//------------------------------------------------------------------------------
int main()
{
    testGrid();
    testAxisLabels();
    testText();
    testNotch();
    testBlockSizeInvariance();
    testDeleteCrossfade();
    testFreqStepKeepsSlot();
    testBypassCrossfade();
    testTimersAndEdges();
    testDetect();
    testDetectLow();
    testHighSense();
    testReject();
    testAddAndTimer();
    testTableFull();
    testClosedLoop();

    if (failures == 0)
        std::printf("RingOutDSPTest: all checks passed\n");
    else
        std::printf("RingOutDSPTest: %d check(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
