// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// Pre35Params.hpp — parameter ids, choice labels and defaults shared by the
// PRE-35 DPF shell and its ImGui UI. One table, so the plugin's value mirror and
// the UI's initial mirror can never drift.
//
// UNITS: every parameter here is in the units the HOST sees, which are not always
// the units pre35::Pre35DSP takes. The two conversions live in
// Pre35Plugin::setParameterValue and are the only place they may live:
//   * Iron  0-200 %       -> setIronAmount(0-2)
//   * Pad   index 0/1/2   -> setPadIndex; the labels are the nominal switch
//                            positions, the modelled offsets are -19.86 / -39.27 dB
//                            (pre35::coeffs::kPads), which is what the bench measured.

#pragma once

enum ParamId
{
    kPad = 0,       // 0 = 0 dB, 1 = -20 dB, 2 = -40 dB (pre35::coeffs::kPads order)
    kTrim,          // %
    kIron,          // % of the measured transformer (100 % = the device)
    kNoise,         // input-referred noise on/off
    kAutoGain,      // cancel the taper+pad gain at the output
    kOutput,        // dB
    kBypass,        // host-designated
    kNumInputParams,
    // output params (meters) — also read directly via the same-process bridge
    kOutPeakL = kNumInputParams,
    kOutPeakR,
    kParamCount
};

// Pad switch positions. NOTE: no label may END in an ASCII double quote — DPF's
// LV2 TTL exporter emits invalid Turtle for those (see the note on
// tmparams::kHeadWidth). These are plain, so nothing special is needed here; the
// rule is recorded because the labels are the natural place to break it.
static constexpr const char* kPadLabels[3] = { "0 dB", "-20 dB", "-40 dB" };
static constexpr int kNumPads = 3;

// Per-parameter defaults, index order = ParamId. The single source of truth for
// both the plugin's values[] seed and the UI's initial mirror.
static constexpr float kParamDefaults[kParamCount] = {
    2.0f,     // kPad      — -40 dB: the safe position for a hot line-level source
    30.0f,    // kTrim     %
    100.0f,   // kIron     % (the measured device)
    0.0f,     // kNoise    off
    0.0f,     // kAutoGain off
    0.0f,     // kOutput   dB
    0.0f,     // kBypass
    0.0f, 0.0f, // kOutPeakL, kOutPeakR (output meters, linear peak)
};

// Host-visible ranges, used by the UI so knob drag ranges cannot drift from
// initParameter(). Meaningless for the two output meters, which are never edited.
static constexpr float kParamMin[kNumInputParams] = { 0.0f, 0.0f,   0.0f,   0.0f, 0.0f, -24.0f, 0.0f };
static constexpr float kParamMax[kNumInputParams] = { 2.0f, 100.0f, 200.0f, 1.0f, 1.0f,  24.0f, 1.0f };

// Version of the host-persisted state blob. The parameters are saved by the host
// itself; this only tags the save so a future format change can migrate.
static constexpr const char* kStateVersionKey   = "stateVersion";
static constexpr const char* kStateVersionValue = "1";
