// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Gates for the plugin-shell logic that does not need DAF linked: the full
// state codec and the factory-program apply path. DuskVerbParams.hpp is
// deliberately free of DafPlugin.hpp so this can be compiled on its own.

#include "DuskVerbParams.hpp"
#include "DuskVerbFormat.hpp"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
int failures = 0;

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

duskverb::StateValues makeState()
{
    duskverb::StateValues s{};
    for (int i = 0; i < duskverb::kNumParams; ++i)
    {
        const auto& d = duskverb::paramDesc(i);
        // A value that is inside the host range but not the default, so a codec
        // that silently substituted defaults could not pass.
        const float lo = duskverb::hostMin(d), hi = duskverb::hostMax(d);
        float v = lo + (hi - lo) * 0.37f;
        if (d.integer) v = std::round(v);
        s.params[static_cast<size_t>(i)] = v;
    }
    s.sixAP.densityBaseline = 0.71f;
    s.sixAP.bloomCeiling    = 0.93f;
    s.sixAP.earlyMix        = 0.66f;
    s.sixAP.outputTrim      = 1.42f;
    for (int i = 0; i < 6; ++i) s.sixAP.bloomStagger[i] = 0.5f + 0.13f * static_cast<float>(i);
    s.presetName = "Blade Runner 224";
    return s;
}

void testStateRoundTrip()
{
    const duskverb::StateValues original = makeState();
    const std::string encoded = duskverb::encodeState(original);

    duskverb::StateValues decoded{};
    checkf(duskverb::decodeState(encoded, decoded), "a freshly encoded state decodes");

    for (int i = 0; i < duskverb::kNumParams; ++i)
    {
        const float a = original.params[static_cast<size_t>(i)];
        const float b = decoded.params[static_cast<size_t>(i)];
        // Bit-exact: the codec carries the IEEE-754 pattern, not a decimal.
        checkf(std::memcmp(&a, &b, sizeof(float)) == 0,
               "'%s' round trips exactly (%.9g -> %.9g)", duskverb::paramDesc(i).id, a, b);
    }
    checkf(decoded.sixAP.densityBaseline == original.sixAP.densityBaseline
        && decoded.sixAP.bloomCeiling == original.sixAP.bloomCeiling
        && decoded.sixAP.earlyMix == original.sixAP.earlyMix
        && decoded.sixAP.outputTrim == original.sixAP.outputTrim,
           "the SixAP scalars round trip");
    for (int i = 0; i < 6; ++i)
        checkf(decoded.sixAP.bloomStagger[i] == original.sixAP.bloomStagger[i],
               "SixAP bloom stagger %d round trips", i);
    checkf(decoded.presetName == original.presetName,
           "the preset identity round trips ('%s')", decoded.presetName.c_str());
}

void testStateRejection()
{
    const std::string good = duskverb::encodeState(makeState());
    duskverb::StateValues out{};

    checkf(!duskverb::decodeState("", out), "an empty state is rejected");
    checkf(!duskverb::decodeState("v=1", out), "a version-only state is rejected");
    checkf(!duskverb::decodeState("v=99;" + good.substr(4), out),
           "an unknown state version is rejected outright");
    checkf(!duskverb::decodeState(good + ";", out), "a trailing separator is rejected");
    checkf(!duskverb::decodeState(good + ";bogus_key=1", out), "an unknown key is rejected");
    checkf(!duskverb::decodeState(good + ";@sixap_bogus=1", out),
           "an unknown '@' key is rejected");

    // A state with one parameter token removed must be refused ENTIRELY rather
    // than applied with a hole: a half-applied state is worse than a rejected one.
    {
        const size_t at = good.find(";width=");
        checkf(at != std::string::npos, "the test state contains a 'width' token");
        if (at != std::string::npos)
        {
            const size_t end = good.find(';', at + 1);
            const std::string missing = good.substr(0, at) + good.substr(end);
            checkf(!duskverb::decodeState(missing, out),
                   "a state missing one parameter is rejected");
        }
    }

    // A duplicate parameter key.
    checkf(!duskverb::decodeState(good + ";mix=0", out), "a duplicate parameter key is rejected");

    // Out-of-range values are refused rather than clamped: a value outside the
    // declared host range did not come from this plugin.
    std::string bad = good;
    const size_t mixAt = bad.find(";mix=");
    checkf(mixAt != std::string::npos, "the test state contains a 'mix' token");
    if (mixAt != std::string::npos)
    {
        const size_t end = bad.find(';', mixAt + 1);
        // 4.0f as hex bits.
        bad = bad.substr(0, mixAt + 5) + "40800000" + bad.substr(end);
        checkf(!duskverb::decodeState(bad, out), "an out-of-range parameter value is rejected");
    }

    // A NaN bit pattern.
    std::string nan = good;
    const size_t sizeAt = nan.find(";size=");
    if (sizeAt != std::string::npos)
    {
        const size_t end = nan.find(';', sizeAt + 1);
        nan = nan.substr(0, sizeAt + 6) + "7fc00000" + nan.substr(end);
        checkf(!duskverb::decodeState(nan, out), "a non-finite parameter value is rejected");
    }
}

// The SixAP brightness block is part of the complete set: the encoder always
// writes all ten keys, so a state missing or repeating one did not come from
// this build and must be refused outright rather than half-applied (the values
// are per-preset engine voicing, not cosmetics).
void testStateSixApCompleteness()
{
    const std::string good = duskverb::encodeState(makeState());
    duskverb::StateValues out{};

    for (const char* key : duskverb::kSixApKeys)
    {
        const std::string tok = std::string(";") + key + "=";
        const size_t at = good.find(tok);
        checkf(at != std::string::npos, "the test state contains '%s'", key);
        if (at == std::string::npos) continue;
        const size_t end = good.find(';', at + 1);
        const std::string missing = good.substr(0, at)
                                  + (end == std::string::npos ? "" : good.substr(end));
        checkf(!duskverb::decodeState(missing, out),
               "a state missing '%s' is rejected", key);
    }

    checkf(!duskverb::decodeState(good + ";@sixap_bloom_ceiling=3f000000", out),
           "a duplicate SixAP key is rejected");
}

void testStateWithoutIdentity()
{
    duskverb::StateValues s = makeState();
    s.presetName.clear();
    duskverb::StateValues out{};
    checkf(duskverb::decodeState(duskverb::encodeState(s), out),
           "a state with no preset identity decodes");
    checkf(out.presetName.empty(), "no identity decodes to an empty preset name");
}

void testProgramApply()
{
    const auto& presets = getFactoryPresets();
    checkf(duskverb::factoryPresetCount() == static_cast<int>(presets.size()),
           "the program count matches the preset table");

    for (const auto& preset : presets)
        checkf(duskverb::factoryPresetByName(preset.name) == &preset,
               "preset '%s' is found by name", preset.name);
    checkf(duskverb::factoryPresetByName("No Such Preset") == nullptr,
           "an unknown preset name resolves to nullptr");

    // Every emitted (index, host value) pair must be inside the declared host
    // range, or a host would clamp it and the preset would not be what shipped.
    for (const auto& preset : presets)
    {
        int emitted = 0;
        duskverb::applyFactoryPresetToHostParameters(preset,
            [&](int index, float host) {
                ++emitted;
                const auto& d = duskverb::paramDesc(index);
                checkf(duskverb::hostValueInRange(d, host),
                       "preset '%s' parameter '%s' host value %g is in [%g, %g]",
                       preset.name, d.id, host, duskverb::hostMin(d), duskverb::hostMax(d));
                checkf(host >= duskverb::hostMin(d) - 1e-6f && host <= duskverb::hostMax(d) + 1e-6f,
                       "preset '%s' parameter '%s' host value %g is in range",
                       preset.name, d.id, host);
            });
        checkf(emitted == 79,
               "preset '%s' emits 79 parameters (got %d)", preset.name, emitted);
    }

    // The 13 parameters a preset deliberately leaves alone. Emitting any of them
    // would reset the user's macro layer or post-tank gains on a preset change —
    // behaviour the JUCE build has never had.
    static const char* const kNotSet[] = {
        "bypass", "bass_choke", "pteq_band0_gain_db", "pteq_band1_gain_db",
        "pteq_band2_gain_db", "pteq_band3_gain_db", "post_band_sub_db",
        "post_band_lowmid_db", "post_band_midhi_db", "post_band_air_db",
        "duck", "tone", "character",
    };
    std::vector<bool> touched(static_cast<size_t>(duskverb::kNumParams), false);
    duskverb::applyFactoryPresetToHostParameters(presets.front(),
        [&](int index, float) { touched[static_cast<size_t>(index)] = true; });
    for (const char* id : kNotSet)
    {
        const int i = duskverb::paramIndexForId(id);
        checkf(i >= 0 && !touched[static_cast<size_t>(i)],
               "a preset apply leaves '%s' alone", id);
    }
}
} // namespace


// The editor's read-outs against the JUCE editor's rules (DuskVerbEditor::
// formatValue and the per-engine valueOverride lambdas), string for string:
// decay switches ms/s at 1 s, frequencies switch Hz/kHz at 1 kHz with two
// decimals below 100 Hz, dB one decimal, multipliers two decimals + "x",
// percentages one decimal, and the Gated / Shimmer engines re-purpose four knobs
// into ms / dB / st / % read-outs.
void testReadoutFormatting()
{
    using duskverb::ui::formatPlainValue;
    using duskverb::ui::labelFor;
    const EngineType hall = EngineType::FDN, gated = EngineType::NonLinear,
                     shimmer = EngineType::Shimmer, spring = EngineType::Spring;
    struct Row { EngineType engine; int param; float value; const char* expected; };
    const Row rows[] = {
        { hall, duskverb::Predelay,     18.8f,     "19 ms" },
        { hall, duskverb::Predelay,     0.0f,      "0 ms" },
        { hall, duskverb::Decay,        0.689f,    "689 ms" },
        { hall, duskverb::Decay,        4.82f,     "4.82 s" },
        { hall, duskverb::Decay,        1.0f,      "1.00 s" },
        { hall, duskverb::LoCut,        30.0f,     "30.00 Hz" },
        { hall, duskverb::Crossover,    392.516f,  "393 Hz" },
        { hall, duskverb::HighCrossover, 2196.6f,  "2.20 kHz" },
        { hall, duskverb::HiCut,        17597.19f, "17.60 kHz" },
        { hall, duskverb::MonoBelow,    20.0f,     "20.00 Hz" },
        { hall, duskverb::ModRate,      1.51f,     "1.51 Hz" },
        { hall, duskverb::GainTrim,     1.9f,      "1.9 dB" },
        { hall, duskverb::GainTrim,     -3.0f,     "-3.0 dB" },
        { hall, duskverb::BassMult,     1.03f,     "1.03x" },
        { hall, duskverb::MidMult,      1.33f,     "1.33x" },
        { hall, duskverb::Damping,      0.5f,      "0.50x" },
        { hall, duskverb::Mix,          0.35f,     "35.0%" },
        { hall, duskverb::Width,        0.98f,     "98.0%" },
        { hall, duskverb::Size,         0.15f,     "15.0%" },
        { hall, duskverb::ModDepth,     0.37f,     "37.0%" },
        { hall, duskverb::Diffusion,    0.58f,     "58.0%" },
        { hall, duskverb::Tone,         0.0f,      "0.00" },
        { hall, duskverb::Tone,         -0.4f,     "-0.40" },
        { hall, duskverb::Character,    0.0f,      "0.0%" },
        // Gated engine: ATTACK 1+depth*49 ms, RELEASE 5+(Hz-0.1)/9.9*1995 ms,
        // HOLD diffusion*500 ms, THRESHOLD -60+(m-0.1)/1.4*60 dB.
        { gated, duskverb::ModDepth,    0.37f,     "19 ms" },
        { gated, duskverb::ModDepth,    0.0f,      "1 ms" },
        { gated, duskverb::ModRate,     1.51f,     "289 ms" },
        { gated, duskverb::ModRate,     10.0f,     "2000 ms" },
        { gated, duskverb::Diffusion,   0.58f,     "290 ms" },
        { gated, duskverb::MidMult,     1.33f,     "-7.3 dB" },
        { gated, duskverb::MidMult,     0.1f,      "-60.0 dB" },
        // Shimmer engine: PITCH depth*24 st with a "+" sign, FEEDBACK 0..95 %.
        { shimmer, duskverb::ModDepth,  0.5f,      "+12 st" },
        { shimmer, duskverb::ModDepth,  0.0f,      "0 st" },
        { shimmer, duskverb::ModRate,   10.0f,     "95 %" },
        { shimmer, duskverb::ModRate,   0.1f,      "0 %" },
        // Shimmer leaves DIFFUSION and MID MULT on their universal formatters.
        { shimmer, duskverb::Diffusion, 0.58f,     "58.0%" },
        { shimmer, duskverb::MidMult,   1.33f,     "1.33x" },
    };
    for (const Row& r : rows)
    {
        char buf[48];
        formatPlainValue(r.engine, r.param, r.value, buf, sizeof(buf));
        checkf(std::strcmp(buf, r.expected) == 0,
               "'%s' at %g prints \"%s\" (got \"%s\")",
               duskverb::paramDesc(r.param).id, r.value, r.expected, buf);
    }

    struct Label { EngineType engine; int param; const char* universal; const char* expected; };
    const Label labels[] = {
        { hall,    duskverb::ModDepth,  "DEPTH",     "DEPTH" },
        { gated,   duskverb::ModDepth,  "DEPTH",     "ATTACK" },
        { gated,   duskverb::ModRate,   "RATE",      "RELEASE" },
        { gated,   duskverb::Diffusion, "DIFFUSION", "HOLD" },
        { gated,   duskverb::MidMult,   "MID MULT",  "THRESHOLD" },
        { shimmer, duskverb::ModDepth,  "DEPTH",     "PITCH" },
        { shimmer, duskverb::ModRate,   "RATE",      "FEEDBACK" },
        { shimmer, duskverb::Diffusion, "DIFFUSION", "DIFFUSION" },
        { spring,  duskverb::ModDepth,  "DEPTH",     "SPRING LEN" },
        { spring,  duskverb::ModRate,   "RATE",      "DRIP" },
        { spring,  duskverb::Diffusion, "DIFFUSION", "CHIRP" },
        { spring,  duskverb::MidMult,   "MID MULT",  "MID MULT" },
    };
    for (const Label& l : labels)
        checkf(std::strcmp(labelFor(l.engine, l.param, l.universal), l.expected) == 0,
               "'%s' on engine %d is labelled %s (got %s)", duskverb::paramDesc(l.param).id,
               (int)l.engine, l.expected, labelFor(l.engine, l.param, l.universal));
}

int main()
{
    testReadoutFormatting();
    testStateRoundTrip();
    testStateRejection();
    testStateSixApCompleteness();
    testStateWithoutIdentity();
    testProgramApply();

    if (failures == 0) std::printf("DuskVerbPluginLayerTests: all checks passed\n");
    else               std::printf("DuskVerbPluginLayerTests: %d check(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
