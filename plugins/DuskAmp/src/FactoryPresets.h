#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ParamIDs.h"

struct DuskAmpPreset
{
    const char* name;
    const char* category;

    // Parameters (matching APVTS IDs)
    int    ampMode;           // 0=DSP, 1=NAM
    int    ampType;           // 0-7 (AmpType enum)
    float  inputGain;         // dB
    float  gateThreshold;     // dB
    float  gateRelease;       // ms
    float  preampGain;        // 0-1
    bool   bright;
    float  bass, mid, treble; // 0-1
    float  powerDrive;        // 0-1
    float  presence;          // 0-1
    float  resonance;         // 0-1
    float  sag;               // 0-1
    float  cabMix;            // 0-1
    float  cabHiCut;          // Hz
    float  cabLoCut;          // Hz
    float  cabMicPos;         // 0-1
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
        set (DuskAmpParams::AMP_TYPE,        static_cast<float> (ampType));
        set (DuskAmpParams::INPUT_GAIN,      inputGain);
        set (DuskAmpParams::GATE_THRESHOLD,  gateThreshold);
        set (DuskAmpParams::GATE_RELEASE,    gateRelease);
        set (DuskAmpParams::PREAMP_GAIN,     preampGain);
        set (DuskAmpParams::PREAMP_BRIGHT,   bright ? 1.0f : 0.0f);
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
        set (DuskAmpParams::CAB_MIC_POS,     cabMicPos);
        set (DuskAmpParams::DELAY_TIME,      delayTime);
        set (DuskAmpParams::DELAY_FEEDBACK,  delayFeedback);
        set (DuskAmpParams::DELAY_MIX,       delayMix);
        set (DuskAmpParams::REVERB_MIX,      reverbMix);
        set (DuskAmpParams::REVERB_DECAY,    reverbDecay);
        set (DuskAmpParams::OUTPUT_LEVEL,    outputLevel);
    }
};

// AmpType indices: 0=FenderDeluxe, 1=VoxAC30, 2=MarshallPlexi

static const DuskAmpPreset kFactoryPresets[] =
{
    // --- Fender Deluxe Reverb 65 ---
    //                                          mode type inG  gateThr gateRel gain brt  B    M    T   pDrv pres reso sag  cMix cHiC  cLoC  mic  dTm  dFb  dMix rMix rDec outLv
    { "Pristine Clean",        "Clean",          0,  0,  0.0f, -60.0f, 50.0f, 0.2f, false, 0.6f, 0.5f, 0.6f, 0.2f, 0.5f, 0.5f, 0.2f, 1.0f, 12000, 60, 0.6f, 0, 0, 0, 0, 0, 0.0f },
    { "Sparkle Clean",         "Clean",          0,  0,  0.0f, -60.0f, 50.0f, 0.4f, false, 0.5f, 0.5f, 0.7f, 0.2f, 0.6f, 0.4f, 0.3f, 1.0f, 12000, 60, 0.7f, 0, 0, 0, 0, 0, 0.0f },
    { "Edge of Breakup",       "Crunch",         0,  0,  0.0f, -60.0f, 50.0f, 0.7f, false, 0.5f, 0.5f, 0.5f, 0.3f, 0.5f, 0.4f, 0.3f, 1.0f, 11000, 70, 0.5f, 0, 0, 0, 0, 0, 0.0f },

    // --- Vox AC30 Top Boost ---
    { "Chimey Clean",          "Clean",          0,  1,  0.0f, -60.0f, 50.0f, 0.3f, false, 0.4f, 0.6f, 0.7f, 0.2f, 0.6f, 0.4f, 0.2f, 1.0f, 12000, 60, 0.6f, 0, 0, 0, 0, 0, 0.0f },
    { "Jangle Pop",            "Crunch",         0,  1,  0.0f, -55.0f, 45.0f, 0.5f, false, 0.5f, 0.5f, 0.6f, 0.3f, 0.5f, 0.5f, 0.3f, 1.0f, 11000, 70, 0.5f, 0, 0, 0, 0, 0, 0.0f },
    { "Queen Crunch",          "Crunch",         0,  1,  0.0f, -55.0f, 40.0f, 0.7f, false, 0.5f, 0.6f, 0.6f, 0.4f, 0.5f, 0.5f, 0.4f, 1.0f, 10000, 80, 0.5f, 0, 0, 0, 0, 0, 0.0f },

    // --- Marshall 1959 Plexi ---
    { "Blues Breakup",         "Crunch",         0,  2,  0.0f, -60.0f, 50.0f, 0.4f, true,  0.5f, 0.5f, 0.5f, 0.3f, 0.4f, 0.5f, 0.3f, 1.0f, 11000, 70, 0.5f, 0, 0, 0, 0, 0, 0.0f },
    { "Classic Rock",          "Crunch",         0,  2,  0.0f, -55.0f, 40.0f, 0.6f, true,  0.6f, 0.6f, 0.5f, 0.4f, 0.5f, 0.5f, 0.3f, 1.0f, 10000, 80, 0.6f, 0, 0, 0, 0, 0, 0.0f },
    { "Cranked Plexi",         "High Gain",      0,  2,  0.0f, -50.0f, 35.0f, 0.85f, false, 0.5f, 0.7f, 0.5f, 0.5f, 0.6f, 0.5f, 0.4f, 1.0f,  9000, 90, 0.5f, 0, 0, 0, 0, 0, 0.0f },

    // --- Ambient presets ---
    { "Ambient Clean",         "Ambient",        0,  0,  0.0f, -60.0f, 50.0f, 0.2f, false, 0.5f, 0.5f, 0.6f, 0.2f, 0.5f, 0.5f, 0.2f, 1.0f, 12000, 60, 0.6f, 400, 0.3f, 0.3f, 0.25f, 0.5f, 0.0f },
    { "Ambient Crunch",        "Ambient",        0,  2,  0.0f, -55.0f, 40.0f, 0.5f, false, 0.5f, 0.6f, 0.5f, 0.3f, 0.5f, 0.5f, 0.3f, 1.0f, 10000, 80, 0.5f, 350, 0.25f, 0.2f, 0.2f, 0.4f, 0.0f },
};

static constexpr int kNumFactoryPresets = static_cast<int> (sizeof (kFactoryPresets) / sizeof (kFactoryPresets[0]));
