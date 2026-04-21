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
        if (auto* p = apvts.getParameter ("gain_trim"))     p->setValueNotifyingHost (p->convertTo0to1 (gainTrim));
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
        // -- Halls --
        // HallQuadSmooth: onset ramp + crest limiter for +5.7dB crest excess on HallQuad
        { "Homestar Blade Runner",                    "Halls",      0, 25.7000f, 31.1f, 0.8f, 1.0338f, 1.2222f, 1.5277f, 1.00f, 0.356f, 3.99f, 0.28f, 0.69f, 0.3f, 20.0f, 6004.0f, 1.0f, 0.0f, 50.0f, -11.7f },
        { "Pad Hall",                                 "Halls",      1, 8.2388f, 50.0f, 1.0f, 1.0104f, 1.6731f, 2288.0f, 0.90f, 0.380f, 2.53f, 0.15f, 0.25f, 0.3f, 27.9f, 5674.1f, 1.0f, 0.0f, 50.0f, -3.5f },
        // HallFDN: Concert Wave (FDN matches its decay/crest character)
        { "Concert Wave",                             "Halls",      2, 4.9103f, 24.1f, 0.35f, 1.5f, 1.03f, 799.0f, 0.47f, 0.350f, 3.78f, 0.39f, 0.11f, 0.3f, 20.0f, 12263.4f, 1.0f, 0.0f, 50.0f, -6.2f },
        // HallQuad: Huge Synth Hall (QuadTank's 48-tap output has lower crest than FDN)
        { "Huge Synth Hall",                          "Halls",      3, 6.2586f, 80.0f, 0.812f, 1.1333f, 1.4f, 2519.0f, 0.76f, 0.808f, 2.25f, 0.69f, 0.52f, 0.3f, 45.8f, 5504.0f, 1.0f, 0.0f, 50.0f, -7.4f },
        // HallQuadSustain: presets where all topologies decay too fast
        { "Small Vocal Hall",                         "Halls",      36, 2.4253f, 23.1f, 0.504f, 1.5000f, 0.6150f, 3332.0f, 0.84f, 1.000f, 0.46f, 0.64f, 0.26f, 0.3f, 87.5f, 5000.0f, 1.0f, 0.0f, 50.0f, -22.3f },
        // HallSlow: DattorroTank with higher decay scale
        { "Fat Snare Hall",                           "Halls",      5, 3.8418f, 11.0f, 0.568f, 1.5f, 0.525f, 0.6562f, 0.92f, 1.000f, 0.40f, 0.07f, 0.48f, 0.3f, 20.0f, 4500.0f, 1.0f, 0.0f, 50.0f, -26.5f },
        { "Snare Hall",                               "Halls",      6, 7.4686f, 11.0f, 0.5f, 1.5f, 0.54f, 0.6750f, 0.77f, 1.000f, 0.40f, 0.20f, 0.20f, 0.3f, 20.0f, 5004.0f, 1.0f, 0.0f, 50.0f, -30.30f },
        // HallQuad: Long Synth Hall (DattorroTank decay too fast at 0.37x, QuadTank sustains better)
        { "Long Synth Hall",                          "Halls",      7, 6.6292f, 8.0f, 1.0f, 1.1638f, 0.5782f, 650.0f, 1.00f, 0.492f, 1.13f, 0.25f, 0.70f, 0.3f, 20.0f, 8314.1f, 1.0f, 0.0f, 50.0f, -4.0f },
        { "Very Nice Hall",                           "Halls",      8, 9.0229f, 80.0f, 0.716f, 1.2618f, 0.9933f, 540.0f, 0.48f, 0.508f, 1.64f, 0.5500f, 0.05f, 0.3f, 150.0f, 10443.8f, 1.0f, 0.0f, 50.0f, -19.6f },
        // HallQuad: QuadTank's 48-tap output reduces crest from +4.1dB (DattorroTank) to within threshold
        { "Vocal Hall",                               "Halls",      9, 2.9134f, 8.0f, 0.55f, 1.2936f, 1.63f, 771.0f, 0.93f, 0.230f, 2.25f, 0.69f, 0.46f, 0.3f, 45.8f, 8504.1f, 1.0f, 0.0f, 50.0f, -7.6f },
        // -- Plates --
        { "Drum Plate",                               "Plates",      10, 2.9249f, 0.0f, 0.8f, 1.2557f, 0.35f, 1425.0f, 0.85f, 0.192f, 1.20f, 1.0f, 0.32f, 0.3f, 20.0f, 10000.0f, 1.0f, 0.0f, 50.0f, -11.65f },
        { "Fat Drums",                                "Plates",      11, 3.5742f, 0.0f, 0.7f, 1.0784f, 1.296f, 1735.0f, 1.00f, 0.192f, 1.80f, 0.05f, 0.52f, 0.3f, 20.0f, 15000.0f, 1.0f, 0.0f, 50.0f, -9.0f },
        { "Large Plate",                              "Plates",      12, 1.7986f, 10.0f, 0.75f, 1.4232f, 1.4976f, 4000.0f, 0.85f, 0.192f, 1.01f, 0.10f, 0.30f, 0.3f, 20.0f, 10000.0f, 1.0f, 0.0f, 50.0f, -10.2f },
        { "Steel Plate",                              "Plates",      13, 5.2871f, 15.0f, 0.472f, 1.5f, 1.9800f, 2.4750f, 0.85f, 0.192f, 1.01f, 0.10f, 0.19f, 0.3f, 200.7f, 15000.0f, 1.0f, 0.0f, 50.0f, -24.01f },
        { "Tight Plate",                              "Plates",      14, 1.3819f, 0.0f, 0.35f, 1.5f, 0.783f, 1147.0f, 0.93f, 0.192f, 1.20f, 0.20f, 0.16f, 0.3f, 20.0f, 10000.0f, 1.0f, 0.0f, 50.0f, -20.5f },
        { "Vocal Plate",                              "Plates",      15, 1.4211f, 20.1f, 0.44f, 0.912f, 0.893f, 4000.0f, 1.00f, 0.192f, 1.80f, 0.05f, 0.23f, 0.3f, 20.0f, 20000.0f, 1.0f, 0.0f, 50.0f, -13.5f },
        { "Vox Plate",                                "Plates",      16, 1.5028f, 0.0f, 0.672f, 1.2696f, 0.3245f, 1918.0f, 0.96f, 1.000f, 1.01f, 1.0000f, 0.33f, 0.3f, 20.0f, 9003.4f, 1.0f, 0.0f, 50.0f, -10.3f },
        // -- Rooms --
        { "Dark Vocal Room",                          "Rooms",      17, 2.6493f, 9.0f, 0.5f, 1.0109f, 0.5529f, 4000.0f, 0.79f, 0.000f, 0.80f, 0.10f, 0.26f, 0.3f, 20.0f, 10000.0f, 1.0f, 0.0f, 50.0f, -11.7f },
        { "Exciting Snare Room",                      "Rooms",      18, 0.4195f, 17.0f, 0.324f, 0.6637f, 1.7f, 799.0f, 1.00f, 0.144f, 0.78f, 0.65f, 0.13f, 0.3f, 20.0f, 6500.0f, 1.0f, 0.0f, 50.0f, -18.7f },
        { "Fat Snare Room",                           "Rooms",      19, 2.7840f, 15.0f, 0.388f, 1.5f, 0.6630f, 0.8288f, 0.94f, 0.05f, 1.50f, 0.42f, 0.18f, 0.3f, 20.0f, 6800.0f, 1.0f, 0.0f, 50.0f, -41.0f },
        { "Lively Snare Room",                        "Rooms",      20, 1.2783f, 11.0f, 0.5039f, 1.5f, 0.3000f, 2220.0f, 0.85f, 0.188f, 1.50f, 0.2500f, 0.20f, 0.3f, 20.0f, 10000.0f, 1.0f, 0.0f, 50.0f, -10.3f },
        { "Long Dark 70s Snare Room",                 "Rooms",      21, 1.5312f, 17.0f, 1.0f, 1.106f, 1.2947f, 878.0f, 1.00f, 0.188f, 1.50f, 0.02f, 0.67f, 0.3f, 20.0f, 6000.0f, 1.0f, 0.0f, 50.0f, -23.8f },
        { "Short Dark Snare Room",                    "Rooms",      22, 0.8983f, 5.0f, 0.3681f, 0.8625f, 0.7752f, 700.0f, 0.96f, 0.188f, 1.50f, 0.40f, 0.18f, 0.3f, 20.0f, 8000.0f, 1.0f, 0.0f, 50.0f, -6.7f },
        // -- Chambers & Plates --
        { "A Plate",                                  "Plates",      23, 3.7559f, 0.0f, 0.6f, 1.4418f, 1.4144f, 479.0f, 0.85f, 0.312f, 0.50f, 0.05f, 0.24f, 0.3f, 20.0f, 20000.0f, 1.0f, 0.0f, 50.0f, -30.0f },
        { "Clear Chamber",                            "Chambers",      24, 1.68f, 0.0f, 1.0f, 1.5f, 1.5444f, 496.0f, 1.00f, 0.192f, 1.40f, 0.60f, 0.40f, 0.3f, 20.0f, 14503.4f, 1.0f, 0.0f, 50.0f, -15.6f },
        { "Fat Plate",                                "Plates",      25, 27.6100f, 2.0f, 0.7f, 0.906f, 1.1495f, 1.4369f, 1.00f, 0.312f, 0.50f, 0.07f, 0.58f, 0.3f, 20.0f, 13000.0f, 1.0f, 0.0f, 50.0f, -33.65f },
        { "Large Chamber",                            "Chambers",      26, 5.9066f, 10.0f, 1.0f, 0.3837f, 0.4641f, 0.5801f, 1.00f, 0.304f, 0.74f, 0.54f, 0.61f, 0.3f, 20.0f, 10000.0f, 1.0f, 0.0f, 50.0f, -15.3f },
        { "Large Wood Room",                          "Chambers",      27, 2.5184f, 0.0f, 0.600f, 1.0722f, 0.5650f, 4000.0f, 0.95f, 0.312f, 0.50f, 0.67f, 0.29f, 0.3f, 20.0f, 10014.1f, 1.0f, 0.0f, 50.0f, -20.46f },
        { "Live Vox Chamber",                         "Chambers",      28, 1.5763f, 0.0f, 0.7001f, 1.4094f, 1.5000f, 2.5000f, 0.80f, 0.360f, 0.32f, 0.15f, 0.36f, 0.3f, 325.9f, 15000.0f, 1.0f, 0.0f, 50.0f, -37.50f },
        { "Medium Gate",                              "Chambers",      29, 6.2162f, 0.0f, 0.14f, 1.5f, 0.48f, 0.6000f, 1.00f, 0.192f, 1.32f, 0.46f, 0.10f, 0.3f, 20.0f, 9504.1f, 1.0f, 106.0f, 150.0f, -17.61f },
        { "Rich Chamber",                             "Chambers",      30, 3.0000f, 25.1f, 0.7f, 1.5f, 1.58f, 3831.0f, 1.00f, 0.160f, 1.30f, 0.05f, 0.39f, 0.3f, 20.0f, 10500.0f, 1.0f, 0.0f, 50.0f, -29.49f },
        { "Small Chamber1",                           "Chambers",      31, 4.5110f, 30.0f, 0.4999f, 1.4681f, 1.9126f, 662.0f, 1.00f, 0.192f, 1.20f, 0.35f, 0.10f, 0.3f, 20.0f, 12502.4f, 1.0f, 0.0f, 50.0f, -23.69f },
        { "Small Chamber2",                           "Chambers",      32, 1.1510f, 0.0f, 0.5001f, 1.3141f, 0.90f, 1079.0f, 1.00f, 0.05f, 1.20f, 0.20f, 0.30f, 0.3f, 80.0f, 15002.4f, 1.0f, 0.0f, 50.0f, -27.09f },
        { "Snare Plate",                              "Plates",      33, 3.1648f, 60.1f, 0.5f, 1.5f, 0.5f, 854.0f, 1.00f, 0.312f, 0.50f, 0.30f, 0.15f, 0.3f, 20.0f, 20000.0f, 1.0f, 0.0f, 50.0f, -17.8f },
        { "Thin Plate",                               "Plates",      34, 3.1260f, 0.0f, 0.4499f, 1.3612f, 0.375f, 354.0f, 0.85f, 0.312f, 0.50f, 0.75f, 0.18f, 0.3f, 20.0f, 20000.0f, 1.0f, 0.0f, 50.0f, -22.43f },
        { "Tiled Room",                               "Chambers",      35, 0.802f, 4.0f, 0.107f, 0.4000f, 1.25f, 2850.0f, 0.42f, 0.144f, 0.78f, 0.50f, 0.04f, 0.3f, 20.0f, 15000.0f, 1.0f, 0.0f, 50.0f, -35.5f },
        // -- Ambience --
        { "Ambience",                                 "Ambience",      36, 2.1611f, 0.0f, 0.2f, 1.5f, 0.61f, 241.0f, 1.00f, 0.360f, 0.32f, 0.20f, 0.00f, 0.3f, 20.0f, 15000.0f, 1.0f, 0.0f, 50.0f, -5.7f },
        { "Ambience Plate",                           "Plates",      37, 1.7822f, 15.5f, 0.168f, 0.5748f, 1.4652f, 885.0f, 1.00f, 0.748f, 1.64f, 0.49f, 0.11f, 0.3f, 33.8f, 9370.4f, 1.0f, 0.0f, 50.0f, -22.9f },
        { "Ambience Tiled Room",                      "Ambience",      38, 2.236f, 4.0f, 0.107f, 0.8063f, 0.42f, 4000.0f, 0.50f, 0.144f, 0.78f, 0.01f, 0.00f, 0.3f, 20.0f, 15005.8f, 1.0f, 0.0f, 50.0f, -8.0f },
        { "Big Ambience Gate",                        "Ambience",      39, 0.7830f, 12.0f, 1.0f, 1.4705f, 1.07f, 401.0f, 1.00f, 1.000f, 1.53f, 0.10f, 0.10f, 0.3f, 20.0f, 8000.0f, 1.0f, 450.0f, 120.0f, -0.5f },
        { "Cross Stick Room",                         "Ambience",      40, 2.2657f, 17.0f, 0.1f, 1.5f, 0.395f, 0.4938f, 1.00f, 0.664f, 3.00f, 0.01f, 0.05f, 0.3f, 20.0f, 9003.8f, 1.0f, 0.0f, 50.0f, -4.79f },
        { "Drum Air",                                 "Ambience",      41, 2.3463f, 9.0f, 0.1001f, 1.5f, 0.325f, 700.0f, 1.00f, 1.000f, 0.40f, 0.10f, 0.00f, 0.3f, 20.0f, 7502.5f, 1.0f, 0.0f, 50.0f, -7.5f },
        { "Gated Snare",                              "Ambience",      42, 1.60f, 9.0f, 0.80f, 1.00f, 1.00f, 700.0f, 0.92f, 0.524f, 0.40f, 0.05f, 0.05f, 0.3f, 20.0f, 8000.0f, 1.1f, 338.0f, 8.7f, 0.30f },
        { "Large Ambience",                           "Ambience",      43, 2.3317f, 0.0f, 0.600f, 1.4634f, 0.4000f, 332.0f, 1.00f, 1.000f, 1.50f, 0.00f, 0.00f, 0.3f, 20.0f, 8000.0f, 1.0f, 0.0f, 50.0f, -14.2f },
        { "Large Gated Snare",                        "Ambience",      44, 2.5426f, 11.0f, 0.4041f, 1.5f, 0.9604f, 401.0f, 1.00f, 1.000f, 1.88f, 0.08f, 0.05f, 0.3f, 20.0f, 10503.8f, 1.0f, 200.0f, 500.0f, -7.0f },
        { "Med Ambience",                             "Ambience",      45, 0.97f, 0.0f, 0.500f, 0.86f, 0.5f, 799.0f, 1.00f, 1.000f, 1.50f, 0.12f, 0.10f, 0.3f, 20.0f, 9000.0f, 1.0f, 0.0f, 50.0f, -20.7f },
        { "Short Vocal Ambience",                     "Ambience",      46, 3.6483f, 23.1f, 0.15f, 1.7f, 1.3268f, 1.6585f, 1.00f, 1.000f, 0.80f, 0.08f, 0.05f, 0.3f, 20.0f, 10000.0f, 1.0f, 0.0f, 50.0f, -21.69f },
        { "Small Ambience",                           "Ambience",      47, 1.6948f, 10.0f, 0.22f, 1.5f, 0.57f, 800.0f, 1.00f, 1.000f, 1.50f, 0.15f, 0.05f, 0.3f, 20.0f, 10500.0f, 1.0f, 0.0f, 50.0f, -17.93f },
        { "Small Drum Room",                          "Ambience",      48, 12.0000f, 11.0f, 0.300f, 0.6000f, 0.6000f, 800.0f, 1.00f, 1.000f, 0.40f, 0.15f, 0.00f, 0.3f, 20.0f, 8000.0f, 2.0f, 0.0f, 50.0f, -15.8f },
        { "Snare Ambience",                           "Ambience",      49, 1.8802f, 0.0f, 0.3401f, 0.5250f, 0.54f, 200.0f, 1.00f, 0.360f, 0.32f, 0.00f, 0.00f, 0.3f, 20.0f, 15002.4f, 1.0f, 0.0f, 50.0f, -8.3f },
        { "Tight Ambience Gate",                      "Ambience",    50, 0.2860f, 11.0f, 0.352f, 1.4415f, 0.6900f, 0.8625f, 1.00f, 1.000f, 0.89f, 0.00f, 0.05f, 0.3f, 20.0f, 5500.0f, 1.0f, 190.8f, 130.0f, -5.16f },
        // AmbientQuad: QuadTank's 48-tap output reduces crest from +3.5dB (AmbientFDN) to within threshold
        { "Trip Hop Snare",                           "Ambience",      51, 1.5740f, 0.0f, 0.636f, 1.4357f, 0.9996f, 800.0f, 1.00f, 1.000f, 0.40f, 0.00f, 0.00f, 0.3f, 20.0f, 6500.0f, 1.5f, 0.0f, 50.0f, -7.45f },
        { "Very Small Ambience",                      "Ambience",      52, 4.0325f, 0.0f, 0.1f, 1.5f, 0.435f, 799.0f, 1.00f, 1.000f, 0.50f, 0.15f, 0.05f, 0.3f, 20.0f, 9500.0f, 1.0f, 0.0f, 50.0f, -15.09f },
    };
    return presets;
}
// clang-format on
