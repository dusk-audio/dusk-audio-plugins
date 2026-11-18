// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// EarlyField gates: no allocation on the audio thread, block-size invariant, deterministic, an
// impulse response that is exactly the configured taps and reflections, an engine input left
// alone at inject 0, an input that drains when written silent (Freeze), and a field that takes
// over another's state rendering what that one renders; plus the voicing table and the tuning
// hook's parser (DuskVerbDv2Voicing.hpp).
//
//   DuskVerbEarlyFieldTest --dump-layout CONFIG [--sample-rate SR]
//
// prints the tap layout CONFIG (the hook's text) gives, as JSON, for the check against the tools
// repo's model (classic_lab/earlyfield_screen.py, taps_kernel).

#include "DuskVerbDv2Voicing.hpp"
#include "DuskVerbEarlyField.hpp"
#include "DuskVerbPresets.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <set>
#include <string>
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

using duskverb::EarlyField;
using duskverb::EarlyFieldConfig;
using duskverb::EarlyFieldInput;

constexpr double kSr = 48000.0;

struct Stereo { std::vector<float> l, r; };

Stereo silence (int n) { return { std::vector<float> (static_cast<size_t> (n)), std::vector<float> (static_cast<size_t> (n)) }; }

Stereo noise (int n, std::uint32_t seed)
{
    Stereo s = silence (n);
    for (int i = 0; i < n; ++i)
    {
        seed = seed * 1664525u + 1013904223u; s.l[static_cast<size_t> (i)] = 0.5f * (static_cast<float> (seed >> 8) / 8388608.0f - 1.0f);
        seed = seed * 1664525u + 1013904223u; s.r[static_cast<size_t> (i)] = 0.5f * (static_cast<float> (seed >> 8) / 8388608.0f - 1.0f);
    }
    return s;
}

// A field and the input it reads, prepared together as DuskVerbDSP prepares them.
struct Rig
{
    EarlyFieldInput in;
    EarlyField f;
    Rig (double sampleRate, int maxBlock, const EarlyFieldConfig& c)
    {
        in.prepare (sampleRate, maxBlock);
        f.prepare (in);
        f.configure (c);
    }
};

// The field around an identity engine, `block` samples at a time: what the engine was given
// (preEngine), what the post path rendered, and the output (the engine's plus the post path).
// From block-aligned sample `silentFrom` to `silentTo` the input is written silent, as under Freeze.
struct Run { Stereo engineIn, post, out; };

Run run (Rig& rig, const Stereo& in, int block, int silentFrom = -1, int silentTo = 1 << 30)
{
    const int n = static_cast<int> (in.l.size());
    Run r { in, silence (n), in };
    for (int i = 0; i < n; i += block)
    {
        const int m = std::min (block, n - i);
        rig.in.write (r.out.l.data() + i, r.out.r.data() + i, m, silentFrom >= 0 && i >= silentFrom && i < silentTo);
        rig.f.preEngine (rig.in, r.out.l.data() + i, r.out.r.data() + i, m);
        std::copy (r.out.l.begin() + i, r.out.l.begin() + i + m, r.engineIn.l.begin() + i);
        std::copy (r.out.r.begin() + i, r.out.r.begin() + i + m, r.engineIn.r.begin() + i);
        rig.f.renderPost (rig.in, r.post.l.data() + i, r.post.r.data() + i, m);
        for (int k = i; k < i + m; ++k)
        {
            r.out.l[static_cast<size_t> (k)] += r.post.l[static_cast<size_t> (k)];
            r.out.r[static_cast<size_t> (k)] += r.post.r[static_cast<size_t> (k)];
        }
    }
    return r;
}

bool same (const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() && std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0;
}

// Taps reaching back to the ring's end, injection, both paths, every reflection.
EarlyFieldConfig longConfig()
{
    EarlyFieldConfig c;
    c.enabled = true;
    c.startMs = 300.0f;
    c.windowMs = 100.0f;
    c.taps = 48;
    c.slopeDb = -9.0f;
    c.attackMs = 20.0f;
    c.inject = 0.6f;
    c.directDb = -6.0f;
    c.reflDb = -9.0f;
    c.reflCount = 4;
    c.reflMs = { 5.0f, 150.0f, 333.3f, 399.9f };
    c.lowpassHz = 7000.0f;
    c.burstDb = -12.0f;
    c.burstStartMs = 2.0f;
    c.burstMs = 5.0f;
    c.burstLowpassHz = 9000.0f;
    c.sideDb = -9.0f;
    c.sideStartMs = 12.0f;
    c.sideMs = 30.0f;
    c.sideAttackMs = 2.0f;
    c.sideLowpassHz = 250.0f;
    return c;
}

// The tap layout at 48 kHz is the model's (taps_kernel in the tools repo's
// classic_lab/earlyfield_screen.py, given this config's float values, 2026-10-24): the same
// delays, the gains to float rounding. A change on either side without the other fails here.
void testMatchesTheModel()
{
    EarlyFieldConfig c;
    c.enabled = true;
    c.startMs = 4.3f;
    c.windowMs = 87.6f;
    c.taps = 24;
    c.slopeDb = -7.5f;
    c.attackMs = 12.5f;
    const Rig rig (kSr, 256, c);
    const EarlyField& f = rig.f;
    struct Golden { const char* name; const EarlyField::Layout& layout; int delay[6]; float gain[6]; int lastDelay; float lastGain; };
    const Golden golden[3] = {
        { "injector", f.injector(), { 206, 352, 472, 650, 824, 1040 },
          { -0.07162808f, -0.1356399f, -0.2306873f, 0.5041057f, 1.0f, -0.9567184f }, 4323, 0.4874341f },
        { "left", f.direct (0), { 206, 435, 602, 727, 966, 1064 },
          { 0.07374347f, -0.2012247f, -0.4202342f, -0.7294359f, 1.0f, 0.9800625f }, 4222, -0.5124328f },
        { "right", f.direct (1), { 206, 313, 635, 784, 921, 1035 },
          { 0.07307183f, 0.11675f, 0.4817158f, -0.9293331f, -1.0f, 0.9768714f }, 4290, 0.5006938f },
    };
    const auto near = [] (float a, float b) { return std::abs (a - b) <= 1e-6f * std::abs (b); };
    for (const auto& g : golden)
    {
        checkf (g.layout.count == c.taps, "%s: %d taps, the model %d", g.name, g.layout.count, c.taps);
        for (int k = 0; k < 6; ++k)
        {
            checkf (g.layout.delay[static_cast<size_t> (k)] == g.delay[k], "%s tap %d: delay %d, the model %d",
                    g.name, k, g.layout.delay[static_cast<size_t> (k)], g.delay[k]);
            checkf (near (g.layout.gain[static_cast<size_t> (k)], g.gain[k]), "%s tap %d: gain %.9g, the model %.9g",
                    g.name, k, static_cast<double> (g.layout.gain[static_cast<size_t> (k)]), static_cast<double> (g.gain[k]));
        }
        const size_t last = static_cast<size_t> (c.taps - 1);
        checkf (g.layout.delay[last] == g.lastDelay && near (g.layout.gain[last], g.lastGain),
                "%s last tap: %d samples, %.9g; the model %d, %.9g", g.name, g.layout.delay[last],
                static_cast<double> (g.layout.gain[last]), g.lastDelay, static_cast<double> (g.lastGain));
    }
}

void testNoAllocationOnTheAudioThread()
{
    Rig rig (kSr, 512, longConfig());
    EarlyField other;
    other.prepare (rig.in);
    Stereo s = noise (512, 7u), post = silence (512);
    EarlyFieldConfig short_ = longConfig();
    short_.taps = 5;
    short_.startMs = 0.0f;
    gAllocations.store (0);
    gCountAllocations.store (true);
    for (int i = 0; i < 50; ++i)
    {
        rig.f.configure (i % 2 ? longConfig() : short_);
        rig.in.write (s.l.data(), s.r.data(), 512, i % 3 == 0);
        rig.f.preEngine (rig.in, s.l.data(), s.r.data(), 512);
        rig.f.renderPost (rig.in, post.l.data(), post.r.data(), 512);
        other.copyStateFrom (rig.f);
        if (i % 10 == 0) { rig.f.clearState(); rig.in.clear(); }
    }
    gCountAllocations.store (false);
    checkf (gAllocations.load() == 0, "%d heap allocations in configure/write/preEngine/renderPost/copyStateFrom/clear",
            gAllocations.load());
}

// The longest taps reach back a whole ring less one block; a block that overwrote them before
// renderPost() read them would make the output depend on the block size.
void testBlockSizeInvariant()
{
    const Stereo in = noise (static_cast<int> (1.5 * kSr), 11u);
    Rig ref (kSr, 4096, longConfig());
    const Run expected = run (ref, in, 1);
    for (int block : { 7, 64, 1000, 4096 })
    {
        Rig rig (kSr, 4096, longConfig());
        const Run got = run (rig, in, block);
        checkf (same (got.engineIn.l, expected.engineIn.l) && same (got.engineIn.r, expected.engineIn.r),
                "block size %d changes what the engine is given", block);
        checkf (same (got.out.l, expected.out.l) && same (got.out.r, expected.out.r),
                "block size %d changes the output", block);
    }
}

void testDeterministic()
{
    const Stereo in = noise (24000, 3u);
    Rig a (kSr, 256, longConfig());
    // b goes through another config and a render first: nothing of that may survive configure
    // and clearing its state and its input.
    EarlyFieldConfig other = longConfig();
    other.taps = 9;
    other.slopeDb = 12.0f;
    Rig b (kSr, 256, other);
    run (b, in, 256);
    b.f.configure (longConfig());
    b.f.clearState();
    b.in.clear();
    const Run ra = run (a, in, 256), rb = run (b, in, 256);
    checkf (same (ra.out.l, rb.out.l) && same (ra.out.r, rb.out.r), "two fields with one config render differently");
    for (int side = 0; side < 2; ++side)
        for (int k = 0; k < EarlyField::kMaxTaps; ++k)
            checkf (a.f.direct (side).delay[static_cast<size_t> (k)] == b.f.direct (side).delay[static_cast<size_t> (k)]
                        && a.f.direct (side).gain[static_cast<size_t> (k)] == b.f.direct (side).gain[static_cast<size_t> (k)],
                    "side %d tap %d: the layout depends on what was configured before", side, k);
}

// One-pole lowpass of the module (duskaudio::OnePoleLP), inverted: the early signal it was fed.
std::vector<float> unLowpass (const std::vector<float>& y, float hz)
{
    const float c = 1.0f - std::exp (-duskaudio::kDuskTwoPi * hz / static_cast<float> (kSr));
    std::vector<float> x (y.size());
    float prev = 0.0f;
    for (size_t i = 0; i < y.size(); ++i)
    {
        x[i] = prev + (y[i] - prev) / c;
        prev = y[i];
    }
    return x;
}

// An impulse in: before the lowpass, each side carries exactly `taps` direct taps inside
// [start, start + window] at its layout's delays and gains, its own reflections (the even ones
// left, the odd ones right) at theirs, and nothing else.
void testImpulseResponse()
{
    EarlyFieldConfig c;
    c.enabled = true;
    c.startMs = 10.0f;
    c.windowMs = 50.0f;
    c.taps = 24;
    c.slopeDb = -6.0f;
    c.attackMs = 8.0f;
    c.inject = 0.0f;
    c.directDb = 0.0f;
    c.reflDb = -6.0f;
    c.reflCount = 4;
    c.reflMs = { 3.0f, 4.5f, 70.0f, 85.0f };
    c.lowpassHz = 6000.0f;
    Rig rig (kSr, 512, c);
    const EarlyField& f = rig.f;
    Stereo in = silence (9600);
    in.l[0] = in.r[0] = 1.0f;
    const Run r = run (rig, in, 512);

    const int lo = static_cast<int> (std::lround (c.startMs * 48.0)), hi = static_cast<int> (std::lround ((c.startMs + c.windowMs) * 48.0));
    const float reflGain = std::pow (10.0f, c.reflDb / 20.0f);
    std::set<int> directSets[2];
    for (int side = 0; side < 2; ++side)
    {
        const std::vector<float>& out = side == 0 ? r.out.l : r.out.r;
        std::vector<float> early (out.size());
        for (size_t i = 0; i < out.size(); ++i)
            early[i] = out[i] - (i == 0 ? 1.0f : 0.0f);
        early = unLowpass (early, c.lowpassHz);

        const auto& layout = f.direct (side);
        int inWindow = 0;
        for (int t = 0; t < static_cast<int> (early.size()); ++t)
        {
            const float v = early[static_cast<size_t> (t)];
            if (std::abs (v) < 1e-4f)
                continue;
            float want = 0.0f;
            bool direct = false;
            for (int k = 0; k < layout.count; ++k)
                if (layout.delay[static_cast<size_t> (k)] == t) { want += layout.gain[static_cast<size_t> (k)]; direct = true; }
            for (int rr = side; rr < c.reflCount; rr += 2)
                if (f.reflectionDelay (rr) == t) want += reflGain;
            checkf (want != 0.0f, "side %d: an arrival at %d samples that no tap or reflection makes (%g)", side, t, v);
            checkf (std::abs (v - want) <= 1e-4f, "side %d: arrival at %d samples is %g, its layout says %g", side, t, v, want);
            if (direct)
            {
                ++inWindow;
                directSets[side].insert (t);
                checkf (t >= lo && t <= hi, "side %d: a direct tap at %d samples, outside [%d, %d]", side, t, lo, hi);
            }
        }
        checkf (inWindow == c.taps, "side %d: %d direct taps, the config asks for %d", side, inWindow, c.taps);
        for (int rr = side; rr < c.reflCount; rr += 2)
        {
            const int t = f.reflectionDelay (rr);
            checkf (t == static_cast<int> (std::lround (c.reflMs[static_cast<size_t> (rr)] * 48.0)), "reflection %d sits at %d samples", rr, t);
            checkf (std::abs (early[static_cast<size_t> (t)] - reflGain) <= 1e-4f,
                    "side %d: reflection %d reads %g, want %g", side, rr, early[static_cast<size_t> (t)], reflGain);
        }
        for (int rr = 1 - side; rr < c.reflCount; rr += 2)
            checkf (std::abs (early[static_cast<size_t> (f.reflectionDelay (rr))]) < 1e-4f,
                    "side %d hears reflection %d, which belongs to the other side", side, rr);
    }
    checkf (directSets[0] != directSets[1], "both sides have the same direct taps (one stream, not two)");
}

// inject 0: the engine is given exactly its input, down to the sign of a zero. inject > 0:
// (1 - inject) x the input plus the injector, renormalised so the injected part carries
// inject x the input's energy.
void testInjection()
{
    {
        EarlyFieldConfig c = longConfig();
        c.inject = 0.0f;
        Rig rig (kSr, 256, c);
        Stereo in = noise (48000, 5u);
        for (size_t i = 0; i < in.l.size(); i += 97)
            in.l[i] = in.r[i] = -0.0f;
        const Run r = run (rig, in, 256);
        checkf (same (r.engineIn.l, in.l) && same (r.engineIn.r, in.r), "inject 0 changes the engine's input");
    }
    EarlyFieldConfig c = longConfig();
    c.startMs = 5.0f;
    c.windowMs = 60.0f;
    c.taps = 16;
    c.inject = 0.5f;
    Rig rig (kSr, 256, c);
    Stereo in = silence (4800);
    in.l[0] = in.r[0] = 1.0f;
    const Run r = run (rig, in, 256);
    const auto& inj = rig.f.injector();
    double e = 0.0;
    for (int k = 0; k < inj.count; ++k)
        e += static_cast<double> (inj.gain[static_cast<size_t> (k)]) * inj.gain[static_cast<size_t> (k)];
    const double send = 0.5 / std::sqrt (e);
    double injected = 0.0;
    for (int t = 0; t < 4800; ++t)
    {
        double want = t == 0 ? 0.5 : 0.0;
        for (int k = 0; k < inj.count; ++k)
            if (inj.delay[static_cast<size_t> (k)] == t) want += send * inj.gain[static_cast<size_t> (k)];
        const double got = r.engineIn.l[static_cast<size_t> (t)];
        checkf (std::abs (got - want) <= 1e-6, "inject 0.5: engine input at %d samples is %g, want %g", t, got, want);
        checkf (r.engineIn.r[static_cast<size_t> (t)] == r.engineIn.l[static_cast<size_t> (t)], "the injector differs between the sides");
        if (t > 0) injected += got * got;
    }
    checkf (std::abs (injected - 0.25) <= 1e-5, "the injected part carries %g of the input's energy, want inject^2 = 0.25", injected);
}

// Both paths at kOffDb: the field has no post path, so nothing of it reaches the output.
void testOffPathsAreSilent()
{
    EarlyFieldConfig c = longConfig();
    c.inject = 0.0f;
    c.directDb = EarlyField::kOffDb;
    c.reflDb = EarlyField::kOffDb;
    c.burstDb = EarlyField::kOffDb;
    c.sideDb = EarlyField::kOffDb;
    Rig rig (kSr, 256, c);
    checkf (rig.f.enabled() && ! rig.f.postEnabled(), "a field with every post path off reports a post path");
    const Stereo in = noise (24000, 9u);
    const Run r = run (rig, in, 256);
    checkf (same (r.out.l, in.l) && same (r.out.r, in.r), "a field with both paths off changes the output");
    c.directDb = -30.0f;
    rig.f.configure (c);
    checkf (rig.f.postEnabled(), "a field with its direct path on reports no post path");
    c.directDb = EarlyField::kOffDb;
    c.reflCount = 1;
    c.reflDb = -30.0f;
    rig.f.configure (c);
    checkf (rig.f.postEnabled(), "a field with a reflection on reports no post path");
    c.reflDb = EarlyField::kOffDb;
    c.burstDb = -30.0f;
    rig.f.configure (c);
    checkf (rig.f.postEnabled(), "a field with its onset burst on reports no post path");
    c.burstDb = EarlyField::kOffDb;
    c.sideDb = -30.0f;
    rig.f.configure (c);
    checkf (rig.f.postEnabled(), "a field with its side burst on reports no post path");

    EarlyFieldInput unprepared;
    EarlyField u;
    u.configure (longConfig());
    checkf (! u.enabled() && ! u.postEnabled(), "an unprepared field reports itself enabled");
    unprepared.prepare (kSr, 256);
    u.prepare (unprepared);
    checkf (u.enabled() && u.postEnabled(), "a prepared field with an enabled config reports itself disabled");
    u.configure (EarlyFieldConfig {});
    checkf (! u.enabled() && ! u.postEnabled(), "the default config is enabled");
}

// Written silent (Freeze), the input takes nothing new once its fade is over: a burst arriving
// then reaches neither the post path nor, through the injector, the engine (only its own dry
// share does), and the post path drains once the longest tap and reflection have passed the
// last sample the input took.
void testASilentInputDrains()
{
    EarlyFieldConfig c = longConfig();
    c.startMs = 2.0f;
    c.windowMs = 60.0f;
    c.reflMs = { 5.0f, 20.0f, 70.0f, 90.0f };
    const int n = 24000, silentFrom = 4096;
    const int fade = static_cast<int> (std::lround (EarlyFieldInput::kSilenceFadeMs * 48.0));
    const Stereo in = noise (n, 13u);
    Stereo burst = in;
    for (int t = silentFrom + fade + 100; t < silentFrom + fade + 120; ++t)
        burst.l[static_cast<size_t> (t)] = burst.r[static_cast<size_t> (t)] = 1.0f;
    Rig quiet (kSr, 256, c), loud (kSr, 256, c);
    const Run a = run (quiet, in, 256, silentFrom), b = run (loud, burst, 256, silentFrom);
    checkf (same (a.post.l, b.post.l) && same (a.post.r, b.post.r), "a burst after the input went silent reaches the post path");
    const float keep = 1.0f - c.inject;
    double sent = 0.0;
    for (size_t i = 0; i < static_cast<size_t> (n); ++i)
        sent = std::max ({ sent, std::abs (static_cast<double> (b.engineIn.l[i] - a.engineIn.l[i]) - keep * (burst.l[i] - in.l[i])),
                           std::abs (static_cast<double> (b.engineIn.r[i] - a.engineIn.r[i]) - keep * (burst.r[i] - in.r[i])) });
    checkf (sent < 1e-6, "a burst after the input went silent reaches the engine through the injector (%g)", sent);

    const int longest = static_cast<int> (std::lround (90.0 * 48.0));
    float before = 0.0f, after = 0.0f;
    for (int t = 0; t < n; ++t)
    {
        const size_t i = static_cast<size_t> (t);
        const float v = std::max (std::abs (a.post.l[i]), std::abs (a.post.r[i]));
        if (t < silentFrom) before = std::max (before, v);
        if (t >= silentFrom + fade + longest + 400) after = std::max (after, v);
    }
    std::printf ("EarlyField: written silent, the post path drains from %.3g to %.3g\n",
                 static_cast<double> (before), static_cast<double> (after));
    checkf (before > 0.05f, "the field is too quiet to test (peak %g)", before);
    checkf (after < 1e-30f, "a silent input still sounds %g after the field's longest delay", after);
}

// Into silence and out again (Freeze on, then off), the input fades over kSilenceFadeMs as a
// raised cosine instead of stepping: its gain never moves by more than the raised cosine's
// steepest slope in one sample, is exactly 0 between the fades and exactly 1 outside them.
// Probed through a single injector tap at delay 0 with inject 1, so the engine is given exactly
// what the input took.
void testSilenceFadesInAndOut()
{
    EarlyFieldConfig c;
    c.enabled = true;
    c.startMs = 0.0f;
    c.windowMs = 0.0f;
    c.taps = 1;
    c.inject = 1.0f;
    Rig rig (kSr, 256, c);
    const int n = 9600, from = 2000, to = 4400;
    const int fade = static_cast<int> (std::lround (EarlyFieldInput::kSilenceFadeMs * 48.0));
    Stereo in = silence (n);
    std::fill (in.l.begin(), in.l.end(), 0.5f);
    std::fill (in.r.begin(), in.r.end(), 0.5f);
    const Run r = run (rig, in, 1, from, to);
    const double sign = rig.f.injector().gain[0];          // the single tap, at full scale
    const double steepest = 0.5 * 3.14159265358979 / fade;
    double worstStep = 0.0, worstShape = 0.0;
    bool exact = true;
    double prev = 1.0;
    for (int t = 0; t < n; ++t)
    {
        const double g = r.engineIn.l[static_cast<size_t> (t)] / (0.5 * sign);
        worstStep = std::max (worstStep, std::abs (g - prev));
        prev = g;
        // k samples into a fade, its float steps land on the end within a sample of `fade`.
        const bool silentNow = t >= from && t < to;
        const double k = silentNow ? t - from + 1.0 : t - to + 1.0;
        double want = 1.0;
        if (silentNow)
            want = 0.5 - 0.5 * std::cos (3.14159265358979 * std::max (0.0, 1.0 - k / fade));
        else if (t >= to)
            want = 0.5 - 0.5 * std::cos (3.14159265358979 * std::min (1.0, k / fade));
        worstShape = std::max (worstShape, std::abs (g - want));
        if ((t < from || k > fade + 1) && g != (silentNow ? 0.0 : 1.0))
            exact = false;
        checkf (r.engineIn.r[static_cast<size_t> (t)] == r.engineIn.l[static_cast<size_t> (t)], "the sides fade apart at %d", t);
    }
    std::printf ("EarlyField: into and out of silence, the input's gain moves by at most %.4f a sample (a %.0f ms raised"
                 " cosine at most %.4f), %.2g from its shape\n", worstStep, static_cast<double> (EarlyFieldInput::kSilenceFadeMs),
                 steepest, worstShape);
    checkf (worstStep <= 1.01 * steepest, "the input steps by %.4f in one sample, more than a %g ms fade's %.4f",
            worstStep, static_cast<double> (EarlyFieldInput::kSilenceFadeMs), steepest);
    checkf (worstShape <= 1e-4, "the fade is %g from a raised cosine", worstShape);
    checkf (exact, "the input is not exactly silent between the fades, or not exactly itself outside them");

    // A ring that starts silent (prepared, or cleared, while frozen) takes nothing at all.
    Rig frozen (kSr, 256, c);
    const Run f = run (frozen, in, 1, 0, n);
    double leaked = 0.0;
    for (float v : f.engineIn.l)
        leaked = std::max (leaked, static_cast<double> (std::abs (v)));
    checkf (leaked == 0.0, "a ring prepared while silent fades in from the input it should not take (%g)", leaked);
}

// A field configured late takes over a running one's state (as at a preset swap, with the same
// preset): reading the same input it renders the running field's samples exactly from then on.
// Started over instead, it does not, until its lowpass has forgotten.
void testTakingOverRendersTheSameSamples()
{
    const Stereo in = noise (12000, 17u);
    EarlyFieldConfig c = longConfig();
    c.startMs = 1.0f;
    c.windowMs = 40.0f;
    c.lowpassHz = 1500.0f;
    EarlyFieldInput input;
    input.prepare (kSr, 256);
    EarlyField running, takeover, fresh;
    for (EarlyField* f : { &running, &takeover, &fresh })
    {
        f->prepare (input);
        f->configure (f == &running ? c : EarlyFieldConfig {});
    }
    std::vector<float> a (256), b (256), x (256), y (256), p (256), q (256);
    double takenOver = 0.0, startedOver = 0.0;
    for (int block = 0; block * 256 < 12000; ++block)
    {
        const size_t at = static_cast<size_t> (block * 256);
        input.write (in.l.data() + at, in.r.data() + at, 256, false);
        if (block == 20)
        {
            takeover.configure (c);
            takeover.copyStateFrom (running);
            fresh.configure (c);
        }
        running.renderPost (input, a.data(), b.data(), 256);
        if (block < 20)
            continue;
        takeover.renderPost (input, x.data(), y.data(), 256);
        fresh.renderPost (input, p.data(), q.data(), 256);
        for (size_t i = 0; i < 256; ++i)
        {
            takenOver = std::max ({ takenOver, std::abs (static_cast<double> (x[i]) - a[i]), std::abs (static_cast<double> (y[i]) - b[i]) });
            startedOver = std::max (startedOver, std::abs (static_cast<double> (p[i]) - a[i]));
        }
    }
    checkf (takenOver == 0.0, "a field that took over another's state renders %g away from it", takenOver);
    checkf (startedOver > 1e-3, "a field started over renders as one that took over (%g): the test cannot see the state", startedOver);
}

// The layout keeps its times in ms at every rate.
void testSampleRates()
{
    const Rig a (48000.0, 256, longConfig()), b (96000.0, 256, longConfig());
    for (int k = 0; k < longConfig().taps; ++k)
    {
        const int da = a.f.direct (0).delay[static_cast<size_t> (k)], db = b.f.direct (0).delay[static_cast<size_t> (k)];
        checkf (std::abs (db - 2 * da) <= 1, "tap %d: %d samples at 96 kHz, %d at 48 kHz", k, db, da);
        checkf (a.f.direct (0).gain[static_cast<size_t> (k)] == b.f.direct (0).gain[static_cast<size_t> (k)], "tap %d: the gain depends on the rate", k);
    }
    checkf (b.f.reflectionDelay (3) == static_cast<int> (std::lround (399.9 * 96.0)), "a 399.9 ms reflection at 96 kHz sits at %d samples", b.f.reflectionDelay (3));
}

bool sameConfig (const EarlyFieldConfig& a, const EarlyFieldConfig& b)
{
    return a.enabled == b.enabled && a.startMs == b.startMs && a.windowMs == b.windowMs && a.taps == b.taps
        && a.slopeDb == b.slopeDb && a.attackMs == b.attackMs && a.inject == b.inject && a.directDb == b.directDb
        && a.reflDb == b.reflDb && a.reflCount == b.reflCount && a.reflMs == b.reflMs && a.lowpassHz == b.lowpassHz
        && a.burstDb == b.burstDb && a.burstStartMs == b.burstStartMs && a.burstMs == b.burstMs
        && a.burstLowpassHz == b.burstLowpassHz && a.sideDb == b.sideDb && a.sideStartMs == b.sideStartMs
        && a.sideMs == b.sideMs && a.sideAttackMs == b.sideAttackMs && a.sideLowpassHz == b.sideLowpassHz;
}

// The side burst: velvet pulses inside [start, start + length], peak 1, played +left / -right, and
// lowpassed: nothing of it reaches the mid, and its energy sits under its corner.
void testSideBurst()
{
    EarlyFieldConfig c;
    c.enabled = true;
    c.sideDb = 0.0f;
    c.sideStartMs = 17.0f;
    c.sideMs = 30.0f;
    c.sideAttackMs = 2.0f;
    c.sideLowpassHz = 250.0f;
    Rig rig (kSr, 256, c);
    const EarlyField::Layout& b = rig.f.sideBurst();
    const int expected = static_cast<int> (std::lround (c.sideMs * 1e-3 * EarlyField::kSidePerSecond));
    checkf (b.count == expected, "the side burst has %d pulses, not %d", b.count, expected);
    const int first = static_cast<int> (std::lround (c.sideStartMs * 1e-3 * kSr));
    const int last  = static_cast<int> (std::lround ((c.sideStartMs + c.sideMs) * 1e-3 * kSr));
    float peak = 0.0f;
    for (int k = 0; k < b.count; ++k)
    {
        const int d = b.delay[static_cast<size_t> (k)];
        checkf (d >= first && d <= last, "side pulse %d at %d samples, outside %d..%d", k, d, first, last);
        peak = std::max (peak, std::abs (b.gain[static_cast<size_t> (k)]));
    }
    checkf (peak == 1.0f, "the side burst's peak pulse is %g, not 1", static_cast<double> (peak));
    // Its envelope: a fade-in from -24 dB over the attack (the first pulse, inside 0.5 ms, sits 14-21 dB
    // under the peak just past the attack), then 60 dB down across the length (the last, 55-56 dB).
    const auto sideDb = [&] (int k) { return 20.0 * std::log10 (std::abs (static_cast<double> (b.gain[static_cast<size_t> (k)]))); };
    checkf (sideDb (0) < -12.0 && sideDb (0) > -24.0, "the side burst's first pulse is %.1f dB, not faded in", sideDb (0));
    checkf (sideDb (b.count - 1) < -50.0 && sideDb (b.count - 1) > -62.0,
            "the side burst's last pulse is %.1f dB, not about 60 dB down", sideDb (b.count - 1));
    checkf (rig.f.postEnabled(), "a field playing only its side burst reports no post path");

    const Stereo in = noise (24000, 21u);
    const Run r = run (rig, in, 128);
    double mid = 0.0, side = 0.0, high = 0.0, prev = 0.0;
    for (size_t i = 0; i < r.post.l.size(); ++i)
    {
        mid  += 0.25 * (r.post.l[i] + r.post.r[i]) * (r.post.l[i] + r.post.r[i]);
        side += 0.25 * (r.post.l[i] - r.post.r[i]) * (r.post.l[i] - r.post.r[i]);
        const double diff = r.post.l[i] - prev;           // first difference: weights the highs
        high += diff * diff;
        prev = r.post.l[i];
    }
    checkf (side > 0.0, "the side burst is silent");
    checkf (mid == 0.0, "the side burst reaches the mid (%g against %g)", mid, side);
    // A 250 Hz corner: the first difference (a 6 dB/octave tilt) keeps well under 1 % of the energy.
    checkf (high < 0.01 * side, "the side burst carries highs: %g of its energy in the first difference", high / side);
}

// The onset burst: velvet pulses (both signs, peak 1) inside [start, start + length] at the burst's
// density, heard the same on both sides whichever side the input arrives on.
void testOnsetBurst()
{
    EarlyFieldConfig c;
    c.enabled = true;
    c.burstDb = -6.0f;
    c.burstStartMs = 29.0f;
    c.burstMs = 5.0f;
    c.burstLowpassHz = 20000.0f;
    Rig rig (kSr, 256, c);
    const EarlyField::Layout& b = rig.f.burst();
    const int expected = static_cast<int> (std::lround (c.burstMs * 1e-3 * EarlyField::kBurstPerSecond));
    checkf (b.count == expected, "the burst has %d pulses, not %d", b.count, expected);
    const int first = static_cast<int> (std::lround (c.burstStartMs * 1e-3 * kSr));
    const int last  = static_cast<int> (std::lround ((c.burstStartMs + c.burstMs) * 1e-3 * kSr));
    bool positive = false, negative = false;
    float peak = 0.0f;
    for (int k = 0; k < b.count; ++k)
    {
        const int d = b.delay[static_cast<size_t> (k)];
        checkf (d >= first && d <= last, "burst pulse %d at %d samples, outside %d..%d", k, d, first, last);
        positive |= b.gain[static_cast<size_t> (k)] > 0.0f;
        negative |= b.gain[static_cast<size_t> (k)] < 0.0f;
        peak = std::max (peak, std::abs (b.gain[static_cast<size_t> (k)]));
    }
    checkf (positive && negative, "the burst's pulses all have one sign: they would sum coherently in the lows");
    checkf (peak == 1.0f, "the burst's peak pulse is %g, not 1", static_cast<double> (peak));
    // Its envelope: the first pulse is the peak and the level falls 20 dB across the length (the last
    // pulse, within one pulse's spacing of the end, 17-20 dB down).
    const double lastDb = 20.0 * std::log10 (std::abs (static_cast<double> (b.gain[static_cast<size_t> (b.count - 1)])));
    checkf (std::abs (b.gain[0]) == 1.0f, "the burst's first pulse is %g, not its peak", static_cast<double> (b.gain[0]));
    checkf (lastDb < -17.0 && lastDb > -20.5, "the burst's last pulse is %.1f dB, not about 20 dB down", lastDb);

    checkf (rig.f.postEnabled(), "a field playing only its burst reports no post path");
    Stereo in = silence (6000);
    in.l[100] = 1.0f;                                   // left only
    const Run r = run (rig, in, 64);
    checkf (same (r.post.l, r.post.r), "the burst plays differently on the two sides");
    float energy = 0.0f;
    for (size_t i = 0; i < r.post.l.size(); ++i)
    {
        energy += r.post.l[i] * r.post.l[i];
        if (static_cast<int> (i) < 100 + first)
            checkf (r.post.l[i] == 0.0f, "the burst sounds %d samples after the input, before its start", static_cast<int> (i) - 100);
    }
    checkf (energy > 0.0f, "the burst is silent");
}

void testParser()
{
    using duskverb::parseEarlyFieldConfig;
    EarlyFieldConfig c;
    char error[256] = {};
    checkf (parseEarlyFieldConfig ("enabled=1,start_ms=4,window_ms=80,taps=24,slope_db=-6,attack_ms=10,inject=0.4,"
                                   "direct_db=-14,refl_db=-18,refl_count=2,refl_ms=11/17.25/0/0,lowpass_hz=7000",
                                   c, error, sizeof (error)),
            "a full config is refused: %s", error);
    checkf (c.enabled && c.startMs == 4.0f && c.windowMs == 80.0f && c.taps == 24 && c.slopeDb == -6.0f
                && c.attackMs == 10.0f && c.inject == 0.4f && c.directDb == -14.0f && c.reflDb == -18.0f
                && c.reflCount == 2 && c.reflMs[0] == 11.0f && c.reflMs[1] == 17.25f && c.reflMs[2] == 0.0f
                && c.reflMs[3] == 0.0f && c.lowpassHz == 7000.0f,
            "a full config parses to other values");

    EarlyFieldConfig d;
    checkf (parseEarlyFieldConfig (" enabled = 1 , inject=0.25 ", d) && d.enabled && d.inject == 0.25f
                && d.taps == EarlyFieldConfig {}.taps && d.windowMs == EarlyFieldConfig {}.windowMs,
            "spaces, or keys left at their defaults, are refused");
    checkf (parseEarlyFieldConfig ("inject=0.25", d) && ! d.enabled, "a config without enabled=1 is enabled");

    const char* refused[] = {
        "", "  ", "enabled=1,", "enabled", "=1", "enable=1", "enabled=1,enabled=1", "enabled=2", "taps=0",
        "taps=65", "taps=2.5", "start_ms=3.5x", "start_ms=", "start_ms=abc", "start_ms=1e", "start_ms=nan",
        "start_ms=inf", "start_ms=1e999", "start_ms=-1", "start_ms=350,window_ms=60", "inject=1.5",
        "refl_count=11", "refl_ms=", "refl_ms=1/2/3/4/5/6/7/8/9/10/11", "refl_ms=1//3/4", "refl_ms=1/2/",
        "refl_count=1,refl_ms=401/0/0/0",
        "lowpass_hz=10", "direct_db=-121", "slope_db=61", "enabled=1;taps=8",
    };
    for (const char* text : refused)
    {
        EarlyFieldConfig untouched = longConfig();
        error[0] = '\0';
        checkf (! parseEarlyFieldConfig (text, untouched, error, sizeof (error)), "'%s' is accepted", text);
        checkf (sameConfig (untouched, longConfig()), "refusing '%s' changed the config", text);
        checkf (error[0] != '\0', "refusing '%s' gives no reason", text);
    }
    // A reflection past refl_count is not heard, so its time is not checked.
    checkf (parseEarlyFieldConfig ("refl_count=1,refl_ms=20/999/0/0", d), "an unused reflection time is checked");
    // One to ten times, the rest left at 0; up to ten reflections.
    EarlyFieldConfig e;
    checkf (parseEarlyFieldConfig ("refl_count=3,refl_ms=10/20/30", e) && e.reflMs[2] == 30.0f && e.reflMs[3] == 0.0f,
            "three reflection times are refused");
    checkf (parseEarlyFieldConfig ("refl_count=10,refl_ms=5/10/15/20/25/30/35/40/45/50", e) && e.reflCount == 10
                && e.reflMs[9] == 50.0f, "ten reflections are refused");

    // The canonical text parses back to the very same floats.
    EarlyFieldConfig odd = longConfig();
    odd.startMs = 0.1f;
    odd.windowMs = 123.456789f;
    odd.slopeDb = -7.77777f;
    odd.inject = 1.0f / 3.0f;
    odd.lowpassHz = 6789.123f;
    for (const EarlyFieldConfig& original : { longConfig(), odd, EarlyFieldConfig {} })
    {
        char text[512];
        duskverb::formatEarlyFieldConfig (original, text, sizeof (text));
        EarlyFieldConfig back;
        checkf (parseEarlyFieldConfig (text, back, error, sizeof (error)) && sameConfig (back, original),
                "'%s' does not parse back to itself (%s)", text, error);
    }
}

// Every row names a factory preset on a shared engine (an own engine never hears the field),
// and a preset without a row has none.
void testTable()
{
    const auto& presets = duskverb::dv2FactoryPresets();
    for (const auto& row : duskverb::kEarlyFieldByName)
    {
        const FactoryPreset* found = nullptr;
        for (const auto& preset : presets)
            if (row.first == preset.name) found = &preset;
        checkf (found != nullptr, "early-field row '%.*s' names no factory preset",
                static_cast<int> (row.first.size()), row.first.data());
        if (found != nullptr)
            checkf (! duskverb::isOwnAlgorithm (found->algorithm), "early-field row '%s' is on an own engine, which never plays it", found->name);
    }
    for (const auto& preset : presets)
    {
        bool listed = false;
        for (const auto& row : duskverb::kEarlyFieldByName)
            listed |= row.first == preset.name;
        const EarlyFieldConfig& c = duskverb::earlyFieldFor (preset.name);
        checkf (listed || ! c.enabled, "'%s' has no row but plays an early field", preset.name);
        checkf (listed || &c == &duskverb::kNoEarlyField, "'%s' has no row but is not given kNoEarlyField", preset.name);
    }
    checkf (! duskverb::earlyFieldFor ("").enabled && ! duskverb::earlyFieldFor ("No Such Preset").enabled,
            "an unknown name plays an early field");
    checkf (! duskverb::kNoEarlyField.enabled, "kNoEarlyField is enabled");
}

void printLayout (const char* name, const EarlyField::Layout& l)
{
    std::printf ("\"%s\": [", name);
    for (int k = 0; k < l.count; ++k)
        std::printf ("%s[%d, %.9g]", k ? ", " : "", l.delay[static_cast<size_t> (k)], static_cast<double> (l.gain[static_cast<size_t> (k)]));
    std::printf ("]");
}

int dumpLayout (const char* text, double sampleRate)
{
    EarlyFieldConfig c;
    char error[256] = {};
    if (! duskverb::parseEarlyFieldConfig (text, c, error, sizeof (error)))
    {
        std::fprintf (stderr, "DuskVerbEarlyFieldTest: %s\n", error);
        return 2;
    }
    const Rig rig (sampleRate, 256, c);
    const EarlyField& f = rig.f;
    // The config as the field holds it (each value a float), exactly, for the model to take.
    std::printf ("{\"sample_rate\": %.17g, \"config\": {\"start_ms\": %.17g, \"window_ms\": %.17g, \"taps\": %d, "
                 "\"slope_db\": %.17g, \"attack_ms\": %.17g, \"refl_count\": %d, \"refl_ms\": [",
                 sampleRate, static_cast<double> (c.startMs), static_cast<double> (c.windowMs), c.taps,
                 static_cast<double> (c.slopeDb), static_cast<double> (c.attackMs), c.reflCount);
    for (size_t r = 0; r < c.reflMs.size(); ++r)
        std::printf (r == 0 ? "%.17g" : ", %.17g", static_cast<double> (c.reflMs[r]));
    std::printf ("]}, ");
    printLayout ("injector", f.injector());
    std::printf (", ");
    printLayout ("direct_left", f.direct (0));
    std::printf (", ");
    printLayout ("direct_right", f.direct (1));
    std::printf (", \"reflections\": [%d, %d, %d, %d]}\n", f.reflectionDelay (0), f.reflectionDelay (1),
                 f.reflectionDelay (2), f.reflectionDelay (3));
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc >= 3 && std::strcmp (argv[1], "--dump-layout") == 0)
    {
        double sampleRate = kSr;
        if (argc >= 5 && std::strcmp (argv[3], "--sample-rate") == 0)
            sampleRate = std::atof (argv[4]);
        return dumpLayout (argv[2], sampleRate);
    }
    testMatchesTheModel();
    testNoAllocationOnTheAudioThread();
    testBlockSizeInvariant();
    testDeterministic();
    testImpulseResponse();
    testInjection();
    testOffPathsAreSilent();
    testASilentInputDrains();
    testSilenceFadesInAndOut();
    testTakingOverRendersTheSameSamples();
    testSampleRates();
    testOnsetBurst();
    testSideBurst();
    testParser();
    testTable();
    if (failures != 0)
    {
        std::printf ("%d EarlyField check(s) failed\n", failures);
        return 1;
    }
    std::printf ("EarlyField: all checks passed\n");
    return 0;
}
