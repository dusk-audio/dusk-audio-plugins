// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// SectionReverbEngine, for each model (the room, reversed too, the chamber and the vintage plate):
// renders repeat exactly, in place renders as separate buffers do, silence stays silence, a
// reconfiguration with the same values changes nothing, configure/clear/process never allocate (a
// change of model and the whole Size range included), a moved delay layout (Size, the pre-delays,
// Room Scale, Spread, Shape, a reflection's or an echo's time) fades instead of stepping, the
// output is a fixed 32 samples late, and every corner of the control ranges, the echoes' feedback
// at its top included, stays finite and dies away.

#include "../dsp/SectionReverbEngine.h"
#include "DuskDenormals.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

static std::atomic<bool> gCountAllocations { false };
static std::atomic<int>  gAllocations { 0 };
void* operator new (std::size_t size)
{
    if (gCountAllocations.load (std::memory_order_relaxed))
        gAllocations.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (size != 0 ? size : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
int failures = 0;

void checkf (bool ok, const char* fmt, ...)
{
    if (ok) return;
    va_list args;
    va_start (args, fmt);
    std::printf ("FAIL: ");
    std::vprintf (fmt, args);
    std::printf ("\n");
    va_end (args);
    ++failures;
}

using duskverb::SectionReverbConfig;
using duskverb::SectionReverbEngine;
using duskverb::SectionReverbModel;

constexpr double kRate = 48000.0;

// The Live Room configuration.
SectionReverbConfig testRoom()
{
    SectionReverbConfig c;
    c.model = static_cast<int> (SectionReverbModel::Room);
    c.diffusion = 0.0f; c.pattern = 0; c.earlyPredelayMs = 3.0f; c.reverbPredelayMs = 0.0f; c.roomScale = 1.0f;
    c.roomBassXoverHz = 325.0f; c.roomBassBoostDb = 6.0f; c.sizeMeters = 31.5f; c.reverbTime = 1.29523706f;
    c.bassRt = 1.5f; c.bassXoverHz = 225.0f; c.rtHiCutHz = 4500.0f; c.damping = 1; c.tapSlope = 0;
    c.spinHz = 1.8f; c.wanderMs = 4.0f; c.earlyLevelDb = -21.0f; c.earlyFilter = { 0, 20000.0f, 3.0f, -96.0f };
    c.reverbLevelDb = -21.0f; c.reverbFilter = { 0, 12000.0f, 2.0f, -96.0f };
    return c;
}

// The Large Chamber configuration, with the echoes' feedback on so that path runs too.
SectionReverbConfig testChamber()
{
    SectionReverbConfig c;
    c.model = static_cast<int> (SectionReverbModel::Chamber);
    c.predelayMs = 6.0f; c.diffusion = 82.0f; c.shape = 30.0f; c.spread = 42.0f; c.sizeMeters = 29.5f;
    c.reverbTime = 1.71944368f; c.bassRt = 1.5f; c.bassXoverHz = 650.0f; c.rtHiCutHz = 5500.0f; c.damping = 2;
    c.tapSlope = 0; c.spinHz = 1.4f; c.wanderMs = 11.0f; c.echoFeedbackPercent = 50.0f;
    c.earlyLevelDb = -5.5f; c.earlyFilter = { 0, 13000.0f, 1.1f, -96.0f };
    c.reverbLevelDb = -5.5f; c.reverbFilter = { 0, 7625.0f, 1.1f, -96.0f };
    c.reflections[0] = { 21.0f, 0, -9.0f };
    c.reflections[1] = { 26.0f, 0, -9.0f };
    c.echoes[0] = { 6.0f, 0, -5.0f, 1, -6.0f };
    c.echoes[1] = { 10.0f, 0, -4.5f, 1, -6.0f };
    return c;
}

// The Vintage Vocal Plate configuration: three diffusers a side, four echoes with feedback.
SectionReverbConfig testPlate()
{
    SectionReverbConfig c;
    c.model = static_cast<int> (SectionReverbModel::VintagePlate);
    c.predelayMs = 20.0f; c.diffusion = 60.0f; c.shape = 16.0f; c.spread = 30.0f; c.sizeMeters = 16.0f;
    c.reverbTime = 0.716446698f; c.bassRt = 1.0f; c.bassXoverHz = 7875.0f; c.rtHiCutHz = 5500.0f; c.damping = 1;
    c.tapSlope = 0; c.spinHz = 1.8f; c.wanderMs = 3.3f;
    c.earlyLevelDb = -2.0f; c.earlyFilter = { 0, 19500.0f, 1.0f, -96.0f };
    c.reverbLevelDb = -2.0f; c.reverbFilter = { 0, 19500.0f, 0.1f, -96.0f };
    c.reflections[0] = { 20.0f, 0, -12.0f };
    c.reflections[1] = { 30.0f, 0, -12.0f };
    c.echoes[0] = { 40.0f, 0, -12.0f, 1, -13.0f };
    c.echoes[1] = { 57.0f, 0, -12.0f, 0, -13.0f };
    c.echoes[2] = { 151.0f, 0, -18.0f, 1, -23.5f };
    c.echoes[3] = { 134.0f, 0, -18.0f, 1, -23.5f };
    return c;
}

// The Reverse Taps configuration: the room's early pattern back to front, no tail.
SectionReverbConfig testReverseRoom()
{
    SectionReverbConfig c = testRoom();
    c.pattern = 1; c.reverse = 1; c.roomScale = 2.0f; c.earlyPredelayMs = 0.0f; c.reverbLevelDb = -96.0f;
    c.earlyLevelDb = -10.5f;
    return c;
}

struct Model { const char* name; SectionReverbConfig (*config)(); };
const Model kModels[] = { { "room", testRoom }, { "reversed room", testReverseRoom }, { "chamber", testChamber },
                          { "vintage plate", testPlate } };

void prepareWith (SectionReverbEngine& e, const SectionReverbConfig& c)
{
    e.prepare (kRate);
    e.configure (c);
    e.clear();
}

struct Stereo { std::vector<float> l, r; };

Stereo noise (int frames, std::uint32_t seed)
{
    Stereo s { std::vector<float> (static_cast<std::size_t> (frames)), std::vector<float> (static_cast<std::size_t> (frames)) };
    for (int i = 0; i < frames; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        s.l[static_cast<std::size_t> (i)] = static_cast<float> (seed >> 8) / 16777216.0f - 0.5f;
        seed = seed * 1664525u + 1013904223u;
        s.r[static_cast<std::size_t> (i)] = static_cast<float> (seed >> 8) / 16777216.0f - 0.5f;
    }
    return s;
}

void run (SectionReverbEngine& e, const Stereo& in, Stereo& out, int from, int to, int block = 64)
{
    for (int i = from; i < to; i += block)
    {
        const int m = std::min (block, to - i);
        e.process (in.l.data() + i, in.r.data() + i, out.l.data() + i, out.r.data() + i, m);
    }
}

Stereo render (SectionReverbEngine& e, const Stereo& in)
{
    Stereo out { std::vector<float> (in.l.size()), std::vector<float> (in.r.size()) };
    run (e, in, out, 0, static_cast<int> (in.l.size()));
    return out;
}

double rms (const std::vector<float>& x, std::size_t from, std::size_t to)
{
    double e = 0.0;
    for (std::size_t i = from; i < to; ++i)
        e += static_cast<double> (x[i]) * x[i];
    return std::sqrt (e / static_cast<double> (std::max<std::size_t> (1, to - from)));
}

void deterministicAndSilent (const Model& m)
{
    SectionReverbEngine a, b;
    prepareWith (a, m.config());
    prepareWith (b, m.config());
    const Stereo in = noise (48000, 7);
    const Stereo x = render (a, in), y = render (b, in);
    checkf (x.l == y.l && x.r == y.r, "%s: two engines with the same configuration render differently", m.name);
    a.clear();
    const Stereo z = render (a, in);
    checkf (x.l == z.l && x.r == z.r, "%s: clear() does not reproduce the first render", m.name);

    a.clear();
    const Stereo silence { std::vector<float> (48000, 0.0f), std::vector<float> (48000, 0.0f) };
    const Stereo out = render (a, silence);
    bool zero = true;
    for (std::size_t i = 0; i < out.l.size(); ++i)
        zero = zero && out.l[i] == 0.0f && out.r[i] == 0.0f;
    checkf (zero, "%s: silence in does not give silence out", m.name);
}

void sameConfigChangesNothing (const Model& m)
{
    SectionReverbEngine a, b;
    prepareWith (a, m.config());
    prepareWith (b, m.config());
    const Stereo in = noise (24000, 11);
    Stereo x { std::vector<float> (in.l.size()), std::vector<float> (in.r.size()) }, y = x;
    run (a, in, x, 0, 12000);
    run (b, in, y, 0, 12000);
    b.configure (b.config());               // the same values, mid-stream
    run (a, in, x, 12000, 24000);
    run (b, in, y, 12000, 24000);
    checkf (x.l == y.l && x.r == y.r, "%s: reconfiguring with the same values changed the render", m.name);
}

// In place (input and output the same buffers, as DuskVerbDSP runs it) renders exactly what
// separate buffers do, the tail after the input included, at host blocks that split the engine's
// 32-sample blocks.
void inPlaceMatchesSeparate (const Model& m)
{
    for (int block : { 7, 64, 100 })
    {
        SectionReverbEngine a, b;
        prepareWith (a, m.config());
        prepareWith (b, m.config());
        Stereo in = noise (4800, 21);
        for (std::size_t i = 0; i < in.l.size(); ++i)
            in.r[i] *= -0.5f;                                   // the two sides differ
        in.l.resize (48000, 0.0f);
        in.r.resize (48000, 0.0f);
        Stereo separate { std::vector<float> (in.l.size()), std::vector<float> (in.r.size()) };
        run (a, in, separate, 0, static_cast<int> (in.l.size()), block);
        Stereo aliased = in;
        for (int i = 0; i < static_cast<int> (aliased.l.size()); i += block)
        {
            const int n = std::min (block, static_cast<int> (aliased.l.size()) - i);
            b.process (aliased.l.data() + i, aliased.r.data() + i, aliased.l.data() + i, aliased.r.data() + i, n);
        }
        checkf (separate.l == aliased.l && separate.r == aliased.r, "%s, block %d: in place renders differently from separate buffers",
                m.name, block);
    }
}

void neverAllocates()
{
    SectionReverbEngine e;
    prepareWith (e, testRoom());
    const Stereo in = noise (9600, 3);
    Stereo out { std::vector<float> (in.l.size()), std::vector<float> (in.r.size()) };
    gAllocations = 0;
    gCountAllocations = true;
    run (e, in, out, 0, 2400);
    SectionReverbConfig c = e.config();
    c.sizeMeters *= 1.4f;
    c.earlyPredelayMs += 7.0f;
    e.configure (c);
    run (e, in, out, 2400, 4800);
    e.configure (testChamber());            // a change of model
    run (e, in, out, 4800, 6000);
    e.configure (testPlate());
    run (e, in, out, 6000, 7200);
    c = e.config();
    c.spread *= 1.5f;
    c.reflections[0].delayMs += 5.0f;
    e.configure (c);
    run (e, in, out, 7200, 9600);
    // Every model across the whole Size range (2 to 80 m), the largest first.
    for (const Model& m : kModels)
        for (float size : { 80.0f, 2.0f, 80.0f })
        {
            c = m.config();
            e.configure (c);
            run (e, in, out, 0, 1200);
            c.sizeMeters = size;
            e.configure (c);
            run (e, in, out, 1200, 4800);
        }
    e.clear();
    gCountAllocations = false;
    checkf (gAllocations.load() == 0, "configure/clear/process allocated %d times", gAllocations.load());
    bool finite = true;
    for (std::size_t i = 0; i < out.l.size(); ++i)
        finite = finite && std::isfinite (out.l[i]) && std::isfinite (out.r[i]);
    checkf (finite, "a change of model gave non-finite output");
}

// A moved layout starts on the outgoing layout and leaves it gradually: the first sample after
// the change is still the old layout's, and on a smooth (sine) input the output's largest
// one-sample step through the fade stays as small as the unmoved render's own. Stepping the
// delay reads instead would put a discontinuity in it.
void layoutChangesFade()
{
    // A reflection's or an echo's gains fade in 32-sample steps, so a move through them steps a
    // little more than the unmoved render (1.9 times on the plate's echo); a delay read switched
    // without the fade steps 6 times and more.
    struct Move { const char* name; SectionReverbConfig (*config)(); void (*apply) (SectionReverbConfig&); double limit = 1.5; };
    const Move moves[] = {
        { "room Size", testRoom, [] (SectionReverbConfig& c) { c.sizeMeters *= 1.3f; } },
        { "room Pre-Delay", testRoom, [] (SectionReverbConfig& c) { c.earlyPredelayMs += 11.0f; } },
        { "room Room Scale", testRoom, [] (SectionReverbConfig& c) { c.roomScale = 1.6f; } },
        { "chamber Size", testChamber, [] (SectionReverbConfig& c) { c.sizeMeters *= 1.3f; } },
        { "chamber Pre-Delay", testChamber, [] (SectionReverbConfig& c) { c.predelayMs += 11.0f; } },
        { "chamber Spread", testChamber, [] (SectionReverbConfig& c) { c.spread = 70.0f; } },
        { "chamber reflection", testChamber, [] (SectionReverbConfig& c) { c.reflections[0].delayMs += 9.0f; }, 2.5 },
        { "plate Size", testPlate, [] (SectionReverbConfig& c) { c.sizeMeters *= 1.3f; } },
        { "plate echo", testPlate, [] (SectionReverbConfig& c) { c.echoes[2].delayMs += 13.0f; }, 2.5 },
        { "plate Shape", testPlate, [] (SectionReverbConfig& c) { c.shape = 40.0f; } },
    };
    Stereo in { std::vector<float> (36000), std::vector<float> (36000) };
    for (std::size_t i = 0; i < in.l.size(); ++i)
    {
        in.l[i] = 0.5f * static_cast<float> (std::sin (2.0 * 3.141592653589793 * 220.0 * static_cast<double> (i) / kRate));
        in.r[i] = 0.5f * static_cast<float> (std::sin (2.0 * 3.141592653589793 * 277.0 * static_cast<double> (i) / kRate));
    }
    for (const Move& m : moves)
    {
        SectionReverbEngine moved, still;
        prepareWith (moved, m.config());
        prepareWith (still, m.config());
        Stereo a { std::vector<float> (in.l.size()), std::vector<float> (in.r.size()) }, b = a;
        const int at = 24000;
        run (moved, in, a, 0, at);
        run (still, in, b, 0, at);
        SectionReverbConfig c = moved.config();
        m.apply (c);
        moved.configure (c);
        run (moved, in, a, at, static_cast<int> (in.l.size()));
        run (still, in, b, at, static_cast<int> (in.l.size()));
        const double level = rms (b.l, at - 4800, at);
        const double first = std::fabs (a.l[at] - b.l[at]) + std::fabs (a.r[at] - b.r[at]);
        double step = 0.0, own = 0.0;
        for (std::size_t i = at + 1; i < at + 1200; ++i)
            for (const auto* ch : { &a, &b })
            {
                const double d = std::max (std::fabs ((*ch).l[i] - (*ch).l[i - 1]), std::fabs ((*ch).r[i] - (*ch).r[i - 1]));
                (ch == &a ? step : own) = std::max (ch == &a ? step : own, d);
            }
        checkf (first < 1e-3 * level, "%s: the first sample after the change already differs (%g, level %g)", m.name, first, level);
        checkf (step < m.limit * own, "%s: the change steps (largest step %g, unmoved %g)", m.name, step, own);
        bool moved_ = false;
        for (std::size_t i = at + 2000; i < a.l.size() && ! moved_; ++i)
            moved_ = a.l[i] != b.l[i];
        checkf (moved_, "%s: the change never reached the output", m.name);
    }
}

// Nothing comes out before kLatencySamples, whatever the host block size.
void latencyIsFixed (const Model& m)
{
    for (int block : { 1, 7, 32, 100, 512 })
    {
        SectionReverbConfig c = m.config();
        c.earlyPredelayMs = 0.0f;
        c.predelayMs = 0.0f;
        SectionReverbEngine e;
        prepareWith (e, c);
        Stereo silence { std::vector<float> (48000 * 2, 0.0f), std::vector<float> (48000 * 2, 0.0f) };
        Stereo out { silence.l, silence.r };
        run (e, silence, out, 0, 48000, block);   // past the early pattern's fade-in
        Stereo click { silence.l, silence.r };
        click.l[48000] = click.r[48000] = 1.0f;
        run (e, click, out, 48000, 48000 * 2, block);
        int first = -1;
        for (int i = 48000; i < 48000 * 2 && first < 0; ++i)
            if (out.l[static_cast<std::size_t> (i)] != 0.0f || out.r[static_cast<std::size_t> (i)] != 0.0f)
                first = i - 48000;
        checkf (first >= SectionReverbEngine::kLatencySamples, "%s, block %d: output %d samples after the input",
                m.name, block, first);
    }
}

void cornersStayFiniteAndDecay (const Model& m)
{
    const Stereo burst = [] {
        Stereo s = noise (4800, 9);
        s.l.resize (48000 * 12, 0.0f);
        s.r.resize (48000 * 12, 0.0f);
        return s;
    }();
    for (int corner = 0; corner < 32; ++corner)
    {
        SectionReverbConfig c = m.config();
        c.sizeMeters = (corner & 1) ? 80.0f : 2.0f;
        c.reverbTime = (corner & 2) ? 8.0f : 0.0f;          // 0 clamps to the shortest step
        c.bassRt = (corner & 4) ? 4.0f : 0.2f;
        c.damping = (corner & 8) ? 2 : 0;
        c.tapSlope = (corner & 16) ? 10 : -10;
        c.diffusion = 100.0f;
        c.spinHz = 4.0f;
        c.wanderMs = 24.0f;
        c.roomBassBoostDb = 12.0f;
        c.roomScale = 2.0f;
        c.shape = (corner & 1) ? 64.0f : 0.0f;
        c.spread = 100.0f;
        c.echoFeedbackPercent = 100.0f;
        c.echoes[0].feedbackDb = c.echoes[1].feedbackDb = 0.0f;
        c.earlyLevelDb = 0.0f;
        c.reverbLevelDb = 0.0f;
        c.earlyFilter = { 4, 1000.0f, 0.5f, -6.0f };
        c.reverbFilter = { 3, 200.0f, 1.0f, -96.0f };
        SectionReverbEngine e;
        prepareWith (e, c);
        const Stereo out = render (e, burst);
        bool finite = true;
        for (std::size_t i = 0; i < out.l.size(); ++i)
            finite = finite && std::isfinite (out.l[i]) && std::isfinite (out.r[i]);
        checkf (finite, "%s corner %d: non-finite output", m.name, corner);
        // The loudest 0.1 s of the first quarter: at 80 m the sections' first pass comes after
        // 0.3 s, so the opening alone can be quieter than the tail's build-up.
        double loud = 0.0;
        for (std::size_t at = 0; at + 4800 <= out.l.size() / 4; at += 4800)
            loud = std::max (loud, rms (out.l, at, at + 4800) + rms (out.r, at, at + 4800));
        const std::size_t half = out.l.size() / 2;
        const double mid = rms (out.l, half - 4800, half) + rms (out.r, half - 4800, half);
        const double end = rms (out.l, out.l.size() - 4800, out.l.size()) + rms (out.r, out.r.size() - 4800, out.r.size());
        // Bounded and dying away.
        checkf ((end < mid || end == 0.0) && mid < loud, "%s corner %d: not decaying (%g, %g, %g)", m.name, corner, loud, mid, end);
    }
}
} // namespace

int main()
{
    // As the plugin runs the engine (DuskVerbDSP::process).
    duskaudio::ScopedFlushDenormals noDenormals;
    for (const Model& m : kModels)
    {
        deterministicAndSilent (m);
        inPlaceMatchesSeparate (m);
        sameConfigChangesNothing (m);
        latencyIsFixed (m);
        cornersStayFiniteAndDecay (m);
    }
    neverAllocates();
    layoutChangesFade();
    if (failures == 0)
        std::printf ("SectionReverbEngine: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
