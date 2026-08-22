// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// DafPluginInfo.h — DAF compile-time configuration for 4K EQ 2.

#pragma once

#define DAF_PLUGIN_BRAND        "Dusk Audio"
#define DAF_PLUGIN_NAME         "4K EQ 2"
#define DAF_PLUGIN_URI          "https://dusk-audio.github.io/plugins/4k-eq-2"
#define DAF_PLUGIN_CLAP_ID      "com.duskaudio.fourkeq2"

#define DAF_PLUGIN_BRAND_ID     Dusk
#define DAF_PLUGIN_UNIQUE_ID    DsFq   // distinct from the JUCE build's FKEQ

#define DAF_PLUGIN_NUM_INPUTS   2
#define DAF_PLUGIN_NUM_OUTPUTS  2
// AU hosts filter insert menus by channel layout.  Keep stereo as the default
// while also exposing a true mono instance for mono Logic channel strips.
#define DAF_PLUGIN_EXTRA_IO     { 1, 1 },
#define DAF_PLUGIN_HAS_UI       1
#define DAF_PLUGIN_IS_RT_SAFE   1
// UI reads the meter/spectrum atomics straight from the DSP when same-process
// (all Linux formats); falls back to the output parameter otherwise.
#define DAF_PLUGIN_WANT_DIRECT_ACCESS 1
#define DAF_PLUGIN_WANT_LATENCY       1
#define DAF_PLUGIN_WANT_PROGRAMS      1
#define DAF_PLUGIN_WANT_STATE         0

// Dear ImGui UI via DAF-Widgets.
#define DAF_UI_USE_CUSTOM           1
#define DAF_UI_CUSTOM_INCLUDE_PATH  "DearImGui.hpp"
#define DAF_UI_CUSTOM_WIDGET_TYPE   DGL_NAMESPACE::ImGuiTopLevelWidget
#define DAF_UI_DEFAULT_WIDTH        960
#define DAF_UI_DEFAULT_HEIGHT       640
#define DAF_UI_USER_RESIZABLE       1

#define DAF_PLUGIN_CLAP_FEATURES   "audio-effect", "equalizer", "stereo"
#define DAF_PLUGIN_LV2_CATEGORY    "lv2:EQPlugin"
#define DAF_PLUGIN_VST3_CATEGORIES "Fx|EQ|Stereo"
