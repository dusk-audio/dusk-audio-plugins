// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// FourKEQParams.hpp — parameter ids, choice labels and factory presets shared
// by the 4K EQ 2 DAF shell and its ImGui UI. Names / ranges / defaults mirror
// the JUCE FourKEQ::createParameterLayout exactly.

#pragma once

#include <cstdint>

enum ParamId
{
    kHpfFreq = 0, kHpfEnabled,
    kLpfFreq, kLpfEnabled,
    kLfGain, kLfFreq, kLfBell,
    kLmGain, kLmFreq, kLmQ,
    kHmGain, kHmFreq, kHmQ,
    kHfGain, kHfFreq, kHfBell,
    kEqType,        // 0 = Brown (E-series), 1 = Black (G-series)
    kBypass,        // host-designated
    kInputGain, kOutputGain,
    // Retained at its shipped index so existing sessions keep every following
    // parameter aligned. 4K EQ 2 no longer exposes or acts on a separate drive
    // amount: the modeled SSL path has fixed native nonlinearity and is driven
    // by Input Gain, like the reference channel strip.
    kSaturation,
    kOversampling,  // 0 = 1x (off), 1 = 2x, 2 = 4x
    kMsMode,
    kSpectrumPrePost, // 0 = post-EQ, 1 = pre-EQ (UI analyzer source)
    kAutoGain,
    kShowGraph,       // UI-only: response graph shown (1) / collapsed (0); persists
    kNumInputParams,
    // output params (meters) — also read directly via the same-process bridge
    kOutPeakL = kNumInputParams,
    kOutPeakR,
    kParamCount
};

static constexpr const char* kEqTypeLabels[2]      = { "Brown", "Black" };
static constexpr const char* kOversampleLabels[3]  = { "1x", "2x", "4x" };

// Per-parameter key + range + default, index order = ParamId. The single source
// of truth shared by the plugin (initParameter symbols/ranges, values[] seed),
// the UI's initial mirror, and the user-preset file format (key=value lines) —
// so a saved preset's keys can never drift from the host-visible symbols.
struct FourKParam
{
    const char* key;   // host parameter symbol AND user-preset file key
    float min, max, def;
};

static constexpr FourKParam kFourKParams[kParamCount] = {
    { "hpf_freq",         16.f,    350.f,   16.f    },
    { "hpf_enabled",      0.f,     1.f,     0.f     },
    { "lpf_freq",         3000.f,  15201.f, 15201.f },
    { "lpf_enabled",      0.f,     1.f,     0.f     },
    { "lf_gain",          -15.f,   15.f,    0.f     },
    { "lf_freq",          30.f,    450.f,   200.f   },
    { "lf_bell",          0.f,     1.f,     0.f     },
    { "lm_gain",          -15.f,   15.f,    0.f     },
    { "lm_freq",          200.f,   2500.f,  1000.f  },
    { "lm_q",             0.5f,    3.f,     1.5f    },
    { "hm_gain",          -15.f,   15.f,    0.f     },
    { "hm_freq",          600.f,   7000.f,  3000.f  },
    { "hm_q",             0.5f,    3.f,     1.5f    },
    { "hf_gain",          -15.f,   15.f,    0.f     },
    { "hf_freq",          1500.f,  16000.f, 8000.f  },
    { "hf_bell",          0.f,     1.f,     0.f     },
    { "eq_type",          0.f,     1.f,     0.f     }, // Brown
    { "bypass",           0.f,     1.f,     0.f     },
    { "input_gain",       -12.f,   12.f,    0.f     },
    { "output_gain",      -12.f,   12.f,    0.f     },
    { "saturation",       0.f,     100.f,   0.f     }, // deprecated compatibility slot
    { "oversampling",     0.f,     2.f,     2.f     }, // 4x
    { "ms_mode",          0.f,     1.f,     0.f     },
    { "spectrum_prepost", 0.f,     1.f,     0.f     },
    { "auto_gain",        0.f,     1.f,     0.f     }, // off; reference has no compensation stage
    { "show_graph",       0.f,     1.f,     1.f     }, // shown
    { "out_peak_l",       0.f,     2.f,     0.f     },
    { "out_peak_r",       0.f,     2.f,     0.f     },
};

// Sound-shaping parameters a preset owns (saved to user preset files, compared
// for preset-identity recovery, reset by INIT). Excluded: bypass (a preset
// recall must never fight the host's bypass state), oversampling (machine-level
// quality/CPU choice), the analyzer/graph UI state, and the meter outputs.
constexpr bool fkIsPresetParam(uint32_t index)
{
    switch (index)
    {
    case kBypass:
    case kSaturation:
    case kOversampling:
    case kMsMode: // retired compatibility slot; DSP is always normal stereo
    case kSpectrumPrePost:
    case kShowGraph:
    case kOutPeakL:
    case kOutPeakR:
        return false;
    default:
        return index < (uint32_t)kNumInputParams;
    }
}

// Factory presets: same musical targets as the JUCE FourKEQPresets. Frequency
// fields are authored in audible/effective Hz; FourKEQPresetRuntime converts
// them to calibrated host-control coordinates when recalled. HPF/LPF are
// auto-enabled when their target departs from the neutral 16 Hz / 15.201 kHz
// endpoints (JUCE left the enables untouched, making "Telephone EQ" inert).
struct FourKEQPreset
{
    const char* name;
    const char* category;
    float lfGain, lfFreq, lfBell;
    float lmGain, lmFreq, lmQ;
    float hmGain, hmFreq, hmQ;
    float hfGain, hfFreq, hfBell;
    float hpfFreq, lpfFreq;
    float legacySaturation, outputGain, inputGain, eqType;
};

static constexpr FourKEQPreset kFactoryPresets[] =
{
    { "Vocal Presence", "Vocals",
      3.0f,100.f,0.f, -3.0f,300.f,1.3f, 4.0f,3500.f,0.7f, 2.0f,8000.f,0.f, 80.f,15201.f, 0.f,0.f,0.f,0.f },
    { "Kick Punch", "Drums",
      4.0f,50.f,0.f, -2.5f,200.f,0.8f, 3.0f,2000.f,1.5f, 0.0f,8000.f,0.f, 30.f,15201.f, 0.f,0.f,0.f,0.f },
    { "Snare Crack", "Drums",
      0.0f,100.f,0.f, 4.0f,250.f,0.7f, 5.0f,5000.f,1.2f, 3.0f,8000.f,1.f, 150.f,15201.f, 0.f,0.f,0.f,0.f },
    { "Drum Bus Punch", "Drums",
      4.0f,70.f,0.f, -3.0f,350.f,0.6f, 3.0f,3500.f,1.0f, 2.5f,10000.f,0.f, 16.f,15201.f, 25.f,0.f,0.f,1.f },
    { "Bass Warmth", "Bass",
      4.0f,80.f,0.f, -3.0f,400.f,0.7f, 2.0f,1500.f,0.7f, 0.0f,8000.f,0.f, 16.f,10000.f, 0.f,0.f,0.f,0.f },
    { "Bass Guitar Polish", "Bass",
      5.0f,60.f,0.f, -2.0f,250.f,1.0f, 3.0f,1200.f,0.8f, 2.0f,4500.f,1.f, 35.f,15201.f, 0.f,0.f,0.f,0.f },
    { "Acoustic Guitar", "Guitar",
      -2.0f,100.f,0.f, 2.0f,200.f,0.7f, 3.0f,2500.f,0.9f, 4.0f,12000.f,0.f, 80.f,15201.f, 0.f,0.f,0.f,0.f },
    { "Piano Brilliance", "Keys",
      2.0f,80.f,0.f, -2.5f,500.f,0.8f, 3.0f,2000.f,0.7f, 3.5f,8000.f,0.f, 30.f,15201.f, 0.f,0.f,0.f,0.f },
    { "Bright Mix", "Mix Bus",
      2.0f,60.f,0.f, 0.0f,600.f,0.7f, -2.0f,2500.f,0.8f, 2.5f,10000.f,0.f, 16.f,15201.f, 20.f,0.f,0.f,0.f },
    { "Glue Bus", "Mix Bus",
      2.0f,100.f,0.f, 0.0f,600.f,0.7f, -1.5f,3000.f,0.7f, 2.0f,10000.f,0.f, 16.f,15201.f, 20.f,0.f,0.f,0.f },
    { "Telephone EQ", "Creative",
      0.0f,100.f,0.f, 6.0f,1000.f,1.5f, 0.0f,2000.f,0.7f, 0.0f,8000.f,0.f, 300.f,3000.f, 0.f,0.f,0.f,0.f },
    { "Air & Silk", "Creative",
      0.0f,100.f,0.f, 0.0f,600.f,0.7f, 3.0f,7000.f,0.7f, 4.0f,15000.f,0.f, 16.f,15201.f, 0.f,0.f,0.f,0.f },
    { "Master Sheen", "Mastering",
      0.0f,100.f,0.f, 0.0f,600.f,0.7f, 1.0f,5000.f,0.7f, 1.5f,16000.f,0.f, 16.f,15201.f, 10.f,0.f,0.f,0.f },
    { "Master Bus Sweetening", "Mastering",
      1.0f,50.f,0.f, -1.0f,600.f,0.5f, 0.5f,4000.f,0.6f, 1.5f,15000.f,0.f, 16.f,15201.f, 15.f,-0.5f,0.f,0.f },
};
static constexpr int kNumFactoryPresets = (int)(sizeof(kFactoryPresets) / sizeof(kFactoryPresets[0]));
