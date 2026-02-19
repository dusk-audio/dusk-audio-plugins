#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

// Minimal factory preset data. Each preset stores parameter values in a flat struct.
struct FactoryPreset
{
    const char* name;
    const char* category;

    int   algorithm;       // 0-4
    float decay;           // 0.2-30
    float predelay;        // 0-250 ms
    float size;            // 0-1
    float damping;         // 0.1-1 (treble multiply)
    float bassMult;        // 0.5-2
    float crossover;       // 200-4000 Hz
    float diffusion;       // 0-1
    float modDepth;        // 0-1
    float modRate;         // 0.1-3 Hz
    float erLevel;         // 0-1
    float erSize;          // 0-1
    float mix;             // 0-1
    float loCut;           // 20-500 Hz
    float hiCut;           // 1000-20000 Hz
    float width;           // 0-2

    void applyTo (juce::AudioProcessorValueTreeState& apvts) const
    {
        if (auto* p = apvts.getParameter ("algorithm"))   p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (algorithm)));
        if (auto* p = apvts.getParameter ("decay"))        p->setValueNotifyingHost (p->convertTo0to1 (decay));
        if (auto* p = apvts.getParameter ("predelay"))     p->setValueNotifyingHost (p->convertTo0to1 (predelay));
        if (auto* p = apvts.getParameter ("size"))         p->setValueNotifyingHost (p->convertTo0to1 (size));
        if (auto* p = apvts.getParameter ("damping"))      p->setValueNotifyingHost (p->convertTo0to1 (damping));
        if (auto* p = apvts.getParameter ("bass_mult"))    p->setValueNotifyingHost (p->convertTo0to1 (bassMult));
        if (auto* p = apvts.getParameter ("crossover"))    p->setValueNotifyingHost (p->convertTo0to1 (crossover));
        if (auto* p = apvts.getParameter ("diffusion"))    p->setValueNotifyingHost (p->convertTo0to1 (diffusion));
        if (auto* p = apvts.getParameter ("mod_depth"))    p->setValueNotifyingHost (p->convertTo0to1 (modDepth));
        if (auto* p = apvts.getParameter ("mod_rate"))     p->setValueNotifyingHost (p->convertTo0to1 (modRate));
        if (auto* p = apvts.getParameter ("er_level"))     p->setValueNotifyingHost (p->convertTo0to1 (erLevel));
        if (auto* p = apvts.getParameter ("er_size"))      p->setValueNotifyingHost (p->convertTo0to1 (erSize));
        if (auto* p = apvts.getParameter ("mix"))          p->setValueNotifyingHost (p->convertTo0to1 (mix));
        if (auto* p = apvts.getParameter ("lo_cut"))       p->setValueNotifyingHost (p->convertTo0to1 (loCut));
        if (auto* p = apvts.getParameter ("hi_cut"))       p->setValueNotifyingHost (p->convertTo0to1 (hiCut));
        if (auto* p = apvts.getParameter ("width"))        p->setValueNotifyingHost (p->convertTo0to1 (width));
        if (auto* p = apvts.getParameter ("freeze"))        p->setValueNotifyingHost (0.0f);
        if (auto* p = apvts.getParameter ("predelay_sync")) p->setValueNotifyingHost (0.0f);
        if (auto* p = apvts.getParameter ("bus_mode"))      p->setValueNotifyingHost (0.0f);
    }
};

// clang-format off
//
// Effective value = raw param * algorithm scale factor
// Algorithm trebleMultScale: Plate=1.0, Hall=1.0, Chamber=1.15, Room=0.85, Ambient=1.1
// Algorithm bassMultScale:   Plate=1.0, Hall=1.0, Chamber=1.0,  Room=0.9,  Ambient=1.2
// Algorithm erLevelScale:    Plate=0.0, Hall=1.0, Chamber=0.8,  Room=1.5,  Ambient=0.0
// Algorithm lateGainScale:   Plate=1.0, Hall=1.0, Chamber=1.0,  Room=0.7,  Ambient=1.0
//
// Treble multiply reference (effective values):
//   Bright: 0.70-0.90  |  Neutral: 0.55-0.70  |  Warm: 0.45-0.55  |  Dark: 0.30-0.45
//
inline const std::vector<FactoryPreset>& getFactoryPresets(){
    static const std::vector<FactoryPreset> presets = {
        // -- Vocals --                          algo  decay  pre    size  damp  bass   xover   diff  modD  modR  erLv  erSz  mix   loCut  hiCut   width
        { "Vocal Plate",      "Vocals",   0, 1.4f,  18.0f, 0.65f, 0.72f, 1.0f,  1200.0f, 0.80f, 0.25f, 0.60f, 0.0f, 0.0f, 0.25f,  80.0f, 12000.0f, 1.0f  },  // eff treble 0.72
        { "Vocal Hall",       "Vocals",   1, 2.2f,  30.0f, 0.70f, 0.58f, 1.15f,  900.0f, 0.70f, 0.40f, 0.80f, 0.55f, 0.50f, 0.30f,  60.0f, 14000.0f, 1.0f  },  // eff treble 0.58
        { "Vocal Room",       "Vocals",   3, 0.8f,  10.0f, 0.40f, 0.72f, 1.0f,  1000.0f, 0.50f, 0.15f, 0.50f, 0.60f, 0.40f, 0.20f,  80.0f, 11000.0f, 0.85f },  // eff treble 0.61

        // -- Drums --
        { "Drum Room",        "Drums",    3, 0.4f,   5.0f, 0.45f, 0.65f, 1.0f,   800.0f, 0.50f, 0.10f, 0.40f, 0.55f, 0.45f, 0.20f,  60.0f, 10000.0f, 0.90f },  // eff treble 0.55
        { "Drum Plate",       "Drums",    0, 0.9f,   8.0f, 0.55f, 0.75f, 0.90f, 1400.0f, 0.85f, 0.20f, 0.50f, 0.0f, 0.0f, 0.25f, 100.0f, 10000.0f, 1.0f  },  // eff treble 0.75
        { "Drum Ambient",     "Drums",    4, 4.5f,  25.0f, 0.80f, 0.50f, 1.15f,  800.0f, 0.80f, 0.55f, 1.10f, 0.0f, 0.0f, 0.35f,  80.0f,  9000.0f, 1.50f },  // eff treble 0.55

        // -- Guitar --
        { "Guitar Spring",    "Guitar",   0, 1.8f,  20.0f, 0.50f, 0.45f, 1.0f,  1000.0f, 0.45f, 0.70f, 1.80f, 0.0f, 0.0f, 0.30f,  60.0f,  8000.0f, 1.0f  },  // eff treble 0.45
        { "Guitar Hall",      "Guitar",   1, 2.8f,  35.0f, 0.75f, 0.58f, 1.10f,  850.0f, 0.75f, 0.45f, 0.70f, 0.50f, 0.55f, 0.30f,  50.0f, 12000.0f, 1.20f },  // eff treble 0.58

        // -- Keys / Synth --
        { "Keys Chamber",     "Keys",     2, 1.8f,  20.0f, 0.60f, 0.55f, 1.10f, 1000.0f, 0.70f, 0.35f, 0.65f, 0.60f, 0.50f, 0.25f,  40.0f, 15000.0f, 1.10f },  // eff treble 0.63
        { "Synth Pad",        "Keys",     4, 6.0f,  40.0f, 0.85f, 0.48f, 1.20f,  700.0f, 0.90f, 0.60f, 0.85f, 0.0f, 0.0f, 0.50f,  30.0f, 16000.0f, 1.60f },  // eff treble 0.53

        // -- Mix Bus --
        { "Mix Glue",         "Mix",      3, 0.5f,   8.0f, 0.30f, 0.80f, 1.05f, 1000.0f, 0.40f, 0.10f, 0.30f, 0.40f, 0.30f, 0.08f, 120.0f, 12000.0f, 1.0f  },  // eff treble 0.68
        { "Mix Space",        "Mix",      1, 1.2f,  15.0f, 0.50f, 0.62f, 1.0f,  1000.0f, 0.55f, 0.25f, 0.50f, 0.45f, 0.45f, 0.10f, 100.0f, 12000.0f, 1.0f  },  // eff treble 0.62

        // -- Rooms --
        { "Small Room",       "Rooms",    3, 0.6f,   3.0f, 0.30f, 0.82f, 1.05f, 1200.0f, 0.40f, 0.10f, 0.35f, 0.65f, 0.30f, 0.25f,  80.0f, 12000.0f, 0.75f },  // eff treble 0.70
        { "Medium Room",      "Rooms",    3, 1.2f,  12.0f, 0.50f, 0.70f, 1.0f,  1000.0f, 0.55f, 0.15f, 0.45f, 0.55f, 0.50f, 0.30f,  60.0f, 12000.0f, 1.0f  },  // eff treble 0.60
        { "Large Hall",       "Rooms",    1, 4.0f,  45.0f, 0.80f, 0.52f, 1.20f,  750.0f, 0.80f, 0.50f, 0.75f, 0.60f, 0.60f, 0.35f,  40.0f, 14000.0f, 1.30f },  // eff treble 0.52

        // -- Plates --
        { "Short Plate",      "Plates",   0, 0.8f,   5.0f, 0.50f, 0.78f, 0.90f, 1500.0f, 0.90f, 0.20f, 0.50f, 0.0f, 0.0f, 0.30f,  80.0f, 14000.0f, 1.0f  },  // eff treble 0.78
        { "Long Plate",       "Plates",   0, 4.5f,  25.0f, 0.70f, 0.58f, 1.10f, 1000.0f, 0.85f, 0.45f, 0.65f, 0.0f, 0.0f, 0.35f,  50.0f, 13000.0f, 1.20f },  // eff treble 0.58

        // -- Ambient / FX --
        { "Infinite Pad",     "Ambient",  4, 20.0f, 50.0f, 0.90f, 0.40f, 1.25f,  600.0f, 0.95f, 0.65f, 0.95f, 0.0f, 0.0f, 0.60f,  30.0f, 16000.0f, 1.80f },  // eff treble 0.44
        { "Dark Cloud",       "Ambient",  4, 8.0f,  35.0f, 0.80f, 0.30f, 1.30f,  500.0f, 0.85f, 0.50f, 0.70f, 0.0f, 0.0f, 0.45f, 120.0f,  5500.0f, 1.30f },  // eff treble 0.33
        { "Shimmer Space",    "Ambient",  4, 5.5f,  30.0f, 0.85f, 0.90f, 0.80f, 2000.0f, 0.90f, 0.60f, 1.10f, 0.0f, 0.0f, 0.45f,  20.0f, 18000.0f, 1.70f },  // eff treble 0.99
        { "Cathedral",        "Ambient",  1, 7.0f,  60.0f, 0.85f, 0.50f, 1.25f,  700.0f, 0.80f, 0.50f, 0.70f, 0.65f, 0.65f, 0.40f,  30.0f, 15000.0f, 1.40f },  // eff treble 0.50

        // -- Special --
        { "Slap Back",        "Special",  3, 0.2f,   0.0f, 0.25f, 0.90f, 1.05f, 1200.0f, 0.30f, 0.05f, 0.30f, 0.80f, 0.25f, 0.20f,  80.0f, 13000.0f, 1.0f  },  // eff treble 0.77
        { "Gated Verb",       "Special",  3, 0.35f,  5.0f, 0.35f, 0.95f, 1.20f, 1000.0f, 0.80f, 0.10f, 0.30f, 0.70f, 0.40f, 0.50f,  60.0f, 13000.0f, 1.0f  },  // eff treble 0.81
        { "Lo-Fi Verb",       "Special",  0, 2.0f,  20.0f, 0.55f, 0.50f, 1.0f,   800.0f, 0.60f, 0.20f, 0.50f, 0.0f, 0.0f, 0.35f, 200.0f,  4000.0f, 0.50f },  // eff treble 0.50
        { "Wide Stereo",      "Special",  2, 1.5f,  15.0f, 0.60f, 0.55f, 1.0f,  1200.0f, 0.75f, 0.40f, 0.80f, 0.50f, 0.50f, 0.25f,  50.0f, 16000.0f, 2.0f  },  // eff treble 0.63
    };
    return presets;
}
// clang-format on
