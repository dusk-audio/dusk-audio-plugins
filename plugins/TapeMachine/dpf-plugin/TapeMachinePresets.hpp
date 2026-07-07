// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// TapeMachinePresets.hpp — factory presets, ported verbatim from the JUCE
// TapeMachine build (plugins/TapeMachine/Source/TapeMachinePresets.h). Shared by
// the DPF program interface (host preset menu) and the UI preset combo. Presets
// only touch the parameters the JUCE presets set; everything else keeps default.

#pragma once

#include "TapeMachineParams.hpp"

struct TmPreset
{
    const char* name;
    const char* category;
    int   tapeMachine, tapeSpeed, tapeType;
    float inputGain, outputGain, bias;
    bool  autoComp;
    float highpassFreq, lowpassFreq, wow, flutter, noiseAmount;
    bool  noiseEnabled;
};

static constexpr TmPreset kTmPresets[] =
{
    // name                 category      mch spd typ  in    out  bias  aComp  hp     lp      wow   flt   noise  nEn
    { "Gentle Warmth",     "Subtle",      0,  2,  3,   2.f,  0.f, 50.f, true,  20.f,  20000.f, 3.f,  1.f,  0.f,   false },
    { "Transparent Glue",  "Subtle",      0,  2,  1,   3.f,  0.f, 55.f, true,  20.f,  18000.f, 2.f,  1.f,  0.f,   false },
    { "Mastering Touch",   "Subtle",      0,  2,  3,   1.f,  0.f, 50.f, true,  20.f,  20000.f, 1.f,  0.5f, 0.f,   false },
    { "Classic Analog",    "Warm",        1,  1,  0,   5.f,  0.f, 50.f, true,  30.f,  16000.f, 7.f,  3.f,  5.f,   false },
    { "Vintage Warmth",    "Warm",        1,  0,  0,   6.f,  0.f, 45.f, true,  40.f,  14000.f, 10.f, 5.f,  8.f,   false },
    { "Tube Console",      "Warm",        1,  1,  2,   7.f,  0.f, 48.f, true,  25.f,  15000.f, 5.f,  2.f,  3.f,   false },
    { "70s Rock",          "Character",   1,  1,  0,   8.f,  0.f, 42.f, true,  50.f,  12000.f, 12.f, 6.f,  10.f,  true  },
    { "Tape Saturation",   "Character",   1,  1,  0,   10.f, 0.f, 40.f, true,  30.f,  14000.f, 8.f,  4.f,  5.f,   false },
    { "Cassette Deck",     "Character",   1,  0,  2,   6.f,  0.f, 55.f, true,  60.f,  10000.f, 15.f, 8.f,  15.f,  true  },
    { "Lo-Fi Warble",      "Lo-Fi",       1,  0,  0,   8.f,  0.f, 38.f, true,  80.f,  8000.f,  25.f, 12.f, 20.f,  true  },
    { "Worn Tape",         "Lo-Fi",       1,  0,  2,   5.f,  0.f, 35.f, true,  100.f, 6000.f,  30.f, 15.f, 30.f,  true  },
    { "Dusty Reel",        "Lo-Fi",       1,  0,  0,   4.f,  0.f, 42.f, true,  70.f,  9000.f,  20.f, 10.f, 40.f,  true  },
    { "Master Bus Glue",   "Mastering",   0,  2,  3,   2.f,  0.f, 52.f, true,  20.f,  20000.f, 2.f,  1.f,  0.f,   false },
    { "Analog Sheen",      "Mastering",   0,  2,  1,   3.f,  0.f, 50.f, true,  20.f,  18000.f, 3.f,  1.5f, 0.f,   false },
    { "Vintage Master",    "Mastering",   0,  1,  0,   4.f,  0.f, 48.f, true,  25.f,  16000.f, 5.f,  2.f,  2.f,   false },
};

static constexpr int kNumTmPresets = (int)(sizeof(kTmPresets) / sizeof(kTmPresets[0]));

// Apply a preset by invoking setter(paramId, value) for each parameter it owns.
// Used by both the plugin (loadProgram) and the UI (preset combo) so the two
// stay in lockstep. Parameters not listed keep their current value.
template <class SetFn>
inline void tmApplyPreset(int idx, SetFn&& set)
{
    if (idx < 0 || idx >= kNumTmPresets) return;
    const TmPreset& p = kTmPresets[idx];
    set(kParamTapeMachine,  (float)p.tapeMachine);
    set(kParamTapeSpeed,    (float)p.tapeSpeed);
    set(kParamTapeType,     (float)p.tapeType);
    set(kParamInputGain,    p.inputGain);
    set(kParamOutputGain,   p.outputGain);
    set(kParamBias,         p.bias);
    set(kParamAutoComp,     p.autoComp ? 1.f : 0.f);
    set(kParamHighpassFreq, p.highpassFreq);
    set(kParamLowpassFreq,  p.lowpassFreq);
    set(kParamWow,          p.wow);
    set(kParamFlutter,      p.flutter);
    set(kParamNoiseAmount,  p.noiseAmount);
    set(kParamNoiseEnabled, p.noiseEnabled ? 1.f : 0.f);
}
