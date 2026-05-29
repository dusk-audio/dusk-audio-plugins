#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>
#include <limits>
#include "ParamIDs.h"

struct DuskAmpPreset
{
    const char* name;
    const char* category;

    // Parameters (matching APVTS IDs). DSP-path values first; trailing
    // fields all have defaults so old positional initializers stay valid
    // while new params (toggles, NAM mirrors, OS, bypass) round-trip
    // correctly on preset recall.
    int    ampMode;           // 0=DSP, 1=NAM
    float  inputGain;         // dB
    float  gateThreshold;     // dB
    float  gateRelease;       // ms
    float  preampGain;        // 0-1
    int    preampChannel;     // 0=Clean, 1=Crunch, 2=Lead
    bool   bright;
    int    toneType;          // 0=American, 1=British, 2=AC
    float  bass, mid, treble; // 0-1
    float  powerDrive;        // 0-1
    float  presence;          // 0-1
    float  resonance;         // 0-1
    float  sag;               // 0-1
    float  cabMix;            // 0-1
    float  cabHiCut;          // Hz
    float  cabLoCut;          // Hz
    float  delayTime;         // ms
    float  delayFeedback;     // 0-0.95
    float  delayMix;          // 0-1
    float  reverbMix;         // 0-1
    float  reverbDecay;       // 0-1
    float  outputLevel;       // dB

    // ---- new tail fields with defaults (preserves positional init) ----
    // Section enables — default to "on" for cab, "off" for FX.
    bool   cabEnabled    = true;
    bool   cabNormalize  = true;
    bool   delayEnabled  = false;
    bool   reverbEnabled = false;
    int    oversampling  = 1;    // 0=Off, 1=2x, 2=4x, 3=8x
    bool   bypass        = false;

    // NAM-path mirrors. Default to DSP-path values via sentinel:
    // NaN means "copy from DSP-path field on apply". This lets the 9
    // current factory presets (all DSP-mode) propagate their tone block
    // into the NAM-path params so flipping to NAM mode after recall
    // doesn't snap to wildcard EQ.
    float  inputGainNAM     = std::numeric_limits<float>::quiet_NaN();
    float  gateThresholdNAM = std::numeric_limits<float>::quiet_NaN();
    float  gateReleaseNAM   = std::numeric_limits<float>::quiet_NaN();
    int    toneTypeNAM      = -1;
    float  bassNAM          = std::numeric_limits<float>::quiet_NaN();
    float  midNAM           = std::numeric_limits<float>::quiet_NaN();
    float  trebleNAM        = std::numeric_limits<float>::quiet_NaN();
    float  outputLevelNAM   = std::numeric_limits<float>::quiet_NaN();

    void applyTo (juce::AudioProcessorValueTreeState& apvts) const
    {
        auto set = [&] (const juce::String& id, float val)
        {
            if (auto* param = apvts.getParameter (id))
                param->setValueNotifyingHost (param->convertTo0to1 (val));
        };
        auto setOr = [&] (const juce::String& id, float v, float fallback)
        {
            set (id, std::isnan (v) ? fallback : v);
        };

        set (DuskAmpParams::AMP_MODE,        static_cast<float> (ampMode));
        set (DuskAmpParams::INPUT_GAIN,      inputGain);
        set (DuskAmpParams::GATE_THRESHOLD,  gateThreshold);
        set (DuskAmpParams::GATE_RELEASE,    gateRelease);
        set (DuskAmpParams::PREAMP_GAIN,     preampGain);
        set (DuskAmpParams::PREAMP_CHANNEL,  static_cast<float> (preampChannel));
        set (DuskAmpParams::PREAMP_BRIGHT,   bright ? 1.0f : 0.0f);
        set (DuskAmpParams::TONE_TYPE,       static_cast<float> (toneType));
        set (DuskAmpParams::BASS,            bass);
        set (DuskAmpParams::MID,             mid);
        set (DuskAmpParams::TREBLE,          treble);
        set (DuskAmpParams::POWER_DRIVE,     powerDrive);
        set (DuskAmpParams::PRESENCE,        presence);
        set (DuskAmpParams::RESONANCE,       resonance);
        set (DuskAmpParams::SAG,             sag);
        set (DuskAmpParams::CAB_ENABLED,     cabEnabled    ? 1.0f : 0.0f);
        set (DuskAmpParams::CAB_NORMALIZE,   cabNormalize  ? 1.0f : 0.0f);
        set (DuskAmpParams::CAB_MIX,         cabMix);
        set (DuskAmpParams::CAB_HICUT,       cabHiCut);
        set (DuskAmpParams::CAB_LOCUT,       cabLoCut);
        set (DuskAmpParams::DELAY_ENABLED,   delayEnabled  ? 1.0f : 0.0f);
        set (DuskAmpParams::DELAY_TIME,      delayTime);
        set (DuskAmpParams::DELAY_FEEDBACK,  delayFeedback);
        set (DuskAmpParams::DELAY_MIX,       delayMix);
        set (DuskAmpParams::REVERB_ENABLED,  reverbEnabled ? 1.0f : 0.0f);
        set (DuskAmpParams::REVERB_MIX,      reverbMix);
        set (DuskAmpParams::REVERB_DECAY,    reverbDecay);
        set (DuskAmpParams::OUTPUT_LEVEL,    outputLevel);

        // NAM mirrors — tone stack + gate mirror the DSP-path settings so
        // flipping modes preserves character. Input gain + output level
        // default to 0 dB on the NAM side regardless of the DSP-path trim:
        // NAM profiles carry their own loudness normalization (−18 dB ref)
        // and the DSP-path output trims are tuned for the DSP signal chain,
        // not the NAM chain. A preset can still set inputGainNAM /
        // outputLevelNAM explicitly to override.
        setOr (DuskAmpParams::INPUT_GAIN_NAM,     inputGainNAM,     0.0f);
        setOr (DuskAmpParams::GATE_THRESHOLD_NAM, gateThresholdNAM, gateThreshold);
        setOr (DuskAmpParams::GATE_RELEASE_NAM,   gateReleaseNAM,   gateRelease);
        set   (DuskAmpParams::TONE_TYPE_NAM,
               static_cast<float> (toneTypeNAM >= 0 ? toneTypeNAM : toneType));
        setOr (DuskAmpParams::BASS_NAM,           bassNAM,          bass);
        setOr (DuskAmpParams::MID_NAM,            midNAM,           mid);
        setOr (DuskAmpParams::TREBLE_NAM,         trebleNAM,        treble);
        setOr (DuskAmpParams::OUTPUT_LEVEL_NAM,   outputLevelNAM,   0.0f);

        set (DuskAmpParams::OVERSAMPLING, static_cast<float> (oversampling));
        set (DuskAmpParams::BYPASS,       bypass ? 1.0f : 0.0f);
    }
};

// Factory presets — a 3 × 3 matrix (American/British/AC × Clean/Crunch/Lead).
// Each preset's TONE_TYPE drives the bundled IR auto-load:
//   tType 0 (American) → Twin Reverb 1x12 SM57
//   tType 1 (British)  → Marshall JCM800 4x12 NT1-A
//   tType 2 (AC)       → Vox AC30 2x12 Celestion Blue
// The editor calls processor.clearUserIROverride() before applyTo so the
// preset's TONE_TYPE swap re-triggers loadDefaultIRForCurrentToneType.
static const DuskAmpPreset kFactoryPresets[] =
{
    // OUTPUT_LEVEL trims measured via duskamp_di_render --loudness-sweep
    // against at_marc_fender.wav (44.1 kHz, 187 s reference DI). Target =
    // median RMS of the 9-preset set (−14.3 dBFS) with peak ≤ −1 dBFS cap.
    // Cleans are peak-bound and end up ≈ 4 dB below median (high peak-to-RMS
    // ratio of clean guitar transients) — distorted presets converge inside
    // ±1 dB of median. Re-run the sweep after any DSP voicing change that
    // affects gain stages or saturation.
    //                          mode inG  gateThr gateRel gain ch  brt tType  B    M    T    pDrv pres reso sag  cMix cHiC  cLoC dTm dFb dMix rMix rDec outLv
    { "American Clean", "Clean", 0,  0.0f, -60.0f, 50.0f, 0.40f, 0, false, 0, 0.50f, 0.50f, 0.55f, 0.20f, 0.50f, 0.50f, 0.30f, 1.0f, 12000, 60,  0, 0, 0, 0, 0,   5.8f },
    { "American Crunch","Crunch",0,  0.0f, -60.0f, 50.0f, 0.60f, 1, false, 0, 0.50f, 0.55f, 0.55f, 0.30f, 0.50f, 0.50f, 0.30f, 1.0f, 12000, 60,  0, 0, 0, 0, 0,  -2.8f },
    { "American Lead",  "Lead",  0,  0.0f, -55.0f, 40.0f, 0.80f, 2, false, 0, 0.50f, 0.60f, 0.50f, 0.40f, 0.50f, 0.50f, 0.30f, 1.0f, 11000, 70,  0, 0, 0, 0, 0, -13.3f },

    { "British Clean",  "Clean", 0,  0.0f, -60.0f, 50.0f, 0.30f, 0, false, 1, 0.50f, 0.55f, 0.55f, 0.20f, 0.50f, 0.50f, 0.30f, 1.0f, 12000, 60,  0, 0, 0, 0, 0,   5.9f },
    { "British Crunch", "Crunch",0,  0.0f, -60.0f, 50.0f, 0.60f, 1, false, 1, 0.50f, 0.60f, 0.55f, 0.40f, 0.50f, 0.50f, 0.30f, 1.0f, 11000, 70,  0, 0, 0, 0, 0,  -6.5f },
    { "British Lead",   "Lead",  0,  0.0f, -55.0f, 40.0f, 0.85f, 2, false, 1, 0.50f, 0.65f, 0.55f, 0.50f, 0.55f, 0.50f, 0.30f, 1.0f,  9500, 80,  0, 0, 0, 0, 0, -18.0f },

    { "AC Clean",       "Clean", 0,  0.0f, -60.0f, 50.0f, 0.35f, 0, true,  2, 0.45f, 0.55f, 0.60f, 0.20f, 0.55f, 0.45f, 0.30f, 1.0f, 12000, 60,  0, 0, 0, 0, 0,   2.2f },
    { "AC Crunch",      "Crunch",0,  0.0f, -60.0f, 50.0f, 0.60f, 1, false, 2, 0.45f, 0.60f, 0.60f, 0.35f, 0.55f, 0.45f, 0.30f, 1.0f, 11000, 70,  0, 0, 0, 0, 0,  -5.1f },
    { "AC Lead",        "Lead",  0,  0.0f, -55.0f, 40.0f, 0.85f, 2, false, 2, 0.45f, 0.65f, 0.55f, 0.45f, 0.60f, 0.50f, 0.30f, 1.0f, 10000, 80,  0, 0, 0, 0, 0, -14.8f },
};

static constexpr int kNumFactoryPresets = static_cast<int> (sizeof (kFactoryPresets) / sizeof (kFactoryPresets[0]));
