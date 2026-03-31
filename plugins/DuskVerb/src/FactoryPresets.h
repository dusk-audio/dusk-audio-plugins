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
    float gainTrim;        // dB offset applied to wet signal (0.0 = no adjustment)

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
        if (auto* p = apvts.getParameter ("bus_mode"))      p->setValueNotifyingHost (1.0f);
    }
};

// clang-format off
//
// Translated from VintageVerb factory presets.
// Effective value = raw param * algorithm scale factor
// Algorithm trebleMultScale: Plate=0.65, Hall=0.50, Chamber=0.90, Room=0.45, Ambient=0.80
// Algorithm bassMultScale:   Plate=1.0, Hall=1.0,  Chamber=1.0, Room=0.85, Ambient=1.0
// Algorithm erLevelScale:    Plate=0.90, Hall=0.90, Chamber=1.20, Room=1.40, Ambient=0.0
// Algorithm lateGainScale:   Plate=0.20, Hall=0.22, Chamber=0.38, Room=0.38, Ambient=0.35
// Algorithm decayTimeScale:  Plate=0.94, Hall=0.79, Chamber=0.99, Room=1.28, Ambient=0.99
//
inline const std::vector<FactoryPreset>& getFactoryPresets(){
    static const std::vector<FactoryPreset> presets = {
        //                                              algo  decay  pre    size   damp  bass   xover   diff  modD   modR  erLv  erSz  mix   loCut  hiCut   width  gHold  gRel   trim
        // -- Halls (full transcoder + ER boost + calibrated decay) --
        { "Concert Wave",                       "Halls",       1, 5.79f, 24.1f, 1.000f, 1.00f, 1.04f, 799.0f, 0.47f, 0.350f, 3.78f, 0.56f, 0.76f, 0.3f, 20.0f, 12263.4f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Fat Snare Hall",                     "Halls",       1, 1.60f, 11.0f, 0.568f, 0.76f, 1.20f, 1429.0f, 0.92f, 1.000f, 0.40f, 0.42f, 0.48f, 0.3f, 20.0f, 6000.0f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Homestar Blade Runner",              "Halls",       1, 9.51f, 31.1f, 1.000f, 0.27f, 1.93f, 2314.0f, 0.62f, 0.356f, 3.99f, 0.69f, 0.69f, 0.3f, 20.0f, 6004.0f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Huge Synth Hall",                    "Halls",       1, 7.65f, 115.1f, 0.812f, 0.52f, 2.00f, 2530.0f, 0.76f, 0.808f, 2.25f, 0.57f, 0.62f, 0.3f, 45.8f, 6004.0f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Long Synth Hall",                    "Halls",       1, 3.92f, 8.0f, 1.000f, 0.66f, 1.64f, 701.0f, 1.00f, 0.492f, 1.13f, 0.67f, 0.70f, 0.3f, 20.0f, 8314.1f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Pad Hall",                           "Halls",       1,13.24f, 115.1f, 1.000f, 0.23f, 1.33f, 2218.0f, 0.90f, 0.380f, 2.53f, 0.85f, 0.60f, 0.3f, 27.9f, 6674.1f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Small Vocal Hall",                   "Halls",       1, 2.06f, 23.1f, 0.504f, 0.70f, 1.41f, 3128.0f, 0.84f, 1.000f, 0.46f, 1.01f, 0.26f, 0.3f, 87.5f, 6000.0f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Snare Hall",                         "Halls",       1, 1.13f, 11.0f, 0.500f, 0.85f, 1.18f, 997.0f, 0.77f, 1.000f, 0.40f, 1.20f, 0.20f, 0.3f, 20.0f, 6004.0f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Very Nice Hall",                     "Halls",       1,11.38f, 42.3f, 0.716f, 0.74f, 1.67f, 548.0f, 0.48f, 0.508f, 1.64f, 0.55f, 0.55f, 0.3f, 117.3f, 10443.8f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Vocal Hall",                         "Halls",       1, 3.92f, 8.0f, 1.000f, 0.63f, 1.65f, 730.0f, 0.93f, 0.230f, 2.25f, 1.09f, 0.46f, 0.3f, 45.8f, 7004.1f, 1.0f, 0.0f, 50.0f, 0.0f },
        // -- Plates (full transcoder + ER boost) --
        { "Drum Plate",                         "Plates",      0, 2.30f, 0.0f, 0.800f, 0.87f, 1.17f, 800.0f, 0.85f, 0.192f, 1.20f, 1.20f, 0.32f, 0.3f, 20.0f, 6000.0f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Fat Drums",                          "Plates",      0, 2.53f, 10.0f, 0.700f, 0.67f, 1.24f, 1514.0f, 1.00f, 0.192f, 1.80f, 0.59f, 0.52f, 0.3f, 20.0f, 15002.4f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Large Plate",                        "Plates",      0, 2.99f, 10.0f, 0.750f, 0.55f, 0.95f, 4000.0f, 0.85f, 0.192f, 1.01f, 1.20f, 0.30f, 0.3f, 20.0f, 14102.8f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Steel Plate",                        "Plates",      0, 3.46f, 15.0f, 0.472f, 0.46f, 1.18f, 2606.0f, 0.85f, 0.192f, 1.01f, 1.20f, 0.19f, 0.3f, 200.7f, 20000.0f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Tight Plate",                        "Plates",      0, 1.13f, 10.0f, 0.350f, 0.83f, 1.34f, 646.0f, 0.93f, 0.192f, 1.20f, 1.09f, 0.16f, 0.3f, 20.0f, 6000.0f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Vocal Plate",                        "Plates",      0, 1.60f, 20.1f, 0.440f, 0.48f, 0.67f, 3913.0f, 1.00f, 0.192f, 1.80f, 0.99f, 0.23f, 0.3f, 20.0f, 20000.0f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Vox Plate",                          "Plates",      0, 1.60f, 0.0f, 0.672f, 0.78f, 1.36f, 3291.0f, 0.96f, 0.192f, 1.01f, 1.05f, 0.33f, 0.3f, 20.0f, 12003.4f, 1.0f, 0.0f, 50.0f, 0.0f },
        // -- Rooms (full transcoder + ER boost) --
        { "Dark Vocal Room",                    "Rooms",       3, 2.53f, 9.0f, 0.500f, 0.43f, 0.57f, 4000.0f, 0.79f, 0.000f, 0.80f, 1.00f, 0.26f, 0.3f, 20.0f, 15002.4f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Exciting Snare room",                "Rooms",       3, 0.37f, 17.0f, 0.324f, 1.00f, 1.40f, 633.0f, 0.85f, 0.144f, 0.78f, 1.20f, 0.13f, 0.3f, 20.0f, 6000.0f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Fat Snare Room",                     "Rooms",       3, 0.67f, 9.0f, 0.388f, 1.00f, 1.37f, 3723.0f, 0.94f, 0.308f, 1.50f, 1.07f, 0.18f, 0.3f, 20.0f, 6000.0f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Lively Snare Room",                  "Rooms",       3, 0.61f, 11.0f, 0.504f, 1.00f, 1.27f, 1116.0f, 0.85f, 0.188f, 1.50f, 1.20f, 0.20f, 0.3f, 20.0f, 7004.1f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Long Dark 70s Snare Room",           "Rooms",       3, 1.60f, 17.0f, 1.000f, 0.77f, 1.20f, 941.0f, 1.00f, 0.188f, 1.50f, 0.73f, 0.67f, 0.3f, 20.0f, 6000.0f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Short Dark Snare Room",              "Rooms",       3, 0.43f, 5.0f, 0.368f, 1.00f, 1.24f, 1863.0f, 0.96f, 0.188f, 1.50f, 1.04f, 0.18f, 0.3f, 20.0f, 6000.0f, 1.0f, 0.0f, 50.0f, 0.0f },
        // -- Chambers (full transcoder + ER boost) --
        { "A Plate",                            "Plates",      0, 2.06f, 0.0f, 0.600f, 1.00f, 0.83f, 842.0f, 0.85f, 0.312f, 0.50f, 1.20f, 0.24f, 0.3f, 20.0f, 8514.1f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Clear Chamber",                      "Chambers",    2, 1.60f, 0.0f, 1.000f, 1.00f, 1.63f, 423.0f, 0.85f, 0.192f, 1.40f, 1.20f, 0.40f, 0.3f, 20.0f, 12003.4f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Fat Plate",                          "Plates",      0, 2.06f, 2.0f, 0.950f, 0.95f, 1.04f, 799.0f, 1.00f, 0.312f, 0.50f, 0.65f, 0.68f, 0.3f, 20.0f, 9282.0f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Large Chamber",                      "Chambers",    2, 2.06f, 10.0f, 1.000f, 0.53f, 0.72f, 4000.0f, 1.00f, 0.304f, 0.74f, 0.83f, 0.61f, 0.3f, 20.0f, 6404.1f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Large Wood Room",                    "Chambers",    2, 2.53f, 0.0f, 0.600f, 0.30f, 0.50f, 4000.0f, 0.95f, 0.312f, 0.50f, 1.06f, 0.29f, 0.3f, 20.0f, 8514.1f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Live Vox Chamber",                   "Chambers",    2, 0.90f, 0.0f, 0.700f, 0.87f, 1.20f, 1294.0f, 0.80f, 0.360f, 0.32f, 1.00f, 0.36f, 0.3f, 325.9f, 15002.4f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Medium Gate",                        "Chambers",    2, 2.00f, 0.0f, 0.140f, 1.00f, 1.50f, 332.0f, 1.00f, 0.192f, 1.32f, 0.69f, 0.10f, 0.3f, 20.0f, 8004.1f, 1.0f, 106.0f, 19.5f, 0.0f },
        { "Rich Chamber",                       "Chambers",    2, 2.06f, 25.1f, 0.700f, 0.83f, 1.15f, 1171.0f, 1.00f, 0.160f, 1.30f, 0.94f, 0.39f, 0.3f, 20.0f, 6000.0f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Small Chamber1",                     "Chambers",    2, 0.90f, 0.0f, 0.500f, 0.81f, 1.23f, 1065.0f, 1.00f, 0.192f, 1.20f, 0.93f, 0.28f, 0.3f, 20.0f, 15002.4f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Small Chamber2",                     "Chambers",    2, 0.90f, 0.0f, 0.500f, 0.81f, 1.25f, 800.0f, 1.00f, 0.192f, 1.20f, 0.84f, 0.30f, 0.3f, 20.0f, 15002.4f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Snare Plate",                        "Plates",      0, 2.06f, 60.1f, 0.500f, 1.00f, 0.93f, 854.0f, 1.00f, 0.312f, 0.50f, 0.77f, 0.32f, 0.3f, 20.0f, 20000.0f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Thin Plate",                         "Plates",      0, 1.60f, 0.0f, 0.450f, 1.00f, 0.77f, 483.0f, 0.85f, 0.312f, 0.50f, 1.20f, 0.18f, 0.3f, 20.0f, 20000.0f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Tiled Room",                         "Chambers",    2, 1.60f, 4.0f, 0.107f, 0.20f, 0.50f, 4000.0f, 0.42f, 0.144f, 0.78f, 1.20f, 0.04f, 0.3f, 20.0f, 10003.9f, 1.0f, 0.0f, 50.0f, 0.0f },
        // -- Ambience (full transcoder, no ERs) --
        { "Ambience",                           "Ambience",    4, 0.90f, 0.0f, 0.200f, 0.94f, 1.59f, 223.0f, 1.00f, 0.360f, 0.32f, 0.00f, 0.00f, 0.3f, 20.0f, 15002.4f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Ambience Plate",                     "Plates",      0, 1.83f, 15.5f, 0.168f, 0.75f, 1.35f, 800.0f, 1.00f, 0.748f, 1.64f, 0.74f, 0.11f, 0.3f, 33.8f, 8370.4f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Ambience Tiled Room",                "Ambience",    4, 0.67f, 4.0f, 0.107f, 1.00f, 1.40f, 4000.0f, 0.50f, 0.144f, 0.78f, 0.00f, 0.00f, 0.3f, 20.0f, 10003.9f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Big Ambience Gate",                  "Ambience",    4, 2.00f, 11.0f, 1.000f, 0.97f, 1.00f, 401.0f, 1.00f, 1.000f, 1.53f, 0.00f, 0.00f, 0.3f, 20.0f, 6000.0f, 1.0f, 450.0f, 5.0f, 0.0f },
        { "Cross Stick Room",                   "Ambience",    4, 0.55f, 17.0f, 0.100f, 1.00f, 1.43f, 471.0f, 1.00f, 0.664f, 3.00f, 0.00f, 0.00f, 0.3f, 20.0f, 6000.0f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Drum Air",                           "Ambience",    4, 0.49f, 9.0f, 0.100f, 0.98f, 1.02f, 799.0f, 1.00f, 1.000f, 0.40f, 0.00f, 0.00f, 0.3f, 20.0f, 6000.0f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Gated Snare",                        "Ambience",    4, 2.00f, 9.0f, 0.720f, 0.83f, 1.00f, 700.0f, 0.82f, 0.524f, 0.40f, 0.00f, 0.00f, 0.3f, 20.0f, 6000.0f, 1.0f, 338.0f, 8.7f, 0.0f },
        { "Large Ambience",                     "Ambience",    4, 2.06f, 0.0f, 0.600f, 0.84f, 1.18f, 800.0f, 1.00f, 1.000f, 1.50f, 0.00f, 0.00f, 0.3f, 20.0f, 6000.0f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Large Gated Snare",                  "Ambience",    4, 0.90f, 11.0f, 0.404f, 0.90f, 1.03f, 800.0f, 1.00f, 1.000f, 1.88f, 0.00f, 0.00f, 0.3f, 20.0f, 6000.0f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Med Ambience",                       "Ambience",    4, 1.83f, 0.0f, 0.500f, 0.85f, 1.19f, 799.0f, 1.00f, 1.000f, 1.50f, 0.00f, 0.00f, 0.3f, 20.0f, 6000.0f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Short Vocal Ambience",               "Ambience",    4, 0.72f, 23.1f, 0.150f, 0.89f, 1.06f, 800.0f, 1.00f, 1.000f, 0.80f, 0.00f, 0.00f, 0.3f, 20.0f, 6000.0f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Small Ambience",                     "Ambience",    4, 1.13f, 0.0f, 0.220f, 0.88f, 1.13f, 800.0f, 1.00f, 1.000f, 1.50f, 0.00f, 0.00f, 0.3f, 20.0f, 6000.0f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Small Drum Room",                    "Ambience",    4, 0.49f, 11.0f, 0.300f, 0.95f, 1.05f, 800.0f, 1.00f, 1.000f, 0.40f, 0.00f, 0.00f, 0.3f, 20.0f, 6000.0f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Snare Ambience",                     "Ambience",    4, 0.90f, 0.0f, 0.340f, 0.94f, 1.70f, 207.0f, 1.00f, 0.360f, 0.32f, 0.00f, 0.00f, 0.3f, 20.0f, 15002.4f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Tight Ambience Gate",                "Ambience",    4, 2.00f, 11.0f, 0.352f, 0.97f, 1.00f, 401.0f, 1.00f, 1.000f, 0.89f, 0.00f, 0.00f, 0.3f, 20.0f, 6000.0f, 1.0f, 190.8f, 5.0f, 0.0f },
        { "Trip Hop Snare",                     "Ambience",    4, 1.13f, 17.0f, 0.636f, 0.84f, 1.17f, 800.0f, 1.00f, 1.000f, 0.40f, 0.00f, 0.00f, 0.3f, 20.0f, 6000.0f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Very Small Ambience",                "Ambience",    4, 0.67f, 0.0f, 0.100f, 0.91f, 1.15f, 799.0f, 1.00f, 1.000f, 0.50f, 0.00f, 0.00f, 0.3f, 20.0f, 6000.0f, 1.0f, 0.0f, 50.0f, 0.0f },
    };
    return presets;
}
// clang-format on
