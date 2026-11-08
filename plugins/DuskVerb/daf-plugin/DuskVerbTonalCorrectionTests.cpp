// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Holds DuskVerbTonalCorrection.hpp to the DSP. The editor shows TONAL CORRECTION
// only where tonalCorrectionActs() says the switch changes the sound, so this
// renders the real DSP with the switch off and on, for every preset identity
// (and none) on every engine, and requires the output to differ exactly where
// the rule says it acts and to be bit-identical everywhere else. No list is
// trusted: a new octave curve, a curve keyed to another engine's tank, or an
// engine that starts or stops running accurateHall_ fails here until the rule
// follows.
#include "DuskVerbDSP.hpp"
#include "DuskVerbParamTable.hpp"
#include "DuskVerbParams.hpp"
#include "DuskVerbTonalCorrection.hpp"
#include "FactoryPresets.h"
#include "dsp/AlgorithmConfig.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <stdlib.h>
#define DUSK_TEST_ENVIRON _environ
#else
extern char** environ;
#define DUSK_TEST_ENVIRON environ
#endif

using namespace duskverb;

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 256;
constexpr int kPreroll = 3072;    // 64 ms of silence: a staged preset swaps and fades in (50 ms)
constexpr int kLength = 9600;     // impulse, noise from 25 to 75 ms, tail to 200 ms

int failures = 0;

// The DSP reads its DUSKVERB_* tuning overrides when constructed, and one of them
// (DUSKVERB_AHOCT) gives every identity an octave curve. A test run from a tuning
// shell must still measure the shipping DSP.
void clearTuningEnvironment()
{
    std::vector<std::string> names;
    for (char** entry = DUSK_TEST_ENVIRON; entry != nullptr && *entry != nullptr; ++entry)
    {
        const std::string text(*entry);
        if (text.rfind("DUSKVERB_", 0) == 0) names.push_back(text.substr(0, text.find('=')));
    }
    for (const std::string& name : names)
    {
#if defined(_WIN32)
        _putenv_s(name.c_str(), "");
#else
        unsetenv(name.c_str());
#endif
    }
}

void setHost(DuskVerbDSP& dsp, int index, float hostValue)
{
    const ParamDesc& d = paramDesc(index);
    dsp.setParameter(index, dspFromHost(d, snapHostValue(d, hostValue)));
}

// One render of the stimulus. preset < 0 renders the defaults with no preset
// identity; otherwise the preset is staged the way loadProgram stages it. The
// engine is switched after the load (as the editor's selector does), and the
// switch is set between blocks (as the editor's button does).
std::vector<float> render(int preset, int algorithm, float tonal)
{
    auto storage = std::make_unique<DuskVerbDSP>();
    DuskVerbDSP& dsp = *storage;
    dsp.prepare(kSampleRate, kBlock);
    std::array<float, kNumParams> host{};
    for (int i = 0; i < kNumParams; ++i) host[static_cast<size_t>(i)] = hostDefault(paramDesc(i));
    if (preset >= 0)
    {
        const FactoryPreset& p = dv2FactoryPresets()[static_cast<size_t>(preset)];
        applyFactoryPresetToHostParameters(p, [&](int i, float v) { host[static_cast<size_t>(i)] = v; });
        // Measure the reverb, not the dry copy, and make sure it sounds inside
        // the short window: no pre-delay, no tempo sync, no freeze.
        host[Mix] = plainToHost(paramDesc(Mix), 1.0f);
        host[Predelay] = plainToHost(paramDesc(Predelay), 0.0f);
        host[PredelaySync] = plainToHost(paramDesc(PredelaySync), 0.0f);
        host[Freeze] = plainToHost(paramDesc(Freeze), 0.0f);
        std::array<float, kNumParams> plain{};
        for (int i = 0; i < kNumParams; ++i)
            plain[static_cast<size_t>(i)] = dspFromHost(paramDesc(i), host[static_cast<size_t>(i)]);
        DuskVerbDSP::SixAPBrightnessState sixAP;
        sixAP.densityBaseline = p.sixAPDensityBaseline;
        sixAP.bloomCeiling = p.sixAPBloomCeiling;
        sixAP.earlyMix = p.sixAPEarlyMix;
        sixAP.outputTrim = p.sixAPOutputTrim;
        std::copy_n(p.sixAPBloomStagger, 6, sixAP.bloomStagger);
        dsp.stageSound(plain, &p, sixAP);
    }
    else
    {
        setHost(dsp, Mix, plainToHost(paramDesc(Mix), 1.0f));
        setHost(dsp, Predelay, plainToHost(paramDesc(Predelay), 0.0f));
        setHost(dsp, PredelaySync, plainToHost(paramDesc(PredelaySync), 0.0f));
        setHost(dsp, Freeze, plainToHost(paramDesc(Freeze), 0.0f));
    }

    std::vector<float> inL(kBlock), inR(kBlock), outL(kBlock), outR(kBlock);
    const auto run = [&](int frames, bool stimulus, std::vector<float>* capture) {
        for (int done = 0; done < frames; done += kBlock)
        {
            const int n = std::min(kBlock, frames - done);
            for (int i = 0; i < n; ++i)
            {
                const int t = done + i;
                float x = 0.0f;
                if (stimulus && t == 0) x = 1.0f;
                if (stimulus && t >= 1200 && t < 3600)
                    x = 0.1f * std::sin(0.0137f * static_cast<float>(t))
                      + 0.1f * std::sin(0.000731f * static_cast<float>(t) * static_cast<float>(t % 977));
                // The same signal on both channels: an engine that sums its input
                // to mono (a plate does) would hear silence from an inverted pair.
                inL[static_cast<size_t>(i)] = x;
                inR[static_cast<size_t>(i)] = x;
            }
            const float* in[2] = { inL.data(), inR.data() };
            float* out[2] = { outL.data(), outR.data() };
            dsp.processBlock(in, out, 2, n);
            if (capture != nullptr)
                for (int i = 0; i < n; ++i)
                {
                    capture->push_back(outL[static_cast<size_t>(i)]);
                    capture->push_back(outR[static_cast<size_t>(i)]);
                }
        }
    };
    run(kPreroll, false, nullptr);
    setHost(dsp, Algorithm, static_cast<float>(algorithm));
    run(kPreroll, false, nullptr); // settle crossings to/from the own nonlinear engine
    setHost(dsp, TonalCorrection, tonal);
    std::vector<float> out;
    out.reserve(static_cast<size_t>(kLength) * 2);
    run(kLength, true, &out);
    return out;
}

float maxDifference(const std::vector<float>& a, const std::vector<float>& b)
{
    float worst = 0.0f;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
        worst = std::max(worst, std::fabs(a[i] - b[i]));
    return a.size() == b.size() ? worst : 1.0f;
}

void expect(int preset, int algorithm)
{
    const char* identity = preset >= 0 ? dv2FactoryPresets()[static_cast<size_t>(preset)].name : "";
    const EngineType engine = controlEngine(algorithm);
    const bool rule = algorithm != kNonLinAlgorithm && ui::tonalCorrectionActs(engine, identity);
    const std::vector<float> off = render(preset, algorithm, 0.0f);
    double energy = 0.0;
    for (float x : off) energy += static_cast<double>(x) * x;
    if (energy < 1.0e-6)
    {
        // Silence would read as "inert" and pass whatever the rule said.
        std::printf("FAIL: identity \"%s\" on engine %d: no reverb output to compare (energy %.3g)\n",
                    identity, algorithm, energy);
        ++failures;
        return;
    }
    const float difference = maxDifference(off, render(preset, algorithm, 1.0f));
    const bool acts = difference > 0.0f;   // inert means bit-identical
    if (acts != rule)
    {
        std::printf("FAIL: identity \"%s\" on engine %d (%s): switch %s (max |diff| %.3g), rule says %s\n",
                    identity, algorithm, algorithmName(algorithm),
                    acts ? "acts" : "is inert", static_cast<double>(difference), rule ? "acts" : "inert");
        ++failures;
    }
}
} // namespace

int main()
{
    clearTuningEnvironment();
    const auto& presets = dv2FactoryPresets();
    int hall = -1, vocalPlate = -1;
    for (int i = 0; i < getNumAlgorithms(); ++i)
        if (getAlgorithmConfig(i).engine == EngineType::AccurateHall) hall = i;
    for (size_t i = 0; i < presets.size(); ++i)
        if (std::string(presets[i].name) == "Vocal Plate") vocalPlate = static_cast<int>(i);
    if (hall < 0 || vocalPlate < 0)
    {
        std::printf("FAIL: the Hall engine or the Vocal Plate preset is missing\n");
        return 1;
    }

    // The harness is deterministic, so a difference comes from the switch.
    if (maxDifference(render(vocalPlate, hall, 1.0f), render(vocalPlate, hall, 1.0f)) != 0.0f)
    {
        std::printf("FAIL: two identical renders differ\n");
        ++failures;
    }

    // Every preset identity, and none, on every engine.
    for (int preset = -1; preset < static_cast<int>(presets.size()); ++preset)
        for (int algorithm = 0; algorithm < kNumAlgorithms; ++algorithm)
            expect(preset, algorithm);

    std::printf("DuskVerbTonalCorrectionTests: %s (%d failures)\n",
                failures == 0 ? "all checks passed" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
