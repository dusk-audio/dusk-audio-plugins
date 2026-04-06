#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ParamIDs.h"

struct DuskAmpPreset
{
    const char* name;
    const char* category;

    // Parameters (matching APVTS IDs)
    int    ampMode;           // 0=DSP, 1=NAM
    int    ampType;           // 0=Fender, 1=Marshall, 2=Vox
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
    // Boost
    bool   boostEnabled;
    float  boostGain;         // 0-1
    float  boostTone;         // 0-1
    float  boostLevel;        // 0-1
    // Cabinet
    float  cabMix;            // 0-1
    float  cabHiCut;          // Hz
    float  cabLoCut;          // Hz
    // Delay
    int    delayType;         // 0=Digital, 1=Analog, 2=Tape
    float  delayTime;         // ms
    float  delayFeedback;     // 0-0.95
    float  delayMix;          // 0-1
    // Reverb
    float  reverbMix;         // 0-1
    float  reverbDecay;       // 0-1
    float  reverbPreDelay;    // ms
    float  reverbDamping;     // 0-1
    float  reverbSize;        // 0-1
    // Output
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
        set (DuskAmpParams::BOOST_ENABLED,   boostEnabled ? 1.0f : 0.0f);
        set (DuskAmpParams::BOOST_GAIN,      boostGain);
        set (DuskAmpParams::BOOST_TONE,      boostTone);
        set (DuskAmpParams::BOOST_LEVEL,     boostLevel);
        set (DuskAmpParams::CAB_MIX,         cabMix);
        set (DuskAmpParams::CAB_HICUT,       cabHiCut);
        set (DuskAmpParams::CAB_LOCUT,       cabLoCut);
        set (DuskAmpParams::DELAY_TYPE,      static_cast<float> (delayType));
        set (DuskAmpParams::DELAY_TIME,      delayTime);
        set (DuskAmpParams::DELAY_FEEDBACK,  delayFeedback);
        set (DuskAmpParams::DELAY_MIX,       delayMix);
        set (DuskAmpParams::REVERB_MIX,      reverbMix);
        set (DuskAmpParams::REVERB_DECAY,    reverbDecay);
        set (DuskAmpParams::REVERB_PREDELAY, reverbPreDelay);
        set (DuskAmpParams::REVERB_DAMPING,  reverbDamping);
        set (DuskAmpParams::REVERB_SIZE,     reverbSize);
        set (DuskAmpParams::OUTPUT_LEVEL,    outputLevel);
    }
};

// ============================================================================
// Factory presets — organized by amp type
// ============================================================================
//
// Columns:
//   name, category,
//   mode, type, inG, gateThr, gateRel, gain, brt,
//   B, M, T, pDrv, pres, reso, sag,
//   bstOn, bstG, bstT, bstL,
//   cMix, cHiC, cLoC,
//   dType, dTm, dFb, dMix,
//   rMix, rDec, rPD, rDmp, rSz,
//   outLv

static const DuskAmpPreset kFactoryPresets[] =
{
    // === FENDER ===
    { "Fender Clean",           "Fender",   0, 0, 0, -60, 50, 0.2f, false,  0.6f, 0.5f, 0.6f, 0.2f, 0.5f, 0.5f, 0.2f,  false, 0.5f, 0.5f, 0.5f,  1, 12000, 60,  0, 0, 0, 0,  0, 0, 0, 0.5f, 0.5f,  0 },
    { "Fender Sparkle",         "Fender",   0, 0, 0, -60, 50, 0.25f, true,  0.5f, 0.5f, 0.7f, 0.2f, 0.6f, 0.4f, 0.3f,  false, 0.5f, 0.5f, 0.5f,  1, 12000, 60,  0, 0, 0, 0,  0, 0, 0, 0.5f, 0.5f,  0 },
    { "Fender Edge",            "Fender",   0, 0, 0, -55, 40, 0.55f, false, 0.6f, 0.4f, 0.5f, 0.3f, 0.5f, 0.5f, 0.4f,  false, 0.5f, 0.5f, 0.5f,  1, 11000, 70,  0, 0, 0, 0,  0, 0, 0, 0.5f, 0.5f,  0 },
    { "Fender Blues",           "Fender",   0, 0, 0, -55, 40, 0.65f, true,  0.6f, 0.4f, 0.5f, 0.35f, 0.4f, 0.5f, 0.4f, true, 0.3f, 0.6f, 0.5f,   1, 11000, 70,  0, 0, 0, 0,  0, 0, 0, 0.5f, 0.5f,  0 },
    { "Fender Country",         "Fender",   0, 0, 0, -60, 50, 0.3f, true,  0.5f, 0.6f, 0.7f, 0.2f, 0.6f, 0.4f, 0.2f,  false, 0.5f, 0.5f, 0.5f,  1, 12000, 60,  0, 0, 0, 0,  0, 0, 0, 0.5f, 0.5f,  0 },
    { "Fender Ambient",         "Fender",   0, 0, 0, -60, 50, 0.2f, false, 0.5f, 0.5f, 0.6f, 0.2f, 0.5f, 0.5f, 0.2f,  false, 0.5f, 0.5f, 0.5f,  1, 12000, 60,  1, 400, 0.3f, 0.25f,  0.2f, 0.6f, 30, 0.5f, 0.6f,  0 },

    // === VOX ===
    { "Vox Chime",              "Vox",      0, 2, 0, -60, 50, 0.3f, false,  0.4f, 0.6f, 0.7f, 0.2f, 0.6f, 0.4f, 0.2f,  false, 0.5f, 0.5f, 0.5f,  1, 12000, 60,  0, 0, 0, 0,  0, 0, 0, 0.5f, 0.5f,  0 },
    { "Vox Jangle",             "Vox",      0, 2, 0, -60, 50, 0.4f, false,  0.4f, 0.7f, 0.7f, 0.25f, 0.5f, 0.4f, 0.2f, false, 0.5f, 0.5f, 0.5f,  1, 11000, 60,  0, 0, 0, 0,  0, 0, 0, 0.5f, 0.5f,  0 },
    { "Vox Crunch",             "Vox",      0, 2, 0, -55, 40, 0.6f, false,  0.5f, 0.7f, 0.6f, 0.3f, 0.5f, 0.5f, 0.3f,  false, 0.5f, 0.5f, 0.5f,  1, 10000, 80,  0, 0, 0, 0,  0, 0, 0, 0.5f, 0.5f,  0 },
    { "Vox Queens",             "Vox",      0, 2, 0, -55, 40, 0.5f, false,  0.5f, 0.6f, 0.6f, 0.3f, 0.5f, 0.5f, 0.3f,  true, 0.4f, 0.6f, 0.5f,   1, 10000, 70,  0, 0, 0, 0,  0, 0, 0, 0.5f, 0.5f,  0 },
    { "Vox Top Boost Lead",     "Vox",      0, 2, 0, -50, 35, 0.75f, false, 0.5f, 0.7f, 0.6f, 0.4f, 0.6f, 0.5f, 0.3f,  true, 0.5f, 0.5f, 0.5f,  1, 9500, 80,   0, 0, 0, 0,  0, 0, 0, 0.5f, 0.5f,  0 },
    { "Vox Shimmer",            "Vox",      0, 2, 0, -60, 50, 0.25f, false, 0.4f, 0.6f, 0.7f, 0.2f, 0.5f, 0.4f, 0.2f,  false, 0.5f, 0.5f, 0.5f,  1, 12000, 60,  1, 350, 0.25f, 0.2f,  0.25f, 0.7f, 40, 0.4f, 0.7f,  0 },

    // === MARSHALL ===
    { "Marshall Clean",         "Marshall", 0, 1, 0, -60, 50, 0.2f, false,  0.5f, 0.5f, 0.5f, 0.2f, 0.5f, 0.5f, 0.2f,  false, 0.5f, 0.5f, 0.5f,  1, 12000, 60,  0, 0, 0, 0,  0, 0, 0, 0.5f, 0.5f,  0 },
    { "Marshall Crunch",        "Marshall", 0, 1, 0, -60, 50, 0.5f, false,  0.5f, 0.6f, 0.5f, 0.3f, 0.5f, 0.5f, 0.3f,  false, 0.5f, 0.5f, 0.5f,  1, 10000, 80,  0, 0, 0, 0,  0, 0, 0, 0.5f, 0.5f,  0 },
    { "Marshall Classic Rock",  "Marshall", 0, 1, 0, -55, 40, 0.7f, true,   0.6f, 0.7f, 0.5f, 0.4f, 0.5f, 0.5f, 0.3f,  false, 0.5f, 0.5f, 0.5f,  1, 10000, 80,  0, 0, 0, 0,  0, 0, 0, 0.5f, 0.5f,  0 },
    { "Marshall Plexi Lead",    "Marshall", 0, 1, 0, -50, 35, 0.8f, false,  0.5f, 0.7f, 0.6f, 0.4f, 0.6f, 0.5f, 0.3f,  false, 0.5f, 0.5f, 0.5f,  1, 9000, 80,   0, 0, 0, 0,  0, 0, 0, 0.5f, 0.5f,  0 },
    { "Marshall Hot Rod",       "Marshall", 0, 1, 2, -50, 30, 0.85f, true,  0.5f, 0.6f, 0.6f, 0.5f, 0.6f, 0.5f, 0.3f,  true, 0.5f, 0.5f, 0.5f,  1, 9000, 90,   0, 0, 0, 0,  0, 0, 0, 0.5f, 0.5f,  0 },
    { "Marshall High Gain",     "Marshall", 0, 1, 2, -50, 30, 0.9f, false,  0.6f, 0.5f, 0.6f, 0.5f, 0.7f, 0.6f, 0.2f,  true, 0.6f, 0.5f, 0.5f,  1, 8000, 100,  0, 0, 0, 0,  0, 0, 0, 0.5f, 0.5f,  0 },
    { "Marshall Shred",         "Marshall", 0, 1, 3, -45, 25, 1.0f, true,   0.5f, 0.6f, 0.7f, 0.5f, 0.7f, 0.5f, 0.2f,  true, 0.7f, 0.5f, 0.5f,  1, 8500, 90,   0, 0, 0, 0,  0, 0, 0, 0.5f, 0.5f,  1 },
    { "Marshall Sludge",        "Marshall", 0, 1, 0, -50, 30, 0.9f, false,  0.7f, 0.4f, 0.4f, 0.5f, 0.4f, 0.7f, 0.4f,  true, 0.5f, 0.3f, 0.5f,  1, 7000, 100,  0, 0, 0, 0,  0, 0, 0, 0.5f, 0.5f,  0 },

    // === GENRE / EFFECTS ===
    { "Blues Harp",             "Genre",    0, 0, 0, -55, 40, 0.5f, true,   0.6f, 0.5f, 0.4f, 0.3f, 0.4f, 0.5f, 0.4f,  true, 0.3f, 0.6f, 0.5f,  1, 10000, 80,  2, 300, 0.2f, 0.15f,  0.15f, 0.4f, 20, 0.5f, 0.5f,  0 },
    { "Indie Clean",           "Genre",    0, 2, 0, -60, 50, 0.35f, false, 0.4f, 0.6f, 0.6f, 0.2f, 0.5f, 0.4f, 0.2f,  false, 0.5f, 0.5f, 0.5f,  1, 12000, 60,  1, 450, 0.3f, 0.25f,  0.2f, 0.5f, 25, 0.4f, 0.5f,  0 },
    { "Ambient Pad",           "Genre",    0, 0, 0, -60, 50, 0.15f, false, 0.5f, 0.5f, 0.5f, 0.15f, 0.5f, 0.5f, 0.2f, false, 0.5f, 0.5f, 0.5f,  1, 12000, 60,  1, 500, 0.4f, 0.35f,  0.35f, 0.8f, 50, 0.5f, 0.8f,  0 },
    { "Post-Rock Swell",       "Genre",    0, 1, 0, -55, 40, 0.6f, false,  0.5f, 0.6f, 0.5f, 0.3f, 0.5f, 0.5f, 0.3f,  true, 0.4f, 0.5f, 0.4f,  1, 10000, 80,  1, 600, 0.35f, 0.3f,  0.3f, 0.7f, 40, 0.4f, 0.7f,  0 },
    { "80s Arena Rock",        "Genre",    0, 1, 0, -55, 40, 0.7f, true,   0.5f, 0.7f, 0.6f, 0.4f, 0.6f, 0.5f, 0.3f,  false, 0.5f, 0.5f, 0.5f,  1, 10000, 80,  0, 400, 0.3f, 0.2f,  0.15f, 0.5f, 30, 0.5f, 0.5f,  0 },
    { "Tape Echo Lead",        "Genre",    0, 1, 0, -50, 35, 0.8f, false,  0.5f, 0.7f, 0.6f, 0.4f, 0.6f, 0.5f, 0.3f,  false, 0.5f, 0.5f, 0.5f,  1, 9000, 80,   2, 350, 0.35f, 0.25f,  0.1f, 0.3f, 15, 0.5f, 0.4f,  0 },
    { "Surf Rock",             "Genre",    0, 0, 0, -60, 50, 0.3f, true,   0.5f, 0.5f, 0.7f, 0.2f, 0.6f, 0.5f, 0.2f,  false, 0.5f, 0.5f, 0.5f,  1, 12000, 60,  0, 0, 0, 0,  0.3f, 0.6f, 40, 0.3f, 0.6f,  0 },
    { "Metal Rhythm",          "Genre",    0, 1, 2, -50, 30, 0.85f, false, 0.6f, 0.5f, 0.6f, 0.5f, 0.7f, 0.6f, 0.2f,  true, 0.6f, 0.5f, 0.5f,  1, 8000, 100,  0, 0, 0, 0,  0, 0, 0, 0.5f, 0.5f,  0 },
    { "Doom",                  "Genre",    0, 1, 0, -50, 30, 0.9f, false,  0.8f, 0.3f, 0.3f, 0.5f, 0.3f, 0.8f, 0.5f,  true, 0.5f, 0.3f, 0.6f,  1, 6000, 100,  0, 0, 0, 0,  0.1f, 0.4f, 20, 0.7f, 0.4f,  0 },
    { "NAM Default",           "NAM",      1, 1, 0, -60, 50, 0.5f, false,  0.5f, 0.5f, 0.5f, 0.3f, 0.5f, 0.5f, 0.3f,  false, 0.5f, 0.5f, 0.5f,  1, 10000, 60,  0, 0, 0, 0,  0, 0, 0, 0.5f, 0.5f,  0 },
};

static constexpr int kNumFactoryPresets = static_cast<int> (sizeof (kFactoryPresets) / sizeof (kFactoryPresets[0]));
