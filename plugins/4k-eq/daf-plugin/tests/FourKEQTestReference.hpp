// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// The FourKEQDSP core configured directly, as the reference the 4K EQ 2 format
// tests compare the built plugin against, and the pre-#288 sessions they load.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "FourKEQDSP.hpp"

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

// The input parameters of 4K EQ 2 1.0.5, the last build before #288, in the
// order it saved them.
struct LegacySettings
{
    float hpfFreq = 16.f, hpfEnabled = 0.f, lpfFreq = 15201.f, lpfEnabled = 0.f;
    float lfGain = 0.f, lfFreq = 200.f, lfBell = 0.f;
    float lmGain = 0.f, lmFreq = 1000.f, lmQ = 1.5f;
    float hmGain = 0.f, hmFreq = 3000.f, hmQ = 1.5f;
    float hfGain = 0.f, hfFreq = 8000.f, hfBell = 0.f;
    float eqType = 0.f, inputGain = 0.f, outputGain = 0.f, oversampling = 2.f, autoGain = 0.f;
};

// The core, set up the way the plugin sets it up, run pass by pass so a test
// can replay the history a plugin instance went through. Without CoreFilters
// the filters follow the settings' dial positions.
struct CoreBands { float lf, lm, hm, hf; bool hz; };
struct CoreFilters { float hpf, lpf; bool hz; };

inline CoreFilters filterDialsOf(const LegacySettings& s) { return { s.hpfFreq, s.lpfFreq, false }; }

class CoreRunner
{
public:
    CoreRunner(const LegacySettings& s, const CoreBands& bands)
        : CoreRunner(s, bands, filterDialsOf(s)) {}

    CoreRunner(const LegacySettings& s, const CoreBands& bands, const CoreFilters& filters)
    {
        apply(s, bands, filters);
        dsp.prepare(kRate, (int)kBlock);
        apply(s, bands, filters);
    }

    void apply(const LegacySettings& s, const CoreBands& bands) { apply(s, bands, filterDialsOf(s)); }

    void apply(const LegacySettings& s, const CoreBands& bands, const CoreFilters& filters)
    {
        dsp.setHpfEnabled(s.hpfEnabled > 0.5f);
        dsp.setLpfEnabled(s.lpfEnabled > 0.5f);
        if (filters.hz)
        {
            dsp.setHpfFreqHz(filters.hpf); dsp.setLpfFreqHz(filters.lpf);
        }
        else
        {
            dsp.setHpfFreq(filters.hpf); dsp.setLpfFreq(filters.lpf);
        }
        dsp.setLfGain(s.lfGain); dsp.setLfBell(s.lfBell > 0.5f);
        dsp.setLmGain(s.lmGain); dsp.setLmQ(s.lmQ);
        dsp.setHmGain(s.hmGain); dsp.setHmQ(s.hmQ);
        dsp.setHfGain(s.hfGain); dsp.setHfBell(s.hfBell > 0.5f);
        if (bands.hz)
        {
            dsp.setLfFreqHz(bands.lf); dsp.setLmFreqHz(bands.lm);
            dsp.setHmFreqHz(bands.hm); dsp.setHfFreqHz(bands.hf);
        }
        else
        {
            dsp.setLfFreq(bands.lf); dsp.setLmFreq(bands.lm);
            dsp.setHmFreq(bands.hm); dsp.setHfFreq(bands.hf);
        }
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

inline CoreBands dialsOf(const LegacySettings& s) { return { s.lfFreq, s.lmFreq, s.hmFreq, s.hfFreq, false }; }

inline double maxDiff(const std::vector<float>& a, const std::vector<float>& b)
{
    if (a.size() != b.size())
        return 1.0e9;
    double worst = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        worst = std::max(worst, (double)std::abs(a[i] - b[i]));
    return worst;
}

// Pre-#288 sessions: the dial settings they were saved with, chosen to reach
// the ends of every dial and the Hz the Hz parameters cannot hold (Black LF
// shelf past 450 Hz, Brown HF shelf below 1.5 kHz, Black HM past 7 kHz).
inline std::vector<std::pair<const char*, LegacySettings>> legacySessions()
{
    std::vector<std::pair<const char*, LegacySettings>> out;
    LegacySettings a;
    a.lfGain = 6.f; a.lfFreq = 110.f;
    a.lmGain = -4.5f; a.lmFreq = 700.f; a.lmQ = 2.2f;
    a.hmGain = 5.f; a.hmFreq = 4200.f; a.hmQ = 0.9f;
    a.hfGain = 3.f; a.hfFreq = 12000.f;
    a.hpfFreq = 80.f; a.hpfEnabled = 1.f;
    out.push_back({ "Brown, mid-dial", a });

    LegacySettings b = a;
    b.eqType = 1.f; b.lfFreq = 420.f; b.lfGain = 9.f; b.hmFreq = 7000.f; b.hfFreq = 16000.f;
    b.hfBell = 1.f; b.lmFreq = 200.f; b.lpfFreq = 9000.f; b.lpfEnabled = 1.f;
    out.push_back({ "Black, dial ends, HF bell", b });

    LegacySettings c = a;
    c.hfFreq = 2000.f; c.hfGain = 12.f; c.lfBell = 1.f; c.lfFreq = 30.f; c.hmFreq = 600.f;
    c.lmFreq = 2500.f; c.oversampling = 0.f;
    out.push_back({ "Brown HF shelf low, LF bell, 1x", c });

    LegacySettings d = c;
    d.eqType = 1.f; d.hfFreq = 1500.f; d.hfGain = -8.f; d.lfBell = 0.f; d.lfFreq = 450.f;
    d.oversampling = 1.f; d.outputGain = -3.f;
    out.push_back({ "Black shelves at the dial ends, 2x", d });

    // Gains moved, frequency knobs never touched: every dial at 1.0.5's default.
    LegacySettings e;
    e.lfGain = 5.f; e.lmGain = -4.f; e.hmGain = 6.f; e.hfGain = 3.f;
    out.push_back({ "frequency dials at 1.0.5 defaults", e });

    // Both filters switched in and never turned: their dials at 1.0.5's
    // defaults, where they play 9.4 Hz and 29.9 kHz.
    LegacySettings f = e;
    f.hpfEnabled = 1.f; f.lpfEnabled = 1.f;
    out.push_back({ "filters in at 1.0.5's default dials", f });

    LegacySettings g = a;
    g.eqType = 1.f; g.hpfFreq = 350.f; g.lpfFreq = 3000.f; g.lpfEnabled = 1.f; g.oversampling = 0.f;
    out.push_back({ "Black filters at the dial ends, 1x", g });
    return out;
}

} // namespace fourk_test
