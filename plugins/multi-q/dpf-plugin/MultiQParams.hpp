// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// MultiQParams.hpp — parameter ids, ranges and choice labels for the Multi-Q 2
// DPF shell (initParameter / setParameterValue / snapshot) and the UI.
//
// Order, ids, names, ranges, defaults and choice-label strings mirror the JUCE
// MultiQ::createParameterLayout() EXACTLY (MultiQ.cpp:3166). The 190 host-
// automation parameters appear in the same order the JUCE build declares them:
//   [0..81]   per-band ×8 (irregular: bands 1/8 lack gain/shape/sat; only 1/8
//             carry a slope; bands 2-7 carry gain/shape/sat) — 82 params
//   [82..87]  globals: master_gain, bypass, hq_enabled, processing_mode,
//             q_couple_mode, eq_type
//   [88..91]  match: apply, smoothing, limit_boost, limit_cut
//   [92..97]  analyzer: enabled, pre_post, mode, resolution, smoothing, decay
//   [98..99]  display: scale_mode, visualize_master_gain
//   [100..119] british ×20
//   [120..137] tube/pultec ×18 (6 stepped freqs are CHOICE, resolved via LUT)
//   [138..185] dyn per-band ×8 (enabled, threshold, attack, release, range, ratio)
//   [186..189] dyn_detection_mode, auto_gain_enabled, limiter_enabled, limiter_ceiling
//
// Match/analyzer/display/dyn-detection/auto-gain/limiter params are exposed for
// host automation completeness; the Phase-2 core snapshot ignores the ones with
// no matching core Params field (eq_type==Match IS handled by the core as an
// all-bands-bypass).

#pragma once

// ---- choice label tables (index == parameter value) ------------------------
namespace mqp
{
    static constexpr const char* kOffOn[]      = { "Off", "On" };
    static constexpr const char* kShapeLoShelf[] = { "Low Shelf", "Peaking", "High Pass" };
    static constexpr const char* kShapeHiShelf[] = { "High Shelf", "Peaking", "Low Pass" };
    static constexpr const char* kShapePeak[]  = { "Peaking", "Notch", "Band Pass", "Tilt Shelf" };
    static constexpr const char* kRouting[]    = { "Global", "Stereo", "Left", "Right", "Mid", "Side" };
    static constexpr const char* kSatType[]    = { "Off", "Tape", "Tube", "Console", "FET" };
    static constexpr const char* kSlope[]      = { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct",
                                                   "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct" };
    static constexpr const char* kOversampling[] = { "Off", "2x", "4x" };
    static constexpr const char* kProcMode[]   = { "Stereo", "Left", "Right", "Mid", "Side" };
    static constexpr const char* kQCouple[]    = { "Off", "Proportional", "Light", "Medium", "Strong",
                                                   "Asymmetric Light", "Asymmetric Medium",
                                                   "Asymmetric Strong", "Vintage" };
    static constexpr const char* kEqType[]     = { "Digital", "Match", "British", "Tube" };
    static constexpr const char* kPeakRms[]    = { "Peak", "RMS" };
    static constexpr const char* kAnaRes[]     = { "Low (2048)", "Medium (4096)", "High (8192)" };
    static constexpr const char* kAnaSmooth[]  = { "Off", "Light", "Medium", "Heavy" };
    static constexpr const char* kDispScale[]  = { "+/-12 dB", "+/-24 dB", "+/-30 dB", "+/-60 dB", "Warped" };
    static constexpr const char* kBritishMode[]= { "Brown", "Black" };
    static constexpr const char* kLfBoostF[]   = { "20 Hz", "30 Hz", "60 Hz", "100 Hz" };
    static constexpr const char* kHfBoostF[]   = { "3 kHz", "4 kHz", "5 kHz", "8 kHz", "10 kHz", "12 kHz", "16 kHz" };
    static constexpr const char* kHfAttenF[]   = { "5 kHz", "10 kHz", "20 kHz" };
    static constexpr const char* kMidLowF[]    = { "0.2 kHz", "0.3 kHz", "0.5 kHz", "0.7 kHz", "1.0 kHz" };
    static constexpr const char* kMidDipF[]    = { "0.2 kHz", "0.3 kHz", "0.5 kHz", "0.7 kHz", "1.0 kHz", "1.5 kHz", "2.0 kHz" };
    static constexpr const char* kMidHighF[]   = { "1.5 kHz", "2.0 kHz", "3.0 kHz", "4.0 kHz", "5.0 kHz" };

    // choice-index → resolved Hz LUTs (MultiQ.cpp:784-842). Applied by the shell
    // snapshot before filling MultiQDSP::Params::Tube (which stores resolved Hz).
    static constexpr float kLfBoostHz[]  = { 20.f, 30.f, 60.f, 100.f };
    static constexpr float kHfBoostHz[]  = { 3000.f, 4000.f, 5000.f, 8000.f, 10000.f, 12000.f, 16000.f };
    static constexpr float kHfAttenHz[]  = { 5000.f, 10000.f, 20000.f };
    static constexpr float kMidLowHz[]   = { 200.f, 300.f, 500.f, 700.f, 1000.f };
    static constexpr float kMidDipHz[]   = { 200.f, 300.f, 500.f, 700.f, 1000.f, 1500.f, 2000.f };
    static constexpr float kMidHighHz[]  = { 1500.f, 2000.f, 3000.f, 4000.f, 5000.f };
}

// ---- named parameter indices ----------------------------------------------
// The 82 per-band params [0..81] are addressed through the mqidx:: helpers
// below (their layout is irregular). Everything after band 8 is named here.
enum MqParamId
{
    kMqGlobalBase       = 82,
    kParamMasterGain    = 82,
    kParamBypass,          // 83 — host-designated bypass
    kParamHqEnabled,       // 84 — Oversampling Off/2x/4x
    kParamProcessingMode,  // 85
    kParamQCoupleMode,     // 86
    kParamEqType,          // 87
    kParamMatchApply,      // 88
    kParamMatchSmoothing,  // 89
    kParamMatchLimitBoost, // 90
    kParamMatchLimitCut,   // 91
    kParamAnalyzerEnabled, // 92
    kParamAnalyzerPrePost, // 93
    kParamAnalyzerMode,    // 94
    kParamAnalyzerResolution, // 95
    kParamAnalyzerSmoothing,  // 96
    kParamAnalyzerDecay,   // 97
    kParamDisplayScaleMode,   // 98
    kParamVisualizeMasterGain,// 99
    kParamBritishHpfFreq,  // 100
    kParamBritishHpfEnabled,
    kParamBritishLpfFreq,
    kParamBritishLpfEnabled,
    kParamBritishLfGain,
    kParamBritishLfFreq,
    kParamBritishLfBell,
    kParamBritishLmGain,
    kParamBritishLmFreq,
    kParamBritishLmQ,
    kParamBritishHmGain,
    kParamBritishHmFreq,
    kParamBritishHmQ,
    kParamBritishHfGain,
    kParamBritishHfFreq,
    kParamBritishHfBell,
    kParamBritishMode,
    kParamBritishSaturation,
    kParamBritishInputGain,
    kParamBritishOutputGain, // 119
    kParamPultecLfBoostGain, // 120
    kParamPultecLfBoostFreq,
    kParamPultecLfAttenGain,
    kParamPultecHfBoostGain,
    kParamPultecHfBoostFreq,
    kParamPultecHfBoostBandwidth,
    kParamPultecHfAttenGain,
    kParamPultecHfAttenFreq,
    kParamPultecInputGain,
    kParamPultecOutputGain,
    kParamPultecTubeDrive,
    kParamPultecMidEnabled,
    kParamPultecMidLowFreq,
    kParamPultecMidLowPeak,
    kParamPultecMidDipFreq,
    kParamPultecMidDip,
    kParamPultecMidHighFreq,
    kParamPultecMidHighPeak, // 137
    kMqDynBase          = 138, // dyn per-band ×8 (6 each) occupy [138..185]
    kParamDynDetectionMode = 186,
    kParamAutoGainEnabled, // 187
    kParamLimiterEnabled,  // 188
    kParamLimiterCeiling,  // 189
    kParamCount            // 190
};

// ---- per-band index helpers -------------------------------------------------
// Bands are 0-indexed here (band b == JUCE band b+1). Bands 0 and 7 are "edge"
// bands (HPF/LPF): 8 params each, no gain/shape/sat, but carry a slope. Bands
// 1..6 are "middle": 11 params each with gain/shape/sat, no slope.
namespace mqidx
{
    constexpr bool isEdge(int b) { return b == 0 || b == 7; }
    // base index of band b's first parameter (enabled).
    constexpr int base(int b) { return b == 0 ? 0 : 8 + 11 * (b - 1); }

    constexpr int enabled(int b)     { return base(b) + 0; }
    constexpr int freq(int b)        { return base(b) + 1; }
    constexpr int gain(int b)        { return isEdge(b) ? -1 : base(b) + 2; }
    constexpr int q(int b)           { return isEdge(b) ? base(b) + 2 : base(b) + 3; }
    constexpr int shape(int b)       { return isEdge(b) ? -1 : base(b) + 4; }
    constexpr int routing(int b)     { return isEdge(b) ? base(b) + 3 : base(b) + 5; }
    constexpr int satType(int b)     { return isEdge(b) ? -1 : base(b) + 6; }
    constexpr int satDrive(int b)    { return isEdge(b) ? -1 : base(b) + 7; }
    constexpr int slope(int b)       { return isEdge(b) ? base(b) + 4 : -1; }
    constexpr int invert(int b)      { return isEdge(b) ? base(b) + 5 : base(b) + 8; }
    constexpr int phaseInvert(int b) { return isEdge(b) ? base(b) + 6 : base(b) + 9; }
    constexpr int pan(int b)         { return isEdge(b) ? base(b) + 7 : base(b) + 10; }

    // dyn per-band [138..185], 6 params/band in fixed order.
    constexpr int dynBase(int b)     { return kMqDynBase + 6 * b; }
    constexpr int dynEnabled(int b)  { return dynBase(b) + 0; }
    constexpr int dynThreshold(int b){ return dynBase(b) + 1; }
    constexpr int dynAttack(int b)   { return dynBase(b) + 2; }
    constexpr int dynRelease(int b)  { return dynBase(b) + 3; }
    constexpr int dynRange(int b)    { return dynBase(b) + 4; }
    constexpr int dynRatio(int b)    { return dynBase(b) + 5; }
}

// ---- unified parameter descriptor (drives initParameter) -------------------
// kind: 'f' linear float, 'g' skewed float (log-ish; UI owns the skew feel — DPF
// stores raw values), 'c' integer choice, 'b' boolean toggle (Off/On). The
// global `bypass` (kParamBypass) is a 'b' entry but is special-cased in
// initParameter to take the host bypass designation.
struct MqParam
{
    const char*  id;
    const char*  name;
    char         kind;
    float        min, max, def;
    const char*  unit;
    const char* const* choices; // non-null for 'c'/'b'
    int          numChoices;
};

static constexpr MqParam kMqParams[kParamCount] =
{
    { "band1_enabled", "Band 1 Enabled", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "band1_freq", "Band 1 Frequency", 'g', 20.f, 20000.f, 20.0f, "Hz", nullptr, 0 },
    { "band1_q", "Band 1 Q", 'g', 0.1f, 100.f, 0.71f, "", nullptr, 0 },
    { "band1_channel_routing", "Band 1 Routing", 'c', 0.f, 5.f, 0.f, "", mqp::kRouting, 6 },
    { "band1_slope", "Band 1 Slope", 'c', 0.f, 7.f, 1.f, "", mqp::kSlope, 8 },
    { "band1_invert", "Band 1 Invert", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "band1_phase_invert", "Band 1 Phase Invert", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "band1_pan", "Band 1 Pan", 'f', -1.f, 1.f, 0.f, "", nullptr, 0 },
    { "band2_enabled", "Band 2 Enabled", 'b', 0.f, 1.f, 1.f, "", mqp::kOffOn, 2 },
    { "band2_freq", "Band 2 Frequency", 'g', 20.f, 20000.f, 100.0f, "Hz", nullptr, 0 },
    { "band2_gain", "Band 2 Gain", 'f', -24.f, 24.f, 0.f, "dB", nullptr, 0 },
    { "band2_q", "Band 2 Q", 'g', 0.1f, 100.f, 0.71f, "", nullptr, 0 },
    { "band2_shape", "Band 2 Shape", 'c', 0.f, 2.f, 0.f, "", mqp::kShapeLoShelf, 3 },
    { "band2_channel_routing", "Band 2 Routing", 'c', 0.f, 5.f, 0.f, "", mqp::kRouting, 6 },
    { "band2_sat_type", "Band 2 Saturation", 'c', 0.f, 4.f, 0.f, "", mqp::kSatType, 5 },
    { "band2_sat_drive", "Band 2 Sat Drive", 'f', 0.f, 1.f, 0.3f, "", nullptr, 0 },
    { "band2_invert", "Band 2 Invert", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "band2_phase_invert", "Band 2 Phase Invert", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "band2_pan", "Band 2 Pan", 'f', -1.f, 1.f, 0.f, "", nullptr, 0 },
    { "band3_enabled", "Band 3 Enabled", 'b', 0.f, 1.f, 1.f, "", mqp::kOffOn, 2 },
    { "band3_freq", "Band 3 Frequency", 'g', 20.f, 20000.f, 200.0f, "Hz", nullptr, 0 },
    { "band3_gain", "Band 3 Gain", 'f', -24.f, 24.f, 0.f, "dB", nullptr, 0 },
    { "band3_q", "Band 3 Q", 'g', 0.1f, 100.f, 0.71f, "", nullptr, 0 },
    { "band3_shape", "Band 3 Shape", 'c', 0.f, 3.f, 0.f, "", mqp::kShapePeak, 4 },
    { "band3_channel_routing", "Band 3 Routing", 'c', 0.f, 5.f, 0.f, "", mqp::kRouting, 6 },
    { "band3_sat_type", "Band 3 Saturation", 'c', 0.f, 4.f, 0.f, "", mqp::kSatType, 5 },
    { "band3_sat_drive", "Band 3 Sat Drive", 'f', 0.f, 1.f, 0.3f, "", nullptr, 0 },
    { "band3_invert", "Band 3 Invert", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "band3_phase_invert", "Band 3 Phase Invert", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "band3_pan", "Band 3 Pan", 'f', -1.f, 1.f, 0.f, "", nullptr, 0 },
    { "band4_enabled", "Band 4 Enabled", 'b', 0.f, 1.f, 1.f, "", mqp::kOffOn, 2 },
    { "band4_freq", "Band 4 Frequency", 'g', 20.f, 20000.f, 500.0f, "Hz", nullptr, 0 },
    { "band4_gain", "Band 4 Gain", 'f', -24.f, 24.f, 0.f, "dB", nullptr, 0 },
    { "band4_q", "Band 4 Q", 'g', 0.1f, 100.f, 0.71f, "", nullptr, 0 },
    { "band4_shape", "Band 4 Shape", 'c', 0.f, 3.f, 0.f, "", mqp::kShapePeak, 4 },
    { "band4_channel_routing", "Band 4 Routing", 'c', 0.f, 5.f, 0.f, "", mqp::kRouting, 6 },
    { "band4_sat_type", "Band 4 Saturation", 'c', 0.f, 4.f, 0.f, "", mqp::kSatType, 5 },
    { "band4_sat_drive", "Band 4 Sat Drive", 'f', 0.f, 1.f, 0.3f, "", nullptr, 0 },
    { "band4_invert", "Band 4 Invert", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "band4_phase_invert", "Band 4 Phase Invert", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "band4_pan", "Band 4 Pan", 'f', -1.f, 1.f, 0.f, "", nullptr, 0 },
    { "band5_enabled", "Band 5 Enabled", 'b', 0.f, 1.f, 1.f, "", mqp::kOffOn, 2 },
    { "band5_freq", "Band 5 Frequency", 'g', 20.f, 20000.f, 1000.0f, "Hz", nullptr, 0 },
    { "band5_gain", "Band 5 Gain", 'f', -24.f, 24.f, 0.f, "dB", nullptr, 0 },
    { "band5_q", "Band 5 Q", 'g', 0.1f, 100.f, 0.71f, "", nullptr, 0 },
    { "band5_shape", "Band 5 Shape", 'c', 0.f, 3.f, 0.f, "", mqp::kShapePeak, 4 },
    { "band5_channel_routing", "Band 5 Routing", 'c', 0.f, 5.f, 0.f, "", mqp::kRouting, 6 },
    { "band5_sat_type", "Band 5 Saturation", 'c', 0.f, 4.f, 0.f, "", mqp::kSatType, 5 },
    { "band5_sat_drive", "Band 5 Sat Drive", 'f', 0.f, 1.f, 0.3f, "", nullptr, 0 },
    { "band5_invert", "Band 5 Invert", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "band5_phase_invert", "Band 5 Phase Invert", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "band5_pan", "Band 5 Pan", 'f', -1.f, 1.f, 0.f, "", nullptr, 0 },
    { "band6_enabled", "Band 6 Enabled", 'b', 0.f, 1.f, 1.f, "", mqp::kOffOn, 2 },
    { "band6_freq", "Band 6 Frequency", 'g', 20.f, 20000.f, 2000.0f, "Hz", nullptr, 0 },
    { "band6_gain", "Band 6 Gain", 'f', -24.f, 24.f, 0.f, "dB", nullptr, 0 },
    { "band6_q", "Band 6 Q", 'g', 0.1f, 100.f, 0.71f, "", nullptr, 0 },
    { "band6_shape", "Band 6 Shape", 'c', 0.f, 3.f, 0.f, "", mqp::kShapePeak, 4 },
    { "band6_channel_routing", "Band 6 Routing", 'c', 0.f, 5.f, 0.f, "", mqp::kRouting, 6 },
    { "band6_sat_type", "Band 6 Saturation", 'c', 0.f, 4.f, 0.f, "", mqp::kSatType, 5 },
    { "band6_sat_drive", "Band 6 Sat Drive", 'f', 0.f, 1.f, 0.3f, "", nullptr, 0 },
    { "band6_invert", "Band 6 Invert", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "band6_phase_invert", "Band 6 Phase Invert", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "band6_pan", "Band 6 Pan", 'f', -1.f, 1.f, 0.f, "", nullptr, 0 },
    { "band7_enabled", "Band 7 Enabled", 'b', 0.f, 1.f, 1.f, "", mqp::kOffOn, 2 },
    { "band7_freq", "Band 7 Frequency", 'g', 20.f, 20000.f, 4000.0f, "Hz", nullptr, 0 },
    { "band7_gain", "Band 7 Gain", 'f', -24.f, 24.f, 0.f, "dB", nullptr, 0 },
    { "band7_q", "Band 7 Q", 'g', 0.1f, 100.f, 0.71f, "", nullptr, 0 },
    { "band7_shape", "Band 7 Shape", 'c', 0.f, 2.f, 0.f, "", mqp::kShapeHiShelf, 3 },
    { "band7_channel_routing", "Band 7 Routing", 'c', 0.f, 5.f, 0.f, "", mqp::kRouting, 6 },
    { "band7_sat_type", "Band 7 Saturation", 'c', 0.f, 4.f, 0.f, "", mqp::kSatType, 5 },
    { "band7_sat_drive", "Band 7 Sat Drive", 'f', 0.f, 1.f, 0.3f, "", nullptr, 0 },
    { "band7_invert", "Band 7 Invert", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "band7_phase_invert", "Band 7 Phase Invert", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "band7_pan", "Band 7 Pan", 'f', -1.f, 1.f, 0.f, "", nullptr, 0 },
    { "band8_enabled", "Band 8 Enabled", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "band8_freq", "Band 8 Frequency", 'g', 20.f, 20000.f, 20000.0f, "Hz", nullptr, 0 },
    { "band8_q", "Band 8 Q", 'g', 0.1f, 100.f, 0.71f, "", nullptr, 0 },
    { "band8_channel_routing", "Band 8 Routing", 'c', 0.f, 5.f, 0.f, "", mqp::kRouting, 6 },
    { "band8_slope", "Band 8 Slope", 'c', 0.f, 7.f, 1.f, "", mqp::kSlope, 8 },
    { "band8_invert", "Band 8 Invert", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "band8_phase_invert", "Band 8 Phase Invert", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "band8_pan", "Band 8 Pan", 'f', -1.f, 1.f, 0.f, "", nullptr, 0 },
    { "master_gain", "Master Gain", 'f', -24.f, 24.f, 0.f, "dB", nullptr, 0 },
    { "bypass", "Bypass", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "hq_enabled", "Oversampling", 'c', 0.f, 2.f, 0.f, "", mqp::kOversampling, 3 },
    { "processing_mode", "Processing Mode", 'c', 0.f, 4.f, 0.f, "", mqp::kProcMode, 5 },
    { "q_couple_mode", "Q-Couple Mode", 'c', 0.f, 8.f, 0.f, "", mqp::kQCouple, 9 },
    { "eq_type", "EQ Type", 'c', 0.f, 3.f, 0.f, "", mqp::kEqType, 4 },
    { "match_apply", "Match Apply", 'f', -100.f, 100.f, 100.f, "%", nullptr, 0 },
    { "match_smoothing", "Match Smoothing", 'f', 1.f, 24.f, 12.f, "st", nullptr, 0 },
    { "match_limit_boost", "Match Limit Boost", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "match_limit_cut", "Match Limit Cut", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "analyzer_enabled", "Analyzer Enabled", 'b', 0.f, 1.f, 1.f, "", mqp::kOffOn, 2 },
    { "analyzer_pre_post", "Analyzer Pre/Post", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "analyzer_mode", "Analyzer Mode", 'c', 0.f, 1.f, 0.f, "", mqp::kPeakRms, 2 },
    { "analyzer_resolution", "Analyzer Resolution", 'c', 0.f, 2.f, 1.f, "", mqp::kAnaRes, 3 },
    { "analyzer_smoothing", "Analyzer Smoothing", 'c', 0.f, 3.f, 2.f, "", mqp::kAnaSmooth, 4 },
    { "analyzer_decay", "Analyzer Decay", 'f', 3.f, 60.f, 20.f, "dB/s", nullptr, 0 },
    { "display_scale_mode", "Display Scale", 'c', 0.f, 4.f, 1.f, "", mqp::kDispScale, 5 },
    { "visualize_master_gain", "Visualize Master Gain", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "british_hpf_freq", "British HPF Frequency", 'g', 20.f, 500.f, 20.f, "Hz", nullptr, 0 },
    { "british_hpf_enabled", "British HPF Enabled", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "british_lpf_freq", "British LPF Frequency", 'g', 3000.f, 20000.f, 20000.f, "Hz", nullptr, 0 },
    { "british_lpf_enabled", "British LPF Enabled", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "british_lf_gain", "British LF Gain", 'f', -20.f, 20.f, 0.f, "dB", nullptr, 0 },
    { "british_lf_freq", "British LF Frequency", 'g', 30.f, 480.f, 100.f, "Hz", nullptr, 0 },
    { "british_lf_bell", "British LF Bell Mode", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "british_lm_gain", "British LM Gain", 'f', -20.f, 20.f, 0.f, "dB", nullptr, 0 },
    { "british_lm_freq", "British LM Frequency", 'g', 200.f, 2500.f, 600.f, "Hz", nullptr, 0 },
    { "british_lm_q", "British LM Q", 'f', 0.4f, 4.f, 0.7f, "", nullptr, 0 },
    { "british_hm_gain", "British HM Gain", 'f', -20.f, 20.f, 0.f, "dB", nullptr, 0 },
    { "british_hm_freq", "British HM Frequency", 'g', 600.f, 7000.f, 2000.f, "Hz", nullptr, 0 },
    { "british_hm_q", "British HM Q", 'f', 0.4f, 4.f, 0.7f, "", nullptr, 0 },
    { "british_hf_gain", "British HF Gain", 'f', -20.f, 20.f, 0.f, "dB", nullptr, 0 },
    { "british_hf_freq", "British HF Frequency", 'g', 1500.f, 16000.f, 8000.f, "Hz", nullptr, 0 },
    { "british_hf_bell", "British HF Bell Mode", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "british_mode", "British Mode", 'c', 0.f, 1.f, 0.f, "", mqp::kBritishMode, 2 },
    { "british_saturation", "British Saturation", 'f', 0.f, 100.f, 0.f, "%", nullptr, 0 },
    { "british_input_gain", "British Input Gain", 'f', -12.f, 12.f, 0.f, "dB", nullptr, 0 },
    { "british_output_gain", "British Output Gain", 'f', -12.f, 12.f, 0.f, "dB", nullptr, 0 },
    { "pultec_lf_boost_gain", "Tube EQ LF Boost", 'f', 0.f, 10.f, 0.f, "", nullptr, 0 },
    { "pultec_lf_boost_freq", "Tube EQ LF Boost Freq", 'c', 0.f, 3.f, 2.f, "", mqp::kLfBoostF, 4 },
    { "pultec_lf_atten_gain", "Tube EQ LF Atten", 'f', 0.f, 10.f, 0.f, "", nullptr, 0 },
    { "pultec_hf_boost_gain", "Tube EQ HF Boost", 'f', 0.f, 10.f, 0.f, "", nullptr, 0 },
    { "pultec_hf_boost_freq", "Tube EQ HF Boost Freq", 'c', 0.f, 6.f, 3.f, "", mqp::kHfBoostF, 7 },
    { "pultec_hf_boost_bw", "Tube EQ HF Bandwidth", 'f', 0.f, 1.f, 0.5f, "", nullptr, 0 },
    { "pultec_hf_atten_gain", "Tube EQ HF Atten", 'f', 0.f, 10.f, 0.f, "", nullptr, 0 },
    { "pultec_hf_atten_freq", "Tube EQ HF Atten Freq", 'c', 0.f, 2.f, 1.f, "", mqp::kHfAttenF, 3 },
    { "pultec_input_gain", "Tube EQ Input Gain", 'f', -12.f, 12.f, 0.f, "dB", nullptr, 0 },
    { "pultec_output_gain", "Tube EQ Output Gain", 'f', -12.f, 12.f, 0.f, "dB", nullptr, 0 },
    { "pultec_tube_drive", "Tube EQ Tube Drive", 'f', 0.f, 1.f, 0.3f, "", nullptr, 0 },
    { "pultec_mid_enabled", "Tube EQ Mid Section Enabled", 'b', 0.f, 1.f, 1.f, "", mqp::kOffOn, 2 },
    { "pultec_mid_low_freq", "Tube EQ Mid Low Freq", 'c', 0.f, 4.f, 2.f, "", mqp::kMidLowF, 5 },
    { "pultec_mid_low_peak", "Tube EQ Mid Low Peak", 'f', 0.f, 10.f, 0.f, "", nullptr, 0 },
    { "pultec_mid_dip_freq", "Tube EQ Mid Dip Freq", 'c', 0.f, 6.f, 3.f, "", mqp::kMidDipF, 7 },
    { "pultec_mid_dip", "Tube EQ Mid Dip", 'f', 0.f, 10.f, 0.f, "", nullptr, 0 },
    { "pultec_mid_high_freq", "Tube EQ Mid High Freq", 'c', 0.f, 4.f, 2.f, "", mqp::kMidHighF, 5 },
    { "pultec_mid_high_peak", "Tube EQ Mid High Peak", 'f', 0.f, 10.f, 0.f, "", nullptr, 0 },
    { "band1_dyn_enabled", "Band 1 Dynamics Enabled", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "band1_dyn_threshold", "Band 1 Threshold", 'f', -48.f, 0.f, -20.f, "dB", nullptr, 0 },
    { "band1_dyn_attack", "Band 1 Attack", 'g', 0.1f, 500.f, 10.f, "ms", nullptr, 0 },
    { "band1_dyn_release", "Band 1 Release", 'g', 10.f, 5000.f, 100.f, "ms", nullptr, 0 },
    { "band1_dyn_range", "Band 1 Range", 'f', 0.f, 24.f, 12.f, "dB", nullptr, 0 },
    { "band1_dyn_ratio", "Band 1 Ratio", 'g', 1.f, 100.f, 4.f, ":1", nullptr, 0 },
    { "band2_dyn_enabled", "Band 2 Dynamics Enabled", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "band2_dyn_threshold", "Band 2 Threshold", 'f', -48.f, 0.f, -20.f, "dB", nullptr, 0 },
    { "band2_dyn_attack", "Band 2 Attack", 'g', 0.1f, 500.f, 10.f, "ms", nullptr, 0 },
    { "band2_dyn_release", "Band 2 Release", 'g', 10.f, 5000.f, 100.f, "ms", nullptr, 0 },
    { "band2_dyn_range", "Band 2 Range", 'f', 0.f, 24.f, 12.f, "dB", nullptr, 0 },
    { "band2_dyn_ratio", "Band 2 Ratio", 'g', 1.f, 100.f, 4.f, ":1", nullptr, 0 },
    { "band3_dyn_enabled", "Band 3 Dynamics Enabled", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "band3_dyn_threshold", "Band 3 Threshold", 'f', -48.f, 0.f, -20.f, "dB", nullptr, 0 },
    { "band3_dyn_attack", "Band 3 Attack", 'g', 0.1f, 500.f, 10.f, "ms", nullptr, 0 },
    { "band3_dyn_release", "Band 3 Release", 'g', 10.f, 5000.f, 100.f, "ms", nullptr, 0 },
    { "band3_dyn_range", "Band 3 Range", 'f', 0.f, 24.f, 12.f, "dB", nullptr, 0 },
    { "band3_dyn_ratio", "Band 3 Ratio", 'g', 1.f, 100.f, 4.f, ":1", nullptr, 0 },
    { "band4_dyn_enabled", "Band 4 Dynamics Enabled", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "band4_dyn_threshold", "Band 4 Threshold", 'f', -48.f, 0.f, -20.f, "dB", nullptr, 0 },
    { "band4_dyn_attack", "Band 4 Attack", 'g', 0.1f, 500.f, 10.f, "ms", nullptr, 0 },
    { "band4_dyn_release", "Band 4 Release", 'g', 10.f, 5000.f, 100.f, "ms", nullptr, 0 },
    { "band4_dyn_range", "Band 4 Range", 'f', 0.f, 24.f, 12.f, "dB", nullptr, 0 },
    { "band4_dyn_ratio", "Band 4 Ratio", 'g', 1.f, 100.f, 4.f, ":1", nullptr, 0 },
    { "band5_dyn_enabled", "Band 5 Dynamics Enabled", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "band5_dyn_threshold", "Band 5 Threshold", 'f', -48.f, 0.f, -20.f, "dB", nullptr, 0 },
    { "band5_dyn_attack", "Band 5 Attack", 'g', 0.1f, 500.f, 10.f, "ms", nullptr, 0 },
    { "band5_dyn_release", "Band 5 Release", 'g', 10.f, 5000.f, 100.f, "ms", nullptr, 0 },
    { "band5_dyn_range", "Band 5 Range", 'f', 0.f, 24.f, 12.f, "dB", nullptr, 0 },
    { "band5_dyn_ratio", "Band 5 Ratio", 'g', 1.f, 100.f, 4.f, ":1", nullptr, 0 },
    { "band6_dyn_enabled", "Band 6 Dynamics Enabled", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "band6_dyn_threshold", "Band 6 Threshold", 'f', -48.f, 0.f, -20.f, "dB", nullptr, 0 },
    { "band6_dyn_attack", "Band 6 Attack", 'g', 0.1f, 500.f, 10.f, "ms", nullptr, 0 },
    { "band6_dyn_release", "Band 6 Release", 'g', 10.f, 5000.f, 100.f, "ms", nullptr, 0 },
    { "band6_dyn_range", "Band 6 Range", 'f', 0.f, 24.f, 12.f, "dB", nullptr, 0 },
    { "band6_dyn_ratio", "Band 6 Ratio", 'g', 1.f, 100.f, 4.f, ":1", nullptr, 0 },
    { "band7_dyn_enabled", "Band 7 Dynamics Enabled", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "band7_dyn_threshold", "Band 7 Threshold", 'f', -48.f, 0.f, -20.f, "dB", nullptr, 0 },
    { "band7_dyn_attack", "Band 7 Attack", 'g', 0.1f, 500.f, 10.f, "ms", nullptr, 0 },
    { "band7_dyn_release", "Band 7 Release", 'g', 10.f, 5000.f, 100.f, "ms", nullptr, 0 },
    { "band7_dyn_range", "Band 7 Range", 'f', 0.f, 24.f, 12.f, "dB", nullptr, 0 },
    { "band7_dyn_ratio", "Band 7 Ratio", 'g', 1.f, 100.f, 4.f, ":1", nullptr, 0 },
    { "band8_dyn_enabled", "Band 8 Dynamics Enabled", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "band8_dyn_threshold", "Band 8 Threshold", 'f', -48.f, 0.f, -20.f, "dB", nullptr, 0 },
    { "band8_dyn_attack", "Band 8 Attack", 'g', 0.1f, 500.f, 10.f, "ms", nullptr, 0 },
    { "band8_dyn_release", "Band 8 Release", 'g', 10.f, 5000.f, 100.f, "ms", nullptr, 0 },
    { "band8_dyn_range", "Band 8 Range", 'f', 0.f, 24.f, 12.f, "dB", nullptr, 0 },
    { "band8_dyn_ratio", "Band 8 Ratio", 'g', 1.f, 100.f, 4.f, ":1", nullptr, 0 },
    { "dyn_detection_mode", "Dynamics Detection Mode", 'c', 0.f, 1.f, 0.f, "", mqp::kPeakRms, 2 },
    { "auto_gain_enabled", "Auto Gain", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "limiter_enabled", "Limiter", 'b', 0.f, 1.f, 0.f, "", mqp::kOffOn, 2 },
    { "limiter_ceiling", "Limiter Ceiling", 'f', -1.f, 0.f, 0.f, "", nullptr, 0 },
};

// Compile-time guards: the named indices must line up with the table's layout.
static_assert(kParamMasterGain == 82, "band block must be 82 params");
static_assert(mqidx::pan(7) == 81, "band8 pan must be index 81");
static_assert(kParamPultecMidHighPeak == 137, "tube block must end at 137");
static_assert(kMqDynBase == 138, "dyn block must start at 138");
static_assert(mqidx::dynRatio(7) == 185, "dyn block must end at 185");
static_assert(kParamLimiterCeiling == 189 && kParamCount == 190, "must total 190 params");
