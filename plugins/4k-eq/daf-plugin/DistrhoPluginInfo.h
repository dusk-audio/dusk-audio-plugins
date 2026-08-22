// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// DistrhoPluginInfo.h — DAF compile-time configuration for 4K EQ 2.

#pragma once

#define DISTRHO_PLUGIN_BRAND        "Dusk Audio"
#define DISTRHO_PLUGIN_NAME         "4K EQ 2"
#define DISTRHO_PLUGIN_URI          "https://dusk-audio.github.io/plugins/4k-eq-2"
#define DISTRHO_PLUGIN_CLAP_ID      "com.duskaudio.fourkeq2"

#define DISTRHO_PLUGIN_BRAND_ID     Dusk
#define DISTRHO_PLUGIN_UNIQUE_ID    DsFq   // distinct from the JUCE build's FKEQ

#define DISTRHO_PLUGIN_NUM_INPUTS   2
#define DISTRHO_PLUGIN_NUM_OUTPUTS  2
// AU hosts filter insert menus by channel layout.  Keep stereo as the default
// while also exposing a true mono instance for mono Logic channel strips.
#define DISTRHO_PLUGIN_EXTRA_IO     { 1, 1 },
#define DISTRHO_PLUGIN_HAS_UI       1
#define DISTRHO_PLUGIN_IS_RT_SAFE   1
// UI reads the meter/spectrum atomics straight from the DSP when same-process
// (all Linux formats); falls back to the output parameter otherwise.
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1
#define DISTRHO_PLUGIN_WANT_LATENCY       1
#define DISTRHO_PLUGIN_WANT_PROGRAMS      1
#define DISTRHO_PLUGIN_WANT_STATE         0

// Dear ImGui UI via DAF-Widgets.
#define DISTRHO_UI_USE_CUSTOM           1
#define DISTRHO_UI_CUSTOM_INCLUDE_PATH  "DearImGui.hpp"
#define DISTRHO_UI_CUSTOM_WIDGET_TYPE   DGL_NAMESPACE::ImGuiTopLevelWidget
#define DISTRHO_UI_DEFAULT_WIDTH        960
#define DISTRHO_UI_DEFAULT_HEIGHT       640
#define DISTRHO_UI_USER_RESIZABLE       1

#define DISTRHO_PLUGIN_CLAP_FEATURES   "audio-effect", "equalizer", "stereo"
#define DISTRHO_PLUGIN_LV2_CATEGORY    "lv2:EQPlugin"
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|EQ|Stereo"
