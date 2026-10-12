// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// RingOutParams.hpp — host parameter ids and the one table (symbol / range /
// default) shared by initParameter, the UI mirror and the user preset files.
//
// The filters themselves are NOT parameters: the detection engine writes them
// from the audio thread, which no host parameter path allows, so they live in
// plugin state (see RingOutFilterTable.hpp and the "filters" state key).

#pragma once

#include "RingOutFilterTable.hpp"

#include <cstdint>

enum RingOutParamId
{
    kParamSense = 0,     // SENSITIVITY: 0 = Low, 1 = High
    kParamSetup,         // LEARN trigger: arm the detection engine for 60 s, or disarm it
    kParamAdd,           // CATCH trigger: search for one filter (10 s at most)
    kParamReset,         // CLEAR trigger: remove every filter
    kParamGlobalQ,       // Q SCALE: multiplier on every filter's Q, 0.2..10
    kParamGlobalAmp,     // CUT TRIM: offset on every filter's cut, -24..+24 dB
    kParamLink,          // CUT TRIM and OUTPUT move together, in opposite directions
    kParamGainOut,       // OUTPUT trim, -24..+24 dB
    kParamBypass,        // host-designated bypass
    kParamInLevel,       // output parameter: input peak, dBFS
    kParamOutLevel,      // output parameter: output peak, dBFS
    kParamCount
};

// These indices are the saved-session ABI from the first release on. Append
// new parameters after kParamOutLevel; never reorder.
static_assert(kParamSense == 0 && kParamBypass == 8 && kParamOutLevel == 10 && kParamCount == 11,
              "Ring Out parameter layout is append-only");

struct RoParam
{
    enum Kind { kFloat, kBool, kTrigger, kBypass, kOutput };
    const char* id;      // DAF symbol and user-preset file key
    const char* name;
    float min, max, def;
    Kind kind;
};

static constexpr RoParam kRoParams[kParamCount] =
{
    // The symbols are the saved-session and preset-file ABI from the first
    // release and never change; the display names (renamed in 1.1) are free to.
    { "sense",      "Sensitivity",  0.0f, 1.0f, 0.0f, RoParam::kBool },
    // LEARN and CATCH are triggers, momentary actions: a trigger is never part
    // of a saved session, so a project saved while the detector was listening
    // cannot re-arm it on a live PA when it is reloaded. The engine state they
    // act on is read back through the editor.
    { "setup",      "Learn",        0.0f, 1.0f, 0.0f, RoParam::kTrigger },
    { "add",        "Catch",        0.0f, 1.0f, 0.0f, RoParam::kTrigger },
    { "reset",      "Clear",        0.0f, 1.0f, 0.0f, RoParam::kTrigger },
    { "global_q",   "Q Scale",      duskaudio::ringout::kGlobalQMin,   duskaudio::ringout::kGlobalQMax,   duskaudio::ringout::kGlobalQDefault,   RoParam::kFloat },
    { "global_amp", "Cut Trim",     duskaudio::ringout::kGlobalAmpMin, duskaudio::ringout::kGlobalAmpMax, duskaudio::ringout::kGlobalAmpDefault, RoParam::kFloat },
    { "link",       "Link",         0.0f, 1.0f, 0.0f, RoParam::kBool },
    { "gain_out",   "Output",       duskaudio::ringout::kGainOutMin,   duskaudio::ringout::kGainOutMax,   duskaudio::ringout::kGainOutDefault,   RoParam::kFloat },
    { "bypass",     "Bypass",       0.0f, 1.0f, 0.0f, RoParam::kBypass },
    { "in_level",   "Input Level",  -120.0f, 6.0f, -120.0f, RoParam::kOutput },
    { "out_level",  "Output Level", -120.0f, 6.0f, -120.0f, RoParam::kOutput },
};

// What a preset (factory or user) carries: the sound-shaping controls. The
// engine switches, the trigger, the host bypass and the meters are not settings.
inline bool roIsPresetParam(uint32_t index) noexcept
{
    switch (index)
    {
    case kParamSense:
    case kParamGlobalQ:
    case kParamGlobalAmp:
    case kParamLink:
    case kParamGainOut:
        return true;
    default:
        return false;
    }
}

// Clamp + quantise exactly as the plugin applies the value, so the UI cache can
// never disagree with what the DSP acts on.
inline float roNormalizeParamValue(uint32_t index, float v) noexcept
{
    if (index >= (uint32_t)kParamCount) return v;
    const RoParam& p = kRoParams[index];
    v = v < p.min ? p.min : (v > p.max ? p.max : v);
    switch (p.kind)
    {
    case RoParam::kBool:
    case RoParam::kTrigger:
    case RoParam::kBypass:
        return v >= 0.5f ? 1.0f : 0.0f;
    default:
        return v;
    }
}

static constexpr const char* kRoFactoryPresetNames[] = { "Default" };
static constexpr int kRoNumFactoryPresets = 1;
