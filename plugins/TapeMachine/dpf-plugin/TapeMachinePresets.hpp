// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// TapeMachinePresets.hpp — calibrated factory presets mapped onto TapeMachine 2's
// parameters. Each preset captures its deck, speed, tape formula, EQ,
// calibration, head width, drive, and reproduce-EQ settings.
//
// Gain link (Auto Compensation) is ON for every preset: driving the tape via Input
// adds saturation while the output holds unity (-12 dBFS in -> -12 dBFS out), which
// is how the reference operates at its calibrated -12 dBFS internal level. Each preset then
// carries a small per-preset Output makeup trim (outTrim, applied on top of the link)
// that matches the loudness of the reference preset it was curated from — the reference
// presets dial their own record/repro levels, so their net output is not always unity.

#pragma once

#include "TapeMachineParams.hpp"

struct TmPreset
{
    const char* name;
    const char* category;
    int   tapeMachine;   // 0 Swiss (Swiss) / 1 American (American)
    int   tapeSpeed;     // 0 7.5 / 1 15 / 2 30 / 3 3.75 IPS
    int   tapeType;      // 0 456 / 1 GP9 / 2 900 / 3 250
    int   eqStandard;    // 0 NAB / 1 CCIR
    int   calibration;   // 0 +3 / 1 +6 / 2 +7.5 / 3 +9 dB
    int   signalPath;    // 0 Repro / 1 Sync / 2 Input / 3 Thru
    int   headWidth;     // 0 1/4" / 1 1/2" / 2 1"  (American only; ignored on Swiss)
    float inputGain;     // dB — the tape "drive"/colour control
    bool  autoCal;       // auto bias (calibrated) vs the manual bias value below
    float bias;          // % (50 = optimal); used only when autoCal is false
    float highpassFreq, lowpassFreq;
    float wow, flutter, noiseAmount;
    float reproLf, reproLmf, reproHmf, reproHf;  // advanced repro-head 4-band EQ (dB); 0 = neutral (rows may omit -> 0)
    float outTrim;  // per-preset OUTPUT makeup trim (dB) applied on top of the gain-link
                    // inverse (output = -input + outTrim); matches the reference preset's
                    // own non-unity output level. Post-tape LINEAR gain: shifts loudness only,
                    // transparent to THD/FR/aliasing. 0 = unity; every row sets it explicitly.
    bool  crosstalk = false;  // American-only adjacent-track bleed toggle, matching the reference
                    // preset's own Crosstalk switch. Omitted (=false) on Swiss presets (that deck
                    // models zero crosstalk) and on American presets whose reference ships it Off;
                    // set true only on American presets whose reference ships Crosstalk On.
    float levelHmfTrim = 0.0f; // hidden above-anchor presence correction data (full-factor dB)
    float levelHfTrim  = 0.0f; // hidden above-anchor top-octave correction data (full-factor dB)
    float lpQ = 0.707f; // lowpass SVF resonance. 0.707 (default) = the historic fixed Butterworth Q;
                    // lo-fi cassette-style presets raise it to model the reference's head-resonance
                    // peak right before the LP cliff (a +/-/+ FR wiggle the 4-band repro EQ cannot make).
    float progHmfTrim = 0.0f; // hidden PROGRAM-BAND above-anchor presence trim (6.3 kHz; Phase C).
    float progHfTrim  = 0.0f; // hidden PROGRAM-BAND above-anchor top-octave trim (11 kHz; Phase C).
                    // Keyed off a 500 Hz low-corner program envelope (NOT the broadband detector),
                    // so they shape sustained-program HF yet stay bypassed on the 1 kHz THD tone =>
                    // byte-identical THD. Only Drum Bus / Old Tape / Sunbaked carry nonzero values.
    float reproSubBell = 0.0f; // per-preset repro sub-bell (31 Hz Q2.5, dB). Fills the American-30
                    // head-bump's narrow LF dip WITHOUT lifting 50 Hz (a shelf would). Exact bypass
                    // at 0 in the DSP => byte-identical everywhere it is 0. Only GP9 Drum Bus is nonzero.
    float progLfTrim = 0.0f; // hidden PROGRAM-BAND deep-sub bloom restore (33 Hz low-shelf, dB; EAR-GREEN).
                    // Adds the reference decks' deep-sub program thickening (measured ref pink-minus-sweep
                    // ~+5 dB @25 Hz @15 IPS) that mine lacked. Keyed off the 500 Hz program envelope =>
                    // neutral on the -12 dBFS sweep/THD tone (byte-identical). Nonzero on 15/7.5 IPS presets.
};

// Preset field order:
//   name, category, machine, speed, type, eq, cal, path, head,
//   inGain, autoCal, bias, hp, lp, wow, flutter, noise,
//   reproLf, reproLmf, reproHmf, reproHf, outTrim, [crosstalk, levelHmfTrim, levelHfTrim, lpQ, progHmfTrim, progHfTrim, reproSubBell, progLfTrim]
// The trailing fields default to false/0/0/0.707/0/0/0. Level trims are multiplied by the DSP's existing
// above-anchor factor, hence are exactly neutral at the -12 dBFS calibration anchor. lpQ only
// deviates from 0.707 on lo-fi presets that model the reference's LP-cliff head resonance. progHmf/HfTrim
// are keyed off a 500 Hz PROGRAM-band envelope (Phase C), neutral on the -12 dBFS THD/sweep stimuli.
static constexpr TmPreset kTmPresets[] =
{
    // Input gain controls drive while the reproduce-EQ bands shape the calibrated
    // response. Higher calibration levels saturate more, and drive-linked HF
    // compensation keeps hot presets balanced.
    // ---- American: mixdown / mastering (2-bus glue) --------------------
    { "Big 456 Master", "American Master", 1, 1, 0, 0, 0, 0, 1, 2.7f, true, 50.f, 20.f, 20000.f, 2.f, 1.f, 65.6f, 0.7f, 0.5f, -0.5f, 2.1f, 0.f, true, 0.0f, 0.0f, 0.707f, 0.0f, 0.0f, 0.0f, 18.0f },
    { "Nice 456 Master", "American Master", 1, 2, 0, 0, 0, 0, 1, 2.6f, true, 50.f, 20.f, 20000.f, 1.f, 1.f, 62.1f, 1.1f, 0.4f, -3.f, 0.7f, 0.f, true, 0.0f, 0.0f, 0.707f, 0.0f, -5.0f, 0.0f, 5.0f },
    { "Jazz Vision Master", "American Master", 1, 1, 0, 0, 0, 0, 1, -4.9f, true, 50.f, 20.f, 20000.f, 1.f, 1.f, 29.5f, -0.7f, 1.f, -1.1f, 6.9f, -0.3f, true },
    { "Clean 900 Master", "American Master", 1, 1, 2, 1, 2, 0, 1, -12.f, true, 50.f, 20.f, 20000.f, 1.f, 1.f, 0.f, -2.3f, -0.2f, 4.5f, 4.7f, 0.f },

    // ---- American: colour / character ----------------------------------
    { "Fat 456 Master", "American Color", 1, 1, 0, 0, 0, 1, 1, 2.7f, true, 50.f, 45.f, 20000.f, 3.f, 2.f, 62.0f, 4.0f, 2.8f, 2.f, -0.3f, -0.5f, true, 0.0f, 0.0f, 0.707f, 0.0f, 0.0f, 0.0f, 9.0f },
    { "GP9 Drum Bus", "American Color", 1, 2, 1, 0, 1, 0, 2, 6.8f, true, 50.f, 20.f, 20000.f, 3.f, 2.f, 44.9f, 2.5f, -0.6f, -3.6f, -0.1f, 1.8f, false, 0.0f, -10.0f, 0.707f, 0.0f, 0.0f, 1.0f },
    { "Bass Bump", "American Color", 1, 1, 3, 0, 3, 0, 1, 7.8f, false, 72.f, 20.f, 20000.f, 3.f, 2.f, 96.7f, -0.5f, 0.4f, 0.6f, 1.6f, -1.0f, false, 0.0f, 0.0f, 0.707f, 0.0f, -6.0f, 0.0f, 16.5f },
    { "Bright & Sizzly", "American Color", 1, 0, 1, 0, 0, 0, 0, -10.2f, false, 26.f, 20.f, 19000.f, 5.f, 3.f, 18.1f, -3.1f, 0.3f, -0.f, 0.8f, -0.7f, false, 0.0f, -3.0f, 0.707f, 0.0f, -3.0f, 0.0f, 15.0f },

    // ---- Swiss: mix / tracking ----------------------------------------
    { "Classic Rock Crisp", "Swiss Mix", 0, 1, 3, 0, 1, 0, 1, 0.0f, true, 50.f, 20.f, 20000.f, 0.f, 0.f, 2.6f, -0.3f, 0.2f, -0.9f, 0.5f, 0.4f, false, 0.0f, 0.0f, 0.707f, -1.0f, -6.0f, 0.0f, 7.0f },
    { "Modern Rock", "Swiss Mix", 0, 1, 0, 0, 1, 0, 1, 5.0f, true, 50.f, 20.f, 20000.f, 0.f, 0.f, 4.0f, -0.2f, 0.2f, -1.f, 1.2f, 0.9f, false, 0.0f, 0.0f, 0.707f, -2.0f, -8.0f, 0.0f, 8.0f },
    // progHmf/HfTrim -14/-14 (Phase C): mine is uniformly +6 dB too bright on sustained program
    // (6.3-16 kHz); the program-band-keyed cut closes it (sustHF 6.12 -> 0.38) while THD stays
    // byte-identical (the 1 kHz tone is below the prog envelope's -12 anchor). Phase B reproHf=8.7 kept.
    // outTrim 1.5 -> 2.53: the HF cut removed ~1.46 dB broadband on loud program (pink loud-region
    // dRMS), so the makeup restores loud-region loudness parity (Cat1 dRMS -1.03 -> 0.0). outTrim is
    // a post-tape LINEAR gain (THD %/FR/alias invariant; it does scale the render level).
    { "Drum Bus", "Swiss Mix", 0, 1, 1, 0, 1, 1, 1, 10.9f, false, 62.f, 20.f, 20000.f, 0.f, 0.f, 0.f, -1.7f, 0.4f, -0.4f, 8.7f, 2.53f, false, 0.0f, 0.0f, 0.707f, -14.0f, -14.0f, 0.0f, 17.5f },
    { "Hi-Fi Shine", "Swiss Mix", 0, 2, 1, 0, 1, 0, 1, 5.1f, true, 50.f, 20.f, 20000.f, 0.f, 0.f, 0.f, 0.6f, 0.3f, -0.8f, 5.5f, 0.3f },
    { "Lush Film", "Swiss Mix", 0, 1, 2, 0, 0, 1, 1, 3.1f, true, 50.f, 20.f, 20000.f, 0.f, 0.f, 0.f, -1.7f, 0.5f, -0.8f, 1.f, 0.9f, false, 0.0f, 0.0f, 0.707f, 0.0f, -5.0f, 0.0f, 5.5f },
    { "Jazz Warmth", "Swiss Mix", 0, 2, 2, 0, 0, 0, 1, -9.1f, true, 50.f, 20.f, 20000.f, 0.f, 0.f, 0.f, 0.6f, 0.4f, 0.2f, -0.6f, 0.f },

    // ---- Swiss: character / saturation --------------------------------
    { "Thick Saturation", "Swiss Color", 0, 1, 1, 0, 3, 1, 1, 2.2f, false, 40.f, 20.f, 20000.f, 0.f, 0.f, 3.7f, -1.8f, 0.7f, 0.0f, 6.1f, 0.0f, false, 0.0f, -5.0f, 0.707f, -8.0f, -8.0f, 0.0f, 19.0f },
    { "Hip-Hop Punch", "Swiss Color", 0, 1, 1, 0, 2, 1, 1, 6.8f, true, 50.f, 20.f, 20000.f, 0.f, 0.f, 6.3f, -0.1f, 0.5f, -0.6f, 7.8f, 0.f, false, 0.0f, -2.0f, 0.707f, -8.0f, -18.0f, 0.0f, 15.5f },
    { "Vocal Presence", "Swiss Color", 0, 2, 2, 0, 3, 1, 1, 11.8f, false, 64.2f, 20.f, 20000.f, 0.f, 0.f, 0.f, 0.5f, 0.5f, -1.6f, 1.7f, 2.5f, false, 0.0f, -7.0f, 0.707f, 0.0f, 10.0f },

    // ---- Lo-Fi (both decks) -------------------------------------------------
    // Sunbaked gets NO prog trim (Phase C wall): its 78% over-bias + +3 cal put the operating flux
    // ~8.8 dB BELOW the -12 anchor on -6 dBFS program, so the above-anchor-keyed prog factor (and
    // the existing levelFactor) are exactly 0 for it — the mechanism cannot engage. Its residual
    // sustHF (~3.4, mostly the 12.5/16 kHz LP-cliff bands past its 10.8 kHz lowpass) is a below-
    // anchor/static-tone effect the above-anchor trim is the wrong tool for. Left byte-identical.
    { "Sunbaked Cassette", "Lo-Fi", 1, 3, 2, 0, 0, 0, 0, 10.9f, false, 78.f, 34.f, 10800.f, 18.f, 12.f, 88.7f, 0.75f, -3.8f, -4.7f, -8.3f, -4.6f, true, 0.0f, 0.0f, 0.89f },
    { "Analog Warmth", "Lo-Fi", 1, 3, 3, 0, 3, 0, 0, -12.f, false, 37.3f, 30.f, 12000.f, 14.f, 10.f, 25.1f, 3.1f, -0.1f, -7.5f, 9.1f, -0.4f, true, -2.0f, -14.0f, 0.707f, 8.0f, 20.0f, 0.0f, 15.5f },
    // Old Tape is Swiss (machine 0); the reference tracking deck has NO wow/flutter, so its
    // W&F is 0/0 by design (2026-07-12 authenticity decision) — loses the old 12/8 lo-fi wobble.
    // EAR-GREEN (2026-07-17c, re-based on PROGRAM/pink, not the synth sustain stimulus):
    //   progLfTrim 20.5 restores the reference's deep-sub program bloom (ear_audit sub -3.5 -> +0.3).
    //   progHmf/HfTrim -8/-20 CUT the top: on pink mine reads BRIGHT (air +3.9, tilt -5.2), the
    //   program-band-keyed cut closes most of it (tilt -> -0.64). Byte-identical on the -12 tone
    //   (progFactor 0). A prior Phase-C +14/+10 BOOST was fit to the sustain stimulus (no real HF ->
    //   mis-read this LP@12k lo-fi preset as DARK) and was reverted; the residual bright tilt -0.64 +
    //   bass3rd -9.1 are documented lo-fi / memoryless-shaper walls. outTrim 0.9 (boost makeup reverted).
    { "Old Tape", "Lo-Fi", 0, 0, 3, 0, 2, 1, 1, 2.5f, false, 42.f, 30.f, 12000.f, 0.f, 0.f, 27.7f, -6.2f, 1.2f, -2.4f, -10.9f, 0.9f, false, 0.0f, 0.0f, 0.707f, -8.0f, -20.0f, 0.0f, 20.5f },
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
    // American front-panel toggles. Crosstalk mirrors the reference preset's own switch
    // (p.crosstalk); the Swiss deck models zero crosstalk so its presets leave it Off (moot).
    // W&F/Transformer stay On (the state the American tuning captured).
    set(kParamCrosstalk,    p.crosstalk ? 1.0f : 0.0f);
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
    set(kParamLevelHmfTrim, p.levelHmfTrim);
    set(kParamLevelHfTrim,  p.levelHfTrim);
    set(kParamLpQ,          p.lpQ);
    set(kParamProgHmfTrim,  p.progHmfTrim);
    set(kParamProgHfTrim,   p.progHfTrim);
    set(kParamProgLfTrim,   p.progLfTrim);
    set(kParamReproSubBell, p.reproSubBell);
}
