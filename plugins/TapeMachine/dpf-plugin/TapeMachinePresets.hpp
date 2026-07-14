// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// TapeMachinePresets.hpp — factory presets curated from the UAD Swiss 800 and
// Classic 102 factory banks (decoded from the plugins' own preset JSON chunks)
// and mapped onto TapeMachine 2's parameters. Each preset captures the deck +
// speed + tape formula + EQ + cal + head width + drive that defines the UAD
// preset's sound. inGain (drive) and the advanced Repro HF/LF trims are jointly
// fitted (joint_fit.py) so each preset's THD@-6 and frequency response match the
// source UAD factory preset (THD within ~0.5%, FR mostly within ~1.5 dB).
//
// Gain link (Auto Compensation) is ON for every preset: driving the tape via Input
// adds saturation while the output holds unity (-12 dBFS in -> -12 dBFS out), which
// is how the UAD operates at its calibrated -12 dBFS internal level. Each preset then
// carries a small per-preset Output makeup trim (outTrim, applied on top of the link)
// that matches the loudness of the UAD factory preset it was curated from — the UAD
// presets dial their own record/repro levels, so their net output is not always unity.

#pragma once

#include "TapeMachineParams.hpp"

struct TmPreset
{
    const char* name;
    const char* category;
    int   tapeMachine;   // 0 Swiss800 (Swiss 800) / 1 Classic102 (Classic 102)
    int   tapeSpeed;     // 0 7.5 / 1 15 / 2 30 / 3 3.75 IPS
    int   tapeType;      // 0 456 / 1 GP9 / 2 900 / 3 250
    int   eqStandard;    // 0 NAB / 1 CCIR
    int   calibration;   // 0 +3 / 1 +6 / 2 +7.5 / 3 +9 dB
    int   signalPath;    // 0 Repro / 1 Sync / 2 Input / 3 Thru
    int   headWidth;     // 0 1/4" / 1 1/2" / 2 1"  (Classic 102 only; ignored on Swiss 800)
    float inputGain;     // dB — the tape "drive"/colour control
    bool  autoCal;       // auto bias (calibrated) vs the manual bias value below
    float bias;          // % (50 = optimal); used only when autoCal is false
    float highpassFreq, lowpassFreq;
    float wow, flutter, noiseAmount;
    float reproLf, reproLmf, reproHmf, reproHf;  // advanced repro-head 4-band EQ (dB); 0 = neutral (rows may omit -> 0)
    float outTrim;  // per-preset OUTPUT makeup trim (dB) applied on top of the gain-link
                    // inverse (output = -input + outTrim); matches the UAD factory preset's
                    // own non-unity output level. Post-tape LINEAR gain: shifts loudness only,
                    // transparent to THD/FR/aliasing. 0 = unity; every row sets it explicitly.
};

// Preset field order:
//   name, category, machine, speed, type, eq, cal, path, head,
//   inGain, autoCal, bias, hp, lp, wow, flutter, noise,
//   reproLf, reproLmf, reproHmf, reproHf, outTrim
static constexpr TmPreset kTmPresets[] =
{
    // inGain + reproHf/reproLf are jointly fitted (joint_fit.py) — drive sets THD@-6,
    // the Repro EQ shelves set FR, and the two are converged together (the HF shelf
    // also shifts harmonic THD). Higher cal saturates MORE (matching the UAD); the
    // drive-linked HF restore keeps hot presets bright.
    // ---- Classic 102: mixdown / mastering (2-bus glue) --------------------
    { "Big 456 Master", "Classic 102 Master", 1, 1, 0, 0, 0, 0, 1, 2.7f, true, 50.f, 20.f, 20000.f, 2.f, 1.f, 65.6f, 0.7f, 0.5f, -0.5f, 2.1f, 0.f },
    { "Nice 456 Master", "Classic 102 Master", 1, 2, 0, 0, 0, 0, 1, 2.6f, true, 50.f, 20.f, 20000.f, 1.f, 1.f, 62.1f, 1.1f, 0.4f, -3.f, 0.7f, 0.f },
    { "Jazz Vision Master", "Classic 102 Master", 1, 1, 0, 0, 0, 0, 1, -4.9f, true, 50.f, 20.f, 20000.f, 1.f, 1.f, 29.5f, -0.7f, 1.f, -1.1f, 6.9f, -0.3f },
    { "Clean 900 Master", "Classic 102 Master", 1, 1, 2, 1, 2, 0, 1, -12.f, true, 50.f, 20.f, 20000.f, 1.f, 1.f, 0.f, -2.3f, -0.2f, 4.5f, 4.7f, 0.f },

    // ---- Classic 102: colour / character ----------------------------------
    { "Fat 456 Master", "Classic 102 Color", 1, 1, 0, 0, 0, 1, 1, 2.7f, true, 50.f, 45.f, 20000.f, 3.f, 2.f, 62.0f, 4.0f, 2.8f, 2.f, -0.3f, -0.5f },
    { "GP9 Drum Bus", "Classic 102 Color", 1, 2, 1, 0, 1, 0, 2, 6.8f, true, 50.f, 20.f, 20000.f, 3.f, 2.f, 44.9f, 2.5f, -0.6f, -3.6f, -0.1f, 1.4f },
    { "Massive Bass", "Classic 102 Color", 1, 1, 3, 0, 3, 0, 1, 7.8f, false, 72.f, 20.f, 20000.f, 3.f, 2.f, 96.7f, -0.7f, 0.4f, 0.6f, 1.6f, -1.0f },
    { "Bright & Sizzly", "Classic 102 Color", 1, 0, 1, 0, 0, 0, 0, -10.2f, false, 26.f, 20.f, 19000.f, 5.f, 3.f, 18.1f, -3.1f, 0.3f, -0.f, 0.8f, -0.8f },

    // ---- Swiss 800: mix / tracking ----------------------------------------
    { "Classic Rock Crisp", "Swiss 800 Mix", 0, 1, 3, 0, 1, 0, 1, 0.0f, true, 50.f, 20.f, 20000.f, 0.f, 0.f, 2.6f, -0.3f, 0.2f, -0.9f, 0.5f, 0.4f },
    { "Modern Rock", "Swiss 800 Mix", 0, 1, 0, 0, 1, 0, 1, 5.0f, true, 50.f, 20.f, 20000.f, 0.f, 0.f, 4.0f, -0.2f, 0.2f, -1.f, 1.2f, 0.9f },
    { "Drum Bus", "Swiss 800 Mix", 0, 1, 1, 0, 1, 1, 1, 10.9f, false, 62.f, 20.f, 20000.f, 0.f, 0.f, 0.f, -1.7f, 0.4f, -0.4f, 7.4f, 1.5f },
    { "Hi-Fi Shine", "Swiss 800 Mix", 0, 2, 1, 0, 1, 0, 1, 5.1f, true, 50.f, 20.f, 20000.f, 0.f, 0.f, 0.f, 0.6f, 0.3f, -0.8f, 5.5f, 0.3f },
    { "Lush Film", "Swiss 800 Mix", 0, 1, 2, 0, 0, 1, 1, 3.1f, true, 50.f, 20.f, 20000.f, 0.f, 0.f, 0.f, -1.7f, 0.5f, -0.8f, 1.f, 0.9f },
    { "Jazz Warmth", "Swiss 800 Mix", 0, 2, 2, 0, 0, 0, 1, -9.1f, true, 50.f, 20.f, 20000.f, 0.f, 0.f, 0.f, 0.6f, 0.4f, 0.2f, -0.6f, 0.f },

    // ---- Swiss 800: character / saturation --------------------------------
    { "Thick Saturation", "Swiss 800 Color", 0, 1, 1, 0, 3, 1, 1, 2.2f, false, 40.f, 20.f, 20000.f, 0.f, 0.f, 3.7f, -1.8f, 0.7f, 0.0f, 6.1f, -0.7f },
    { "Hip-Hop Punch", "Swiss 800 Color", 0, 1, 1, 0, 2, 1, 1, 6.8f, true, 50.f, 20.f, 20000.f, 0.f, 0.f, 6.3f, -0.1f, 0.5f, -0.6f, 7.8f, 0.f },
    { "Vocal Presence", "Swiss 800 Color", 0, 2, 2, 0, 3, 1, 1, 11.8f, false, 64.2f, 20.f, 20000.f, 0.f, 0.f, 0.f, 0.5f, 0.5f, -1.6f, 1.7f, 2.5f },

    // ---- Lo-Fi (both decks) -------------------------------------------------
    { "Sunbaked Cassette", "Lo-Fi", 1, 3, 2, 0, 0, 0, 0, 10.9f, false, 78.f, 39.f, 10000.f, 18.f, 12.f, 88.7f, 2.2f, -4.1f, -4.9f, -4.4f, -4.0f },
    { "Analog Warmth", "Lo-Fi", 1, 3, 3, 0, 3, 0, 0, -12.f, false, 37.3f, 30.f, 12000.f, 14.f, 10.f, 25.1f, 3.1f, -0.1f, -7.5f, 9.1f, -0.4f },
    // Old Tape is Swiss 800 (machine 0); the UAD Studer A800 has NO wow/flutter, so its
    // W&F is 0/0 by design (2026-07-12 authenticity decision) — loses the old 12/8 lo-fi wobble.
    { "Old Tape", "Lo-Fi", 0, 0, 3, 0, 2, 1, 1, 2.5f, false, 42.f, 30.f, 12000.f, 0.f, 0.f, 24.5f, -6.2f, 1.2f, -2.4f, -10.9f, 0.9f },
};

static constexpr int kNumTmPresets = (int)(sizeof(kTmPresets) / sizeof(kTmPresets[0]));

// Apply a preset by invoking setter(paramId, value) for each parameter it owns.
// Used by both the plugin (loadProgram) and the UI (preset combo) so the two stay
// in lockstep. Gain link is forced on; Output Gain carries the preset's makeup trim
// (added to the link's -input inverse). Parameters not listed keep their current value.
template <class SetFn>
inline void tmApplyPreset(int idx, SetFn&& set)
{
    if (idx < 0 || idx >= kNumTmPresets) return;
    const TmPreset& p = kTmPresets[idx];
    set(kParamTapeMachine,  (float)p.tapeMachine);
    set(kParamTapeSpeed,    (float)p.tapeSpeed);
    set(kParamTapeType,     (float)p.tapeType);
    set(kParamEqStandard,   (float)p.eqStandard);
    set(kParamCalibration,  (float)p.calibration);
    set(kParamSignalPath,   (float)p.signalPath);
    set(kParamHeadWidth,    (float)p.headWidth);
    // ATR-102 front-panel toggles: every factory preset was curated from the UAD
    // default state (Crosstalk/W&F/Transformer all On), which is also the state the
    // Classic 102 tuning captured — force On so preset recall always uses the fitted path.
    set(kParamCrosstalk,    1.0f);
    set(kParamWowFlutterOn, 1.0f);
    set(kParamTransformer,  1.0f);
    set(kParamInputGain,    p.inputGain);
    set(kParamOutputGain,   p.outTrim);                 // makeup trim on top of the gain-link inverse
    set(kParamAutoComp,     1.0f);                      // gain link on (unity + outTrim output)
    set(kParamAutoCal,      p.autoCal ? 1.f : 0.f);
    set(kParamBias,         p.bias);
    set(kParamHighpassFreq, p.highpassFreq);
    set(kParamLowpassFreq,  p.lowpassFreq);
    set(kParamWow,          p.wow);
    set(kParamFlutter,      p.flutter);
    set(kParamNoiseAmount,  p.noiseAmount);
    // Sync the noise on/off toggle to the preset: a preset with noise must activate it
    // regardless of the user's previous state; zero amount disables it.
    set(kParamNoiseEnabled, p.noiseAmount > 0.0f ? 1.0f : 0.0f);
    set(kParamReproLF,      p.reproLf);
    set(kParamReproLMF,     p.reproLmf);
    set(kParamReproHMF,     p.reproHmf);
    set(kParamReproHF,      p.reproHf);
}
