// DafPluginInfo.h — DAF compile-time plugin configuration for Tape Echo.

#pragma once

#define DAF_PLUGIN_BRAND        "Dusk Audio"
#define DAF_PLUGIN_NAME         "Tape Echo 2"
#define DAF_PLUGIN_URI          "https://dusk-audio.github.io/plugins/tape-echo"
#define DAF_PLUGIN_CLAP_ID      "com.duskaudio.tape-echo"

#define DAF_PLUGIN_BRAND_ID     Dusk
#define DAF_PLUGIN_UNIQUE_ID    DsTE

#define DAF_PLUGIN_NUM_INPUTS   2
#define DAF_PLUGIN_NUM_OUTPUTS  2
// AU hosts filter insert menus by channel layout.  Keep stereo as the default
// while also exposing a true mono instance for mono Logic channel strips.
#define DAF_PLUGIN_EXTRA_IO     { 1, 1 },
#define DAF_PLUGIN_HAS_UI       1
#define DAF_PLUGIN_IS_RT_SAFE   1
// UI reads the meter atomic straight from the DSP when same-process
// (all Linux formats); falls back to the output parameter otherwise.
#define DAF_PLUGIN_WANT_DIRECT_ACCESS 1
#define DAF_PLUGIN_WANT_TIMEPOS       1

// Dear ImGui UI via DAF-Widgets: UI base class becomes ImGuiTopLevelWidget.
#define DAF_UI_USE_CUSTOM           1
#define DAF_UI_CUSTOM_INCLUDE_PATH  "DearImGui.hpp"
#define DAF_UI_CUSTOM_WIDGET_TYPE   DGL_NAMESPACE::ImGuiTopLevelWidget
#define DAF_UI_DEFAULT_WIDTH        900
#define DAF_UI_DEFAULT_HEIGHT       340
#define DAF_UI_USER_RESIZABLE       1
#define DAF_PLUGIN_WANT_LATENCY 0
#define DAF_PLUGIN_WANT_PROGRAMS 1
#define DAF_PLUGIN_WANT_STATE   0

#define DAF_PLUGIN_CLAP_FEATURES   "audio-effect", "delay", "reverb", "stereo"
#define DAF_PLUGIN_LV2_CATEGORY    "lv2:DelayPlugin"
#define DAF_PLUGIN_VST3_CATEGORIES "Fx|Delay|Stereo"
