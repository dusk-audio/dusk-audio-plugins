// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// DistrhoPluginInfo.h — DPF compile-time configuration for Multi-Synth.
//
// Multi-Synth was never released as a JUCE product, so there is no legacy build
// to disambiguate from: the display name stays plain "Multi-Synth" (design doc
// 09-multi-synth.md). New IDs are chosen distinct from every JUCE PLUGIN_CODE
// and every existing DPF d_cconst in the repo (see the collision check in the
// commit message): VST3 code DsMs, CLAP id com.duskaudio.multi-synth (repo
// com.duskaudio.<slug> convention, matching tape-echo), LV2 URI per design doc.

#pragma once

#define DISTRHO_PLUGIN_BRAND        "Dusk Audio"
#define DISTRHO_PLUGIN_NAME         "Multi-Synth"
#define DISTRHO_PLUGIN_URI          "https://dusk-audio.github.io/plugins/multi-synth"
#define DISTRHO_PLUGIN_CLAP_ID      "com.duskaudio.multi-synth"

#define DISTRHO_PLUGIN_BRAND_ID     Dusk
#define DISTRHO_PLUGIN_UNIQUE_ID    DsMs

// Instrument: no audio inputs, stereo out, MIDI in.
#define DISTRHO_PLUGIN_NUM_INPUTS   0
#define DISTRHO_PLUGIN_NUM_OUTPUTS  2
#define DISTRHO_PLUGIN_HAS_UI       1   // Phase 4: Dear ImGui UI (MultiSynthUI.cpp)
#define DISTRHO_PLUGIN_IS_SYNTH     1
#define DISTRHO_PLUGIN_IS_RT_SAFE   1

// Fixed design space 1240x780 (09-multi-synth-ui-spec.md), uniformly scaled.
#define DISTRHO_UI_USE_CUSTOM             1
#define DISTRHO_UI_CUSTOM_INCLUDE_PATH    "DearImGui.hpp"
#define DISTRHO_UI_CUSTOM_WIDGET_TYPE     DGL_NAMESPACE::ImGuiTopLevelWidget
#define DISTRHO_UI_DEFAULT_WIDTH          1240
#define DISTRHO_UI_DEFAULT_HEIGHT         780
#define DISTRHO_UI_USER_RESIZABLE         1

#define DISTRHO_PLUGIN_WANT_MIDI_INPUT    1
#define DISTRHO_PLUGIN_WANT_TIMEPOS       1   // BPM for arp/seq/delay/LFO sync
#define DISTRHO_PLUGIN_WANT_PROGRAMS      1   // 40 factory presets
#define DISTRHO_PLUGIN_WANT_STATE         0
// Same-process meter/scope access for the Phase-4 UI (weak-symbol bridge). With
// MONOLITHIC LV2 (see CMakeLists) this resolves in every format.
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1

#define DISTRHO_PLUGIN_CLAP_FEATURES   "instrument", "synthesizer", "stereo"
#define DISTRHO_PLUGIN_LV2_CATEGORY    "lv2:InstrumentPlugin"
#define DISTRHO_PLUGIN_VST3_CATEGORIES "Instrument|Synth|Stereo"
