// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// DuskVerb 2's recall of its factory presets (DuskVerbPresets.hpp collectDv2Parameters): the
// legacy row's values, with Decay's reading (kDecayShownByName) and DuskVerb 2's own values
// (kPresetOverrides) over them, and nothing else changed.
//
// What Decay reads: a calibrated preset hands the host its measured RT60 in place of its row's
// decay. Recalled as a host recalls it, its engines are still given the row's decay, bit for bit,
// so it sounds as before, and Decay moves them in proportion from there. On any other engine, on
// a preset without a reading and in a preset-less session the engines are given Decay itself.

#include "DuskVerbDSP.hpp"
#include "DuskVerbDv2Voicing.hpp"
#include "DuskVerbPresetEngineConfig.h"
#include "DuskVerbParamTable.hpp"
#include "DuskVerbPresets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

using namespace duskverb;

constexpr double kSr = 48000.0;
constexpr int kBlock = 256;
constexpr int kSettleBlocks = 12;
// Medium Drum Room's 1-3 kHz tail at line 5's allpass delays: +0.347 / +0.179 at the tank's own
// 0.55, +0.084 / -0.008 with its patch (measured 2026-10-26).
constexpr double kDensifiedRepeat = 0.15;      // past the 50 ms preset fade, so the next swap may start

bool sameBits (float a, float b) noexcept { return std::memcmp (&a, &b, sizeof (float)) == 0; }

void setPlain (DuskVerbDSP& dsp, int index, float plain)
{
    dsp.setParameter (index, dspFromPlain (paramDesc (index), plain));
}

void settle (DuskVerbDSP& dsp)
{
    std::vector<float> l (kBlock), r (kBlock);
    for (int b = 0; b < kSettleBlocks; ++b)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        const float* in[] = { l.data(), r.data() };
        float* out[] = { l.data(), r.data() };
        dsp.processBlock (in, out, 2, kBlock);
    }
}

// `preset` recalled onto `dsp` as a host recalls it: its values at 100 % wet, then the swap.
void recallOnto (DuskVerbDSP& dsp, const FactoryPreset& preset)
{
    collectDv2Parameters (preset, [&] (const char* id, float v) {
        const int i = paramIndexForId (id);
        if (i >= 0) setPlain (dsp, i, v);
    });
    setPlain (dsp, Mix, 1.0f);
    dsp.applyFactoryPresetConfig (preset);
    settle (dsp);
}

std::unique_ptr<DuskVerbDSP> recalled (const FactoryPreset& preset)
{
    auto dsp = std::make_unique<DuskVerbDSP>();
    dsp->prepare (kSr, kBlock);
    recallOnto (*dsp, preset);
    return dsp;
}

// Half a second of `dsp`'s output (left, then right) after 50 ms of a fixed noise burst.
std::vector<float> burstResponse (DuskVerbDSP& dsp)
{
    constexpr int kBurst = 2400, kLength = 24064;
    std::vector<float> l (kBlock), r (kBlock), out;
    uint32_t seed = 12345u;
    for (int start = 0; start < kLength; start += kBlock)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            const float noise = static_cast<float> (seed >> 8) / 8388608.0f - 1.0f;
            l[static_cast<size_t> (i)] = r[static_cast<size_t> (i)] = start + i < kBurst ? 0.5f * noise : 0.0f;
        }
        const float* in[] = { l.data(), r.data() };
        float* outs[] = { l.data(), r.data() };
        dsp.processBlock (in, outs, 2, kBlock);
        out.insert (out.end(), l.begin(), l.end());
        out.insert (out.end(), r.begin(), r.end());
    }
    return out;
}

const FactoryPreset* presetNamed (std::string_view name)
{
    for (const auto& preset : dv2FactoryPresets())
        if (name == preset.name)
            return &preset;
    return nullptr;
}

// Every row names a shared factory preset whose Decay is an exponential tail, and reads a time
// the parameter can hold.
void testTable()
{
    const ParamDesc& decay = paramDesc (Decay);
    for (const auto& [name, shown] : kDecayShownByName)
    {
        const std::string n (name);
        const FactoryPreset* preset = presetNamed (name);
        checkf (preset != nullptr, "'%s' is a factory preset", n.c_str());
        if (preset == nullptr)
            continue;
        checkf (shown >= decay.min && shown <= decay.max, "'%s' reads %g s, inside Decay's range", n.c_str(), shown);
        const EngineType type = controlEngine (preset->algorithm);
        checkf (! isOwnAlgorithm (preset->algorithm) && type != EngineType::NonLinear && type != EngineType::ReverseRoom,
                "'%s' is not on the Gated, Reverse or Nonlinear engine", n.c_str());
    }
}

// Every override names a shared factory preset and a parameter its row sets, with a value the
// parameter can hold.
void testOverrides()
{
    for (const auto& row : kPresetOverrides)
    {
        const std::string preset (row.preset), id (row.id);
        const FactoryPreset* p = presetNamed (row.preset);
        checkf (p != nullptr, "override '%s'/'%s' names a factory preset", preset.c_str(), id.c_str());
        const int index = paramIndexForId (row.id);
        checkf (index >= 0, "override '%s'/'%s' names a parameter", preset.c_str(), id.c_str());
        if (p == nullptr || index < 0)
            continue;
        bool emitted = false;
        p->collectParameters ([&] (const char* rowId, float) { emitted = emitted || row.id == rowId; });
        checkf (emitted, "'%s' row sets '%s', so the override has something to replace", preset.c_str(), id.c_str());
        const ParamDesc& d = paramDesc (index);
        checkf (row.value >= d.min && row.value <= d.max, "'%s' %s = %g is inside its range", preset.c_str(), id.c_str(), row.value);
    }
}

// The plain value DuskVerb 2 recalls for `id` on `preset`, given the row's.
float recallValue (const FactoryPreset& preset, const std::string& id, float rowValue)
{
    if (id == paramDesc (Decay).id)
        if (const float* shown = decayShownFor (preset.name))
            return *shown;
    for (const auto& row : kPresetOverrides)
        if (row.preset == preset.name && row.id == id)
            return row.value;
    return rowValue;
}

// The DuskVerb 2 recall is the row's, value for value and in the row's order, but for Decay on a
// calibrated preset (its reading) and the overridden parameters.
void testEmission()
{
    for (const auto& preset : dv2FactoryPresets())
    {
        std::vector<std::pair<std::string, float>> row, dv2;
        preset.collectParameters ([&] (const char* id, float v) { row.emplace_back (id, v); });
        collectDv2Parameters (preset, [&] (const char* id, float v) { dv2.emplace_back (id, v); });
        checkf (row.size() == dv2.size(), "'%s' emits as many values as its row", preset.name);
        int decays = 0;
        for (size_t i = 0; i < std::min (row.size(), dv2.size()); ++i)
        {
            checkf (row[i].first == dv2[i].first, "'%s' value %zu is '%s' as in its row", preset.name, i, row[i].first.c_str());
            const bool decay = row[i].first == paramDesc (Decay).id;
            decays += decay ? 1 : 0;
            const float want = recallValue (preset, row[i].first, row[i].second);
            checkf (sameBits (dv2[i].second, want), "'%s' %s is %g (got %g)", preset.name,
                    row[i].first.c_str(), want, dv2[i].second);
        }
        checkf (decays == 1, "'%s' emits Decay once", preset.name);
    }
}

// Recalled, a calibrated preset hands its engines the row's decay bit for bit, and Decay moves
// them in proportion from there; off its own engine, or without a preset, they get Decay itself.
void testEngineDecay()
{
    for (const auto& [name, shown] : kDecayShownByName)
    {
        const FactoryPreset* preset = presetNamed (name);
        if (preset == nullptr)
            continue;                  // reported by testTable
        const auto dsp = recalled (*preset);
        const float row = dspFromPlain (paramDesc (Decay), preset->decay);
        checkf (sameBits (dsp->engineDecay(), row), "%s: at its reading the engines get the row's %.9g s (got %.9g)",
                preset->name, row, dsp->engineDecay());

        for (const float factor : { 0.5f, 2.0f })
        {
            setPlain (*dsp, Decay, shown * factor);
            const double want = static_cast<double> (row) * dsp->getParameter (Decay) / dspFromPlain (paramDesc (Decay), shown);
            checkf (std::abs (dsp->engineDecay() / want - 1.0) < 1.0e-6, "%s: Decay x%g gives the engines %.6g s (want %.6g)",
                    preset->name, factor, dsp->engineDecay(), want);
        }

        // Another engine hears Decay as it reads; back on its own engine the calibration returns.
        int other = 0;
        while (resolveAlgorithm (other) == resolveAlgorithm (preset->algorithm) || isOwnAlgorithm (other))
            ++other;
        setPlain (*dsp, Decay, shown);
        setPlain (*dsp, Algorithm, static_cast<float> (other));
        settle (*dsp);
        checkf (sameBits (dsp->engineDecay(), dsp->getParameter (Decay)), "%s: on engine %d the engines get Decay itself",
                preset->name, other);
        setPlain (*dsp, Algorithm, static_cast<float> (preset->algorithm));
        settle (*dsp);
        checkf (sameBits (dsp->engineDecay(), row), "%s: back on its engine, the row's decay again", preset->name);

        // A session without a preset identity keeps no calibration.
        dsp->restorePresetIdentity (nullptr);
        settle (*dsp);
        checkf (sameBits (dsp->engineDecay(), dsp->getParameter (Decay)), "%s: a preset-less session gets Decay itself",
                preset->name);
    }

    // A preset without a reading: Decay itself, at its own value and away from it.
    for (const auto& preset : dv2FactoryPresets())
    {
        if (decayShownFor (preset.name) != nullptr)
            continue;
        const auto dsp = recalled (preset);
        checkf (sameBits (dsp->engineDecay(), dsp->getParameter (Decay)), "%s: uncalibrated, the engines get Decay", preset.name);
        setPlain (*dsp, Decay, preset.decay * 2.0f);
        checkf (sameBits (dsp->engineDecay(), dsp->getParameter (Decay)), "%s: uncalibrated, Decay x2 reaches the engines as is",
                preset.name);
    }
}

// The swap gives the new engine its decay through the calibration, as a later Decay edge does:
// a recall renders as the same recall whose Decay was moved away and back before the audio. The
// two match to float noise on every preset but Large Chamber, whose engine keeps a trace of the
// excursion near -79 dB (measured 2026-10-25); a decay given wrong at the swap differs by far
// more than the -60 dB allowed.
void testSwapGivesCalibratedDecay()
{
    for (const auto& [name, shown] : kDecayShownByName)
    {
        const FactoryPreset* preset = presetNamed (name);
        if (preset == nullptr)
            continue;
        const auto direct = recalled (*preset);
        const auto moved = recalled (*preset);
        settle (*direct);                          // the same time passes on both (LFO phases)
        settle (*direct);
        setPlain (*moved, Decay, shown * 2.0f);
        settle (*moved);
        setPlain (*moved, Decay, shown);
        settle (*moved);
        const std::vector<float> a = burstResponse (*direct), b = burstResponse (*moved);
        double peak = 0.0, worst = 0.0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            peak = std::max (peak, static_cast<double> (std::abs (a[i])));
            worst = std::max (worst, static_cast<double> (std::abs (a[i] - b[i])));
        }
        checkf (peak > 1.0e-3, "%s: the burst is audible (peak %g)", preset->name, peak);
        checkf (worst <= 1.0e-3 * peak, "%s: a fresh recall renders as one whose Decay moved away and back "
                "(worst difference %.1f dB re peak)", preset->name, 20.0 * std::log10 (std::max (worst, 1.0e-30) / peak));
    }
}
// Butterworth-4 low- or high-pass at `hz`, in place (two RBJ sections, Q 0.5412 and 1.3066, in double).
void butterworth4 (std::vector<float>& x, double hz, bool highPass)
{
    const double w0 = 2.0 * 3.14159265358979323846 * hz / kSr, cw = std::cos (w0), sw = std::sin (w0);
    for (const double q : { 0.5412, 1.3066 })
    {
        const double al = sw / (2.0 * q), a0 = 1.0 + al;
        const double b0 = (highPass ? (1.0 + cw) : (1.0 - cw)) * 0.5 / a0;
        const double b1 = (highPass ? -(1.0 + cw) : (1.0 - cw)) / a0, a1 = -2.0 * cw / a0, a2 = (1.0 - al) / a0;
        double z1 = 0.0, z2 = 0.0;
        for (float& v : x)
        {
            const double in = v, out = b0 * in + z1;
            z1 = b1 * in - a1 * out + z2;
            z2 = b0 * in - a2 * out;
            v = static_cast<float> (out);
        }
    }
}

// Below 300 Hz, the L/R correlation of `dsp`'s tail after 100 ms of decorrelated noise, from 50
// to 500 ms past the burst (Butterworth-4 band edges, butterworth4).
double tailCorrelation (DuskVerbDSP& dsp, double loHz, double hiHz);

double lowTailCorrelation (DuskVerbDSP& dsp) { return tailCorrelation (dsp, 0.0, 300.0); }

// L/R correlation of the tail (from 50 ms after the burst) of a decorrelated noise burst, in the
// band loHz..hiHz (Butterworth-4 low- and high-pass; loHz 0: low-pass only).
double tailCorrelation (DuskVerbDSP& dsp, double loHz, double hiHz)
{
    constexpr int kBurst = 4800, kLength = 28800;
    std::vector<float> l (kBlock), r (kBlock), outL, outR;
    uint32_t a = 0x2468ace1u, c = 0x13579bdfu;
    const auto next = [] (uint32_t& seed) { seed = seed * 1664525u + 1013904223u; return static_cast<float> (seed >> 8) / 8388608.0f - 1.0f; };
    for (int start = 0; start < kLength; start += kBlock)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            const bool on = start + i < kBurst;
            l[static_cast<size_t> (i)] = on ? 0.25f * next (a) : 0.0f;
            r[static_cast<size_t> (i)] = on ? 0.25f * next (c) : 0.0f;
        }
        const float* in[] = { l.data(), r.data() };
        float* outs[] = { l.data(), r.data() };
        dsp.processBlock (in, outs, 2, kBlock);
        outL.insert (outL.end(), l.begin(), l.end());
        outR.insert (outR.end(), r.begin(), r.end());
    }
    for (auto* x : { &outL, &outR })
    {
        butterworth4 (*x, hiHz, false);
        if (loHz > 0.0)
            butterworth4 (*x, loHz, true);
    }
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (int i = kBurst + 2400; i < kLength; ++i)
    {
        const double x = outL[static_cast<size_t> (i)], y = outR[static_cast<size_t> (i)];
        lr += x * y; ll += x * x; rr += y * y;
    }
    checkf (ll > 1.0e-9, "the burst is answered");
    return lr / std::sqrt (std::max (ll * rr, 1.0e-300));
}

// Medium Drum Room recalls with its stereo patch (Mono Below 40 Hz, the PMB engine's decorrelated
// side and band widths, DuskVerbDv2Voicing.hpp): below 300 Hz, the tail of a decorrelated noise
// burst stays decorrelated, where the legacy side collapses it (+0.76). And it keeps the patch
// through a host deactivate/activate, whose re-prepare re-installs the engine config: the shared
// table's narrow low widths came back there once, with the side still on (+0.5 broadband).
void testStereoPatch()
{
    const FactoryPreset* preset = presetNamed ("Medium Drum Room");
    checkf (preset != nullptr, "Medium Drum Room is a factory preset");
    if (preset == nullptr)
        return;
    const auto fresh = recalled (*preset);
    const double recall = lowTailCorrelation (*fresh);
    const auto cycled = recalled (*preset);
    cycled->releaseResources();
    cycled->prepare (kSr, kBlock);
    settle (*cycled);
    const double afterCycle = lowTailCorrelation (*cycled);
    std::printf ("Medium Drum Room tail below 300 Hz: L/R correlation %+.3f, %+.3f after a deactivate/activate\n", recall, afterCycle);
    checkf (recall < 0.5, "Medium Drum Room's low tail stays decorrelated (L/R correlation %+.3f)", recall);
    checkf (std::abs (afterCycle - recall) < 0.05, "a deactivate/activate keeps the patch (%+.3f against %+.3f)", afterCycle, recall);
}
// L/R correlation below `hiHz` of the first `ms` of `dsp`'s answer to one impulse (both sides).
double earlyLowCorrelation (DuskVerbDSP& dsp, double hiHz, double ms)
{
    const int length = static_cast<int> (ms * 1e-3 * kSr) / kBlock * kBlock + kBlock;
    std::vector<float> l (kBlock), r (kBlock), outL, outR;
    for (int start = 0; start < length; start += kBlock)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        if (start == 0)
            l[0] = r[0] = 1.0f;
        const float* in[] = { l.data(), r.data() };
        float* outs[] = { l.data(), r.data() };
        dsp.processBlock (in, outs, 2, kBlock);
        outL.insert (outL.end(), l.begin(), l.end());
        outR.insert (outR.end(), r.begin(), r.end());
    }
    const double w0 = 2.0 * 3.14159265358979323846 * hiHz / kSr, cw = std::cos (w0), sw = std::sin (w0);
    for (auto* x : { &outL, &outR })
        for (const double q : { 0.5412, 1.3066 })
        {
            const double al = sw / (2.0 * q), a0 = 1.0 + al;
            const double b0 = (1.0 - cw) * 0.5 / a0, b1 = (1.0 - cw) / a0, a1 = -2.0 * cw / a0, a2 = (1.0 - al) / a0;
            double z1 = 0.0, z2 = 0.0;
            for (float& v : *x)
            {
                const double i = v, o = b0 * i + z1;
                z1 = b1 * i - a1 * o + z2;
                z2 = b0 * i - a2 * o;
                v = static_cast<float> (o);
            }
        }
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (size_t i = 0; i < outL.size(); ++i)
    {
        lr += static_cast<double> (outL[i]) * outR[i];
        ll += static_cast<double> (outL[i]) * outL[i];
        rr += static_cast<double> (outR[i]) * outR[i];
    }
    checkf (ll > 1.0e-12, "the impulse is answered");
    return lr / std::sqrt (std::max (ll * rr, 1.0e-300));
}

// Live Room recalls with DuskVerb 2's stereo voicing (DuskVerbDv2Voicing.hpp): the early field's
// side burst opens the response with anti-phase lows, as the reference's first reflection does,
// while the band width stage (Linkwitz-Riley crossovers) keeps the tail's lows and mids out of the
// anti-phase the shared one-pole width split ran them into (-0.84 broadband). And the stage plays
// on after a host deactivate/activate.
void testStereoBandsPatch()
{
    const FactoryPreset* preset = presetNamed ("Live Room");
    checkf (preset != nullptr, "Live Room is a factory preset");
    if (preset == nullptr)
        return;
    const auto early = recalled (*preset);
    const double opening = earlyLowCorrelation (*early, 300.0, 60.0);
    const auto fresh = recalled (*preset);
    const double low = tailCorrelation (*fresh, 0.0, 300.0);
    const auto again = recalled (*preset);
    const double mid = tailCorrelation (*again, 300.0, 5000.0);
    const auto cycled = recalled (*preset);
    cycled->releaseResources();
    cycled->prepare (kSr, kBlock);
    settle (*cycled);
    const double midAfterCycle = tailCorrelation (*cycled, 300.0, 5000.0);
    std::printf ("Live Room L/R correlation: first 60 ms below 300 Hz %+.3f; tail below 300 Hz %+.3f, "
                 "300 Hz-5 kHz %+.3f (%+.3f after a deactivate/activate)\n", opening, low, mid, midAfterCycle);
    checkf (opening < -0.7, "Live Room opens with anti-phase lows (L/R correlation %+.3f)", opening);
    checkf (low > -0.5, "Live Room's low tail is out of the legacy anti-phase (L/R correlation %+.3f)", low);
    checkf (mid > -0.25, "Live Room's mid tail stays out of anti-phase (L/R correlation %+.3f)", mid);
    checkf (std::abs (midAfterCycle - mid) < 0.05, "a deactivate/activate keeps the stage (%+.3f against %+.3f)", midAfterCycle, mid);
}

// The normalized autocorrelation at `lag` samples of `dsp`'s mono answer to one impulse, band-passed
// to loHz..hiHz, over its tail from 200 ms to 1.2 s past the peak (full_check's boing window); each
// sample is taken over the 100 ms RMS around it, so the decay does not weight the lags.
double tailAutocorrelation (DuskVerbDSP& dsp, double loHz, double hiHz, int lag)
{
    constexpr int kLength = 282 * kBlock;              // 1.5 s
    std::vector<float> l (kBlock), r (kBlock), mono;
    for (int start = 0; start < kLength; start += kBlock)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        if (start == 0)
            l[0] = r[0] = 1.0f;
        const float* in[] = { l.data(), r.data() };
        float* outs[] = { l.data(), r.data() };
        dsp.processBlock (in, outs, 2, kBlock);
        for (int i = 0; i < kBlock; ++i)
            mono.push_back (0.5f * (l[static_cast<size_t> (i)] + r[static_cast<size_t> (i)]));
    }
    const auto loudest = std::max_element (mono.begin(), mono.end(), [] (float a, float b) { return std::abs (a) < std::abs (b); });
    const size_t peak = static_cast<size_t> (loudest - mono.begin());
    butterworth4 (mono, loHz, true);
    butterworth4 (mono, hiHz, false);
    const size_t t0 = peak + static_cast<size_t> (0.2 * kSr), t1 = std::min (mono.size(), peak + static_cast<size_t> (1.2 * kSr));
    const size_t half = static_cast<size_t> (0.05 * kSr);
    std::vector<double> sum (mono.size() + 1, 0.0);
    for (size_t i = 0; i < mono.size(); ++i)
        sum[i + 1] = sum[i] + static_cast<double> (mono[i]) * mono[i];
    std::vector<double> z;
    for (size_t i = t0; i < t1; ++i)
    {
        const size_t a = i - half, b = std::min (mono.size(), i + half);
        z.push_back (mono[i] / std::sqrt ((sum[b] - sum[a]) / static_cast<double> (b - a) + 1.0e-30));
    }
    double num = 0.0, den = 0.0;
    for (size_t i = 0; i < z.size(); ++i)
    {
        den += z[i] * z[i];
        if (i + static_cast<size_t> (lag) < z.size())
            num += z[i] * z[i + static_cast<size_t> (lag)];
    }
    return num / std::max (den, 1.0e-300);
}

// Medium Drum Room recalls with its densifier coefficients (DuskVerbDv2Voicing.hpp): its 1-3 kHz
// tail no longer repeats at the delays of line 5's allpasses (348 and 246 samples; line 5 is the
// band's mid tap), where the tank's own 0.55 bunches the band's modes every fs / delay Hz. And it
// keeps the coefficients through a host deactivate/activate.
void testDensifierPatch()
{
    const FactoryPreset* preset = presetNamed ("Medium Drum Room");
    checkf (preset != nullptr, "Medium Drum Room is a factory preset");
    if (preset == nullptr)
        return;
    const auto first = recalled (*preset);
    const double at348 = tailAutocorrelation (*first, 1000.0, 3000.0, 348);
    const auto second = recalled (*preset);
    const double at246 = tailAutocorrelation (*second, 1000.0, 3000.0, 246);
    const auto cycled = recalled (*preset);
    cycled->releaseResources();
    cycled->prepare (kSr, kBlock);
    settle (*cycled);
    const double afterCycle = tailAutocorrelation (*cycled, 1000.0, 3000.0, 348);
    std::printf ("Medium Drum Room 1-3 kHz tail autocorrelation: %+.3f at 348 samples, %+.3f at 246 (%+.3f at 348 after a "
                 "deactivate/activate)\n", at348, at246, afterCycle);
    checkf (at348 < kDensifiedRepeat && at246 < kDensifiedRepeat,
            "Medium Drum Room's 1-3 kHz tail repeats at line 5's allpass delays (%+.3f, %+.3f)", at348, at246);
    checkf (std::abs (afterCycle - at348) < 0.05, "a deactivate/activate keeps the coefficients (%+.3f against %+.3f)", afterCycle, at348);
}

// Every engine-patch row names a DuskVerb 2 factory preset: a misspelt name would play as no patch.
void testEnginePatchNames()
{
    for (const auto& row : kEnginePatchByName)
        checkf (presetNamed (row.first) != nullptr, "engine patch row '%.*s' names no factory preset",
                static_cast<int> (row.first.size()), row.first.data());
}

// `second`'s burst after `first`, then `second` again: each swap's idle engine takes the next preset,
// so the third recall lands on the engine that played `first`.
std::vector<float> burstAfter (const FactoryPreset& first, const FactoryPreset& second)
{
    const auto dsp = recalled (first);
    recallOnto (*dsp, second);
    recallOnto (*dsp, second);
    return burstResponse (*dsp);
}

// `preset` recalled on an engine that held `previous` renders as on one that held itself. A fresh
// recall is no reference: an engine's time since its swap shows in its render (-34 dB on a
// thrice-recalled Vocal Hall, measured 2026-10-27), so the pair keeps the history alike but for
// the first preset.
void checkRendersAsAfterItself (const FactoryPreset& previous, const FactoryPreset& preset)
{
    const std::vector<float> a = burstAfter (preset, preset), b = burstAfter (previous, preset);
    double peak = 0.0, worst = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        peak = std::max (peak, static_cast<double> (std::abs (a[i])));
        worst = std::max (worst, static_cast<double> (std::abs (a[i] - b[i])));
    }
    std::printf ("%s after %s against after itself: worst difference %.1f dB re peak\n", preset.name,
                 previous.name, 20.0 * std::log10 (std::max (worst, 1.0e-30) / std::max (peak, 1.0e-30)));
    checkf (peak > 1.0e-3, "%s: the burst is audible (peak %g)", preset.name, peak);
    checkf (worst <= 1.0e-4 * peak, "%s after %s renders as after itself", preset.name, previous.name);
}

// Ambience's own delay set (its engine-patch row) plays on an engine that held Vocal Hall's shared
// set, and a preset without a set (Tiled Room) gets the default back on one that held Ambience's.
// A leaked set is a different tank.
void testFdnDelaysPatch()
{
    const FactoryPreset* ambience = presetNamed ("Ambience");
    const FactoryPreset* hall = presetNamed ("Vocal Hall");
    const FactoryPreset* tiled = presetNamed ("Tiled Room");
    checkf (ambience != nullptr && hall != nullptr && tiled != nullptr, "Ambience, Vocal Hall and Tiled Room are factory presets");
    if (ambience == nullptr || hall == nullptr || tiled == nullptr)
        return;
    checkf (enginePatchFor ("Ambience").fdnDelays[0] != 0, "Ambience no longer has its own delays, so this proves nothing");
    checkf (enginePatchFor ("Tiled Room").fdnDelays[0] == 0, "Tiled Room has its own delays now, so this proves nothing");
    checkRendersAsAfterItself (*hall, *ambience);
    checkRendersAsAfterItself (*ambience, *tiled);
}

// Deep Blue Day turns the Shimmer hall (its engine-patch row's feedback rotation), which Black Hole
// plays too: Black Hole after Deep Blue Day renders as after itself, so the turn never outlives
// its preset.
void testFeedbackRotationPatch()
{
    const FactoryPreset* dbd = presetNamed ("Deep Blue Day");
    const FactoryPreset* hole = presetNamed ("Black Hole");
    checkf (dbd != nullptr && hole != nullptr, "Deep Blue Day and Black Hole are factory presets");
    if (dbd == nullptr || hole == nullptr)
        return;
    checkf (enginePatchFor ("Deep Blue Day").fbRotation[0] > 0.0f, "Deep Blue Day no longer turns its hall, so this proves nothing");
    checkf (enginePatchFor ("Black Hole").fbRotation[0] == 0.0f, "Black Hole turns its hall now, so this proves nothing");
    checkRendersAsAfterItself (*dbd, *hole);
}

// A preset-less swap puts the ParallelMultiband bands back where the shared config leaves a preset
// without rows (duskverb::applyNeutralPmbAndModeSmear): Medium Drum Room's table and engine patch
// set every band, and the shared neutral config (reapplyNeutralEngineConfig) leaves them, so a
// restore with no preset identity on an engine that played it kept its bands. (Other name-keyed
// state still survives that path: the widths, the dense early field, the sparse early field's duck;
// it predates this and is not what this checks.)
void testNeutralPmbReset()
{
    const FactoryPreset* preset = presetNamed ("Medium Drum Room");
    checkf (preset != nullptr, "Medium Drum Room is a factory preset");
    if (preset == nullptr)
        return;
    auto engine = std::make_unique<DuskVerbEngine>();
    engine->prepare (kSr, kBlock);
    preset->applyEngineConfig (*engine);
    checkf (engine->getPmbBandLevel (1) != 1.0f, "Medium Drum Room no longer sets its band levels, so this proves nothing");
    engine->reapplyNeutralEngineConfig();
    duskverb::applyNeutralPmbAndModeSmear (*engine);
    constexpr float kNoRowT60[6] = { 2.0f, 2.2f, 2.0f, 1.6f, 1.2f, 0.8f };
    for (int b = 0; b < 6; ++b)
        checkf (engine->getPmbBandT60 (b) == kNoRowT60[b] && engine->getPmbBandLevel (b) == 1.0f
                    && engine->getPmbBandDirect (b) == 0.0f && engine->getPmbBandWidth (b) == 1.0f,
                "after the neutral reset PMB band %d is t60 %g level %g direct %g width %g", b,
                static_cast<double> (engine->getPmbBandT60 (b)), static_cast<double> (engine->getPmbBandLevel (b)),
                static_cast<double> (engine->getPmbBandDirect (b)), static_cast<double> (engine->getPmbBandWidth (b)));
}
} // namespace

int main()
{
    testTable();
    testOverrides();
    testEmission();
    testEngineDecay();
    testSwapGivesCalibratedDecay();
    testStereoPatch();
    testStereoBandsPatch();
    testDensifierPatch();
    testEnginePatchNames();
    testFdnDelaysPatch();
    testFeedbackRotationPatch();
    testNeutralPmbReset();
    if (failures != 0)
    {
        std::printf ("%d failure(s)\n", failures);
        return 1;
    }
    std::printf ("Preset recall: %zu Decay readings, %zu overrides OK\n", kDecayShownByName.size(), kPresetOverrides.size());
    return 0;
}
