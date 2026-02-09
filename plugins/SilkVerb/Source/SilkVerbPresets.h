#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <vector>

namespace SilkVerbPresets
{

//==============================================================================
struct Preset
{
    juce::String name;
    juce::String category;
    int mode;           // 0=Plate..5=Ambience, 6=BrightHall, 7=ChorusSpace, 8=RandomSpace, 9=DirtyHall
    int color;          // 0=1970s, 1=1980s, 2=Now

    float size;         // 0.0 - 1.0
    float damping;      // 0.0 - 1.0
    float predelay;     // 0 - 250 ms
    float mix;          // 0.0 - 1.0
    float modRate;      // 0.1 - 5.0 Hz
    float modDepth;     // 0.0 - 1.0
    float width;        // 0.0 - 1.0
    float earlyDiff;    // 0.0 - 1.0
    float lateDiff;     // 0.0 - 1.0
    float bassMult;     // 0.5 - 2.0
    float bassFreq;     // 100 - 1000 Hz
    float lowCut;       // 20 - 500 Hz
    float highCut;      // 1000 - 20000 Hz
    bool freeze;        // typically false for presets
    float roomSize = 0.5f;      // 0.0 - 1.0 (default center)
    float earlyLateBal = 0.7f;  // 0.0 = all ER, 1.0 = all late
    float highDecay = 1.0f;     // 0.25 - 4.0
    float midDecay = 1.0f;      // 0.25 - 4.0 (mid-frequency decay multiplier)
    float highFreq = 4000.0f;   // 1000 - 12000 Hz (upper crossover frequency)
    float erShape = 0.5f;       // 0.0 - 1.0 (ER envelope shape)
    float erSpread = 0.5f;      // 0.0 - 1.0 (ER timing spread)
    float erBassCut = 20.0f;    // 20 - 500 Hz (ER bass cut frequency)
};

//==============================================================================
// PCM 90-inspired categories: algorithm type grouping
inline const juce::StringArray Categories = {
    "Halls",
    "Rooms",
    "Plates",
    "Creative"
};

//==============================================================================
// 192 factory presets matched from Lexicon PCM 90 impulse responses
// Generated: 2026-02-09
// Average match score: 70.9%
inline std::vector<Preset> getFactoryPresets()
{
    std::vector<Preset> presets;
    presets.reserve(192);

    // ==================== HALLS (59) ====================

    // Two different shaped ballrooms (match: 85%)
    presets.push_back({
        "Ballrooms",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.5643f,  // damping
        38.6f,  // predelay ms
        0.30f,  // mix
        0.78f,  // modRate Hz
        0.2437f,  // modDepth
        0.9216f,  // width
        0.6930f,  // earlyDiff
        0.5424f,  // lateDiff
        1.10f,  // bassMult
        403.5f,  // bassFreq Hz
        65.9f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.4580f,  // roomSize
        0.6000f,  // earlyLateBal
        1.06f  // highDecay
    });

    // Wide and abrupt sounding, gated (match: 57%)
    presets.push_back({
        "Brick Wallz",
        "Halls",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.0504f,  // damping
        4.4f,  // predelay ms
        0.30f,  // mix
        0.87f,  // modRate Hz
        0.1787f,  // modDepth
        0.8599f,  // width
        0.6143f,  // earlyDiff
        0.5114f,  // lateDiff
        1.46f,  // bassMult
        393.4f,  // bassFreq Hz
        20.7f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6051f,  // roomSize
        0.6000f,  // earlyLateBal
        0.93f  // highDecay
    });

    // Light reverb, great deal of high end (match: 82%)
    presets.push_back({
        "Bright Hall",
        "Halls",
        6,  // Bright Hall
        0,  // 1970s
        0.2343f,  // size
        0.2609f,  // damping
        0.1f,  // predelay ms
        0.30f,  // mix
        0.79f,  // modRate Hz
        0.2496f,  // modDepth
        1.0000f,  // width
        0.5608f,  // earlyDiff
        0.5513f,  // lateDiff
        0.95f,  // bassMult
        394.5f,  // bassFreq Hz
        20.7f,  // lowCut Hz
        19517.4f,  // highCut Hz
        false,
        0.6211f,  // roomSize
        0.6000f,  // earlyLateBal
        0.82f  // highDecay
    });

    // Medium-sized room, sharp medium long decay (match: 76%)
    presets.push_back({
        "Cannon Gate",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.4542f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.78f,  // modRate Hz
        0.2575f,  // modDepth
        0.9260f,  // width
        0.5307f,  // earlyDiff
        0.5279f,  // lateDiff
        0.72f,  // bassMult
        388.5f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5913f,  // roomSize
        0.6000f,  // earlyLateBal
        1.18f  // highDecay
    });

    // Medium-sized space with lots of reflections (match: 83%)
    presets.push_back({
        "Choir Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.1526f,  // damping
        27.6f,  // predelay ms
        0.30f,  // mix
        0.81f,  // modRate Hz
        0.2470f,  // modDepth
        0.8671f,  // width
        0.5567f,  // earlyDiff
        0.5243f,  // lateDiff
        1.27f,  // bassMult
        400.3f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        19896.1f,  // highCut Hz
        false,
        0.6171f,  // roomSize
        0.6000f,  // earlyLateBal
        0.95f  // highDecay
    });

    // Dense, classic Lexicon hall (match: 91%)
    presets.push_back({
        "Concert Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.4573f,  // size
        0.6182f,  // damping
        11.9f,  // predelay ms
        0.30f,  // mix
        0.80f,  // modRate Hz
        0.2543f,  // modDepth
        0.9810f,  // width
        0.5540f,  // earlyDiff
        0.5160f,  // lateDiff
        0.90f,  // bassMult
        364.6f,  // bassFreq Hz
        20.7f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6058f,  // roomSize
        0.6000f,  // earlyLateBal
        0.96f  // highDecay
    });

    // Medium bright hall (match: 78%)
    presets.push_back({
        "Dance Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.5572f,  // damping
        0.9f,  // predelay ms
        0.30f,  // mix
        0.10f,  // modRate Hz
        0.1481f,  // modDepth
        0.8663f,  // width
        0.5547f,  // earlyDiff
        0.5340f,  // lateDiff
        0.93f,  // bassMult
        402.0f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.4841f,  // roomSize
        0.6000f,  // earlyLateBal
        0.79f  // highDecay
    });

    // All-purpose hall, moderate size/decay (match: 81%)
    presets.push_back({
        "Deep Blue",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.2343f,  // size
        0.0503f,  // damping
        43.5f,  // predelay ms
        0.30f,  // mix
        0.80f,  // modRate Hz
        0.2490f,  // modDepth
        0.9120f,  // width
        0.5519f,  // earlyDiff
        0.6006f,  // lateDiff
        1.00f,  // bassMult
        400.4f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        18598.8f,  // highCut Hz
        false,
        0.6104f,  // roomSize
        0.8000f,  // earlyLateBal
        1.29f  // highDecay
    });

    // Large, washy, chorused space (match: 91%)
    presets.push_back({
        "Deep Verb",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.0752f,  // size
        0.1513f,  // damping
        12.5f,  // predelay ms
        0.30f,  // mix
        0.80f,  // modRate Hz
        0.2462f,  // modDepth
        0.9208f,  // width
        0.5848f,  // earlyDiff
        0.5239f,  // lateDiff
        1.33f,  // bassMult
        419.1f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        19057.2f,  // highCut Hz
        false,
        0.5797f,  // roomSize
        0.6000f,  // earlyLateBal
        0.94f  // highDecay
    });

    // Split, short ping-pong delay, medium-long hallway (match: 32%)
    presets.push_back({
        "Delay Hallway",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.0000f,  // size
        0.6243f,  // damping
        19.5f,  // predelay ms
        0.30f,  // mix
        0.82f,  // modRate Hz
        0.2440f,  // modDepth
        0.8614f,  // width
        0.5589f,  // earlyDiff
        0.5596f,  // lateDiff
        0.99f,  // bassMult
        408.5f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        19626.9f,  // highCut Hz
        false,
        0.6118f,  // roomSize
        0.6000f,  // earlyLateBal
        0.83f  // highDecay
    });

    // Bright, crystalline hall with subtle delay taps (match: 73%)
    presets.push_back({
        "Dream Hall",
        "Halls",
        6,  // Bright Hall
        0,  // 1970s
        0.0752f,  // size
        0.1441f,  // damping
        0.3f,  // predelay ms
        0.30f,  // mix
        0.77f,  // modRate Hz
        0.2428f,  // modDepth
        0.9180f,  // width
        0.5244f,  // earlyDiff
        0.5708f,  // lateDiff
        1.04f,  // bassMult
        375.6f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6363f,  // roomSize
        0.6000f,  // earlyLateBal
        1.37f  // highDecay
    });

    // Medium sized cave, short decay time (match: 80%)
    presets.push_back({
        "Drum Cave",
        "Halls",
        1,  // Room
        0,  // 1970s
        0.3008f,  // size
        0.3694f,  // damping
        11.0f,  // predelay ms
        0.30f,  // mix
        0.31f,  // modRate Hz
        0.2634f,  // modDepth
        0.8623f,  // width
        0.6204f,  // earlyDiff
        0.4775f,  // lateDiff
        1.17f,  // bassMult
        387.0f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6445f,  // roomSize
        0.6000f,  // earlyLateBal
        0.81f  // highDecay
    });

    // Typical Monday night at the club (match: 77%)
    presets.push_back({
        "Empty Club",
        "Halls",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.5346f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.82f,  // modRate Hz
        0.2610f,  // modDepth
        0.8573f,  // width
        0.5592f,  // earlyDiff
        0.3502f,  // lateDiff
        0.77f,  // bassMult
        390.4f,  // bassFreq Hz
        40.8f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5864f,  // roomSize
        0.6000f,  // earlyLateBal
        0.77f  // highDecay
    });

    // Large, dense room reverb for toms (match: 48%)
    presets.push_back({
        "For The Toms",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.0000f,  // size
        0.6528f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.83f,  // modRate Hz
        0.2556f,  // modDepth
        0.8671f,  // width
        0.5435f,  // earlyDiff
        0.5460f,  // lateDiff
        0.89f,  // bassMult
        279.0f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6060f,  // roomSize
        0.5000f,  // earlyLateBal
        0.79f  // highDecay
    });

    // If possible to have a gated hall (match: 29%)
    presets.push_back({
        "Gated Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.2619f,  // size
        0.6150f,  // damping
        1.2f,  // predelay ms
        0.30f,  // mix
        0.13f,  // modRate Hz
        0.2476f,  // modDepth
        0.8815f,  // width
        0.5569f,  // earlyDiff
        0.5548f,  // lateDiff
        0.82f,  // bassMult
        403.9f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5810f,  // roomSize
        0.6000f,  // earlyLateBal
        0.84f  // highDecay
    });

    // Generic concert hall, starting place (match: 74%)
    presets.push_back({
        "Gen. Concert",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.2343f,  // size
        0.5583f,  // damping
        2.0f,  // predelay ms
        0.30f,  // mix
        0.81f,  // modRate Hz
        0.2524f,  // modDepth
        0.8998f,  // width
        0.5445f,  // earlyDiff
        0.5203f,  // lateDiff
        1.47f,  // bassMult
        384.8f,  // bassFreq Hz
        20.3f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6144f,  // roomSize
        0.6000f,  // earlyLateBal
        0.85f  // highDecay
    });

    // Generic hall with random reflections (match: 82%)
    presets.push_back({
        "Gen. Random Hall",
        "Halls",
        8,  // Random Space
        0,  // 1970s
        0.2343f,  // size
        0.4470f,  // damping
        2.3f,  // predelay ms
        0.30f,  // mix
        0.79f,  // modRate Hz
        0.2565f,  // modDepth
        0.8578f,  // width
        0.5591f,  // earlyDiff
        0.5020f,  // lateDiff
        1.18f,  // bassMult
        406.4f,  // bassFreq Hz
        20.3f,  // lowCut Hz
        19945.1f,  // highCut Hz
        false,
        0.6095f,  // roomSize
        0.6000f,  // earlyLateBal
        1.00f  // highDecay
    });

    // Quick solution, well rounded reverb (match: 79%)
    presets.push_back({
        "Good Ol' Verb",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.2343f,  // size
        0.3554f,  // damping
        12.9f,  // predelay ms
        0.30f,  // mix
        0.85f,  // modRate Hz
        0.2510f,  // modDepth
        0.8778f,  // width
        0.5547f,  // earlyDiff
        0.5478f,  // lateDiff
        1.22f,  // bassMult
        413.6f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        19669.6f,  // highCut Hz
        false,
        0.6097f,  // roomSize
        0.6000f,  // earlyLateBal
        0.79f  // highDecay
    });

    // Large, filtered, medium-bright hall of stone (match: 73%)
    presets.push_back({
        "Gothic Hall",
        "Halls",
        4,  // Cathedral
        0,  // 1970s
        0.3008f,  // size
        0.0000f,  // damping
        63.9f,  // predelay ms
        0.30f,  // mix
        0.79f,  // modRate Hz
        0.2491f,  // modDepth
        0.8588f,  // width
        0.5578f,  // earlyDiff
        0.5338f,  // lateDiff
        1.28f,  // bassMult
        397.4f,  // bassFreq Hz
        20.3f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6202f,  // roomSize
        0.6000f,  // earlyLateBal
        0.95f  // highDecay
    });

    // Great hall reverb, works with all material (match: 91%)
    presets.push_back({
        "Great Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.0476f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.79f,  // modRate Hz
        0.2515f,  // modDepth
        0.8850f,  // width
        0.5296f,  // earlyDiff
        0.5603f,  // lateDiff
        0.98f,  // bassMult
        395.7f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.7556f,  // roomSize
        0.4000f,  // earlyLateBal
        1.02f  // highDecay
    });

    // Medium-sized room, 2-second reverb (match: 77%)
    presets.push_back({
        "Guitar Ballad",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.2881f,  // size
        0.1546f,  // damping
        2.3f,  // predelay ms
        0.30f,  // mix
        0.80f,  // modRate Hz
        0.2355f,  // modDepth
        0.8896f,  // width
        0.5681f,  // earlyDiff
        0.5099f,  // lateDiff
        0.95f,  // bassMult
        379.7f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6461f,  // roomSize
        0.6000f,  // earlyLateBal
        0.95f  // highDecay
    });

    // Long predelay with recirculating echoes (match: 64%)
    presets.push_back({
        "Guitar Cave",
        "Halls",
        1,  // Room
        0,  // 1970s
        0.5068f,  // size
        0.5639f,  // damping
        100.0f,  // predelay ms
        0.30f,  // mix
        0.82f,  // modRate Hz
        0.2542f,  // modDepth
        0.8305f,  // width
        0.5448f,  // earlyDiff
        0.5206f,  // lateDiff
        0.97f,  // bassMult
        399.1f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6350f,  // roomSize
        0.6000f,  // earlyLateBal
        0.79f  // highDecay
    });

    // Very large space, ideal for horns (match: 88%)
    presets.push_back({
        "Horns Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.1521f,  // damping
        3.7f,  // predelay ms
        0.30f,  // mix
        0.78f,  // modRate Hz
        0.2597f,  // modDepth
        0.9329f,  // width
        0.5389f,  // earlyDiff
        0.4336f,  // lateDiff
        1.42f,  // bassMult
        372.4f,  // bassFreq Hz
        54.4f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6283f,  // roomSize
        0.6000f,  // earlyLateBal
        1.17f  // highDecay
    });

    // Large hall with stage reflections (match: 89%)
    presets.push_back({
        "Large Hall+Stage",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.3532f,  // damping
        17.1f,  // predelay ms
        0.30f,  // mix
        0.79f,  // modRate Hz
        0.2624f,  // modDepth
        0.9069f,  // width
        0.5294f,  // earlyDiff
        0.5337f,  // lateDiff
        1.17f,  // bassMult
        280.4f,  // bassFreq Hz
        21.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5306f,  // roomSize
        0.5000f,  // earlyLateBal
        1.03f  // highDecay
    });

    // Split with empty and full hall (match: 70%)
    presets.push_back({
        "Lecture Halls",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.5522f,  // damping
        58.6f,  // predelay ms
        0.30f,  // mix
        0.77f,  // modRate Hz
        0.2562f,  // modDepth
        0.8661f,  // width
        0.5282f,  // earlyDiff
        0.5553f,  // lateDiff
        1.00f,  // bassMult
        395.5f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        17155.8f,  // highCut Hz
        false,
        0.5812f,  // roomSize
        0.6000f,  // earlyLateBal
        1.14f  // highDecay
    });

    // Very large hall, moderate decay (match: 83%)
    presets.push_back({
        "Live Arena",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.4425f,  // damping
        3.6f,  // predelay ms
        0.30f,  // mix
        0.81f,  // modRate Hz
        0.2511f,  // modDepth
        0.9069f,  // width
        0.5399f,  // earlyDiff
        0.5426f,  // lateDiff
        0.88f,  // bassMult
        385.5f,  // bassFreq Hz
        20.6f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5927f,  // roomSize
        0.6000f,  // earlyLateBal
        0.82f  // highDecay
    });

    // Liveness controls let you design your room (match: 83%)
    presets.push_back({
        "Make-A-Space",
        "Halls",
        1,  // Room
        0,  // 1970s
        0.4573f,  // size
        0.2031f,  // damping
        2.6f,  // predelay ms
        0.30f,  // mix
        0.80f,  // modRate Hz
        0.2500f,  // modDepth
        0.9414f,  // width
        0.5668f,  // earlyDiff
        0.4855f,  // lateDiff
        1.01f,  // bassMult
        404.2f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6408f,  // roomSize
        0.6000f,  // earlyLateBal
        0.99f  // highDecay
    });

    // Medium hall with stage reflections (match: 88%)
    presets.push_back({
        "Med Hall+Stage",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.0752f,  // size
        0.1462f,  // damping
        11.4f,  // predelay ms
        0.30f,  // mix
        0.80f,  // modRate Hz
        0.2465f,  // modDepth
        0.8689f,  // width
        0.5819f,  // earlyDiff
        0.5529f,  // lateDiff
        1.27f,  // bassMult
        391.3f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        19611.5f,  // highCut Hz
        false,
        0.5648f,  // roomSize
        0.6000f,  // earlyLateBal
        0.86f  // highDecay
    });

    // Natural medium-size hall (match: 91%)
    presets.push_back({
        "Medium Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.0512f,  // damping
        3.5f,  // predelay ms
        0.30f,  // mix
        0.81f,  // modRate Hz
        0.2447f,  // modDepth
        0.9121f,  // width
        0.5760f,  // earlyDiff
        0.5358f,  // lateDiff
        1.40f,  // bassMult
        407.2f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6112f,  // roomSize
        0.6000f,  // earlyLateBal
        0.94f  // highDecay
    });

    // Short, boomny, and bright, like anechoic chamber (match: 68%)
    presets.push_back({
        "Metal Chamber",
        "Halls",
        3,  // Chamber
        0,  // 1970s
        0.3008f,  // size
        0.3556f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.82f,  // modRate Hz
        0.2560f,  // modDepth
        0.9097f,  // width
        0.5601f,  // earlyDiff
        0.4796f,  // lateDiff
        1.21f,  // bassMult
        392.0f,  // bassFreq Hz
        20.8f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6175f,  // roomSize
        0.6000f,  // earlyLateBal
        0.94f  // highDecay
    });

    // Reverberant hall like a large room in a museum (match: 85%)
    presets.push_back({
        "Museum Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.5335f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.82f,  // modRate Hz
        0.2576f,  // modDepth
        0.7275f,  // width
        0.5571f,  // earlyDiff
        0.5480f,  // lateDiff
        1.15f,  // bassMult
        411.6f,  // bassFreq Hz
        20.3f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5794f,  // roomSize
        0.5000f,  // earlyLateBal
        1.07f  // highDecay
    });

    // Acoustics of two famous NYC nightclubs (match: 82%)
    presets.push_back({
        "NYC Clubs",
        "Halls",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.1517f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.77f,  // modRate Hz
        0.2522f,  // modDepth
        0.8900f,  // width
        0.5656f,  // earlyDiff
        0.5383f,  // lateDiff
        0.97f,  // bassMult
        407.0f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6122f,  // roomSize
        0.6000f,  // earlyLateBal
        1.25f  // highDecay
    });

    // Dense, medium long, nonlinear gated verb (match: 44%)
    presets.push_back({
        "NonLinear #1",
        "Halls",
        9,  // Dirty Hall
        0,  // 1970s
        0.0000f,  // size
        0.6691f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.83f,  // modRate Hz
        0.2582f,  // modDepth
        0.9088f,  // width
        0.5134f,  // earlyDiff
        0.5450f,  // lateDiff
        0.72f,  // bassMult
        372.7f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        19414.3f,  // highCut Hz
        false,
        0.5709f,  // roomSize
        0.6000f,  // earlyLateBal
        0.81f  // highDecay
    });

    // Large nonlinear reverb, like gated warehouse (match: 81%)
    presets.push_back({
        "Nonlin Warehouse",
        "Halls",
        9,  // Dirty Hall
        0,  // 1970s
        0.0000f,  // size
        0.2023f,  // damping
        0.6f,  // predelay ms
        0.30f,  // mix
        0.79f,  // modRate Hz
        0.2450f,  // modDepth
        0.9061f,  // width
        0.5952f,  // earlyDiff
        0.5599f,  // lateDiff
        1.22f,  // bassMult
        390.3f,  // bassFreq Hz
        65.3f,  // lowCut Hz
        19551.1f,  // highCut Hz
        false,
        0.5734f,  // roomSize
        0.6000f,  // earlyLateBal
        1.17f  // highDecay
    });

    // LFO patched to OutWidth, subtle sweeping (match: 21%)
    presets.push_back({
        "Pan Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.0000f,  // size
        0.6500f,  // damping
        89.0f,  // predelay ms
        0.30f,  // mix
        0.80f,  // modRate Hz
        0.2500f,  // modDepth
        0.8000f,  // width
        0.5551f,  // earlyDiff
        0.5500f,  // lateDiff
        1.04f,  // bassMult
        400.0f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6000f,  // roomSize
        0.6000f,  // earlyLateBal
        0.80f  // highDecay
    });

    // Strange, semi-gated reverb with pumping (match: 85%)
    presets.push_back({
        "Pump Verb",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.3008f,  // size
        0.1527f,  // damping
        57.6f,  // predelay ms
        0.30f,  // mix
        0.81f,  // modRate Hz
        0.2516f,  // modDepth
        0.7211f,  // width
        0.5559f,  // earlyDiff
        0.5151f,  // lateDiff
        0.99f,  // bassMult
        399.2f,  // bassFreq Hz
        20.4f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6606f,  // roomSize
        0.6000f,  // earlyLateBal
        1.02f  // highDecay
    });

    // Small, bright sounding hall (match: 81%)
    presets.push_back({
        "Real Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.2343f,  // size
        0.4520f,  // damping
        0.3f,  // predelay ms
        0.30f,  // mix
        0.80f,  // modRate Hz
        0.2466f,  // modDepth
        0.8369f,  // width
        0.5479f,  // earlyDiff
        0.5477f,  // lateDiff
        0.99f,  // bassMult
        401.0f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6192f,  // roomSize
        0.6000f,  // earlyLateBal
        1.32f  // highDecay
    });

    // Long ER rise, short decay (match: 86%)
    presets.push_back({
        "Rise'n Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.0752f,  // size
        0.0482f,  // damping
        2.5f,  // predelay ms
        0.30f,  // mix
        0.75f,  // modRate Hz
        0.2456f,  // modDepth
        0.9514f,  // width
        0.5302f,  // earlyDiff
        0.5532f,  // lateDiff
        1.60f,  // bassMult
        395.7f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        19375.2f,  // highCut Hz
        false,
        0.6064f,  // roomSize
        0.6000f,  // earlyLateBal
        1.41f  // highDecay
    });

    // Airplane hangar for spacious sax (match: 77%)
    presets.push_back({
        "Saxy Hangar",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.3008f,  // size
        0.4551f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.80f,  // modRate Hz
        0.2505f,  // modDepth
        0.7952f,  // width
        0.5618f,  // earlyDiff
        0.5380f,  // lateDiff
        0.84f,  // bassMult
        479.8f,  // bassFreq Hz
        20.3f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6019f,  // roomSize
        0.6000f,  // earlyLateBal
        1.02f  // highDecay
    });

    // Short reverse reverb, quick build up (match: 25%)
    presets.push_back({
        "Short Reverse",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.0000f,  // size
        0.6452f,  // damping
        9.4f,  // predelay ms
        0.30f,  // mix
        0.76f,  // modRate Hz
        0.2465f,  // modDepth
        0.9259f,  // width
        0.5608f,  // earlyDiff
        0.5548f,  // lateDiff
        0.99f,  // bassMult
        409.1f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5796f,  // roomSize
        0.6000f,  // earlyLateBal
        0.83f  // highDecay
    });

    // Bright, close hall, medium short decay, live quality (match: 73%)
    presets.push_back({
        "Sizzle Hall",
        "Halls",
        6,  // Bright Hall
        0,  // 1970s
        0.3008f,  // size
        0.5762f,  // damping
        23.4f,  // predelay ms
        0.30f,  // mix
        0.80f,  // modRate Hz
        0.2490f,  // modDepth
        1.0000f,  // width
        0.5487f,  // earlyDiff
        0.5001f,  // lateDiff
        0.97f,  // bassMult
        397.4f,  // bassFreq Hz
        20.6f,  // lowCut Hz
        19527.3f,  // highCut Hz
        false,
        0.6322f,  // roomSize
        0.6000f,  // earlyLateBal
        0.98f  // highDecay
    });

    // Slap initial double tap, dark (match: 86%)
    presets.push_back({
        "Slap Hall",
        "Halls",
        1,  // Room
        0,  // 1970s
        0.3008f,  // size
        0.5450f,  // damping
        16.3f,  // predelay ms
        0.30f,  // mix
        0.82f,  // modRate Hz
        0.2544f,  // modDepth
        0.8981f,  // width
        0.5690f,  // earlyDiff
        0.4662f,  // lateDiff
        1.09f,  // bassMult
        401.6f,  // bassFreq Hz
        20.4f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.7466f,  // roomSize
        0.7000f,  // earlyLateBal
        1.01f  // highDecay
    });

    // Small hall, no reflections, short decay (match: 92%)
    presets.push_back({
        "Small Church",
        "Halls",
        4,  // Cathedral
        0,  // 1970s
        0.3008f,  // size
        0.6343f,  // damping
        0.8f,  // predelay ms
        0.30f,  // mix
        0.80f,  // modRate Hz
        0.2530f,  // modDepth
        0.9152f,  // width
        0.5579f,  // earlyDiff
        0.5222f,  // lateDiff
        0.90f,  // bassMult
        386.3f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6479f,  // roomSize
        0.6000f,  // earlyLateBal
        0.98f  // highDecay
    });

    // Small hall, bright initial reverb (match: 93%)
    presets.push_back({
        "Small Hall",
        "Halls",
        3,  // Chamber
        0,  // 1970s
        0.1733f,  // size
        0.0510f,  // damping
        4.1f,  // predelay ms
        0.30f,  // mix
        1.19f,  // modRate Hz
        0.2299f,  // modDepth
        1.0000f,  // width
        0.5501f,  // earlyDiff
        0.4981f,  // lateDiff
        1.25f,  // bassMult
        397.7f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6135f,  // roomSize
        0.6000f,  // earlyLateBal
        0.97f  // highDecay
    });

    // Small hall with stage reflections (match: 75%)
    presets.push_back({
        "Small Hall+Stage",
        "Halls",
        3,  // Chamber
        0,  // 1970s
        0.1733f,  // size
        0.4667f,  // damping
        3.4f,  // predelay ms
        0.30f,  // mix
        1.05f,  // modRate Hz
        0.3877f,  // modDepth
        0.9809f,  // width
        0.5503f,  // earlyDiff
        0.4883f,  // lateDiff
        0.88f,  // bassMult
        384.1f,  // bassFreq Hz
        20.4f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6239f,  // roomSize
        0.6000f,  // earlyLateBal
        0.77f  // highDecay
    });

    // Tight, gated hall for snares (match: 67%)
    presets.push_back({
        "Snare Gate",
        "Halls",
        1,  // Room
        0,  // 1970s
        0.6081f,  // size
        0.3492f,  // damping
        1.7f,  // predelay ms
        0.30f,  // mix
        0.81f,  // modRate Hz
        0.2543f,  // modDepth
        0.8393f,  // width
        0.5574f,  // earlyDiff
        0.5092f,  // lateDiff
        1.59f,  // bassMult
        400.0f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6173f,  // roomSize
        0.6000f,  // earlyLateBal
        0.99f  // highDecay
    });

    // Strange hall with LFO controlling spatial EQ (match: 82%)
    presets.push_back({
        "Spatial Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.3439f,  // damping
        0.9f,  // predelay ms
        0.30f,  // mix
        0.81f,  // modRate Hz
        0.2493f,  // modDepth
        0.9221f,  // width
        0.3895f,  // earlyDiff
        0.5270f,  // lateDiff
        1.13f,  // bassMult
        407.1f,  // bassFreq Hz
        20.7f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5972f,  // roomSize
        0.6000f,  // earlyLateBal
        1.05f  // highDecay
    });

    // Split reverb with locker room and arena (match: 80%)
    presets.push_back({
        "Sports Verbs",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.3605f,  // size
        0.1528f,  // damping
        7.2f,  // predelay ms
        0.30f,  // mix
        0.41f,  // modRate Hz
        0.2523f,  // modDepth
        0.8650f,  // width
        0.5613f,  // earlyDiff
        0.5105f,  // lateDiff
        0.97f,  // bassMult
        311.1f,  // bassFreq Hz
        20.4f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.7155f,  // roomSize
        0.6000f,  // earlyLateBal
        0.79f  // highDecay
    });

    // Short decay of single room, large reflections (match: 91%)
    presets.push_back({
        "Stairwell",
        "Halls",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.1501f,  // damping
        3.1f,  // predelay ms
        0.30f,  // mix
        0.28f,  // modRate Hz
        0.2588f,  // modDepth
        0.9760f,  // width
        0.5814f,  // earlyDiff
        0.5016f,  // lateDiff
        1.11f,  // bassMult
        396.6f,  // bassFreq Hz
        39.6f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6038f,  // roomSize
        0.6000f,  // earlyLateBal
        0.98f  // highDecay
    });

    // Chorused hall, long decay for synths (match: 81%)
    presets.push_back({
        "Synth Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.2881f,  // size
        0.5606f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.80f,  // modRate Hz
        0.2557f,  // modDepth
        0.8879f,  // width
        0.5559f,  // earlyDiff
        0.5208f,  // lateDiff
        1.10f,  // bassMult
        386.4f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6249f,  // roomSize
        0.6000f,  // earlyLateBal
        1.01f  // highDecay
    });

    // Very reflective sound, like pounding a brick wall (match: 63%)
    presets.push_back({
        "Tap Brick",
        "Halls",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.4763f,  // damping
        2.4f,  // predelay ms
        0.30f,  // mix
        0.13f,  // modRate Hz
        0.2570f,  // modDepth
        0.9145f,  // width
        0.5467f,  // earlyDiff
        0.5240f,  // lateDiff
        0.98f,  // bassMult
        381.6f,  // bassFreq Hz
        20.4f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6044f,  // roomSize
        0.6000f,  // earlyLateBal
        0.79f  // highDecay
    });

    // Strange hall with LFO controlling reverb HF cut (match: 90%)
    presets.push_back({
        "Tidal Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.4352f,  // damping
        25.6f,  // predelay ms
        0.30f,  // mix
        0.79f,  // modRate Hz
        0.2555f,  // modDepth
        0.8017f,  // width
        0.5455f,  // earlyDiff
        0.5673f,  // lateDiff
        0.92f,  // bassMult
        397.6f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        19946.7f,  // highCut Hz
        false,
        0.5895f,  // roomSize
        0.6000f,  // earlyLateBal
        1.10f  // highDecay
    });

    // Large hall, very little HF content (match: 78%)
    presets.push_back({
        "Utility Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.0000f,  // size
        0.4340f,  // damping
        3.7f,  // predelay ms
        0.30f,  // mix
        0.79f,  // modRate Hz
        0.2506f,  // modDepth
        0.8975f,  // width
        0.5270f,  // earlyDiff
        0.5777f,  // lateDiff
        1.17f,  // bassMult
        386.0f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6110f,  // roomSize
        0.6000f,  // earlyLateBal
        1.32f  // highDecay
    });

    // General, all purpose reverb (match: 77%)
    presets.push_back({
        "Utility Verb",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.3605f,  // size
        0.3539f,  // damping
        24.1f,  // predelay ms
        0.30f,  // mix
        1.13f,  // modRate Hz
        0.2496f,  // modDepth
        0.8961f,  // width
        0.5423f,  // earlyDiff
        0.4932f,  // lateDiff
        0.89f,  // bassMult
        403.6f,  // bassFreq Hz
        20.6f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6319f,  // roomSize
        0.6000f,  // earlyLateBal
        0.78f  // highDecay
    });

    // Enormous, silky reflective room (match: 78%)
    presets.push_back({
        "Vocal Concert",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.0000f,  // size
        0.6256f,  // damping
        8.0f,  // predelay ms
        0.30f,  // mix
        0.81f,  // modRate Hz
        0.2500f,  // modDepth
        0.8774f,  // width
        0.5646f,  // earlyDiff
        0.5508f,  // lateDiff
        0.99f,  // bassMult
        413.1f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.3716f,  // roomSize
        0.6000f,  // earlyLateBal
        0.85f  // highDecay
    });

    // Medium-sized hall, short clear decay (match: 82%)
    presets.push_back({
        "Vocal Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.0000f,  // size
        0.5481f,  // damping
        2.9f,  // predelay ms
        0.30f,  // mix
        0.79f,  // modRate Hz
        0.3693f,  // modDepth
        0.9956f,  // width
        0.5604f,  // earlyDiff
        0.5461f,  // lateDiff
        1.34f,  // bassMult
        417.2f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        19962.8f,  // highCut Hz
        false,
        0.5924f,  // roomSize
        0.6000f,  // earlyLateBal
        0.81f  // highDecay
    });

    // Fairly large hall, generous reverb decay (match: 91%)
    presets.push_back({
        "Vocal Hall 2",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.1498f,  // damping
        20.3f,  // predelay ms
        0.30f,  // mix
        0.68f,  // modRate Hz
        0.2514f,  // modDepth
        0.9402f,  // width
        0.5152f,  // earlyDiff
        0.5220f,  // lateDiff
        1.14f,  // bassMult
        393.0f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        19778.1f,  // highCut Hz
        false,
        0.5934f,  // roomSize
        0.6000f,  // earlyLateBal
        1.05f  // highDecay
    });

    // Lovely reverb with short decay (match: 89%)
    presets.push_back({
        "Vocal Magic",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.0486f,  // damping
        1.1f,  // predelay ms
        0.30f,  // mix
        0.81f,  // modRate Hz
        0.2592f,  // modDepth
        0.9230f,  // width
        0.5473f,  // earlyDiff
        0.5312f,  // lateDiff
        1.33f,  // bassMult
        502.5f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        19026.0f,  // highCut Hz
        false,
        0.5486f,  // roomSize
        0.6000f,  // earlyLateBal
        0.93f  // highDecay
    });

    // Close delays double the source, wide (match: 65%)
    presets.push_back({
        "Wide Vox",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.0499f,  // damping
        1.0f,  // predelay ms
        0.30f,  // mix
        0.81f,  // modRate Hz
        0.2544f,  // modDepth
        0.6614f,  // width
        0.5459f,  // earlyDiff
        0.5343f,  // lateDiff
        1.24f,  // bassMult
        389.8f,  // bassFreq Hz
        20.8f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5879f,  // roomSize
        0.5000f,  // earlyLateBal
        0.86f  // highDecay
    });


    // ==================== ROOMS (60) ====================

    // Bit of dry delay, sweet for vocals/instruments (match: 78%)
    presets.push_back({
        "Ambient Sustain",
        "Rooms",
        5,  // Ambience
        0,  // 1970s
        0.1733f,  // size
        0.2528f,  // damping
        44.8f,  // predelay ms
        0.22f,  // mix
        0.80f,  // modRate Hz
        0.2508f,  // modDepth
        0.7812f,  // width
        0.5770f,  // earlyDiff
        0.5242f,  // lateDiff
        0.96f,  // bassMult
        402.3f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6133f,  // roomSize
        0.6000f,  // earlyLateBal
        0.98f  // highDecay
    });

    // Small bedroom with furniture and heavy curtains (match: 62%)
    presets.push_back({
        "Bedroom",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0000f,  // size
        0.6148f,  // damping
        1.4f,  // predelay ms
        0.22f,  // mix
        0.83f,  // modRate Hz
        0.2566f,  // modDepth
        0.8544f,  // width
        0.5225f,  // earlyDiff
        0.5579f,  // lateDiff
        0.84f,  // bassMult
        395.3f,  // bassFreq Hz
        20.4f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6453f,  // roomSize
        0.5000f,  // earlyLateBal
        0.66f  // highDecay
    });

    // Perfect for dreamy soundscapes, atmospheric (match: 45%)
    presets.push_back({
        "BeeBee Slapz",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.0521f,  // damping
        51.1f,  // predelay ms
        0.22f,  // mix
        1.11f,  // modRate Hz
        0.2589f,  // modDepth
        1.0000f,  // width
        0.5679f,  // earlyDiff
        0.4890f,  // lateDiff
        1.46f,  // bassMult
        384.4f,  // bassFreq Hz
        69.7f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5900f,  // roomSize
        0.6000f,  // earlyLateBal
        0.93f  // highDecay
    });

    // Saturated bottom-heavy, dense reverb (match: 65%)
    presets.push_back({
        "Big Boom Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.5567f,  // damping
        3.7f,  // predelay ms
        0.22f,  // mix
        0.81f,  // modRate Hz
        0.2567f,  // modDepth
        0.8388f,  // width
        0.5524f,  // earlyDiff
        0.4186f,  // lateDiff
        1.11f,  // bassMult
        286.9f,  // bassFreq Hz
        20.5f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.7364f,  // roomSize
        0.6000f,  // earlyLateBal
        0.95f  // highDecay
    });

    // Bit of predelay separates bright reverb from source (match: 82%)
    presets.push_back({
        "Bright Vocal",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.1558f,  // damping
        19.6f,  // predelay ms
        0.22f,  // mix
        0.81f,  // modRate Hz
        0.2513f,  // modDepth
        0.7717f,  // width
        0.5560f,  // earlyDiff
        0.5396f,  // lateDiff
        0.96f,  // bassMult
        398.3f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6103f,  // roomSize
        0.6000f,  // earlyLateBal
        0.96f  // highDecay
    });

    // Sounds like snowed in too long (match: 62%)
    presets.push_back({
        "Cabin Fever",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.3008f,  // size
        0.4564f,  // damping
        0.3f,  // predelay ms
        0.22f,  // mix
        0.81f,  // modRate Hz
        0.1236f,  // modDepth
        0.8199f,  // width
        0.5566f,  // earlyDiff
        0.5239f,  // lateDiff
        1.25f,  // bassMult
        407.7f,  // bassFreq Hz
        20.3f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6074f,  // roomSize
        0.6000f,  // earlyLateBal
        0.58f  // highDecay
    });

    // Tight small space, open or closed casket (match: 41%)
    presets.push_back({
        "Coffin",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0000f,  // size
        0.6819f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        0.80f,  // modRate Hz
        0.2603f,  // modDepth
        0.8625f,  // width
        0.5573f,  // earlyDiff
        0.5117f,  // lateDiff
        0.85f,  // bassMult
        384.1f,  // bassFreq Hz
        20.4f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6153f,  // roomSize
        0.6000f,  // earlyLateBal
        0.74f  // highDecay
    });

    // Live sound with less dominating, punchier sound (match: 67%)
    presets.push_back({
        "Delay Space",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.2343f,  // size
        0.2515f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        0.80f,  // modRate Hz
        0.2537f,  // modDepth
        0.8235f,  // width
        0.5553f,  // earlyDiff
        0.5231f,  // lateDiff
        1.59f,  // bassMult
        402.7f,  // bassFreq Hz
        20.4f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6272f,  // roomSize
        0.6000f,  // earlyLateBal
        1.29f  // highDecay
    });

    // Dark preset, dense saturated, for whole drum kit (match: 80%)
    presets.push_back({
        "Drum Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.0530f,  // damping
        0.1f,  // predelay ms
        0.22f,  // mix
        0.79f,  // modRate Hz
        0.2585f,  // modDepth
        0.8856f,  // width
        0.5512f,  // earlyDiff
        0.5370f,  // lateDiff
        0.94f,  // bassMult
        267.7f,  // bassFreq Hz
        70.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5926f,  // roomSize
        0.6000f,  // earlyLateBal
        0.91f  // highDecay
    });

    // Split effect, empty and full closet (match: 74%)
    presets.push_back({
        "Dual Closets",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0000f,  // size
        0.4776f,  // damping
        0.3f,  // predelay ms
        0.22f,  // mix
        0.83f,  // modRate Hz
        0.2658f,  // modDepth
        0.8745f,  // width
        0.5225f,  // earlyDiff
        0.5278f,  // lateDiff
        1.09f,  // bassMult
        397.1f,  // bassFreq Hz
        58.5f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5937f,  // roomSize
        0.6000f,  // earlyLateBal
        0.68f  // highDecay
    });

    // Syncopated echo delay inside small kitchen (match: 53%)
    presets.push_back({
        "Echo Kitchen",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0000f,  // size
        0.4737f,  // damping
        1.0f,  // predelay ms
        0.22f,  // mix
        0.81f,  // modRate Hz
        0.2535f,  // modDepth
        0.9540f,  // width
        0.6542f,  // earlyDiff
        0.5330f,  // lateDiff
        1.42f,  // bassMult
        389.3f,  // bassFreq Hz
        23.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6068f,  // roomSize
        0.6000f,  // earlyLateBal
        0.78f  // highDecay
    });

    // Generic ambience, starting place (match: 72%)
    presets.push_back({
        "Gen. Ambience",
        "Rooms",
        5,  // Ambience
        0,  // 1970s
        0.0752f,  // size
        0.0964f,  // damping
        0.9f,  // predelay ms
        0.22f,  // mix
        0.84f,  // modRate Hz
        0.2540f,  // modDepth
        0.9096f,  // width
        0.6929f,  // earlyDiff
        0.5311f,  // lateDiff
        1.34f,  // bassMult
        389.8f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        19386.3f,  // highCut Hz
        false,
        0.6118f,  // roomSize
        0.6000f,  // earlyLateBal
        1.03f  // highDecay
    });

    // Warm smooth reverb of Real Room with more decay (match: 83%)
    presets.push_back({
        "Great Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.2343f,  // size
        0.2519f,  // damping
        4.9f,  // predelay ms
        0.22f,  // mix
        0.81f,  // modRate Hz
        0.2536f,  // modDepth
        0.8566f,  // width
        0.5557f,  // earlyDiff
        0.5295f,  // lateDiff
        1.00f,  // bassMult
        400.7f,  // bassFreq Hz
        20.3f,  // lowCut Hz
        19863.6f,  // highCut Hz
        false,
        0.5967f,  // roomSize
        0.6000f,  // earlyLateBal
        1.33f  // highDecay
    });

    // Tight and punchy ambience, combining small sizes (match: 60%)
    presets.push_back({
        "Guitar Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0000f,  // size
        0.7212f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        0.82f,  // modRate Hz
        0.2438f,  // modDepth
        0.8885f,  // width
        0.5081f,  // earlyDiff
        0.5402f,  // lateDiff
        0.87f,  // bassMult
        392.9f,  // bassFreq Hz
        32.9f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6247f,  // roomSize
        0.6000f,  // earlyLateBal
        0.71f  // highDecay
    });

    // Designed to sound like hardwood floor room (match: 65%)
    presets.push_back({
        "Hardwood Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.4700f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        1.09f,  // modRate Hz
        0.2569f,  // modDepth
        0.8055f,  // width
        0.5527f,  // earlyDiff
        0.4439f,  // lateDiff
        0.92f,  // bassMult
        395.5f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        17577.8f,  // highCut Hz
        false,
        0.5369f,  // roomSize
        0.6000f,  // earlyLateBal
        0.74f  // highDecay
    });

    // A dense concert hall (match: 62%)
    presets.push_back({
        "Hole Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.6933f,  // size
        0.5529f,  // damping
        41.3f,  // predelay ms
        0.22f,  // mix
        0.80f,  // modRate Hz
        0.2474f,  // modDepth
        0.8137f,  // width
        0.5606f,  // earlyDiff
        0.5042f,  // lateDiff
        0.98f,  // bassMult
        400.6f,  // bassFreq Hz
        20.4f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6189f,  // roomSize
        0.6000f,  // earlyLateBal
        0.80f  // highDecay
    });

    // Backwards effect, great as a special effect (match: 16%)
    presets.push_back({
        "Inverse Drums",
        "Rooms",
        2,  // Hall
        0,  // 1970s
        0.0000f,  // size
        0.6062f,  // damping
        84.0f,  // predelay ms
        0.22f,  // mix
        0.80f,  // modRate Hz
        0.2490f,  // modDepth
        0.9461f,  // width
        0.5612f,  // earlyDiff
        0.5483f,  // lateDiff
        1.10f,  // bassMult
        323.8f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        19771.2f,  // highCut Hz
        false,
        0.4349f,  // roomSize
        0.6000f,  // earlyLateBal
        0.85f  // highDecay
    });

    // Lots of options, backwards effect (match: 73%)
    presets.push_back({
        "Inverse Room 2",
        "Rooms",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.1486f,  // damping
        99.6f,  // predelay ms
        0.22f,  // mix
        0.78f,  // modRate Hz
        0.2451f,  // modDepth
        0.9282f,  // width
        0.5658f,  // earlyDiff
        0.5352f,  // lateDiff
        0.94f,  // bassMult
        410.8f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        19700.6f,  // highCut Hz
        false,
        0.6309f,  // roomSize
        0.6000f,  // earlyLateBal
        1.32f  // highDecay
    });

    // Smooth, large reverberant space using Shape and Spread (match: 91%)
    presets.push_back({
        "Large Chamber",
        "Rooms",
        3,  // Chamber
        0,  // 1970s
        0.4157f,  // size
        0.2445f,  // damping
        18.7f,  // predelay ms
        0.22f,  // mix
        0.83f,  // modRate Hz
        0.3429f,  // modDepth
        0.8498f,  // width
        0.4287f,  // earlyDiff
        0.5121f,  // lateDiff
        1.00f,  // bassMult
        394.9f,  // bassFreq Hz
        20.3f,  // lowCut Hz
        19619.6f,  // highCut Hz
        false,
        0.6315f,  // roomSize
        0.7000f,  // earlyLateBal
        1.01f  // highDecay
    });

    // Perfectly smooth listening room, high diffusion (match: 91%)
    presets.push_back({
        "Large Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.3605f,  // size
        0.0000f,  // damping
        2.5f,  // predelay ms
        0.22f,  // mix
        0.82f,  // modRate Hz
        0.2525f,  // modDepth
        0.9501f,  // width
        0.5749f,  // earlyDiff
        0.4873f,  // lateDiff
        1.02f,  // bassMult
        336.7f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        19882.5f,  // highCut Hz
        false,
        0.6467f,  // roomSize
        0.6000f,  // earlyLateBal
        0.76f  // highDecay
    });

    // Designed for live sound reinforcement (match: 79%)
    presets.push_back({
        "Large Space",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.1564f,  // size
        0.1537f,  // damping
        4.1f,  // predelay ms
        0.22f,  // mix
        0.81f,  // modRate Hz
        0.2535f,  // modDepth
        0.8475f,  // width
        0.5576f,  // earlyDiff
        0.5349f,  // lateDiff
        0.96f,  // bassMult
        410.3f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6022f,  // roomSize
        0.6000f,  // earlyLateBal
        0.99f  // highDecay
    });

    // More spacious version of S Vocal Amb (match: 82%)
    presets.push_back({
        "Lg Vocal Amb",
        "Rooms",
        5,  // Ambience
        0,  // 1970s
        0.0752f,  // size
        0.1456f,  // damping
        15.5f,  // predelay ms
        0.22f,  // mix
        1.10f,  // modRate Hz
        0.2571f,  // modDepth
        0.9286f,  // width
        0.5780f,  // earlyDiff
        0.5255f,  // lateDiff
        1.00f,  // bassMult
        403.0f,  // bassFreq Hz
        20.4f,  // lowCut Hz
        18853.3f,  // highCut Hz
        false,
        0.5850f,  // roomSize
        0.6000f,  // earlyLateBal
        1.27f  // highDecay
    });

    // More spacious version of S Vocal Space (match: 72%)
    presets.push_back({
        "Lg Vocal Space",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.2343f,  // size
        0.0512f,  // damping
        54.0f,  // predelay ms
        0.22f,  // mix
        0.81f,  // modRate Hz
        0.2545f,  // modDepth
        0.8250f,  // width
        0.5484f,  // earlyDiff
        0.5093f,  // lateDiff
        1.53f,  // bassMult
        405.3f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6137f,  // roomSize
        0.6000f,  // earlyLateBal
        1.34f  // highDecay
    });

    // Soft room with short RT, some stereo width (match: 43%)
    presets.push_back({
        "Living Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0000f,  // size
        0.4606f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        0.80f,  // modRate Hz
        0.2501f,  // modDepth
        0.7997f,  // width
        0.5502f,  // earlyDiff
        0.5501f,  // lateDiff
        1.00f,  // bassMult
        399.2f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6151f,  // roomSize
        0.6000f,  // earlyLateBal
        1.00f  // highDecay
    });

    // Ambience of a locker room (match: 82%)
    presets.push_back({
        "Locker Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.5700f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        0.81f,  // modRate Hz
        0.2600f,  // modDepth
        0.8896f,  // width
        0.5715f,  // earlyDiff
        0.4917f,  // lateDiff
        0.87f,  // bassMult
        388.3f,  // bassFreq Hz
        20.6f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5990f,  // roomSize
        0.6000f,  // earlyLateBal
        0.95f  // highDecay
    });

    // Smaller version of Large Room (match: 89%)
    presets.push_back({
        "Medium Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.0520f,  // damping
        2.9f,  // predelay ms
        0.22f,  // mix
        0.83f,  // modRate Hz
        0.1693f,  // modDepth
        0.9574f,  // width
        0.5348f,  // earlyDiff
        0.5065f,  // lateDiff
        1.48f,  // bassMult
        336.4f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6062f,  // roomSize
        0.6000f,  // earlyLateBal
        0.99f  // highDecay
    });

    // Small, intimate setting, smooth reverb (match: 67%)
    presets.push_back({
        "Medium Space",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.3671f,  // damping
        1.2f,  // predelay ms
        0.22f,  // mix
        0.83f,  // modRate Hz
        0.2617f,  // modDepth
        0.8796f,  // width
        0.5399f,  // earlyDiff
        0.5145f,  // lateDiff
        1.09f,  // bassMult
        413.2f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6108f,  // roomSize
        0.6000f,  // earlyLateBal
        0.89f  // highDecay
    });

    // Hotel-like meeting room (match: 49%)
    presets.push_back({
        "Meeting Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.5769f,  // damping
        0.2f,  // predelay ms
        0.22f,  // mix
        0.82f,  // modRate Hz
        0.2850f,  // modDepth
        0.9285f,  // width
        0.5689f,  // earlyDiff
        0.4864f,  // lateDiff
        1.15f,  // bassMult
        398.7f,  // bassFreq Hz
        75.5f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6105f,  // roomSize
        0.6000f,  // earlyLateBal
        0.94f  // highDecay
    });

    // Resonant drum preset, very small Size/Mid RT (match: 56%)
    presets.push_back({
        "Metallic Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.5526f,  // damping
        9.3f,  // predelay ms
        0.22f,  // mix
        0.84f,  // modRate Hz
        0.2605f,  // modDepth
        0.8036f,  // width
        0.6009f,  // earlyDiff
        0.4950f,  // lateDiff
        1.37f,  // bassMult
        323.1f,  // bassFreq Hz
        20.4f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6665f,  // roomSize
        0.6000f,  // earlyLateBal
        0.84f  // highDecay
    });

    // Chamber/Room for organ and other keyboards (match: 80%)
    presets.push_back({
        "Organ Room",
        "Rooms",
        3,  // Chamber
        0,  // 1970s
        0.1733f,  // size
        0.1567f,  // damping
        19.5f,  // predelay ms
        0.22f,  // mix
        0.79f,  // modRate Hz
        0.2630f,  // modDepth
        0.8862f,  // width
        0.5418f,  // earlyDiff
        0.4842f,  // lateDiff
        1.51f,  // bassMult
        394.3f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6214f,  // roomSize
        0.6000f,  // earlyLateBal
        1.25f  // highDecay
    });

    // Takes you back to the good old days (match: 80%)
    presets.push_back({
        "PCM 60 Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.3008f,  // size
        0.3610f,  // damping
        0.4f,  // predelay ms
        0.22f,  // mix
        0.81f,  // modRate Hz
        0.1373f,  // modDepth
        0.8661f,  // width
        0.5668f,  // earlyDiff
        0.4565f,  // lateDiff
        1.22f,  // bassMult
        257.0f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        19995.3f,  // highCut Hz
        false,
        0.6249f,  // roomSize
        0.6000f,  // earlyLateBal
        0.83f  // highDecay
    });

    // Full and resonant reverb, accentuates transients (match: 78%)
    presets.push_back({
        "Percussion Place",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.2343f,  // size
        0.2530f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        0.84f,  // modRate Hz
        0.1747f,  // modDepth
        0.8422f,  // width
        0.5659f,  // earlyDiff
        0.4996f,  // lateDiff
        1.22f,  // bassMult
        352.8f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6094f,  // roomSize
        0.6000f,  // earlyLateBal
        0.85f  // highDecay
    });

    // Similar to PercussPlace, slightly smaller (match: 74%)
    presets.push_back({
        "Percussion Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.4990f,  // damping
        1.1f,  // predelay ms
        0.22f,  // mix
        0.96f,  // modRate Hz
        0.2584f,  // modDepth
        0.8124f,  // width
        0.5608f,  // earlyDiff
        0.5026f,  // lateDiff
        0.90f,  // bassMult
        389.1f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6128f,  // roomSize
        0.6000f,  // earlyLateBal
        0.78f  // highDecay
    });

    // How much sound can you squeeze into a phone booth? (match: 49%)
    presets.push_back({
        "Phone Booth",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0000f,  // size
        0.6770f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        0.83f,  // modRate Hz
        0.2477f,  // modDepth
        0.9659f,  // width
        0.5021f,  // earlyDiff
        0.5045f,  // lateDiff
        1.06f,  // bassMult
        373.1f,  // bassFreq Hz
        81.6f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6400f,  // roomSize
        0.6000f,  // earlyLateBal
        0.79f  // highDecay
    });

    // Natural reverb for a live setting (match: 77%)
    presets.push_back({
        "Real Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.4770f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        0.82f,  // modRate Hz
        0.2654f,  // modDepth
        0.8577f,  // width
        0.5541f,  // earlyDiff
        0.4887f,  // lateDiff
        0.84f,  // bassMult
        394.3f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5909f,  // roomSize
        0.6000f,  // earlyLateBal
        0.76f  // highDecay
    });

    // Super-saturated, atmospheric quality (match: 65%)
    presets.push_back({
        "Reflect Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.0497f,  // damping
        0.4f,  // predelay ms
        0.22f,  // mix
        0.85f,  // modRate Hz
        0.2688f,  // modDepth
        0.8339f,  // width
        0.5592f,  // earlyDiff
        0.5047f,  // lateDiff
        1.33f,  // bassMult
        385.6f,  // bassFreq Hz
        20.5f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6066f,  // roomSize
        0.7000f,  // earlyLateBal
        0.72f  // highDecay
    });

    // Extremely bright live drum sound (match: 68%)
    presets.push_back({
        "Rock Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.2881f,  // size
        0.3535f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        0.83f,  // modRate Hz
        0.2530f,  // modDepth
        0.8664f,  // width
        0.5686f,  // earlyDiff
        0.4674f,  // lateDiff
        0.97f,  // bassMult
        407.2f,  // bassFreq Hz
        20.5f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6196f,  // roomSize
        0.6000f,  // earlyLateBal
        0.77f  // highDecay
    });

    // All you could ever want for drums — punch, attitude (match: 79%)
    presets.push_back({
        "Room 4 Drums",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.4255f,  // damping
        0.1f,  // predelay ms
        0.22f,  // mix
        1.51f,  // modRate Hz
        0.2563f,  // modDepth
        0.8078f,  // width
        0.5542f,  // earlyDiff
        0.5244f,  // lateDiff
        0.76f,  // bassMult
        394.2f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6093f,  // roomSize
        0.6000f,  // earlyLateBal
        0.97f  // highDecay
    });

    // Dark and wet reverb, medium room long reverb tail (match: 62%)
    presets.push_back({
        "Slap Place",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.3556f,  // damping
        8.0f,  // predelay ms
        0.22f,  // mix
        0.82f,  // modRate Hz
        0.2479f,  // modDepth
        0.8260f,  // width
        0.5599f,  // earlyDiff
        0.5191f,  // lateDiff
        1.26f,  // bassMult
        398.9f,  // bassFreq Hz
        20.3f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6119f,  // roomSize
        0.6000f,  // earlyLateBal
        1.00f  // highDecay
    });

    // Unnatural room reverb, enhances any drum track (match: 66%)
    presets.push_back({
        "Sloppy Place",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.1518f,  // damping
        0.4f,  // predelay ms
        0.22f,  // mix
        0.79f,  // modRate Hz
        0.2529f,  // modDepth
        0.8873f,  // width
        0.5472f,  // earlyDiff
        0.5047f,  // lateDiff
        0.92f,  // bassMult
        425.2f,  // bassFreq Hz
        20.5f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6089f,  // roomSize
        0.6000f,  // earlyLateBal
        1.26f  // highDecay
    });

    // Spacious version of S Vocal Amb, set to Studio A (match: 23%)
    presets.push_back({
        "Sm Vocal Amb",
        "Rooms",
        5,  // Ambience
        0,  // 1970s
        0.0000f,  // size
        0.6346f,  // damping
        17.4f,  // predelay ms
        0.22f,  // mix
        0.79f,  // modRate Hz
        0.2415f,  // modDepth
        0.8805f,  // width
        0.5647f,  // earlyDiff
        0.5731f,  // lateDiff
        1.00f,  // bassMult
        397.2f,  // bassFreq Hz
        20.4f,  // lowCut Hz
        19873.8f,  // highCut Hz
        false,
        0.5988f,  // roomSize
        0.6000f,  // earlyLateBal
        0.80f  // highDecay
    });

    // Bigger version of S VocalSpace (match: 90%)
    presets.push_back({
        "Sm Vocal Space",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.0528f,  // damping
        26.3f,  // predelay ms
        0.22f,  // mix
        0.76f,  // modRate Hz
        0.2522f,  // modDepth
        0.7592f,  // width
        0.5387f,  // earlyDiff
        0.5232f,  // lateDiff
        1.20f,  // bassMult
        403.8f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6151f,  // roomSize
        0.6000f,  // earlyLateBal
        0.87f  // highDecay
    });

    // Similar to Large Chamber with tighter Mid RT/size (match: 91%)
    presets.push_back({
        "Small Chamber",
        "Rooms",
        3,  // Chamber
        0,  // 1970s
        0.1733f,  // size
        0.4716f,  // damping
        10.7f,  // predelay ms
        0.22f,  // mix
        0.83f,  // modRate Hz
        0.2569f,  // modDepth
        0.8608f,  // width
        0.5212f,  // earlyDiff
        0.5113f,  // lateDiff
        0.87f,  // bassMult
        402.1f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6219f,  // roomSize
        0.6000f,  // earlyLateBal
        0.77f  // highDecay
    });

    // Tight, smooth and natural sounding room (match: 90%)
    presets.push_back({
        "Small Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.3805f,  // damping
        0.9f,  // predelay ms
        0.22f,  // mix
        0.81f,  // modRate Hz
        0.1588f,  // modDepth
        0.8952f,  // width
        0.5595f,  // earlyDiff
        0.5058f,  // lateDiff
        0.94f,  // bassMult
        403.4f,  // bassFreq Hz
        32.4f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.4638f,  // roomSize
        0.6000f,  // earlyLateBal
        0.84f  // highDecay
    });

    // Large room, short Mid RT, Spatial EQ bass boost (match: 77%)
    presets.push_back({
        "Snare Trash",
        "Rooms",
        9,  // Dirty Hall
        0,  // 1970s
        0.0000f,  // size
        0.3977f,  // damping
        2.0f,  // predelay ms
        0.22f,  // mix
        0.51f,  // modRate Hz
        0.2568f,  // modDepth
        0.8763f,  // width
        0.5535f,  // earlyDiff
        0.5396f,  // lateDiff
        0.95f,  // bassMult
        500.2f,  // bassFreq Hz
        21.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5044f,  // roomSize
        0.7000f,  // earlyLateBal
        1.06f  // highDecay
    });

    // Spatial EQ bass boost enhances lower frequencies (match: 68%)
    presets.push_back({
        "Spatial Bass",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.2601f,  // damping
        0.2f,  // predelay ms
        0.22f,  // mix
        0.79f,  // modRate Hz
        0.2552f,  // modDepth
        1.0000f,  // width
        0.4285f,  // earlyDiff
        0.5195f,  // lateDiff
        0.86f,  // bassMult
        519.1f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5375f,  // roomSize
        0.6000f,  // earlyLateBal
        0.84f  // highDecay
    });

    // Similar to SpinningRoom with different parameters (match: 78%)
    presets.push_back({
        "Spatial Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.2343f,  // size
        0.2566f,  // damping
        24.5f,  // predelay ms
        0.22f,  // mix
        0.80f,  // modRate Hz
        0.2537f,  // modDepth
        0.8921f,  // width
        0.5585f,  // earlyDiff
        0.5243f,  // lateDiff
        0.74f,  // bassMult
        410.1f,  // bassFreq Hz
        20.3f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6165f,  // roomSize
        0.7000f,  // earlyLateBal
        0.78f  // highDecay
    });

    // Nice Ambience reverb with circular sweep of Out Width (match: 82%)
    presets.push_back({
        "Spinning Room",
        "Rooms",
        7,  // Chorus Space
        0,  // 1970s
        0.1733f,  // size
        0.3499f,  // damping
        33.0f,  // predelay ms
        0.22f,  // mix
        0.80f,  // modRate Hz
        0.2457f,  // modDepth
        0.7775f,  // width
        0.5742f,  // earlyDiff
        0.5852f,  // lateDiff
        0.93f,  // bassMult
        404.6f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6078f,  // roomSize
        0.6000f,  // earlyLateBal
        0.83f  // highDecay
    });

    // Chamber/Room where a small and big room are mixed (match: 82%)
    presets.push_back({
        "Split Rooms",
        "Rooms",
        3,  // Chamber
        0,  // 1970s
        0.3719f,  // size
        0.5237f,  // damping
        9.0f,  // predelay ms
        0.22f,  // mix
        0.80f,  // modRate Hz
        0.2542f,  // modDepth
        0.9756f,  // width
        0.5765f,  // earlyDiff
        0.4820f,  // lateDiff
        1.21f,  // bassMult
        406.1f,  // bassFreq Hz
        20.5f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5950f,  // roomSize
        0.6000f,  // earlyLateBal
        1.34f  // highDecay
    });

    // Metallic sound and bright resonance (match: 76%)
    presets.push_back({
        "Storage Tank",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.3605f,  // size
        0.5507f,  // damping
        6.0f,  // predelay ms
        0.22f,  // mix
        0.81f,  // modRate Hz
        0.2518f,  // modDepth
        0.7957f,  // width
        0.5660f,  // earlyDiff
        0.5039f,  // lateDiff
        0.97f,  // bassMult
        400.4f,  // bassFreq Hz
        20.4f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6401f,  // roomSize
        0.6000f,  // earlyLateBal
        0.81f  // highDecay
    });

    // Customize how empty or full this storeroom is (match: 77%)
    presets.push_back({
        "Storeroom",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.4573f,  // size
        0.4575f,  // damping
        1.9f,  // predelay ms
        0.22f,  // mix
        0.80f,  // modRate Hz
        0.3007f,  // modDepth
        0.8417f,  // width
        0.5616f,  // earlyDiff
        0.5021f,  // lateDiff
        0.91f,  // bassMult
        400.8f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6201f,  // roomSize
        0.7000f,  // earlyLateBal
        0.82f  // highDecay
    });

    // Super-tight concert hall with lots of spatial enhancement (match: 25%)
    presets.push_back({
        "Strange Place",
        "Rooms",
        7,  // Chorus Space
        0,  // 1970s
        0.0000f,  // size
        0.4521f,  // damping
        43.4f,  // predelay ms
        0.22f,  // mix
        0.80f,  // modRate Hz
        0.2504f,  // modDepth
        0.8356f,  // width
        0.5494f,  // earlyDiff
        0.5485f,  // lateDiff
        0.98f,  // bassMult
        400.8f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5937f,  // roomSize
        0.6000f,  // earlyLateBal
        1.30f  // highDecay
    });

    // Vibrancy and attitude with a gated feel (match: 52%)
    presets.push_back({
        "Tight Space",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.2825f,  // damping
        27.1f,  // predelay ms
        0.22f,  // mix
        0.83f,  // modRate Hz
        0.2589f,  // modDepth
        0.8461f,  // width
        0.5470f,  // earlyDiff
        0.5326f,  // lateDiff
        0.99f,  // bassMult
        524.3f,  // bassFreq Hz
        48.5f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6037f,  // roomSize
        0.6000f,  // earlyLateBal
        1.12f  // highDecay
    });

    // Incredibly sibilant and bright reverberant space (match: 77%)
    presets.push_back({
        "Tiled Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.1536f,  // damping
        16.8f,  // predelay ms
        0.22f,  // mix
        0.82f,  // modRate Hz
        0.2537f,  // modDepth
        0.8366f,  // width
        0.5468f,  // earlyDiff
        0.5418f,  // lateDiff
        0.98f,  // bassMult
        413.3f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        19849.6f,  // highCut Hz
        false,
        0.6096f,  // roomSize
        0.6000f,  // earlyLateBal
        1.20f  // highDecay
    });

    // Just like Vocal Amb, but smaller and tighter (match: 73%)
    presets.push_back({
        "Very Small Amb",
        "Rooms",
        5,  // Ambience
        0,  // 1970s
        0.0752f,  // size
        0.4653f,  // damping
        19.0f,  // predelay ms
        0.22f,  // mix
        0.90f,  // modRate Hz
        0.2562f,  // modDepth
        0.8451f,  // width
        0.5761f,  // earlyDiff
        0.3868f,  // lateDiff
        1.19f,  // bassMult
        405.0f,  // bassFreq Hz
        20.4f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5699f,  // roomSize
        0.6000f,  // earlyLateBal
        1.00f  // highDecay
    });

    // Short and soft, very realistic small room (match: 59%)
    presets.push_back({
        "Vocal Ambience",
        "Rooms",
        5,  // Ambience
        0,  // 1970s
        0.0000f,  // size
        0.2520f,  // damping
        12.0f,  // predelay ms
        0.22f,  // mix
        0.81f,  // modRate Hz
        0.2504f,  // modDepth
        0.8567f,  // width
        0.5658f,  // earlyDiff
        0.5399f,  // lateDiff
        0.97f,  // bassMult
        395.4f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5821f,  // roomSize
        0.6000f,  // earlyLateBal
        0.66f  // highDecay
    });

    // Most confining of isolation booths (match: 74%)
    presets.push_back({
        "Vocal Booth",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0000f,  // size
        0.6210f,  // damping
        0.7f,  // predelay ms
        0.22f,  // mix
        0.83f,  // modRate Hz
        0.2593f,  // modDepth
        0.7984f,  // width
        0.4953f,  // earlyDiff
        0.5515f,  // lateDiff
        1.04f,  // bassMult
        377.9f,  // bassFreq Hz
        20.8f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6474f,  // roomSize
        0.6000f,  // earlyLateBal
        0.89f  // highDecay
    });

    // Short Mid RT and small Size — ideal for vocals (match: 69%)
    presets.push_back({
        "Vocal Space",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.1528f,  // damping
        95.7f,  // predelay ms
        0.22f,  // mix
        0.80f,  // modRate Hz
        0.1861f,  // modDepth
        0.9542f,  // width
        0.6402f,  // earlyDiff
        0.5195f,  // lateDiff
        0.99f,  // bassMult
        451.7f,  // bassFreq Hz
        20.7f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6039f,  // roomSize
        0.6000f,  // earlyLateBal
        0.93f  // highDecay
    });

    // Big, wide space with dark, somber effect (match: 84%)
    presets.push_back({
        "Wide Chamber",
        "Rooms",
        3,  // Chamber
        0,  // 1970s
        0.3008f,  // size
        0.5372f,  // damping
        60.4f,  // predelay ms
        0.22f,  // mix
        0.84f,  // modRate Hz
        0.2563f,  // modDepth
        0.8794f,  // width
        0.5619f,  // earlyDiff
        0.4844f,  // lateDiff
        1.02f,  // bassMult
        389.2f,  // bassFreq Hz
        20.5f,  // lowCut Hz
        18178.2f,  // highCut Hz
        false,
        0.6205f,  // roomSize
        0.6000f,  // earlyLateBal
        0.85f  // highDecay
    });

    // Special drum effect, narrow to wide, slap happy (match: 38%)
    presets.push_back({
        "Wide Slap Drum",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.4582f,  // damping
        62.7f,  // predelay ms
        0.22f,  // mix
        0.84f,  // modRate Hz
        0.2523f,  // modDepth
        0.8638f,  // width
        0.5654f,  // earlyDiff
        0.4909f,  // lateDiff
        0.70f,  // bassMult
        392.0f,  // bassFreq Hz
        26.8f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6036f,  // roomSize
        0.5000f,  // earlyLateBal
        0.77f  // highDecay
    });


    // ==================== PLATES (46) ====================

    // Really smooth plate with slow reverb build (match: 85%)
    presets.push_back({
        "Acoustic Gtr Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.2343f,  // size
        0.2579f,  // damping
        20.8f,  // predelay ms
        0.28f,  // mix
        0.82f,  // modRate Hz
        0.2561f,  // modDepth
        0.9561f,  // width
        0.5080f,  // earlyDiff
        0.5222f,  // lateDiff
        1.23f,  // bassMult
        401.2f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        19679.3f,  // highCut Hz
        false,
        0.5939f,  // roomSize
        0.6000f,  // earlyLateBal
        1.31f  // highDecay
    });

    // Medium size plate, high diffusion, moderate decay (match: 84%)
    presets.push_back({
        "Big Drums",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.2343f,  // size
        0.5463f,  // damping
        3.9f,  // predelay ms
        0.28f,  // mix
        0.79f,  // modRate Hz
        0.2522f,  // modDepth
        0.8955f,  // width
        0.5584f,  // earlyDiff
        0.5165f,  // lateDiff
        1.28f,  // bassMult
        401.2f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        19849.4f,  // highCut Hz
        false,
        0.6048f,  // roomSize
        0.6000f,  // earlyLateBal
        1.37f  // highDecay
    });

    // Gives bongos and native drums thickness (match: 85%)
    presets.push_back({
        "Bongo Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.0752f,  // size
        0.0508f,  // damping
        45.7f,  // predelay ms
        0.28f,  // mix
        1.22f,  // modRate Hz
        0.2885f,  // modDepth
        0.9202f,  // width
        0.6473f,  // earlyDiff
        0.4975f,  // lateDiff
        1.48f,  // bassMult
        408.9f,  // bassFreq Hz
        82.5f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5844f,  // roomSize
        0.7000f,  // earlyLateBal
        1.25f  // highDecay
    });

    // Small bright plate, short decay, enhancing (match: 83%)
    presets.push_back({
        "Bright Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.0000f,  // size
        0.5369f,  // damping
        45.2f,  // predelay ms
        0.28f,  // mix
        0.82f,  // modRate Hz
        0.2470f,  // modDepth
        0.9323f,  // width
        0.6025f,  // earlyDiff
        0.5331f,  // lateDiff
        0.91f,  // bassMult
        407.6f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5942f,  // roomSize
        0.7000f,  // earlyLateBal
        1.32f  // highDecay
    });

    // Large bright plate, long decay for various vocals (match: 88%)
    presets.push_back({
        "Bright Vox Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.2343f,  // size
        0.2492f,  // damping
        21.5f,  // predelay ms
        0.28f,  // mix
        1.41f,  // modRate Hz
        0.2560f,  // modDepth
        0.9189f,  // width
        0.5456f,  // earlyDiff
        0.5550f,  // lateDiff
        1.27f,  // bassMult
        529.1f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        19530.6f,  // highCut Hz
        false,
        0.6021f,  // roomSize
        0.6000f,  // earlyLateBal
        1.28f  // highDecay
    });

    // Large silky plate, long decay for background (match: 80%)
    presets.push_back({
        "Choir Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.4674f,  // size
        0.1483f,  // damping
        68.0f,  // predelay ms
        0.28f,  // mix
        0.80f,  // modRate Hz
        0.2585f,  // modDepth
        0.8647f,  // width
        0.4367f,  // earlyDiff
        0.5069f,  // lateDiff
        1.25f,  // bassMult
        402.5f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6055f,  // roomSize
        0.6000f,  // earlyLateBal
        0.83f  // highDecay
    });

    // Clean plate with diffusion control (match: 86%)
    presets.push_back({
        "Clean Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.3132f,  // size
        0.0000f,  // damping
        19.7f,  // predelay ms
        0.28f,  // mix
        0.82f,  // modRate Hz
        0.2541f,  // modDepth
        0.8851f,  // width
        0.5576f,  // earlyDiff
        0.5232f,  // lateDiff
        0.99f,  // bassMult
        397.4f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        19928.0f,  // highCut Hz
        false,
        0.6222f,  // roomSize
        0.6000f,  // earlyLateBal
        0.97f  // highDecay
    });

    // Short dull plate for percussion (match: 80%)
    presets.push_back({
        "Cool Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.1733f,  // size
        0.0505f,  // damping
        16.1f,  // predelay ms
        0.28f,  // mix
        0.78f,  // modRate Hz
        0.2606f,  // modDepth
        0.8799f,  // width
        0.5931f,  // earlyDiff
        0.5068f,  // lateDiff
        1.42f,  // bassMult
        271.8f,  // bassFreq Hz
        83.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5838f,  // roomSize
        0.6000f,  // earlyLateBal
        0.81f  // highDecay
    });

    // Classic! Dark, smooth, long decay, fatten percussion (match: 81%)
    presets.push_back({
        "Dark Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.4050f,  // size
        0.3628f,  // damping
        47.4f,  // predelay ms
        0.28f,  // mix
        0.82f,  // modRate Hz
        0.2073f,  // modDepth
        1.0000f,  // width
        0.5695f,  // earlyDiff
        0.4946f,  // lateDiff
        1.26f,  // bassMult
        454.2f,  // bassFreq Hz
        20.5f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6123f,  // roomSize
        0.7000f,  // earlyLateBal
        0.78f  // highDecay
    });

    // Large dark plate, high diffusion, long decay (match: 80%)
    presets.push_back({
        "Drum Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.3008f,  // size
        0.5370f,  // damping
        32.6f,  // predelay ms
        0.28f,  // mix
        0.80f,  // modRate Hz
        0.2610f,  // modDepth
        0.9007f,  // width
        0.5446f,  // earlyDiff
        0.5020f,  // lateDiff
        0.92f,  // bassMult
        327.0f,  // bassFreq Hz
        20.5f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6095f,  // roomSize
        0.7000f,  // earlyLateBal
        1.00f  // highDecay
    });

    // Sweet combination of recirculating pre-echoes (match: 71%)
    presets.push_back({
        "Eko Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.6081f,  // size
        0.4417f,  // damping
        0.9f,  // predelay ms
        0.28f,  // mix
        0.79f,  // modRate Hz
        0.2537f,  // modDepth
        0.8651f,  // width
        0.5484f,  // earlyDiff
        0.5426f,  // lateDiff
        1.26f,  // bassMult
        398.7f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5946f,  // roomSize
        0.6000f,  // earlyLateBal
        1.00f  // highDecay
    });

    // Mono level patched to Attack and Spread (match: 64%)
    presets.push_back({
        "Ever Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.0752f,  // size
        0.5550f,  // damping
        9.9f,  // predelay ms
        0.28f,  // mix
        1.34f,  // modRate Hz
        0.2575f,  // modDepth
        0.7497f,  // width
        0.5552f,  // earlyDiff
        0.5533f,  // lateDiff
        1.50f,  // bassMult
        409.5f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        18975.4f,  // highCut Hz
        false,
        0.5887f,  // roomSize
        0.6000f,  // earlyLateBal
        0.98f  // highDecay
    });

    // Moderate sized, deep sounding plate, high attack (match: 83%)
    presets.push_back({
        "Fat Drums",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.2343f,  // size
        0.5525f,  // damping
        1.2f,  // predelay ms
        0.28f,  // mix
        0.82f,  // modRate Hz
        0.2509f,  // modDepth
        0.8603f,  // width
        0.5589f,  // earlyDiff
        0.5431f,  // lateDiff
        1.01f,  // bassMult
        401.1f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5677f,  // roomSize
        0.6000f,  // earlyLateBal
        1.26f  // highDecay
    });

    // Big plate with long predelay and repeating echo (match: 75%)
    presets.push_back({
        "Floyd Wash",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.5354f,  // size
        0.4371f,  // damping
        8.6f,  // predelay ms
        0.28f,  // mix
        0.79f,  // modRate Hz
        0.2493f,  // modDepth
        0.8388f,  // width
        0.5533f,  // earlyDiff
        0.5687f,  // lateDiff
        1.00f,  // bassMult
        399.0f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5774f,  // roomSize
        0.6000f,  // earlyLateBal
        1.32f  // highDecay
    });

    // Generic plate preset, starting place (match: 83%)
    presets.push_back({
        "Gen. Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.1733f,  // size
        0.4536f,  // damping
        0.6f,  // predelay ms
        0.28f,  // mix
        0.81f,  // modRate Hz
        0.2422f,  // modDepth
        0.9257f,  // width
        0.5786f,  // earlyDiff
        0.5077f,  // lateDiff
        1.09f,  // bassMult
        394.1f,  // bassFreq Hz
        20.6f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5832f,  // roomSize
        0.6000f,  // earlyLateBal
        0.98f  // highDecay
    });

    // Classic plate with long decay, medium high end (match: 85%)
    presets.push_back({
        "Gold Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.1733f,  // size
        0.1544f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.79f,  // modRate Hz
        0.2511f,  // modDepth
        0.9372f,  // width
        0.6108f,  // earlyDiff
        0.5217f,  // lateDiff
        1.20f,  // bassMult
        370.5f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6562f,  // roomSize
        0.6000f,  // earlyLateBal
        1.26f  // highDecay
    });

    // Basic plate, not too dark and not too bright (match: 81%)
    presets.push_back({
        "Great Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.3008f,  // size
        0.4536f,  // damping
        26.4f,  // predelay ms
        0.28f,  // mix
        0.95f,  // modRate Hz
        0.2624f,  // modDepth
        0.8387f,  // width
        0.5574f,  // earlyDiff
        0.4988f,  // lateDiff
        0.97f,  // bassMult
        399.6f,  // bassFreq Hz
        20.4f,  // lowCut Hz
        19640.2f,  // highCut Hz
        false,
        0.5221f,  // roomSize
        0.7000f,  // earlyLateBal
        1.03f  // highDecay
    });

    // Basic guitar delay with plate reverb mixed in (match: 49%)
    presets.push_back({
        "Guitar Dly Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.3605f,  // size
        0.1521f,  // damping
        13.0f,  // predelay ms
        0.28f,  // mix
        0.81f,  // modRate Hz
        0.2586f,  // modDepth
        0.7192f,  // width
        0.5686f,  // earlyDiff
        0.5135f,  // lateDiff
        1.20f,  // bassMult
        399.6f,  // bassFreq Hz
        21.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6092f,  // roomSize
        0.6000f,  // earlyLateBal
        0.99f  // highDecay
    });

    // Moderate size, dark plate reverb for guitar (match: 77%)
    presets.push_back({
        "Guitar Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.4674f,  // size
        0.0790f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.81f,  // modRate Hz
        0.3876f,  // modDepth
        0.8603f,  // width
        0.5554f,  // earlyDiff
        0.4474f,  // lateDiff
        1.55f,  // bassMult
        396.9f,  // bassFreq Hz
        20.3f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6048f,  // roomSize
        0.6000f,  // earlyLateBal
        0.90f  // highDecay
    });

    // Medium sizzling plate, optimized for live mixing (match: 75%)
    presets.push_back({
        "Hot Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.3008f,  // size
        0.3559f,  // damping
        2.3f,  // predelay ms
        0.28f,  // mix
        0.80f,  // modRate Hz
        0.2534f,  // modDepth
        0.8983f,  // width
        0.5586f,  // earlyDiff
        0.4817f,  // lateDiff
        1.22f,  // bassMult
        385.9f,  // bassFreq Hz
        20.4f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6107f,  // roomSize
        0.6000f,  // earlyLateBal
        0.81f  // highDecay
    });

    // Basic plate for any kind of sound source (match: 76%)
    presets.push_back({
        "Just Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.4573f,  // size
        0.3609f,  // damping
        0.1f,  // predelay ms
        0.28f,  // mix
        0.84f,  // modRate Hz
        0.2577f,  // modDepth
        0.8653f,  // width
        0.5415f,  // earlyDiff
        0.5001f,  // lateDiff
        1.29f,  // bassMult
        393.1f,  // bassFreq Hz
        20.6f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6010f,  // roomSize
        0.6000f,  // earlyLateBal
        0.81f  // highDecay
    });

    // Medium plate, short reverb time for full kit (match: 79%)
    presets.push_back({
        "Live Drums Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.1733f,  // size
        0.4605f,  // damping
        0.9f,  // predelay ms
        0.28f,  // mix
        0.80f,  // modRate Hz
        0.2471f,  // modDepth
        0.8647f,  // width
        0.5578f,  // earlyDiff
        0.5387f,  // lateDiff
        1.12f,  // bassMult
        390.3f,  // bassFreq Hz
        20.3f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5784f,  // roomSize
        0.6000f,  // earlyLateBal
        0.83f  // highDecay
    });

    // Tight gate or crisp inverse sounds on the fly (match: 41%)
    presets.push_back({
        "Live Gate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.0000f,  // size
        0.5771f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.81f,  // modRate Hz
        0.2927f,  // modDepth
        0.9445f,  // width
        0.5837f,  // earlyDiff
        0.5136f,  // lateDiff
        0.90f,  // bassMult
        394.3f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        19625.3f,  // highCut Hz
        false,
        0.5979f,  // roomSize
        0.6000f,  // earlyLateBal
        0.74f  // highDecay
    });

    // Crisp clean basic plate, medium decay (match: 68%)
    presets.push_back({
        "Live Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.3008f,  // size
        0.5623f,  // damping
        37.7f,  // predelay ms
        0.28f,  // mix
        0.82f,  // modRate Hz
        0.2649f,  // modDepth
        0.8885f,  // width
        0.5539f,  // earlyDiff
        0.4968f,  // lateDiff
        1.12f,  // bassMult
        392.7f,  // bassFreq Hz
        20.6f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6060f,  // roomSize
        0.6000f,  // earlyLateBal
        0.97f  // highDecay
    });

    // General plate, run in mono, stereo, or 3 choices (match: 75%)
    presets.push_back({
        "Mono Or Stereo",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.3008f,  // size
        0.5516f,  // damping
        0.3f,  // predelay ms
        0.28f,  // mix
        0.73f,  // modRate Hz
        0.2547f,  // modDepth
        0.8770f,  // width
        0.5695f,  // earlyDiff
        0.4973f,  // lateDiff
        1.11f,  // bassMult
        395.7f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5521f,  // roomSize
        0.6000f,  // earlyLateBal
        0.82f  // highDecay
    });

    // Multi-purpose plate delay with custom controls (match: 68%)
    presets.push_back({
        "Multi Plate Dly",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.3008f,  // size
        0.5394f,  // damping
        0.9f,  // predelay ms
        0.28f,  // mix
        0.83f,  // modRate Hz
        0.2482f,  // modDepth
        0.8965f,  // width
        0.5632f,  // earlyDiff
        0.5148f,  // lateDiff
        0.94f,  // bassMult
        394.5f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6152f,  // roomSize
        0.6000f,  // earlyLateBal
        1.04f  // highDecay
    });

    // Small short plate for gang vocals (match: 82%)
    presets.push_back({
        "Multi Vox",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.1733f,  // size
        0.4636f,  // damping
        5.2f,  // predelay ms
        0.28f,  // mix
        0.82f,  // modRate Hz
        0.1061f,  // modDepth
        0.9888f,  // width
        0.5654f,  // earlyDiff
        0.5133f,  // lateDiff
        0.93f,  // bassMult
        400.5f,  // bassFreq Hz
        20.4f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5806f,  // roomSize
        0.6000f,  // earlyLateBal
        0.99f  // highDecay
    });

    // Tempo-driven spatial effect for dramatic spatial effects (match: 48%)
    presets.push_back({
        "Patterns",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.6599f,  // size
        0.4156f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.80f,  // modRate Hz
        0.2486f,  // modDepth
        0.7440f,  // width
        0.5534f,  // earlyDiff
        0.5614f,  // lateDiff
        1.60f,  // bassMult
        402.4f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5821f,  // roomSize
        0.6000f,  // earlyLateBal
        1.01f  // highDecay
    });

    // General purpose, dark plate (match: 85%)
    presets.push_back({
        "Plate 90",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.1733f,  // size
        0.1583f,  // damping
        62.4f,  // predelay ms
        0.28f,  // mix
        0.78f,  // modRate Hz
        0.2602f,  // modDepth
        0.9208f,  // width
        0.5857f,  // earlyDiff
        0.4912f,  // lateDiff
        1.06f,  // bassMult
        409.8f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5988f,  // roomSize
        0.6000f,  // earlyLateBal
        0.92f  // highDecay
    });

    // A good plate for brass sounds (match: 86%)
    presets.push_back({
        "Plate For Brass",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.3008f,  // size
        0.1561f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.82f,  // modRate Hz
        0.3879f,  // modDepth
        0.9095f,  // width
        0.5546f,  // earlyDiff
        0.5056f,  // lateDiff
        1.11f,  // bassMult
        406.2f,  // bassFreq Hz
        33.6f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5970f,  // roomSize
        0.5000f,  // earlyLateBal
        1.00f  // highDecay
    });

    // Gate with tonal qualities of a plate (match: 85%)
    presets.push_back({
        "Plate Gate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.0000f,  // size
        0.4370f,  // damping
        0.2f,  // predelay ms
        0.28f,  // mix
        0.79f,  // modRate Hz
        0.2520f,  // modDepth
        0.8859f,  // width
        0.5428f,  // earlyDiff
        0.5707f,  // lateDiff
        0.96f,  // bassMult
        410.9f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5761f,  // roomSize
        0.6000f,  // earlyLateBal
        1.30f  // highDecay
    });

    // Heavy, dense, short, nonlinear reverb (match: 70%)
    presets.push_back({
        "Plate Gate 2",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.0000f,  // size
        0.5399f,  // damping
        2.3f,  // predelay ms
        0.28f,  // mix
        0.81f,  // modRate Hz
        0.2545f,  // modDepth
        0.8903f,  // width
        0.5534f,  // earlyDiff
        0.5463f,  // lateDiff
        0.94f,  // bassMult
        397.7f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5486f,  // roomSize
        0.6000f,  // earlyLateBal
        1.30f  // highDecay
    });

    // An old standard, bright and diffuse (match: 85%)
    presets.push_back({
        "Rich Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.1733f,  // size
        0.0512f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.80f,  // modRate Hz
        0.3063f,  // modDepth
        0.8683f,  // width
        0.5546f,  // earlyDiff
        0.5124f,  // lateDiff
        1.11f,  // bassMult
        384.8f,  // bassFreq Hz
        20.4f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.4513f,  // roomSize
        0.6000f,  // earlyLateBal
        0.98f  // highDecay
    });

    // Big boomy dark plate, moderate reverb tail (match: 82%)
    presets.push_back({
        "Rock Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.3008f,  // size
        0.3451f,  // damping
        23.2f,  // predelay ms
        0.28f,  // mix
        0.82f,  // modRate Hz
        0.2551f,  // modDepth
        0.8792f,  // width
        0.5628f,  // earlyDiff
        0.5207f,  // lateDiff
        1.21f,  // bassMult
        393.8f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5889f,  // roomSize
        0.6000f,  // earlyLateBal
        0.81f  // highDecay
    });

    // Short plate reverb, fairly short decay, good high end (match: 77%)
    presets.push_back({
        "Short Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.0000f,  // size
        0.4219f,  // damping
        2.7f,  // predelay ms
        0.28f,  // mix
        1.27f,  // modRate Hz
        0.3751f,  // modDepth
        0.8957f,  // width
        0.6927f,  // earlyDiff
        0.5610f,  // lateDiff
        1.30f,  // bassMult
        404.3f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        19479.4f,  // highCut Hz
        false,
        0.4966f,  // roomSize
        0.6000f,  // earlyLateBal
        0.83f  // highDecay
    });

    // Small bright plate for vocals (match: 69%)
    presets.push_back({
        "Small Vox Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.0000f,  // size
        0.6230f,  // damping
        0.7f,  // predelay ms
        0.28f,  // mix
        1.53f,  // modRate Hz
        0.2538f,  // modDepth
        0.8785f,  // width
        0.5413f,  // earlyDiff
        0.5568f,  // lateDiff
        1.28f,  // bassMult
        416.8f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        19909.1f,  // highCut Hz
        false,
        0.5651f,  // roomSize
        0.6000f,  // earlyLateBal
        0.79f  // highDecay
    });

    // Plate reverb with two LFOs controlling InWidth/OutWidth (match: 72%)
    presets.push_back({
        "Spatial Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.1733f,  // size
        0.5690f,  // damping
        8.8f,  // predelay ms
        0.28f,  // mix
        0.43f,  // modRate Hz
        0.2569f,  // modDepth
        0.8207f,  // width
        0.5597f,  // earlyDiff
        0.5181f,  // lateDiff
        0.71f,  // bassMult
        399.1f,  // bassFreq Hz
        20.5f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5835f,  // roomSize
        0.6000f,  // earlyLateBal
        1.15f  // highDecay
    });

    // Medium bright plate with tempo delays for synth (match: 78%)
    presets.push_back({
        "Synth Lead",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.3008f,  // size
        0.1017f,  // damping
        59.2f,  // predelay ms
        0.28f,  // mix
        0.81f,  // modRate Hz
        0.2541f,  // modDepth
        0.8272f,  // width
        0.5860f,  // earlyDiff
        0.5041f,  // lateDiff
        1.56f,  // bassMult
        405.0f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6104f,  // roomSize
        0.6000f,  // earlyLateBal
        1.02f  // highDecay
    });

    // Small and tight, moderate diffusion, for percussion (match: 87%)
    presets.push_back({
        "Tight Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.0000f,  // size
        0.4402f,  // damping
        0.2f,  // predelay ms
        0.28f,  // mix
        0.80f,  // modRate Hz
        0.2490f,  // modDepth
        0.9013f,  // width
        0.5359f,  // earlyDiff
        0.5644f,  // lateDiff
        1.02f,  // bassMult
        397.6f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5779f,  // roomSize
        0.6000f,  // earlyLateBal
        1.29f  // highDecay
    });

    // Silky smooth plate, moderate decay, recirculating (match: 64%)
    presets.push_back({
        "Vocal Echo",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.3605f,  // size
        0.4627f,  // damping
        63.0f,  // predelay ms
        0.28f,  // mix
        0.79f,  // modRate Hz
        0.2597f,  // modDepth
        0.8451f,  // width
        0.5703f,  // earlyDiff
        0.4882f,  // lateDiff
        1.56f,  // bassMult
        399.2f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5963f,  // roomSize
        0.6000f,  // earlyLateBal
        1.01f  // highDecay
    });

    // Large dark plate, just the right amount of delay (match: 71%)
    presets.push_back({
        "Vocal Echo Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.4157f,  // size
        0.2493f,  // damping
        97.9f,  // predelay ms
        0.28f,  // mix
        0.78f,  // modRate Hz
        0.2594f,  // modDepth
        0.8717f,  // width
        0.5670f,  // earlyDiff
        0.5127f,  // lateDiff
        1.29f,  // bassMult
        390.9f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6090f,  // roomSize
        0.6000f,  // earlyLateBal
        0.99f  // highDecay
    });

    // Short plate, low diffusion, solo vocal track (match: 81%)
    presets.push_back({
        "Vocal Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.0752f,  // size
        0.2525f,  // damping
        10.6f,  // predelay ms
        0.28f,  // mix
        1.08f,  // modRate Hz
        0.2518f,  // modDepth
        0.9937f,  // width
        0.5689f,  // earlyDiff
        0.5235f,  // lateDiff
        1.19f,  // bassMult
        395.9f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6400f,  // roomSize
        0.7000f,  // earlyLateBal
        1.03f  // highDecay
    });

    // Large plate, moderate decay for backing vocals (match: 84%)
    presets.push_back({
        "Vocal Plate 2",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.1733f,  // size
        0.0073f,  // damping
        49.3f,  // predelay ms
        0.28f,  // mix
        0.82f,  // modRate Hz
        0.2477f,  // modDepth
        0.8757f,  // width
        0.5746f,  // earlyDiff
        0.5162f,  // lateDiff
        0.99f,  // bassMult
        539.1f,  // bassFreq Hz
        20.5f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5907f,  // roomSize
        0.6000f,  // earlyLateBal
        1.19f  // highDecay
    });

    // Similar to VocalEcho with different delay taps (match: 72%)
    presets.push_back({
        "Vocal Tap",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.6081f,  // size
        0.4554f,  // damping
        11.7f,  // predelay ms
        0.28f,  // mix
        0.80f,  // modRate Hz
        0.2510f,  // modDepth
        0.7833f,  // width
        0.5673f,  // earlyDiff
        0.5233f,  // lateDiff
        1.27f,  // bassMult
        395.8f,  // bassFreq Hz
        20.8f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5950f,  // roomSize
        0.6000f,  // earlyLateBal
        0.82f  // highDecay
    });

    // Slightly warmer plate with less edge (match: 85%)
    presets.push_back({
        "Warm Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.1733f,  // size
        0.3601f,  // damping
        13.3f,  // predelay ms
        0.28f,  // mix
        0.81f,  // modRate Hz
        0.2590f,  // modDepth
        0.8960f,  // width
        0.5600f,  // earlyDiff
        0.5151f,  // lateDiff
        1.06f,  // bassMult
        399.0f,  // bassFreq Hz
        20.6f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5840f,  // roomSize
        0.6000f,  // earlyLateBal
        0.93f  // highDecay
    });

    // Tap tempo-controlled LFO1 modulates High Cut (match: 70%)
    presets.push_back({
        "What The Heck",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.1733f,  // size
        0.1546f,  // damping
        22.8f,  // predelay ms
        0.28f,  // mix
        0.82f,  // modRate Hz
        0.2519f,  // modDepth
        0.8278f,  // width
        0.5648f,  // earlyDiff
        0.5019f,  // lateDiff
        1.49f,  // bassMult
        399.7f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5820f,  // roomSize
        0.6000f,  // earlyLateBal
        0.99f  // highDecay
    });


    // ==================== CREATIVE (27) ====================

    // Adjust compression/expansion and Custom 1 (match: 46%)
    presets.push_back({
        "Air Pressure",
        "Creative",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.0512f,  // damping
        8.9f,  // predelay ms
        0.35f,  // mix
        0.84f,  // modRate Hz
        0.2604f,  // modDepth
        0.7804f,  // width
        0.5613f,  // earlyDiff
        0.5241f,  // lateDiff
        1.32f,  // bassMult
        397.5f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.7320f,  // roomSize
        0.6000f,  // earlyLateBal
        0.87f  // highDecay
    });

    // Input signals reflect off brick buildings (match: 53%)
    presets.push_back({
        "Block Party",
        "Creative",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.5683f,  // damping
        16.2f,  // predelay ms
        0.35f,  // mix
        0.69f,  // modRate Hz
        0.2596f,  // modDepth
        0.8703f,  // width
        0.5614f,  // earlyDiff
        0.4908f,  // lateDiff
        1.07f,  // bassMult
        380.6f,  // bassFreq Hz
        21.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5948f,  // roomSize
        0.6000f,  // earlyLateBal
        0.81f  // highDecay
    });

    // Varies Decay, Out Width, and High Cut (match: 66%)
    presets.push_back({
        "Bombay Club",
        "Creative",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.5260f,  // damping
        0.0f,  // predelay ms
        0.35f,  // mix
        0.81f,  // modRate Hz
        0.2558f,  // modDepth
        0.9102f,  // width
        0.5623f,  // earlyDiff
        0.5019f,  // lateDiff
        1.18f,  // bassMult
        271.2f,  // bassFreq Hz
        20.5f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5953f,  // roomSize
        0.5000f,  // earlyLateBal
        0.82f  // highDecay
    });

    // Dull backstage sound and large open space (match: 76%)
    presets.push_back({
        "Dull/Bright",
        "Creative",
        2,  // Hall
        0,  // 1970s
        0.0000f,  // size
        0.6283f,  // damping
        0.0f,  // predelay ms
        0.35f,  // mix
        0.45f,  // modRate Hz
        0.2523f,  // modDepth
        0.9282f,  // width
        0.5206f,  // earlyDiff
        0.5454f,  // lateDiff
        0.93f,  // bassMult
        272.0f,  // bassFreq Hz
        20.3f,  // lowCut Hz
        19427.7f,  // highCut Hz
        false,
        0.4680f,  // roomSize
        0.7000f,  // earlyLateBal
        0.87f  // highDecay
    });

    // Echo, echo, echo. Master delays and outdoor echo (match: 31%)
    presets.push_back({
        "Echo Beach",
        "Creative",
        2,  // Hall
        0,  // 1970s
        0.0000f,  // size
        0.6035f,  // damping
        0.7f,  // predelay ms
        0.35f,  // mix
        0.78f,  // modRate Hz
        0.2522f,  // modDepth
        0.8857f,  // width
        0.5437f,  // earlyDiff
        0.5508f,  // lateDiff
        1.01f,  // bassMult
        413.9f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        19673.8f,  // highCut Hz
        false,
        0.5981f,  // roomSize
        0.6000f,  // earlyLateBal
        0.84f  // highDecay
    });

    // Medium chamber and an outdoor space (match: 72%)
    presets.push_back({
        "Indoors/Out",
        "Creative",
        3,  // Chamber
        0,  // 1970s
        0.0752f,  // size
        0.0504f,  // damping
        0.5f,  // predelay ms
        0.35f,  // mix
        0.83f,  // modRate Hz
        0.2550f,  // modDepth
        0.8632f,  // width
        0.5548f,  // earlyDiff
        0.5252f,  // lateDiff
        1.45f,  // bassMult
        390.0f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6140f,  // roomSize
        0.6000f,  // earlyLateBal
        1.00f  // highDecay
    });

    // Strange hall with input level controlling width (match: 37%)
    presets.push_back({
        "Inside-Out",
        "Creative",
        2,  // Hall
        0,  // 1970s
        0.3008f,  // size
        0.5702f,  // damping
        11.2f,  // predelay ms
        0.35f,  // mix
        0.37f,  // modRate Hz
        0.2548f,  // modDepth
        0.7300f,  // width
        0.5425f,  // earlyDiff
        0.5145f,  // lateDiff
        1.22f,  // bassMult
        388.2f,  // bassFreq Hz
        20.5f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6175f,  // roomSize
        0.7000f,  // earlyLateBal
        0.69f  // highDecay
    });

    // Bipolar ADJUST to add Predelay or Dry Delay (match: 4%)
    presets.push_back({
        "Mic Location",
        "Creative",
        1,  // Room
        0,  // 1970s
        0.0000f,  // size
        0.5536f,  // damping
        43.1f,  // predelay ms
        0.35f,  // mix
        0.79f,  // modRate Hz
        0.2560f,  // modDepth
        0.7739f,  // width
        0.5590f,  // earlyDiff
        0.5634f,  // lateDiff
        0.96f,  // bassMult
        404.9f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5522f,  // roomSize
        0.5000f,  // earlyLateBal
        0.84f  // highDecay
    });

    // Select Buzzing or Modulated special effects (match: 69%)
    presets.push_back({
        "Mr. Vader",
        "Creative",
        9,  // Dirty Hall
        0,  // 1970s
        0.0752f,  // size
        0.2987f,  // damping
        0.0f,  // predelay ms
        0.35f,  // mix
        0.80f,  // modRate Hz
        0.2493f,  // modDepth
        0.6915f,  // width
        0.6000f,  // earlyDiff
        0.5529f,  // lateDiff
        0.99f,  // bassMult
        403.1f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        19941.9f,  // highCut Hz
        false,
        0.5968f,  // roomSize
        0.6000f,  // earlyLateBal
        1.43f  // highDecay
    });

    // Size and Delay inversely proportionate, supernatural (match: 88%)
    presets.push_back({
        "Mythology",
        "Creative",
        2,  // Hall
        0,  // 1970s
        0.2343f,  // size
        0.3573f,  // damping
        0.1f,  // predelay ms
        0.35f,  // mix
        0.51f,  // modRate Hz
        0.3102f,  // modDepth
        0.8608f,  // width
        0.5439f,  // earlyDiff
        0.5290f,  // lateDiff
        0.97f,  // bassMult
        404.0f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6346f,  // roomSize
        0.6000f,  // earlyLateBal
        1.05f  // highDecay
    });

    // Split simulating two automobile tunnels (match: 89%)
    presets.push_back({
        "NYC Tunnels",
        "Creative",
        2,  // Hall
        0,  // 1970s
        0.3008f,  // size
        0.4671f,  // damping
        10.2f,  // predelay ms
        0.35f,  // mix
        1.17f,  // modRate Hz
        0.2548f,  // modDepth
        0.9026f,  // width
        0.5657f,  // earlyDiff
        0.5201f,  // lateDiff
        1.13f,  // bassMult
        397.4f,  // bassFreq Hz
        20.3f,  // lowCut Hz
        19869.1f,  // highCut Hz
        false,
        0.5912f,  // roomSize
        0.5000f,  // earlyLateBal
        0.98f  // highDecay
    });

    // Open space, not much reflection, max DryDly (match: 13%)
    presets.push_back({
        "Outdoor PA",
        "Creative",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.6162f,  // damping
        78.8f,  // predelay ms
        0.35f,  // mix
        0.82f,  // modRate Hz
        0.2447f,  // modDepth
        0.7891f,  // width
        0.5772f,  // earlyDiff
        0.5578f,  // lateDiff
        1.62f,  // bassMult
        407.3f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        19944.8f,  // highCut Hz
        false,
        0.5811f,  // roomSize
        0.6000f,  // earlyLateBal
        0.83f  // highDecay
    });

    // Similar to Outdoor PA, 5 different settings (match: 29%)
    presets.push_back({
        "Outdoor PA 2",
        "Creative",
        1,  // Room
        0,  // 1970s
        0.0000f,  // size
        0.5940f,  // damping
        1.6f,  // predelay ms
        0.35f,  // mix
        0.82f,  // modRate Hz
        0.2612f,  // modDepth
        0.8226f,  // width
        0.5145f,  // earlyDiff
        0.5395f,  // lateDiff
        0.69f,  // bassMult
        394.7f,  // bassFreq Hz
        21.3f,  // lowCut Hz
        17839.0f,  // highCut Hz
        false,
        0.6482f,  // roomSize
        0.6000f,  // earlyLateBal
        0.70f  // highDecay
    });

    // Tempo reflects Dry L/R from 0.292-32.49 sec (match: 25%)
    presets.push_back({
        "Reverse Taps",
        "Creative",
        2,  // Hall
        0,  // 1970s
        0.0000f,  // size
        0.3327f,  // damping
        89.0f,  // predelay ms
        0.35f,  // mix
        0.79f,  // modRate Hz
        0.2409f,  // modDepth
        0.9419f,  // width
        0.6858f,  // earlyDiff
        0.3984f,  // lateDiff
        1.01f,  // bassMult
        406.4f,  // bassFreq Hz
        72.6f,  // lowCut Hz
        19104.0f,  // highCut Hz
        false,
        0.6055f,  // roomSize
        0.7000f,  // earlyLateBal
        0.80f  // highDecay
    });

    // Imagine an empty hall from the perspective of stage (match: 83%)
    presets.push_back({
        "Sound Check",
        "Creative",
        2,  // Hall
        0,  // 1970s
        0.0000f,  // size
        0.5108f,  // damping
        5.3f,  // predelay ms
        0.35f,  // mix
        0.83f,  // modRate Hz
        0.2471f,  // modDepth
        0.9359f,  // width
        0.5468f,  // earlyDiff
        0.5468f,  // lateDiff
        1.50f,  // bassMult
        399.6f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        19511.7f,  // highCut Hz
        false,
        0.5932f,  // roomSize
        0.6000f,  // earlyLateBal
        0.84f  // highDecay
    });

    // Changes Pre Delay/Dry Delay mix (match: 23%)
    presets.push_back({
        "Sound Stage",
        "Creative",
        2,  // Hall
        0,  // 1970s
        0.0985f,  // size
        0.6350f,  // damping
        4.0f,  // predelay ms
        0.35f,  // mix
        0.80f,  // modRate Hz
        0.1811f,  // modDepth
        0.7731f,  // width
        0.5462f,  // earlyDiff
        0.5523f,  // lateDiff
        1.05f,  // bassMult
        401.4f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5956f,  // roomSize
        0.6000f,  // earlyLateBal
        0.85f  // highDecay
    });

    // Compress and Expand ratios are cranked (match: 58%)
    presets.push_back({
        "Spatializer",
        "Creative",
        7,  // Chorus Space
        0,  // 1970s
        0.0000f,  // size
        0.4419f,  // damping
        65.5f,  // predelay ms
        0.35f,  // mix
        0.83f,  // modRate Hz
        0.2606f,  // modDepth
        0.8234f,  // width
        0.5810f,  // earlyDiff
        0.5405f,  // lateDiff
        0.84f,  // bassMult
        410.7f,  // bassFreq Hz
        20.9f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5146f,  // roomSize
        0.7000f,  // earlyLateBal
        1.52f  // highDecay
    });

    // Designed to simulate a large sports stadium (match: 74%)
    presets.push_back({
        "Stadium",
        "Creative",
        2,  // Hall
        0,  // 1970s
        0.0752f,  // size
        0.1528f,  // damping
        0.0f,  // predelay ms
        0.35f,  // mix
        0.86f,  // modRate Hz
        0.2233f,  // modDepth
        0.6061f,  // width
        0.4770f,  // earlyDiff
        0.5579f,  // lateDiff
        1.30f,  // bassMult
        400.0f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6101f,  // roomSize
        0.6000f,  // earlyLateBal
        1.29f  // highDecay
    });

    // Places source within a very reflective tomb (match: 60%)
    presets.push_back({
        "The Tomb",
        "Creative",
        4,  // Cathedral
        0,  // 1970s
        0.3605f,  // size
        0.4452f,  // damping
        4.5f,  // predelay ms
        0.35f,  // mix
        0.81f,  // modRate Hz
        0.2527f,  // modDepth
        0.8398f,  // width
        0.5620f,  // earlyDiff
        0.5077f,  // lateDiff
        1.26f,  // bassMult
        401.7f,  // bassFreq Hz
        20.3f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6347f,  // roomSize
        0.6000f,  // earlyLateBal
        1.27f  // highDecay
    });

    // Inside of a VW van and inside of a VW bug (match: 60%)
    presets.push_back({
        "Two Autos",
        "Creative",
        1,  // Room
        0,  // 1970s
        0.0000f,  // size
        0.5976f,  // damping
        0.0f,  // predelay ms
        0.35f,  // mix
        0.82f,  // modRate Hz
        0.2503f,  // modDepth
        0.9720f,  // width
        0.5208f,  // earlyDiff
        0.6668f,  // lateDiff
        0.87f,  // bassMult
        387.2f,  // bassFreq Hz
        20.4f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6296f,  // roomSize
        0.5000f,  // earlyLateBal
        0.87f  // highDecay
    });

    // Get lost in the crowd, produces multiple voices (match: 65%)
    presets.push_back({
        "Voices?",
        "Creative",
        8,  // Random Space
        0,  // 1970s
        0.3008f,  // size
        0.3651f,  // damping
        35.8f,  // predelay ms
        0.35f,  // mix
        0.82f,  // modRate Hz
        0.2510f,  // modDepth
        0.8757f,  // width
        0.5531f,  // earlyDiff
        0.5245f,  // lateDiff
        0.97f,  // bassMult
        437.3f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        18379.5f,  // highCut Hz
        false,
        0.6118f,  // roomSize
        0.6000f,  // earlyLateBal
        0.92f  // highDecay
    });

    // Similar to Voices?, with LFO controlling OutWidth (match: 84%)
    presets.push_back({
        "Voices? 2",
        "Creative",
        8,  // Random Space
        0,  // 1970s
        0.3008f,  // size
        0.4747f,  // damping
        8.8f,  // predelay ms
        0.35f,  // mix
        0.82f,  // modRate Hz
        0.2547f,  // modDepth
        0.8625f,  // width
        0.6916f,  // earlyDiff
        0.5240f,  // lateDiff
        0.95f,  // bassMult
        410.6f,  // bassFreq Hz
        91.8f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6610f,  // roomSize
        0.6000f,  // earlyLateBal
        0.75f  // highDecay
    });

    // Decay level, predelay, dry delay, dry mix (match: 26%)
    presets.push_back({
        "Wall Slap",
        "Creative",
        1,  // Room
        0,  // 1970s
        0.0000f,  // size
        0.5538f,  // damping
        36.6f,  // predelay ms
        0.35f,  // mix
        0.81f,  // modRate Hz
        0.2520f,  // modDepth
        0.8060f,  // width
        0.5540f,  // earlyDiff
        0.5532f,  // lateDiff
        1.14f,  // bassMult
        501.6f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6041f,  // roomSize
        0.5000f,  // earlyLateBal
        0.80f  // highDecay
    });

    // Opposite side of windows that can be opened (match: 70%)
    presets.push_back({
        "Window",
        "Creative",
        1,  // Room
        0,  // 1970s
        0.0000f,  // size
        0.3642f,  // damping
        2.7f,  // predelay ms
        0.35f,  // mix
        0.82f,  // modRate Hz
        0.3014f,  // modDepth
        0.9119f,  // width
        0.5108f,  // earlyDiff
        0.5469f,  // lateDiff
        0.94f,  // bassMult
        263.8f,  // bassFreq Hz
        20.9f,  // lowCut Hz
        19690.8f,  // highCut Hz
        false,
        0.6260f,  // roomSize
        0.6000f,  // earlyLateBal
        0.63f  // highDecay
    });

    // LFO drives OutWidth to make the room wobble (match: 67%)
    presets.push_back({
        "Wobble Room",
        "Creative",
        7,  // Chorus Space
        0,  // 1970s
        0.2343f,  // size
        0.5543f,  // damping
        0.4f,  // predelay ms
        0.35f,  // mix
        1.38f,  // modRate Hz
        0.2561f,  // modDepth
        0.7827f,  // width
        0.5737f,  // earlyDiff
        0.5462f,  // lateDiff
        0.82f,  // bassMult
        359.8f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6100f,  // roomSize
        0.5000f,  // earlyLateBal
        1.28f  // highDecay
    });

    // Custom Controls for variable equation (match: 81%)
    presets.push_back({
        "X Variable",
        "Creative",
        2,  // Hall
        0,  // 1970s
        0.3008f,  // size
        0.4692f,  // damping
        41.1f,  // predelay ms
        0.35f,  // mix
        0.82f,  // modRate Hz
        0.2534f,  // modDepth
        0.8668f,  // width
        0.5521f,  // earlyDiff
        0.5038f,  // lateDiff
        1.27f,  // bassMult
        386.1f,  // bassFreq Hz
        20.3f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6186f,  // roomSize
        0.6000f,  // earlyLateBal
        0.99f  // highDecay
    });

    // Random Hall version of X Variable (match: 83%)
    presets.push_back({
        "Y Variable",
        "Creative",
        2,  // Hall
        0,  // 1970s
        0.3490f,  // size
        0.0000f,  // damping
        13.8f,  // predelay ms
        0.35f,  // mix
        0.81f,  // modRate Hz
        0.2536f,  // modDepth
        0.8468f,  // width
        0.5521f,  // earlyDiff
        0.5092f,  // lateDiff
        0.98f,  // bassMult
        332.2f,  // bassFreq Hz
        41.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6038f,  // roomSize
        0.6000f,  // earlyLateBal
        0.78f  // highDecay
    });

    return presets;
}

//==============================================================================
inline void applyPreset(juce::AudioProcessorValueTreeState& params, const Preset& preset)
{
    // Mode (10 choices: normalize by 9.0)
    if (auto* p = params.getParameter("mode"))
        p->setValueNotifyingHost(preset.mode / 9.0f);

    // Color (3 choices: normalize by 2.0)
    if (auto* p = params.getParameter("color"))
        p->setValueNotifyingHost(preset.color / 2.0f);

    // Continuous parameters — use convertTo0to1 for ranged params
    if (auto* p = params.getParameter("size"))
        p->setValueNotifyingHost(preset.size);

    if (auto* p = params.getParameter("damping"))
        p->setValueNotifyingHost(preset.damping);

    if (auto* p = params.getParameter("predelay"))
        p->setValueNotifyingHost(params.getParameterRange("predelay").convertTo0to1(preset.predelay));

    if (auto* p = params.getParameter("mix"))
        p->setValueNotifyingHost(preset.mix);

    if (auto* p = params.getParameter("modrate"))
        p->setValueNotifyingHost(params.getParameterRange("modrate").convertTo0to1(preset.modRate));

    if (auto* p = params.getParameter("moddepth"))
        p->setValueNotifyingHost(preset.modDepth);

    if (auto* p = params.getParameter("width"))
        p->setValueNotifyingHost(preset.width);

    if (auto* p = params.getParameter("earlydiff"))
        p->setValueNotifyingHost(preset.earlyDiff);

    if (auto* p = params.getParameter("latediff"))
        p->setValueNotifyingHost(preset.lateDiff);

    if (auto* p = params.getParameter("bassmult"))
        p->setValueNotifyingHost(params.getParameterRange("bassmult").convertTo0to1(preset.bassMult));

    if (auto* p = params.getParameter("bassfreq"))
        p->setValueNotifyingHost(params.getParameterRange("bassfreq").convertTo0to1(preset.bassFreq));

    if (auto* p = params.getParameter("lowcut"))
        p->setValueNotifyingHost(params.getParameterRange("lowcut").convertTo0to1(preset.lowCut));

    if (auto* p = params.getParameter("highcut"))
        p->setValueNotifyingHost(params.getParameterRange("highcut").convertTo0to1(preset.highCut));

    if (auto* p = params.getParameter("freeze"))
        p->setValueNotifyingHost(preset.freeze ? 1.0f : 0.0f);

    // New parameters (with defaults in Preset struct)
    if (auto* p = params.getParameter("roomsize"))
        p->setValueNotifyingHost(preset.roomSize);

    if (auto* p = params.getParameter("erlatebal"))
        p->setValueNotifyingHost(preset.earlyLateBal);

    if (auto* p = params.getParameter("highdecay"))
        p->setValueNotifyingHost(params.getParameterRange("highdecay").convertTo0to1(preset.highDecay));

    if (auto* p = params.getParameter("middecay"))
        p->setValueNotifyingHost(params.getParameterRange("middecay").convertTo0to1(preset.midDecay));

    if (auto* p = params.getParameter("highfreq"))
        p->setValueNotifyingHost(params.getParameterRange("highfreq").convertTo0to1(preset.highFreq));

    if (auto* p = params.getParameter("ershape"))
        p->setValueNotifyingHost(preset.erShape);

    if (auto* p = params.getParameter("erspread"))
        p->setValueNotifyingHost(preset.erSpread);

    if (auto* p = params.getParameter("erbasscut"))
        p->setValueNotifyingHost(params.getParameterRange("erbasscut").convertTo0to1(preset.erBassCut));
}

} // namespace SilkVerbPresets
