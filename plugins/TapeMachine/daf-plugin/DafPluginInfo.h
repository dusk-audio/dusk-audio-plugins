// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components (DAF — ISC; Dear ImGui — MIT; and others) are attributed
// in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// DafPluginInfo.h — DAF compile-time configuration for TapeMachine 2, the
// DAF successor to the JUCE TapeMachine (v1.x). Distinct IDs from the JUCE build
// (PLUGIN_CODE "Tape", URI .../tapemachine) so both can coexist in a session.

#pragma once

#define DAF_PLUGIN_BRAND        "Dusk Audio"
#define DAF_PLUGIN_NAME         "TapeMachine 2"
#define DAF_PLUGIN_URI          "https://dusk-audio.github.io/plugins/tapemachine-2"
#define DAF_PLUGIN_CLAP_ID      "com.duskaudio.tapemachine2"

#define DAF_PLUGIN_BRAND_ID     Dusk
#define DAF_PLUGIN_UNIQUE_ID    DsTM

#define DAF_PLUGIN_NUM_INPUTS   2
#define DAF_PLUGIN_NUM_OUTPUTS  2
// AU hosts filter insert menus by channel layout.  Keep stereo as the default
// while also exposing a true mono instance for mono Logic channel strips.
#define DAF_PLUGIN_EXTRA_IO     { 1, 1 },
#define DAF_PLUGIN_HAS_UI       1
#define DAF_PLUGIN_IS_RT_SAFE   1
// UI reads the VU meter atomics straight from the DSP when same-process (all
// Linux formats); falls back to the output parameters otherwise.
#define DAF_PLUGIN_WANT_DIRECT_ACCESS 1
#define DAF_PLUGIN_WANT_TIMEPOS       0

// Dear ImGui UI via DAF-Widgets: UI base class becomes ImGuiTopLevelWidget.
#define DAF_UI_USE_CUSTOM           1
#define DAF_UI_CUSTOM_INCLUDE_PATH  "DearImGui.hpp"
#define DAF_UI_CUSTOM_WIDGET_TYPE   DGL_NAMESPACE::ImGuiTopLevelWidget
#define DAF_UI_DEFAULT_WIDTH        800
#define DAF_UI_DEFAULT_HEIGHT       470
#define DAF_UI_USER_RESIZABLE       1

// Oversampling adds FIR group delay -> report it to the host.
#define DAF_PLUGIN_WANT_LATENCY  1
#define DAF_PLUGIN_WANT_PROGRAMS 1
#define DAF_PLUGIN_WANT_STATE    0

#define DAF_PLUGIN_CLAP_FEATURES   "audio-effect", "distortion", "filter", "stereo"
#define DAF_PLUGIN_LV2_CATEGORY    "lv2:DistortionPlugin"
#define DAF_PLUGIN_VST3_CATEGORIES "Fx|Distortion|Stereo"
