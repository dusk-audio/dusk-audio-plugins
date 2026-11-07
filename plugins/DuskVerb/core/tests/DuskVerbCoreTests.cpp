// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Core gates for the framework-free DuskVerb layer. No DAF, no JUCE - these
// compile and run anywhere a C++17 compiler does, which is what makes them
// usable as the fast inner loop while porting.

#include "DuskVerbDSP.hpp"
#include "DuskVerbDv2Voicing.hpp"
#include "DuskVerbParamTable.hpp"
#include "DuskVerbPresets.hpp"
#include "DuskVerbTextParse.hpp"
#include "../src/FactoryPresets.h"
#include "../src/dsp/AlgorithmConfig.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <vector>

// Heap allocations made while armed: proves clearState() is safe on the audio thread.
static std::atomic<bool> gCountAllocations { false };
static std::atomic<int>  gAllocations { 0 };

void* operator new(std::size_t size)
{
    if (gCountAllocations.load(std::memory_order_relaxed))
        gAllocations.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(size != 0 ? size : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace
{
int failures = 0;

void check(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
}

void checkf(bool ok, const char* fmt, ...)
{
    if (ok) return;
    va_list args;
    va_start(args, fmt);
    std::printf("FAIL: ");
    std::vprintf(fmt, args);
    std::printf("\n");
    va_end(args);
    ++failures;
}

using duskverb::paramDesc;
using duskverb::kNumParams;

// 1. The parameter table
void testParamTable()
{
    check(kNumParams == 92, "parameter count is 92 (the APVTS layout size)");

    std::set<std::string> ids, names;
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& d = paramDesc(i);
        checkf(ids.insert(d.id).second, "parameter id '%s' is unique", d.id);
        checkf(names.insert(d.name).second, "parameter name '%s' is unique", d.name);
        checkf(d.max > d.min, "'%s' has max > min", d.id);
        checkf(d.skew > 0.0f, "'%s' has a positive skew", d.id);
        checkf(d.interval >= 0.0f, "'%s' has a non-negative interval", d.id);
        checkf(d.def >= d.min && d.def <= d.max, "'%s' default %g is inside [%g, %g]",
               d.id, d.def, d.min, d.max);
        checkf(duskverb::paramIndexForId(d.id) == i, "'%s' looks up to its own index", d.id);

        // The host<->plain mapping must be a round trip to within the taper's
        // own resolution. A parameter whose default cannot survive it would
        // load a different value than it saved.
        const float host = duskverb::hostDefault(d);
        checkf(host >= duskverb::hostMin(d) && host <= duskverb::hostMax(d),
               "'%s' host default %g is inside the host range", d.id, host);
        const float back = duskverb::dspFromHost(d, host);
        const float tol = std::max(1e-4f * std::fabs(d.max - d.min), 1e-5f);
        checkf(std::fabs(back - duskverb::rangeSnap(d, d.def)) <= tol,
               "'%s' default survives host round trip (%g -> %g)", d.id, d.def, back);

        if (d.integer)
            checkf(d.enumLabels != nullptr && d.enumCount >= 2,
                   "'%s' is integral and carries enum labels", d.id);
    }

    // Bypass must be present and boolean - it is the host-designated bypass.
    check(std::strcmp(paramDesc(duskverb::Bypass).id, "bypass") == 0,
          "ParamId::Bypass indexes the 'bypass' parameter");
    check(paramDesc(duskverb::Bypass).def == 0.0f, "bypass defaults to off");

    // The algorithm labels must not drift from the engine table: the parameter
    // stores an INDEX, and saved sessions and every factory preset are keyed on
    // it, so a mismatch silently reroutes presets to the wrong engine.
    // The shared prefix must equal the legacy engine table entry for entry; the
    // own engines follow it, then Reserved slots up to the fixed slot count.
    //
    // The slot count is part of every recorded VST3 automation lane: VST3 stores a
    // parameter as a fraction of its range, so changing the range after a release
    // moves every lane to another engine. Add an engine by naming the next Reserved
    // slot, never by changing this number.
    static_assert(duskverb::kNumAlgorithmSlots == 32,
                  "the Algorithm range is in every VST3 automation lane: never change it after a release");
    const auto& algo = paramDesc(duskverb::Algorithm);
    checkf(getNumAlgorithms() == duskverb::kNumSharedAlgorithms,
           "the shared engine table has %d entries, DuskVerb 2 shares %d",
           getNumAlgorithms(), duskverb::kNumSharedAlgorithms);
    checkf(algo.enumCount == duskverb::kNumAlgorithmSlots,
           "algorithm label count %d == kNumAlgorithmSlots %d", algo.enumCount, duskverb::kNumAlgorithmSlots);
    checkf(algo.max == static_cast<float>(duskverb::kNumAlgorithmSlots - 1), "the algorithm range covers every slot");
    for (int i = 0; i < algo.enumCount; ++i)
    {
        checkf(std::strcmp(algo.enumLabels[i], duskverb::algorithmLabel(i)) == 0,
               "algorithm label %d is '%s' (the registry says '%s')",
               i, algo.enumLabels[i], duskverb::algorithmLabel(i));
        if (i < duskverb::kNumSharedAlgorithms)
            checkf(std::strcmp(algo.enumLabels[i], getAlgorithmConfig(i).name) == 0,
                   "algorithm label %d is '%s' (engine table says '%s')",
                   i, algo.enumLabels[i], getAlgorithmConfig(i).name);
        if (i >= duskverb::kNumAlgorithms)
        {
            checkf(std::strcmp(algo.enumLabels[i], "Reserved") == 0, "slot %d past the engines is Reserved", i);
            checkf(!duskverb::algorithmVisible(i), "Reserved slot %d is not offered in the engine menu", i);
            checkf(duskverb::resolveAlgorithm(i) == duskverb::kReservedFallbackAlgorithm,
                   "Reserved slot %d plays the stand-in engine", i);
        }
    }
}

// 2. The factory preset table
// The host domain is PLAIN units and the editor's knob domain is the JUCE
// slider coordinate. For every parameter and five positions along the JUCE
// knob travel, the plain value handed to the host must reach the DSP as the
// float JUCE's APVTS produces for that plain value, and the knob domain must
// round-trip through the host domain.
void testHostAndKnobDomains()
{
    constexpr float positions[5] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& d = paramDesc(i);
        checkf(duskverb::hostMin(d) == (d.skew == 1.0f ? d.min : 0.0f)
            && duskverb::hostMax(d) == (d.skew == 1.0f ? d.max : 1.0f),
               "'%s' host range preserves JUCE automation taper", d.id);
        checkf(duskverb::hostDefault(d) == duskverb::plainToHost(d, d.def),
               "'%s' host default maps from the table", d.id);
        for (float pos : positions)
        {
            // Position on the JUCE slider -> plain value, computed here the JUCE
            // way (NormalisableRange::convertFrom0to1 with skew) rather than
            // through the table's helpers.
            float proportion = pos;
            if (d.skew != 1.0f && proportion > 0.0f)
                proportion = std::exp(std::log(proportion) / d.skew);
            float plain = d.min + (d.max - d.min) * proportion;
            if (d.interval > 0.0f)
                plain = d.min + d.interval * std::floor((plain - d.min) / d.interval + 0.5f);
            plain = std::min(std::max(plain, d.min), d.max);

            // JUCE: setValueNotifyingHost(convertTo0to1(plain)) -> setValue ->
            // convertFrom0to1 -> adapter convertFrom0to1(convertTo0to1(.)).
            const float jd1 = duskverb::cvFrom01(d, duskverb::cvTo01(d, plain));
            const float juceDsp = duskverb::cvFrom01(d, duskverb::cvTo01(d, jd1));

            const float host = duskverb::plainToHost(d, plain);
            checkf(duskverb::hostValueInRange(d, host),
                   "'%s' plain %g is inside the host range", d.id, plain);
            checkf(duskverb::dspFromHost(d, host) == juceDsp,
                   "'%s' at knob %.2f: host %g reaches the DSP as JUCE's %g (got %g)",
                   d.id, pos, host, juceDsp, duskverb::dspFromHost(d, host));

            const float knob = duskverb::plainToKnob(d, plain);
            checkf(knob >= duskverb::knobMin(d) && knob <= duskverb::knobMax(d),
                   "'%s' knob value %g is inside the knob range", d.id, knob);
            const float tol = std::max(1e-4f * std::fabs(d.max - d.min), 1e-5f);
            checkf(std::fabs(duskverb::knobToHost(d, knob) - host) <= tol,
                   "'%s' knob %g -> host %g matches plain %g", d.id, knob,
                   duskverb::knobToHost(d, knob), host);
            checkf(std::fabs(duskverb::hostToKnob(d, host) - knob) <= 1e-6f
                       || (d.integer && duskverb::hostToKnob(d, host) == knob),
                   "'%s' host %g -> knob matches %g", d.id, host, knob);
            // A tapered knob at half travel sits at the JUCE skew midpoint, not the
            // linear one: that is the whole point of keeping the knob domain.
            if (d.skew != 1.0f && pos == 0.5f)
                checkf(std::fabs(knob - 0.5f) <= 1e-6f,
                       "'%s' half travel is knob 0.5 (skew kept), got %g", d.id, knob);
        }
    }
}

void testPresetTable()
{
    const auto& presets = duskverb::dv2FactoryPresets();
    check(getFactoryPresets().size() == 20, "the shared list holds the legacy plugin's 20 presets");
    check(presets.size() == getFactoryPresets().size() + duskverb::ownFactoryPresets().size(),
          "DuskVerb 2's list is the shared list followed by its own");
    for (size_t i = 0; i < getFactoryPresets().size(); ++i)
        checkf(std::strcmp(presets[i].name, getFactoryPresets()[i].name) == 0,
               "program %zu is the shared preset '%s' (a shared program number never moves)", i, getFactoryPresets()[i].name);

    // The editor's preset dropdown emits a section heading whenever the category
    // changes, so a category that appears in two separate runs produces two
    // identical headings.
    std::set<std::string> seenCategories;
    std::string current;
    for (const auto& preset : presets)
    {
        checkf(preset.name != nullptr && preset.name[0] != '\0', "every preset has a name");
        checkf(preset.category != nullptr && preset.category[0] != '\0',
               "preset '%s' has a category", preset.name);
        checkf(preset.algorithm >= 0 && preset.algorithm < duskverb::kNumAlgorithms,
               "preset '%s' algorithm %d is a real engine", preset.name, preset.algorithm);
        // Neither ';' nor '=' may appear in a name: the DAF state codec stores
        // the preset identity in a ';'/'=' delimited string.
        checkf(std::strchr(preset.name, ';') == nullptr && std::strchr(preset.name, '=') == nullptr,
               "preset name '%s' is free of the state codec's delimiters", preset.name);
        if (preset.category != current)
        {
            current = preset.category;
            checkf(seenCategories.insert(current).second,
                   "category '%s' is contiguous (seen only once)", current.c_str());
        }
    }

    std::set<std::string> names;
    for (const auto& preset : presets)
        checkf(names.insert(preset.name).second, "preset name '%s' is unique", preset.name);
}

// 3. Program 0 reproduces the parameter defaults
// createParameterLayout() seeds every default from factory preset 0, so a host
// "reset to defaults" must land on the same voicing as loading program 0. Only
// the parameters whose default is literally a preset-0 field are checked; the
// rest are set from name-keyed override maps that do not feed the defaults.
void testDefaultsMatchProgramZero()
{
    static const char* const kDirect[] = {
        "algorithm", "mix", "bus_mode", "predelay", "predelay_sync", "decay", "size",
        "mod_depth", "mod_rate", "tail_spin_depth", "tail_spin_rate", "bass_mult",
        "mid_mult", "damping", "crossover", "high_crossover", "saturation", "diffusion",
        "er_level", "er_size", "lo_cut", "hi_cut", "hi_cut_shelf_db", "width", "freeze",
        "gain_trim", "mono_below",
        "dpv_hf_shelf_db", "dpv_hf_shelf_hz", "dpv_struct_hf_damp_hz",
        "dpv_box_cut_db", "dpv_box_cut_hz", "dpv_bass_shelf_db", "dpv_bass_shelf_hz",
    };

    std::vector<float> fromPreset(static_cast<size_t>(kNumParams),
                                  std::numeric_limits<float>::quiet_NaN());
    getFactoryPresets().front().collectParameters([&](const char* id, float v) {
        const int i = duskverb::paramIndexForId(id);
        if (i >= 0) fromPreset[static_cast<size_t>(i)] = v;
    });

    for (const char* id : kDirect)
    {
        const int i = duskverb::paramIndexForId(id);
        checkf(i >= 0, "'%s' exists in the parameter table", id);
        if (i < 0) continue;
        const auto& d = paramDesc(i);
        const float presetDsp  = duskverb::dspFromHost(d, duskverb::plainToHost(d, fromPreset[static_cast<size_t>(i)]));
        const float defaultDsp = duskverb::dspFromHost(d, duskverb::hostDefault(d));
        checkf(presetDsp == defaultDsp,
               "'%s': program 0 gives %.9g, the default gives %.9g", id, presetDsp, defaultDsp);
    }
}

// 4. The DSP runs every program cleanly
struct Stereo
{
    std::vector<float> l, r;
    explicit Stereo(int n) : l(static_cast<size_t>(n), 0.0f), r(static_cast<size_t>(n), 0.0f) {}
    const float* inPtr[2]  {};
    float* outPtr[2] {};
};

void runBlocks(duskverb::DuskVerbDSP& dsp, std::vector<float>& l, std::vector<float>& r,
               int total, int block)
{
    for (int off = 0; off < total; off += block)
    {
        const int n = std::min(block, total - off);
        const float* in[2]  = { l.data() + off, r.data() + off };
        float*       out[2] = { l.data() + off, r.data() + off };
        dsp.processBlock(in, out, 2, n);
    }
}

bool allFinite(const std::vector<float>& v)
{
    for (float x : v) if (!std::isfinite(x)) return false;
    return true;
}

double peak(const std::vector<float>& v)
{
    double m = 0.0;
    for (float x : v) m = std::max(m, static_cast<double>(std::fabs(x)));
    return m;
}

// Reverb objects live on the heap, as in the plugin. Two simultaneous engines
// exceed the default Windows test executable stack when allocated as locals.
void testEveryProgram()
{
    constexpr double kSampleRate = 48000.0;
    constexpr int kBlock = 256;
    constexpr int kLen = 48000;   // 1 s

    const auto& presets = duskverb::dv2FactoryPresets();
    for (size_t pi = 0; pi < presets.size(); ++pi)
    {
        const FactoryPreset& preset = presets[pi];
        auto dspStorage = std::make_unique<duskverb::DuskVerbDSP>();
        auto& dsp = *dspStorage;
        dsp.prepare(kSampleRate, kBlock);

        duskverb::collectDv2Parameters(preset, [&](const char* id, float v) {
            const int i = duskverb::paramIndexForId(id);
            if (i < 0) return;
            const auto& d = paramDesc(i);
            dsp.setParameter(i, duskverb::dspFromHost(d, duskverb::plainToHost(d, v)));
        });
        dsp.applyFactoryPresetConfig(preset);

        // Silence in: the output must be finite and must not run away. It is not
        // required to be exactly zero - several engines carry modulation and a
        // dither-scale noise floor by design.
        {
            std::vector<float> l(kLen, 0.0f), r(kLen, 0.0f);
            runBlocks(dsp, l, r, kLen, kBlock);
            checkf(allFinite(l) && allFinite(r), "'%s': silence in -> finite out", preset.name);
            checkf(peak(l) < 1e-3 && peak(r) < 1e-3,
                   "'%s': silence in -> near-silence out (peak %.3g / %.3g)",
                   preset.name, peak(l), peak(r));
        }

        // Impulse in: finite, bounded, and audible. A preset that produces
        // nothing is a preset whose engine config failed to install.
        {
            auto freshStorage = std::make_unique<duskverb::DuskVerbDSP>();
            auto& fresh = *freshStorage;
            fresh.prepare(kSampleRate, kBlock);
            duskverb::collectDv2Parameters(preset, [&](const char* id, float v) {
                const int i = duskverb::paramIndexForId(id);
                if (i < 0) return;
                const auto& d = paramDesc(i);
                fresh.setParameter(i, duskverb::dspFromHost(d, duskverb::plainToHost(d, v)));
            });
            // 100 % wet, so the measurement cannot be satisfied by the dry copy.
            {
                const auto& d = paramDesc(duskverb::Mix);
                fresh.setParameter(duskverb::Mix, duskverb::dspFromHost(d, duskverb::plainToHost(d, 1.0f)));
            }
            fresh.applyFactoryPresetConfig(preset);

            std::vector<float> l(kLen, 0.0f), r(kLen, 0.0f);
            l[0] = r[0] = 1.0f;
            runBlocks(fresh, l, r, kLen, kBlock);
            checkf(allFinite(l) && allFinite(r), "'%s': impulse in -> finite out", preset.name);
            checkf(peak(l) < 100.0 && peak(r) < 100.0,
                   "'%s': impulse in -> bounded out (peak %.3g / %.3g)", preset.name, peak(l), peak(r));

            // Energy after the first 50 ms proves a tail exists at all.
            double tail = 0.0;
            for (int i = 2400; i < kLen; ++i)
                tail += static_cast<double>(l[static_cast<size_t>(i)]) * l[static_cast<size_t>(i)]
                      + static_cast<double>(r[static_cast<size_t>(i)]) * r[static_cast<size_t>(i)];
            checkf(tail > 1e-9, "'%s': impulse produces a reverb tail (energy %.3g)", preset.name, tail);
        }
    }
}

// 5. Settled bypass is a bit-exact passthrough
void testBypassIsExact()
{
    constexpr int kBlock = 256;
    auto dspStorage = std::make_unique<duskverb::DuskVerbDSP>();
    auto& dsp = *dspStorage;
    dsp.prepare(48000.0, kBlock);
    {
        const auto& d = paramDesc(duskverb::Bypass);
        dsp.setParameter(duskverb::Bypass, duskverb::dspFromHost(d, 1.0f));
    }

    unsigned seed = 12345u;
    auto nextNoise = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<int>(seed >> 8) % 20001 - 10000) / 10000.0f;
    };

    // Let the 30 ms crossfade settle, then compare.
    bool exact = true;
    for (int block = 0; block < 40; ++block)
    {
        std::vector<float> inL(kBlock), inR(kBlock), outL(kBlock), outR(kBlock);
        for (int i = 0; i < kBlock; ++i) { inL[static_cast<size_t>(i)] = nextNoise(); inR[static_cast<size_t>(i)] = nextNoise(); }
        const float* in[2]  = { inL.data(), inR.data() };
        float*       out[2] = { outL.data(), outR.data() };
        dsp.processBlock(in, out, 2, kBlock);
        if (block < 20) continue;   // 20 * 256 = 5120 samples > 30 ms at 48 kHz
        for (int i = 0; i < kBlock; ++i)
            if (outL[static_cast<size_t>(i)] != inL[static_cast<size_t>(i)]
                || outR[static_cast<size_t>(i)] != inR[static_cast<size_t>(i)])
                exact = false;
    }
    check(exact, "settled bypass is a bit-exact passthrough (max|out-in| == 0)");
}

// 6. reset() actually silences a ringing tail
// reset() is deliberately NOT what the host's deactivate() calls (that is
// releaseResources(), for JUCE parity - see DuskVerbDSP.hpp), so this is the
// gate that keeps it honest: after a hard flush the reverb must go quiet, and
// releaseResources() must leave the DSP in a state a later prepare() revives.
void testResetSilencesTail()
{
    constexpr int kBlock = 256;
    constexpr double kSr = 48000.0;

    auto dspStorage = std::make_unique<duskverb::DuskVerbDSP>();
    auto& dsp = *dspStorage;
    dsp.prepare(kSr, kBlock);
    {
        const auto& d = paramDesc(duskverb::Mix);
        dsp.setParameter(duskverb::Mix, duskverb::dspFromHost(d, duskverb::plainToHost(d, 1.0f)));
    }

    // Excite a tail, then let the input go silent and confirm one is ringing.
    std::vector<float> l(kBlock, 0.0f), r(kBlock, 0.0f);
    l[0] = r[0] = 1.0f;
    const float* in[2]  = { l.data(), r.data() };
    float*       out[2] = { l.data(), r.data() };
    dsp.processBlock(in, out, 2, kBlock);
    for (int b = 0; b < 8; ++b)
    {
        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        dsp.processBlock(in, out, 2, kBlock);
    }
    const double ringing = peak(l);
    checkf(ringing > 1e-6, "the test actually excited a tail (peak %.3g)", ringing);

    dsp.reset();
    dsp.prepare(kSr, kBlock);
    std::fill(l.begin(), l.end(), 0.0f);
    std::fill(r.begin(), r.end(), 0.0f);
    dsp.processBlock(in, out, 2, kBlock);
    const double after = std::max(peak(l), peak(r));
    // Not exactly zero: DuskVerbEngine::clearAllBuffers() + prepare() still
    // leaves ~1e-13 of numerical dust in the shell filters (measured), which is
    // ~-260 dBFS and inaudible. The gate is that the tail is GONE, by nine
    // orders of magnitude, not that the last bit is scrubbed.
    checkf(after < ringing * 1e-9 && after < 1e-9,
           "reset() silences the tail (%.3g -> %.3g)", ringing, after);
}

// clearState() is the host-reset path (CLAP reset, AU Reset) and runs on the audio
// thread: no allocation, no re-prepare. It must still kill the tail, and the
// instance must ring again afterwards. Default program only: clearState() names the
// presets whose output match EQ it cannot clear.
void testClearStateIsRealtime()
{
    // Every program, own engines included: a host reset must leave each one silent
    // at once, without allocating. -100 dBFS after a full-scale impulse is the bar;
    // the strict relative check and the "rings again" check run on the default
    // program, whose tail is long enough to measure against.
    constexpr int kBlock = 256;
    constexpr double kSr = 48000.0;
    const auto& presets = duskverb::dv2FactoryPresets();
    double worst = 0.0;
    const char* worstName = "";
    for (size_t pi = 0; pi < presets.size(); ++pi)
    {
        const FactoryPreset& preset = presets[pi];
        auto dspStorage = std::make_unique<duskverb::DuskVerbDSP>();
        auto& dsp = *dspStorage;
        dsp.prepare(kSr, kBlock);
        duskverb::collectDv2Parameters(preset, [&](const char* id, float v) {
            const int i = duskverb::paramIndexForId(id);
            if (i < 0) return;
            const auto& d = paramDesc(i);
            dsp.setParameter(i, duskverb::dspFromHost(d, duskverb::plainToHost(d, v)));
        });
        dsp.applyFactoryPresetConfig(preset);
        {
            const auto& d = paramDesc(duskverb::Mix);
            dsp.setParameter(duskverb::Mix, duskverb::dspFromHost(d, duskverb::plainToHost(d, 1.0f)));
        }

        std::vector<float> l(kBlock, 0.0f), r(kBlock, 0.0f);
        const float* in[2]  = { l.data(), r.data() };
        float*       out[2] = { l.data(), r.data() };
        const auto excite = [&] {
            std::fill(l.begin(), l.end(), 0.0f);
            std::fill(r.begin(), r.end(), 0.0f);
            l[0] = r[0] = 1.0f;
            dsp.processBlock(in, out, 2, kBlock);
            for (int b = 0; b < 8; ++b)
            {
                std::fill(l.begin(), l.end(), 0.0f);
                std::fill(r.begin(), r.end(), 0.0f);
                dsp.processBlock(in, out, 2, kBlock);
            }
            return std::max(peak(l), peak(r));
        };

        const double ringing = excite();

        gAllocations.store(0, std::memory_order_relaxed);
        gCountAllocations.store(true, std::memory_order_relaxed);
        dsp.clearState();
        gCountAllocations.store(false, std::memory_order_relaxed);
        checkf(gAllocations.load() == 0, "'%s': clearState() allocates nothing (%d allocations)",
               preset.name, gAllocations.load());

        std::fill(l.begin(), l.end(), 0.0f);
        std::fill(r.begin(), r.end(), 0.0f);
        dsp.processBlock(in, out, 2, kBlock);
        const double after = std::max(peak(l), peak(r));
        checkf(after < 1e-5, "'%s': clearState() leaves %.1f dBFS (tail was %.1f dBFS)", preset.name,
               20.0 * std::log10(after + 1e-30), 20.0 * std::log10(ringing + 1e-30));
        if (after > worst)
        {
            worst = after;
            worstName = preset.name;
        }

        if (pi == 0)
        {
            std::printf("clearState: tail %.3g -> first block %.3g\n", ringing, after);
            checkf(ringing > 1e-6, "the test actually excited a tail (peak %.3g)", ringing);
            checkf(after < ringing * 1e-6, "clearState() silences the tail (%.3g -> %.3g)", ringing, after);
            const double again = excite();
            checkf(again > 1e-6, "the instance rings again after clearState() (peak %.3g)", again);
        }
    }
    std::printf("clearState: worst program '%s' at %.1f dBFS on the first block\n", worstName,
                20.0 * std::log10(worst + 1e-30));
}

// 6b. DuskVerb 2's own engines inside the shared pair
namespace family
{
constexpr int kBlock = 256;
constexpr double kSr = 48000.0;
constexpr int kSecond = 48000;
constexpr int kPlate = 0;

const FactoryPreset& presetNamed(const char* name)
{
    for (const auto& preset : duskverb::dv2FactoryPresets())
        if (std::strcmp(preset.name, name) == 0)
            return preset;
    std::printf("FAIL: no factory preset named '%s'\n", name);
    std::exit(1);
}

void setPlain(duskverb::DuskVerbDSP& dsp, int index, float plain)
{
    const auto& d = paramDesc(index);
    dsp.setParameter(index, duskverb::dspFromHost(d, duskverb::plainToHost(d, plain)));
}

struct Instance
{
    std::unique_ptr<duskverb::DuskVerbDSP> storage = std::make_unique<duskverb::DuskVerbDSP>();
    duskverb::DuskVerbDSP& dsp = *storage;
    unsigned seed = 1u;
    int fadesStarted = 0;

    // As a host loads a program: its parameters at 100 % wet, then the swap.
    explicit Instance(const FactoryPreset& preset)
    {
        dsp.prepare(kSr, kBlock);
        duskverb::collectDv2Parameters(preset, [&](const char* id, float v) {
            const int i = duskverb::paramIndexForId(id);
            if (i >= 0) setPlain(dsp, i, v);
        });
        setPlain(dsp, duskverb::Mix, 1.0f);
        dsp.applyFactoryPresetConfig(preset);
    }

    // `samples` of noise at `level` (0: silence), block by block, counting the preset
    // fades that start. Returns the output peak.
    double run(int samples, float level)
    {
        double outPeak = 0.0;
        std::vector<float> l(kBlock), r(kBlock);
        for (int done = 0; done < samples; done += kBlock)
        {
            const int n = std::min(kBlock, samples - done);
            for (int i = 0; i < n; ++i)
            {
                seed = seed * 1664525u + 1013904223u;
                l[static_cast<size_t>(i)] = level * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f);
                seed = seed * 1664525u + 1013904223u;
                r[static_cast<size_t>(i)] = level * (static_cast<float>(seed >> 8) / 8388608.0f - 1.0f);
            }
            const bool idle = dsp.canRecall();
            const float* in[2] = { l.data(), r.data() };
            float* out[2] = { l.data(), r.data() };
            dsp.processBlock(in, out, 2, n);
            if (idle && !dsp.canRecall()) ++fadesStarted;
            for (int i = 0; i < n; ++i)
                outPeak = std::max({ outPeak, static_cast<double>(std::fabs(l[static_cast<size_t>(i)])),
                                     static_cast<double>(std::fabs(r[static_cast<size_t>(i)])) });
        }
        return outPeak;
    }

    // The response to a unit impulse: left, then right.
    std::vector<float> impulseResponse(int samples)
    {
        std::vector<float> l(static_cast<size_t>(samples), 0.0f), r(static_cast<size_t>(samples), 0.0f);
        l[0] = r[0] = 1.0f;
        runBlocks(dsp, l, r, samples, kBlock);
        l.insert(l.end(), r.begin(), r.end());
        return l;
    }
};

// Share of an impulse response's energy from 1.2 s to 1.6 s. The own engine's longest
// burst is over by 0.8 s; a 3 s shared tail is still plainly there.
double lateShare(const std::vector<float>& ir)
{
    const size_t half = ir.size() / 2;
    const size_t from = static_cast<size_t>(1.2 * kSr), to = static_cast<size_t>(1.6 * kSr);
    double late = 0.0, total = 0.0;
    for (size_t i = 0; i < half; ++i)
    {
        const double e = static_cast<double>(ir[i]) * ir[i] + static_cast<double>(ir[half + i]) * ir[half + i];
        total += e;
        if (i >= from && i < to) late += e;
    }
    return total > 0.0 ? late / total : 0.0;
}
} // namespace family

// Moving Algorithm across the shared/own boundary swaps the pair with the preset
// crossfade (the outgoing engine's tail plays out), a crossing asked for while a fade
// runs still happens once it ends, and the instance then renders exactly what an
// instance that loaded the destination engine renders.
void testEngineFamilyChange()
{
    using namespace family;
    const FactoryPreset& nonlin = presetNamed("Nonlinear Small");
    const float noise = 0.25f;

    // The own engine loaded directly: what every route onto it must render, bit for bit.
    Instance reference(nonlin);
    reference.run(4 * kSecond, 0.0f);
    const auto expected = reference.impulseResponse(kSecond);

    {
        Instance s(nonlin);
        setPlain(s.dsp, duskverb::Algorithm, kPlate);
        s.run(kSecond / 2, noise);
        s.fadesStarted = 0;
        setPlain(s.dsp, duskverb::Algorithm, duskverb::kNonLinAlgorithm);
        s.run(kBlock, noise);
        check(s.fadesStarted == 1, "shared -> own crossfades like a preset swap");
        s.run(kSecond / 5, noise);
        s.run(4 * kSecond, 0.0f);
        check(s.impulseResponse(kSecond) == expected,
              "shared -> own renders exactly what the own engine loaded directly renders");
    }

    {
        Instance own(nonlin);
        setPlain(own.dsp, duskverb::Decay, 3.0f);
        own.run(kSecond, 0.0f);
        const double ownLate = lateShare(own.impulseResponse(2 * kSecond));
        checkf(ownLate < 1e-10, "the own engine is silent 1.2 s after an impulse (late share %.3g)", ownLate);

        Instance s(nonlin);
        setPlain(s.dsp, duskverb::Decay, 3.0f);
        s.run(kSecond / 2, noise);
        s.fadesStarted = 0;
        setPlain(s.dsp, duskverb::Algorithm, kPlate);
        s.run(kBlock, noise);
        check(s.fadesStarted == 1, "own -> shared crossfades like a preset swap");
        s.run(kSecond / 5, noise);
        s.run(3 * kSecond, 0.0f);
        const double late = lateShare(s.impulseResponse(2 * kSecond));
        checkf(late > 1e-6, "own -> shared hands over to the shared engine (late share %.3g)", late);
    }

    {
        Instance s(nonlin);
        setPlain(s.dsp, duskverb::Algorithm, kPlate);
        setPlain(s.dsp, duskverb::Decay, 3.0f);
        s.run(kSecond / 2, noise);
        s.fadesStarted = 0;
        setPlain(s.dsp, duskverb::Algorithm, duskverb::kNonLinAlgorithm);
        s.run(kBlock, noise);
        setPlain(s.dsp, duskverb::Algorithm, kPlate);
        s.run(kBlock, noise);
        check(s.fadesStarted == 1, "a crossing asked for mid-fade waits for the fade");
        s.run(kSecond / 2, noise);
        check(s.fadesStarted == 2, "the waiting crossing crossfades once the fade ends");
        s.run(3 * kSecond, 0.0f);
        const double late = lateShare(s.impulseResponse(2 * kSecond));
        checkf(late > 1e-6, "shared -> own -> shared mid-fade ends on the shared engine (late share %.3g)", late);
    }

    {
        Instance s(nonlin);
        s.run(kSecond / 2, noise);
        s.fadesStarted = 0;
        setPlain(s.dsp, duskverb::Algorithm, kPlate);
        s.run(kBlock, noise);
        setPlain(s.dsp, duskverb::Algorithm, duskverb::kNonLinAlgorithm);
        s.run(kSecond / 2, noise);
        check(s.fadesStarted == 2, "own -> shared -> own mid-fade crossfades twice");
        s.run(4 * kSecond, 0.0f);
        check(s.impulseResponse(kSecond) == expected,
              "own -> shared -> own mid-fade ends on the own engine, exactly");
    }

    // A host reset silences a sounding own voice.
    {
        Instance s(nonlin);
        s.run(kSecond / 4, 0.0f);
        const double ringing = s.run(kSecond / 10, noise);
        s.dsp.clearState();
        const double after = s.run(kBlock, 0.0f);
        checkf(ringing > 1e-3 && after < ringing * 1e-6,
               "clearState() silences the own voice (%.3g -> %.3g)", ringing, after);
    }
}

// A shared engine that sat idle under its own voice is never heard holding what it held
// before. A host deactivate keeps the engines' buffers (releaseResources()), so input
// history in the shared engine outlives a re-prepare. Coming back on the own engine and
// then returning to the shared family, by a crossing or by a fresh start after another
// re-prepare, must not replay it.
void testIdleSharedEngineIsNotReplayed()
{
    using namespace family;
    const FactoryPreset& start = duskverb::dv2FactoryPresets().front();
    for (const bool viaCrossing : { true, false })
    {
        Instance s(start);
        setPlain(s.dsp, duskverb::Predelay, 120.0f);
        s.run(kSecond, 0.5f);
        s.dsp.releaseResources();
        setPlain(s.dsp, duskverb::Algorithm, duskverb::kNonLinAlgorithm);
        s.dsp.prepare(kSr, kBlock);                 // a fresh start on the own engine
        s.run(2 * kSecond, 0.0f);
        if (viaCrossing)
            setPlain(s.dsp, duskverb::Algorithm, static_cast<float>(start.algorithm));
        else
        {
            s.dsp.releaseResources();
            setPlain(s.dsp, duskverb::Algorithm, static_cast<float>(start.algorithm));
            s.dsp.prepare(kSr, kBlock);             // a fresh start back on the idle shared engine
        }
        // Measured: numerical dust (under 1e-13) when nothing is replayed; 6e-6 (crossing)
        // and 4e-2 (fresh start) when the stale state is heard.
        const double ghost = s.run(kSecond, 0.0f);
        checkf(ghost < 1e-9, "returning to the shared family by %s replays stale input (peak %.3g)",
               viaCrossing ? "a crossing" : "a fresh start", ghost);
    }
}

// 7. The sweep-string tokenizer stays inside its buffer
// duskverb::Tokens is the audio-thread-safe replacement for juce::StringArray in
// FactoryPreset::applyEngineConfig(). It has a fixed capacity and TRUNCATES past
// it, so the interesting case is saturation: an over-long DUSKVERB_* override
// must not hand a caller a pointer past the end of the inline buffer.
// A Reserved slot (from automation drawn there, or a session saved by a later version)
// must play exactly its stand-in engine, never an out-of-range table entry.
void testReservedSlotsPlayTheFallback()
{
    constexpr double kSampleRate = 48000.0;
    constexpr int kBlock = 256;
    constexpr int kLen = 24000;
    auto render = [&](float slot) {
        auto dsp = std::make_unique<duskverb::DuskVerbDSP>();
        dsp->prepare(kSampleRate, kBlock);
        dsp->setParameter(duskverb::Algorithm, slot);
        std::vector<float> l(kLen, 0.0f), r(kLen, 0.0f);
        l[0] = r[0] = 0.5f;
        runBlocks(*dsp, l, r, kLen, kBlock);
        return std::make_pair(l, r);
    };
    const auto standIn = render(static_cast<float>(duskverb::kReservedFallbackAlgorithm));
    for (int slot : { duskverb::kNumAlgorithms, duskverb::kNumAlgorithmSlots - 1 })
    {
        const auto reserved = render(static_cast<float>(slot));
        checkf(reserved == standIn, "Reserved slot %d renders like the stand-in engine %d",
               slot, duskverb::kReservedFallbackAlgorithm);
    }
    checkf(peak(standIn.first) > 1e-4, "the stand-in engine sounds");
}

void testTokenizer()
{
    using duskverb::Tokens;

    // Ordinary use.
    {
        Tokens t("1.5,-2,3e2", ',');
        check(t.size() == 3, "a three-field CSV splits into three tokens");
        check(t.getFloatValue(0) == 1.5f, "token 0 parses as 1.5");
        check(t.getFloatValue(1) == -2.0f, "token 1 parses as -2");
        check(t.getFloatValue(2) == 300.0f, "token 2 parses as 3e2");
        check(t.getIntValue(1) == -2, "token 1 parses as the integer -2");
    }
    // juce::StringArray::addTokens semantics: empty input yields no tokens,
    // empty fields between separators are kept.
    {
        check(Tokens("", ',').size() == 0, "an empty override yields no tokens");
        Tokens t("a,,b,", ',');
        check(t.size() == 4, "empty fields between separators are kept");
        check(t[1][0] == '\0', "an empty field reads as an empty string");
        check(t[99][0] == '\0', "an out-of-range index reads as an empty string");
    }
    // Too MANY fields: the token count saturates and the rest are dropped.
    {
        std::string many;
        for (int i = 0; i < Tokens::kMaxTokens * 4; ++i)
        {
            if (i) many.push_back(',');
            many += "7";
        }
        Tokens t(many.c_str(), ',');
        checkf(t.size() <= Tokens::kMaxTokens,
               "a field-count overflow truncates to at most kMaxTokens (got %d)", t.size());
    }
    // Too much TEXT: few enough fields to be recorded, but their combined length
    // runs past the inline buffer. This is the case that matters - the write
    // cursor saturates at kBufSize, and an unclamped token offset would hand the
    // caller a one-past-the-end pointer. Every recorded token must still point
    // inside the buffer, and a token past the cut must read as empty so the
    // caller's size/value check rejects the override instead of acting on it.
    {
        const std::string field(200, '9');
        std::string wide;
        for (int i = 0; i < 12; ++i)
        {
            if (i) wide.push_back(',');
            wide += field;
        }
        checkf(static_cast<int>(wide.size()) > Tokens::kBufSize,
               "the text-overflow case really exceeds kBufSize (%zu chars)", wide.size());
        Tokens t(wide.c_str(), ',');
        checkf(t.size() == 12, "all 12 fields are still recorded (got %d)", t.size());

        const char* base = t[0];
        int outOfBounds = -1;
        for (int i = 0; i < t.size(); ++i)
        {
            const char* tok = t[i];
            if (tok < base || tok >= base + Tokens::kBufSize) { outOfBounds = i; break; }
        }
        checkf(outOfBounds < 0,
               "every token of a text-overflowing override points inside the buffer "
               "(token %d escaped)", outOfBounds);
        // The final token lies entirely past the cut, so it must be empty.
        check(t[11][0] == '\0', "a token past the buffer cut reads as empty");
        check(t.getFloatValue(11) == 0.0f, "a token past the buffer cut parses as 0");
    }
}

// 8. Degenerate blocks
void testDegenerateBlocks()
{
    auto dspStorage = std::make_unique<duskverb::DuskVerbDSP>();
    auto& dsp = *dspStorage;
    dsp.prepare(48000.0, 256);
    std::vector<float> l(256, 0.5f), r(256, -0.5f);
    const float* in[2]  = { l.data(), r.data() };
    float*       out[2] = { l.data(), r.data() };
    dsp.processBlock(in, out, 2, 0);      // must not touch anything or crash
    check(l[0] == 0.5f && r[0] == -0.5f, "a zero-frame block is a no-op");
    dsp.processBlock(in, out, 2, -1);
    check(l[0] == 0.5f && r[0] == -0.5f, "a negative-frame block is a no-op");

    // Mono layout.
    std::vector<float> m(256, 0.0f);
    m[0] = 1.0f;
    const float* monoIn[1]  = { m.data() };
    float*       monoOut[1] = { m.data() };
    dsp.processBlock(monoIn, monoOut, 1, 256);
    check(allFinite(m), "a mono block produces finite output");
}

// DuskVerb 2's band width stage (Dv2StereoBands.hpp): off, the wet goes through untouched; on, the
// mid (and so the mono sum) is untouched and only the side is shaped; a swap back to off fades and
// then leaves the wet exactly alone again; and the block size never changes the output.
// The ParallelMultiband densifier and band-level hooks: six values in range, nothing else.
void testPmbPatchParsers()
{
    std::array<float, 6> k {}, m {};
    check (duskverb::parsePmbDensifier ("-1,0.15,-1,0.15,-1,-1", k) && k[1] == 0.15f && k[0] == -1.0f,
           "the densifier hook reads six coefficients, -1 for the tank's own");
    check (duskverb::parsePmbLevel ("1,0.8,1,1.2,1,1", m) && m[1] == 0.8f && m[3] == 1.2f, "the band-level hook reads six multipliers");
    const std::array<float, 6> keptK = k, keptM = m;
    for (const char* bad : { "0.8,0,0,0,0,0", "-1.5,0,0,0,0,0", "0,0,0,0,0", "0,0,0,0,0,0,0", "a,0,0,0,0,0", "", "nan,0,0,0,0,0", "inf,0,0,0,0,0", "0,,0,0,0,0" })
        check (! duskverb::parsePmbDensifier (bad, k) && k == keptK, "the densifier hook refuses text out of range or of the wrong length");
    for (const char* bad : { "0,1,1,1,1,1", "4.5,1,1,1,1,1", "1,1,1,1,1", "1,1,1,1,1,1,1", "", "nan,1,1,1,1,1", "inf,1,1,1,1,1", "1,,1,1,1,1" })
        check (! duskverb::parsePmbLevel (bad, m) && m == keptM, "the band-level hook refuses text out of range or of the wrong length");
}

void testStereoBands()
{
    std::vector<float> l (24000), r (24000);
    uint32_t seed = 0x2468u;
    for (size_t i = 0; i < l.size(); ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        l[i] = static_cast<float> (seed >> 8) / 8388608.0f - 1.0f;
        seed = seed * 1664525u + 1013904223u;
        r[i] = 0.5f * l[i] + 0.5f * (static_cast<float> (seed >> 8) / 8388608.0f - 1.0f);
    }
    duskverb::StereoBands off;
    off.prepare (48000.0);
    std::vector<float> ol = l, orr = r;
    off.process (ol.data(), orr.data(), static_cast<int> (ol.size()));
    check (ol == l && orr == r, "the stereo band stage with no row leaves the wet untouched");

    const auto runIn = [&] (int block, std::vector<float>& a, std::vector<float>& b) {
        duskverb::StereoBands st;
        st.prepare (48000.0);
        st.swapTo ({ 2.0f, 0.5f, 1.2f }, 0);
        a = l; b = r;
        for (size_t i = 0; i < a.size(); i += static_cast<size_t> (block))
        {
            const int n = static_cast<int> (std::min (a.size() - i, static_cast<size_t> (block)));
            st.process (a.data() + i, b.data() + i, n);
        }
    };
    std::vector<float> a64, b64, a1000, b1000;
    runIn (64, a64, b64);
    runIn (1000, a1000, b1000);
    check (a64 == a1000 && b64 == b1000, "the stereo band stage's output does not depend on the block size");
    double midErr = 0.0;
    for (size_t i = 0; i < l.size(); ++i)
        midErr = std::max (midErr, static_cast<double> (std::abs (0.5f * (a64[i] + b64[i]) - 0.5f * (l[i] + r[i]))));
    check (midErr < 1.0e-6, "the stereo band stage leaves the mid alone");

    duskverb::StereoBands st;
    st.prepare (48000.0);
    st.swapTo ({ 2.0f, 0.5f, 1.2f }, 0);
    std::vector<float> fl = l, fr = r;
    st.process (fl.data(), fr.data(), 4800);
    st.swapTo (duskverb::StereoBands::kOff, 2400);
    st.process (fl.data() + 4800, fr.data() + 4800, 19200);
    bool exactAfterFade = true;
    for (size_t i = 4800 + 2400; i < l.size(); ++i)
        exactAfterFade &= fl[i] == l[i] && fr[i] == r[i];
    check (exactAfterFade && ! st.active(), "after its fade to no row the stage leaves the wet exactly alone");

    // Each gain reaches its own band: a side tone at 100 Hz, 1 kHz and 10 kHz comes out doubled by
    // the gain of 2 on its band (against a unity row) and untouched by a 2 on the others.
    const auto sideGain = [] (double hz, const duskverb::StereoBands::Gains& g) {
        duskverb::StereoBands sb, unity;
        sb.prepare (48000.0);
        unity.prepare (48000.0);
        sb.swapTo (g, 0);
        unity.swapTo ({ 1.0f, 1.0f, 1.0f }, 0);
        std::vector<float> al (48000), ar (48000);
        for (size_t i = 0; i < al.size(); ++i)
        {
            al[i] = 0.25f * static_cast<float> (std::sin (2.0 * 3.14159265358979323846 * hz * static_cast<double> (i) / 48000.0));
            ar[i] = -al[i];
        }
        std::vector<float> bl = al, br = ar;
        sb.process (al.data(), ar.data(), static_cast<int> (al.size()));
        unity.process (bl.data(), br.data(), static_cast<int> (bl.size()));
        double shaped = 0.0, plain = 0.0;
        for (size_t i = al.size() / 2; i < al.size(); ++i)
        {
            shaped += 0.25 * (al[i] - ar[i]) * (al[i] - ar[i]);
            plain  += 0.25 * (bl[i] - br[i]) * (bl[i] - br[i]);
        }
        return std::sqrt (shaped / std::max (plain, 1.0e-30));
    };
    const struct { double hz; int band; } tones[] = { { 100.0, 0 }, { 1000.0, 1 }, { 10000.0, 2 } };
    for (const auto& tone : tones)
        for (int band = 0; band < 3; ++band)
        {
            duskverb::StereoBands::Gains g { 1.0f, 1.0f, 1.0f };
            g[static_cast<size_t> (band)] = 2.0f;
            const double ratio = sideGain (tone.hz, g);
            const bool own = band == tone.band;
            checkf ((own && ratio > 1.8 && ratio < 2.2) || (! own && ratio > 0.9 && ratio < 1.12),
                    "a gain of 2 on band %d moves a %g Hz side tone x%.3f", band, tone.hz, ratio);
        }
}

} // namespace

int main()
{
    testParamTable();
    testHostAndKnobDomains();
    testPresetTable();
    testDefaultsMatchProgramZero();
    testEveryProgram();
    testBypassIsExact();
    testResetSilencesTail();
    testClearStateIsRealtime();
    testEngineFamilyChange();
    testIdleSharedEngineIsNotReplayed();
    testReservedSlotsPlayTheFallback();
    testTokenizer();
    testDegenerateBlocks();
    testStereoBands();
    testPmbPatchParsers();

    if (failures == 0) std::printf("DuskVerbCoreTests: all checks passed\n");
    else               std::printf("DuskVerbCoreTests: %d check(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
