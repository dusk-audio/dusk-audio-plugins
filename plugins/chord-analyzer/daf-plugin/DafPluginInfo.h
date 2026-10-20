// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once

#define DAF_PLUGIN_BRAND "Dusk Audio"
#define DAF_PLUGIN_BRAND_ID Dusk
#if CHORD_ANALYZER_DAF_MIDI_MODE
# define DAF_PLUGIN_NAME "Chord Analyzer MIDI 2"
# define DAF_PLUGIN_URI "https://dusk-audio.github.io/plugins/chord-analyzer-midi-2"
# define DAF_PLUGIN_CLAP_ID "com.duskaudio.chord-analyzer-midi-2"
# define DAF_PLUGIN_UNIQUE_ID DsCM
# define DAF_PLUGIN_NUM_INPUTS 0
# define DAF_PLUGIN_NUM_OUTPUTS 0
# define DAF_PLUGIN_IS_SYNTH 0
# define DAF_PLUGIN_AU_TYPE aumi
# define DAF_PLUGIN_CLAP_FEATURES "note-effect", "utility"
# define DAF_PLUGIN_LV2_CATEGORY "lv2:UtilityPlugin"
# define DAF_PLUGIN_VST3_CATEGORIES "Fx|Tools"
#else
# define DAF_PLUGIN_NAME "Chord Analyzer 2"
# define DAF_PLUGIN_URI "https://dusk-audio.github.io/plugins/chord-analyzer-2"
# define DAF_PLUGIN_CLAP_ID "com.duskaudio.chord-analyzer-2"
# define DAF_PLUGIN_UNIQUE_ID DsCA
// Retained desktop instrument topology: accept audio, produce silence.
# define DAF_PLUGIN_NUM_INPUTS 2
# define DAF_PLUGIN_NUM_OUTPUTS 2
# define DAF_PLUGIN_EXTRA_IO { 1, 1 },
# define DAF_PLUGIN_IS_SYNTH 1
# define DAF_PLUGIN_AU_TYPE aumu
# define DAF_PLUGIN_CLAP_FEATURES "instrument", "utility", "stereo"
# define DAF_PLUGIN_LV2_CATEGORY "lv2:InstrumentPlugin"
# define DAF_PLUGIN_VST3_CATEGORIES "Instrument|Tools"
#endif

// Initial, testable shells; editor integration is a separate #183 milestone.
#define DAF_PLUGIN_HAS_UI 0
#define DAF_PLUGIN_IS_RT_SAFE 1
#define DAF_PLUGIN_WANT_MIDI_INPUT 1
#define DAF_PLUGIN_WANT_MIDI_OUTPUT 1
#define DAF_PLUGIN_WANT_TIMEPOS 1
#define DAF_PLUGIN_WANT_STATE 0
#define DAF_PLUGIN_WANT_PROGRAMS 0
#define DAF_PLUGIN_WANT_LATENCY 0
