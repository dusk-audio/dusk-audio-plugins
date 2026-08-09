// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// DistrhoPluginInfo.h — DPF compile-time configuration for PRE-35.
//
// IDs are chosen distinct from every JUCE PLUGIN_CODE (FKEQ/MuCo/Tape/TpEc/MulQ/
// CnvR/DkVb/DkAm/ChAn/ChMi/Grvm/SpAn) and every existing DPF d_cconst in the repo
// (DsFq/DsTM/DsMq/DsTE/DsSC): VST3/AU code DsP3, CLAP id com.duskaudio.pre-35
// (repo com.duskaudio.<slug> convention), LV2 URI
// dusk-audio.github.io/plugins/pre-35.
//
// CHANNEL CONFIGURATION: DPF fixes the port count at compile time — there is no
// per-instantiation bus negotiation in any of its wrappers. The whole Dusk DPF
// fleet therefore ships 2-in/2-out and lets the host adapt a mono track, which is
// what this does too. The DSP core is mono, so the shell runs one instance per
// channel with decorrelated noise seeds; a mono feed duplicated into both inputs
// comes back as a true dual-mono pair, not a doubled single channel.

#pragma once

#define DISTRHO_PLUGIN_BRAND        "Dusk Audio"
#define DISTRHO_PLUGIN_NAME         "PRE-35"
#define DISTRHO_PLUGIN_URI          "https://dusk-audio.github.io/plugins/pre-35"
#define DISTRHO_PLUGIN_CLAP_ID      "com.duskaudio.pre-35"

#define DISTRHO_PLUGIN_BRAND_ID     Dusk
#define DISTRHO_PLUGIN_UNIQUE_ID    DsP3

#define DISTRHO_PLUGIN_NUM_INPUTS   2
#define DISTRHO_PLUGIN_NUM_OUTPUTS  2
#define DISTRHO_PLUGIN_HAS_UI       1
#define DISTRHO_PLUGIN_IS_RT_SAFE   1
// UI reads the output-meter atomics straight from the DSP when same-process (all
// single-binary formats); falls back to the output parameters otherwise.
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1
// The core's two linear-phase resampler stages cost 20 host samples whenever the
// core oversamples, which is every normal rate; above ~344 kHz it stops
// oversampling and the cost is 0 (measured, not assumed). Queried from the core
// per prepare and cleared while bypassed — see Pre35Plugin::updateLatency.
#define DISTRHO_PLUGIN_WANT_LATENCY       1
#define DISTRHO_PLUGIN_WANT_PROGRAMS      0
// One state key, "stateVersion": the parameters ARE the state (the host saves and
// restores them itself), so this exists purely so a future format change has
// something to key a migration off. WANT_FULL_STATE makes the host pull it on save.
#define DISTRHO_PLUGIN_WANT_STATE         1
#define DISTRHO_PLUGIN_WANT_FULL_STATE    1

// Dear ImGui UI via DPF-Widgets.
#define DISTRHO_UI_USE_CUSTOM           1
#define DISTRHO_UI_CUSTOM_INCLUDE_PATH  "DearImGui.hpp"
#define DISTRHO_UI_CUSTOM_WIDGET_TYPE   DGL_NAMESPACE::ImGuiTopLevelWidget
#define DISTRHO_UI_DEFAULT_WIDTH        640
#define DISTRHO_UI_DEFAULT_HEIGHT       380
#define DISTRHO_UI_USER_RESIZABLE       1

#define DISTRHO_PLUGIN_CLAP_FEATURES   "audio-effect", "distortion", "stereo"
#define DISTRHO_PLUGIN_LV2_CATEGORY    "lv2:AmplifierPlugin"
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Distortion|Stereo"
