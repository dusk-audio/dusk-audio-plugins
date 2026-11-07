// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// Every control the editor draws either changes the sound on the engine it is used with, or the
// editor draws it dimmed. The declaration it dims by (DuskVerbKnobLiveness.hpp controlLive) is
// checked here in both directions: a control declared live must change the render, one declared
// dead must leave it bit-identical. Every factory program is checked on its own engine, and
// every engine with one program forced onto it, so a declaration for an engine no program uses
// cannot go stale either.
//
// Each control is moved to either end of its range (a switch flips) from a base that lets it act
// at all: a crossover between two bands that differ, Mono Below with a depth and the depth with a
// cutoff, Bus with a mix under 100 %, the ER size with early reflections up, the modulation rate
// with some depth and the depth with a fast rate, the pre-delay knob with sync off. Both ends, because a control can sit at a
// clamp in one direction (Tone cannot brighten a preset whose Hi Cut is already at the top). The
// stimulus is a full-scale 50 ms noise burst, short enough that the Gated engine's release shows
// inside the render. Saturation (and Character, which adds to it) gets 0.4 s at +18 dBFS
// instead: its soft clip is calibrated hot (see DuskVerbKnobLiveness.hpp) and only engages once
// a sustained, hot input has built the tank up.

#include "DuskVerbDSP.hpp"
#include "DuskVerbDampingScaler.hpp"
#include "DuskVerbKnobLiveness.hpp"
#include "DuskVerbParamTable.hpp"
#include "DuskVerbPresetEngineConfig.h"
#include "DuskVerbPresets.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <utility>
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

using namespace duskverb;

constexpr double kSr = 48000.0;
constexpr int kBlock = 256;
constexpr int kRenderSamples = 24000;   // 0.5 s: past every engine's first tank pass

// What the render is driven with.
enum class Stimulus { Burst, Hot };
struct Drive { int samples; float gain; };
constexpr Drive driveFor(Stimulus s) { return s == Stimulus::Burst ? Drive { 2400, 1.0f } : Drive { 19200, 8.0f }; }

using Overrides = std::vector<std::pair<int, float>>;   // (parameter, PLAIN value)

const FactoryPreset& presetNamed(const char* name)
{
    for (const auto& preset : dv2FactoryPresets())
        if (std::strcmp(preset.name, name) == 0)
            return preset;
    std::printf("FAIL: no factory preset named '%s'\n", name);
    std::exit(1);
}

float dspValue(int index, float plain)
{
    const ParamDesc& d = paramDesc(index);
    return dspFromHost(d, plainToHost(d, plain));
}

// The response (left, then right) of `preset` at 100 % wet to `stimulus`, with `overrides`
// written over the preset's own values, as a recalled session that carries them would be.
// Renders are cached: most controls share their base render.
class Renderer
{
public:
    explicit Renderer(const FactoryPreset& preset) : preset_(preset) {}

    const std::vector<float>& render(const Overrides& overrides, Stimulus stimulus)
    {
        const auto key = std::make_pair(overrides, stimulus);
        auto it = cache_.find(key);
        if (it == cache_.end())
            it = cache_.emplace(key, renderNow(overrides, driveFor(stimulus))).first;
        return it->second;
    }

private:
    std::vector<float> renderNow(const Overrides& overrides, Drive drive) const
    {
        auto dsp = std::make_unique<DuskVerbDSP>();
        dsp->prepare(kSr, kBlock);
        collectDv2Parameters(preset_, [&](const char* id, float v) {
            const int i = paramIndexForId(id);
            if (i >= 0) dsp->setParameter(i, dspValue(i, v));
        });
        dsp->setParameter(Mix, dspValue(Mix, 1.0f));
        for (const auto& [index, plain] : overrides)
            dsp->setParameter(index, dspValue(index, plain));
        dsp->applyFactoryPresetConfig(preset_);

        std::vector<float> l(kRenderSamples, 0.0f), r(kRenderSamples, 0.0f);
        uint32_t seed = 0x2545F491u;
        for (int i = 0; i < drive.samples; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            l[i] = drive.gain * ((float) (seed >> 8) / 8388608.0f - 1.0f);
            seed = seed * 1664525u + 1013904223u;
            r[i] = drive.gain * ((float) (seed >> 8) / 8388608.0f - 1.0f);
        }
        for (int off = 0; off < kRenderSamples; off += kBlock)
        {
            const int n = std::min(kBlock, kRenderSamples - off);
            const float* in[2] = { l.data() + off, r.data() + off };
            float* out[2] = { l.data() + off, r.data() + off };
            dsp->processBlock(in, out, 2, n);
        }
        l.insert(l.end(), r.begin(), r.end());
        return l;
    }

    const FactoryPreset& preset_;
    std::map<std::pair<Overrides, Stimulus>, std::vector<float>> cache_;
};

// The value a control holds before it is moved: the base override if there is one, the
// harness's 100 % wet for Mix, the preset's own value, else the parameter default.
float baseValue(const FactoryPreset& preset, const Overrides& base, int index)
{
    for (auto it = base.rbegin(); it != base.rend(); ++it)
        if (it->first == index) return it->second;
    if (index == Mix) return 1.0f;
    float value = paramDesc(index).def;
    collectDv2Parameters(preset, [&](const char* id, float v) {
        if (paramIndexForId(id) == index) value = v;
    });
    // What the DSP holds: a stored value outside the range arrives as its clamp.
    return std::clamp(value, paramDesc(index).min, paramDesc(index).max);
}

// Where a control is moved to: a switch flips; anything else goes to each end of its range that
// it is not already at.
std::vector<float> movedValues(int index, float from)
{
    const ParamDesc& d = paramDesc(index);
    if (d.integer) return { from >= 0.5f ? 0.0f : 1.0f };
    std::vector<float> out;
    if (from != d.min) out.push_back(d.min);
    if (from != d.max) out.push_back(d.max);
    return out;
}

// What a control needs set first to be able to act at all (applied to BOTH renders).
Overrides setupFor(const FactoryPreset& preset, int control)
{
    const DampingKnobs stored = storedDampingKnobs(preset);
    switch (control)
    {
        // A crossover only acts between two bands that differ: the band on its far side is
        // pulled well away from Mid (the geometric middles of 0.3..2.5 and 0.1..1.5).
        case Crossover:      return { { BassMult, stored.mid < 0.866f ? 2.5f : 0.3f } };
        case HighCrossover:  return { { Damping, stored.mid < 0.387f ? 1.5f : 0.1f } };
        case MonoBelow:      return { { MonoBelowDepth, 1.0f } };
        case MonoBelowDepth: return { { MonoBelow, 150.0f } };
        case BusMode:        return { { Mix, 0.5f } };
        case ErSize:         return { { ErLevel, 0.5f } };
        case ModRate:        return { { ModDepth, 0.5f } };
        // The tank LFOs are random walks that start at rest and take their first step one
        // period in, which at a preset's slow rate is past the render: depth (and Character,
        // which adds to it) can only show with the walk already moving.
        case ModDepth:
        case Character:      return { { ModRate, 10.0f } };
        case Predelay:       return { { PredelaySync, 0.0f } };
        default:             return {};
    }
}

// Every control on the panel: knobs, graph handles and switches.
constexpr int kPanelControls[] = {
    Predelay, Size, Diffusion, Decay, Freeze, Width, GainTrim, Mix, BusMode,
    LoCut, BassMult, Crossover, MidMult, HighCrossover, Damping, HiCut, Tone, TonalCorrection,
    ErLevel, ErSize, ModDepth, ModRate, GateEnabled, MonoBelow, MonoBelowDepth,
    Saturation, Character, Duck,
};

// Moves every panel control on `preset` (with Algorithm forced to `algorithm` when >= 0) and
// holds the render against controlLive.
void checkPanelAgainstLiveness(const FactoryPreset& preset, int algorithm, const char* label)
{
    const int engine = algorithm >= 0 ? algorithm : preset.algorithm;
    Renderer renderer(preset);
    for (int control : kPanelControls)
    {
        Overrides base = algorithm >= 0 ? Overrides { { Algorithm, (float) algorithm } } : Overrides {};
        const Overrides setup = setupFor(preset, control);
        base.insert(base.end(), setup.begin(), setup.end());
        const Stimulus stimulus = control == Saturation || control == Character ? Stimulus::Hot : Stimulus::Burst;
        bool differs = false;
        for (float to : movedValues(control, baseValue(preset, base, control)))
        {
            Overrides changed = base;
            changed.push_back({ control, to });
            differs = differs || renderer.render(base, stimulus) != renderer.render(changed, stimulus);
        }
        const bool live = controlLive(engine, control);
        checkf(differs == live, "%s (engine %d '%s'): %s is declared %s but moving it %s the render",
               label, engine, algorithmName(engine), paramDesc(control).name, live ? "live" : "dead",
               differs ? "changes" : "does not change");
    }
}

void testEveryProgram()
{
    for (const auto& preset : dv2FactoryPresets())
        checkPanelAgainstLiveness(preset, -1, preset.name);
}

void testEveryEngine()
{
    const FactoryPreset& vocalPlate = presetNamed("Vocal Plate");
    for (int algorithm = 0; algorithm < kNumAlgorithms; ++algorithm)
    {
        char label[64];
        std::snprintf(label, sizeof(label), "Vocal Plate on engine %d", algorithm);
        checkPanelAgainstLiveness(vocalPlate, algorithm, label);
    }
}
} // namespace

int main()
{
    primeTuningEnvCache();
    testEveryProgram();
    testEveryEngine();

    if (failures == 0) std::printf("DuskVerbControlLivenessTests: all checks passed\n");
    else               std::printf("DuskVerbControlLivenessTests: %d check(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
