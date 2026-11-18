// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// OctaveTrim gates: the measured magnitude at a band's centre is the gain asked of that band
// alone, and the formula's (DuskVerbOctaveTrim.hpp) for every setting, which the whole response
// follows; the top band at 44.1 kHz, and the bands left out at low rates; a flat trim is the
// identity, bit for bit; no allocation; block-size invariant and deterministic; a swap is a linear
// crossfade from the outgoing cascade to the incoming one started from rest, and a swap to the
// trim playing is none; plus the voicing table and the tuning hook's parser
// (DuskVerbDv2Voicing.hpp).
//
//   DuskVerbOctaveTrimTest --dump-response GAINS [--sample-rate SR]
//
// prints the stage's measured magnitude for GAINS (the hook's text) on a twelfth-octave grid,
// with the formula's beside it, as JSON, for the check against the tools repo's model.

#include "DuskVerbDv2Voicing.hpp"
#include "DuskVerbOctaveTrim.hpp"
#include "DuskVerbPresets.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

using duskverb::kOctaveTrimBands;
using duskverb::OctaveTrim;
using duskverb::OctaveTrimGains;

constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;
constexpr int kFade = 2400;                  // DuskVerbDSP's preset fade at 48 kHz
const OctaveTrimGains kFlat {};

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

bool same (const Stereo& a, const Stereo& b)
{
    return a.l.size() == b.l.size() && std::memcmp (a.l.data(), b.l.data(), a.l.size() * sizeof (float)) == 0
        && std::memcmp (a.r.data(), b.r.data(), a.r.size() * sizeof (float)) == 0;
}

OctaveTrimGains allAt (float db)
{
    OctaveTrimGains g;
    g.fill (db);
    return g;
}

OctaveTrimGains oneBand (int band, float db)
{
    OctaveTrimGains g {};
    g[static_cast<size_t> (band)] = db;
    return g;
}

// The settings the tests sweep besides single bands: every band up, every band down, alternating,
// a tilt, and two irregular ones.
struct Setting { const char* name; OctaveTrimGains gains; };
const Setting kSettings[] = {
    { "all +3",      allAt (3.0f) },
    { "all -3",      allAt (-3.0f) },
    { "alternating", { { 3.0f, -3.0f, 3.0f, -3.0f, 3.0f, -3.0f, 3.0f, -3.0f, 3.0f } } },
    { "tilt",        { { -3.0f, -2.25f, -1.5f, -0.75f, 0.0f, 0.75f, 1.5f, 2.25f, 3.0f } } },
    { "irregular",   { { 1.25f, -0.5f, 2.75f, 0.0f, -2.0f, 0.3f, -1.1f, 2.9f, -2.6f } } },
    { "sparse",      { { 0.0f, 2.0f, 0.0f, 0.0f, -2.5f, 0.0f, 0.0f, 1.0f, 0.0f } } },
};

// The bands and Q the brief gives the trim, kept apart from the stage's own constants so a wrong
// one there shows up against them.
constexpr double kBandHz[kOctaveTrimBands] = { 63.0, 126.0, 252.0, 504.0, 1008.0, 2016.0, 4032.0, 8064.0, 16128.0 };
constexpr double kQ = 1.41;

// The formula (DuskVerbOctaveTrim.hpp) in double: the RBJ peaking sections' magnitudes
// multiplied, a 0 dB band and a band at or above the designers' ceiling left out. The tools
// repo's cascade_db() is the same arithmetic.
double cascadeDb (double f, const OctaveTrimGains& gains, double fs)
{
    const std::complex<double> z1 = std::polar (1.0, -2.0 * kPi * f / fs), z2 = z1 * z1;
    double db = 0.0;
    for (int b = 0; b < kOctaveTrimBands; ++b)
    {
        const double g = gains[static_cast<size_t> (b)], f0 = kBandHz[b];
        if (g == 0.0 || f0 >= 0.4998 * fs)
            continue;
        const double A = std::pow (10.0, g / 40.0), w0 = 2.0 * kPi * f0 / fs;
        const double alpha = std::sin (w0) / (2.0 * kQ), cw = std::cos (w0);
        const std::complex<double> h = (1.0 + alpha * A - 2.0 * cw * z1 + (1.0 - alpha * A) * z2)
                                     / (1.0 + alpha / A - 2.0 * cw * z1 + (1.0 - alpha / A) * z2);
        db += 20.0 * std::log10 (std::abs (h));
    }
    return db;
}

// The stage's impulse response, `gains` installed at once, rendered in 512-sample blocks.
std::vector<float> impulseResponse (const OctaveTrimGains& gains, double fs, int n)
{
    OctaveTrim t;
    t.prepare (fs);
    t.swapTo (gains, 0);
    Stereo s = silence (n);
    s.l[0] = s.r[0] = 1.0f;
    for (int at = 0; at < n; at += 512)
        t.process (s.l.data() + at, s.r.data() + at, std::min (512, n - at));
    checkf (s.l == s.r, "the two sides differ for one input");
    return s.l;
}

// Its magnitude (dB) at f: the response's DFT evaluated there, up to where it has rung out (past
// 1e-20 the rest of a decaying response cannot move a magnitude near 1).
double measuredDb (const std::vector<float>& h, double f, double fs)
{
    size_t n = h.size();
    while (n > 1 && std::abs (h[n - 1]) < 1e-20f) --n;
    const std::complex<double> step = std::polar (1.0, -2.0 * kPi * f / fs);
    std::complex<double> z = 1.0, sum = 0.0;
    for (size_t i = 0; i < n; ++i, z *= step)
        sum += static_cast<double> (h[i]) * z;
    return 20.0 * std::log10 (std::abs (sum));
}

// Long enough at every rate tested for the 63 Hz section to ring out far below float precision.
int responseLength (double fs) { return fs > 100000.0 ? 1 << 18 : 1 << 16; }

// Twelfth-octave points from 20 Hz to just under Nyquist.
std::vector<double> grid (double fs)
{
    std::vector<double> f;
    for (double x = 20.0; x < 0.499 * fs; x *= std::pow (2.0, 1.0 / 12.0))
        f.push_back (x);
    return f;
}

// How far the measured response may stray from the formula: the sections run in double, so only
// the float samples between them (a float section strayed by 0.03 dB at 48 kHz, 0.4 dB at 192).
constexpr double kCurveTolDb = 1e-4;

double curveError (const std::vector<float>& h, const OctaveTrimGains& g, double fs)
{
    double worst = 0.0;
    for (const double f : grid (fs))
        worst = std::max (worst, std::abs (measuredDb (h, f, fs) - cascadeDb (f, g, fs)));
    return worst;
}

// One band at a time: its centre reads the gain asked of it, within the 0.3 dB the brief allows
// (and in fact far closer), and the whole response follows the formula.
void testSingleBandsHitTheirGain()
{
    for (const double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        double worstCentre = 0.0, worstCurve = 0.0;
        for (int b = 0; b < kOctaveTrimBands; ++b)
            for (const float db : { 3.0f, -3.0f, 1.5f, -0.75f })
            {
                const OctaveTrimGains g = oneBand (b, db);
                const auto h = impulseResponse (g, fs, responseLength (fs));
                const double f0 = kBandHz[b];
                const double centre = measuredDb (h, f0, fs);
                worstCentre = std::max (worstCentre, std::abs (centre - db));
                checkf (std::abs (centre - db) <= 0.3, "%g kHz: band %g Hz at %+g dB reads %+.4f dB at its centre",
                        fs / 1000.0, f0, static_cast<double> (db), centre);
                worstCurve = std::max (worstCurve, curveError (h, g, fs));
            }
        std::printf ("OctaveTrim: %g kHz, one band at a time: centres within %.2g dB of the gain asked; the response within"
                     " %.2g dB of the formula\n", fs / 1000.0, worstCentre, worstCurve);
        checkf (worstCurve <= kCurveTolDb, "%g kHz: one band's response is %.2g dB from the formula", fs / 1000.0, worstCurve);
    }
}

// Every band set: the sections overlap, so a centre reads its own gain plus its neighbours'
// skirts. The measured response is the formula's, centres and all, within 0.3 dB (in fact far
// closer); what each centre reads against the gain asked of that band is printed, not checked.
void testAllBandsFollowTheFormula()
{
    for (const double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const Setting& s : kSettings)
        {
            const auto h = impulseResponse (s.gains, fs, responseLength (fs));
            double worstCentre = 0.0, offAsked = 0.0;
            for (int b = 0; b < kOctaveTrimBands; ++b)
            {
                const double f0 = kBandHz[b];
                const double got = measuredDb (h, f0, fs), want = cascadeDb (f0, s.gains, fs);
                worstCentre = std::max (worstCentre, std::abs (got - want));
                offAsked = std::max (offAsked, std::abs (got - s.gains[static_cast<size_t> (b)]));
                checkf (std::abs (got - want) <= 0.3, "%g kHz, %s: band %g Hz reads %+.4f dB, the formula %+.4f dB",
                        fs / 1000.0, s.name, f0, got, want);
            }
            const double worstCurve = curveError (h, s.gains, fs);
            std::printf ("OctaveTrim: %g kHz, %-11s: centres within %.2g dB of the formula, the response within %.2g dB;"
                         " a centre is up to %.2f dB from its own band's gain\n", fs / 1000.0, s.name, worstCentre, worstCurve, offAsked);
            checkf (worstCurve <= kCurveTolDb, "%g kHz, %s: the response is %.2g dB from the formula", fs / 1000.0, s.name, worstCurve);
            if (fs == kSr && &s == &kSettings[0])
            {
                std::printf ("OctaveTrim: all nine at +3 dB, each centre reads");
                for (int b = 0; b < kOctaveTrimBands; ++b)
                    std::printf (" %+.2f", measuredDb (h, kBandHz[b], fs));
                std::printf (" dB\n");
            }
        }
}

// The top band near Nyquist: at 44.1 kHz it is designed where asked (0.366 fs, inside the
// designers' 0.4998 fs ceiling), reads its gain at its centre and nothing at Nyquist; at 32 kHz
// and below its centre is past the ceiling and it is left out, and at 16 kHz the 8 kHz band too.
// At any rate, a trim with every band set stays finite and rings out.
void testTheTopBandAndLowRates()
{
    const OctaveTrimGains top = oneBand (8, 3.0f);
    for (const double fs : { 44100.0, 48000.0 })
    {
        const auto h = impulseResponse (top, fs, responseLength (fs));
        std::printf ("OctaveTrim: %g kHz, the 16128 Hz band at +3 dB reads", fs / 1000.0);
        for (const double f : { 8000.0, 12000.0, 16128.0, 18000.0, 20000.0, 0.5 * fs })
            std::printf (" %+.2f dB at %g Hz,", measuredDb (h, f, fs), f);
        std::printf ("\n");
        checkf (std::abs (measuredDb (h, 0.5 * fs, fs)) <= 1e-3, "%g kHz: the top band moves Nyquist by %.4f dB",
                fs / 1000.0, measuredDb (h, 0.5 * fs, fs));
    }
    checkf (OctaveTrim::inBand (8, 44100.0) && OctaveTrim::inBand (8, 32270.0) && ! OctaveTrim::inBand (8, 32268.0)
                && ! OctaveTrim::inBand (8, 32000.0) && OctaveTrim::inBand (7, 22050.0) && ! OctaveTrim::inBand (7, 16000.0),
            "the bands left out are not those past the designers' ceiling");
    for (const double fs : { 32000.0, 22050.0 })
    {
        OctaveTrim t;
        t.prepare (fs);
        t.swapTo (top, 0);
        Stereo s = noise (4096, 5u);
        const Stereo before = s;
        t.process (s.l.data(), s.r.data(), 4096);
        checkf (! t.active() && same (s, before), "%g kHz: the 16 kHz band, past Nyquist, still runs", fs / 1000.0);
    }
    for (const double fs : { 1234.57, 8000.0, 16000.0, 22050.0, 32000.0, 384000.0, 768000.0 })
        for (const Setting& s : kSettings)
        {
            const int n = 1 << 18;
            const auto h = impulseResponse (s.gains, fs, n);
            bool finite = true;
            double tail = 0.0;
            for (int i = 0; i < n; ++i)
            {
                finite = finite && std::isfinite (h[static_cast<size_t> (i)]);
                if (i >= n - 4096) tail = std::max (tail, static_cast<double> (std::abs (h[static_cast<size_t> (i)])));
            }
            checkf (finite && tail < 1e-6, "%g Hz, %s: the impulse response is not finite or does not ring out (%g at the end)",
                    fs, s.name, tail);
        }
}

// A flat trim, of +0 or -0 dB bands, never runs: the wet passes bit for bit, -0.0 and subnormals
// included (a 0 dB section would turn -0.0 into +0.0). So after a fade to flat.
void testFlatIsTheIdentity()
{
    Stereo in = noise (24000, 3u);
    for (size_t i = 0; i < in.l.size(); i += 97)
        in.l[i] = in.r[i] = -0.0f;
    for (size_t i = 50; i < in.l.size(); i += 101)
        in.l[i] = in.r[i] = 1.0e-40f;
    for (const OctaveTrimGains& flat : { kFlat, allAt (-0.0f) })
    {
        checkf (OctaveTrim::flat (flat), "a trim of 0 dB bands is not flat");
        OctaveTrim t;
        t.prepare (kSr);
        t.swapTo (flat, kFade);
        Stereo s = in;
        for (int at = 0; at < 24000; at += 333)
            t.process (s.l.data() + at, s.r.data() + at, std::min (333, 24000 - at));
        checkf (! t.active() && same (s, in), "a flat trim changes the wet");
    }
    OctaveTrim t;
    t.prepare (kSr);
    t.swapTo (allAt (2.0f), 0);
    Stereo s = in;
    t.process (s.l.data(), s.r.data(), 8000);
    t.swapTo (kFlat, kFade);
    t.process (s.l.data() + 8000, s.r.data() + 8000, kFade);
    checkf (! t.active(), "the trim still runs once its fade to flat is over");
    t.process (s.l.data() + 8000 + kFade, s.r.data() + 8000 + kFade, 24000 - 8000 - kFade);
    const size_t from = 8000 + kFade;
    checkf (std::memcmp (s.l.data() + from, in.l.data() + from, (in.l.size() - from) * sizeof (float)) == 0
                && std::memcmp (s.r.data() + from, in.r.data() + from, (in.r.size() - from) * sizeof (float)) == 0,
            "after a fade to flat, the wet is not passed bit for bit");
}

void testNoAllocation()
{
    OctaveTrim t;
    Stereo s = noise (512, 7u);
    const OctaveTrimGains trims[] = { kSettings[0].gains, kSettings[4].gains, kFlat, kSettings[2].gains };
    gAllocations.store (0);
    gCountAllocations.store (true);
    for (int i = 0; i < 80; ++i)
    {
        if (i % 20 == 0)
            t.prepare (i % 40 ? 44100.0 : kSr);
        if (i % 6 == 0)
            t.swapTo (trims[(i / 6) % 4], i % 12 ? kFade : 0);
        t.process (s.l.data(), s.r.data(), 512);
        if (i % 25 == 24)
            t.clear();
    }
    gCountAllocations.store (false);
    checkf (gAllocations.load() == 0, "%d heap allocations in prepare/swapTo/process/clear", gAllocations.load());
}

// One run of the stage over `in`, `block` samples at a time, each event applied at its sample
// (the blocks split there): a swap, or a clear.
struct Event { int at; OctaveTrimGains gains; int fade; bool clear; };

Stereo run (const Stereo& in, int block, const std::vector<Event>& events, double fs = kSr)
{
    OctaveTrim t;
    t.prepare (fs);
    Stereo s = in;
    const int n = static_cast<int> (in.l.size());
    size_t e = 0;
    for (int at = 0; at < n;)
    {
        for (; e < events.size() && events[e].at == at; ++e)
        {
            if (events[e].clear) t.clear();
            else t.swapTo (events[e].gains, events[e].fade);
        }
        int m = std::min (block, n - at);
        if (e < events.size())
            m = std::min (m, events[e].at - at);
        t.process (s.l.data() + at, s.r.data() + at, m);
        at += m;
    }
    return s;
}

const std::vector<Event>& schedule()
{
    static const std::vector<Event> events = {
        { 0, kSettings[0].gains, kFade, false },          // flat -> all +3, from the start
        { 20000, kSettings[4].gains, kFade, false },      // -> irregular
        { 21000, kSettings[2].gains, kFade, false },      // -> alternating, mid-fade: the fade in flight ends
        { 40000, kFlat, kFade, false },                   // -> flat
        { 50000, kSettings[3].gains, 0, false },          // -> tilt, at once
        { 60000, {}, 0, true },                           // a host reset
        { 61000, kSettings[1].gains, 1, false },          // -> all -3, a one-sample fade
    };
    return events;
}

// The longest blocks DuskVerbDSP is given and the oddest: the same output, bit for bit.
void testBlockSizeInvariant()
{
    const Stereo in = noise (72000, 11u);
    const Stereo expected = run (in, 1, schedule());
    checkf (! same (expected, in), "the schedule changes nothing, so this test cannot see the stage");
    for (const int block : { 7, 64, 256, 1000, 2400, 4096 })
        checkf (same (run (in, block, schedule()), expected), "block size %d changes the output", block);
}

// A stage that has played other trims, then been cleared, renders as a fresh one.
void testDeterministic()
{
    const Stereo in = noise (30000, 13u);
    OctaveTrim used;
    used.prepare (kSr);
    used.swapTo (kSettings[2].gains, 0);
    Stereo scrap = noise (30000, 17u);
    used.process (scrap.l.data(), scrap.r.data(), 20000);
    used.swapTo (kSettings[0].gains, kFade);
    used.process (scrap.l.data() + 20000, scrap.r.data() + 20000, 1000);      // mid-fade
    used.clear();
    OctaveTrim fresh;
    fresh.prepare (kSr);
    fresh.swapTo (kSettings[0].gains, 0);
    Stereo a = in, b = in;
    fresh.process (a.l.data(), a.r.data(), 30000);
    used.process (b.l.data(), b.r.data(), 30000);
    checkf (same (a, b), "a cleared stage renders what it played before");
}

// A swap at `swapAt` from `from` to `to`: before it the outgoing trim alone; over the fade its
// output faded linearly into that of the incoming one started from rest at the swap, with
// DuskVerbDSP's t; after it the incoming one alone. Built from fixed stages, bit for bit.
void testASwapIsALinearCrossfade()
{
    const Stereo in = noise (48000, 19u);
    const int swapAt = 12000;
    struct Pair { const char* name; OctaveTrimGains from, to; };
    const Pair pairs[] = {
        { "all +3 -> all -3", kSettings[0].gains, kSettings[1].gains },
        { "irregular -> tilt", kSettings[4].gains, kSettings[3].gains },
        { "flat -> alternating", kFlat, kSettings[2].gains },
        { "sparse -> flat", kSettings[5].gains, kFlat },
    };
    // Each swap comes after another, so the incoming trim lands in the cascade that last played
    // the earlier trim: starting from rest, it carries none of that state.
    const OctaveTrimGains earlier = allAt (1.0f);
    for (const Pair& p : pairs)
    {
        const std::vector<Event> before { { 0, earlier, 0, false }, { 4000, p.from, kFade, false } };
        std::vector<Event> events = before;
        events.push_back ({ swapAt, p.to, kFade, false });
        const Stereo got = run (in, 256, events);
        const Stereo outgoing = run (in, 256, before);
        Stereo tail = silence (48000 - swapAt);
        std::copy (in.l.begin() + swapAt, in.l.end(), tail.l.begin());
        std::copy (in.r.begin() + swapAt, in.r.end(), tail.r.begin());
        const Stereo incoming = run (tail, 256, { { 0, p.to, 0, false } });
        Stereo want = outgoing;
        const float invSpan = 1.0f / static_cast<float> (kFade - 1);
        for (int i = swapAt; i < 48000; ++i)
        {
            const size_t k = static_cast<size_t> (i), j = static_cast<size_t> (i - swapAt);
            const float t = std::min (1.0f, static_cast<float> (i - swapAt) * invSpan);
            want.l[k] = i - swapAt < kFade ? outgoing.l[k] + t * (incoming.l[j] - outgoing.l[k]) : incoming.l[j];
            want.r[k] = i - swapAt < kFade ? outgoing.r[k] + t * (incoming.r[j] - outgoing.r[k]) : incoming.r[j];
        }
        checkf (same (got, want), "%s: the swap is not the linear crossfade into the incoming trim started from rest", p.name);
    }
    // To the trim already playing: no swap at all, bit for bit.
    const Stereo still = run (in, 256, { { 0, kSettings[4].gains, 0, false } });
    checkf (same (run (in, 256, { { 0, kSettings[4].gains, 0, false }, { swapAt, kSettings[4].gains, kFade, false } }), still),
            "a swap to the trim playing changes the output");
}

void testParser()
{
    using duskverb::parseOctaveTrim;
    OctaveTrimGains g {};
    char error[256] = {};
    checkf (parseOctaveTrim ("0.5,0,-1,0,0,1.25,0,0,-2", g, error, sizeof (error)), "a full trim is refused: %s", error);
    checkf (g == OctaveTrimGains { { 0.5f, 0.0f, -1.0f, 0.0f, 0.0f, 1.25f, 0.0f, 0.0f, -2.0f } }, "a full trim parses to other values");
    checkf (parseOctaveTrim (" 3 , -3,+1.5,\t0,0,0,0,0, 0.125 ", g) && g[0] == 3.0f && g[1] == -3.0f && g[2] == 1.5f && g[8] == 0.125f,
            "spaces around values are refused, or read wrong");
    checkf (parseOctaveTrim ("3.00000001,0,0,0,0,0,0,0,0", g) && g[0] == 3.0f, "a value that rounds to 3 dB as a float is refused");
    checkf (parseOctaveTrim ("-0,0,0,0,0,0,0,0,0", g) && OctaveTrim::flat (g) && std::signbit (g[0]), "-0 is not read as a flat -0 dB band");

    const char* refused[] = {
        "", "  ", "1", "0,0,0,0,0,0,0,0", "0,0,0,0,0,0,0,0,0,0", "0,0,0,0,0,0,0,0,0,", ",0,0,0,0,0,0,0,0,0",
        "0,,0,0,0,0,0,0,0", "a,0,0,0,0,0,0,0,0", "3.1,0,0,0,0,0,0,0,0", "0,0,0,0,0,0,0,0,-3.0001",
        "3.0000003,0,0,0,0,0,0,0,0", "nan,0,0,0,0,0,0,0,0", "inf,0,0,0,0,0,0,0,0", "1e999,0,0,0,0,0,0,0,0",
        "0x1,0,0,0,0,0,0,0,0", "1 2,0,0,0,0,0,0,0,0", "1e,0,0,0,0,0,0,0,0", "0;0;0;0;0;0;0;0;0", "0,0,0,0,0,0,0,0,0dB",
    };
    const OctaveTrimGains untouched = kSettings[4].gains;
    for (const char* text : refused)
    {
        OctaveTrimGains kept = untouched;
        error[0] = '\0';
        checkf (! parseOctaveTrim (text, kept, error, sizeof (error)), "'%s' is accepted", text);
        checkf (kept == untouched, "refusing '%s' changed the trim", text);
        checkf (error[0] != '\0', "refusing '%s' gives no reason", text);
    }
    checkf (! parseOctaveTrim (nullptr, g), "no text is accepted");

    // The canonical text parses back to the very same floats, signs of zero included.
    for (const OctaveTrimGains& original : { kSettings[4].gains, kSettings[3].gains, allAt (-0.0f),
                                             OctaveTrimGains { { 1.0f / 3.0f, -2.7182817f, 0.1f, -0.0f, 3.0f, -3.0f, 1e-7f, 0.0f, 2.9999998f } } })
    {
        char text[256];
        duskverb::formatOctaveTrim (original, text, sizeof (text));
        OctaveTrimGains back {};
        checkf (parseOctaveTrim (text, back, error, sizeof (error)) && std::memcmp (back.data(), original.data(), sizeof (back)) == 0,
                "'%s' does not parse back to itself (%s)", text, error);
    }
}

// Every row names a factory preset once, and a preset without a row is flat.
void testTable()
{
    const auto& presets = duskverb::dv2FactoryPresets();
    for (const auto& row : duskverb::kOctaveTrimByName)
    {
        bool found = false;
        for (const auto& preset : presets)
            found |= row.first == preset.name;
        checkf (found, "octave-trim row '%.*s' names no factory preset", static_cast<int> (row.first.size()), row.first.data());
    }
    int flat = 0;
    for (const auto& preset : presets)
    {
        bool listed = false;
        for (const auto& row : duskverb::kOctaveTrimByName)
            listed |= row.first == preset.name;
        const OctaveTrimGains& g = duskverb::octaveTrimFor (preset.name);
        checkf (listed || &g == &duskverb::kFlatOctaveTrim, "'%s' has no row but is not given kFlatOctaveTrim", preset.name);
        flat += OctaveTrim::flat (g) ? 1 : 0;
    }
    checkf (&duskverb::octaveTrimFor ("") == &duskverb::kFlatOctaveTrim
                && &duskverb::octaveTrimFor ("No Such Preset") == &duskverb::kFlatOctaveTrim,
            "an unknown name plays a trim");
    checkf (OctaveTrim::flat (duskverb::kFlatOctaveTrim), "kFlatOctaveTrim is not flat");
    std::printf ("OctaveTrim: %zu table rows; %d of %zu factory presets flat\n", duskverb::kOctaveTrimByName.size(), flat, presets.size());
}

int dumpResponse (const char* text, double sampleRate)
{
    OctaveTrimGains g {};
    char error[256] = {};
    if (! duskverb::parseOctaveTrim (text, g, error, sizeof (error)))
    {
        std::fprintf (stderr, "DuskVerbOctaveTrimTest: %s\n", error);
        return 2;
    }
    const auto h = impulseResponse (g, sampleRate, responseLength (sampleRate));
    std::printf ("{\"sample_rate\": %.17g, \"gains_db\": [", sampleRate);
    for (int b = 0; b < kOctaveTrimBands; ++b)
        std::printf ("%s%.9g", b ? ", " : "", static_cast<double> (g[static_cast<size_t> (b)]));
    std::printf ("], \"response\": [");
    std::vector<double> f = grid (sampleRate);
    for (const double hz : kBandHz)
        if (hz < 0.499 * sampleRate) f.push_back (hz);
    std::sort (f.begin(), f.end());
    for (size_t i = 0; i < f.size(); ++i)
        std::printf ("%s[%.9g, %.9g, %.9g]", i ? ", " : "", f[i], measuredDb (h, f[i], sampleRate), cascadeDb (f[i], g, sampleRate));
    std::printf ("]}\n");
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc >= 3 && std::strcmp (argv[1], "--dump-response") == 0)
    {
        double sampleRate = kSr;
        if (argc >= 5 && std::strcmp (argv[3], "--sample-rate") == 0)
            sampleRate = std::atof (argv[4]);
        return dumpResponse (argv[2], sampleRate);
    }
    testSingleBandsHitTheirGain();
    testAllBandsFollowTheFormula();
    testTheTopBandAndLowRates();
    testFlatIsTheIdentity();
    testNoAllocation();
    testBlockSizeInvariant();
    testDeterministic();
    testASwapIsALinearCrossfade();
    testParser();
    testTable();
    if (failures != 0)
    {
        std::printf ("%d OctaveTrim check(s) failed\n", failures);
        return 1;
    }
    std::printf ("OctaveTrim: all checks passed\n");
    return 0;
}
