// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// BandLimit gates (DuskVerbBandLimit.hpp). The stage alone: its measured magnitude is the closed
// form's (the Butterworth's |H|^2 = 1 / (1 + (tan (pi f / fs) / tan (pi fc / fs))^(2N)) times the
// RBJ peaking section's) at 44.1, 48 and 96 kHz; a section at or above the design ceiling does not
// run; no band limit is the identity, bit for bit; block-size invariant and deterministic; a swap
// is a linear crossfade from the outgoing cascade to the incoming one started from rest, and a
// swap to the setting playing is none; no allocation. The voicing table and the tuning hook's
// parser (DuskVerbDv2Voicing.hpp). Inside DuskVerbDSP: a preset without a row renders exactly as
// one given no band limit does; DUSKVERB2_BANDLIMIT takes effect only in a hook build, and only
// whole; a render with a band limit is, to float rounding, the stage run over the same render
// without one, swapped where DuskVerbDSP swaps.

#include "DuskVerbDSP.hpp"
#include "DuskVerbDv2Voicing.hpp"
#include "DuskVerbParamTable.hpp"
#include "DuskVerbPresets.hpp"
#include "dsp/SparseEarlyField.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

#if ! defined (DUSKVERB2_TUNING_HOOKS)
  #define DUSKVERB2_TUNING_HOOKS 0
#endif

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

using duskverb::BandLimit;
using duskverb::BandLimitSetting;
using duskverb::DuskVerbDSP;

constexpr double kSr = 48000.0;
constexpr double kPi = 3.14159265358979323846;
constexpr int kBlock = 480;
constexpr int kFade = 2400;                  // DuskVerbDSP's preset fade at 48 kHz
constexpr BandLimitSetting kNone {};
constexpr BandLimitSetting kLowpassOnly { 8500.0f, 16, 0.0f, 0.0f, 0.0f };
constexpr BandLimitSetting kLiftOnly { 0.0f, 0, 7000.0f, 4.0f, 0.9f };
constexpr BandLimitSetting kBoth { 8750.0f, 24, 8375.0f, 3.0f, 1.25f };

struct Stereo
{
    std::vector<float> l, r;
    bool operator== (const Stereo& o) const noexcept
    {
        return l.size() == o.l.size() && std::memcmp (l.data(), o.l.data(), l.size() * sizeof (float)) == 0
            && std::memcmp (r.data(), o.r.data(), r.size() * sizeof (float)) == 0;
    }
    bool operator!= (const Stereo& o) const noexcept { return ! (*this == o); }
    int size() const noexcept { return static_cast<int> (l.size()); }
};

float noiseAt (int t, int channel)
{
    std::uint32_t x = static_cast<std::uint32_t> (t * 2 + channel) * 2654435761u;
    x ^= x >> 15;
    x *= 2246822519u;
    x ^= x >> 13;
    return 0.25f * (static_cast<float> (x >> 8) / 8388608.0f - 1.0f);
}

Stereo noise (int n)
{
    Stereo s { std::vector<float> (static_cast<size_t> (n)), std::vector<float> (static_cast<size_t> (n)) };
    for (int i = 0; i < n; ++i)
    {
        s.l[static_cast<size_t> (i)] = noiseAt (i, 0);
        s.r[static_cast<size_t> (i)] = noiseAt (i, 1);
    }
    return s;
}

// `x` through a stage prepared at `sr` and set to `s` at once, in blocks of `block`.
Stereo through (const Stereo& x, const BandLimitSetting& s, double sr = kSr, int block = kBlock)
{
    BandLimit b;
    b.prepare (sr);
    b.swapTo (s, 0);
    Stereo y = x;
    for (int done = 0; done < y.size(); done += block)
        b.process (y.l.data() + done, y.r.data() + done, std::min (block, y.size() - done));
    return y;
}

// The closed form: the RBJ peaking section's magnitude (if it runs) times the Butterworth's (if it
// runs), derived here from the formulas, not from the stage's coefficients.
double formulaDb (const BandLimitSetting& s, double sr, double f)
{
    const double ceiling = duskaudio::kMaxDesignFreqRatio * sr, w = 2.0 * kPi * f / sr;
    double db = 0.0;
    if (BandLimit::lifts (s) && s.liftHz < ceiling)
    {
        const double A = std::pow (10.0, s.liftDb / 40.0), w0 = 2.0 * kPi * s.liftHz / sr;
        const double alpha = std::sin (w0) / (2.0 * s.liftQ);
        const std::complex<double> z = std::polar (1.0, -w);
        const std::complex<double> num = (1.0 + alpha * A) - 2.0 * std::cos (w0) * z + (1.0 - alpha * A) * z * z;
        const std::complex<double> den = (1.0 + alpha / A) - 2.0 * std::cos (w0) * z + (1.0 - alpha / A) * z * z;
        db += 20.0 * std::log10 (std::abs (num / den));
    }
    if (BandLimit::lowpasses (s) && s.cornerHz < ceiling)
    {
        const double ratio = std::tan (kPi * f / sr) / std::tan (kPi * s.cornerHz / sr);
        db += -10.0 * std::log10 (1.0 + std::pow (ratio, 2.0 * s.order));
    }
    return db;
}

// The stage's measured magnitude at `f` (dB): a DFT bin of its impulse response.
double measuredDb (const std::vector<float>& h, double sr, double f)
{
    std::complex<double> acc = 0.0;
    for (size_t n = 0; n < h.size(); ++n)
        acc += static_cast<double> (h[n]) * std::polar (1.0, -2.0 * kPi * f * static_cast<double> (n) / sr);
    return 20.0 * std::log10 (std::max (std::abs (acc), 1e-300));
}

void testTheResponseIsTheFormula()
{
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
        for (const BandLimitSetting& s : { kLowpassOnly, kLiftOnly, kBoth })
        {
            const int n = static_cast<int> (sr);     // one second: every section rung out to far below float
            Stereo x { std::vector<float> (static_cast<size_t> (n)), std::vector<float> (static_cast<size_t> (n)) };
            x.l[0] = x.r[0] = 1.0f;
            const Stereo y = through (x, s, sr);
            double worst = 0.0;
            int compared = 0;
            for (double f = 100.0; f < 0.45 * sr; f *= std::pow (2.0, 1.0 / 12.0))
            {
                const double want = formulaDb (s, sr, f);
                if (want < -80.0)                    // float rounding of the impulse sets the floor there
                    continue;
                worst = std::max (worst, std::abs (measuredDb (y.l, sr, f) - want));
                ++compared;
            }
            checkf (worst <= 0.01, "%g Hz rate, corner %g order %d lift %g Hz %+g dB Q %g: the response strays %.4f dB"
                                   " from the formula", sr, s.cornerHz, s.order, s.liftHz, s.liftDb, s.liftQ, worst);
            checkf (compared > 40, "only %d frequencies compared", compared);
        }
    // The corner is the corner at every rate: -3.01 dB there.
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
        checkf (std::abs (formulaDb (kLowpassOnly, sr, 8500.0) + 10.0 * std::log10 (2.0)) < 1e-9,
                "the closed form misses -3 dB at the corner at %g Hz", sr);
    std::printf ("BandLimit: the response follows the closed form within 0.01 dB at 44.1, 48 and 96 kHz\n");
}

// At 16 kHz the design ceiling is 7997 Hz: a lift at 8200 Hz and a corner at 8500 Hz do not run, so
// the stage is the identity; a lift at 7000 Hz runs alone, as if it had no low-pass.
void testSectionsAboveTheCeilingDoNotRun()
{
    const Stereo x = noise (4800);
    BandLimit b;
    b.prepare (16000.0);
    b.swapTo ({ 8500.0f, 16, 8200.0f, 4.0f, 0.9f }, 0);
    checkf (! b.active(), "at 16 kHz a band limit above the ceiling still runs a section");
    checkf (through (x, { 8500.0f, 16, 8200.0f, 4.0f, 0.9f }, 16000.0) == x, "at 16 kHz the stage is not the identity");
    const Stereo lifted = through (x, { 8500.0f, 16, 7000.0f, 4.0f, 0.9f }, 16000.0);
    checkf (lifted == through (x, kLiftOnly, 16000.0), "at 16 kHz the low-pass above the ceiling still runs");
    checkf (lifted != x, "at 16 kHz the lift under the ceiling does not run");
}

void testNoneIsTheIdentity()
{
    const Stereo x = noise (9600);
    BandLimit b;
    b.prepare (kSr);
    checkf (! b.active(), "a fresh stage is active");
    checkf (through (x, kNone) == x, "no band limit changes the wet");
    // From a band limit to none: after the fade, the wet passes untouched again.
    b.swapTo (kBoth, 0);
    Stereo y = x;
    b.process (y.l.data(), y.r.data(), 4800);
    b.swapTo (kNone, kFade);
    b.process (y.l.data() + 4800, y.r.data() + 4800, kFade);
    checkf (! b.active(), "the stage still runs after fading to none");
    const Stereo before = y;
    b.process (y.l.data() + 4800 + kFade, y.r.data() + 4800 + kFade, 9600 - 4800 - kFade);
    checkf (std::memcmp (y.l.data() + 4800 + kFade, before.l.data() + 4800 + kFade, sizeof (float) * (9600 - 4800 - kFade)) == 0,
            "after fading to none, the stage still touches the wet");
}

void testBlockSizeInvariantAndDeterministic()
{
    const Stereo x = noise (24000);
    const Stereo a = through (x, kBoth, kSr, 1), b = through (x, kBoth, kSr, 37), c = through (x, kBoth, kSr, 4096);
    checkf (a == b && b == c, "the band limit depends on the block size");
    checkf (through (x, kBoth) == through (x, kBoth), "the band limit is not deterministic");
    checkf (through (x, kBoth) != x, "the band limit changes nothing, so this test cannot see it");
}

// A swap: the outgoing cascade carries on, the incoming one starts from rest, and the output
// fades linearly from the first to the second over the fade; after it, the incoming alone. A swap
// to the setting playing is none.
void testASwapIsALinearCrossfade()
{
    constexpr int n = 12000, at = 4800;
    const Stereo x = noise (n);
    BandLimit b;
    b.prepare (kSr);
    b.swapTo (kLowpassOnly, 0);
    Stereo y = x;
    b.process (y.l.data(), y.r.data(), at);
    b.swapTo (kLowpassOnly, kFade);             // to the setting playing: none
    checkf (b.active() && b.setting() == kLowpassOnly, "a swap to the setting playing changed it");
    b.swapTo (kBoth, kFade);
    b.process (y.l.data() + at, y.r.data() + at, n - at);

    const Stereo out = through (x, kLowpassOnly);           // the outgoing, run all along
    Stereo tail { std::vector<float> (x.l.begin() + at, x.l.end()), std::vector<float> (x.r.begin() + at, x.r.end()) };
    const Stereo in = through (tail, kBoth);                // the incoming, from rest at the swap
    double worst = 0.0;
    for (int i = at; i < n; ++i)
    {
        const int k = i - at;
        const float t = k < kFade ? std::min (1.0f, static_cast<float> (k) / static_cast<float> (kFade - 1)) : 1.0f;
        const float o = out.l[static_cast<size_t> (i)], c = in.l[static_cast<size_t> (k)];
        const float want = k < kFade ? o + t * (c - o) : c;
        worst = std::max (worst, static_cast<double> (std::abs (y.l[static_cast<size_t> (i)] - want)));
    }
    checkf (worst <= 1e-7, "a swap is %g from the linear crossfade of the two cascades", worst);
    checkf (std::memcmp (y.l.data(), out.l.data(), sizeof (float) * at) == 0, "the band limit before the swap moved");
}

void testNoAllocation()
{
    const Stereo x = noise (9600);
    BandLimit b;
    b.prepare (kSr);
    Stereo y = x;
    gAllocations.store (0);
    gCountAllocations.store (true);
    b.swapTo (kBoth, 0);
    b.process (y.l.data(), y.r.data(), 4800);
    b.swapTo (kLiftOnly, kFade);
    b.process (y.l.data() + 4800, y.r.data() + 4800, 4800);
    gCountAllocations.store (false);
    checkf (gAllocations.load() == 0, "the band limit allocated %d times", gAllocations.load());
}

void testTableAndParser()
{
    checkf (duskverb::bandLimitFor ("No Such Preset") == kNone, "a preset without a row gets a band limit");
    for (const auto& row : duskverb::kBandLimitByName)
    {
        checkf (duskverb::bandLimitFor (row.first) == row.second, "'%.*s' does not get its own row",
                static_cast<int> (row.first.size()), row.first.data());
        bool named = false;
        for (const auto& preset : duskverb::dv2FactoryPresets())
            named |= row.first == preset.name;
        checkf (named, "the band-limit row '%.*s' names no factory preset", static_cast<int> (row.first.size()), row.first.data());
    }

    BandLimitSetting s {};
    checkf (duskverb::parseBandLimit ("8750, 24, 8375, 3, 1.25", s) && s == kBoth, "a valid hook text is refused or misread");
    char text[128];
    duskverb::formatBandLimit (kBoth, text, sizeof (text));
    BandLimitSetting back {};
    checkf (duskverb::parseBandLimit (text, back) && back == kBoth, "the hook text '%s' does not read back", text);
    checkf (duskverb::parseBandLimit ("0,0,0,0,0", s) && s == kNone, "'0,0,0,0,0' is not none");
    const BandLimitSetting kept = kLiftOnly;
    for (const char* bad : { "", "8500,16,7000,4", "8500,16,7000,4,0.9,1", "8500,15,7000,4,0.9", "8500,16.5,7000,4,0.9",
                             "500,16,7000,4,0.9", "8500,26,7000,4,0.9", "8500,16,7000,0,0.9", "8500,16,7000,13,0.9",
                             "8500,16,7000,4,0.1", "8500,0,0,0,0", "x,16,7000,4,0.9", "nan,16,7000,4,0.9" })
    {
        s = kept;
        char error[128] = "";
        checkf (! duskverb::parseBandLimit (bad, s, error, sizeof (error)) && s == kept && error[0] != '\0',
                "the hook text '%s' is not refused whole with a reason", bad);
    }
}

// The sparse-ER tone: off (0 Hz, or a corner at the ceiling) leaves the buffer bit for bit; on, its
// measured magnitude is the 4th-order Butterworth's |H|^2 = 1 / (1 + (tan (pi f / fs) / tan (pi fc / fs))^8).
void testTheSparseERTone()
{
    SparseEarlyField field;
    field.prepare (kSr, kBlock);
    const Stereo x = noise (4800);
    Stereo y = x;
    field.applyTone (y.l.data(), y.r.data(), y.size());
    checkf (y == x, "the sparse-ER tone, never set, changes the field");
    field.setToneLowpassHz (24000.0f);
    field.applyTone (y.l.data(), y.r.data(), y.size());
    checkf (y == x, "a sparse-ER tone at the ceiling changes the field");

    field.setToneLowpassHz (8000.0f);
    const int n = 48000;
    Stereo h { std::vector<float> (static_cast<size_t> (n)), std::vector<float> (static_cast<size_t> (n)) };
    h.l[0] = h.r[0] = 1.0f;
    field.applyTone (h.l.data(), h.r.data(), n);
    double worst = 0.0;
    for (double f = 100.0; f < 0.45 * kSr; f *= std::pow (2.0, 1.0 / 12.0))
    {
        const double ratio = std::tan (kPi * f / kSr) / std::tan (kPi * 8000.0 / kSr);
        const double want = -10.0 * std::log10 (1.0 + std::pow (ratio, 8.0));
        if (want < -80.0)
            continue;
        worst = std::max (worst, std::abs (measuredDb (h.l, kSr, f) - want));
    }
    checkf (worst <= 0.01, "the sparse-ER tone strays %.4f dB from the 4th-order Butterworth", worst);
    field.setToneLowpassHz (0.0f);
    y = x;
    field.applyTone (y.l.data(), y.r.data(), y.size());
    checkf (y == x, "the sparse-ER tone set back to 0 Hz still changes the field");
}

// ---- inside DuskVerbDSP -----------------------------------------------------

void setEnv (const char* name, const char* value)
{
#if defined (_WIN32)
    _putenv_s (name, value != nullptr ? value : "");
#else
    if (value != nullptr) setenv (name, value, 1); else unsetenv (name);
#endif
}

void setPlain (DuskVerbDSP& dsp, int index, float plain)
{
    const auto& d = duskverb::paramDesc (index);
    dsp.setParameter (index, duskverb::dspFromHost (d, duskverb::plainToHost (d, plain)));
}

// The impulse, then from 0.4 s a quarter-scale noise burst.
float impulseThenNoise (int t, int channel)
{
    if (t == 0)
        return 1.0f;
    return t < 19200 || t >= 28800 ? 0.0f : noiseAt (t, channel);
}

const FactoryPreset& presetNamed (const char* name)
{
    for (const auto& preset : duskverb::dv2FactoryPresets())
        if (std::strcmp (preset.name, name) == 0)
            return preset;
    std::printf ("FAIL: no factory preset '%s'\n", name);
    std::exit (1);
}

void setPreset (DuskVerbDSP& dsp, const FactoryPreset& preset)
{
    duskverb::collectDv2Parameters (preset, [&] (const char* id, float v) {
        const int i = duskverb::paramIndexForId (id);
        if (i >= 0) setPlain (dsp, i, v);
    });
    setPlain (dsp, duskverb::Mix, 1.0f);
}

// The impulse, then from 0.4 s a quarter-scale noise burst, at 100 % wet, preset loaded at block 0;
// at block `at` (if any) the session goes preset-less, and at `reset` (if any) the host resets.
Stereo render (const FactoryPreset& preset, const BandLimitSetting* override, int n, int at = -1, int reset = -1)
{
    auto dsp = std::make_unique<DuskVerbDSP> (nullptr, nullptr, override);
    setPreset (*dsp, preset);
    dsp->applyFactoryPresetConfig (preset);
    dsp->prepare (kSr, kBlock);
    std::vector<float> l (kBlock), r (kBlock);
    const float* in[2] = { l.data(), r.data() };
    float* out[2] = { l.data(), r.data() };
    Stereo result { std::vector<float> (static_cast<size_t> (n)), std::vector<float> (static_cast<size_t> (n)) };
    for (int done = 0, block = 0; done < n; done += kBlock, ++block)
    {
        if (block == at) dsp->restorePresetIdentity (nullptr);
        if (block == reset) dsp->clearState();
        const int m = std::min (kBlock, n - done);
        for (int i = 0; i < m; ++i)
        {
            l[static_cast<size_t> (i)] = impulseThenNoise (done + i, 0);
            r[static_cast<size_t> (i)] = impulseThenNoise (done + i, 1);
        }
        dsp->processBlock (in, out, 2, m);
        std::copy (l.begin(), l.begin() + m, result.l.begin() + done);
        std::copy (r.begin(), r.begin() + m, result.r.begin() + done);
    }
    return result;
}

void testPresetsWithoutARowAreUnchanged()
{
    setEnv ("DUSKVERB2_BANDLIMIT", nullptr);
    int compared = 0;
    for (const auto& preset : duskverb::dv2FactoryPresets())
    {
        if (duskverb::bandLimitFor (preset.name) != kNone)
            continue;
        checkf (render (preset, nullptr, 24000) == render (preset, &kNone, 24000),
                "'%s' renders differently from one given no band limit", preset.name);
        ++compared;
    }
    std::printf ("BandLimit DSP: %d presets without a row render as with none (tuning hooks %s)\n",
                 compared, DUSKVERB2_TUNING_HOOKS ? "compiled in" : "compiled out");
}

void testTheTuningHook()
{
    const FactoryPreset& preset = presetNamed ("Large Chamber");
    setEnv ("DUSKVERB2_BANDLIMIT", "8750,24,8375,3,1.25");
    const Stereo hooked = render (preset, nullptr, 14400);
    const Stereo overridden = render (preset, &kNone, 14400);          // an override wins over the hook
    setEnv ("DUSKVERB2_BANDLIMIT", "8750,23,8375,3,1.25");
    const Stereo refused = render (preset, nullptr, 14400);
    setEnv ("DUSKVERB2_BANDLIMIT", nullptr);
    const Stereo own = render (preset, nullptr, 14400);
    const Stereo given = render (preset, &kBoth, 14400);
    checkf (given != own, "the hook's band limit changes nothing, so this test cannot see it");
    checkf (refused == own, "a refused DUSKVERB2_BANDLIMIT changes the render");
    checkf (overridden == render (preset, &kNone, 14400), "DUSKVERB2_BANDLIMIT wins over a setting given directly");
#if DUSKVERB2_TUNING_HOOKS
    checkf (hooked == given, "DUSKVERB2_BANDLIMIT does not render as the same setting given directly");
#else
    checkf (hooked == own, "a build without DUSKVERB2_TUNING_HOOKS reads DUSKVERB2_BANDLIMIT");
#endif
}

// With a band limit, a session renders, to float rounding, what the stage makes of the same session
// rendered without one, swapped where DuskVerbDSP swaps: at the load, to none at a preset-less
// restore, and ended by a host reset mid-fade.
void testTheBandLimitIsTheStageOnTheSummedWet()
{
    constexpr int n = 33600, at = 40;
    for (const char* name : { "79 Vocal Chamber", "Large Chamber" })
    {
        const FactoryPreset& preset = presetNamed (name);
        for (const int reset : { -1, at + 2 })
        {
            const Stereo with = render (preset, &kBoth, n, at, reset);
            const Stereo none = render (preset, &kNone, n, at, reset);
            BandLimit b;
            b.prepare (kSr);
            Stereo model = none;
            for (int done = 0, block = 0; done < n; done += kBlock, ++block)
            {
                if (block == 0) b.swapTo (kBoth, kFade);
                if (block == at) b.swapTo (kNone, kFade);
                if (block == reset) b.clear();
                b.process (model.l.data() + done, model.r.data() + done, std::min (kBlock, n - done));
            }
            double off = 0.0, moved = 0.0;
            for (int i = 0; i < n; ++i)
            {
                off = std::max (off, static_cast<double> (std::abs (with.l[static_cast<size_t> (i)] - model.l[static_cast<size_t> (i)])));
                off = std::max (off, static_cast<double> (std::abs (with.r[static_cast<size_t> (i)] - model.r[static_cast<size_t> (i)])));
                moved = std::max (moved, static_cast<double> (std::abs (with.l[static_cast<size_t> (i)] - none.l[static_cast<size_t> (i)])));
            }
            checkf (off <= 1e-6, "%s%s: the render is %g from the stage run over the render without one", name,
                    reset >= 0 ? ", reset mid-fade" : "", off);
            checkf (moved > 1e-4, "%s: the band limit moves the render by only %g, so this test cannot see it", name, moved);
        }
    }
    std::printf ("BandLimit DSP: a render with a band limit is the stage over the render without one\n");
}

void testNoAllocationInProcess()
{
    const FactoryPreset& preset = presetNamed ("79 Vocal Chamber");
    auto dsp = std::make_unique<DuskVerbDSP> (nullptr, nullptr, &kBoth);
    setPreset (*dsp, preset);
    dsp->applyFactoryPresetConfig (preset);
    dsp->prepare (kSr, kBlock);
    std::vector<float> l (kBlock, 0.0f), r (kBlock, 0.0f);
    l[0] = r[0] = 1.0f;
    const float* in[2] = { l.data(), r.data() };
    float* out[2] = { l.data(), r.data() };
    gAllocations.store (0);
    gCountAllocations.store (true);
    for (int block = 0; block < 20; ++block)
        dsp->processBlock (in, out, 2, kBlock);
    gCountAllocations.store (false);
    checkf (gAllocations.load() == 0, "processing with a band limit allocated %d times", gAllocations.load());
}
} // namespace

int main()
{
    testTheResponseIsTheFormula();
    testSectionsAboveTheCeilingDoNotRun();
    testNoneIsTheIdentity();
    testBlockSizeInvariantAndDeterministic();
    testASwapIsALinearCrossfade();
    testNoAllocation();
    testTableAndParser();
    testTheSparseERTone();
    testPresetsWithoutARowAreUnchanged();
    testTheTuningHook();
    testTheBandLimitIsTheStageOnTheSummedWet();
    testNoAllocationInProcess();
    if (failures == 0)
        std::printf ("BandLimit: all tests passed\n");
    return failures == 0 ? 0 : 1;
}
