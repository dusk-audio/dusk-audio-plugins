#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ParamIDs.h"

struct DuskAmpPreset
{
    const char* name;
    const char* category;

    // Parameters (matching APVTS IDs)
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

    void applyTo (juce::AudioProcessorValueTreeState& apvts) const
    {
        auto set = [&] (const juce::String& id, float val)
        {
            if (auto* param = apvts.getParameter (id))
                param->setValueNotifyingHost (param->convertTo0to1 (val));
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
        set (DuskAmpParams::CAB_MIX,         cabMix);
        set (DuskAmpParams::CAB_HICUT,       cabHiCut);
        set (DuskAmpParams::CAB_LOCUT,       cabLoCut);
        set (DuskAmpParams::DELAY_TIME,      delayTime);
        set (DuskAmpParams::DELAY_FEEDBACK,  delayFeedback);
        set (DuskAmpParams::DELAY_MIX,       delayMix);
        set (DuskAmpParams::REVERB_MIX,      reverbMix);
        set (DuskAmpParams::REVERB_DECAY,    reverbDecay);
        set (DuskAmpParams::OUTPUT_LEVEL,    outputLevel);
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
    // OUTPUT_LEVEL trims set so Clean/Crunch/Lead sit within ~3 dB of each
    // other. Without trims, the matrix spans 25 dB (Lead at -0.7 dBFS,
    // basically peaking). Trims balance the increased saturation/level
    // boost from the higher gain settings.
    //                          mode inG  gateThr gateRel gain ch  brt tType  B    M    T    pDrv pres reso sag  cMix cHiC  cLoC dTm dFb dMix rMix rDec outLv
    { "American Clean", "Clean", 0,  0.0f, -60.0f, 50.0f, 0.40f, 0, false, 0, 0.50f, 0.50f, 0.55f, 0.20f, 0.50f, 0.50f, 0.30f, 1.0f, 12000, 60,  0, 0, 0, 0, 0,   0.0f },
    { "American Crunch","Crunch",0,  0.0f, -60.0f, 50.0f, 0.60f, 1, false, 0, 0.50f, 0.55f, 0.55f, 0.30f, 0.50f, 0.50f, 0.30f, 1.0f, 12000, 60,  0, 0, 0, 0, 0,  -3.0f },
    { "American Lead",  "Lead",  0,  0.0f, -55.0f, 40.0f, 0.80f, 2, false, 0, 0.50f, 0.60f, 0.50f, 0.40f, 0.50f, 0.50f, 0.30f, 1.0f, 11000, 70,  0, 0, 0, 0, 0, -10.0f },

    { "British Clean",  "Clean", 0,  0.0f, -60.0f, 50.0f, 0.30f, 0, false, 1, 0.50f, 0.55f, 0.55f, 0.20f, 0.50f, 0.50f, 0.30f, 1.0f, 12000, 60,  0, 0, 0, 0, 0,   3.0f },
    { "British Crunch", "Crunch",0,  0.0f, -60.0f, 50.0f, 0.60f, 1, false, 1, 0.50f, 0.60f, 0.55f, 0.40f, 0.50f, 0.50f, 0.30f, 1.0f, 11000, 70,  0, 0, 0, 0, 0,  -5.0f },
    { "British Lead",   "Lead",  0,  0.0f, -55.0f, 40.0f, 0.85f, 2, false, 1, 0.50f, 0.65f, 0.55f, 0.50f, 0.55f, 0.50f, 0.30f, 1.0f,  9500, 80,  0, 0, 0, 0, 0, -12.0f },

    { "AC Clean",       "Clean", 0,  0.0f, -60.0f, 50.0f, 0.35f, 0, true,  2, 0.45f, 0.55f, 0.60f, 0.20f, 0.55f, 0.45f, 0.30f, 1.0f, 12000, 60,  0, 0, 0, 0, 0,   0.0f },
    { "AC Crunch",      "Crunch",0,  0.0f, -60.0f, 50.0f, 0.60f, 1, false, 2, 0.45f, 0.60f, 0.60f, 0.35f, 0.55f, 0.45f, 0.30f, 1.0f, 11000, 70,  0, 0, 0, 0, 0,  -3.0f },
    { "AC Lead",        "Lead",  0,  0.0f, -55.0f, 40.0f, 0.85f, 2, false, 2, 0.45f, 0.65f, 0.55f, 0.45f, 0.60f, 0.50f, 0.30f, 1.0f, 10000, 80,  0, 0, 0, 0, 0, -10.0f },
};

static constexpr int kNumFactoryPresets = static_cast<int> (sizeof (kFactoryPresets) / sizeof (kFactoryPresets[0]));
