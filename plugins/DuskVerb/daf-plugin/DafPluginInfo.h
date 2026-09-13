// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// DafPluginInfo.h — DAF compile-time configuration for DuskVerb 2.

#pragma once

#define DAF_PLUGIN_BRAND        "Dusk Audio"
#define DAF_PLUGIN_NAME         "DuskVerb 2"
#define DAF_PLUGIN_URI          "https://dusk-audio.github.io/plugins/duskverb-2"
#define DAF_PLUGIN_CLAP_ID      "com.duskaudio.duskverb2"

#define DAF_PLUGIN_BRAND_ID     Dusk
#define DAF_PLUGIN_UNIQUE_ID    DsDv   // distinct from the JUCE build's DkVb

#define DAF_PLUGIN_NUM_INPUTS   2
#define DAF_PLUGIN_NUM_OUTPUTS  2
// AU hosts filter insert menus by channel layout. Keep stereo as the default
// while also exposing a true mono instance for mono channel strips. The trailing
// comma is required.
#define DAF_PLUGIN_EXTRA_IO     { 1, 1 }, { 1, 2 },
#define DAF_PLUGIN_HAS_UI       1
#define DAF_PLUGIN_IS_RT_SAFE   1
// The UI reads the level meters and the decay-envelope ring straight off the DSP
// atomics when same-process (every Linux format, and MONOLITHIC LV2); it falls
// back to the output parameters otherwise.
#define DAF_PLUGIN_WANT_DIRECT_ACCESS 1
#define DAF_PLUGIN_WANT_PROGRAMS      1
// The Pre-Delay Sync parameter resolves note values against the host tempo,
// exactly as the JUCE build queries its playhead.
#define DAF_PLUGIN_WANT_TIMEPOS       1
// The JUCE build reports a fixed 30 s tail (getTailLengthSeconds); reporting
// the same keeps hosts from stopping a bounce or suspending the instance
// mid-decay. Set in activate() and on sample-rate changes, in frames.
#define DAF_PLUGIN_WANT_TAIL          1
#define DAF_PLUGIN_WANT_STATE         1
#define DAF_PLUGIN_WANT_FULL_STATE    1
// JUCE consumes the final point of each host parameter queue before processing
// the block. Preserve that automation timing along with its parameter taper.
#define DAF_PLUGIN_VST3_LAST_PARAMETER_POINT 1
// No DAF_PLUGIN_WANT_LATENCY: every DuskVerb engine is zero-latency and the
// JUCE build reports setLatencySamples(0). Declaring it would advertise a
// latency contract the plugin does not need.

// Dear ImGui UI via DAF-Widgets.
#define DAF_UI_USE_CUSTOM           1
#define DAF_UI_FILE_BROWSER         1
#define DAF_UI_CUSTOM_INCLUDE_PATH  "DearImGui.hpp"
#define DAF_UI_CUSTOM_WIDGET_TYPE   DGL_NAMESPACE::ImGuiTopLevelWidget
// Uniformly scaled 1200 x 800 editor; 1050 x 700 minimum preserves 3:2.
#define DAF_UI_DEFAULT_WIDTH        1200
#define DAF_UI_DEFAULT_HEIGHT       800
#define DAF_UI_USER_RESIZABLE       1

#define DAF_PLUGIN_CLAP_FEATURES   "audio-effect", "reverb", "stereo"
#define DAF_PLUGIN_LV2_CATEGORY    "lv2:ReverbPlugin"
#define DAF_PLUGIN_VST3_CATEGORIES "Fx|Reverb|Stereo"
