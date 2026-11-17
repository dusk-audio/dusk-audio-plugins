// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// The FourKEQDSP core configured directly, as the reference the 4K EQ 2 format
// tests compare the built plugin against, and the 1.0.5 sessions they load.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "FourKEQDSP.hpp"
#include "FourKEQParams.hpp"
#include "FourKEQPresetRuntime.hpp"

namespace fourk_test
{
using duskaudio::FourKEQDSP;

constexpr double kRate = 48000.0;
constexpr uint32_t kBlock = 256;
constexpr int kBlocks = 64;

// Interleaved stereo, kBlocks * kBlock frames.
inline std::vector<float> noise()
{
    std::vector<float> v(2 * kBlock * kBlocks);
    uint32_t x = 0x12345678u;
    for (float& s : v)
    {
        x = x * 1664525u + 1013904223u;
        s = 0.25f * ((float)(x >> 8) / 8388608.0f - 1.0f);
    }
    return v;
}

// The input parameters of 4K EQ 2 1.0.5, in the order it saved them. The six
// frequencies are positions on the console's printed dial.
struct Settings
{
    float hpfFreq = 16.f, hpfEnabled = 0.f, lpfFreq = 15201.f, lpfEnabled = 0.f;
    float lfGain = 0.f, lfFreq = 200.f, lfBell = 0.f;
    float lmGain = 0.f, lmFreq = 1000.f, lmQ = 1.5f;
    float hmGain = 0.f, hmFreq = 3000.f, hmQ = 1.5f;
    float hfGain = 0.f, hfFreq = 8000.f, hfBell = 0.f;
    float eqType = 0.f, inputGain = 0.f, outputGain = 0.f, oversampling = 2.f, autoGain = 0.f;
};

// The core, set up the way the plugin sets it up (every frequency through the
// dial API), run pass by pass so a test can replay the history a plugin
// instance went through.
class CoreRunner
{
public:
    explicit CoreRunner(const Settings& s)
    {
        apply(s);
        dsp.prepare(kRate, (int)kBlock);
        apply(s);
    }

    void apply(const Settings& s)
    {
        dsp.setHpfFreq(s.hpfFreq); dsp.setHpfEnabled(s.hpfEnabled > 0.5f);
        dsp.setLpfFreq(s.lpfFreq); dsp.setLpfEnabled(s.lpfEnabled > 0.5f);
        dsp.setLfGain(s.lfGain); dsp.setLfFreq(s.lfFreq); dsp.setLfBell(s.lfBell > 0.5f);
        dsp.setLmGain(s.lmGain); dsp.setLmFreq(s.lmFreq); dsp.setLmQ(s.lmQ);
        dsp.setHmGain(s.hmGain); dsp.setHmFreq(s.hmFreq); dsp.setHmQ(s.hmQ);
        dsp.setHfGain(s.hfGain); dsp.setHfFreq(s.hfFreq); dsp.setHfBell(s.hfBell > 0.5f);
        dsp.setEqType((int)std::lround(s.eqType));
        dsp.setBypass(false);
        dsp.setInputGainDb(s.inputGain); dsp.setOutputGainDb(s.outputGain);
        dsp.setSaturation(0.0f);
        dsp.setOversampling((int)std::lround(s.oversampling));
        dsp.setMsMode(false);
        dsp.setAutoGain(s.autoGain > 0.5f);
    }

    std::vector<float> render(const std::vector<float>& input)
    {
        std::vector<float> inL(kBlock), inR(kBlock), outL(kBlock), outR(kBlock), result;
        const float* in[2] = { inL.data(), inR.data() };
        float* out[2] = { outL.data(), outR.data() };
        for (int b = 0; b < kBlocks; ++b)
        {
            for (uint32_t i = 0; i < kBlock; ++i)
            {
                inL[i] = input[2 * (b * kBlock + i)];
                inR[i] = input[2 * (b * kBlock + i) + 1];
            }
            dsp.processBlock(in, out, 2, (int)kBlock);
            for (uint32_t i = 0; i < kBlock; ++i)
            {
                result.push_back(outL[i]);
                result.push_back(outR[i]);
            }
        }
        return result;
    }

private:
    FourKEQDSP dsp;
};

inline double maxDiff(const std::vector<float>& a, const std::vector<float>& b)
{
    if (a.size() != b.size())
        return 1.0e9;
    double worst = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        worst = std::max(worst, (double)std::abs(a[i] - b[i]));
    return worst;
}

// Sessions saved by 1.0.5: the settings they were saved with, chosen to reach
// the ends of every dial, both modes, both shapes and every oversampling
// factor.
inline std::vector<std::pair<const char*, Settings>> sessions105()
{
    std::vector<std::pair<const char*, Settings>> out;
    Settings a;
    a.lfGain = 6.f; a.lfFreq = 110.f;
    a.lmGain = -4.5f; a.lmFreq = 700.f; a.lmQ = 2.2f;
    a.hmGain = 5.f; a.hmFreq = 4200.f; a.hmQ = 0.9f;
    a.hfGain = 3.f; a.hfFreq = 12000.f;
    a.hpfFreq = 80.f; a.hpfEnabled = 1.f;
    out.push_back({ "Brown, mid-dial", a });

    Settings b = a;
    b.eqType = 1.f; b.lfFreq = 420.f; b.lfGain = 9.f; b.hmFreq = 7000.f; b.hfFreq = 16000.f;
    b.hfBell = 1.f; b.lmFreq = 200.f; b.lpfFreq = 9000.f; b.lpfEnabled = 1.f;
    out.push_back({ "Black, dial ends, HF bell", b });

    Settings c = a;
    c.hfFreq = 2000.f; c.hfGain = 12.f; c.lfBell = 1.f; c.lfFreq = 30.f; c.hmFreq = 600.f;
    c.lmFreq = 2500.f; c.oversampling = 0.f;
    out.push_back({ "Brown HF shelf low, LF bell, 1x", c });

    Settings d = c;
    d.eqType = 1.f; d.hfFreq = 1500.f; d.hfGain = -8.f; d.lfBell = 0.f; d.lfFreq = 450.f;
    d.oversampling = 1.f; d.outputGain = -3.f;
    out.push_back({ "Black shelves at the dial ends, 2x", d });

    // Gains moved, frequency knobs never touched: every dial at its default.
    Settings e;
    e.lfGain = 5.f; e.lmGain = -4.f; e.hmGain = 6.f; e.hfGain = 3.f;
    out.push_back({ "frequency dials at their defaults", e });

    // Both filters switched in and never turned: their dials at the defaults.
    Settings f = e;
    f.hpfEnabled = 1.f; f.lpfEnabled = 1.f;
    out.push_back({ "filters in at their default dials", f });

    Settings g = a;
    g.eqType = 1.f; g.hpfFreq = 350.f; g.lpfFreq = 3000.f; g.lpfEnabled = 1.f; g.oversampling = 0.f;
    out.push_back({ "Black filters at the dial ends, 1x", g });
    return out;
}

// The six frequency dials each factory preset loads in 4K EQ 2 1.0.5, frozen
// from its build: the programs of the 1.0.5 CLAP and its preset runtime run
// alone agree bit for bit. Parameter order HPF, LPF, LF, LM, HM, HF.
//
// The preset runtime finds each dial by bisection on the core's measured
// law, whose interpolation (y0 + t * (y1 - y0)) a compiler fuses into one
// multiply-add or not. One value depends on that: Bass Guitar Polish's LM
// dial, 0x1.195c78p+8 (281.361206) where the build fuses (Apple clang's
// default on arm64) and 0x1.195c74p+8 (281.361145) where it does not
// (-ffp-contract=off; baseline x86-64 has no fused multiply-add), 2 ulps
// apart. 1.0.5's build loads one or the other; every other value is the same
// both ways.
constexpr uint32_t kPresetDialParams[6] = { kHpfFreq, kLpfFreq, kLfFreq, kLmFreq, kHmFreq, kHfFreq };
struct PresetDials { const char* name; float dial[6]; };
constexpr PresetDials kPresetDials105[] = {
    { "Vocal Presence",
      { 0x1.da5b08p+6f, 0x1.4b6d64p+13f, 0x1.403fb4p+6f, 0x1.806b4p+8f, 0x1.0c7dfcp+12f, 0x1.f4p+13f } },
    { "Kick Punch",
      { 0x1.550cc8p+6f, 0x1.4b6d64p+13f, 0x1.ep+4f, 0x1.9p+7f, 0x1.8fbedcp+10f, 0x1.f4p+13f } },
    { "Snare Crack",
      { 0x1.e73e04p+7f, 0x1.4b6d64p+13f, 0x1.42c82cp+6f, 0x1.19319p+8f, 0x1.5cc57p+12f, 0x1.605ee4p+13f } },
    { "Drum Bus Punch",
      { 0x1p+4f, 0x1.5a6d5p+13f, 0x1.ep+4f, 0x1.6155bp+9f, 0x1.0cc838p+12f, 0x1.f4p+13f } },
    { "Bass Warmth",
      { 0x1.3319d4p+5f, 0x1.f3f034p+12f, 0x1.fde49p+5f, 0x1.24cc4cp+9f, 0x1.4da534p+10f, 0x1.f4p+13f } },
    { "Bass Guitar Polish",
      { 0x1.736454p+6f, 0x1.4b6d64p+13f, 0x1.64796p+5f, 0x1.195c78p+8f, 0x1.1bc6e4p+10f, 0x1.38f79cp+12f } },
    { "Acoustic Guitar",
      { 0x1.da5b08p+6f, 0x1.4b6d64p+13f, 0x1.41ee7cp+6f, 0x1.9p+7f, 0x1.429504p+11f, 0x1.f4p+13f } },
    { "Piano Brilliance",
      { 0x1.550cc8p+6f, 0x1.4b6d64p+13f, 0x1.04a608p+6f, 0x1.64a998p+9f, 0x1.8fbedcp+10f, 0x1.f4p+13f } },
    { "Bright Mix",
      { 0x1.3319d4p+5f, 0x1.4b6d64p+13f, 0x1.701e1cp+5f, 0x1.896d4cp+9f, 0x1.42b78p+11f, 0x1.f4p+13f } },
    { "Glue Bus",
      { 0x1.3319d4p+5f, 0x1.4b6d64p+13f, 0x1.41ee7cp+6f, 0x1.896d4cp+9f, 0x1.bf3c3p+11f, 0x1.f4p+13f } },
    { "Telephone EQ",
      { 0x1.3f3e38p+8f, 0x1.77p+11f, 0x1.42c82cp+6f, 0x1.5df6b8p+10f, 0x1.8fd1f8p+10f, 0x1.f4p+13f } },
    { "Air & Silk",
      { 0x1.3319d4p+5f, 0x1.4b6d64p+13f, 0x1.42c82cp+6f, 0x1.896d4cp+9f, 0x1.b58p+12f, 0x1.f4p+13f } },
    { "Master Sheen",
      { 0x1.3319d4p+5f, 0x1.4b6d64p+13f, 0x1.42c82cp+6f, 0x1.896d4cp+9f, 0x1.5cfdcp+12f, 0x1.f4p+13f } },
    { "Master Bus Sweetening",
      { 0x1.3319d4p+5f, 0x1.4b6d64p+13f, 0x1.26089cp+5f, 0x1.896d4cp+9f, 0x1.2e51a4p+12f, 0x1.f4p+13f } },
};
constexpr int kBassGuitarPolish = 5;
constexpr float kBassGuitarPolishLmUnfused105 = 0x1.195c74p+8f;
static_assert(sizeof(kPresetDials105) / sizeof(kPresetDials105[0]) == 14, "1.0.5 had 14 factory presets");

// Whether `value` is the dial 1.0.5 loaded for preset `index`'s parameter
// kPresetDialParams[column]: exactly, either of the two values a build can
// compute where the fusing above matters.
inline bool isPresetDial105(int index, int column, float value)
{
    return value == kPresetDials105[index].dial[column]
        || (index == kBassGuitarPolish && kPresetDialParams[column] == kLmFreq
            && value == kBassGuitarPolishLmUnfused105);
}

// What factory preset i sets, as settings for the core (oversampling is not a
// preset's: it stays at `oversampling`).
inline Settings presetSettings(int index, float oversampling = 2.f)
{
    Settings s;
    s.oversampling = oversampling;
    forEachFourKEQFactoryPresetParam(index, [&s](uint32_t id, float v) {
        switch (id)
        {
        case kHpfFreq: s.hpfFreq = v; break;
        case kHpfEnabled: s.hpfEnabled = v; break;
        case kLpfFreq: s.lpfFreq = v; break;
        case kLpfEnabled: s.lpfEnabled = v; break;
        case kLfGain: s.lfGain = v; break;
        case kLfFreq: s.lfFreq = v; break;
        case kLfBell: s.lfBell = v; break;
        case kLmGain: s.lmGain = v; break;
        case kLmFreq: s.lmFreq = v; break;
        case kLmQ: s.lmQ = v; break;
        case kHmGain: s.hmGain = v; break;
        case kHmFreq: s.hmFreq = v; break;
        case kHmQ: s.hmQ = v; break;
        case kHfGain: s.hfGain = v; break;
        case kHfFreq: s.hfFreq = v; break;
        case kHfBell: s.hfBell = v; break;
        case kEqType: s.eqType = v; break;
        case kInputGain: s.inputGain = v; break;
        case kOutputGain: s.outputGain = v; break;
        case kAutoGain: s.autoGain = v; break;
        default: break;
        }
    });
    return s;
}

} // namespace fourk_test
