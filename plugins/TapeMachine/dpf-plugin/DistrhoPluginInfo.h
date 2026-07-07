// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components (DPF — ISC; Dear ImGui — MIT; and others) are attributed
// in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// DistrhoPluginInfo.h — DPF compile-time configuration for TapeMachine 2, the
// DPF successor to the JUCE TapeMachine (v1.x). Distinct IDs from the JUCE build
// (PLUGIN_CODE "Tape", URI .../tapemachine) so both can coexist in a session.

#pragma once

#define DISTRHO_PLUGIN_BRAND        "Dusk Audio"
#define DISTRHO_PLUGIN_NAME         "TapeMachine 2"
#define DISTRHO_PLUGIN_URI          "https://dusk-audio.github.io/plugins/tapemachine-2"
#define DISTRHO_PLUGIN_CLAP_ID      "com.duskaudio.tapemachine2"

#define DISTRHO_PLUGIN_BRAND_ID     Dusk
#define DISTRHO_PLUGIN_UNIQUE_ID    DsTM

#define DISTRHO_PLUGIN_NUM_INPUTS   2
#define DISTRHO_PLUGIN_NUM_OUTPUTS  2
#define DISTRHO_PLUGIN_HAS_UI       1
#define DISTRHO_PLUGIN_IS_RT_SAFE   1
// UI reads the VU meter atomics straight from the DSP when same-process (all
// Linux formats); falls back to the output parameters otherwise.
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1
#define DISTRHO_PLUGIN_WANT_TIMEPOS       0

// Dear ImGui UI via DPF-Widgets: UI base class becomes ImGuiTopLevelWidget.
#define DISTRHO_UI_USE_CUSTOM           1
#define DISTRHO_UI_CUSTOM_INCLUDE_PATH  "DearImGui.hpp"
#define DISTRHO_UI_CUSTOM_WIDGET_TYPE   DGL_NAMESPACE::ImGuiTopLevelWidget
#define DISTRHO_UI_DEFAULT_WIDTH        800
#define DISTRHO_UI_DEFAULT_HEIGHT       568
#define DISTRHO_UI_USER_RESIZABLE       1

// Oversampling adds FIR group delay -> report it to the host.
#define DISTRHO_PLUGIN_WANT_LATENCY  1
#define DISTRHO_PLUGIN_WANT_PROGRAMS 0
#define DISTRHO_PLUGIN_WANT_STATE    0

#define DISTRHO_PLUGIN_CLAP_FEATURES   "audio-effect", "distortion", "filter", "stereo"
#define DISTRHO_PLUGIN_LV2_CATEGORY    "lv2:DistortionPlugin"
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Distortion|Stereo"
