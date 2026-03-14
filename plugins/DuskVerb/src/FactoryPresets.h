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
    float modRate;         // 0.1-10 Hz
    float erLevel;         // 0-1
    float erSize;          // 0-1
    float mix;             // 0-1
    float loCut;           // 20-500 Hz
    float hiCut;           // 1000-20000 Hz
    float width;           // 0-2
    float gateHold;        // 0-500 ms (0 = disabled)
    float gateRelease;     // 5-500 ms

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
        if (auto* p = apvts.getParameter ("gate_hold"))    p->setValueNotifyingHost (p->convertTo0to1 (gateHold));
        if (auto* p = apvts.getParameter ("gate_release")) p->setValueNotifyingHost (p->convertTo0to1 (gateRelease));
        if (auto* p = apvts.getParameter ("freeze"))        p->setValueNotifyingHost (0.0f);
        if (auto* p = apvts.getParameter ("predelay_sync")) p->setValueNotifyingHost (0.0f);
        if (auto* p = apvts.getParameter ("bus_mode"))      p->setValueNotifyingHost (0.0f);
    }
};

// clang-format off
//
// Translated from VintageVerb factory presets.
// Effective value = raw param * algorithm scale factor
// Algorithm trebleMultScale: Plate=1.30, Hall=0.75, Chamber=1.20, Room=0.45, Ambient=0.60
// Algorithm bassMultScale:   Plate=1.0, Hall=1.0,  Chamber=1.0, Room=0.85, Ambient=1.0
// Algorithm erLevelScale:    Plate=0.0, Hall=1.0,  Chamber=0.8, Room=0.5,  Ambient=0.0
// Algorithm lateGainScale:   Plate=0.47, Hall=0.65, Chamber=0.45, Room=1.10, Ambient=0.60
// Algorithm decayTimeScale:  Plate=1.0, Hall=1.0,  Chamber=1.0, Room=1.4,  Ambient=1.0
//
inline const std::vector<FactoryPreset>& getFactoryPresets(){
    static const std::vector<FactoryPreset> presets = {
        //                                              algo  decay  pre    size   damp  bass   xover   diff  modD   modR  erLv  erSz  mix   loCut  hiCut   width  gHold  gRel
        // -- Halls --
        { "Concert Wave",                  "Halls",       1, 2.99f, 21.9f, 1.0f, 1.0f, 1.0f, 4000.0f, 0.48f, 0.35f, 0.55f, 0.04f, 0.59f, 0.3f, 20.0f, 6476.6f, 1.0f, 0.0f, 50.0f },
        { "Fat Snare Hall",                "Halls",       1, 0.55f, 11.2f, 0.568f, 1.0f, 1.2f, 348.9f, 1.0f, 1.0f, 0.11f, 0.13f, 0.56f, 0.25f, 20.0f, 1000.0f, 1.0f, 0.0f, 50.0f },
        { "Homestar Blade Runner",         "Halls",       1, 4.86f, 27.3f, 1.0f, 1.0f, 1.41f, 415.9f, 0.7f, 0.356f, 0.61f, 0.14f, 0.62f, 0.3f, 20.0f, 2000.0f, 1.0f, 0.0f, 50.0f },
        { "Huge Synth Hall",               "Halls",       1, 2.99f, 84.6f, 0.812f, 1.0f, 1.41f, 415.9f, 0.8f, 0.808f, 0.27f, 0.0f, 0.46f, 0.35f, 22.3f, 2000.0f, 1.0f, 0.0f, 50.0f },
        { "Long Synth Hall",               "Halls",       1, 2.06f, 8.5f, 1.0f, 0.7f, 1.84f, 500.0f, 1.0f, 0.492f, 0.16f, 0.15f, 0.57f, 0.35f, 20.0f, 3223.4f, 1.0f, 0.0f, 50.0f },
        { "Pad Hall",                      "Halls",       1, 4.86f, 84.6f, 1.0f, 1.0f, 1.41f, 327.3f, 1.0f, 0.38f, 0.31f, 0.0f, 0.46f, 0.45f, 21.1f, 2313.5f, 1.0f, 0.0f, 50.0f },
        { "Small Vocal Hall",              "Halls",       1, 0.78f, 21.1f, 0.504f, 1.0f, 1.32f, 365.6f, 1.0f, 1.0f, 0.12f, 0.4f, 0.57f, 0.25f, 25.4f, 1232.5f, 1.0f, 0.0f, 50.0f },
        { "Snare Hall",                    "Halls",       1, 0.4f, 11.2f, 0.5f, 1.0f, 1.2f, 348.9f, 1.0f, 1.0f, 0.11f, 0.19f, 0.52f, 0.25f, 20.0f, 2000.0f, 1.0f, 0.0f, 50.0f },
        { "Very Nice Hall",                "Halls",       1, 7.65f, 35.7f, 0.716f, 0.5f, 1.82f, 339.6f, 0.64f, 0.508f, 0.21f, 0.06f, 0.68f, 0.3f, 27.9f, 4764.3f, 1.0f, 0.0f, 50.0f },
        { "Vocal Hall",                    "Halls",       1, 1.83f, 8.5f, 1.0f, 0.8f, 1.84f, 500.0f, 1.0f, 0.23f, 0.27f, 0.23f, 0.54f, 0.25f, 22.3f, 2479.7f, 1.0f, 0.0f, 50.0f },
        // -- Plates --
        { "A Plate",                       "Plates",      0, 1.36f, 0.0f, 0.6f, 0.66f, 0.68f, 407.4f, 1.0f, 0.312f, 0.12f, 0.0f, 0.0f, 0.3f, 20.0f, 3349.6f, 1.0f, 0.0f, 50.0f },
        { "Ambience Plate",                "Plates",      0, 1.25f, 15.1f, 0.168f, 0.3f, 1.73f, 656.1f, 1.0f, 0.748f, 0.21f, 0.0f, 0.0f, 0.45f, 21.5f, 3258.6f, 1.0f, 0.0f, 50.0f },
        { "Drum Plate",                    "Plates",      0, 1.13f, 0.0f, 0.8f, 0.55f, 1.41f, 2529.1f, 1.0f, 0.192f, 0.17f, 0.0f, 0.0f, 0.25f, 20.0f, 1511.6f, 1.0f, 0.0f, 50.0f },
        { "Fat Drums",                     "Plates",      0, 1.36f, 10.3f, 0.7f, 0.72f, 1.41f, 833.3f, 1.0f, 0.192f, 0.22f, 0.0f, 0.0f, 0.25f, 20.0f, 9928.1f, 1.0f, 0.0f, 50.0f },
        { "Fat Plate",                     "Plates",      0, 1.36f, 2.6f, 0.95f, 1.0f, 1.0f, 716.1f, 1.0f, 0.312f, 0.12f, 0.0f, 0.0f, 0.3f, 20.0f, 3868.3f, 1.0f, 0.0f, 50.0f },
        { "Large Plate",                   "Plates",      0, 1.83f, 10.3f, 0.75f, 1.0f, 0.84f, 200.0f, 1.0f, 0.192f, 0.15f, 0.0f, 0.0f, 0.35f, 20.0f, 8663.3f, 1.0f, 0.0f, 50.0f },
        { "Snare Plate",                   "Plates",      0, 1.6f, 48.3f, 0.5f, 0.66f, 0.68f, 200.0f, 1.0f, 0.312f, 0.12f, 0.0f, 0.0f, 0.25f, 20.0f, 20000.0f, 1.0f, 0.0f, 50.0f },
        { "Steel Plate",                   "Plates",      0, 2.3f, 14.6f, 0.472f, 1.0f, 1.39f, 357.1f, 1.0f, 0.192f, 0.15f, 0.0f, 0.0f, 0.3f, 36.1f, 20000.0f, 1.0f, 0.0f, 50.0f },
        { "Thin Plate",                    "Plates",      0, 1.01f, 0.0f, 0.45f, 1.0f, 0.68f, 407.4f, 1.0f, 0.312f, 0.12f, 0.0f, 0.0f, 0.3f, 20.0f, 20000.0f, 1.0f, 0.0f, 50.0f },
        { "Tight Plate",                   "Plates",      0, 0.78f, 10.3f, 0.35f, 0.55f, 1.41f, 758.4f, 1.0f, 0.192f, 0.17f, 0.0f, 0.0f, 0.3f, 20.0f, 1000.0f, 1.0f, 0.0f, 50.0f },
        { "Vocal Plate",                   "Plates",      0, 0.67f, 18.8f, 0.44f, 0.85f, 1.0f, 3664.8f, 1.0f, 0.192f, 0.22f, 0.0f, 0.0f, 0.25f, 20.0f, 20000.0f, 1.0f, 0.0f, 50.0f },
        { "Vox Plate",                     "Plates",      0, 1.13f, 0.0f, 0.672f, 1.0f, 1.16f, 357.1f, 1.0f, 0.192f, 0.15f, 0.0f, 0.0f, 0.25f, 20.0f, 6206.5f, 1.0f, 0.0f, 50.0f },
        // -- Rooms --
        { "Dark Vocal Room",               "Rooms",       3, 2.06f, 9.4f, 0.5f, 0.7f, 1.0f, 348.9f, 0.8f, 0.0f, 0.14f, 0.2f, 0.54f, 0.25f, 20.0f, 9928.1f, 1.0f, 0.0f, 50.0f },
        { "Exciting Snare room",           "Rooms",       3, 0.4f, 16.3f, 0.324f, 1.0f, 1.32f, 444.3f, 1.0f, 0.144f, 0.14f, 0.48f, 0.55f, 0.25f, 20.0f, 1105.5f, 1.0f, 0.0f, 50.0f },
        { "Fat Snare Room",                "Rooms",       3, 0.84f, 9.4f, 0.388f, 1.0f, 1.2f, 464.5f, 1.0f, 0.308f, 0.19f, 0.35f, 0.57f, 0.25f, 20.0f, 1402.9f, 1.0f, 0.0f, 50.0f },
        { "Lively Snare Room",             "Rooms",       3, 0.67f, 11.2f, 0.504f, 1.0f, 1.2f, 500.0f, 1.0f, 0.188f, 0.19f, 0.39f, 0.58f, 0.25f, 20.0f, 2479.7f, 1.0f, 0.0f, 50.0f },
        { "Long Dark 70s Snare Room",      "Rooms",       3, 1.48f, 16.3f, 1.0f, 1.0f, 1.32f, 444.3f, 1.0f, 0.188f, 0.19f, 0.01f, 0.63f, 0.35f, 20.0f, 1000.0f, 1.0f, 0.0f, 50.0f },
        { "Short Dark Snare Room",         "Rooms",       3, 0.43f, 5.7f, 0.368f, 1.0f, 1.2f, 500.0f, 1.0f, 0.188f, 0.19f, 0.43f, 0.56f, 0.25f, 20.0f, 1068.9f, 1.0f, 0.0f, 50.0f },
        // -- Chambers --
        { "Clear Chamber",                 "Chambers",    2, 1.83f, 0.0f, 1.0f, 1.0f, 1.41f, 309.0f, 1.0f, 0.192f, 0.18f, 0.21f, 0.53f, 0.3f, 20.0f, 6206.5f, 1.0f, 0.0f, 50.0f },
        { "Large Chamber",                 "Chambers",    2, 1.6f, 10.3f, 1.0f, 1.0f, 0.96f, 395.2f, 1.0f, 0.304f, 0.13f, 0.14f, 0.59f, 0.35f, 20.0f, 2183.4f, 1.0f, 0.0f, 50.0f },
        { "Large Wood Room",               "Chambers",    2, 1.36f, 0.0f, 0.6f, 1.0f, 0.84f, 558.3f, 1.0f, 0.312f, 0.12f, 0.29f, 0.53f, 0.35f, 20.0f, 3349.6f, 1.0f, 0.0f, 50.0f },
        { "Live Vox Chamber",              "Chambers",    2, 1.01f, 0.0f, 0.7f, 1.0f, 1.41f, 256.3f, 0.8f, 0.36f, 0.11f, 0.36f, 0.54f, 0.25f, 53.1f, 9928.1f, 1.0f, 0.0f, 50.0f },
        { "Medium Gate",                   "Chambers",    2, 2.0f, 0.0f, 0.14f, 1.0f, 1.41f, 309.0f, 1.0f, 0.192f, 0.18f, 0.4f, 0.57f, 0.5f, 20.0f, 3034.6f, 1.0f, 106.0f, 19.5f },
        { "Rich Chamber",                  "Chambers",    2, 2.53f, 22.7f, 0.7f, 1.0f, 1.2f, 222.0f, 1.0f, 0.16f, 0.17f, 0.13f, 0.6f, 0.3f, 20.0f, 1912.3f, 1.0f, 0.0f, 50.0f },
        { "Small Chamber1",                "Chambers",    2, 0.9f, 0.0f, 0.5f, 1.0f, 1.41f, 256.3f, 1.0f, 0.192f, 0.17f, 0.37f, 0.53f, 0.3f, 20.0f, 9928.1f, 1.0f, 0.0f, 50.0f },
        { "Small Chamber2",                "Chambers",    2, 0.9f, 0.0f, 0.5f, 1.0f, 1.41f, 309.0f, 1.0f, 0.192f, 0.17f, 0.3f, 0.56f, 0.3f, 20.0f, 9928.1f, 1.0f, 0.0f, 50.0f },
        { "Tiled Room",                    "Chambers",    2, 0.67f, 4.7f, 0.107f, 1.0f, 1.2f, 1432.4f, 0.5f, 0.144f, 0.14f, 0.45f, 0.52f, 0.3f, 20.0f, 4408.4f, 1.0f, 0.0f, 50.0f },
        // -- Ambience --
        { "Ambience",                      "Ambience",    4, 0.9f, 0.0f, 0.2f, 1.0f, 1.84f, 200.0f, 1.0f, 0.36f, 0.11f, 0.0f, 0.0f, 0.45f, 20.0f, 9928.1f, 1.0f, 0.0f, 50.0f },
        { "Ambience Tiled Room",           "Ambience",    4, 0.84f, 4.7f, 0.107f, 1.0f, 1.2f, 1432.4f, 0.5f, 0.144f, 0.14f, 0.0f, 0.0f, 0.45f, 20.0f, 4408.4f, 1.0f, 0.0f, 50.0f },
        { "Big Ambience Gate",             "Ambience",    4, 2.0f, 11.2f, 1.0f, 1.0f, 1.0f, 348.9f, 1.0f, 1.0f, 0.19f, 0.0f, 0.0f, 0.5f, 20.0f, 1000.0f, 1.0f, 450.0f, 5.0f },
        { "Cross Stick Room",              "Ambience",    4, 0.55f, 16.3f, 0.1f, 1.0f, 1.32f, 365.6f, 1.0f, 0.664f, 0.39f, 0.0f, 0.0f, 0.3f, 20.0f, 1586.9f, 1.0f, 0.0f, 50.0f },
        { "Drum Air",                      "Ambience",    4, 0.46f, 9.4f, 0.1f, 1.0f, 1.0f, 500.0f, 1.0f, 1.0f, 0.11f, 0.0f, 0.0f, 0.25f, 20.0f, 1000.0f, 1.0f, 0.0f, 50.0f },
        { "Gated Snare",                   "Ambience",    4, 2.0f, 9.4f, 0.72f, 1.0f, 1.0f, 500.0f, 0.82f, 0.524f, 0.11f, 0.0f, 0.0f, 0.5f, 20.0f, 1000.0f, 1.0f, 338.0f, 8.7f },
        { "Large Ambience",                "Ambience",    4, 2.06f, 0.0f, 0.6f, 1.0f, 1.2f, 309.0f, 1.0f, 1.0f, 0.19f, 0.0f, 0.0f, 0.45f, 20.0f, 1000.0f, 1.0f, 0.0f, 50.0f },
        { "Large Gated Snare",             "Ambience",    4, 0.78f, 11.2f, 0.404f, 1.0f, 1.0f, 348.9f, 1.0f, 1.0f, 0.23f, 0.0f, 0.0f, 0.5f, 20.0f, 1586.9f, 1.0f, 0.0f, 50.0f },
        { "Med Ambience",                  "Ambience",    4, 1.83f, 0.0f, 0.5f, 1.0f, 1.2f, 309.0f, 1.0f, 1.0f, 0.19f, 0.0f, 0.0f, 0.45f, 20.0f, 1105.5f, 1.0f, 0.0f, 50.0f },
        { "Short Vocal Ambience",          "Ambience",    4, 0.67f, 21.1f, 0.15f, 1.0f, 1.0f, 348.9f, 1.0f, 1.0f, 0.14f, 0.0f, 0.0f, 0.45f, 20.0f, 1000.0f, 1.0f, 0.0f, 50.0f },
        { "Small Ambience",                "Ambience",    4, 1.25f, 0.0f, 0.22f, 1.0f, 1.2f, 309.0f, 1.0f, 1.0f, 0.19f, 0.0f, 0.0f, 0.45f, 20.0f, 1367.7f, 1.0f, 0.0f, 50.0f },
        { "Small Drum Room",               "Ambience",    4, 0.43f, 11.2f, 0.3f, 1.0f, 1.0f, 500.0f, 1.0f, 1.0f, 0.11f, 0.0f, 0.0f, 0.25f, 20.0f, 1000.0f, 1.0f, 0.0f, 50.0f },
        { "Snare Ambience",                "Ambience",    4, 1.01f, 0.0f, 0.34f, 1.0f, 1.84f, 200.0f, 1.0f, 0.36f, 0.11f, 0.0f, 0.0f, 0.45f, 20.0f, 9928.1f, 1.0f, 0.0f, 50.0f },
        { "Tight Ambience Gate",           "Ambience",    4, 2.0f, 11.2f, 0.352f, 1.0f, 1.0f, 348.9f, 1.0f, 1.0f, 0.14f, 0.0f, 0.0f, 0.5f, 20.0f, 1000.0f, 1.0f, 190.8f, 5.0f },
        { "Trip Hop Snare",                "Ambience",    4, 1.13f, 16.3f, 0.636f, 1.0f, 1.32f, 365.6f, 1.0f, 1.0f, 0.11f, 0.0f, 0.0f, 0.25f, 20.0f, 1586.9f, 1.0f, 0.0f, 50.0f },
        { "Very Small Ambience",           "Ambience",    4, 0.67f, 0.0f, 0.1f, 1.0f, 1.2f, 309.0f, 1.0f, 1.0f, 0.12f, 0.0f, 0.0f, 0.45f, 20.0f, 1367.7f, 1.0f, 0.0f, 50.0f },
    };
    return presets;
}
// clang-format on
