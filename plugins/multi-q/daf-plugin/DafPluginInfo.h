// DafPluginInfo.h — DAF compile-time plugin configuration for Multi-Q 2.
//
// IDs are deliberately DISTINCT from the JUCE Multi-Q build (PLUGIN_CODE MulQ,
// LV2URI .../multi-q, BUNDLE_ID com.DuskAudio.MultiQ) so both can coexist in a
// user's session — the "2" is the human-facing successor marker.

#pragma once

#define DAF_PLUGIN_BRAND        "Dusk Audio"
#define DAF_PLUGIN_NAME         "Multi-Q 2"
#define DAF_PLUGIN_URI          "https://dusk-audio.github.io/plugins/multi-q-2"
#define DAF_PLUGIN_CLAP_ID      "com.duskaudio.multiq2"

#define DAF_PLUGIN_BRAND_ID     Dusk
#define DAF_PLUGIN_UNIQUE_ID    DsMq   // distinct from the JUCE build's MulQ

#define DAF_PLUGIN_NUM_INPUTS   2
#define DAF_PLUGIN_NUM_OUTPUTS  2
// AU hosts filter insert menus by channel layout.  Keep stereo as the default
// while also exposing a true mono instance for mono Logic channel strips.
#define DAF_PLUGIN_EXTRA_IO     { 1, 1 },
#define DAF_PLUGIN_HAS_UI       1
#define DAF_PLUGIN_IS_RT_SAFE   1
// UI reads the analyzer/meter atomics straight from the DSP when same-process
// (all Linux formats); falls back to output parameters otherwise.
#define DAF_PLUGIN_WANT_DIRECT_ACCESS 1
#define DAF_PLUGIN_WANT_TIMEPOS       0

// Dear ImGui UI via DAF-Widgets: UI base class becomes ImGuiTopLevelWidget.
#define DAF_UI_USE_CUSTOM           1
#define DAF_UI_CUSTOM_INCLUDE_PATH  "DearImGui.hpp"
#define DAF_UI_CUSTOM_WIDGET_TYPE   DGL_NAMESPACE::ImGuiTopLevelWidget
#define DAF_UI_DEFAULT_WIDTH        1040
#define DAF_UI_DEFAULT_HEIGHT       680
#define DAF_UI_USER_RESIZABLE       1
// British character routes through FourKEQDSP, which reports oversampler latency
// when its oversampling factor > 0; the shell forwards MultiQDSP::getLatencySamples()
// via setLatency() each block. Digital/Tube and un-oversampled British report 0.
#define DAF_PLUGIN_WANT_LATENCY 1
#define DAF_PLUGIN_WANT_PROGRAMS 1
// Match spectrum-EQ persists its learned spectra + correction FIR as one base64
// state blob ("matchData"). WANT_STATE enables initState()/setState(); WANT_FULL_
// STATE enables getState() so the host pulls the current blob on save.
#define DAF_PLUGIN_WANT_STATE      1
#define DAF_PLUGIN_WANT_FULL_STATE 1

#define DAF_PLUGIN_CLAP_FEATURES   "audio-effect", "equalizer", "stereo"
#define DAF_PLUGIN_LV2_CATEGORY    "lv2:EQPlugin"
#define DAF_PLUGIN_VST3_CATEGORIES "Fx|EQ|Stereo"
