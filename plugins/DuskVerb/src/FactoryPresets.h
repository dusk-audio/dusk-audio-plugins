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
        { "Homestar Blade Runner",                    "Halls",      32, 4.75f, 31.1f, 1.000f, 0.26f, 1.26f, 2302.0f, 1.00f, 0.356f, 3.99f, 0.00f, 0.69f, 0.3f, 20.0f, 6004.0f, 1.0f, 0.0f, 50.0f, -40.0f },
        { "Pad Hall",                                 "Halls",      33, 5.69f, 115.1f, 1.000f, 0.20f, 1.69f, 2288.0f, 0.90f, 0.380f, 2.53f, 0.15f, 0.25f, 0.3f, 27.9f, 5674.1f, 1.0f, 0.0f, 50.0f, -7.3f },
        // HallFDN: Concert Wave (FDN matches its decay/crest character)
        { "Concert Wave",                             "Halls",      34, 3.03f, 24.1f, 1.000f, 1.00f, 1.03f, 799.0f, 0.47f, 0.350f, 3.78f, 0.39f, 0.11f, 0.3f, 20.0f, 12263.4f, 1.0f, 0.0f, 50.0f, -24.4f },
        // HallQuad: Huge Synth Hall (QuadTank's 48-tap output has lower crest than FDN)
        { "Huge Synth Hall",                          "Halls",      35, 5.17f, 115.1f, 0.812f, 0.53f, 1.40f, 2519.0f, 0.76f, 0.808f, 2.25f, 0.69f, 0.52f, 0.3f, 45.8f, 5504.0f, 1.0f, 0.0f, 50.0f, -24.4f },
        // HallQuadSustain: presets where all topologies decay too fast
        { "Small Vocal Hall",                         "Halls",      36, 10.15f, 23.1f, 0.504f, 1.00f, 0.50f, 3332.0f, 0.84f, 1.000f, 0.46f, 0.64f, 0.26f, 0.3f, 87.5f, 5000.0f, 1.0f, 0.0f, 50.0f, -35.6f },
        // HallSlow: DattorroTank with higher decay scale
        { "Fat Snare Hall",                           "Halls",      37, 1.49f, 11.0f, 0.568f, 1.00f, 0.50f, 3396.0f, 0.92f, 1.000f, 0.40f, 0.31f, 0.48f, 0.3f, 20.0f, 4500.0f, 1.0f, 0.0f, 50.0f, -31.8f },
        { "Snare Hall",                               "Halls",      38, 1.17f, 11.0f, 0.500f, 1.00f, 0.50f, 4000.0f, 0.77f, 1.000f, 0.40f, 0.75f, 0.20f, 0.3f, 20.0f, 5004.0f, 1.0f, 0.0f, 50.0f, -40.0f },
        // HallQuad: Long Synth Hall (DattorroTank decay too fast at 0.37x, QuadTank sustains better)
        { "Long Synth Hall",                          "Halls",      39, 11.02f, 8.0f, 1.000f, 0.67f, 0.59f, 650.0f, 1.00f, 0.492f, 1.13f, 0.25f, 0.70f, 0.3f, 20.0f, 8314.1f, 1.0f, 0.0f, 50.0f, -21.8f },
        { "Very Nice Hall",                           "Halls",      40, 11.38f, 42.3f, 0.716f, 0.73f, 1.72f, 540.0f, 0.48f, 0.508f, 1.64f, 0.10f, 0.05f, 0.3f, 117.3f, 10443.8f, 1.0f, 0.0f, 50.0f, -30.6f },
        // HallQuad: QuadTank's 48-tap output reduces crest from +4.1dB (DattorroTank) to within threshold
        { "Vocal Hall",                               "Halls",      41, 2.61f, 8.0f, 1.000f, 0.82f, 1.63f, 771.0f, 0.93f, 0.230f, 2.25f, 0.69f, 0.46f, 0.3f, 45.8f, 8504.1f, 1.0f, 0.0f, 50.0f, -28.2f },
        // -- Plates --
        { "Drum Plate",                               "Plates",      42, 2.53f, 0.0f, 0.800f, 0.77f, 0.83f, 1425.0f, 0.85f, 0.192f, 1.20f, 0.30f, 0.32f, 0.3f, 20.0f, 10000.0f, 1.0f, 0.0f, 50.0f, -29.6f },
        { "Fat Drums",                                "Plates",      43, 2.96f, 10.0f, 0.700f, 0.65f, 1.20f, 1735.0f, 1.00f, 0.192f, 1.80f, 0.05f, 0.52f, 0.3f, 20.0f, 15000.0f, 1.0f, 0.0f, 50.0f, -17.9f },
        { "Large Plate",                              "Plates",      44, 1.81f, 10.0f, 0.750f, 0.94f, 1.44f, 4000.0f, 0.85f, 0.192f, 1.01f, 0.10f, 0.30f, 0.3f, 20.0f, 10000.0f, 1.0f, 0.0f, 50.0f, -30.0f },
        { "Steel Plate",                              "Plates",      45, 1.87f, 15.0f, 0.472f, 1.00f, 2.00f, 2895.0f, 0.85f, 0.192f, 1.01f, 0.10f, 0.19f, 0.3f, 200.7f, 15000.0f, 1.0f, 0.0f, 50.0f, -26.7f },
        { "Tight Plate",                              "Plates",      46, 0.67f, 10.0f, 0.350f, 1.00f, 0.58f, 1147.0f, 0.93f, 0.192f, 1.20f, 0.20f, 0.16f, 0.3f, 20.0f, 10000.0f, 1.0f, 0.0f, 50.0f, -31.3f },
        { "Vocal Plate",                              "Plates",      47, 1.00f, 20.1f, 0.440f, 0.19f, 0.95f, 4000.0f, 1.00f, 0.192f, 1.80f, 0.05f, 0.23f, 0.3f, 20.0f, 20000.0f, 1.0f, 0.0f, 50.0f, -27.2f },
        { "Vox Plate",                                "Plates",      48, 0.96f, 0.0f, 0.672f, 0.81f, 1.10f, 1918.0f, 0.96f, 1.000f, 1.01f, 0.66f, 0.33f, 0.3f, 20.0f, 9003.4f, 1.0f, 0.0f, 50.0f, -26.6f },
        // -- Rooms --
        { "Dark Vocal Room",                          "Rooms",      49, 1.94f, 9.0f, 0.500f, 0.43f, 0.57f, 4000.0f, 0.79f, 0.000f, 0.80f, 0.10f, 0.26f, 0.3f, 20.0f, 10000.0f, 1.0f, 0.0f, 50.0f, -31.5f },
        { "Exciting Snare Room",                      "Rooms",      50, 0.91f, 17.0f, 0.324f, 0.76f, 2.00f, 799.0f, 1.00f, 0.144f, 0.78f, 1.00f, 0.13f, 0.3f, 20.0f, 6500.0f, 1.0f, 0.0f, 50.0f, -34.0f },
        { "Fat Snare Room",                           "Rooms",      51, 6.80f, 9.0f, 0.388f, 1.00f, 0.65f, 626.0f, 0.94f, 0.308f, 1.50f, 0.50f, 0.18f, 0.3f, 20.0f, 6800.0f, 1.0f, 0.0f, 50.0f, -15.9f },
        { "Lively Snare Room",                        "Rooms",      52, 1.73f, 11.0f, 0.504f, 1.00f, 0.50f, 2220.0f, 0.85f, 0.188f, 1.50f, 0.05f, 0.20f, 0.3f, 20.0f, 10000.0f, 1.0f, 0.0f, 50.0f, -30.8f },
        { "Long Dark 70s Snare Room",                 "Rooms",      53, 0.78f, 17.0f, 1.000f, 0.62f, 1.21f, 878.0f, 1.00f, 0.188f, 1.50f, 0.02f, 0.67f, 0.3f, 20.0f, 6000.0f, 1.0f, 0.0f, 50.0f, -28.4f },
        { "Short Dark Snare Room",                    "Rooms",      54, 1.13f, 5.0f, 0.368f, 0.50f, 1.02f, 700.0f, 0.96f, 0.188f, 1.50f, 0.40f, 0.18f, 0.3f, 20.0f, 8000.0f, 1.0f, 0.0f, 50.0f, -36.0f },
        // -- Chambers & Plates --
        { "A Plate",                                  "Plates",      55, 2.06f, 0.0f, 0.600f, 0.94f, 1.36f, 479.0f, 0.85f, 0.312f, 0.50f, 0.75f, 0.24f, 0.3f, 20.0f, 20000.0f, 1.0f, 0.0f, 50.0f, -33.5f },
        { "Clear Chamber",                            "Chambers",      56, 1.63f, 0.0f, 1.000f, 1.00f, 1.56f, 496.0f, 1.00f, 0.192f, 1.40f, 0.60f, 0.40f, 0.3f, 20.0f, 14503.4f, 1.0f, 0.0f, 50.0f, -40.0f },
        { "Fat Plate",                                "Plates",      57, 5.53f, 2.0f, 0.950f, 0.10f, 1.21f, 1178.0f, 1.00f, 0.312f, 0.50f, 0.10f, 0.58f, 0.3f, 20.0f, 13000.0f, 1.0f, 0.0f, 50.0f, -14.5f },
        { "Large Chamber",                            "Chambers",      58, 4.95f, 10.0f, 1.000f, 0.54f, 0.51f, 4000.0f, 1.00f, 0.304f, 0.74f, 0.54f, 0.61f, 0.3f, 20.0f, 10000.0f, 1.0f, 0.0f, 50.0f, -40.0f },
        { "Large Wood Room",                          "Chambers",      59, 4.90f, 0.0f, 0.600f, 0.38f, 0.50f, 4000.0f, 0.95f, 0.312f, 0.50f, 0.67f, 0.29f, 0.3f, 20.0f, 10014.1f, 1.0f, 0.0f, 50.0f, -35.1f },
        { "Live Vox Chamber",                         "Chambers",      60, 0.82f, 0.0f, 0.700f, 0.92f, 2.00f, 1227.0f, 0.80f, 0.360f, 0.32f, 0.15f, 0.36f, 0.3f, 325.9f, 15000.0f, 1.0f, 0.0f, 50.0f, -16.7f },
        { "Medium Gate",                              "Chambers",      61, 3.02f, 0.0f, 0.140f, 1.00f, 0.50f, 332.0f, 1.00f, 0.192f, 1.32f, 0.46f, 0.10f, 0.3f, 20.0f, 9504.1f, 1.0f, 106.0f, 19.5f, -40.0f },
        { "Rich Chamber",                             "Chambers",      62, 2.02f, 25.1f, 0.700f, 1.00f, 1.58f, 3831.0f, 1.00f, 0.160f, 1.30f, 0.34f, 0.39f, 0.3f, 20.0f, 10500.0f, 1.0f, 0.0f, 50.0f, -16.4f },
        { "Small Chamber1",                           "Chambers",      63, 0.74f, 0.0f, 0.500f, 0.97f, 1.46f, 662.0f, 1.00f, 0.192f, 1.20f, 0.29f, 0.28f, 0.3f, 20.0f, 12502.4f, 1.0f, 0.0f, 50.0f, -39.5f },
        { "Small Chamber2",                           "Chambers",      64, 0.58f, 0.0f, 0.500f, 0.85f, 1.27f, 1079.0f, 1.00f, 0.192f, 1.20f, 0.45f, 0.30f, 0.3f, 20.0f, 15002.4f, 1.0f, 0.0f, 50.0f, -24.8f },
        { "Snare Plate",                              "Plates",      65, 2.33f, 60.1f, 0.500f, 1.00f, 0.50f, 854.0f, 1.00f, 0.312f, 0.50f, 0.00f, 0.05f, 0.3f, 20.0f, 20000.0f, 1.0f, 0.0f, 50.0f, -37.4f },
        { "Thin Plate",                               "Plates",      66, 1.70f, 0.0f, 0.450f, 0.85f, 0.50f, 354.0f, 0.85f, 0.312f, 0.50f, 0.75f, 0.18f, 0.3f, 20.0f, 20000.0f, 1.0f, 0.0f, 50.0f, -34.7f },
        { "Tiled Room",                               "Chambers",      67, 1.92f, 4.0f, 0.107f, 1.00f, 1.25f, 2850.0f, 0.42f, 0.144f, 0.78f, 0.50f, 0.04f, 0.3f, 20.0f, 15000.0f, 1.0f, 0.0f, 50.0f, -40.0f },
        // -- Ambience --
        { "Ambience",                                 "Ambience",      68, 3.32f, 0.0f, 0.200f, 1.00f, 0.50f, 241.0f, 1.00f, 0.360f, 0.32f, 0.20f, 0.00f, 0.3f, 20.0f, 15000.0f, 1.0f, 0.0f, 50.0f, -25.7f },
        { "Ambience Plate",                           "Plates",      69, 1.46f, 15.5f, 0.168f, 0.73f, 1.32f, 885.0f, 1.00f, 0.748f, 1.64f, 0.49f, 0.11f, 0.3f, 33.8f, 9370.4f, 1.0f, 0.0f, 50.0f, -30.9f },
        { "Ambience Tiled Room",                      "Ambience",      70, 3.77f, 4.0f, 0.107f, 0.25f, 0.50f, 4000.0f, 0.50f, 0.144f, 0.78f, 0.00f, 0.00f, 0.3f, 20.0f, 15005.8f, 1.0f, 0.0f, 50.0f, -31.1f },
        { "Big Ambience Gate",                        "Ambience",      71, 2.00f, 11.0f, 1.000f, 0.97f, 1.00f, 401.0f, 1.00f, 1.000f, 1.53f, 0.00f, 0.00f, 0.3f, 20.0f, 8000.0f, 1.0f, 450.0f, 5.0f, -18.1f },
        { "Cross Stick Room",                         "Ambience",      72, 3.11f, 17.0f, 0.100f, 1.00f, 0.50f, 432.0f, 1.00f, 0.664f, 3.00f, 0.00f, 0.05f, 0.3f, 20.0f, 9003.8f, 1.0f, 0.0f, 50.0f, -20.5f },
        { "Drum Air",                                 "Ambience",      73, 4.42f, 9.0f, 0.100f, 1.00f, 0.50f, 700.0f, 1.00f, 1.000f, 0.40f, 0.15f, 0.00f, 0.3f, 20.0f, 7502.5f, 1.0f, 0.0f, 50.0f, -26.0f },
        { "Gated Snare",                              "Ambience",      74, 1.77f, 9.0f, 0.720f, 1.00f, 0.90f, 700.0f, 0.92f, 0.524f, 0.40f, 0.00f, 0.05f, 0.3f, 20.0f, 8000.0f, 1.0f, 338.0f, 8.7f, -16.5f },
        { "Large Ambience",                           "Ambience",      75, 2.30f, 0.0f, 0.600f, 0.97f, 1.25f, 332.0f, 1.00f, 1.000f, 1.50f, 0.00f, 0.00f, 0.3f, 20.0f, 8000.0f, 1.0f, 0.0f, 50.0f, -30.0f },
        { "Large Gated Snare",                        "Ambience",      76, 1.54f, 11.0f, 0.404f, 1.00f, 0.98f, 401.0f, 1.00f, 1.000f, 1.88f, 0.15f, 0.05f, 0.3f, 20.0f, 10503.8f, 1.0f, 300.0f, 10.0f, -7.2f },
        { "Med Ambience",                             "Ambience",      77, 0.97f, 0.0f, 0.500f, 0.86f, 1.17f, 799.0f, 1.00f, 1.000f, 1.50f, 0.00f, 0.05f, 0.3f, 20.0f, 6500.0f, 1.0f, 0.0f, 50.0f, -33.5f },
        { "Short Vocal Ambience",                     "Ambience",      78, 3.73f, 23.1f, 0.150f, 1.00f, 1.24f, 4000.0f, 1.00f, 1.000f, 0.80f, 0.08f, 0.05f, 0.3f, 20.0f, 9000.0f, 1.0f, 0.0f, 50.0f, -13.3f },
        { "Small Ambience",                           "Ambience",      79, 0.89f, 0.0f, 0.220f, 1.00f, 0.50f, 800.0f, 1.00f, 1.000f, 1.50f, 0.00f, 0.05f, 0.3f, 20.0f, 8500.0f, 1.0f, 0.0f, 50.0f, -33.3f },
        { "Small Drum Room",                          "Ambience",      80, 3.13f, 11.0f, 0.300f, 0.82f, 1.05f, 800.0f, 1.00f, 1.000f, 0.40f, 0.15f, 0.00f, 0.3f, 20.0f, 6500.0f, 1.0f, 0.0f, 50.0f, -5.9f },
        { "Snare Ambience",                           "Ambience",      81, 1.47f, 0.0f, 0.340f, 1.00f, 0.50f, 200.0f, 1.00f, 0.360f, 0.32f, 0.00f, 0.00f, 0.3f, 20.0f, 15002.4f, 1.0f, 0.0f, 50.0f, -27.3f },
        { "Tight Ambience Gate",                      "Ambience",    82, 1.37f, 11.0f, 0.352f, 0.97f, 1.00f, 401.0f, 1.00f, 1.000f, 0.89f, 0.00f, 0.05f, 0.3f, 20.0f, 5500.0f, 1.0f, 190.8f, 5.0f, -22.5f },
        // AmbientQuad: QuadTank's 48-tap output reduces crest from +3.5dB (AmbientFDN) to within threshold
        { "Trip Hop Snare",                           "Ambience",      83, 0.86f, 17.0f, 0.636f, 0.95f, 0.98f, 800.0f, 1.00f, 1.000f, 0.40f, 0.00f, 0.00f, 0.3f, 20.0f, 6500.0f, 1.0f, 0.0f, 50.0f, -24.4f },
        { "Very Small Ambience",                      "Ambience",      84, 3.46f, 0.0f, 0.100f, 1.00f, 0.50f, 799.0f, 1.00f, 1.000f, 0.50f, 0.15f, 0.05f, 0.3f, 20.0f, 9500.0f, 1.0f, 0.0f, 50.0f, -21.0f },
    };
    return presets;
}
// clang-format on
