// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// The octave trim inside DuskVerbDSP. Every preset without a row renders as a flat trim does,
// whether the tuning hooks are compiled in or not; DUSKVERB2_OCTAVETRIM takes effect only in a
// hook build, and only whole. The trim is the stage (DuskVerbOctaveTrim.hpp) on the summed wet:
// a render with one is, to float rounding, the stage run over the same render without one,
// swapped where DuskVerbDSP swaps (a preset load, a preset-less session, a reload, a change of
// engine family, a host reset, a re-prepare), early fields and own engines included; and it sits
// ahead of the ducker and the dry/wet mix, which never feed it. Through a
// real preset swap under a sustained tone, a swap between two trims is click-free and
// swell-free, measured against the ideal crossfade and against the trim-free swap. Processing
// with a trim allocates nothing.

#include "DuskVerbDSP.hpp"
#include "DuskVerbDv2Voicing.hpp"
#include "DuskVerbParamTable.hpp"
#include "DuskVerbPresets.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
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

using duskverb::DuskVerbDSP;
using duskverb::OctaveTrim;
using duskverb::OctaveTrimGains;

constexpr double kSr = 48000.0;
constexpr int kBlock = 256;
constexpr int kSettle = 4800;          // silence first: the first swap's 50 ms fade is over by then
constexpr int kFade = 2400;            // DuskVerbDSP's preset fade at 48 kHz
constexpr double kPi = 3.14159265358979323846;
const OctaveTrimGains kFlat {};
const OctaveTrimGains kTrim { { 1.25f, -0.5f, 2.75f, -1.0f, -2.0f, 0.3f, -1.1f, 2.9f, -2.6f } };   // every band, signs mixed

void setEnv (const char* name, const char* value)
{
#if defined (_WIN32)
    _putenv_s (name, value != nullptr ? value : "");
#else
    if (value != nullptr) setenv (name, value, 1);
    else unsetenv (name);
#endif
}

void setPlain (DuskVerbDSP& dsp, int index, float plain)
{
    const auto& d = duskverb::paramDesc (index);
    dsp.setParameter (index, duskverb::dspFromHost (d, duskverb::plainToHost (d, plain)));
}

const FactoryPreset& presetNamed (const char* name)
{
    for (const auto& preset : duskverb::dv2FactoryPresets())
        if (std::strcmp (preset.name, name) == 0)
            return preset;
    std::printf ("FAIL: no factory preset named '%s'\n", name);
    std::exit (1);
}

// `preset`'s values, as a host writes them before loading it, at 100 % wet.
void setPreset (DuskVerbDSP& dsp, const FactoryPreset& preset)
{
    duskverb::collectDv2Parameters (preset, [&] (const char* id, float v) {
        const int i = duskverb::paramIndexForId (id);
        if (i >= 0) setPlain (dsp, i, v);
    });
    setPlain (dsp, duskverb::Mix, 1.0f);
}

struct Render
{
    std::vector<float> l, r;
    bool operator== (const Render& o) const noexcept
    {
        return l.size() == o.l.size() && r.size() == o.r.size()
            && std::memcmp (l.data(), o.l.data(), l.size() * sizeof (float)) == 0
            && std::memcmp (r.data(), o.r.data(), r.size() * sizeof (float)) == 0;
    }
    bool operator!= (const Render& o) const noexcept { return ! (*this == o); }
    const std::vector<float>& side (int s) const noexcept { return s == 0 ? l : r; }
    int size() const noexcept { return static_cast<int> (l.size()); }
};

Render silence (int n) { return { std::vector<float> (static_cast<size_t> (n)), std::vector<float> (static_cast<size_t> (n)) }; }

double worstDifference (const Render& a, const Render& b)
{
    double worst = 0.0;
    for (int s = 0; s < 2; ++s)
        for (size_t i = 0; i < a.l.size(); ++i)
            worst = std::max (worst, std::abs (static_cast<double> (a.side (s)[i]) - b.side (s)[i]));
    return worst;
}

float noiseAt (int t, int channel)
{
    std::uint32_t x = static_cast<std::uint32_t> (t * 2 + channel) * 2654435761u;
    x ^= x >> 15;
    x *= 2246822519u;
    x ^= x >> 13;
    return 0.25f * (static_cast<float> (x >> 8) / 8388608.0f - 1.0f);
}

// The impulse, then from 0.4 s a quarter-scale noise burst.
float impulseThenNoise (int t, int channel)
{
    if (t == 0)
        return 1.0f;
    return t < 19200 || t >= 28800 ? 0.0f : noiseAt (t, channel);
}

// The brief's sustained stimulus: 440 Hz at -13.3 dBFS peak.
float tone440 (int t, int) { return 0.2162718f * static_cast<float> (std::sin (2.0 * kPi * 440.0 * t / kSr)); }

// A host's session at 100 % wet, from the first block on: kSettle samples of silence, in which
// the first swap's fade runs, then `source (t, channel)`. `before (dsp, block)` runs ahead of each
// block, so a swap it arms runs at that block's first sample, and once as block -1 to set the
// session's values up. Preset-less, the session starts with no preset identity (a restored session
// that names none). The values go in before the prepare, as the plugin's activate() puts them:
// the mix then starts on its value, where a mix gliding up to 1 stalls 3e-6 short of it in float
// and lets the dry through at -107 dB.
template <typename Source, typename Before>
Render render (const FactoryPreset& preset, const OctaveTrimGains* trim, int length, Source&& source,
               Before&& before, bool presetLess = false)
{
    auto dsp = std::make_unique<DuskVerbDSP> (nullptr, trim);
    setPreset (*dsp, preset);
    before (*dsp, -1);
    if (presetLess)
        dsp->restorePresetIdentity (nullptr);
    else
        dsp->applyFactoryPresetConfig (preset);
    dsp->prepare (kSr, kBlock);

    std::vector<float> l (kBlock), r (kBlock);
    const float* in[2] = { l.data(), r.data() };
    float* out[2] = { l.data(), r.data() };
    Render result = silence (length);
    for (int done = 0, block = 0; done < length; done += kBlock, ++block)
    {
        before (*dsp, block);
        const int n = std::min (kBlock, length - done);
        for (int i = 0; i < n; ++i)
        {
            const int t = done + i - kSettle;
            l[static_cast<size_t> (i)] = t < 0 ? 0.0f : source (t, 0);
            r[static_cast<size_t> (i)] = t < 0 ? 0.0f : source (t, 1);
        }
        dsp->processBlock (in, out, 2, n);
        std::copy (l.begin(), l.begin() + n, result.l.begin() + done);
        std::copy (r.begin(), r.begin() + n, result.r.begin() + done);
    }
    return result;
}

Render render (const FactoryPreset& preset, const OctaveTrimGains* trim, int length)
{
    return render (preset, trim, length, impulseThenNoise, [] (DuskVerbDSP&, int) {});
}

// The stage over `wet` in the session's blocks, `at (stage, block)` ahead of each.
template <typename At>
Render trimmed (const Render& wet, At&& at)
{
    OctaveTrim t;
    t.prepare (kSr);
    Render y = wet;
    for (int done = 0, block = 0; done < y.size(); done += kBlock, ++block)
    {
        at (t, block);
        t.process (y.l.data() + done, y.r.data() + done, std::min (kBlock, y.size() - done));
    }
    return y;
}

// RMS (dBFS, both sides) of `x` over [from, from + length), and of x above 4 kHz: an 8th-order
// Butterworth high-pass run over the whole of x, so its start-up is long gone at `from`.
double rmsDb (const Render& x, int from, int length)
{
    double e = 0.0;
    for (int s = 0; s < 2; ++s)
        for (int i = from; i < from + length; ++i)
            e += static_cast<double> (x.side (s)[static_cast<size_t> (i)]) * x.side (s)[static_cast<size_t> (i)];
    return 10.0 * std::log10 (std::max (e / (2.0 * length), 1e-300));
}

double highBandDb (const Render& x, int from, int length)
{
    static constexpr double kQ[4] = { 0.50979558, 0.60134489, 0.89997622, 2.56291545 };
    Render y = x;
    for (int s = 0; s < 2; ++s)
    {
        auto& v = s == 0 ? y.l : y.r;
        for (double q : kQ)
        {
            const double w0 = 2.0 * kPi * 4000.0 / kSr, c = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
            const double a0 = 1.0 + alpha, b0 = 0.5 * (1.0 + c) / a0, b1 = -(1.0 + c) / a0, a1 = -2.0 * c / a0, a2 = (1.0 - alpha) / a0;
            double z1 = 0.0, z2 = 0.0;
            for (float& sample : v)
            {
                const double in = sample, out = b0 * in + z1;
                z1 = b1 * in - a1 * out + z2;
                z2 = b0 * in - a2 * out;
                sample = static_cast<float> (out);
            }
        }
    }
    return rmsDb (y, from, length);
}

// Every preset without a row renders exactly as a flat trim (of +0 or -0 dB bands) does: here
// with the hooks compiled in or out as this build was configured, and DUSKVERB2_OCTAVETRIM unset.
void testPresetsWithoutATrimAreUnchanged()
{
    setEnv ("DUSKVERB2_OCTAVETRIM", nullptr);
    OctaveTrimGains negativeZeros;
    negativeZeros.fill (-0.0f);
    int compared = 0;
    for (const auto& preset : duskverb::dv2FactoryPresets())
    {
        if (! OctaveTrim::flat (duskverb::octaveTrimFor (preset.name)))
            continue;
        const Render plain = render (preset, nullptr, 40800);
        checkf (plain == render (preset, &kFlat, 40800), "'%s' renders differently from a flat trim", preset.name);
        checkf (plain == render (preset, &negativeZeros, 40800), "'%s' renders differently from a trim of -0 dB bands", preset.name);
        ++compared;
    }
    std::printf ("OctaveTrim DSP: %d presets without a trim render as a flat one (tuning hooks %s)\n",
                 compared, DUSKVERB2_TUNING_HOOKS ? "compiled in" : "compiled out");
    const size_t rowless = duskverb::dv2FactoryPresets().size() - duskverb::kOctaveTrimByName.size();
    checkf (static_cast<size_t> (compared) == rowless, "%d presets play a flat trim, but %zu have no row", compared, rowless);
}

void testTheTuningHook()
{
    const FactoryPreset& preset = presetNamed ("Large Chamber");
    const char* text = "1.25,-0.5,2.75,-1,-2,0.3,-1.1,2.9,-2.6";
    OctaveTrimGains parsed {};
    checkf (duskverb::parseOctaveTrim (text, parsed) && parsed == kTrim, "the test's hook text is refused or misread");
    setEnv ("DUSKVERB2_OCTAVETRIM", text);
    const Render hooked = render (preset, nullptr, 14400);
    const Render overridden = render (preset, &kFlat, 14400);           // an override wins over the hook
    setEnv ("DUSKVERB2_OCTAVETRIM", "1.25,-0.5,2.75");
    const Render tooFew = render (preset, nullptr, 14400);
    setEnv ("DUSKVERB2_OCTAVETRIM", "1.25,-0.5,2.75,-1,-2,0.3,-1.1,2.9,3.5");
    const Render outOfRange = render (preset, nullptr, 14400);
    setEnv ("DUSKVERB2_OCTAVETRIM", nullptr);
    const Render own = render (preset, nullptr, 14400);                 // the preset's own row, if it has one
    const Render flat = render (preset, &kFlat, 14400);
    const Render given = render (preset, &kTrim, 14400);
    checkf (given != flat, "the hook's trim changes nothing, so this test cannot see it");
    checkf (tooFew == own && outOfRange == own, "a refused DUSKVERB2_OCTAVETRIM changes the render");
    checkf (overridden == flat, "DUSKVERB2_OCTAVETRIM wins over a trim given directly");
#if DUSKVERB2_TUNING_HOOKS
    checkf (hooked == given, "DUSKVERB2_OCTAVETRIM does not render as the same trim given directly");
#else
    checkf (hooked == own, "a build without DUSKVERB2_TUNING_HOOKS reads DUSKVERB2_OCTAVETRIM");
#endif
}

// With a trim on every band, each session renders, to float rounding, what the stage makes of the
// same session rendered flat, swapped where DuskVerbDSP swaps: at the first block (the load), at a
// later load after a preset-less start, to flat at a preset-less restore, not at all at a reload
// or a change of engine family, ended at a host reset mid-fade, and started over at a re-prepare.
// On a preset with an early field (its post path is in the wet the trim takes), one without, and
// an own engine.
void testTheTrimIsTheStageOnTheSummedWet()
{
    static constexpr int n = 40800, at = 40;          // captureless callbacks also compile on MSVC
    struct Scenario
    {
        const char* name;
        bool presetLess;
        void (*session) (DuskVerbDSP&, const FactoryPreset&, int block);
        void (*stage) (OctaveTrim&, const OctaveTrimGains& trim, int block);
    };
    static const Scenario scenarios[] = {
        { "loaded", false, [] (DuskVerbDSP&, const FactoryPreset&, int) {},
          [] (OctaveTrim& t, const OctaveTrimGains& g, int b) { if (b == 0) t.swapTo (g, kFade); } },
        { "loaded after a preset-less start", true,
          [] (DuskVerbDSP& d, const FactoryPreset& p, int b) { if (b == at) d.applyFactoryPresetConfig (p); },
          [] (OctaveTrim& t, const OctaveTrimGains& g, int b) { if (b == at) t.swapTo (g, kFade); } },
        { "restored preset-less", false,
          [] (DuskVerbDSP& d, const FactoryPreset&, int b) { if (b == at) d.restorePresetIdentity (nullptr); },
          [] (OctaveTrim& t, const OctaveTrimGains& g, int b) { if (b == 0) t.swapTo (g, kFade); if (b == at) t.swapTo (kFlat, kFade); } },
        { "reloaded", false,
          [] (DuskVerbDSP& d, const FactoryPreset& p, int b) { if (b == at) d.applyFactoryPresetConfig (p); },
          [] (OctaveTrim& t, const OctaveTrimGains& g, int b) { if (b == 0) t.swapTo (g, kFade); } },
        { "engine family changed", false,
          [] (DuskVerbDSP& d, const FactoryPreset& p, int b) {
              if (b == at) setPlain (d, duskverb::Algorithm, duskverb::isOwnAlgorithm (p.algorithm) ? 10.0f
                                                                                                    : static_cast<float> (duskverb::kNonLinAlgorithm));
          },
          [] (OctaveTrim& t, const OctaveTrimGains& g, int b) { if (b == 0) t.swapTo (g, kFade); } },
        { "host reset mid-fade", false,
          [] (DuskVerbDSP& d, const FactoryPreset&, int b) { if (b == at) d.restorePresetIdentity (nullptr); if (b == at + 2) d.clearState(); },
          [] (OctaveTrim& t, const OctaveTrimGains& g, int b) {
              if (b == 0) t.swapTo (g, kFade);
              if (b == at) t.swapTo (kFlat, kFade);
              if (b == at + 2) t.clear();
          } },
        { "re-prepared", false,
          [] (DuskVerbDSP& d, const FactoryPreset&, int b) { if (b == at) { d.releaseResources(); d.prepare (kSr, kBlock); } },
          [] (OctaveTrim& t, const OctaveTrimGains& g, int b) { if (b == 0) t.swapTo (g, kFade); if (b == at) t.prepare (kSr); } },
    };
    for (const char* name : { "Large Chamber", "Surf '63 Spring", "Nonlinear Medium" })
    {
        const FactoryPreset& preset = presetNamed (name);
        double worst = 0.0, moved = 1.0e9;
        for (const Scenario& s : scenarios)
        {
            const auto session = [&] (DuskVerbDSP& d, int b) { s.session (d, preset, b); };
            const Render with = render (preset, &kTrim, n, impulseThenNoise, session, s.presetLess);
            const Render flat = render (preset, &kFlat, n, impulseThenNoise, session, s.presetLess);
            const Render model = trimmed (flat, [&] (OctaveTrim& t, int b) { s.stage (t, kTrim, b); });
            const double off = worstDifference (with, model), trim = worstDifference (with, flat);
            worst = std::max (worst, off);
            moved = std::min (moved, trim);
            checkf (off <= 1e-6, "%s, %s: the render is %g from the stage run over its flat render", name, s.name, off);
            checkf (trim > 1e-3, "%s, %s: the trim moves the render by only %g, so this test cannot see it", name, s.name, trim);
        }
        std::printf ("OctaveTrim DSP: %-17s %zu sessions: the render is the stage run over the flat render to within %.3g"
                     " (the trim moves it by at least %.3g)\n", name, std::size (scenarios), worst, moved);
    }
}

// The trim takes the wet before the ducker and the dry/wet mix. Fully dry (Mix 0, Bus Mode off),
// a render is the dry alone, trim or not. Ducked (Duck 1), the ducker's gain comes from the dry
// alone, so with the trim ahead of it a ducked trimmed render times an unducked flat one is the
// ducked flat render times the unducked trimmed one, sample by sample. With the trim behind it
// (the stage run over the ducked flat render) that does not hold, which the test checks it sees.
void testTheTrimSitsBeforeTheDuckerAndTheMix()
{
    const FactoryPreset& preset = presetNamed ("Large Chamber");
    constexpr int n = 40800;
    const auto setUp = [] (int index, float value) {
        return [index, value] (DuskVerbDSP& d, int b) {
            if (b != -1) return;
            setPlain (d, duskverb::BusMode, 0.0f);
            setPlain (d, index, value);
        };
    };
    const Render dryTrim = render (preset, &kTrim, n, impulseThenNoise, setUp (duskverb::Mix, 0.0f));
    const Render dryFlat = render (preset, &kFlat, n, impulseThenNoise, setUp (duskverb::Mix, 0.0f));
    checkf (dryTrim == dryFlat, "fully dry, the trim still changes the render (by %g): it is not on the wet alone",
            worstDifference (dryTrim, dryFlat));

    const Render duckedTrim = render (preset, &kTrim, n, impulseThenNoise, setUp (duskverb::Duck, 1.0f));
    const Render duckedFlat = render (preset, &kFlat, n, impulseThenNoise, setUp (duskverb::Duck, 1.0f));
    const Render openTrim = render (preset, &kTrim, n, impulseThenNoise, setUp (duskverb::Duck, 0.0f));
    const Render openFlat = render (preset, &kFlat, n, impulseThenNoise, setUp (duskverb::Duck, 0.0f));
    const Render behind = trimmed (duckedFlat, [] (OctaveTrim& t, int b) { if (b == 0) t.swapTo (kTrim, kFade); });
    const auto crossError = [&] (const Render& ducked) {
        double worst = 0.0, scale = 0.0;
        for (int s = 0; s < 2; ++s)
            for (size_t i = 0; i < static_cast<size_t> (n); ++i)
            {
                const double a = duckedFlat.side (s)[i], b = ducked.side (s)[i], c = openFlat.side (s)[i], d = openTrim.side (s)[i];
                worst = std::max (worst, std::abs (b * c - a * d));
                scale = std::max (scale, std::abs (a * d));
            }
        return worst / std::max (scale, 1e-30);
    };
    const double ahead = crossError (duckedTrim), wrongWay = crossError (behind);
    std::printf ("OctaveTrim DSP: ducked, trimmed x flat less flat x trimmed is %.2g of the products' peak (the trim behind the"
                 " ducker: %.2g); fully dry, the trim changes nothing\n", ahead, wrongWay);
    checkf (worstDifference (duckedFlat, openFlat) > 1e-2, "Duck 1 barely ducks (%g), so this test cannot see the order",
            worstDifference (duckedFlat, openFlat));
    checkf (ahead <= 1e-5, "ducked, the trim does not run ahead of the ducker (%.3g of the products' peak)", ahead);
    checkf (wrongWay >= 100.0 * std::max (ahead, 1e-9), "the test cannot tell the trim ahead of the ducker from behind it (%.3g, %.3g)",
            ahead, wrongWay);
}

// Large Chamber to Medium Drum Room under a sustained 440 Hz tone (-13.3 dBFS), 1.5 s in: the
// session rendered flat, then the stage over it, swapping from one trim to another where the
// engines swap (as the previous test shows DuskVerbDSP does). The ideal is the crossfade (with the
// engines' t) of the two trims, each run over the whole render. Measured, with a step and an
// equal-power fade as controls the measures must see:
//   click: what the swap adds to the ideal above 4 kHz in the 150 ms after it, against what the
//          trim-free swap itself puts there;
//   swell: the largest level change against the ideal in any 5 ms window from 20 ms before the
//          swap to 100 ms after the fade, and the level kept between the two trims' own levels.
void testASwapBetweenTwoTrimsIsClean()
{
    const FactoryPreset& from = presetNamed ("Large Chamber");
    const FactoryPreset& to = presetNamed ("Medium Drum Room");
    const int swapBlock = (kSettle + 72000) / kBlock, swap = swapBlock * kBlock, n = swap + 24000;
    // Both at their rows' Gain Trim, not DuskVerb 2's level match (kPresetOverrides): the click is
    // read against the swap's own energy, which the incoming preset sets, so turning one preset up
    // and the other down moves the measure with no change to the swap (the 2026-10-28 match, +3.5 and
    // -2.9 dB, moved every click by 6.4 dB: 56.2 -> 49.8 and 53.3 -> 46.9 on two of the pairs).
    const Render wet = render (from, &kFlat, n, tone440, [&] (DuskVerbDSP& d, int b) {
        if (b == -1)
            setPlain (d, duskverb::GainTrim, from.gainTrim);
        if (b == swapBlock)
        {
            setPreset (d, to);
            setPlain (d, duskverb::GainTrim, to.gainTrim);
            d.applyFactoryPresetConfig (to);
        }
    });
    struct Pair { const char* name; OctaveTrimGains a, b; };
    OctaveTrimGains up, down, alternating, inverted;
    up.fill (3.0f);
    down.fill (-3.0f);
    for (size_t k = 0; k < up.size(); ++k)
    {
        alternating[k] = k % 2 ? -3.0f : 3.0f;
        inverted[k] = -alternating[k];
    }
    const Pair pairs[] = {
        { "all +3 -> all -3", up, down }, { "all -3 -> all +3", down, up },
        { "alternating -> inverted", alternating, inverted }, { "mixed -> flat", kTrim, kFlat },
    };
    const float invSpan = 1.0f / static_cast<float> (kFade - 1);
    const double trimFreeHigh = highBandDb (wet, swap, 7200);
    for (const Pair& p : pairs)
    {
        const auto fixedAt = [] (const OctaveTrimGains& g) { return [g] (OctaveTrim& t, int b) { if (b == 0) t.swapTo (g, kFade); }; };
        const Render first = trimmed (wet, fixedAt (p.a)), second = trimmed (wet, fixedAt (p.b));
        const auto swapped = [&] (int fade) {
            return trimmed (wet, [&] (OctaveTrim& t, int b) {
                if (b == 0) t.swapTo (p.a, kFade);
                if (b == swapBlock) t.swapTo (p.b, fade);
            });
        };
        const Render got = swapped (kFade), step = swapped (0);
        Render ideal = first, equalPower = first;
        for (int i = swap; i < n; ++i)
            for (int s = 0; s < 2; ++s)
            {
                const size_t k = static_cast<size_t> (i);
                const float t = std::min (1.0f, static_cast<float> (i - swap) * invSpan);
                const float a = first.side (s)[k], b = second.side (s)[k];
                (s == 0 ? ideal.l : ideal.r)[k] = a + t * (b - a);
                (s == 0 ? equalPower.l : equalPower.r)[k] = std::cos (t * 1.5707963f) * a + std::sin (t * 1.5707963f) * b;
            }
        const auto click = [&] (const Render& x) {
            Render d = silence (n);
            for (int i = 0; i < n; ++i)
            {
                d.l[static_cast<size_t> (i)] = x.l[static_cast<size_t> (i)] - ideal.l[static_cast<size_t> (i)];
                d.r[static_cast<size_t> (i)] = x.r[static_cast<size_t> (i)] - ideal.r[static_cast<size_t> (i)];
            }
            return highBandDb (d, swap, 7200) - trimFreeHigh;
        };
        const auto swell = [&] (const Render& x) {
            double worst = 0.0;
            for (int w = swap - 960; w < swap + kFade + 4800; w += 240)
                worst = std::max (worst, std::abs (rmsDb (x, w, 240) - rmsDb (ideal, w, 240)));
            return worst;
        };
        double outside = 0.0;                       // past the two trims' own levels, mid-fade
        for (int w = swap; w + 240 <= swap + kFade; w += 240)
        {
            const double l = rmsDb (got, w, 240), la = rmsDb (first, w, 240), lb = rmsDb (second, w, 240);
            outside = std::max ({ outside, l - std::max (la, lb), std::min (la, lb) - l });
        }
        const double clickDb = click (got), stepDb = click (step), swellDb = swell (got), equalPowerDb = swell (equalPower);
        const double highVsIdeal = highBandDb (got, swap, 7200) - highBandDb (ideal, swap, 7200);
        const double highVsTrimFree = highBandDb (got, swap, 7200) - trimFreeHigh;
        std::printf ("OctaveTrim DSP: %-23s click %6.1f dB under the trim-free swap's own energy above 4 kHz (a step: %6.1f);"
                     " above 4 kHz %+.4f dB on the ideal, %+.2f dB on the trim-free swap; level within %.4f dB of the ideal"
                     " in every 5 ms (equal power: %.2f), %.4f dB outside the trims' own\n",
                     p.name, -clickDb, -stepDb, highVsIdeal, highVsTrimFree, swellDb, equalPowerDb, std::max (0.0, outside));
        checkf (clickDb <= -50.0, "%s: the swap adds %.1f dB (on the trim-free swap) above 4 kHz", p.name, clickDb);
        checkf (stepDb >= clickDb + 30.0, "%s: a step swap reads %.1f dB, too close to the fade's %.1f for the measure to see a click",
                p.name, stepDb, clickDb);
        checkf (std::abs (highVsIdeal) <= 0.01, "%s: the swap moves the energy above 4 kHz by %+.4f dB on the ideal", p.name, highVsIdeal);
        checkf (swellDb <= 0.05, "%s: the level moves %.4f dB from the ideal in a 5 ms window", p.name, swellDb);
        checkf (equalPowerDb >= 1.0, "%s: an equal-power fade moves the level only %.2f dB, too little for the measure to see a swell",
                p.name, equalPowerDb);
        checkf (outside <= 0.05, "%s: mid-fade the level leaves the two trims' own by %.4f dB", p.name, outside);
    }
}

// Processing with a trim allocates nothing: steadily, and through a fade to flat and back.
void testNoAllocationInProcess()
{
    const FactoryPreset& preset = presetNamed ("Large Chamber");
    auto dsp = std::make_unique<DuskVerbDSP> (nullptr, &kTrim);
    dsp->prepare (kSr, kBlock);
    setPreset (*dsp, preset);
    dsp->applyFactoryPresetConfig (preset);
    std::vector<float> l (kBlock), r (kBlock);
    const float* in[2] = { l.data(), r.data() };
    float* out[2] = { l.data(), r.data() };
    const auto block = [&] (int b) {
        for (int i = 0; i < kBlock; ++i)
        {
            l[static_cast<size_t> (i)] = noiseAt (b * kBlock + i, 0);
            r[static_cast<size_t> (i)] = noiseAt (b * kBlock + i, 1);
        }
        dsp->processBlock (in, out, 2, kBlock);
    };
    for (int b = 0; b < 20; ++b)
        block (b);                                            // the first swap's fade is over
    gAllocations.store (0);
    gCountAllocations.store (true);
    for (int b = 20; b < 60; ++b)
        block (b);
    gCountAllocations.store (false);
    const int steady = gAllocations.load();
    gAllocations.store (0);
    gCountAllocations.store (true);
    for (int b = 60; b < 120; ++b)
    {
        if (b == 60) dsp->restorePresetIdentity (nullptr);
        if (b == 90) dsp->applyFactoryPresetConfig (preset);
        block (b);
    }
    gCountAllocations.store (false);
    std::printf ("OctaveTrim DSP: %d allocations in 40 blocks with a trim, %d through a fade to flat and back\n",
                 steady, gAllocations.load());
    checkf (steady == 0, "%d heap allocations processing with a trim", steady);
    checkf (gAllocations.load() == 0, "%d heap allocations through a trim's fades", gAllocations.load());
}
} // namespace

int main()
{
    testPresetsWithoutATrimAreUnchanged();
    testTheTuningHook();
    testTheTrimIsTheStageOnTheSummedWet();
    testTheTrimSitsBeforeTheDuckerAndTheMix();
    testASwapBetweenTwoTrimsIsClean();
    testNoAllocationInProcess();
    if (failures != 0)
    {
        std::printf ("%d OctaveTrim DSP check(s) failed\n", failures);
        return 1;
    }
    std::printf ("OctaveTrim DSP: all checks passed\n");
    return 0;
}
