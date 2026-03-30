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

// Factory presets
static const DuskAmpPreset kFactoryPresets[] =
{
    //                                          mode inG  gateThr gateRel gain ch  brt tType  B    M    T   pDrv pres reso sag  cMix cHiC  cLoC  dTm  dFb  dMix rMix rDec outLv
    { "Clean American",        "Clean",          0,  0.0f, -60.0f, 50.0f, 0.2f, 0, false, 0, 0.6f, 0.5f, 0.6f, 0.2f, 0.5f, 0.5f, 0.2f, 1.0f, 12000, 60, 0, 0, 0, 0, 0, 0.0f },
    { "Clean AC Chime",        "Clean",          0,  0.0f, -60.0f, 50.0f, 0.3f, 0, true,  2, 0.4f, 0.6f, 0.7f, 0.2f, 0.6f, 0.4f, 0.2f, 1.0f, 12000, 60, 0, 0, 0, 0, 0, 0.0f },
    { "Crunch British",        "Crunch",         0,  0.0f, -60.0f, 50.0f, 0.6f, 1, false, 1, 0.5f, 0.6f, 0.5f, 0.3f, 0.5f, 0.5f, 0.3f, 1.0f, 10000, 80, 0, 0, 0, 0, 0, 0.0f },
    { "Blues Breakup",         "Crunch",         0,  0.0f, -60.0f, 50.0f, 0.5f, 1, true,  0, 0.6f, 0.4f, 0.5f, 0.3f, 0.4f, 0.5f, 0.4f, 1.0f, 11000, 70, 0, 0, 0, 0, 0, 0.0f },
    { "Classic Rock",          "Crunch",         0,  0.0f, -55.0f, 40.0f, 0.7f, 1, false, 1, 0.6f, 0.7f, 0.5f, 0.4f, 0.5f, 0.5f, 0.3f, 1.0f, 10000, 80, 0, 0, 0, 0, 0, 0.0f },
    { "Lead British",          "High Gain",      0,  0.0f, -50.0f, 35.0f, 0.8f, 2, false, 1, 0.5f, 0.7f, 0.6f, 0.4f, 0.6f, 0.5f, 0.3f, 1.0f,  9000, 80, 0, 0, 0, 0, 0, 0.0f },
    { "Modern High Gain",      "High Gain",      0,  2.0f, -50.0f, 30.0f, 0.9f, 2, false, 1, 0.6f, 0.5f, 0.6f, 0.5f, 0.7f, 0.6f, 0.2f, 1.0f,  8000, 100, 0, 0, 0, 0, 0, 0.0f },
    { "Shred Lead",            "High Gain",      0,  3.0f, -45.0f, 25.0f, 1.0f, 2, true,  1, 0.5f, 0.6f, 0.7f, 0.5f, 0.7f, 0.5f, 0.2f, 1.0f,  8500, 90, 0, 0, 0, 0, 0, 1.0f },
    { "Ambient Clean",         "Ambient",        0,  0.0f, -60.0f, 50.0f, 0.2f, 0, false, 0, 0.5f, 0.5f, 0.6f, 0.2f, 0.5f, 0.5f, 0.2f, 1.0f, 12000, 60, 400, 0.3f, 0.3f, 0.25f, 0.5f, 0.0f },
    { "Ambient Crunch",        "Ambient",        0,  0.0f, -55.0f, 40.0f, 0.5f, 1, false, 1, 0.5f, 0.6f, 0.5f, 0.3f, 0.5f, 0.5f, 0.3f, 1.0f, 10000, 80, 350, 0.25f, 0.2f, 0.2f, 0.4f, 0.0f },
};

static constexpr int kNumFactoryPresets = static_cast<int> (sizeof (kFactoryPresets) / sizeof (kFactoryPresets[0]));
