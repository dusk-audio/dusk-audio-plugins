// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// The early field inside DuskVerbDSP. A preset without one renders exactly what the field-off
// path renders, whether the tuning hooks are compiled in or not; DUSKVERB2_EARLYFIELD takes
// effect only in a hook build, and only whole. A field's post path reaches the wet output where
// its layout says, through the engine's output controls, each of which moves it as it moves the
// reverb; the own engines never play it; Freeze fades its input out (and back in) without a
// step, and it takes nothing new while frozen; a reload of the same
// preset leaves it untouched and a swap to another crossfades it linearly, apart from the
// engines; a preset-less session has none, and a host reset silences it.
//
// Most checks isolate the field: a config that sends nothing into the engine (inject 0) leaves
// both renders' engines identical, so the field-on render less the field-off one is the field's
// own contribution.

#include "DuskVerbDSP.hpp"
#include "DuskVerbDv2Voicing.hpp"
#include "DuskVerbParamTable.hpp"
#include "DuskVerbPresets.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#if ! defined (DUSKVERB2_TUNING_HOOKS)
  #define DUSKVERB2_TUNING_HOOKS 0
#endif

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
using duskverb::Dv2OutputShell;
using duskverb::EarlyField;
using duskverb::EarlyFieldConfig;
using duskverb::EarlyFieldInput;

constexpr double kSr = 48000.0;
constexpr int kBlock = 256;
constexpr int kSettle = 4800;          // silence first: the 50 ms preset fade is over by then
constexpr int kFade = 2400;            // DuskVerbDSP's preset fade at 48 kHz
constexpr double kPi = 3.14159265358979323846;
const EarlyFieldConfig kOff {};

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
};

// `length` samples of x from `from` on (all of it by default), less those of `minus` if given.
Render slice (const Render& x, int from = 0, int length = -1, const Render* minus = nullptr)
{
    if (length < 0) length = static_cast<int> (x.l.size()) - from;
    Render d { std::vector<float> (static_cast<size_t> (length)), std::vector<float> (static_cast<size_t> (length)) };
    for (size_t i = 0; i < static_cast<size_t> (length); ++i)
    {
        const size_t at = static_cast<size_t> (from) + i;
        d.l[i] = x.l[at] - (minus != nullptr ? minus->l[at] : 0.0f);
        d.r[i] = x.r[at] - (minus != nullptr ? minus->r[at] : 0.0f);
    }
    return d;
}

Render difference (const Render& a, const Render& b, int from = 0, int length = -1) { return slice (a, from, length, &b); }

float impulse (int t, int) { return t == 0 ? 1.0f : 0.0f; }

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

// As a host loads `preset` (100 % wet): kSettle samples of silence, then `length` samples of
// `source (t, channel)`, block by block. `afterBlock (dsp, block)` runs after each block of it.
template <typename Source, typename AfterBlock>
Render render (const FactoryPreset& preset, const EarlyFieldConfig* field, int length, Source&& source,
               AfterBlock&& afterBlock)
{
    // The octave trim (tested in DuskVerbOctaveTrimDspTests) is held flat, so these checks see
    // the field alone on presets that carry both.
    auto dsp = std::make_unique<DuskVerbDSP> (field, &duskverb::kFlatOctaveTrim);
    dsp->prepare (kSr, kBlock);
    setPreset (*dsp, preset);
    dsp->applyFactoryPresetConfig (preset);

    std::vector<float> l (kBlock), r (kBlock);
    const float* in[2] = { l.data(), r.data() };
    float* out[2] = { l.data(), r.data() };
    for (int done = 0; done < kSettle; done += kBlock)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        dsp->processBlock (in, out, 2, kBlock);
    }
    Render result { std::vector<float> (static_cast<size_t> (length)), std::vector<float> (static_cast<size_t> (length)) };
    for (int done = 0, block = 0; done < length; done += kBlock, ++block)
    {
        const int n = std::min (kBlock, length - done);
        for (int i = 0; i < n; ++i)
        {
            l[static_cast<size_t> (i)] = source (done + i, 0);
            r[static_cast<size_t> (i)] = source (done + i, 1);
        }
        dsp->processBlock (in, out, 2, n);
        std::copy (l.begin(), l.begin() + n, result.l.begin() + done);
        std::copy (r.begin(), r.begin() + n, result.r.begin() + done);
        afterBlock (*dsp, block);
    }
    return result;
}

template <typename Source = float (*) (int, int)>
Render render (const FactoryPreset& preset, const EarlyFieldConfig* field, int length, Source&& source = impulse)
{
    return render (preset, field, length, source, [] (DuskVerbDSP&, int) {});
}

// The output stage DuskVerbDSP is to give a field under `preset` (and `knobs` on top): the
// controls the engine's own output applies, as the engine is given them (Hi Cut times the Tone
// macro's factor), settled.
struct Knob { int index; float value; };

Dv2OutputShell stageFor (const FactoryPreset& preset, const std::vector<Knob>& knobs = {})
{
    // The two-engine DSP exceeds Windows' default 1 MB thread stack.
    // This is test setup, so use the same heap ownership as render().
    auto dsp = std::make_unique<DuskVerbDSP> (nullptr, &duskverb::kFlatOctaveTrim);
    setPreset (*dsp, preset);
    for (const Knob& k : knobs)
        setPlain (*dsp, k.index, k.value);
    const auto p = [&dsp] (int index) { return dsp->getParameter (index); };
    Dv2OutputShell s;
    s.prepare (kSr);
    s.setLoCut (p (duskverb::LoCut));
    s.setHiCut (std::clamp (p (duskverb::HiCut) * std::pow (2.0f, p (duskverb::Tone) * 0.6f), 200.0f, 20000.0f));
    s.setHiCutShelfDb (p (duskverb::HiCutShelfDb));
    s.setMonoBelow (p (duskverb::MonoBelow));
    s.setMonoBelowDepth (p (duskverb::MonoBelowDepth));
    s.setWidth (p (duskverb::Width));
    s.setGainTrim (p (duskverb::GainTrim));
    s.snap();
    return s;
}

// The field's own answer to the impulse, built from its layout rather than by running it: per
// side, the lowpass of the direct taps and that side's reflections, then `stage`.
std::array<std::vector<float>, 2> predict (const EarlyFieldConfig& c, int length, Dv2OutputShell stage)
{
    EarlyFieldInput input;
    input.prepare (kSr, kBlock);
    EarlyField f;
    f.prepare (input);
    f.configure (c);
    const auto gain = [] (float db) { return db <= EarlyField::kOffDb ? 0.0f : std::pow (10.0f, db / 20.0f); };
    const float direct = gain (c.directDb), refl = gain (c.reflDb);
    const float hz = std::min (c.lowpassHz, 0.45f * static_cast<float> (kSr));
    const float k = 1.0f - std::exp (-duskaudio::kDuskTwoPi * hz / static_cast<float> (kSr));
    std::array<std::vector<float>, 2> out;
    for (int side = 0; side < 2; ++side)
    {
        std::vector<float> e (static_cast<size_t> (length), 0.0f);
        const auto& taps = f.direct (side);
        for (int j = 0; j < taps.count; ++j)
            if (taps.delay[static_cast<size_t> (j)] < length)
                e[static_cast<size_t> (taps.delay[static_cast<size_t> (j)])] += direct * taps.gain[static_cast<size_t> (j)];
        for (int r = side; r < c.reflCount; r += 2)
            if (f.reflectionDelay (r) < length)
                e[static_cast<size_t> (f.reflectionDelay (r))] += refl;
        float z = 0.0f;
        for (float& v : e)
        {
            z += k * (v - z);
            v = z;
        }
        out[static_cast<size_t> (side)] = e;
    }
    stage.process (out[0].data(), out[1].data(), length);
    return out;
}

// Taps from 7 to 47 ms on both sides, three reflections, nothing sent into the engine.
EarlyFieldConfig directField()
{
    EarlyFieldConfig c;
    c.enabled = true;
    c.startMs = 7.0f;
    c.windowMs = 40.0f;
    c.taps = 12;
    c.slopeDb = 4.0f;
    c.attackMs = 5.0f;
    c.inject = 0.0f;
    c.directDb = -6.0f;
    c.reflDb = -9.0f;
    c.reflCount = 3;
    c.reflMs = { 2.5f, 3.5f, 60.0f, 0.0f };
    c.lowpassHz = 9000.0f;
    return c;
}

// Energy (dB) of `x`'s left plus right, mid or side signal between lo and hi Hz: its DFT,
// zero-padded to a power of two.
enum class Part { Both, Mid, Side };

double bandDb (const Render& x, Part part, double lo, double hi)
{
    size_t n = 1;
    while (n < x.l.size()) n <<= 1;
    const auto spectrum = [n] (const std::vector<double>& v) {
        std::vector<std::complex<double>> a (n);
        std::copy (v.begin(), v.end(), a.begin());
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (a[i], a[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const std::complex<double> w = std::polar (1.0, -2.0 * kPi / static_cast<double> (len));
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<double> wk = 1.0;
                for (size_t k = 0; k < len / 2; ++k, wk *= w)
                {
                    const auto u = a[i + k], t = wk * a[i + k + len / 2];
                    a[i + k] = u + t;
                    a[i + k + len / 2] = u - t;
                }
            }
        }
        return a;
    };
    std::vector<std::vector<double>> signals;
    const size_t m = x.l.size();
    if (part == Part::Both)
    {
        signals.emplace_back (x.l.begin(), x.l.end());
        signals.emplace_back (x.r.begin(), x.r.end());
    }
    else
    {
        std::vector<double> v (m);
        for (size_t i = 0; i < m; ++i)
            v[i] = 0.5 * (static_cast<double> (x.l[i]) + (part == Part::Mid ? 1.0 : -1.0) * x.r[i]);
        signals.push_back (v);
    }
    double e = 0.0;
    for (const auto& v : signals)
    {
        const auto a = spectrum (v);
        for (size_t k = 0; k <= n / 2; ++k)
        {
            const double f = static_cast<double> (k) * kSr / static_cast<double> (n);
            if (f >= lo && f <= hi) e += std::norm (a[k]);
        }
    }
    return 10.0 * std::log10 (std::max (e, 1e-300));
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

// Every preset without a row renders exactly as the field-off path does: here with the hooks
// compiled in or out as this build was configured, and with DUSKVERB2_EARLYFIELD unset.
void testPresetsWithoutAFieldAreUnchanged()
{
    setEnv ("DUSKVERB2_EARLYFIELD", nullptr);
    int compared = 0;
    for (const auto& preset : duskverb::dv2FactoryPresets())
    {
        if (duskverb::earlyFieldFor (preset.name).enabled)
            continue;
        const Render plain = render (preset, nullptr, 36000, impulseThenNoise);
        checkf (plain == render (preset, &kOff, 36000, impulseThenNoise), "'%s' renders differently from the field-off path", preset.name);
        ++compared;
    }
    std::printf ("EarlyField DSP: %d presets without a field render as the field-off path (tuning hooks %s)\n",
                 compared, DUSKVERB2_TUNING_HOOKS ? "compiled in" : "compiled out");
}

void testTheTuningHook()
{
    const FactoryPreset& preset = presetNamed ("Large Chamber");
    const char* text = "enabled=1,start_ms=7,window_ms=40,taps=12,slope_db=4,attack_ms=5,inject=0.3,"
                       "direct_db=-6,refl_db=-9,refl_count=3,refl_ms=2.5/3.5/60/0,lowpass_hz=9000";
    EarlyFieldConfig parsed;
    checkf (duskverb::parseEarlyFieldConfig (text, parsed), "the test's hook text is refused");
    setEnv ("DUSKVERB2_EARLYFIELD", text);
    const Render hooked = render (preset, nullptr, 9600);
    setEnv ("DUSKVERB2_EARLYFIELD", "enabled=1,taps=0");
    const Render refused = render (preset, nullptr, 9600);
    setEnv ("DUSKVERB2_EARLYFIELD", nullptr);
    const Render own = render (preset, nullptr, 9600);           // the preset's own row, if it has one
    const Render off = render (preset, &kOff, 9600);
    const Render given = render (preset, &parsed, 9600);
    checkf (given != off, "the hook's config changes nothing, so this test cannot see it");
    checkf (refused == own, "a refused DUSKVERB2_EARLYFIELD changes the render");
#if DUSKVERB2_TUNING_HOOKS
    checkf (hooked == given, "DUSKVERB2_EARLYFIELD does not render as the same config given directly");
#else
    checkf (hooked == own, "a build without DUSKVERB2_TUNING_HOOKS reads DUSKVERB2_EARLYFIELD");
#endif
}

// With nothing sent into the engine, the field's whole effect on the output is its own answer,
// through the preset's output controls.
void testTheFieldReachesTheOutput()
{
    const FactoryPreset& preset = presetNamed ("Large Chamber");
    const EarlyFieldConfig c = directField();
    const int n = 9600;
    const Render on = render (preset, &c, n), off = render (preset, &kOff, n);
    const auto e = predict (c, n, stageFor (preset));
    double worst = 0.0, peakE = 0.0;
    for (int side = 0; side < 2; ++side)
        for (int t = 0; t < n; ++t)
        {
            const size_t i = static_cast<size_t> (t);
            const double diff = static_cast<double> (on.side (side)[i]) - off.side (side)[i];
            worst = std::max (worst, std::abs (diff - e[static_cast<size_t> (side)][i]));
            peakE = std::max (peakE, static_cast<double> (std::abs (e[static_cast<size_t> (side)][i])));
        }
    std::printf ("EarlyField DSP: output - field-off = the layout's answer through the output stage to within %.3g (its peak %.3g)\n",
                 worst, peakE);
    checkf (peakE > 0.1, "the predicted field is too quiet to test (peak %g)", peakE);
    checkf (worst <= 1e-6, "the field's effect on the output is not its layout's answer through the output stage (worst %g)", worst);

    // Sent into the engine instead (both of its own paths off), it changes the tail.
    EarlyFieldConfig sent = c;
    sent.inject = 1.0f;
    sent.directDb = sent.reflDb = EarlyField::kOffDb;
    const Render injected = render (preset, &sent, n);
    double moved = 0.0, peak = 0.0;
    for (int t = 0; t < n; ++t)
    {
        const size_t i = static_cast<size_t> (t);
        moved = std::max (moved, static_cast<double> (std::abs (injected.l[i] - off.l[i])));
        peak = std::max ({ peak, static_cast<double> (std::abs (injected.l[i])), static_cast<double> (std::abs (injected.r[i])) });
        checkf (std::isfinite (injected.l[i]) && std::isfinite (injected.r[i]), "the injected render is not finite at %d", t);
    }
    checkf (moved > 1e-3 && peak < 100.0, "inject 1 does not reach the engine (largest change %g, peak %g)", moved, peak);
}

// Each output control, moved once the preset is in (as a user turns it), moves the field's
// contribution as it moves the reverb's wet. Lo Cut takes the lows, Hi Cut, its shelf and the
// Tone macro the highs, Mono Below the side signal under its corner, Width the side signal, Gain
// Trim everything by its dB; each leaves the rest alone. Where the engine's wet should move the
// same way, the field's change is the engine's to within 1 dB, in a band narrow enough that the
// control's gain barely varies across it (then the change is that gain, whatever the spectrum).
void testTheOutputControlsMoveTheField()
{
    const FactoryPreset& preset = presetNamed ("Large Chamber");
    const EarlyFieldConfig c = directField();
    const int at = 14400, length = 9600;     // the impulse 0.3 s after the move: every glide is over
    const auto source = [] (int t, int) { return t == 14400 ? 1.0f : 0.0f; };
    struct Contribution { std::vector<Knob> knobs; Render field, engine; };
    std::vector<Contribution> measured;      // one render pair per setting, however many bands read it
    const auto measure = [&] (const std::vector<Knob>& knobs) {
        for (const Contribution& m : measured)
            if (std::equal (m.knobs.begin(), m.knobs.end(), knobs.begin(), knobs.end(),
                            [] (const Knob& a, const Knob& b) { return a.index == b.index && a.value == b.value; }))
                return m;
        const auto move = [&knobs] (DuskVerbDSP& dsp, int block) {
            if (block == 0)
                for (const Knob& k : knobs)
                    setPlain (dsp, k.index, k.value);
        };
        const Render on = render (preset, &c, at + length, source, move), off = render (preset, &kOff, at + length, source, move);
        measured.push_back ({ knobs, difference (on, off, at, length), slice (off, at, length) });
        return measured.back();
    };
    struct Case
    {
        const char* name;
        std::vector<Knob> base, moved;
        Part part;
        double lo, hi;
        double minChange, maxChange;      // the field's change in the band, dB
        bool likeTheEngine;               // and the engine's, to within 1 dB
    };
    using namespace duskverb;
    const std::vector<Knob> hiCut20k { { HiCut, 20000.0f }, { HiCutShelfDb, -12.0f } }, hiCut2k { { HiCut, 2000.0f }, { HiCutShelfDb, -12.0f } };
    const std::vector<Knob> mono20 { { MonoBelow, 20.0f }, { MonoBelowDepth, 1.0f } }, mono300 { { MonoBelow, 300.0f }, { MonoBelowDepth, 1.0f } };
    const std::vector<Knob> tone0 { { HiCut, 8000.0f }, { HiCutShelfDb, -12.0f }, { Tone, 0.0f } };
    const Case cases[] = {
        { "Gain Trim 0 -> +6 dB",      { { GainTrim, 0.0f } }, { { GainTrim, 6.0f } }, Part::Both, 0.0, 24000.0, 5.95, 6.05, true },
        { "Lo Cut 20 -> 300 Hz",       { { LoCut, 20.0f } }, { { LoCut, 300.0f } }, Part::Both, 20.0, 150.0, -60.0, -10.0, false },
        { "Lo Cut, at its corner",     { { LoCut, 20.0f } }, { { LoCut, 300.0f } }, Part::Both, 290.0, 310.0, -4.0, -2.0, true },
        { "Lo Cut, its highs",         { { LoCut, 20.0f } }, { { LoCut, 300.0f } }, Part::Both, 3000.0, 20000.0, -0.5, 0.5, true },
        { "Hi Cut 20 -> 2 kHz",        hiCut20k, hiCut2k, Part::Both, 6000.0, 20000.0, -13.0, -8.0, false },
        { "Hi Cut, an octave above",   hiCut20k, hiCut2k, Part::Both, 3900.0, 4100.0, -12.0, -6.0, true },
        { "Hi Cut, its lows",          hiCut20k, hiCut2k, Part::Both, 20.0, 500.0, -0.5, 0.5, true },
        { "Hi Cut shelf -1 -> -24 dB", { { HiCut, 3000.0f }, { HiCutShelfDb, -1.0f } }, { { HiCut, 3000.0f }, { HiCutShelfDb, -24.0f } },
          Part::Both, 10000.0, 11000.0, -24.0, -15.0, true },
        { "Mono Below 20 -> 300 Hz, side", mono20, mono300, Part::Side, 20.0, 100.0, -40.0, -8.0, false },
        { "Mono Below, side at 60 Hz", mono20, mono300, Part::Side, 55.0, 65.0, -20.0, -10.0, true },
        { "Mono Below, side highs",    mono20, mono300, Part::Side, 3000.0, 20000.0, -0.5, 0.5, true },
        { "Mono Below, mid",           mono20, mono300, Part::Mid, 20.0, 20000.0, -0.05, 0.05, true },
        { "Width 1 -> 2, side",        { { Width, 1.0f } }, { { Width, 2.0f } }, Part::Side, 0.0, 24000.0, 5.97, 6.07, true },
        { "Width 1 -> 2, mid",         { { Width, 1.0f } }, { { Width, 2.0f } }, Part::Mid, 0.0, 24000.0, -0.05, 0.05, true },
        { "Width 1 -> 0, side",        { { Width, 1.0f } }, { { Width, 0.0f } }, Part::Side, 0.0, 24000.0, -1.0e9, -100.0, false },
        // Tone also scales the engine's damping, so only the field's change is checked.
        { "Tone 0 -> -1",              tone0, { { HiCut, 8000.0f }, { HiCutShelfDb, -12.0f }, { Tone, -1.0f } },
          Part::Both, 5500.0, 8000.0, -12.0, -2.0, false },
        { "Tone 0 -> +1",              tone0, { { HiCut, 8000.0f }, { HiCutShelfDb, -12.0f }, { Tone, 1.0f } },
          Part::Both, 9000.0, 13000.0, 2.0, 12.0, false },
    };
    for (const Case& k : cases)
    {
        const Contribution base = measure (k.base), moved = measure (k.moved);
        const double field = bandDb (moved.field, k.part, k.lo, k.hi) - bandDb (base.field, k.part, k.lo, k.hi);
        const double engine = bandDb (moved.engine, k.part, k.lo, k.hi) - bandDb (base.engine, k.part, k.lo, k.hi);
        std::printf ("EarlyField DSP: %-30s field %+8.2f dB, engine %+8.2f dB (%g-%g Hz)\n", k.name, field, engine, k.lo, k.hi);
        checkf (field >= k.minChange && field <= k.maxChange, "%s moves the field %+.2f dB, want %+.2f..%+.2f", k.name, field,
                k.minChange, k.maxChange);
        if (k.likeTheEngine)
            checkf (std::abs (field - engine) <= 1.0, "%s moves the field %+.2f dB but the engine %+.2f dB", k.name, field, engine);
    }
}

void testOwnEnginesNeverPlayIt()
{
    EarlyFieldConfig loud = directField();
    loud.directDb = 0.0f;
    loud.inject = 0.5f;
    for (const auto& preset : duskverb::ownFactoryPresets())
        checkf (render (preset, &loud, 36000, impulseThenNoise) == render (preset, &kOff, 36000, impulseThenNoise),
                "'%s', on an own engine, plays the early field", preset.name);
}

// Freeze on under a steady noise: the field takes no new input, so its contribution drains once
// its longest tap has passed (its output stage rings out after, the Lo Cut longest), and a burst
// played while frozen never reaches it.
void testFreezeStopsTheField()
{
    const FactoryPreset& preset = presetNamed ("Large Chamber");
    const EarlyFieldConfig c = directField();                       // the longest arrival at 60 ms
    const int n = 48000, frozenFrom = 9 * kBlock, longest = 2880;
    const auto freeze = [] (DuskVerbDSP& dsp, int block) { if (block == 8) setPlain (dsp, duskverb::Freeze, 1.0f); };
    const auto noise = [] (int t, int ch) { return noiseAt (t, ch); };
    const auto noiseAndBurst = [] (int t, int ch) { return noiseAt (t, ch) + (t >= 12000 && t < 12100 ? 0.5f : 0.0f); };
    const Render d = difference (render (preset, &c, n, noise, freeze), render (preset, &kOff, n, noise, freeze));
    const Render withBurst = difference (render (preset, &c, n, noiseAndBurst, freeze), render (preset, &kOff, n, noiseAndBurst, freeze));
    const auto peak = [] (const Render& x, int from, int to) {
        double v = 0.0;
        for (int s = 0; s < 2; ++s)
            for (int t = from; t < to; ++t)
                v = std::max (v, static_cast<double> (std::abs (x.side (s)[static_cast<size_t> (t)])));
        return v;
    };
    const double playing = peak (d, 0, frozenFrom);
    const double drained = peak (d, frozenFrom + longest + 4800, n);       // the window, then 100 ms
    const double silent = peak (d, n - 9600, n);
    double burst = 0.0;
    for (int s = 0; s < 2; ++s)
        for (int t = 0; t < n; ++t)
            burst = std::max (burst, std::abs (static_cast<double> (withBurst.side (s)[static_cast<size_t> (t)]) - d.side (s)[static_cast<size_t> (t)]));
    std::printf ("EarlyField DSP: frozen, the field drains from %.3g to %.3g (%.1f dB) 100 ms after its window, %.3g in the last 0.2 s;"
                 " a burst while frozen moves it %.3g\n", playing, drained, 20.0 * std::log10 (std::max (drained, 1e-300) / playing),
                 silent, burst);
    checkf (playing > 0.05, "the field is too quiet to test (peak %g)", playing);
    checkf (drained <= 1e-6 * playing, "frozen, the field still sounds %g 100 ms after its window (it played %g)", drained, playing);
    checkf (silent == 0.0, "frozen, the field has not gone silent (%g) by the end", silent);
    checkf (burst <= 1e-6, "a burst while frozen reaches the field (%g)", burst);
}

// Freeze on, then off again, under a sustained 440 Hz tone (-13.3 dBFS), on the two presets whose
// engines freeze cleanly: their own rows, with nothing sent into the engine so the field can be
// told apart. The field's input fades rather than steps, so what the field puts above 4 kHz in
// the 150 ms after each switch stays 10 dB under what the engine alone puts there. In one step,
// every tap would cut the tone off, or back in, in turn.
void testFreezeFadesTheFieldsInput()
{
    for (const char* name : { "Medium Drum Room", "Vocal Hall" })
    {
        const FactoryPreset& preset = presetNamed (name);
        EarlyFieldConfig c = duskverb::earlyFieldFor (preset.name);
        c.inject = 0.0f;
        const int n = 72000, frozenFrom = 94 * kBlock, thawedFrom = 188 * kBlock;
        const auto toggle = [] (DuskVerbDSP& dsp, int block) {
            if (block == 93) setPlain (dsp, duskverb::Freeze, 1.0f);
            if (block == 187) setPlain (dsp, duskverb::Freeze, 0.0f);
        };
        const Render engine = render (preset, &kOff, n, tone440, toggle);
        const Render field = difference (render (preset, &c, n, tone440, toggle), engine);
        const double level = rmsDb (field, frozenFrom - 4800, 4800);
        checkf (level > -60.0, "%s: the field is too quiet to test (%.1f dBFS)", name, level);
        for (const int at : { frozenFrom, thawedFrom })
        {
            const char* what = at == frozenFrom ? "on" : "off";
            const double fieldDb = highBandDb (field, at, 7200), engineDb = highBandDb (engine, at, 7200);
            std::printf ("EarlyField DSP: %s, Freeze %s: the field (%.1f dBFS) puts %.1f dBFS above 4 kHz in the next 150 ms,"
                         " the engine %.1f\n", name, what, level, fieldDb, engineDb);
            checkf (fieldDb <= engineDb - 10.0, "%s: Freeze %s steps the field's input: %.1f dBFS above 4 kHz, the engine %.1f",
                    name, what, fieldDb, engineDb);
        }
    }
}

// A preset-less session (restorePresetIdentity (nullptr)) right after the impulse: the swap fades
// the outgoing engine's field out linearly, still through that engine's controls, and the neutral
// engine has none, not even for a second impulse once the fade is over.
void testAnEngineFadingOutKeepsItsField()
{
    const FactoryPreset& preset = presetNamed ("Large Chamber");
    EarlyFieldConfig c = directField();
    c.startMs = 1.0f;
    c.windowMs = 99.0f;
    c.taps = 40;
    c.reflCount = 0;
    const auto neutral = [] (DuskVerbDSP& dsp, int block) { if (block == 0) dsp.restorePresetIdentity (nullptr); };
    const int n = 9600;
    const auto twoImpulses = [] (int t, int) { return t == 0 || t == 3600 ? 1.0f : 0.0f; };
    const Render on = render (preset, &c, n, twoImpulses, neutral), off = render (preset, &kOff, n, twoImpulses, neutral);
    const auto e = predict (c, n, stageFor (preset));
    const int swap = kBlock, fadeEnd = kBlock + kFade;     // the swap runs at the top of block 1
    double heardInFade = 0.0;
    for (int side = 0; side < 2; ++side)
        for (int t = 0; t < n; ++t)
        {
            const size_t i = static_cast<size_t> (t);
            const float a = on.side (side)[i], b = off.side (side)[i];
            const double diff = static_cast<double> (a) - b, want = e[static_cast<size_t> (side)][i];
            if (t < swap)
                checkf (std::abs (diff - want) <= 1e-6, "side %d, %d samples, before the swap: %g, want %g", side, t, diff, want);
            else if (t < fadeEnd)
            {
                const double g = 1.0 - std::min (1.0, static_cast<double> (t - swap) / (kFade - 1));
                checkf (std::abs (diff - g * want) <= 1e-6,
                        "side %d, %d samples, in the fade: %g, want %g (the field's own, times %.4f)", side, t, diff, g * want, g);
                heardInFade = std::max (heardInFade, std::abs (g * want));
            }
            else
                checkf (std::memcmp (&a, &b, sizeof (float)) == 0,
                        "side %d, %d samples, after the fade: %g, but the neutral engine has no field", side, t, diff);
        }
    checkf (heardInFade > 0.05, "the outgoing engine's field stops at the swap (%g heard in the fade)", heardInFade);
}

// The same preset loaded twice mid-stream, the second load bringing back the engine the first
// faded out: the field's contribution is the one it makes without the reloads, to the rounding of
// the engine's sum. The fields share their input and the incoming one carries on from the
// outgoing one's state, so their linear crossfade changes nothing; the engines' own swap is
// unchanged by the field (its pre path is off here).
void testAReloadLeavesTheFieldAlone()
{
    const FactoryPreset& preset = presetNamed ("Large Chamber");
    EarlyFieldConfig c = directField();
    c.startMs = 1.0f;
    c.windowMs = 99.0f;
    c.taps = 40;
    const auto reload = [&preset] (DuskVerbDSP& dsp, int block) {
        if (block == 0 || block == 12)
            dsp.applyFactoryPresetConfig (preset);          // swaps at the top of blocks 1 and 13
    };
    const int n = 9600;
    const Render moved = difference (render (preset, &c, n, impulse, reload), render (preset, &kOff, n, impulse, reload));
    const Render still = difference (render (preset, &c, n), render (preset, &kOff, n));
    double worst = 0.0, peak = 0.0;
    for (int s = 0; s < 2; ++s)
        for (size_t i = 0; i < static_cast<size_t> (n); ++i)
        {
            worst = std::max (worst, std::abs (static_cast<double> (moved.side (s)[i]) - still.side (s)[i]));
            peak = std::max (peak, static_cast<double> (std::abs (still.side (s)[i])));
        }
    checkf (peak > 0.05, "the field is too quiet to test (peak %g)", peak);
    checkf (worst <= 1e-6, "two reloads of the same preset move the field's contribution by %g (its peak %g)", worst, peak);
}

// The same under a sustained 440 Hz tone (-13.3 dBFS), with the measures stated: what the
// reload changes in the field's contribution above 4 kHz in the 150 ms after it, and the field's
// level in each 5 ms of the fade, against the same render without the reload.
void testAReloadUnderASustainedToneIsClean()
{
    const FactoryPreset& preset = presetNamed ("Large Chamber");
    EarlyFieldConfig c = directField();
    c.directDb = -3.0f;
    const int n = 24000, swap = 40 * kBlock;
    const auto reload = [&preset] (DuskVerbDSP& dsp, int block) { if (block == 39) dsp.applyFactoryPresetConfig (preset); };
    const Render moved = difference (render (preset, &c, n, tone440, reload), render (preset, &kOff, n, tone440, reload));
    const Render still = difference (render (preset, &c, n, tone440), render (preset, &kOff, n, tone440));
    const double level = rmsDb (still, swap - 4800, 4800);
    const double clicks = highBandDb (difference (moved, still), swap, 7200);
    double swell = 0.0, worst = 0.0;
    for (int w = swap; w < swap + kFade; w += 240)
        swell = std::max (swell, std::abs (rmsDb (moved, w, 240) - rmsDb (still, w, 240)));
    for (int s = 0; s < 2; ++s)
        for (size_t i = 0; i < static_cast<size_t> (n); ++i)
            worst = std::max (worst, std::abs (static_cast<double> (moved.side (s)[i]) - still.side (s)[i]));
    std::printf ("EarlyField DSP: a reload under a 440 Hz tone: the field (%.1f dBFS) changes by %.1f dBFS above 4 kHz in the"
                 " next 150 ms, its level by at most %.4f dB in any 5 ms of the fade, any sample by %.3g\n",
                 level, clicks, swell, worst);
    checkf (level > -40.0, "the field is too quiet to test (%.1f dBFS)", level);
    checkf (clicks < -120.0, "a reload puts %.1f dBFS above 4 kHz into the field", clicks);
    checkf (swell < 0.01, "a reload moves the field's level by %.4f dB mid-fade", swell);
    checkf (worst <= 1e-6, "a reload moves the field's contribution by %g", worst);
}

// From one preset to another, the outgoing field keeps its own controls, the incoming one takes
// the new preset's and carries on from the outgoing one's state, and the two are crossfaded
// linearly over the engines' fade (the same t), apart from the engines: a model built from the
// module's own classes, fed the same input, predicts the field's contribution exactly.
void testASwapToAnotherPresetCrossfadesLinearly()
{
    const FactoryPreset& from = presetNamed ("Large Chamber");
    const FactoryPreset& to = presetNamed ("Medium Drum Room");       // every output control differs
    const EarlyFieldConfig c = directField();
    const int n = 19200, swapBlock = 20;
    const auto noise = [] (int t, int ch) { return noiseAt (t, ch); };
    const auto load = [&to, swapBlock] (DuskVerbDSP& dsp, int block) {
        if (block == swapBlock - 1)
        {
            setPreset (dsp, to);
            dsp.applyFactoryPresetConfig (to);
        }
    };
    const Render got = difference (render (from, &c, n, noise, load), render (from, &kOff, n, noise, load));

    EarlyFieldInput input;
    input.prepare (kSr, kBlock);
    EarlyField outgoing, incoming;
    for (EarlyField* f : { &outgoing, &incoming })
    {
        f->prepare (input);
        f->configure (c);
    }
    Dv2OutputShell outStage = stageFor (from), inStage = stageFor (to);
    Render linear { std::vector<float> (static_cast<size_t> (n)), std::vector<float> (static_cast<size_t> (n)) };
    Render equalPower = linear;
    std::vector<float> l (kBlock), r (kBlock), ol (kBlock), orr (kBlock), il (kBlock), ir (kBlock);
    const float invSpan = 1.0f / static_cast<float> (kFade - 1);          // the engines' t, as DuskVerbDSP forms it
    for (int block = 0; block * kBlock < n; ++block)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            l[static_cast<size_t> (i)] = noiseAt (block * kBlock + i, 0);
            r[static_cast<size_t> (i)] = noiseAt (block * kBlock + i, 1);
        }
        input.write (l.data(), r.data(), kBlock, false);
        if (block == swapBlock)
        {
            incoming.copyStateFrom (outgoing);
            inStage.copyStateFrom (outStage);
        }
        if (block < swapBlock + kFade / kBlock + 1)
        {
            outgoing.renderPost (input, ol.data(), orr.data(), kBlock);
            outStage.process (ol.data(), orr.data(), kBlock);
        }
        if (block >= swapBlock)
        {
            incoming.renderPost (input, il.data(), ir.data(), kBlock);
            inStage.process (il.data(), ir.data(), kBlock);
        }
        for (int i = 0; i < kBlock; ++i)
        {
            const size_t k = static_cast<size_t> (i), at = static_cast<size_t> (block * kBlock + i);
            const int index = (block - swapBlock) * kBlock + i;
            if (block < swapBlock)
            {
                linear.l[at] = equalPower.l[at] = ol[k];
                linear.r[at] = equalPower.r[at] = orr[k];
            }
            else if (index < kFade)
            {
                const float t = std::min (1.0f, static_cast<float> (index) * invSpan);
                linear.l[at] = ol[k] + t * (il[k] - ol[k]);
                linear.r[at] = orr[k] + t * (ir[k] - orr[k]);
                const float gNew = std::sin (t * 1.5707963f), gOld = std::cos (t * 1.5707963f);
                equalPower.l[at] = gOld * ol[k] + gNew * il[k];
                equalPower.r[at] = gOld * orr[k] + gNew * ir[k];
            }
            else
            {
                linear.l[at] = equalPower.l[at] = il[k];
                linear.r[at] = equalPower.r[at] = ir[k];
            }
        }
    }
    double worst = 0.0, worstEqualPower = 0.0, peak = 0.0;
    for (int s = 0; s < 2; ++s)
        for (size_t i = 0; i < static_cast<size_t> (n); ++i)
        {
            worst = std::max (worst, std::abs (static_cast<double> (got.side (s)[i]) - linear.side (s)[i]));
            worstEqualPower = std::max (worstEqualPower, std::abs (static_cast<double> (got.side (s)[i]) - equalPower.side (s)[i]));
            peak = std::max (peak, static_cast<double> (std::abs (linear.side (s)[i])));
        }
    std::printf ("EarlyField DSP: %s -> %s: the field is the linear model's to within %.3g (an equal-power fade would be %.3g off;"
                 " peak %.3g)\n", from.name, to.name, worst, worstEqualPower, peak);
    checkf (peak > 0.05, "the field is too quiet to test (peak %g)", peak);
    checkf (worstEqualPower > 1e-2, "the two fields are too alike for the test to tell a linear fade (%g)", worstEqualPower);
    checkf (worst <= 1e-6, "a swap to another preset is not the linear crossfade of the two fields (worst %g)", worst);
}

// A host reset right after the impulse (every tap still to come): none of them plays.
void testAHostResetSilencesIt()
{
    const FactoryPreset& preset = presetNamed ("Large Chamber");
    EarlyFieldConfig c = directField();
    c.startMs = 20.0f;
    c.reflMs = { 30.0f, 40.0f, 60.0f, 0.0f };
    const auto reset = [] (DuskVerbDSP& dsp, int block) { if (block == 0) dsp.clearState(); };
    checkf (render (preset, &c, 9600, impulse, reset) == render (preset, &kOff, 9600, impulse, reset),
            "the field still plays after clearState()");
}
} // namespace

int main()
{
    testPresetsWithoutAFieldAreUnchanged();
    testTheTuningHook();
    testTheFieldReachesTheOutput();
    testTheOutputControlsMoveTheField();
    testOwnEnginesNeverPlayIt();
    testFreezeStopsTheField();
    testFreezeFadesTheFieldsInput();
    testAnEngineFadingOutKeepsItsField();
    testAReloadLeavesTheFieldAlone();
    testAReloadUnderASustainedToneIsClean();
    testASwapToAnotherPresetCrossfadesLinearly();
    testAHostResetSilencesIt();
    if (failures != 0)
    {
        std::printf ("%d EarlyField DSP check(s) failed\n", failures);
        return 1;
    }
    std::printf ("EarlyField DSP: all checks passed\n");
    return 0;
}
