#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

// Minimal factory preset data. Each preset stores parameter values in a flat struct.
struct FactoryPreset
{
    const char* name;
    const char* category;

    int   algorithm;       // 0..24 (index into AlgorithmConfig::kAlgorithms)
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
        if (auto* p = apvts.getParameter ("gain_trim"))     p->setValueNotifyingHost (p->convertTo0to1 (gainTrim));
    }
};

// clang-format off
inline const std::vector<FactoryPreset>& getFactoryPresets(){
    static const std::vector<FactoryPreset> presets = {
        //                                              algo  decay  pre    size   damp  bass   xover   diff  modD   modR  erLv  erSz  mix   loCut  hiCut   width  gHold  gRel   trim
        { "Vocal Plate",                              "Plates",      0, 2.7500f, 20.0f, 0.44f, 0.912f, 0.893f, 1000.0f, 1.00f, 0.192f, 1.80f, 0.05f, 0.23f, 0.3f, 20.0f, 20000.0f, 1.0f, 0.0f, 50.0f, 0.0f },
        { "Drum Plate",                               "Plates",      1, 2.9249f, 0.0f, 0.8f, 1.2557f, 0.35f, 1425.0f, 0.85f, 0.192f, 1.20f, 1.0f, 0.32f, 0.3f, 20.0f, 10000.0f, 1.0f, 0.0f, 50.0f, -11.65f },
        { "Bright Plate",                             "Plates",      2, 3.1260f, 0.0f, 0.4499f, 1.3612f, 0.375f, 354.0f, 0.85f, 0.312f, 0.50f, 0.75f, 0.18f, 0.3f, 20.0f, 20000.0f, 1.0f, 0.0f, 50.0f, -22.43f },
        { "Dark Plate",                               "Plates",      3, 27.6100f, 2.0f, 0.7f, 0.906f, 1.1495f, 1.4369f, 1.00f, 0.312f, 0.50f, 0.07f, 0.58f, 0.3f, 20.0f, 13000.0f, 1.0f, 0.0f, 50.0f, -33.65f },
        { "Rich Plate",                               "Plates",      4, 1.7986f, 10.0f, 0.75f, 1.4232f, 1.4976f, 4000.0f, 0.85f, 0.192f, 1.01f, 0.10f, 0.30f, 0.3f, 20.0f, 10000.0f, 1.0f, 0.0f, 50.0f, -10.2f },
        { "Small Hall",                               "Halls",       5, 4.5110f, 30.0f, 0.4999f, 1.4681f, 1.9126f, 662.0f, 1.00f, 0.192f, 1.20f, 0.35f, 0.10f, 0.3f, 20.0f, 12502.4f, 1.0f, 0.0f, 50.0f, -23.69f },
        { "Medium Hall",                              "Halls",       6, 7.4686f, 11.0f, 0.5f, 1.5f, 0.54f, 0.6750f, 0.77f, 1.000f, 0.40f, 0.20f, 0.20f, 0.3f, 20.0f, 5004.0f, 1.0f, 0.0f, 50.0f, -30.30f },
        { "Large Hall",                               "Halls",       7, 6.2586f, 80.0f, 0.812f, 1.1333f, 1.4f, 2519.0f, 0.76f, 0.808f, 2.25f, 0.69f, 0.52f, 0.3f, 45.8f, 5504.0f, 1.0f, 0.0f, 50.0f, -7.4f },
        { "Vocal Hall",                               "Halls",       8, 2.9134f, 8.0f, 0.55f, 1.2936f, 1.63f, 771.0f, 0.93f, 0.230f, 2.25f, 0.69f, 0.46f, 0.3f, 45.8f, 8504.1f, 1.0f, 0.0f, 50.0f, -7.6f },
        { "Cathedral",                                "Halls",       9, 8.2388f, 50.0f, 1.0f, 1.0104f, 1.6731f, 2288.0f, 0.90f, 0.380f, 2.53f, 0.15f, 0.25f, 0.3f, 27.9f, 5674.1f, 1.0f, 0.0f, 50.0f, -3.5f },
        { "Drum Room",                                "Rooms",      10, 12.0000f, 11.0f, 0.300f, 0.6000f, 0.6000f, 800.0f, 1.00f, 1.000f, 0.40f, 0.10f, 0.00f, 0.3f, 20.0f, 8000.0f, 2.0f, 0.0f, 50.0f, -14.2f },
        { "Vocal Booth",                              "Rooms",      11, 4.0325f, 0.0f, 0.1f, 1.5f, 0.435f, 799.0f, 1.00f, 1.000f, 0.50f, 0.15f, 0.05f, 0.3f, 20.0f, 9500.0f, 1.0f, 0.0f, 50.0f, -15.09f },
        { "Studio Room",                              "Rooms",      12, 0.8983f, 5.0f, 0.3681f, 0.8625f, 0.7752f, 700.0f, 0.96f, 0.188f, 1.50f, 0.40f, 0.18f, 0.3f, 20.0f, 8000.0f, 1.0f, 0.0f, 50.0f, -6.7f },
        { "Live Room",                                "Rooms",      13, 0.4195f, 17.0f, 0.324f, 0.6637f, 1.7f, 799.0f, 1.00f, 0.144f, 0.78f, 0.65f, 0.13f, 0.3f, 20.0f, 6500.0f, 1.0f, 0.0f, 50.0f, -18.7f },
        { "Tight Room",                               "Rooms",      14, 0.2860f, 11.0f, 0.352f, 1.4415f, 0.6900f, 0.8625f, 1.00f, 1.000f, 0.89f, 0.00f, 0.05f, 0.3f, 20.0f, 5500.0f, 1.0f, 190.8f, 130.0f, -5.16f },
        { "Vocal Chamber",                            "Chambers",   15, 0.97f, 0.0f, 0.500f, 0.86f, 0.5f, 799.0f, 1.00f, 1.000f, 1.50f, 0.12f, 0.10f, 0.3f, 20.0f, 9000.0f, 1.0f, 0.0f, 50.0f, -20.7f },
        { "Drum Chamber",                             "Chambers",   16, 3.1648f, 60.1f, 0.5f, 1.5f, 0.5f, 854.0f, 1.00f, 0.312f, 0.50f, 0.30f, 0.15f, 0.3f, 20.0f, 20000.0f, 1.0f, 0.0f, 50.0f, -17.8f },
        { "Bright Chamber",                           "Chambers",   17, 1.68f, 0.0f, 1.0f, 1.5f, 1.5444f, 496.0f, 1.00f, 0.192f, 1.40f, 0.60f, 0.40f, 0.3f, 20.0f, 14503.4f, 1.0f, 0.0f, 50.0f, -15.6f },
        { "Dark Chamber",                             "Chambers",   18, 2.6493f, 9.0f, 0.5f, 1.0109f, 0.5529f, 4000.0f, 0.79f, 0.000f, 0.80f, 0.10f, 0.26f, 0.3f, 20.0f, 10000.0f, 1.0f, 0.0f, 50.0f, -11.7f },
        { "Live Chamber",                             "Chambers",   19, 5.9066f, 10.0f, 1.0f, 0.3837f, 0.4641f, 0.5801f, 1.00f, 0.304f, 0.74f, 0.54f, 0.61f, 0.3f, 20.0f, 10000.0f, 1.0f, 0.0f, 50.0f, -15.3f },
        { "Shimmer",                                  "Special",    20, 2.3463f, 9.0f, 0.1001f, 0.7f, 0.325f, 700.0f, 1.00f, 1.000f, 0.40f, 0.10f, 0.00f, 0.3f, 20.0f, 7502.5f, 1.0f, 0.0f, 50.0f, -7.5f },
        { "Reverse",                                  "Special",    21, 6.2162f, 0.0f, 0.14f, 1.5f, 0.48f, 0.6000f, 1.00f, 0.192f, 1.32f, 0.46f, 0.10f, 0.3f, 20.0f, 9504.1f, 1.0f, 106.0f, 150.0f, -17.61f },
        { "Modulated",                                "Special",    22, 0.7830f, 12.0f, 1.0f, 1.4705f, 1.07f, 401.0f, 1.00f, 1.000f, 1.53f, 0.10f, 0.10f, 0.3f, 20.0f, 8000.0f, 1.0f, 450.0f, 120.0f, -0.5f },
        { "Infinite",                                 "Special",    23, 1.60f, 9.0f, 0.80f, 1.00f, 1.00f, 700.0f, 0.92f, 0.524f, 0.40f, 0.05f, 0.05f, 0.3f, 20.0f, 8000.0f, 1.1f, 338.0f, 8.7f, 0.30f },
        { "Gated",                                    "Special",    24, 20.0000f, 30.0f, 0.7f, 1.0f, 1.5f, 500.0f, 1.00f, 0.356f, 1.5f, 0.10f, 0.69f, 0.3f, 20.0f, 7000.0f, 1.0f, 0.0f, 50.0f, -8.1f },
    };
    return presets;
}
// clang-format on
