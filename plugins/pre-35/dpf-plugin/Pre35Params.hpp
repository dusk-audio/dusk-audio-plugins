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
//
// PAD is a tombstone, forced to 0 dB. It is modelled and measured, and the core
// still carries all three positions, but there is no musical reason to expose it
// once the plugin is unity in/out. The pad sits ahead of the amplifier the noise
// is referred to, so engaging it and letting Auto Gain make the level back up
// costs +20 or +40 dB of hiss (measured: -109.5 / -89.6 / -69.7 dBFS at trim 0),
// and it moves the amp's clip point 20-40 dB out of reach, which disables Trim as
// a drive control. The only thing it buys is ~0.5 dB of bass at 20 Hz. Faithful
// to the hardware and strictly worse to use, so position 0 is the fixed choice.

#pragma once

#include <cstdint>

enum ParamId
{
    kPad = 0,       // compatibility tombstone: retained at this index, forced to 0 dB
    kTrim,          // %
    kIron,          // % of the measured transformer (100 % = the device)
    kNoise,         // input-referred noise on/off
    kAutoGain,      // compatibility tombstone: retained at this index, forced on
    kOutput,        // compatibility tombstone: retained at this index, forced to 0 dB
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
    0.0f,     // kPad      — tombstone, forced to 0 dB (see the note above)
    30.0f,    // kTrim     %
    100.0f,   // kIron     % (the measured device)
    0.0f,     // kNoise    off
    1.0f,     // kAutoGain on
    0.0f,     // kOutput   dB
    0.0f,     // kBypass
    0.0f, 0.0f, // kOutPeakL, kOutPeakR (output meters, linear peak)
};

// Host-visible ranges, used by the UI so knob drag ranges cannot drift from
// initParameter(). Meaningless for the two output meters, which are never edited.
static constexpr float kParamMin[kNumInputParams] = { 0.0f, 0.0f,   0.0f,   0.0f, 0.0f, -24.0f, 0.0f };
static constexpr float kParamMax[kNumInputParams] = { 2.0f, 100.0f, 200.0f, 1.0f, 1.0f,  24.0f, 1.0f };

/** Preserve the legacy host parameter ABI while enforcing PRE-35's shipped
    contract. Hosts may restore or automate the old values, but the plugin always
    uses deterministic static level matching, no manual output trim, and no pad.
    Keeping this policy beside the stable ids gives the shell and its regression
    tests one source of truth. */
static constexpr float canonicalPre35InputValue(uint32_t index, float requested) noexcept
{
    return index == kAutoGain ? 1.0f
         : index == kOutput   ? 0.0f
         : index == kPad      ? 0.0f
                              : requested;
}

// Version of the host-persisted state blob. The parameters are saved by the host
// itself; this only tags the save so a future format change can migrate.
static constexpr const char* kStateVersionKey   = "stateVersion";
static constexpr const char* kStateVersionValue = "2";
