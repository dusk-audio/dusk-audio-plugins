#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ParamIDs.h"

struct DuskAmpPreset
{
    const char* name;
    const char* category;

    // Parameters (matching APVTS IDs)
    int    ampMode;           // 0=DSP, 1=NAM
    int    ampModel;          // 0=Round, 1=Chime, 2=Punch
    float  inputGain;         // dB
    float  gateThreshold;     // dB
    float  gateRelease;       // ms
    float  drive;             // 0-1
    float  bass, mid, treble; // 0-1
    float  presence;          // 0-1
    float  resonance;         // 0-1
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
        set (DuskAmpParams::AMP_MODEL,       static_cast<float> (ampModel));
        set (DuskAmpParams::INPUT_GAIN,      inputGain);
        set (DuskAmpParams::GATE_THRESHOLD,  gateThreshold);
        set (DuskAmpParams::GATE_RELEASE,    gateRelease);
        set (DuskAmpParams::DRIVE,           drive);
        set (DuskAmpParams::BASS,            bass);
        set (DuskAmpParams::MID,             mid);
        set (DuskAmpParams::TREBLE,          treble);
        set (DuskAmpParams::PRESENCE,        presence);
        set (DuskAmpParams::RESONANCE,       resonance);
        set (DuskAmpParams::CAB_MIX,         cabMix);
        set (DuskAmpParams::CAB_HICUT,       cabHiCut);
        set (DuskAmpParams::CAB_LOCUT,       cabLoCut);
        set (DuskAmpParams::DELAY_TIME,      delayTime);
        set (DuskAmpParams::DELAY_FEEDBACK,  delayFeedback);
        set (DuskAmpParams::DELAY_MIX,       delayMix);
        set (DuskAmpParams::REVERB_MIX,      reverbMix);
        set (DuskAmpParams::REVERB_DECAY,    reverbDecay);
        set (DuskAmpParams::NAM_INPUT_LEVEL,  0.0f);  // factory presets are DSP mode
        set (DuskAmpParams::NAM_OUTPUT_LEVEL, 0.0f);
        set (DuskAmpParams::OUTPUT_LEVEL,    outputLevel);
    }
};

// Factory presets
//
// Amp models: 0=Blackface (Deluxe Reverb), 1=British Combo (AC30), 2=Plexi (Marshall 1959)
//
// Each model has its own tube topology, tone stack, power section, and sag
// baked in — the Drive knob distributes gain across preamp + power amp
// stages according to the model's architecture.
static const DuskAmpPreset kFactoryPresets[] =
{
    //                                          mode model inG   gateThr gateRel drive  B     M     T    pres reso cMix cHiC  cLoC  dTm  dFb  dMix rMix rDec outLv

    // -- Round (Deluxe Reverb character) --
    { "Round Clean",           "Clean",          0,  0,  0.0f, -60.0f, 50.0f, 0.20f, 0.40f, 0.35f, 0.60f, 0.45f, 0.50f, 1.0f,  9000, 80, 0, 0, 0, 0, 0,  0.0f },
    { "Round Sparkle",         "Clean",          0,  0,  0.0f, -60.0f, 50.0f, 0.45f, 0.35f, 0.40f, 0.65f, 0.50f, 0.50f, 1.0f,  9000, 80, 0, 0, 0, 0, 0,  0.0f },
    { "Round Breakup",         "Crunch",         0,  0,  0.0f, -60.0f, 50.0f, 0.65f, 0.40f, 0.40f, 0.60f, 0.45f, 0.50f, 1.0f,  9000, 80, 0, 0, 0, 0, 0, -1.0f },

    // -- Chime (AC30 Top Boost character) --
    { "Chime Clean",           "Clean",          0,  1,  0.0f, -60.0f, 50.0f, 0.25f, 0.43f, 0.55f, 0.58f, 0.60f, 0.40f, 1.0f, 10000, 90, 0, 0, 0, 0, 0,  0.0f },
    { "Chime Jangle",          "Crunch",         0,  1,  0.0f, -60.0f, 50.0f, 0.50f, 0.43f, 0.58f, 0.60f, 0.65f, 0.40f, 1.0f, 10000, 90, 0, 0, 0, 0, 0, -1.0f },
    { "Chime Overdrive",       "Crunch",         0,  1,  0.0f, -55.0f, 40.0f, 0.75f, 0.45f, 0.60f, 0.55f, 0.65f, 0.45f, 1.0f, 10000, 90, 0, 0, 0, 0, 0, -2.0f },

    // -- Punch (Plexi 1959 character) --
    { "Punch Crunch",          "Crunch",         0,  2,  0.0f, -55.0f, 40.0f, 0.50f, 0.42f, 0.65f, 0.60f, 0.65f, 0.55f, 1.0f,  8500, 70, 0, 0, 0, 0, 0, -1.0f },
    { "Punch Classic Rock",    "Crunch",         0,  2,  0.0f, -55.0f, 40.0f, 0.70f, 0.42f, 0.72f, 0.65f, 0.70f, 0.55f, 1.0f,  8500, 70, 0, 0, 0, 0, 0, -2.0f },
    { "Punch High Gain",       "High Gain",      0,  2,  2.0f, -50.0f, 35.0f, 0.90f, 0.45f, 0.70f, 0.65f, 0.75f, 0.60f, 1.0f,  8500, 70, 0, 0, 0, 0, 0, -3.0f },

    // -- Ambient --
    { "Ambient Round",         "Ambient",        0,  0,  0.0f, -60.0f, 50.0f, 0.25f, 0.45f, 0.45f, 0.55f, 0.45f, 0.50f, 1.0f,  9000, 80, 400, 0.3f, 0.3f, 0.25f, 0.5f, 0.0f },
    { "Ambient Chime",         "Ambient",        0,  1,  0.0f, -60.0f, 50.0f, 0.35f, 0.43f, 0.55f, 0.60f, 0.60f, 0.40f, 1.0f, 10000, 90, 350, 0.25f, 0.25f, 0.20f, 0.4f, 0.0f },
};

static constexpr int kNumFactoryPresets = static_cast<int> (sizeof (kFactoryPresets) / sizeof (kFactoryPresets[0]));
