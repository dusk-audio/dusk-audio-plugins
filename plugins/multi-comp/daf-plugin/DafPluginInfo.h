// DAF identity and format contract for Multi-Comp 2.
#pragma once

#define DAF_PLUGIN_BRAND        "Dusk Audio"
#define DAF_PLUGIN_NAME         "Multi-Comp 2"
#define DAF_PLUGIN_URI         "https://dusk-audio.github.io/plugins/multi-comp-2"
#define DAF_PLUGIN_CLAP_ID      "com.duskaudio.multicomp2"
#define DAF_PLUGIN_BRAND_ID     Dusk
#define DAF_PLUGIN_UNIQUE_ID    DsMc

// Main stereo input/output plus a second stereo input group marked as the
// optional sidechain. DAF maps this to LV2/VST3/CLAP sidechain buses where the
// format supports them; JACK exposes all four inputs as named ports.
#define DAF_PLUGIN_NUM_INPUTS   4
#define DAF_PLUGIN_NUM_OUTPUTS  2
// AU hosts filter insert menus by channel layout, and DAF's AU wrapper carries
// every input channel on ONE element rather than a separate sidechain bus. The
// base { 4, 2 } entry therefore describes a 4-in insert, which no stereo track
// offers: without { 2, 2 } here auval reports the handling matrix as 1-1 and 4-2
// only, and Logic omits the AU from every stereo channel strip. The three
// entries are the three real layouts -- stereo with external sidechain, plain
// stereo, and mono -- and run() picks the sidechain path off the input count
// reported by ioChanged(), not off the output count.
#define DAF_PLUGIN_EXTRA_IO     { 2, 2 }, { 1, 1 },
#define DAF_PLUGIN_HAS_UI       1
#define DAF_PLUGIN_IS_RT_SAFE   1
#define DAF_PLUGIN_WANT_DIRECT_ACCESS 1
#define DAF_PLUGIN_WANT_LATENCY 1
#define DAF_PLUGIN_WANT_PROGRAMS 1
#define DAF_PLUGIN_WANT_STATE 1
#define DAF_PLUGIN_WANT_FULL_STATE 1
#define DAF_UI_USE_CUSTOM           1
#define DAF_UI_CUSTOM_INCLUDE_PATH  "DearImGui.hpp"
#define DAF_UI_CUSTOM_WIDGET_TYPE   DGL_NAMESPACE::ImGuiTopLevelWidget
#define DAF_UI_DEFAULT_WIDTH        1120
#define DAF_UI_DEFAULT_HEIGHT       380
#define DAF_UI_USER_RESIZABLE       1
#define DAF_PLUGIN_CLAP_FEATURES "audio-effect", "compressor", "stereo"
#define DAF_PLUGIN_LV2_CATEGORY "lv2:CompressorPlugin"
#define DAF_PLUGIN_VST3_CATEGORIES "Fx|Dynamics|Compressor"
