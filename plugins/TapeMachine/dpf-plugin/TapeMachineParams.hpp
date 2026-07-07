// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// TapeMachineParams.hpp — parameter ids, ranges and choice labels shared by the
// DSP shell (initParameter / setParameterValue) and the UI.
//
// Order mirrors the JUCE TapeMachine parameter layout exactly (values are host-
// automation indices, not display strings). Trademark tape-stock / machine
// names are kept verbatim per the product owner's decision (this plugin is an
// approved exception to the migration's no-trademark guideline).

#pragma once

enum ParamId
{
    kParamTapeMachine = 0, // choice: Swiss 800 / Classic 102
    kParamTapeSpeed,       // choice: 7.5 / 15 / 30 IPS
    kParamTapeType,        // choice: 456 / GP9 / 911 / 250
    kParamSignalPath,      // choice: Repro / Sync / Input / Thru
    kParamEqStandard,      // choice: NAB / CCIR / AES
    kParamInputGain,       // -12..12 dB  (this is the real tape-drive control)
    kParamBias,            // 0..100 % (50 = optimal)
    kParamCalibration,     // choice: 0 / +3 / +6 / +9 dB
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
    kParamBypass,          // host-designated bypass
    // --- output-only (meters); kept as params for generic-UI fallback ---
    kParamVuL,             // output: L peak level for the VU meter
    kParamVuR,             // output: R peak level for the VU meter
    kParamCount
};

// ---- choice label tables (index == parameter value) ------------------------
namespace tmparams
{
    static constexpr const char* kTapeMachine[] = { "Swiss 800", "Classic 102" };
    static constexpr const char* kTapeSpeed[]   = { "7.5 IPS", "15 IPS", "30 IPS" };
    static constexpr const char* kTapeType[]    = { "Type 456", "Type GP9", "Type 911", "Type 250" };
    static constexpr const char* kSignalPath[]  = { "Repro", "Sync", "Input", "Thru" };
    static constexpr const char* kEqStandard[]  = { "NAB", "CCIR", "AES" };
    static constexpr const char* kCalibration[] = { "0dB", "+3dB", "+6dB", "+9dB" };
    static constexpr const char* kOffOn[]       = { "Off", "On" };
    static constexpr const char* kOversampling[]= { "1x", "2x", "4x" };

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
    { "tapeSpeed",    "Tape Speed",   'c', 0.f, 2.f,     1.f,  "",   tmparams::kTapeSpeed,   tmparams::count(tmparams::kTapeSpeed) },
    { "tapeType",     "Tape Type",    'c', 0.f, 3.f,     0.f,  "",   tmparams::kTapeType,    tmparams::count(tmparams::kTapeType) },
    { "signalPath",   "Signal Path",  'c', 0.f, 3.f,     0.f,  "",   tmparams::kSignalPath,  tmparams::count(tmparams::kSignalPath) },
    { "eqStandard",   "EQ Standard",  'c', 0.f, 2.f,     0.f,  "",   tmparams::kEqStandard,  tmparams::count(tmparams::kEqStandard) },
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
    { "oversampling", "Oversampling", 'c', 0.f, 2.f,     2.f,  "",   tmparams::kOversampling,tmparams::count(tmparams::kOversampling) },
    { "bypass",       "Bypass",       'b', 0.f, 1.f,     0.f,  "",   tmparams::kOffOn,       tmparams::count(tmparams::kOffOn) },
    { "vuL",          "VU L",         'o', 0.f, 2.f,     0.f,  "",   nullptr, 0 },
    { "vuR",          "VU R",         'o', 0.f, 2.f,     0.f,  "",   nullptr, 0 },
};
