// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// DuskVerbParamTable.hpp — framework-free mirror of the JUCE APVTS parameter
// layout in plugins/DuskVerb/src/PluginProcessor.cpp::createParameterLayout().
//
// Why the JUCE range maths is reimplemented here rather than approximated
// -----------------------------------------------------------------------
// The whole point of the DAF port is that it renders BIT-IDENTICALLY to the
// JUCE build for every factory program. A factory preset does not reach the
// DSP as the literal float written in FactoryPresets.h: JUCE routes it through
//
//     setValueNotifyingHost (param->convertTo0to1 (v))
//
// and the value the DSP later reads back from getRawParameterValue() is the
// result of TWO normalise/denormalise round trips (AudioParameterFloat stores
// the denormalised value, then APVTS::ParameterAdapter re-derives its own copy
// from parameter.getValue()). Each round trip rounds in float, so the DSP sees
// a value that differs from the table literal in the last few ULPs. Feed a
// different coefficient into a 30-second recursive reverb tail and the residual
// is audible in a null test long before it is audible in a mix.
//
// So: rangeTo01 / rangeFrom01 / rangeSnap below are transcribed from
// juce_NormalisableRange.h, cvTo01 / cvFrom01 from RangedAudioParameter, and
// dspFromHost() applies exactly the same two round trips as the JUCE chain.
// Do not "simplify" any of them (in particular, convertFrom0to1's inverse skew
// is exp(log(p)/skew), NOT pow(p, 1/skew) — they differ in the last bit).
//
// Host domain: parameters with a skew expose a normalised 0..1 coordinate,
// matching the convention the other Dusk DAF ports use (DAF has no taper the
// shipping formats honour, see AGENTS.md). Everything else exposes its plain
// physical range.

#pragma once

#include "../src/FactoryPresets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace duskverb
{

// ── Parameter identity ───────────────────────────────────────────────────────
// Index order is the APVTS layout order, which is also the host parameter order
// of the JUCE build. Do not reorder: saved sessions and automation lanes are
// keyed on it.
enum ParamId : int
{
    Algorithm              = 0,
    Mix                    = 1,
    BusMode                = 2,
    Bypass                 = 3,
    Predelay               = 4,
    PredelaySync           = 5,
    Decay                  = 6,
    Size                   = 7,
    ModDepth               = 8,
    ModRate                = 9,
    TailSpinDepth          = 10,
    TailSpinRate           = 11,
    BassMult               = 12,
    MidMult                = 13,
    Damping                = 14,
    Crossover              = 15,
    HighCrossover          = 16,
    SubMult                = 17,
    HiMidMult              = 18,
    CrossoverSub           = 19,
    CrossoverAir           = 20,
    TransientShaper        = 21,
    ShaperTime             = 22,
    ShaperXover            = 23,
    ShaperSens             = 24,
    InputSubGain           = 25,
    InputMidGain           = 26,
    InputHighGain          = 27,
    BassChoke              = 28,
    Saturation             = 29,
    Diffusion              = 30,
    ErLevel                = 31,
    ErSize                 = 32,
    ErBoost                = 33,
    QtHimidMult            = 34,
    QtAirMult              = 35,
    ErRise                 = 36,
    ErBusLowGain           = 37,
    ErBusHighGain          = 38,
    TankLevel              = 39,
    TankSplitHz            = 40,
    ErStereoNeutral        = 41,
    ErDecorr               = 42,
    Xtalk                  = 43,
    MbEnable               = 44,
    MbLowDecay             = 45,
    MbMidDecay             = 46,
    MbHighDecay            = 47,
    LoCut                  = 48,
    HiCut                  = 49,
    HiCutShelfDb           = 50,
    PteqBand0GainDb        = 51,
    PteqBand1GainDb        = 52,
    PteqBand2GainDb        = 53,
    PteqBand3GainDb        = 54,
    PostBandSubDb          = 55,
    PostBandLowmidDb       = 56,
    PostBandMidhiDb        = 57,
    PostBandAirDb          = 58,
    EdtSubAttackDb         = 59,
    EdtSubTauMs            = 60,
    EdtLowmidAttackDb      = 61,
    EdtLowmidTauMs         = 62,
    EdtMidhiAttackDb       = 63,
    EdtMidhiTauMs          = 64,
    EdtAirAttackDb         = 65,
    EdtAirTauMs            = 66,
    InLoopPeakHz           = 67,
    InLoopPeakQ            = 68,
    InLoopPeakDb           = 69,
    BassShelfFastFc        = 70,
    BassShelfSlowFc        = 71,
    BassShelfFastDb        = 72,
    BassShelfSlowDb        = 73,
    BassShelfTransitionMs  = 74,
    Width                  = 75,
    Freeze                 = 76,
    GateEnabled            = 77,
    GainTrim               = 78,
    MonoBelow              = 79,
    MonoBelowDepth         = 80,
    DpvHfShelfDb           = 81,
    DpvHfShelfHz           = 82,
    DpvStructHfDampHz      = 83,
    DpvBoxCutDb            = 84,
    DpvBoxCutHz            = 85,
    DpvBassShelfDb         = 86,
    DpvBassShelfHz         = 87,
    Duck                   = 88,
    Tone                   = 89,
    Character              = 90,
    TonalCorrection        = 91,    kNumParams
};

// ── Enumerated value labels ─────────────────────────────────────────────────
// kAlgorithmLabels mirrors getAlgorithmConfig(i).name for i in [0, 16). Kept as
// a literal list (rather than built from AlgorithmConfig.h at runtime) so the
// table stays constexpr; a static_assert in the core tests re-checks it against
// the engine table.
inline constexpr const char* const kAlgorithmLabels[16] = {
    "Plate", "Vintage Plate", "Smooth Plate", "Chamber",
    "Studio", "Spring", "Gated", "Shimmer",
    "Vintage Hall", "Reverse", "Hall", "Sparse",
    "Concert Hall", "Tiled Room", "Dense Hall", "Parallel Hall"
};
inline constexpr const char* const kPreDelaySyncLabels[7] = {
    "Free", "1/32", "1/16", "1/8", "1/4", "1/2", "1/1"
};
inline constexpr const char* const kOnOffLabels[2] = { "Off", "On" };

struct ParamDesc
{
    const char* id;             // APVTS parameter id — also the DAF symbol
    const char* name;           // user-facing name (identical to the JUCE build)
    const char* unit;           // display unit; empty when the host sees 0..1
    float       min, max;       // juce::NormalisableRange start / end
    float       interval;       // NormalisableRange interval (0 = continuous)
    float       skew;           // NormalisableRange skew (1 = linear)
    float       def;            // default in PLAIN units
    bool        integer;        // choice / bool
    const char* const* enumLabels;
    int         enumCount;
};

// ── The table ───────────────────────────────────────────────────────────────
// Defaults are seeded from factory preset 0 exactly as createParameterLayout()
// does (`const auto& fp0 = getFactoryPresets().front();`), so a host "reset to
// defaults" reproduces the same startup voicing in both builds.
inline const std::array<ParamDesc, kNumParams>& paramTable()
{
    static const std::array<ParamDesc, kNumParams> table = [] {
        const FactoryPreset& fp0 = getFactoryPresets().front();
        return std::array<ParamDesc, kNumParams> {{
            { "algorithm", "Algorithm", "", 0.0f, 15.0f, 1.0f, 1.0f, (float) fp0.algorithm, true, kAlgorithmLabels, 16 },
            { "mix", "Dry/Wet", "", 0.0f, 1.0f, 0.0f, 1.0f, fp0.mix, false, nullptr, 0 },
            { "bus_mode", "Bus Mode", "", 0.0f, 1.0f, 1.0f, 1.0f, (fp0.busMode ? 1.0f : 0.0f), true, kOnOffLabels, 2 },
            { "bypass", "Bypass", "", 0.0f, 1.0f, 1.0f, 1.0f, (false ? 1.0f : 0.0f), true, kOnOffLabels, 2 },
            { "predelay", "Pre-Delay", "ms", 0.0f, 250.0f, 0.0f, 0.4f, fp0.predelay, false, nullptr, 0 },
            { "predelay_sync", "Pre-Delay Sync", "", 0.0f, 6.0f, 1.0f, 1.0f, (float) fp0.predelaySync, true, kPreDelaySyncLabels, 7 },
            { "decay", "Decay Time", "s", 0.2f, 30.0f, 0.0f, 0.4f, fp0.decay, false, nullptr, 0 },
            { "size", "Size", "", 0.0f, 1.0f, 0.0f, 1.0f, fp0.size, false, nullptr, 0 },
            { "mod_depth", "Mod Depth", "", 0.0f, 1.0f, 0.001f, 1.0f, fp0.modDepth, false, nullptr, 0 },
            { "mod_rate", "Mod Rate", "Hz", 0.10f, 10.0f, 0.0f, 0.5f, fp0.modRate, false, nullptr, 0 },
            { "tail_spin_depth", "Tail Spin Depth", "", 0.0f, 1.0f, 0.001f, 1.0f, fp0.tailSpinDepth, false, nullptr, 0 },
            { "tail_spin_rate", "Tail Spin Rate", "Hz", 0.10f, 10.0f, 0.0f, 0.5f, fp0.tailSpinRate, false, nullptr, 0 },
            { "bass_mult", "Bass Multiply", "", 0.3f, 2.5f, 0.0f, 1.0f, fp0.bassMult, false, nullptr, 0 },
            { "mid_mult", "Mid Multiply", "", 0.3f, 2.5f, 0.0f, 1.0f, fp0.midMult, false, nullptr, 0 },
            { "damping", "Treble Multiply", "", 0.1f, 1.5f, 0.0f, 1.0f, fp0.damping, false, nullptr, 0 },
            { "crossover", "Low Crossover", "Hz", 200.0f, 4000.0f, 0.0f, 0.5f, fp0.crossover, false, nullptr, 0 },
            { "high_crossover", "High Crossover", "Hz", 1000.0f, 12000.0f, 0.0f, 0.5f, fp0.highCrossover, false, nullptr, 0 },
            { "sub_mult", "Sub Multiply", "", 0.1f, 2.0f, 0.0f, 1.0f, fp0.bassMult, false, nullptr, 0 },
            { "hi_mid_mult", "Hi-Mid Multiply", "", 0.1f, 2.0f, 0.0f, 1.0f, fp0.damping, false, nullptr, 0 },
            { "crossover_sub", "Sub Crossover", "Hz", 20.0f, 200.0f, 0.0f, 0.5f, 120.0f, false, nullptr, 0 },
            { "crossover_air", "Air Crossover", "Hz", 4000.0f, 20000.0f, 0.0f, 0.5f, 8000.0f, false, nullptr, 0 },
            { "transient_shaper", "Transient Shaper", "", 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "shaper_time", "Shaper Time", "ms", 20.0f, 300.0f, 0.0f, 0.5f, 120.0f, false, nullptr, 0 },
            { "shaper_xover", "Shaper Xover", "Hz", 120.0f, 500.0f, 0.0f, 0.5f, 250.0f, false, nullptr, 0 },
            { "shaper_sens", "Shaper Sens", "", 0.5f, 4.0f, 0.0f, 1.0f, 1.5f, false, nullptr, 0 },
            { "input_sub_gain", "Input Sub Gain", "dB", -6.0f, 6.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "input_mid_gain", "Input Mid Gain", "dB", -6.0f, 6.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "input_high_gain", "Input High Gain", "dB", -6.0f, 6.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "bass_choke", "Bass Choke", "Hz", 20.0f, 500.0f, 0.0f, 0.5f, fp0.bassChoke, false, nullptr, 0 },
            { "saturation", "Saturation", "", 0.0f, 1.0f, 0.0f, 1.0f, fp0.saturation, false, nullptr, 0 },
            { "diffusion", "Diffusion", "", 0.0f, 1.0f, 0.0f, 1.0f, fp0.diffusion, false, nullptr, 0 },
            { "er_level", "Early Ref Level", "", 0.0f, 1.0f, 0.0f, 1.0f, fp0.erLevel, false, nullptr, 0 },
            { "er_size", "Early Ref Size", "", 0.0f, 1.0f, 0.0f, 1.0f, fp0.erSize, false, nullptr, 0 },
            { "er_boost", "Early Ref Boost", "", 1.0f, 8.0f, 0.0f, 1.0f, 1.0f, false, nullptr, 0 },
            { "qt_himid_mult", "QT Hi-Mid Multiply", "", -1.0f, 2.0f, 0.0f, 1.0f, -1.0f, false, nullptr, 0 },
            { "qt_air_mult", "QT Air Multiply", "", -1.0f, 2.0f, 0.0f, 1.0f, -1.0f, false, nullptr, 0 },
            { "er_rise", "Early Ref Rise", "ms", 0.0f, 40.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "er_bus_low_gain", "ER Bus Low Gain", "dB", -12.0f, 18.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "er_bus_high_gain", "ER Bus High Gain", "dB", -12.0f, 18.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "tank_level", "Tank Level", "", 0.0f, 2.0f, 0.0f, 1.0f, 1.0f, false, nullptr, 0 },
            { "tank_split_hz", "Tank Split Hz", "Hz", 0.0f, 1000.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "er_stereo_neutral", "ER Stereo Neutral", "", 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "er_decorr", "ER Decorr", "", 0.0f, 0.7f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "xtalk", "HF Cross-Talk", "", 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "mb_enable", "Multiband", "", 0.0f, 1.0f, 1.0f, 1.0f, (false ? 1.0f : 0.0f), true, kOnOffLabels, 2 },
            { "mb_low_decay", "MB Low Decay", "s", 0.0f, 12.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "mb_mid_decay", "MB Mid Decay", "s", 0.0f, 12.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "mb_high_decay", "MB High Decay", "s", 0.0f, 12.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "lo_cut", "Lo Cut", "Hz", 5.0f, 500.0f, 0.0f, 0.3f, fp0.loCut, false, nullptr, 0 },
            { "hi_cut", "Hi Cut", "Hz", 1000.0f, 20000.0f, 0.0f, 0.3f, fp0.hiCut, false, nullptr, 0 },
            { "hi_cut_shelf_db", "Hi Cut Shelf", "dB", -24.0f, 0.0f, 0.0f, 1.0f, fp0.hiCutShelfGainDb, false, nullptr, 0 },
            { "pteq_band0_gain_db", "PostTankEQ Band 0 Gain", "dB", -12.0f, 12.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "pteq_band1_gain_db", "PostTankEQ Band 1 Gain", "dB", -12.0f, 12.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "pteq_band2_gain_db", "PostTankEQ Band 2 Gain", "dB", -12.0f, 12.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "pteq_band3_gain_db", "PostTankEQ Band 3 Gain", "dB", -12.0f, 12.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "post_band_sub_db", "Post Band Sub Gain", "dB", -8.0f, 8.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "post_band_lowmid_db", "Post Band Low-Mid Gain", "dB", -8.0f, 8.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "post_band_midhi_db", "Post Band Mid-High Gain", "dB", -8.0f, 8.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "post_band_air_db", "Post Band Air Gain", "dB", -8.0f, 8.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "edt_sub_attack_db", "EDT Sub Attack", "dB", -12.0f, 12.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "edt_sub_tau_ms", "EDT Sub Tau", "ms", 5.0f, 500.0f, 0.0f, 0.5f, 100.0f, false, nullptr, 0 },
            { "edt_lowmid_attack_db", "EDT Low-Mid Attack", "dB", -12.0f, 12.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "edt_lowmid_tau_ms", "EDT Low-Mid Tau", "ms", 5.0f, 500.0f, 0.0f, 0.5f, 100.0f, false, nullptr, 0 },
            { "edt_midhi_attack_db", "EDT Mid-High Attack", "dB", -12.0f, 12.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "edt_midhi_tau_ms", "EDT Mid-High Tau", "ms", 5.0f, 500.0f, 0.0f, 0.5f, 100.0f, false, nullptr, 0 },
            { "edt_air_attack_db", "EDT Air Attack", "dB", -12.0f, 12.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "edt_air_tau_ms", "EDT Air Tau", "ms", 5.0f, 500.0f, 0.0f, 0.5f, 100.0f, false, nullptr, 0 },
            { "in_loop_peak_hz", "In-Loop Peak Freq", "Hz", 200.0f, 8000.0f, 0.0f, 0.5f, 1000.0f, false, nullptr, 0 },
            { "in_loop_peak_q", "In-Loop Peak Q", "", 0.5f, 10.0f, 0.0f, 0.7f, 2.0f, false, nullptr, 0 },
            { "in_loop_peak_db", "In-Loop Peak Gain", "dB", -12.0f, 12.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "bass_shelf_fast_fc", "Bass Shelf Fast Fc", "Hz", 100.0f, 1000.0f, 0.0f, 0.5f, 400.0f, false, nullptr, 0 },
            { "bass_shelf_slow_fc", "Bass Shelf Slow Fc", "Hz", 50.0f, 500.0f, 0.0f, 0.5f, 200.0f, false, nullptr, 0 },
            { "bass_shelf_fast_db", "Bass Shelf Fast Gain", "dB", -12.0f, 12.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "bass_shelf_slow_db", "Bass Shelf Slow Gain", "dB", -12.0f, 12.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "bass_shelf_transition_ms", "Bass Shelf Transition", "ms", 10.0f, 1000.0f, 0.0f, 0.5f, 100.0f, false, nullptr, 0 },
            { "width", "Width", "", 0.0f, 2.0f, 0.0f, 1.0f, fp0.width, false, nullptr, 0 },
            { "freeze", "Freeze", "", 0.0f, 1.0f, 1.0f, 1.0f, (fp0.freeze ? 1.0f : 0.0f), true, kOnOffLabels, 2 },
            { "gate_enabled", "Gate", "", 0.0f, 1.0f, 1.0f, 1.0f, (true ? 1.0f : 0.0f), true, kOnOffLabels, 2 },
            { "gain_trim", "Gain Trim", "dB", -48.0f, 48.0f, 0.1f, 1.0f, fp0.gainTrim, false, nullptr, 0 },
            { "mono_below", "Mono Below", "Hz", 20.0f, 300.0f, 0.0f, 0.5f, fp0.monoBelow, false, nullptr, 0 },
            { "mono_below_depth", "Mono Below Depth", "", 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, false, nullptr, 0 },
            { "dpv_hf_shelf_db", "DPV HF Shelf Gain", "dB", -12.0f, 24.0f, 0.1f, 1.0f, fp0.dpvHfShelfGainDb, false, nullptr, 0 },
            { "dpv_hf_shelf_hz", "DPV HF Shelf Freq", "Hz", 2000.0f, 20000.0f, 1.0f, 0.5f, fp0.dpvHfShelfFreqHz, false, nullptr, 0 },
            { "dpv_struct_hf_damp_hz", "DPV Struct HF Damp", "Hz", 2000.0f, 18000.0f, 1.0f, 0.5f, fp0.dpvStructHfDampHz, false, nullptr, 0 },
            { "dpv_box_cut_db", "DPV Box Cut Gain", "dB", -12.0f, 6.0f, 0.1f, 1.0f, fp0.dpvBoxCutGainDb, false, nullptr, 0 },
            { "dpv_box_cut_hz", "DPV Box Cut Freq", "Hz", 100.0f, 800.0f, 1.0f, 0.5f, fp0.dpvBoxCutFreqHz, false, nullptr, 0 },
            { "dpv_bass_shelf_db", "DPV Bass Shelf Gain", "dB", -6.0f, 18.0f, 0.1f, 1.0f, fp0.dpvBassShelfGainDb, false, nullptr, 0 },
            { "dpv_bass_shelf_hz", "DPV Bass Shelf Freq", "Hz", 60.0f, 500.0f, 1.0f, 0.5f, fp0.dpvBassShelfFreqHz, false, nullptr, 0 },
            { "duck", "Duck", "", 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "tone", "Tone", "", -1.0f, 1.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "character", "Character", "", 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, false, nullptr, 0 },
            { "tonal_correction", "Tonal Correction", "", 0.0f, 1.0f, 1.0f, 1.0f, (false ? 1.0f : 0.0f), true, kOnOffLabels, 2 },        }};
    }();
    return table;
}

// Real-time note: paramDesc/dspFromHost are reachable from the audio thread —
// a CLAP or VST3 host delivers automation inside the process callback. The
// function-local static above is therefore PRIMED FROM THE PLUGIN CONSTRUCTOR
// (DuskVerbDSP's ctor calls paramTable()), so the audio thread only ever sees
// the initialised-guard fast path: a load and a branch, no allocation and no
// lock. Everything below it is pure float arithmetic.
inline const ParamDesc& paramDesc(int index) { return paramTable()[static_cast<size_t>(index)]; }

// Linear scan; called from the message thread only (state decode, preset apply).
inline int paramIndexForId(std::string_view id) noexcept
{
    const auto& t = paramTable();
    for (int i = 0; i < kNumParams; ++i)
        if (id == t[static_cast<size_t>(i)].id) return i;
    return -1;
}

// ── juce::NormalisableRange<float>, transcribed ─────────────────────────────
inline float clamp01(float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

inline float rangeTo01(const ParamDesc& d, float v) noexcept
{
    const float proportion = clamp01((v - d.min) / (d.max - d.min));
    if (d.skew == 1.0f) return proportion;
    return std::pow(proportion, d.skew);
}

inline float rangeFrom01(const ParamDesc& d, float proportion) noexcept
{
    proportion = clamp01(proportion);
    if (d.skew != 1.0f && proportion > 0.0f)
        proportion = std::exp(std::log(proportion) / d.skew);
    return d.min + (d.max - d.min) * proportion;
}

inline float rangeSnap(const ParamDesc& d, float v) noexcept
{
    if (d.interval > 0.0f)
        v = d.min + d.interval * std::floor((v - d.min) / d.interval + 0.5f);
    return (v <= d.min || d.max <= d.min) ? d.min : (v >= d.max ? d.max : v);
}

// juce::RangedAudioParameter::convertTo0to1 / convertFrom0to1.
inline float cvTo01(const ParamDesc& d, float v) noexcept   { return rangeTo01(d, rangeSnap(d, v)); }
inline float cvFrom01(const ParamDesc& d, float p) noexcept { return rangeSnap(d, rangeFrom01(d, clamp01(p))); }

// Nonlinear parameters expose the original JUCE normalized coordinate. DAF's
// logarithmic metadata does not supply a taper in VST3, CLAP or AU. Host text
// callbacks convert this coordinate to the physical value shown to musicians.
inline bool  hasSkew(const ParamDesc& d) noexcept { return d.skew != 1.0f; }
inline float hostMin(const ParamDesc& d) noexcept { return hasSkew(d) ? 0.0f : d.min; }
inline float hostMax(const ParamDesc& d) noexcept { return hasSkew(d) ? 1.0f : d.max; }

// Plain value -> host coordinate: the snapped plain value itself.
inline float plainToHost(const ParamDesc& d, float v) noexcept
{ return hasSkew(d) ? cvTo01(d, v) : rangeSnap(d, v); }

// Host (plain) coordinate -> the float the DSP must see.
//
// Reproduces, exactly, what a plain value goes through in the JUCE build:
//   Slider / preset -> setValueNotifyingHost(convertTo0to1(plain))
//   AudioParameterFloat::setValue()        -> convertFrom0to1(...)           = d1
//   APVTS::ParameterAdapter::parameterValueChanged()
//         -> convertFrom0to1(convertTo0to1(d1))                              = d2
// so a knob at "30 Hz" feeds both builds the identical float.
inline float dspFromHost(const ParamDesc& d, float host) noexcept
{
    const float d1 = cvFrom01(d, hasSkew(d) ? host : cvTo01(d, host));
    return cvFrom01(d, cvTo01(d, d1));
}

inline float hostDefault(const ParamDesc& d) noexcept { return plainToHost(d, d.def); }

// Clamp into range, and snap integer parameters, at the point a host value
// enters the plugin (see MultiCompParams.hpp for why: a state saved mid-ramp on
// an integer parameter must still be loadable).
// Bit classification remains valid in translation units compiled with the
// original engine's fast-math flags. Floating isfinite may be optimized away.
inline bool finiteFloat(float v) noexcept
{
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7f800000u) != 0x7f800000u;
}

inline float snapHostValue(const ParamDesc& d, float v) noexcept
{
    if (!finiteFloat(v)) return hostDefault(d);
    const float clamped = std::min(std::max(v, hostMin(d)), hostMax(d));
    if (!hasSkew(d)) return rangeSnap(d, clamped);
    return d.interval > 0.0f ? cvTo01(d, cvFrom01(d, clamped)) : clamped;
}

inline bool hostValueInRange(const ParamDesc& d, float v) noexcept
{
    return finiteFloat(v) && v >= hostMin(d) && v <= hostMax(d)
        && (!d.integer || std::trunc(v) == v);
}

// ── Knob domain: the editor's travel coordinate ─────────────────────────────
//
// A tapered parameter's knob turns in the JUCE slider's normalised coordinate
// (0..1 through the skew), so the arc, the drag speed and the position for a
// given value all match the JUCE editor. An un-tapered one turns in its plain
// range. The editor converts to the plain host domain at its boundary only.
inline float knobMin(const ParamDesc& d) noexcept { return hasSkew(d) ? 0.0f : d.min; }
inline float knobMax(const ParamDesc& d) noexcept { return hasSkew(d) ? 1.0f : d.max; }

inline float plainToKnob(const ParamDesc& d, float v) noexcept
{
    return hasSkew(d) ? cvTo01(d, v) : rangeSnap(d, v);
}

inline float knobToHost(const ParamDesc& d, float knob) noexcept { return snapHostValue(d, knob); }

inline float hostToKnob(const ParamDesc&, float host) noexcept { return host; }
inline float knobDefault(const ParamDesc& d) noexcept { return plainToKnob(d, d.def); }

inline float snapKnobValue(const ParamDesc& d, float v) noexcept
{
    return snapHostValue(d, v);
}

} // namespace duskverb
