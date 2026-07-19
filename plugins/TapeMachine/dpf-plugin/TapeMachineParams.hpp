// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// TapeMachineParams.hpp — parameter ids, ranges and choice labels shared by the
// DSP shell (initParameter / setParameterValue) and the UI.
//
// Order mirrors the JUCE TapeMachine parameter layout exactly (values are host-
// automation indices, not display strings). Public labels use Dusk Audio's
// generic deck and formulation names.

#pragma once

enum ParamId
{
    kParamTapeMachine = 0, // choice: Swiss / American
    kParamTapeSpeed,       // choice: 7.5 / 15 / 30 / 3.75 IPS (3.75 appended, American-only)
    kParamTapeType,        // choice: 456 / GP9 / 900 / 250
    kParamSignalPath,      // choice: Repro / Sync / Input / Thru
    kParamEqStandard,      // choice: NAB / CCIR
    kParamInputGain,       // -12..12 dB  (this is the real tape-drive control)
    kParamBias,            // 0..100 % (50 = optimal)
    kParamCalibration,     // choice: +3 / +6 / +7.5 / +9 dB
    kParamAutoCal,         // choice: Off / On
    kParamHighpassFreq,    // 20..500 Hz (skew 0.5)
    kParamLowpassFreq,     // 3000..20000 Hz (skew 0.5)
    kParamNoiseAmount,     // 0..100 %
    kParamNoiseEnabled,    // choice: Off / On
    kParamWow,             // 0..100 %
    kParamFlutter,         // 0..100 %
    kParamOutputGain,      // -12..12 dB
    kParamAutoComp,        // choice: Off / On
    kParamOversampling,    // choice: 1x / 2x / 4x
    kParamHeadWidth,       // choice: 1/4" / 1/2" / 1"  (American only; ignored on Swiss)
    // American front-panel toggles (American only; hidden + ignored on Swiss). Each
    // defaults ON = the state the American tuning captured, so defaults are byte-identical.
    kParamCrosstalk,       // choice: Off / On  (American adjacent-track crosstalk bleed)
    kParamWowFlutterOn,    // choice: Off / On  (American Wow & Flutter master enable; gates the W&F knobs)
    kParamTransformer,     // choice: Off / On  (American output transformer: LF roll-off + 2nd-harmonic colour)
    kParamBypass,          // host-designated bypass
    // Advanced Repro EQ appended AFTER bypass so the bypass host-param ID stays fixed as
    // more params are added later (host bypass automation survives version updates).
    kParamReproLF,         // -12..12 dB repro-head LF shelf  (advanced; models the reference Repro EQ)
    kParamReproLMF,        // -12..12 dB repro-head low-mid peak (advanced)
    kParamReproHMF,        // -12..12 dB repro-head high-mid/presence peak (advanced)
    kParamReproHF,         // -12..12 dB repro-head HF shelf  (advanced)
    // Hidden factory-preset calibration data. Both gains are multiplied by the
    // above-anchor level factor in the DSP, so 0 VU / -12 dBFS remains neutral.
    kParamLevelHmfTrim,    // -24..24 dB full-factor presence correction
    kParamLevelHfTrim,     // -24..24 dB full-factor top-octave correction
    kParamLpQ,             // 0.5..2.5 lowpass resonance Q (cassette head-resonance peak at the LP cliff)
    // Hidden program-band above-anchor trims (Phase C). Keyed off a 500 Hz low-corner program
    // envelope (not the broadband detector), so they engage on sustained low-corner program yet
    // stay bypassed on the 1 kHz THD tone => byte-identical THD. Both default 0 = neutral.
    kParamProgHmfTrim,     // -24..24 dB program-band presence correction (6.3 kHz peak)
    kParamProgHfTrim,      // -24..24 dB program-band top-octave correction (11 kHz shelf)
    // Per-preset repro sub-bell (31 Hz Q2.5). HIDDEN calibration data, like the trims above: it has
    // no Advanced-panel control, and automating it would desync a preset from its fitted response.
    // Appended here (after the trims) so the earlier param IDs stay fixed. Neutral at 0 dB
    // (exact bypass in the DSP) => byte-identical on every preset that leaves it 0. Only GP9 Drum
    // Bus carries a nonzero value (fills the American-30 head-bump's narrow LF dip at ~31 Hz).
    kParamReproSubBell,    // -12..12 dB repro-head sub-bell
    // Hidden PROGRAM-BAND deep-sub bloom restore (EAR-GREEN). Keyed off the SAME 500 Hz program
    // envelope as the prog trims => byte-null on the -12 dBFS sweep / 1 kHz THD tone. Adds the
    // reference decks' deep-sub program thickening (a 33 Hz low-shelf); mine lacked it so hot presets
    // read thin/bright. The gain is fitted per preset on program pink, NOT derived from tape speed —
    // it is nonzero across every speed (see the progLfTrim notes in TapeMachinePresets.hpp for the
    // full list). Default 0 = neutral. Appended after reproSubBell so its ID stays
    // fixed (only the output-meter IDs below shift).
    kParamProgLfTrim,      // -24..24 dB program-band deep-sub bloom (33 Hz low-shelf)
    // --- output-only (meters); kept as params for generic-UI fallback ---
    kParamVuL,             // output: L peak level for the VU meter
    kParamVuR,             // output: R peak level for the VU meter
    kParamCount
};

// ---- choice label tables (index == parameter value) ------------------------
namespace tmparams
{
    static constexpr const char* kTapeMachine[] = { "Swiss", "American" };
    static constexpr const char* kTapeSpeed[]   = { "7.5 IPS", "15 IPS", "30 IPS", "3.75 IPS" };
    static constexpr const char* kTapeType[]    = { "456", "GP9", "900", "250" };
    static constexpr const char* kSignalPath[]  = { "Repro", "Sync", "Input", "Thru" };
    static constexpr const char* kEqStandard[]  = { "NAB", "CCIR" };   // both decks: NAB/CCIR only
    static constexpr const char* kCalibration[] = { "+3dB", "+6dB", "+7.5dB", "+9dB" };
    static constexpr const char* kOffOn[]       = { "Off", "On" };
    static constexpr const char* kOversampling[]= { "1x", "2x", "4x" };
    static constexpr const char* kHeadWidth[]   = { "1/4\"", "1/2\"", "1\"" };   // American head stack

    template <int N> static constexpr int count(const char* const (&)[N]) { return N; }
}

// ---- unified parameter descriptor (drives initParameter) -------------------
// kind: 'f' linear float, 'g' skewed float (log-ish), 'c' integer choice,
// 'b' boolean/bypass, 'o' output (meter).
struct TmParam
{
    const char*  id;
    const char*  name;
    char         kind;
    float        min, max, def;
    const char*  unit;
    const char* const* choices; // non-null for 'c'/'b'
    int          numChoices;
};

static constexpr TmParam kTmParams[kParamCount] =
{
    // choice counts derive from their arrays via tmparams::count() so they can't
    // drift out of sync (guards initParameter / selector against OOB choice access).
    { "tapeMachine",  "Tape Machine", 'c', 0.f, 1.f,     0.f,  "",   tmparams::kTapeMachine, tmparams::count(tmparams::kTapeMachine) },
    { "tapeSpeed",    "Tape Speed",   'c', 0.f, 3.f,     1.f,  "",   tmparams::kTapeSpeed,   tmparams::count(tmparams::kTapeSpeed) },
    { "tapeType",     "Tape Type",    'c', 0.f, 3.f,     0.f,  "",   tmparams::kTapeType,    tmparams::count(tmparams::kTapeType) },
    { "signalPath",   "Signal Path",  'c', 0.f, 3.f,     0.f,  "",   tmparams::kSignalPath,  tmparams::count(tmparams::kSignalPath) },
    { "eqStandard",   "EQ Standard",  'c', 0.f, 1.f,     0.f,  "",   tmparams::kEqStandard,  tmparams::count(tmparams::kEqStandard) },
    { "inputGain",    "Input Gain",   'f', -12.f, 12.f,  0.f,  "dB", nullptr, 0 },
    { "bias",         "Bias",         'f', 0.f, 100.f,   50.f, "%",  nullptr, 0 },
    { "calibration",  "Calibration",  'c', 0.f, 3.f,     0.f,  "",   tmparams::kCalibration, tmparams::count(tmparams::kCalibration) },
    { "autoCal",      "Auto Calibration",'c',0.f,1.f,    1.f,  "",   tmparams::kOffOn,       tmparams::count(tmparams::kOffOn) },
    { "highpassFreq", "Highpass Freq",'g', 20.f, 500.f,  20.f, "Hz", nullptr, 0 },
    { "lowpassFreq",  "Lowpass Freq", 'g', 3000.f,20000.f,20000.f,"Hz",nullptr,0 },
    { "noiseAmount",  "Noise Amount", 'f', 0.f, 100.f,   0.f,  "%",  nullptr, 0 },
    { "noiseEnabled", "Noise Enabled",'c', 0.f, 1.f,     0.f,  "",   tmparams::kOffOn,       tmparams::count(tmparams::kOffOn) },
    { "wowAmount",    "Wow",          'f', 0.f, 100.f,   7.f,  "%",  nullptr, 0 },
    { "flutterAmount","Flutter",      'f', 0.f, 100.f,   3.f,  "%",  nullptr, 0 },
    { "outputGain",   "Output Gain",  'f', -12.f, 12.f,  0.f,  "dB", nullptr, 0 },
    { "autoComp",     "Auto Compensation",'c',0.f,1.f,   1.f,  "",   tmparams::kOffOn,       tmparams::count(tmparams::kOffOn) },
    { "oversampling", "Oversampling", 'c', 0.f, 2.f,     1.f,  "",   tmparams::kOversampling,tmparams::count(tmparams::kOversampling) },
    { "headWidth",    "Head Width",   'c', 0.f, 2.f,     1.f,  "",   tmparams::kHeadWidth,   tmparams::count(tmparams::kHeadWidth) },
    { "crosstalk",    "Crosstalk",    'c', 0.f, 1.f,     1.f,  "",   tmparams::kOffOn,       tmparams::count(tmparams::kOffOn) },
    { "wowFlutterOn", "Wow & Flutter",'c', 0.f, 1.f,     1.f,  "",   tmparams::kOffOn,       tmparams::count(tmparams::kOffOn) },
    { "transformer",  "Transformer",  'c', 0.f, 1.f,     1.f,  "",   tmparams::kOffOn,       tmparams::count(tmparams::kOffOn) },
    { "bypass",       "Bypass",       'b', 0.f, 1.f,     0.f,  "",   tmparams::kOffOn,       tmparams::count(tmparams::kOffOn) },
    { "reproLF",      "Repro LF",     'f', -12.f, 12.f,  0.f,  "dB", nullptr, 0 },
    { "reproLMF",     "Repro LMF",    'f', -12.f, 12.f,  0.f,  "dB", nullptr, 0 },
    { "reproHMF",     "Repro HMF",    'f', -12.f, 12.f,  0.f,  "dB", nullptr, 0 },
    { "reproHF",      "Repro HF",     'f', -12.f, 12.f,  0.f,  "dB", nullptr, 0 },
    { "levelHmfTrim", "Level HMF Trim",'f',-24.f, 24.f,  0.f,  "dB", nullptr, 0 },
    { "levelHfTrim",  "Level HF Trim", 'f',-24.f, 24.f,  0.f,  "dB", nullptr, 0 },
    { "lpQ",          "Lowpass Q",    'f', 0.5f, 2.5f,   0.707f,"",  nullptr, 0 },
    { "progHmfTrim",  "Prog HMF Trim",'f',-24.f, 24.f,  0.f,  "dB", nullptr, 0 },
    { "progHfTrim",   "Prog HF Trim", 'f',-24.f, 24.f,  0.f,  "dB", nullptr, 0 },
    { "reproSubBell", "Repro Sub Bell",'f',-12.f, 12.f,  0.f,  "dB", nullptr, 0 },
    { "progLfTrim",   "Prog LF Trim", 'f',-24.f, 24.f,  0.f,  "dB", nullptr, 0 },
    { "vuL",          "VU L",         'o', 0.f, 2.f,     0.f,  "",   nullptr, 0 },
    { "vuR",          "VU R",         'o', 0.f, 2.f,     0.f,  "",   nullptr, 0 },
};

// The table is POSITIONAL: kTmParams[i] must describe ParamId i. Declaring it [kParamCount]
// already rejects a surplus row (compile error), but a MISSING row is silently zero-filled from
// the end — that row would reach initParameter with a null symbol/name. These anchors pin the
// last three entries so an insertion that forgets its table row fails to compile instead.
static_assert(kTmParams[kParamCount - 1].id != nullptr, "kTmParams has a zero-filled row (missing entry)");
static_assert(kTmParams[kParamVuL].kind == 'o' && kTmParams[kParamVuR].kind == 'o',
              "kTmParams out of sync with ParamId: meters must be last and kind 'o'");
static_assert(kTmParams[kParamBypass].kind == 'b',
              "kTmParams out of sync with ParamId: bypass row moved");
