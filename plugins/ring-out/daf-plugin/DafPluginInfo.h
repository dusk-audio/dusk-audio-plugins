// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// DafPluginInfo.h — DAF compile-time plugin configuration for Ring Out.

#pragma once

#define DAF_PLUGIN_BRAND        "Dusk Audio"
#define DAF_PLUGIN_NAME         "Ring Out"
#define DAF_PLUGIN_URI          "https://dusk-audio.github.io/plugins/ring-out"
#define DAF_PLUGIN_CLAP_ID      "com.duskaudio.ring-out"

#define DAF_PLUGIN_BRAND_ID     Dusk
#define DAF_PLUGIN_UNIQUE_ID    DsRO

#define DAF_PLUGIN_NUM_INPUTS   2
#define DAF_PLUGIN_NUM_OUTPUTS  2
// AU hosts filter insert menus by channel layout. A monitor send is as likely
// mono as stereo, so expose a true mono instance as well. Trailing comma required.
#define DAF_PLUGIN_EXTRA_IO     { 1, 1 },
#define DAF_PLUGIN_HAS_UI       1
#define DAF_PLUGIN_IS_RT_SAFE   1
// The UI reads the filter table, the analyser spectrum and the meters straight
// off the DSP when same-process (MONOLITHIC build); see RingOutAccess.hpp.
#define DAF_PLUGIN_WANT_DIRECT_ACCESS 1
#define DAF_PLUGIN_WANT_TIMEPOS       0
#define DAF_PLUGIN_WANT_LATENCY       0
#define DAF_PLUGIN_WANT_PROGRAMS      1
// The filter table is plugin state, not parameters: the detection engine writes
// it from the audio thread. FULL_STATE makes the host read it back (getState)
// when it saves, so filters the engine placed survive a session reload.
#define DAF_PLUGIN_WANT_STATE         1
#define DAF_PLUGIN_WANT_FULL_STATE    1

// Dear ImGui UI via DAF-Widgets: UI base class becomes ImGuiTopLevelWidget.
#define DAF_UI_USE_CUSTOM           1
#define DAF_UI_CUSTOM_INCLUDE_PATH  "DearImGui.hpp"
#define DAF_UI_CUSTOM_WIDGET_TYPE   DGL_NAMESPACE::ImGuiTopLevelWidget
#define DAF_UI_DEFAULT_WIDTH        960
#define DAF_UI_DEFAULT_HEIGHT       600
#define DAF_UI_USER_RESIZABLE       1

#define DAF_PLUGIN_CLAP_FEATURES   "audio-effect", "equalizer", "stereo"
#define DAF_PLUGIN_LV2_CATEGORY    "lv2:EQPlugin"
#define DAF_PLUGIN_VST3_CATEGORIES "Fx|EQ|Stereo"
