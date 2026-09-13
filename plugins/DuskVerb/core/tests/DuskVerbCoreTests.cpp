// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Core gates for the framework-free DuskVerb layer. No DAF, no JUCE — these
// compile and run anywhere a C++17 compiler does, which is what makes them
// usable as the fast inner loop while porting.

#include "DuskVerbDSP.hpp"
#include "DuskVerbParamTable.hpp"
#include "DuskVerbTextParse.hpp"
#include "../src/FactoryPresets.h"
#include "../src/dsp/AlgorithmConfig.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <vector>

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

// ── 1. The parameter table ──────────────────────────────────────────────────
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

    // Bypass must be present and boolean — it is the host-designated bypass.
    check(std::strcmp(paramDesc(duskverb::Bypass).id, "bypass") == 0,
          "ParamId::Bypass indexes the 'bypass' parameter");
    check(paramDesc(duskverb::Bypass).def == 0.0f, "bypass defaults to off");

    // The algorithm labels must not drift from the engine table: the parameter
    // stores an INDEX, and saved sessions and every factory preset are keyed on
    // it, so a mismatch silently reroutes presets to the wrong engine.
    const auto& algo = paramDesc(duskverb::Algorithm);
    checkf(algo.enumCount == getNumAlgorithms(),
           "algorithm label count %d == getNumAlgorithms() %d", algo.enumCount, getNumAlgorithms());
    for (int i = 0; i < getNumAlgorithms(); ++i)
        checkf(std::strcmp(algo.enumLabels[i], getAlgorithmConfig(i).name) == 0,
               "algorithm label %d is '%s' (engine table says '%s')",
               i, algo.enumLabels[i], getAlgorithmConfig(i).name);
}

// ── 2. The factory preset table ─────────────────────────────────────────────
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
    const auto& presets = getFactoryPresets();
    check(presets.size() == 20, "20 factory presets");

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
        checkf(preset.algorithm >= 0 && preset.algorithm < getNumAlgorithms(),
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

// ── 3. Program 0 reproduces the parameter defaults ──────────────────────────
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

// ── 4. The DSP runs every program cleanly ───────────────────────────────────
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

    const auto& presets = getFactoryPresets();
    for (size_t pi = 0; pi < presets.size(); ++pi)
    {
        const FactoryPreset& preset = presets[pi];
        auto dspStorage = std::make_unique<duskverb::DuskVerbDSP>();
        auto& dsp = *dspStorage;
        dsp.prepare(kSampleRate, kBlock);

        preset.collectParameters([&](const char* id, float v) {
            const int i = duskverb::paramIndexForId(id);
            if (i < 0) return;
            const auto& d = paramDesc(i);
            dsp.setParameter(i, duskverb::dspFromHost(d, duskverb::plainToHost(d, v)));
        });
        dsp.applyFactoryPresetConfig(preset);

        // Silence in: the output must be finite and must not run away. It is not
        // required to be exactly zero — several engines carry modulation and a
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
            preset.collectParameters([&](const char* id, float v) {
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

// ── 5. Settled bypass is a bit-exact passthrough ────────────────────────────
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

// ── 6. reset() actually silences a ringing tail ─────────────────────────────
// reset() is deliberately NOT what the host's deactivate() calls (that is
// releaseResources(), for JUCE parity — see DuskVerbDSP.hpp), so this is the
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

// ── 7. The sweep-string tokenizer stays inside its buffer ───────────────────
// duskverb::Tokens is the audio-thread-safe replacement for juce::StringArray in
// FactoryPreset::applyEngineConfig(). It has a fixed capacity and TRUNCATES past
// it, so the interesting case is saturation: an over-long DUSKVERB_* override
// must not hand a caller a pointer past the end of the inline buffer.
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
    // runs past the inline buffer. This is the case that matters — the write
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

// ── 8. Degenerate blocks ────────────────────────────────────────────────────
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

void testHistoryCadence()
{
    for (const int rate : {44100, 48000, 96000})
        for (const int block : {1, 127, 4096})
        {
            auto dspStorage = std::make_unique<duskverb::DuskVerbDSP>();
            auto& dsp = *dspStorage;
            dsp.prepare(rate, block);
            std::array<float, duskverb::DuskVerbDSP::kTailHistorySize> history{};
            dsp.getTailHistory(history.data(), history.size());
            check(std::all_of(history.begin(), history.end(), [](float v) { return v == -100.0f; }),
                  "new history begins at silence");
            dsp.setParameter(duskverb::Bypass, 1.0f);
            std::vector<float> input(block, 0.25f), output(block);
            const float* in[] = {input.data()};
            float* out[] = {output.data()};
            // Exactly 7 complete 15-Hz windows and half of the next window.
            int remaining = rate / 2;
            while (remaining > 0)
            {
                const int count = std::min(block, remaining);
                dsp.processBlock(in, out, 1, count);
                remaining -= count;
            }
            dsp.getTailHistory(history.data(), history.size());
            check(std::count_if(history.begin(), history.end(), [](float v) { return v > -99.0f; }) == 7,
                  "history has a fixed 15-Hz cadence across rates and block sizes");
            dsp.reset();
            dsp.getTailHistory(history.data(), history.size());
            check(std::all_of(history.begin(), history.end(), [](float v) { return v == -100.0f; }),
                  "reset clears history including partially accumulated peak");
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
    testTokenizer();
    testDegenerateBlocks();
    testHistoryCadence();

    if (failures == 0) std::printf("DuskVerbCoreTests: all checks passed\n");
    else               std::printf("DuskVerbCoreTests: %d check(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
