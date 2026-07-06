// DistrhoPluginInfo.h — DPF compile-time plugin configuration for Tape Echo.

#pragma once

#define DISTRHO_PLUGIN_BRAND        "Dusk Audio"
#define DISTRHO_PLUGIN_NAME         "Tape Echo"
#define DISTRHO_PLUGIN_URI          "https://dusk-audio.github.io/plugins/tape-echo"
#define DISTRHO_PLUGIN_CLAP_ID      "com.duskaudio.tape-echo"

#define DISTRHO_PLUGIN_BRAND_ID     Dusk
#define DISTRHO_PLUGIN_UNIQUE_ID    DsTE

#define DISTRHO_PLUGIN_NUM_INPUTS   2
#define DISTRHO_PLUGIN_NUM_OUTPUTS  2
#define DISTRHO_PLUGIN_HAS_UI       1
#define DISTRHO_PLUGIN_IS_RT_SAFE   1
// UI reads the meter atomic straight from the DSP when same-process
// (all Linux formats); falls back to the output parameter otherwise.
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1
#define DISTRHO_PLUGIN_WANT_TIMEPOS       1

// Dear ImGui UI via DPF-Widgets: UI base class becomes ImGuiTopLevelWidget.
#define DISTRHO_UI_USE_CUSTOM           1
#define DISTRHO_UI_CUSTOM_INCLUDE_PATH  "DearImGui.hpp"
#define DISTRHO_UI_CUSTOM_WIDGET_TYPE   DGL_NAMESPACE::ImGuiTopLevelWidget
#define DISTRHO_UI_DEFAULT_WIDTH        900
#define DISTRHO_UI_DEFAULT_HEIGHT       320
#define DISTRHO_UI_USER_RESIZABLE       1
#define DISTRHO_PLUGIN_WANT_LATENCY 0
#define DISTRHO_PLUGIN_WANT_PROGRAMS 1
#define DISTRHO_PLUGIN_WANT_STATE   0

#define DISTRHO_PLUGIN_CLAP_FEATURES   "audio-effect", "delay", "reverb", "stereo"
#define DISTRHO_PLUGIN_LV2_CATEGORY    "lv2:DelayPlugin"
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Fx|Delay|Stereo"
