#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <vector>

namespace Velvet90Presets
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

    // Optimizer-controllable parameters (defaults = transparent passthrough)
    float trebleRatio = 1.0f;       // 0.3 - 2.0 (HF feedback scaling)
    float stereoCoupling = 0.15f;   // 0.0 - 0.5 (cross-channel coupling)
    float lowMidFreq = 700.0f;      // 100 - 8000 Hz (low-mid crossover)
    float lowMidDecay = 1.0f;       // 0.25 - 4.0 (low-mid decay multiplier)
    int envMode = 0;                // 0=Off, 1=Gate, 2=Reverse, 3=Swell, 4=Ducked
    float envHold = 500.0f;         // 10 - 2000 ms
    float envRelease = 500.0f;      // 10 - 3000 ms
    float envDepth = 0.0f;          // 0 - 100 %
    float echoDelay = 0.0f;         // 0 - 500 ms
    float echoFeedback = 0.0f;      // 0 - 90 %
    float outEQ1Freq = 1000.0f;     // 100 - 8000 Hz
    float outEQ1Gain = 0.0f;        // -12 - +12 dB
    float outEQ1Q = 1.0f;           // 0.3 - 5.0
    float outEQ2Freq = 4000.0f;     // 100 - 8000 Hz
    float outEQ2Gain = 0.0f;        // -12 - +12 dB
    float outEQ2Q = 1.0f;           // 0.3 - 5.0
    float stereoInvert = 0.0f;      // 0.0 - 1.0 (stereo anti-correlation)
    float resonance = 0.0f;         // 0.0 - 1.0 (metallic/resonant coloration)
    float echoPingPong = 0.0f;      // 0.0 - 1.0 (cross-channel echo feedback)
    float dynAmount = 0.0f;         // -1.0 - +1.0 (sidechain dynamics)
    float dynSpeed = 0.5f;          // 0.0 - 1.0 (envelope follower speed)
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
// 192 factory presets matched from PCM 90 impulse responses
// Generated: 2026-02-11
// Average match score: 90.3%
inline std::vector<Preset> getFactoryPresets()
{
    std::vector<Preset> presets;
    presets.reserve(192);

    // ==================== HALLS (59) ====================

    // Two different shaped ballrooms (match: 90%)
    presets.push_back({
        "Ballrooms",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.8975f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.80f,  // modRate Hz
        0.2551f,  // modDepth
        0.9987f,  // width
        0.7128f,  // earlyDiff
        0.5509f,  // lateDiff
        1.00f,  // bassMult
        229.0f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        19907.3f,  // highCut Hz
        false,
        0.5777f,  // roomSize
        0.4000f,  // earlyLateBal
        1.11f,  // highDecay
        1.01f,  // midDecay
        2171.3f,  // highFreq Hz
        0.9891f,  // erShape
        0.0000f,  // erSpread
        20.0f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1902f,  // stereoCoupling
        2470.8f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        1,  // envMode (Gate)
        867.8f,  // envHold ms
        2463.6f,  // envRelease ms
        96.2f,  // envDepth %
        56.1f,  // echoDelay ms
        0.0f,  // echoFeedback %
        929.9f,  // outEQ1Freq Hz
        -11.97f,  // outEQ1Gain dB
        1.01f,  // outEQ1Q
        4007.7f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.0000f,  // dynAmount
        0.2498f  // dynSpeed
    });

    // Wide and abrupt sounding, gated (match: 96%)
    presets.push_back({
        "Brick Wallz",
        "Halls",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.0000f,  // damping
        70.3f,  // predelay ms
        0.30f,  // mix
        0.80f,  // modRate Hz
        0.2391f,  // modDepth
        0.9999f,  // width
        0.6604f,  // earlyDiff
        0.0375f,  // lateDiff
        0.71f,  // bassMult
        101.9f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        19997.6f,  // highCut Hz
        false,
        0.8749f,  // roomSize
        0.9000f,  // earlyLateBal
        1.97f,  // highDecay
        0.42f,  // midDecay
        2919.0f,  // highFreq Hz
        0.9643f,  // erShape
        0.2753f,  // erSpread
        81.1f,  // erBassCut Hz
        1.17f,  // trebleRatio
        0.1471f,  // stereoCoupling
        1877.8f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        1,  // envMode (Gate)
        500.2f,  // envHold ms
        500.5f,  // envRelease ms
        80.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        919.6f,  // outEQ1Freq Hz
        -9.45f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4213.2f,  // outEQ2Freq Hz
        -12.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.6251f,  // resonance
        0.0000f,  // echoPingPong
        -0.0000f,  // dynAmount
        0.4999f  // dynSpeed
    });

    // Light reverb, great deal of high end (match: 96%)
    presets.push_back({
        "Bright Hall",
        "Halls",
        6,  // Bright Hall
        0,  // 1970s
        0.1733f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.76f,  // modRate Hz
        0.2549f,  // modDepth
        1.0000f,  // width
        0.1860f,  // earlyDiff
        0.5136f,  // lateDiff
        0.63f,  // bassMult
        966.1f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.9972f,  // roomSize
        0.6000f,  // earlyLateBal
        4.00f,  // highDecay
        1.00f,  // midDecay
        7126.0f,  // highFreq Hz
        0.8991f,  // erShape
        0.3888f,  // erSpread
        119.0f,  // erBassCut Hz
        1.81f,  // trebleRatio
        0.1489f,  // stereoCoupling
        4647.1f,  // lowMidFreq Hz
        0.77f,  // lowMidDecay
        1,  // envMode (Gate)
        1106.1f,  // envHold ms
        1953.9f,  // envRelease ms
        87.4f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        3062.5f,  // outEQ1Freq Hz
        -8.85f,  // outEQ1Gain dB
        0.99f,  // outEQ1Q
        4790.4f,  // outEQ2Freq Hz
        -3.01f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6250f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Medium-sized room, sharp medium long decay (match: 95%)
    presets.push_back({
        "Cannon Gate",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.0752f,  // size
        0.1500f,  // damping
        51.8f,  // predelay ms
        0.30f,  // mix
        0.83f,  // modRate Hz
        0.2540f,  // modDepth
        1.0000f,  // width
        0.4120f,  // earlyDiff
        0.2835f,  // lateDiff
        0.70f,  // bassMult
        656.3f,  // bassFreq Hz
        84.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.9390f,  // roomSize
        0.6000f,  // earlyLateBal
        2.48f,  // highDecay
        1.36f,  // midDecay
        6546.9f,  // highFreq Hz
        0.6972f,  // erShape
        0.5954f,  // erSpread
        163.4f,  // erBassCut Hz
        1.50f,  // trebleRatio
        0.1138f,  // stereoCoupling
        3047.0f,  // lowMidFreq Hz
        0.73f,  // lowMidDecay
        2,  // envMode (Reverse)
        1011.3f,  // envHold ms
        383.9f,  // envRelease ms
        88.0f,  // envDepth %
        252.8f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1005.5f,  // outEQ1Freq Hz
        -10.96f,  // outEQ1Gain dB
        1.01f,  // outEQ1Q
        4644.9f,  // outEQ2Freq Hz
        -7.92f,  // outEQ2Gain dB
        1.01f,  // outEQ2Q
        0.5976f,  // stereoInvert
        0.5979f,  // resonance
        0.0000f,  // echoPingPong
        -0.9949f,  // dynAmount
        0.4977f  // dynSpeed
    });

    // Medium-sized space with lots of reflections (match: 95%)
    presets.push_back({
        "Choir Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.5010f,  // damping
        10.6f,  // predelay ms
        0.30f,  // mix
        0.80f,  // modRate Hz
        0.2505f,  // modDepth
        1.0000f,  // width
        0.7025f,  // earlyDiff
        0.4989f,  // lateDiff
        1.11f,  // bassMult
        212.7f,  // bassFreq Hz
        99.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6210f,  // roomSize
        0.7000f,  // earlyLateBal
        0.62f,  // highDecay
        0.95f,  // midDecay
        2103.4f,  // highFreq Hz
        0.9910f,  // erShape
        0.0000f,  // erSpread
        140.2f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1488f,  // stereoCoupling
        1780.7f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        503.2f,  // envHold ms
        509.8f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1002.1f,  // outEQ1Freq Hz
        0.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4008.2f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6007f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -0.6007f,  // dynAmount
        0.2503f  // dynSpeed
    });

    // Dense, classic Lexicon hall (match: 97%)
    presets.push_back({
        "Concert Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.3831f,  // size
        0.6250f,  // damping
        31.1f,  // predelay ms
        0.30f,  // mix
        0.81f,  // modRate Hz
        0.2487f,  // modDepth
        0.9643f,  // width
        0.2555f,  // earlyDiff
        0.4988f,  // lateDiff
        1.07f,  // bassMult
        972.2f,  // bassFreq Hz
        20.4f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.4984f,  // roomSize
        0.4000f,  // earlyLateBal
        4.00f,  // highDecay
        0.88f,  // midDecay
        7217.8f,  // highFreq Hz
        0.5870f,  // erShape
        0.1041f,  // erSpread
        152.0f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1490f,  // stereoCoupling
        6815.5f,  // lowMidFreq Hz
        0.99f,  // lowMidDecay
        0,  // envMode (Off)
        500.8f,  // envHold ms
        500.8f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1009.8f,  // outEQ1Freq Hz
        0.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4006.0f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6009f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        1.0000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Medium bright hall (match: 93%)
    presets.push_back({
        "Dance Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.0752f,  // size
        0.8739f,  // damping
        0.9f,  // predelay ms
        0.30f,  // mix
        0.53f,  // modRate Hz
        0.1573f,  // modDepth
        1.0000f,  // width
        0.9287f,  // earlyDiff
        0.5506f,  // lateDiff
        1.55f,  // bassMult
        434.3f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.1246f,  // roomSize
        0.7000f,  // earlyLateBal
        4.00f,  // highDecay
        1.01f,  // midDecay
        9646.9f,  // highFreq Hz
        0.5784f,  // erShape
        0.8453f,  // erSpread
        140.2f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1493f,  // stereoCoupling
        3461.4f,  // lowMidFreq Hz
        1.03f,  // lowMidDecay
        0,  // envMode (Off)
        503.0f,  // envHold ms
        503.0f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        9.0f,  // echoFeedback %
        919.7f,  // outEQ1Freq Hz
        -3.00f,  // outEQ1Gain dB
        1.01f,  // outEQ1Q
        4047.2f,  // outEQ2Freq Hz
        -3.00f,  // outEQ2Gain dB
        1.01f,  // outEQ2Q
        0.6876f,  // stereoInvert
        0.1075f,  // resonance
        0.0000f,  // echoPingPong
        -0.6007f,  // dynAmount
        0.2503f  // dynSpeed
    });

    // All-purpose hall, moderate size/decay (match: 91%)
    presets.push_back({
        "Deep Blue",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.2483f,  // size
        0.2904f,  // damping
        22.1f,  // predelay ms
        0.30f,  // mix
        0.10f,  // modRate Hz
        0.2808f,  // modDepth
        1.0000f,  // width
        0.5631f,  // earlyDiff
        0.5467f,  // lateDiff
        1.46f,  // bassMult
        434.7f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        13498.5f,  // highCut Hz
        false,
        0.5617f,  // roomSize
        1.0000f,  // earlyLateBal
        1.32f,  // highDecay
        0.78f,  // midDecay
        1001.1f,  // highFreq Hz
        1.0000f,  // erShape
        0.0000f,  // erSpread
        20.0f,  // erBassCut Hz
        0.90f,  // trebleRatio
        0.1531f,  // stereoCoupling
        976.0f,  // lowMidFreq Hz
        1.01f,  // lowMidDecay
        0,  // envMode (Off)
        553.0f,  // envHold ms
        503.1f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1002.3f,  // outEQ1Freq Hz
        -1.30f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        999.7f,  // outEQ2Freq Hz
        0.09f,  // outEQ2Gain dB
        0.49f,  // outEQ2Q
        0.5422f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.8646f,  // dynAmount
        0.2492f  // dynSpeed
    });

    // Large, washy, chorused space (match: 97%)
    presets.push_back({
        "Deep Verb",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.0752f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        1.18f,  // modRate Hz
        0.1736f,  // modDepth
        0.9624f,  // width
        0.4914f,  // earlyDiff
        0.5920f,  // lateDiff
        1.18f,  // bassMult
        137.9f,  // bassFreq Hz
        20.4f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.1057f,  // roomSize
        0.7000f,  // earlyLateBal
        0.92f,  // highDecay
        1.01f,  // midDecay
        1000.3f,  // highFreq Hz
        1.0000f,  // erShape
        0.2501f,  // erSpread
        158.6f,  // erBassCut Hz
        1.05f,  // trebleRatio
        0.1684f,  // stereoCoupling
        871.0f,  // lowMidFreq Hz
        1.01f,  // lowMidDecay
        0,  // envMode (Off)
        502.4f,  // envHold ms
        502.8f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1130.1f,  // outEQ1Freq Hz
        -11.11f,  // outEQ1Gain dB
        0.99f,  // outEQ1Q
        4013.6f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6013f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2502f  // dynSpeed
    });

    // Split, short ping-pong delay, medium-long hallway (match: 79%)
    presets.push_back({
        "Delay Hallway",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.0000f,  // size
        0.6222f,  // damping
        23.5f,  // predelay ms
        0.30f,  // mix
        0.81f,  // modRate Hz
        0.2507f,  // modDepth
        0.9179f,  // width
        0.5496f,  // earlyDiff
        0.4988f,  // lateDiff
        1.60f,  // bassMult
        563.9f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        19828.4f,  // highCut Hz
        false,
        0.6222f,  // roomSize
        0.6000f,  // earlyLateBal
        1.34f,  // highDecay
        1.00f,  // midDecay
        4031.0f,  // highFreq Hz
        0.4996f,  // erShape
        0.7504f,  // erSpread
        80.0f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.0000f,  // stereoCoupling
        2075.6f,  // lowMidFreq Hz
        0.99f,  // lowMidDecay
        1,  // envMode (Gate)
        507.5f,  // envHold ms
        383.7f,  // envRelease ms
        79.3f,  // envDepth %
        125.5f,  // echoDelay ms
        0.0f,  // echoFeedback %
        5826.8f,  // outEQ1Freq Hz
        3.01f,  // outEQ1Gain dB
        1.01f,  // outEQ1Q
        4009.7f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.3254f,  // stereoInvert
        0.6010f,  // resonance
        0.1243f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.5002f  // dynSpeed
    });

    // Bright, crystalline hall with subtle delay taps (match: 77%)
    presets.push_back({
        "Dream Hall",
        "Halls",
        6,  // Bright Hall
        0,  // 1970s
        0.0752f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        1.52f,  // modRate Hz
        0.2529f,  // modDepth
        0.9859f,  // width
        0.5372f,  // earlyDiff
        0.7339f,  // lateDiff
        1.55f,  // bassMult
        279.1f,  // bassFreq Hz
        196.5f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.7753f,  // roomSize
        0.6000f,  // earlyLateBal
        1.33f,  // highDecay
        0.99f,  // midDecay
        7872.4f,  // highFreq Hz
        0.7501f,  // erShape
        0.4635f,  // erSpread
        20.0f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.0951f,  // stereoCoupling
        2200.9f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        500.6f,  // envHold ms
        500.6f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1001.4f,  // outEQ1Freq Hz
        0.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        3289.2f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6007f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.6006f,  // dynAmount
        0.2502f  // dynSpeed
    });

    // Medium sized cave, short decay time (match: 95%)
    presets.push_back({
        "Drum Cave",
        "Halls",
        1,  // Room
        0,  // 1970s
        0.2483f,  // size
        0.2376f,  // damping
        93.7f,  // predelay ms
        0.30f,  // mix
        0.79f,  // modRate Hz
        0.2535f,  // modDepth
        1.0000f,  // width
        0.9156f,  // earlyDiff
        0.4256f,  // lateDiff
        1.18f,  // bassMult
        662.5f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.7505f,  // roomSize
        1.0000f,  // earlyLateBal
        0.94f,  // highDecay
        0.65f,  // midDecay
        1000.0f,  // highFreq Hz
        0.4972f,  // erShape
        0.5198f,  // erSpread
        20.0f,  // erBassCut Hz
        0.94f,  // trebleRatio
        0.1499f,  // stereoCoupling
        100.0f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        500.0f,  // envHold ms
        500.0f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        105.8f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        3062.5f,  // outEQ2Freq Hz
        -1.85f,  // outEQ2Gain dB
        1.01f,  // outEQ2Q
        0.3000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.0000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Typical Monday night at the club (match: 97%)
    presets.push_back({
        "Empty Club",
        "Halls",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.0000f,  // damping
        37.9f,  // predelay ms
        0.30f,  // mix
        0.81f,  // modRate Hz
        0.3315f,  // modDepth
        1.0000f,  // width
        0.5755f,  // earlyDiff
        0.3241f,  // lateDiff
        1.94f,  // bassMult
        478.7f,  // bassFreq Hz
        81.5f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.3752f,  // roomSize
        0.6000f,  // earlyLateBal
        1.38f,  // highDecay
        1.01f,  // midDecay
        5127.9f,  // highFreq Hz
        0.4282f,  // erShape
        0.3742f,  // erSpread
        271.2f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1257f,  // stereoCoupling
        100.1f,  // lowMidFreq Hz
        0.95f,  // lowMidDecay
        1,  // envMode (Gate)
        299.2f,  // envHold ms
        384.0f,  // envRelease ms
        78.5f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        2094.0f,  // outEQ1Freq Hz
        7.74f,  // outEQ1Gain dB
        1.02f,  // outEQ1Q
        4016.2f,  // outEQ2Freq Hz
        -5.51f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6254f,  // stereoInvert
        0.6003f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2501f  // dynSpeed
    });

    // Large, dense room reverb for toms (match: 90%)
    presets.push_back({
        "For The Toms",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.0000f,  // size
        0.1378f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.86f,  // modRate Hz
        0.2449f,  // modDepth
        1.0000f,  // width
        0.7506f,  // earlyDiff
        0.3728f,  // lateDiff
        2.28f,  // bassMult
        196.0f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        19999.9f,  // highCut Hz
        false,
        0.1246f,  // roomSize
        0.9000f,  // earlyLateBal
        1.61f,  // highDecay
        1.63f,  // midDecay
        2370.4f,  // highFreq Hz
        0.5012f,  // erShape
        0.8204f,  // erSpread
        153.7f,  // erBassCut Hz
        1.15f,  // trebleRatio
        0.1809f,  // stereoCoupling
        1777.3f,  // lowMidFreq Hz
        1.01f,  // lowMidDecay
        0,  // envMode (Off)
        500.8f,  // envHold ms
        91.2f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        569.7f,  // outEQ1Freq Hz
        -3.75f,  // outEQ1Gain dB
        0.99f,  // outEQ1Q
        3963.2f,  // outEQ2Freq Hz
        -0.00f,  // outEQ2Gain dB
        0.45f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.0000f,  // dynAmount
        0.5006f  // dynSpeed
    });

    // If possible to have a gated hall (match: 84%)
    presets.push_back({
        "Gated Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.7260f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        1.23f,  // modRate Hz
        0.3664f,  // modDepth
        0.9601f,  // width
        0.6282f,  // earlyDiff
        0.6228f,  // lateDiff
        0.10f,  // bassMult
        1000.0f,  // bassFreq Hz
        58.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.7731f,  // roomSize
        0.7000f,  // earlyLateBal
        2.13f,  // highDecay
        0.25f,  // midDecay
        2743.5f,  // highFreq Hz
        0.3980f,  // erShape
        0.6668f,  // erSpread
        276.6f,  // erBassCut Hz
        0.94f,  // trebleRatio
        0.5000f,  // stereoCoupling
        1097.9f,  // lowMidFreq Hz
        0.52f,  // lowMidDecay
        1,  // envMode (Gate)
        293.0f,  // envHold ms
        200.6f,  // envRelease ms
        99.8f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        2078.0f,  // outEQ1Freq Hz
        -5.40f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4102.0f,  // outEQ2Freq Hz
        -1.85f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2502f  // dynSpeed
    });

    // Generic concert hall, starting place (match: 93%)
    presets.push_back({
        "Gen. Concert",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.2483f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        1.15f,  // modRate Hz
        0.4905f,  // modDepth
        0.9598f,  // width
        0.0376f,  // earlyDiff
        0.5008f,  // lateDiff
        1.31f,  // bassMult
        550.0f,  // bassFreq Hz
        134.6f,  // lowCut Hz
        18590.5f,  // highCut Hz
        false,
        0.8731f,  // roomSize
        0.5000f,  // earlyLateBal
        4.00f,  // highDecay
        0.92f,  // midDecay
        4300.0f,  // highFreq Hz
        0.4970f,  // erShape
        0.9261f,  // erSpread
        20.1f,  // erBassCut Hz
        1.58f,  // trebleRatio
        0.1511f,  // stereoCoupling
        7012.5f,  // lowMidFreq Hz
        0.99f,  // lowMidDecay
        0,  // envMode (Off)
        501.7f,  // envHold ms
        501.6f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        4.8f,  // echoFeedback %
        448.7f,  // outEQ1Freq Hz
        -5.55f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4000.4f,  // outEQ2Freq Hz
        -0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.7160f,  // stereoInvert
        0.0679f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Generic hall with random reflections (match: 95%)
    presets.push_back({
        "Gen. Random Hall",
        "Halls",
        8,  // Random Space
        0,  // 1970s
        0.1733f,  // size
        0.0376f,  // damping
        62.5f,  // predelay ms
        0.30f,  // mix
        0.28f,  // modRate Hz
        0.2253f,  // modDepth
        1.0000f,  // width
        0.0000f,  // earlyDiff
        0.5308f,  // lateDiff
        1.17f,  // bassMult
        133.8f,  // bassFreq Hz
        20.4f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        1.0000f,  // roomSize
        0.3000f,  // earlyLateBal
        4.00f,  // highDecay
        1.05f,  // midDecay
        6189.5f,  // highFreq Hz
        0.5010f,  // erShape
        0.2152f,  // erSpread
        202.0f,  // erBassCut Hz
        1.80f,  // trebleRatio
        0.1497f,  // stereoCoupling
        3656.8f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        501.1f,  // envHold ms
        500.9f,  // envRelease ms
        0.0f,  // envDepth %
        151.9f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1100.2f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        3357.9f,  // outEQ2Freq Hz
        -6.65f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.3006f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -0.6003f,  // dynAmount
        0.5003f  // dynSpeed
    });

    // Quick solution, well rounded reverb (match: 96%)
    presets.push_back({
        "Good Ol' Verb",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.7464f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.71f,  // modRate Hz
        0.1375f,  // modDepth
        1.0000f,  // width
        0.0753f,  // earlyDiff
        0.4728f,  // lateDiff
        1.41f,  // bassMult
        1000.0f,  // bassFreq Hz
        57.6f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.3718f,  // roomSize
        0.4000f,  // earlyLateBal
        0.63f,  // highDecay
        1.00f,  // midDecay
        2168.1f,  // highFreq Hz
        0.6272f,  // erShape
        0.0750f,  // erSpread
        500.0f,  // erBassCut Hz
        1.17f,  // trebleRatio
        0.1478f,  // stereoCoupling
        2073.5f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        358.4f,  // envHold ms
        505.1f,  // envRelease ms
        0.0f,  // envDepth %
        62.6f,  // echoDelay ms
        0.0f,  // echoFeedback %
        918.0f,  // outEQ1Freq Hz
        -2.71f,  // outEQ1Gain dB
        0.36f,  // outEQ1Q
        4020.0f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.01f,  // outEQ2Q
        0.3252f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -0.6014f,  // dynAmount
        0.2506f  // dynSpeed
    });

    // Large, filtered, medium-bright hall of stone (match: 94%)
    presets.push_back({
        "Gothic Hall",
        "Halls",
        4,  // Cathedral
        0,  // 1970s
        0.3132f,  // size
        0.8693f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.79f,  // modRate Hz
        0.4408f,  // modDepth
        1.0000f,  // width
        0.5958f,  // earlyDiff
        0.5449f,  // lateDiff
        0.85f,  // bassMult
        437.5f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        16634.0f,  // highCut Hz
        false,
        0.3699f,  // roomSize
        1.0000f,  // earlyLateBal
        2.97f,  // highDecay
        0.94f,  // midDecay
        2086.8f,  // highFreq Hz
        0.9815f,  // erShape
        0.0000f,  // erSpread
        440.0f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1445f,  // stereoCoupling
        2091.0f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        500.8f,  // envHold ms
        500.9f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        2411.9f,  // outEQ1Freq Hz
        3.05f,  // outEQ1Gain dB
        1.01f,  // outEQ1Q
        2478.2f,  // outEQ2Freq Hz
        1.21f,  // outEQ2Gain dB
        0.99f,  // outEQ2Q
        0.6009f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.5000f  // dynSpeed
    });

    // Great hall reverb, works with all material (match: 96%)
    presets.push_back({
        "Great Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.1593f,  // damping
        9.0f,  // predelay ms
        0.30f,  // mix
        1.12f,  // modRate Hz
        0.2554f,  // modDepth
        0.9695f,  // width
        0.1250f,  // earlyDiff
        0.3760f,  // lateDiff
        0.97f,  // bassMult
        1000.0f,  // bassFreq Hz
        43.2f,  // lowCut Hz
        19971.8f,  // highCut Hz
        false,
        0.8662f,  // roomSize
        0.6000f,  // earlyLateBal
        1.20f,  // highDecay
        0.93f,  // midDecay
        3089.3f,  // highFreq Hz
        0.4885f,  // erShape
        1.0000f,  // erSpread
        20.2f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1467f,  // stereoCoupling
        5041.4f,  // lowMidFreq Hz
        0.96f,  // lowMidDecay
        0,  // envMode (Off)
        504.3f,  // envHold ms
        504.2f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1089.0f,  // outEQ1Freq Hz
        -6.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4015.1f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6250f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -0.7249f,  // dynAmount
        0.5000f  // dynSpeed
    });

    // Medium-sized room, 2-second reverb (match: 87%)
    presets.push_back({
        "Guitar Ballad",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.2483f,  // size
        0.7293f,  // damping
        217.8f,  // predelay ms
        0.30f,  // mix
        0.84f,  // modRate Hz
        0.2590f,  // modDepth
        1.0000f,  // width
        0.8168f,  // earlyDiff
        0.5024f,  // lateDiff
        1.11f,  // bassMult
        190.7f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6973f,  // roomSize
        0.4000f,  // earlyLateBal
        3.69f,  // highDecay
        0.72f,  // midDecay
        2103.4f,  // highFreq Hz
        0.0376f,  // erShape
        0.6223f,  // erSpread
        152.3f,  // erBassCut Hz
        0.37f,  // trebleRatio
        0.1378f,  // stereoCoupling
        941.1f,  // lowMidFreq Hz
        1.03f,  // lowMidDecay
        3,  // envMode (Swell)
        1006.0f,  // envHold ms
        1449.0f,  // envRelease ms
        91.6f,  // envDepth %
        121.9f,  // echoDelay ms
        0.0f,  // echoFeedback %
        509.7f,  // outEQ1Freq Hz
        -7.06f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        3554.9f,  // outEQ2Freq Hz
        -4.46f,  // outEQ2Gain dB
        1.02f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.7510f,  // dynAmount
        0.5007f  // dynSpeed
    });

    // Long predelay with recirculating echoes (match: 85%)
    presets.push_back({
        "Guitar Cave",
        "Halls",
        1,  // Room
        0,  // 1970s
        0.3719f,  // size
        0.0040f,  // damping
        213.4f,  // predelay ms
        0.30f,  // mix
        0.79f,  // modRate Hz
        0.2702f,  // modDepth
        0.9520f,  // width
        0.8754f,  // earlyDiff
        0.5776f,  // lateDiff
        0.81f,  // bassMult
        428.1f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        18664.7f,  // highCut Hz
        false,
        0.3722f,  // roomSize
        1.0000f,  // earlyLateBal
        3.19f,  // highDecay
        0.50f,  // midDecay
        1475.3f,  // highFreq Hz
        0.0000f,  // erShape
        1.0000f,  // erSpread
        72.0f,  // erBassCut Hz
        1.80f,  // trebleRatio
        0.1481f,  // stereoCoupling
        2103.2f,  // lowMidFreq Hz
        1.01f,  // lowMidDecay
        0,  // envMode (Off)
        400.1f,  // envHold ms
        503.9f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        100.0f,  // outEQ1Freq Hz
        10.43f,  // outEQ1Gain dB
        1.06f,  // outEQ1Q
        1668.1f,  // outEQ2Freq Hz
        12.00f,  // outEQ2Gain dB
        1.01f,  // outEQ2Q
        0.6003f,  // stereoInvert
        0.0414f,  // resonance
        0.0000f,  // echoPingPong
        0.7503f,  // dynAmount
        0.2501f  // dynSpeed
    });

    // Very large space, ideal for horns (match: 95%)
    presets.push_back({
        "Horns Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.82f,  // modRate Hz
        0.2670f,  // modDepth
        0.9401f,  // width
        0.1977f,  // earlyDiff
        0.3725f,  // lateDiff
        0.96f,  // bassMult
        243.8f,  // bassFreq Hz
        28.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        1.0000f,  // roomSize
        0.6000f,  // earlyLateBal
        1.34f,  // highDecay
        0.93f,  // midDecay
        1953.2f,  // highFreq Hz
        0.4128f,  // erShape
        1.0000f,  // erSpread
        20.0f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1885f,  // stereoCoupling
        100.2f,  // lowMidFreq Hz
        1.01f,  // lowMidDecay
        0,  // envMode (Off)
        855.3f,  // envHold ms
        500.4f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        2077.9f,  // outEQ1Freq Hz
        -10.05f,  // outEQ1Gain dB
        0.76f,  // outEQ1Q
        4055.7f,  // outEQ2Freq Hz
        -1.54f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0258f,  // echoPingPong
        0.7511f,  // dynAmount
        0.2504f  // dynSpeed
    });

    // Large hall with stage reflections (match: 96%)
    presets.push_back({
        "Large Hall+Stage",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.6140f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.80f,  // modRate Hz
        0.4462f,  // modDepth
        0.9526f,  // width
        0.2568f,  // earlyDiff
        0.5462f,  // lateDiff
        0.72f,  // bassMult
        600.1f,  // bassFreq Hz
        47.9f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.8740f,  // roomSize
        0.6000f,  // earlyLateBal
        4.00f,  // highDecay
        0.95f,  // midDecay
        8497.9f,  // highFreq Hz
        0.7564f,  // erShape
        0.0752f,  // erSpread
        20.1f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1512f,  // stereoCoupling
        6825.4f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        501.1f,  // envHold ms
        539.6f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        874.1f,  // outEQ1Freq Hz
        -5.41f,  // outEQ1Gain dB
        1.13f,  // outEQ1Q
        4008.3f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0881f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2504f  // dynSpeed
    });

    // Split with empty and full hall (match: 80%)
    presets.push_back({
        "Lecture Halls",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.0752f,  // size
        0.7249f,  // damping
        70.9f,  // predelay ms
        0.30f,  // mix
        0.79f,  // modRate Hz
        0.2517f,  // modDepth
        0.9632f,  // width
        0.5294f,  // earlyDiff
        0.6282f,  // lateDiff
        1.60f,  // bassMult
        438.5f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.7146f,  // roomSize
        0.6000f,  // earlyLateBal
        0.90f,  // highDecay
        1.01f,  // midDecay
        3345.4f,  // highFreq Hz
        0.5012f,  // erShape
        0.2506f,  // erSpread
        302.7f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1512f,  // stereoCoupling
        2475.8f,  // lowMidFreq Hz
        1.01f,  // lowMidDecay
        0,  // envMode (Off)
        502.3f,  // envHold ms
        501.0f,  // envRelease ms
        0.0f,  // envDepth %
        18.9f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1003.1f,  // outEQ1Freq Hz
        0.00f,  // outEQ1Gain dB
        0.65f,  // outEQ1Q
        4418.4f,  // outEQ2Freq Hz
        3.02f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.6014f,  // resonance
        0.0000f,  // echoPingPong
        0.7518f,  // dynAmount
        0.2506f  // dynSpeed
    });

    // Very large hall, moderate decay (match: 97%)
    presets.push_back({
        "Live Arena",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.0752f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.84f,  // modRate Hz
        0.2581f,  // modDepth
        1.0000f,  // width
        0.2964f,  // earlyDiff
        0.3874f,  // lateDiff
        1.51f,  // bassMult
        195.8f,  // bassFreq Hz
        20.5f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.7530f,  // roomSize
        0.6000f,  // earlyLateBal
        2.71f,  // highDecay
        1.04f,  // midDecay
        2985.7f,  // highFreq Hz
        0.5000f,  // erShape
        0.8777f,  // erSpread
        260.8f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1923f,  // stereoCoupling
        2942.5f,  // lowMidFreq Hz
        1.29f,  // lowMidDecay
        0,  // envMode (Off)
        503.3f,  // envHold ms
        499.9f,  // envRelease ms
        0.0f,  // envDepth %
        125.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1445.8f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        0.99f,  // outEQ1Q
        4052.5f,  // outEQ2Freq Hz
        -7.29f,  // outEQ2Gain dB
        1.01f,  // outEQ2Q
        0.2990f,  // stereoInvert
        0.6007f,  // resonance
        0.0000f,  // echoPingPong
        -0.6004f,  // dynAmount
        0.5004f  // dynSpeed
    });

    // Liveness controls let you design your room (match: 94%)
    presets.push_back({
        "Make-A-Space",
        "Halls",
        1,  // Room
        0,  // 1970s
        0.3719f,  // size
        0.4334f,  // damping
        2.6f,  // predelay ms
        0.30f,  // mix
        0.81f,  // modRate Hz
        0.2554f,  // modDepth
        1.0000f,  // width
        0.7746f,  // earlyDiff
        0.5495f,  // lateDiff
        1.51f,  // bassMult
        325.9f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5745f,  // roomSize
        0.5000f,  // earlyLateBal
        0.72f,  // highDecay
        0.95f,  // midDecay
        4028.7f,  // highFreq Hz
        0.4004f,  // erShape
        0.6243f,  // erSpread
        20.0f,  // erBassCut Hz
        1.41f,  // trebleRatio
        0.1496f,  // stereoCoupling
        3658.4f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        1,  // envMode (Gate)
        1120.4f,  // envHold ms
        915.6f,  // envRelease ms
        72.6f,  // envDepth %
        39.1f,  // echoDelay ms
        20.2f,  // echoFeedback %
        1374.4f,  // outEQ1Freq Hz
        9.01f,  // outEQ1Gain dB
        1.01f,  // outEQ1Q
        8000.0f,  // outEQ2Freq Hz
        3.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.1295f,  // resonance
        0.0000f,  // echoPingPong
        0.7507f,  // dynAmount
        0.5005f  // dynSpeed
    });

    // Medium hall with stage reflections (match: 96%)
    presets.push_back({
        "Med Hall+Stage",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.0752f,  // size
        0.6268f,  // damping
        10.8f,  // predelay ms
        0.30f,  // mix
        0.80f,  // modRate Hz
        0.2496f,  // modDepth
        0.9708f,  // width
        0.4994f,  // earlyDiff
        0.4761f,  // lateDiff
        0.85f,  // bassMult
        190.4f,  // bassFreq Hz
        22.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5986f,  // roomSize
        0.6000f,  // earlyLateBal
        3.67f,  // highDecay
        1.02f,  // midDecay
        7809.0f,  // highFreq Hz
        0.5052f,  // erShape
        0.1597f,  // erSpread
        20.0f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1504f,  // stereoCoupling
        7577.0f,  // lowMidFreq Hz
        1.01f,  // lowMidDecay
        0,  // envMode (Off)
        500.0f,  // envHold ms
        500.7f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        898.2f,  // outEQ1Freq Hz
        -1.84f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4052.5f,  // outEQ2Freq Hz
        -1.85f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.5995f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.5003f  // dynSpeed
    });

    // Natural medium-size hall (match: 97%)
    presets.push_back({
        "Medium Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.4506f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.33f,  // modRate Hz
        0.1744f,  // modDepth
        1.0000f,  // width
        0.6250f,  // earlyDiff
        0.5063f,  // lateDiff
        1.18f,  // bassMult
        350.5f,  // bassFreq Hz
        20.3f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.4977f,  // roomSize
        0.6000f,  // earlyLateBal
        1.15f,  // highDecay
        0.99f,  // midDecay
        1001.2f,  // highFreq Hz
        0.7131f,  // erShape
        0.5835f,  // erSpread
        92.1f,  // erBassCut Hz
        1.14f,  // trebleRatio
        0.1494f,  // stereoCoupling
        1539.4f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        504.8f,  // envHold ms
        508.0f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1002.4f,  // outEQ1Freq Hz
        0.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        2943.1f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        0.40f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.6000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Short, boomny, and bright, like anechoic chamber (match: 94%)
    presets.push_back({
        "Metal Chamber",
        "Halls",
        3,  // Chamber
        0,  // 1970s
        0.2049f,  // size
        1.0000f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.80f,  // modRate Hz
        0.3561f,  // modDepth
        0.9683f,  // width
        0.4717f,  // earlyDiff
        0.5481f,  // lateDiff
        1.30f,  // bassMult
        400.4f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        19679.0f,  // highCut Hz
        false,
        0.5001f,  // roomSize
        0.6000f,  // earlyLateBal
        0.25f,  // highDecay
        0.71f,  // midDecay
        1000.0f,  // highFreq Hz
        0.5013f,  // erShape
        0.2502f,  // erSpread
        71.0f,  // erBassCut Hz
        1.01f,  // trebleRatio
        0.1490f,  // stereoCoupling
        882.5f,  // lowMidFreq Hz
        0.96f,  // lowMidDecay
        0,  // envMode (Off)
        1006.6f,  // envHold ms
        2264.4f,  // envRelease ms
        98.3f,  // envDepth %
        56.2f,  // echoDelay ms
        0.0f,  // echoFeedback %
        510.8f,  // outEQ1Freq Hz
        3.18f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        6051.6f,  // outEQ2Freq Hz
        -1.87f,  // outEQ2Gain dB
        1.01f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.3003f,  // resonance
        0.0000f,  // echoPingPong
        0.6012f,  // dynAmount
        0.2511f  // dynSpeed
    });

    // Reverberant hall like a large room in a museum (match: 97%)
    presets.push_back({
        "Museum Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.1036f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        1.32f,  // modRate Hz
        0.2403f,  // modDepth
        0.6513f,  // width
        0.1757f,  // earlyDiff
        0.2498f,  // lateDiff
        1.21f,  // bassMult
        285.8f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.7904f,  // roomSize
        0.6000f,  // earlyLateBal
        1.65f,  // highDecay
        0.82f,  // midDecay
        8896.1f,  // highFreq Hz
        0.5687f,  // erShape
        0.2955f,  // erSpread
        74.2f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1515f,  // stereoCoupling
        1286.8f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        500.9f,  // envHold ms
        502.2f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        896.5f,  // outEQ1Freq Hz
        1.80f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        3443.2f,  // outEQ2Freq Hz
        -5.99f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Acoustics of two famous NYC nightclubs (match: 95%)
    presets.push_back({
        "NYC Clubs",
        "Halls",
        1,  // Room
        0,  // 1970s
        0.2199f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.81f,  // modRate Hz
        0.2528f,  // modDepth
        1.0000f,  // width
        0.5002f,  // earlyDiff
        0.5531f,  // lateDiff
        1.19f,  // bassMult
        775.3f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        1.0000f,  // roomSize
        0.6000f,  // earlyLateBal
        4.00f,  // highDecay
        0.93f,  // midDecay
        8089.3f,  // highFreq Hz
        0.8481f,  // erShape
        0.1380f,  // erSpread
        85.9f,  // erBassCut Hz
        1.77f,  // trebleRatio
        0.1629f,  // stereoCoupling
        8000.0f,  // lowMidFreq Hz
        0.94f,  // lowMidDecay
        1,  // envMode (Gate)
        507.6f,  // envHold ms
        542.2f,  // envRelease ms
        80.2f,  // envDepth %
        102.8f,  // echoDelay ms
        1.1f,  // echoFeedback %
        903.4f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        1.01f,  // outEQ1Q
        4531.7f,  // outEQ2Freq Hz
        -5.11f,  // outEQ2Gain dB
        1.01f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.5948f,  // resonance
        0.0000f,  // echoPingPong
        0.6002f,  // dynAmount
        0.5002f  // dynSpeed
    });

    // Dense, medium long, nonlinear gated verb (match: 86%)
    presets.push_back({
        "NonLinear #1",
        "Halls",
        9,  // Dirty Hall
        0,  // 1970s
        0.0000f,  // size
        0.0000f,  // damping
        93.9f,  // predelay ms
        0.30f,  // mix
        1.38f,  // modRate Hz
        0.1097f,  // modDepth
        1.0000f,  // width
        0.5086f,  // earlyDiff
        0.4046f,  // lateDiff
        0.54f,  // bassMult
        1000.0f,  // bassFreq Hz
        42.6f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5004f,  // roomSize
        1.0000f,  // earlyLateBal
        4.00f,  // highDecay
        2.59f,  // midDecay
        2156.7f,  // highFreq Hz
        0.0376f,  // erShape
        0.6239f,  // erSpread
        450.7f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.3924f,  // stereoCoupling
        3031.4f,  // lowMidFreq Hz
        0.92f,  // lowMidDecay
        1,  // envMode (Gate)
        497.5f,  // envHold ms
        502.2f,  // envRelease ms
        100.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1077.8f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        1.01f,  // outEQ1Q
        5055.8f,  // outEQ2Freq Hz
        -8.16f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0001f,  // echoPingPong
        0.7513f,  // dynAmount
        0.2504f  // dynSpeed
    });

    // Large nonlinear reverb, like gated warehouse (match: 95%)
    presets.push_back({
        "Nonlin Warehouse",
        "Halls",
        9,  // Dirty Hall
        0,  // 1970s
        0.0752f,  // size
        0.0376f,  // damping
        10.8f,  // predelay ms
        0.30f,  // mix
        0.75f,  // modRate Hz
        0.2517f,  // modDepth
        0.9577f,  // width
        0.6136f,  // earlyDiff
        0.4612f,  // lateDiff
        0.10f,  // bassMult
        569.7f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5990f,  // roomSize
        0.7000f,  // earlyLateBal
        1.66f,  // highDecay
        0.49f,  // midDecay
        3036.5f,  // highFreq Hz
        0.5048f,  // erShape
        0.5539f,  // erSpread
        362.6f,  // erBassCut Hz
        1.33f,  // trebleRatio
        0.0562f,  // stereoCoupling
        1218.5f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        1,  // envMode (Gate)
        1005.0f,  // envHold ms
        10.0f,  // envRelease ms
        100.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1351.4f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        3062.5f,  // outEQ2Freq Hz
        1.80f,  // outEQ2Gain dB
        1.04f,  // outEQ2Q
        0.3258f,  // stereoInvert
        0.6015f,  // resonance
        0.0000f,  // echoPingPong
        -0.6000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // LFO patched to OutWidth, subtle sweeping (match: 89%)
    presets.push_back({
        "Pan Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.0000f,  // size
        0.1067f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.79f,  // modRate Hz
        0.2535f,  // modDepth
        0.7934f,  // width
        0.6026f,  // earlyDiff
        0.1075f,  // lateDiff
        0.90f,  // bassMult
        1000.0f,  // bassFreq Hz
        93.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6066f,  // roomSize
        0.9000f,  // earlyLateBal
        2.04f,  // highDecay
        1.65f,  // midDecay
        3764.0f,  // highFreq Hz
        0.8930f,  // erShape
        0.2569f,  // erSpread
        317.9f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.3532f,  // stereoCoupling
        5302.7f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        1,  // envMode (Gate)
        506.8f,  // envHold ms
        10.0f,  // envRelease ms
        10.2f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        3037.9f,  // outEQ1Freq Hz
        -6.02f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        6387.6f,  // outEQ2Freq Hz
        -6.22f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        1.0000f,  // dynAmount
        0.2505f  // dynSpeed
    });

    // Strange, semi-gated reverb with pumping (match: 91%)
    presets.push_back({
        "Pump Verb",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.2619f,  // size
        0.0000f,  // damping
        93.9f,  // predelay ms
        0.30f,  // mix
        1.73f,  // modRate Hz
        0.2717f,  // modDepth
        0.6116f,  // width
        0.0000f,  // earlyDiff
        0.6103f,  // lateDiff
        0.95f,  // bassMult
        918.6f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        19589.1f,  // highCut Hz
        false,
        0.2933f,  // roomSize
        1.0000f,  // earlyLateBal
        1.66f,  // highDecay
        0.52f,  // midDecay
        1000.8f,  // highFreq Hz
        1.0000f,  // erShape
        0.0000f,  // erSpread
        20.1f,  // erBassCut Hz
        0.93f,  // trebleRatio
        0.1538f,  // stereoCoupling
        1223.6f,  // lowMidFreq Hz
        1.19f,  // lowMidDecay
        0,  // envMode (Off)
        507.0f,  // envHold ms
        508.6f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1374.9f,  // outEQ1Freq Hz
        -9.02f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4053.3f,  // outEQ2Freq Hz
        -1.83f,  // outEQ2Gain dB
        1.01f,  // outEQ2Q
        0.6269f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        1.0000f,  // dynAmount
        0.2502f  // dynSpeed
    });

    // Small, bright sounding hall (match: 96%)
    presets.push_back({
        "Real Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.2343f,  // size
        0.0000f,  // damping
        9.7f,  // predelay ms
        0.30f,  // mix
        0.84f,  // modRate Hz
        0.3505f,  // modDepth
        0.9631f,  // width
        0.0000f,  // earlyDiff
        0.4980f,  // lateDiff
        0.70f,  // bassMult
        728.2f,  // bassFreq Hz
        21.5f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.7360f,  // roomSize
        0.5000f,  // earlyLateBal
        3.06f,  // highDecay
        0.99f,  // midDecay
        10642.6f,  // highFreq Hz
        0.4947f,  // erShape
        1.0000f,  // erSpread
        20.1f,  // erBassCut Hz
        1.80f,  // trebleRatio
        0.1495f,  // stereoCoupling
        3062.7f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        500.5f,  // envHold ms
        500.9f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        917.0f,  // outEQ1Freq Hz
        2.25f,  // outEQ1Gain dB
        1.04f,  // outEQ1Q
        4000.0f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6001f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.0000f,  // dynAmount
        0.5000f  // dynSpeed
    });

    // Long ER rise, short decay (match: 97%)
    presets.push_back({
        "Rise'n Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.0752f,  // size
        0.0000f,  // damping
        183.9f,  // predelay ms
        0.30f,  // mix
        0.71f,  // modRate Hz
        0.2454f,  // modDepth
        1.0000f,  // width
        0.8744f,  // earlyDiff
        0.6016f,  // lateDiff
        1.54f,  // bassMult
        616.4f,  // bassFreq Hz
        20.3f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.8867f,  // roomSize
        0.7000f,  // earlyLateBal
        1.66f,  // highDecay
        1.35f,  // midDecay
        1008.3f,  // highFreq Hz
        0.0000f,  // erShape
        0.7850f,  // erSpread
        152.2f,  // erBassCut Hz
        1.00f,  // trebleRatio
        0.1509f,  // stereoCoupling
        693.3f,  // lowMidFreq Hz
        1.02f,  // lowMidDecay
        0,  // envMode (Off)
        381.8f,  // envHold ms
        501.6f,  // envRelease ms
        14.7f,  // envDepth %
        53.1f,  // echoDelay ms
        0.0f,  // echoFeedback %
        918.9f,  // outEQ1Freq Hz
        0.10f,  // outEQ1Gain dB
        0.92f,  // outEQ1Q
        4068.6f,  // outEQ2Freq Hz
        -4.81f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.5449f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.0000f,  // dynAmount
        0.2505f  // dynSpeed
    });

    // Airplane hangar for spacious sax (match: 97%)
    presets.push_back({
        "Saxy Hangar",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.2483f,  // size
        0.1264f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.79f,  // modRate Hz
        0.2542f,  // modDepth
        0.0000f,  // width
        0.4769f,  // earlyDiff
        0.5667f,  // lateDiff
        0.94f,  // bassMult
        1000.0f,  // bassFreq Hz
        20.4f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6066f,  // roomSize
        0.8000f,  // earlyLateBal
        3.02f,  // highDecay
        0.89f,  // midDecay
        9467.8f,  // highFreq Hz
        0.8505f,  // erShape
        0.8686f,  // erSpread
        237.9f,  // erBassCut Hz
        1.91f,  // trebleRatio
        0.1462f,  // stereoCoupling
        8000.0f,  // lowMidFreq Hz
        0.99f,  // lowMidDecay
        0,  // envMode (Off)
        507.9f,  // envHold ms
        502.9f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        507.0f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        0.93f,  // outEQ1Q
        3406.2f,  // outEQ2Freq Hz
        -9.01f,  // outEQ2Gain dB
        0.98f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.3004f,  // resonance
        0.0000f,  // echoPingPong
        0.7510f,  // dynAmount
        0.2503f  // dynSpeed
    });

    // Short reverse reverb, quick build up (match: 93%)
    presets.push_back({
        "Short Reverse",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.0000f,  // size
        1.0000f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.74f,  // modRate Hz
        0.3229f,  // modDepth
        1.0000f,  // width
        0.7511f,  // earlyDiff
        0.3673f,  // lateDiff
        1.47f,  // bassMult
        1000.0f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        19980.2f,  // highCut Hz
        false,
        0.0786f,  // roomSize
        1.0000f,  // earlyLateBal
        0.72f,  // highDecay
        1.40f,  // midDecay
        11618.9f,  // highFreq Hz
        1.0000f,  // erShape
        0.1509f,  // erSpread
        56.2f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.3119f,  // stereoCoupling
        100.0f,  // lowMidFreq Hz
        0.99f,  // lowMidDecay
        3,  // envMode (Swell)
        1028.3f,  // envHold ms
        805.5f,  // envRelease ms
        81.2f,  // envDepth %
        18.4f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1004.2f,  // outEQ1Freq Hz
        0.00f,  // outEQ1Gain dB
        1.01f,  // outEQ1Q
        4035.8f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.01f,  // outEQ2Q
        0.6009f,  // stereoInvert
        0.6004f,  // resonance
        0.0000f,  // echoPingPong
        0.5999f,  // dynAmount
        0.2497f  // dynSpeed
    });

    // Bright, close hall, medium short decay, live quality (match: 88%)
    presets.push_back({
        "Sizzle Hall",
        "Halls",
        6,  // Bright Hall
        0,  // 1970s
        0.2483f,  // size
        0.0000f,  // damping
        11.0f,  // predelay ms
        0.30f,  // mix
        0.80f,  // modRate Hz
        0.2421f,  // modDepth
        1.0000f,  // width
        0.6256f,  // earlyDiff
        0.5491f,  // lateDiff
        0.46f,  // bassMult
        1000.0f,  // bassFreq Hz
        33.6f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6768f,  // roomSize
        0.6000f,  // earlyLateBal
        4.00f,  // highDecay
        0.94f,  // midDecay
        8016.2f,  // highFreq Hz
        1.0000f,  // erShape
        0.0000f,  // erSpread
        38.0f,  // erBassCut Hz
        1.49f,  // trebleRatio
        0.1493f,  // stereoCoupling
        8000.0f,  // lowMidFreq Hz
        0.91f,  // lowMidDecay
        0,  // envMode (Off)
        501.5f,  // envHold ms
        331.1f,  // envRelease ms
        0.0f,  // envDepth %
        37.9f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1616.9f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        6646.1f,  // outEQ2Freq Hz
        3.02f,  // outEQ2Gain dB
        1.31f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -0.6006f,  // dynAmount
        0.2502f  // dynSpeed
    });

    // Slap initial double tap, dark (match: 97%)
    presets.push_back({
        "Slap Hall",
        "Halls",
        1,  // Room
        0,  // 1970s
        0.2483f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.88f,  // modRate Hz
        0.2562f,  // modDepth
        0.9999f,  // width
        0.4999f,  // earlyDiff
        0.4998f,  // lateDiff
        0.64f,  // bassMult
        100.0f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        18671.2f,  // highCut Hz
        false,
        0.6239f,  // roomSize
        0.7000f,  // earlyLateBal
        0.73f,  // highDecay
        0.96f,  // midDecay
        2378.1f,  // highFreq Hz
        1.0000f,  // erShape
        0.1249f,  // erSpread
        80.0f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1498f,  // stereoCoupling
        100.0f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        403.7f,  // envHold ms
        499.9f,  // envRelease ms
        0.0f,  // envDepth %
        83.7f,  // echoDelay ms
        12.6f,  // echoFeedback %
        1023.5f,  // outEQ1Freq Hz
        -0.00f,  // outEQ1Gain dB
        1.35f,  // outEQ1Q
        4181.8f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.23f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.6253f,  // echoPingPong
        0.9993f,  // dynAmount
        0.2501f  // dynSpeed
    });

    // Small hall, no reflections, short decay (match: 97%)
    presets.push_back({
        "Small Church",
        "Halls",
        4,  // Cathedral
        0,  // 1970s
        0.2619f,  // size
        0.3759f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.81f,  // modRate Hz
        0.2596f,  // modDepth
        0.9861f,  // width
        0.5021f,  // earlyDiff
        0.4984f,  // lateDiff
        1.09f,  // bassMult
        333.1f,  // bassFreq Hz
        21.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.2498f,  // roomSize
        0.4000f,  // earlyLateBal
        0.98f,  // highDecay
        0.72f,  // midDecay
        1000.0f,  // highFreq Hz
        0.5001f,  // erShape
        0.7884f,  // erSpread
        224.0f,  // erBassCut Hz
        0.61f,  // trebleRatio
        0.1987f,  // stereoCoupling
        100.1f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        502.6f,  // envHold ms
        502.6f,  // envRelease ms
        0.0f,  // envDepth %
        125.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        918.2f,  // outEQ1Freq Hz
        1.85f,  // outEQ1Gain dB
        1.03f,  // outEQ1Q
        4008.4f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6004f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Small hall, bright initial reverb (match: 96%)
    presets.push_back({
        "Small Hall",
        "Halls",
        3,  // Chamber
        0,  // 1970s
        0.1733f,  // size
        0.4484f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.85f,  // modRate Hz
        0.2854f,  // modDepth
        1.0000f,  // width
        0.2964f,  // earlyDiff
        0.5626f,  // lateDiff
        0.94f,  // bassMult
        825.5f,  // bassFreq Hz
        20.7f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.9250f,  // roomSize
        0.7000f,  // earlyLateBal
        4.00f,  // highDecay
        0.90f,  // midDecay
        3071.0f,  // highFreq Hz
        0.8725f,  // erShape
        0.0767f,  // erSpread
        38.2f,  // erBassCut Hz
        1.64f,  // trebleRatio
        0.1450f,  // stereoCoupling
        5057.5f,  // lowMidFreq Hz
        0.98f,  // lowMidDecay
        0,  // envMode (Off)
        751.9f,  // envHold ms
        504.1f,  // envRelease ms
        0.0f,  // envDepth %
        62.4f,  // echoDelay ms
        3.9f,  // echoFeedback %
        156.9f,  // outEQ1Freq Hz
        -3.06f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4014.4f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        0.81f,  // outEQ2Q
        0.6006f,  // stereoInvert
        0.6254f,  // resonance
        0.0000f,  // echoPingPong
        1.0000f,  // dynAmount
        0.2504f  // dynSpeed
    });

    // Small hall with stage reflections (match: 95%)
    presets.push_back({
        "Small Hall+Stage",
        "Halls",
        3,  // Chamber
        0,  // 1970s
        0.0752f,  // size
        0.0000f,  // damping
        10.6f,  // predelay ms
        0.30f,  // mix
        0.83f,  // modRate Hz
        0.2480f,  // modDepth
        1.0000f,  // width
        0.5457f,  // earlyDiff
        0.6278f,  // lateDiff
        1.19f,  // bassMult
        100.8f,  // bassFreq Hz
        22.5f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.9028f,  // roomSize
        0.5000f,  // earlyLateBal
        1.37f,  // highDecay
        1.01f,  // midDecay
        3015.1f,  // highFreq Hz
        0.7982f,  // erShape
        0.3199f,  // erSpread
        320.9f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.0988f,  // stereoCoupling
        2815.6f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        1,  // envMode (Gate)
        719.0f,  // envHold ms
        107.4f,  // envRelease ms
        100.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1001.7f,  // outEQ1Freq Hz
        0.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4007.0f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6010f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Tight, gated hall for snares (match: 85%)
    presets.push_back({
        "Snare Gate",
        "Halls",
        1,  // Room
        0,  // 1970s
        0.5632f,  // size
        0.5007f,  // damping
        219.3f,  // predelay ms
        0.30f,  // mix
        0.82f,  // modRate Hz
        0.2497f,  // modDepth
        1.0000f,  // width
        0.6217f,  // earlyDiff
        0.5470f,  // lateDiff
        0.72f,  // bassMult
        271.2f,  // bassFreq Hz
        20.6f,  // lowCut Hz
        19986.9f,  // highCut Hz
        false,
        1.0000f,  // roomSize
        0.7000f,  // earlyLateBal
        0.63f,  // highDecay
        1.02f,  // midDecay
        1005.4f,  // highFreq Hz
        0.9349f,  // erShape
        0.4583f,  // erSpread
        42.2f,  // erBassCut Hz
        1.30f,  // trebleRatio
        0.1499f,  // stereoCoupling
        100.0f,  // lowMidFreq Hz
        1.01f,  // lowMidDecay
        0,  // envMode (Off)
        500.7f,  // envHold ms
        500.7f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1000.3f,  // outEQ1Freq Hz
        0.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4001.4f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.7500f,  // dynAmount
        0.5000f  // dynSpeed
    });

    // Strange hall with LFO controlling spatial EQ (match: 95%)
    presets.push_back({
        "Spatial Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.0000f,  // damping
        2.1f,  // predelay ms
        0.30f,  // mix
        0.85f,  // modRate Hz
        0.2513f,  // modDepth
        0.9527f,  // width
        0.1483f,  // earlyDiff
        0.3785f,  // lateDiff
        0.10f,  // bassMult
        334.6f,  // bassFreq Hz
        77.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6761f,  // roomSize
        0.6000f,  // earlyLateBal
        1.67f,  // highDecay
        1.00f,  // midDecay
        3090.4f,  // highFreq Hz
        0.5071f,  // erShape
        0.5832f,  // erSpread
        152.1f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1515f,  // stereoCoupling
        5031.5f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        1,  // envMode (Gate)
        1288.8f,  // envHold ms
        388.5f,  // envRelease ms
        71.3f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        2078.0f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4017.2f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -0.6000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Split reverb with locker room and arena (match: 90%)
    presets.push_back({
        "Sports Verbs",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.2199f,  // size
        0.0000f,  // damping
        69.3f,  // predelay ms
        0.30f,  // mix
        0.79f,  // modRate Hz
        0.2422f,  // modDepth
        1.0000f,  // width
        0.9268f,  // earlyDiff
        0.6303f,  // lateDiff
        1.31f,  // bassMult
        728.2f,  // bassFreq Hz
        65.3f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.9693f,  // roomSize
        0.7000f,  // earlyLateBal
        3.47f,  // highDecay
        1.02f,  // midDecay
        4001.2f,  // highFreq Hz
        0.3948f,  // erShape
        0.2501f,  // erSpread
        116.1f,  // erBassCut Hz
        1.15f,  // trebleRatio
        0.1516f,  // stereoCoupling
        3064.2f,  // lowMidFreq Hz
        1.03f,  // lowMidDecay
        0,  // envMode (Off)
        362.9f,  // envHold ms
        502.5f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        446.5f,  // outEQ1Freq Hz
        3.00f,  // outEQ1Gain dB
        1.03f,  // outEQ1Q
        5040.4f,  // outEQ2Freq Hz
        -4.09f,  // outEQ2Gain dB
        0.49f,  // outEQ2Q
        0.6015f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2501f  // dynSpeed
    });

    // Short decay of single room, large reflections (match: 97%)
    presets.push_back({
        "Stairwell",
        "Halls",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.5016f,  // damping
        31.9f,  // predelay ms
        0.30f,  // mix
        0.80f,  // modRate Hz
        0.2501f,  // modDepth
        1.0000f,  // width
        0.6350f,  // earlyDiff
        0.5441f,  // lateDiff
        1.55f,  // bassMult
        100.5f,  // bassFreq Hz
        38.3f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        1.0000f,  // roomSize
        0.6000f,  // earlyLateBal
        0.80f,  // highDecay
        0.99f,  // midDecay
        3077.2f,  // highFreq Hz
        0.3765f,  // erShape
        0.4113f,  // erSpread
        153.0f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.2262f,  // stereoCoupling
        100.2f,  // lowMidFreq Hz
        0.95f,  // lowMidDecay
        1,  // envMode (Gate)
        474.7f,  // envHold ms
        1038.3f,  // envRelease ms
        84.8f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        100.9f,  // outEQ1Freq Hz
        4.83f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        3393.3f,  // outEQ2Freq Hz
        9.00f,  // outEQ2Gain dB
        1.38f,  // outEQ2Q
        0.1250f,  // stereoInvert
        0.6042f,  // resonance
        0.0000f,  // echoPingPong
        0.6025f,  // dynAmount
        0.4088f  // dynSpeed
    });

    // Chorused hall, long decay for synths (match: 96%)
    presets.push_back({
        "Synth Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.2483f,  // size
        0.8099f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        1.40f,  // modRate Hz
        0.2554f,  // modDepth
        1.0000f,  // width
        0.5007f,  // earlyDiff
        0.6247f,  // lateDiff
        0.70f,  // bassMult
        549.7f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6499f,  // roomSize
        0.9000f,  // earlyLateBal
        0.25f,  // highDecay
        1.01f,  // midDecay
        7868.2f,  // highFreq Hz
        0.0000f,  // erShape
        0.7590f,  // erSpread
        80.1f,  // erBassCut Hz
        0.30f,  // trebleRatio
        0.1502f,  // stereoCoupling
        2077.7f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        500.7f,  // envHold ms
        500.7f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1348.1f,  // outEQ1Freq Hz
        3.21f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        6891.3f,  // outEQ2Freq Hz
        4.80f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.3004f,  // resonance
        0.0000f,  // echoPingPong
        1.0000f,  // dynAmount
        0.2503f  // dynSpeed
    });

    // Very reflective sound, like pounding a brick wall (match: 82%)
    presets.push_back({
        "Tap Brick",
        "Halls",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.5103f,  // damping
        9.3f,  // predelay ms
        0.30f,  // mix
        0.10f,  // modRate Hz
        0.2579f,  // modDepth
        0.3512f,  // width
        1.0000f,  // earlyDiff
        0.5396f,  // lateDiff
        0.10f,  // bassMult
        298.7f,  // bassFreq Hz
        21.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.2496f,  // roomSize
        0.7000f,  // earlyLateBal
        1.28f,  // highDecay
        1.18f,  // midDecay
        1411.1f,  // highFreq Hz
        0.8930f,  // erShape
        0.3710f,  // erSpread
        229.0f,  // erBassCut Hz
        0.66f,  // trebleRatio
        0.1823f,  // stereoCoupling
        1724.4f,  // lowMidFreq Hz
        0.93f,  // lowMidDecay
        1,  // envMode (Gate)
        566.2f,  // envHold ms
        680.7f,  // envRelease ms
        62.4f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        549.3f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        1.01f,  // outEQ1Q
        4627.2f,  // outEQ2Freq Hz
        -7.06f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2507f  // dynSpeed
    });

    // Strange hall with LFO controlling reverb HF cut (match: 97%)
    presets.push_back({
        "Tidal Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.0752f,  // size
        0.0939f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        1.41f,  // modRate Hz
        0.2606f,  // modDepth
        0.8151f,  // width
        0.3585f,  // earlyDiff
        0.1944f,  // lateDiff
        2.47f,  // bassMult
        628.0f,  // bassFreq Hz
        92.8f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        1.0000f,  // roomSize
        0.7000f,  // earlyLateBal
        2.90f,  // highDecay
        1.94f,  // midDecay
        3102.5f,  // highFreq Hz
        1.0000f,  // erShape
        0.0000f,  // erSpread
        20.0f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1914f,  // stereoCoupling
        5142.4f,  // lowMidFreq Hz
        0.99f,  // lowMidDecay
        0,  // envMode (Off)
        502.6f,  // envHold ms
        504.7f,  // envRelease ms
        0.0f,  // envDepth %
        62.3f,  // echoDelay ms
        0.0f,  // echoFeedback %
        441.6f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        2479.0f,  // outEQ2Freq Hz
        -7.08f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6003f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.7525f,  // dynAmount
        0.5005f  // dynSpeed
    });

    // Large hall, very little HF content (match: 93%)
    presets.push_back({
        "Utility Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.0752f,  // size
        0.6145f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.85f,  // modRate Hz
        0.2271f,  // modDepth
        1.0000f,  // width
        0.5392f,  // earlyDiff
        0.5603f,  // lateDiff
        0.48f,  // bassMult
        773.9f,  // bassFreq Hz
        21.6f,  // lowCut Hz
        19055.3f,  // highCut Hz
        false,
        0.5622f,  // roomSize
        0.6000f,  // earlyLateBal
        4.00f,  // highDecay
        0.93f,  // midDecay
        7874.8f,  // highFreq Hz
        0.4995f,  // erShape
        0.1959f,  // erSpread
        500.0f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1891f,  // stereoCoupling
        100.1f,  // lowMidFreq Hz
        1.01f,  // lowMidDecay
        1,  // envMode (Gate)
        1007.4f,  // envHold ms
        759.3f,  // envRelease ms
        100.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1004.0f,  // outEQ1Freq Hz
        0.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4016.1f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6014f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2503f  // dynSpeed
    });

    // General, all purpose reverb (match: 93%)
    presets.push_back({
        "Utility Verb",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.2483f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.82f,  // modRate Hz
        0.2471f,  // modDepth
        1.0000f,  // width
        0.0375f,  // earlyDiff
        0.6267f,  // lateDiff
        1.07f,  // bassMult
        662.3f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.4966f,  // roomSize
        0.5000f,  // earlyLateBal
        1.43f,  // highDecay
        0.62f,  // midDecay
        1011.3f,  // highFreq Hz
        0.5795f,  // erShape
        0.0000f,  // erSpread
        38.1f,  // erBassCut Hz
        1.05f,  // trebleRatio
        0.1565f,  // stereoCoupling
        100.6f,  // lowMidFreq Hz
        1.29f,  // lowMidDecay
        0,  // envMode (Off)
        501.1f,  // envHold ms
        504.8f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        798.2f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        0.99f,  // outEQ1Q
        5905.1f,  // outEQ2Freq Hz
        3.59f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6022f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        1.0000f,  // dynAmount
        0.2501f  // dynSpeed
    });

    // Enormous, silky reflective room (match: 96%)
    presets.push_back({
        "Vocal Concert",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.0752f,  // size
        0.0000f,  // damping
        10.6f,  // predelay ms
        0.30f,  // mix
        0.73f,  // modRate Hz
        0.1140f,  // modDepth
        1.0000f,  // width
        0.6033f,  // earlyDiff
        0.6427f,  // lateDiff
        0.42f,  // bassMult
        888.8f,  // bassFreq Hz
        114.3f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.3705f,  // roomSize
        0.6000f,  // earlyLateBal
        0.98f,  // highDecay
        0.25f,  // midDecay
        3132.6f,  // highFreq Hz
        0.2850f,  // erShape
        0.2755f,  // erSpread
        152.8f,  // erBassCut Hz
        1.83f,  // trebleRatio
        0.4142f,  // stereoCoupling
        2836.7f,  // lowMidFreq Hz
        1.34f,  // lowMidDecay
        0,  // envMode (Off)
        782.3f,  // envHold ms
        503.1f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        898.5f,  // outEQ1Freq Hz
        -1.83f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4015.9f,  // outEQ2Freq Hz
        -4.84f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6024f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -0.6009f,  // dynAmount
        0.2504f  // dynSpeed
    });

    // Medium-sized hall, short clear decay (match: 97%)
    presets.push_back({
        "Vocal Hall",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.0752f,  // size
        0.6424f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.79f,  // modRate Hz
        0.3333f,  // modDepth
        0.9617f,  // width
        0.3783f,  // earlyDiff
        0.5015f,  // lateDiff
        0.93f,  // bassMult
        713.2f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        19609.9f,  // highCut Hz
        false,
        0.1398f,  // roomSize
        0.6000f,  // earlyLateBal
        0.96f,  // highDecay
        0.98f,  // midDecay
        1829.5f,  // highFreq Hz
        0.6168f,  // erShape
        0.4337f,  // erSpread
        71.0f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1478f,  // stereoCoupling
        2083.3f,  // lowMidFreq Hz
        1.04f,  // lowMidDecay
        1,  // envMode (Gate)
        1018.9f,  // envHold ms
        834.3f,  // envRelease ms
        69.8f,  // envDepth %
        63.1f,  // echoDelay ms
        0.0f,  // echoFeedback %
        447.3f,  // outEQ1Freq Hz
        -2.49f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        3846.6f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.7500f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Fairly large hall, generous reverb decay (match: 97%)
    presets.push_back({
        "Vocal Hall 2",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.3838f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        0.80f,  // modRate Hz
        0.2562f,  // modDepth
        0.9662f,  // width
        0.1503f,  // earlyDiff
        0.4999f,  // lateDiff
        0.97f,  // bassMult
        1000.0f,  // bassFreq Hz
        20.3f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5007f,  // roomSize
        0.6000f,  // earlyLateBal
        3.06f,  // highDecay
        0.93f,  // midDecay
        5127.1f,  // highFreq Hz
        0.9920f,  // erShape
        0.0379f,  // erSpread
        20.0f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1493f,  // stereoCoupling
        8000.0f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        665.8f,  // envHold ms
        501.7f,  // envRelease ms
        2.8f,  // envDepth %
        63.5f,  // echoDelay ms
        0.0f,  // echoFeedback %
        897.2f,  // outEQ1Freq Hz
        -9.85f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        3994.0f,  // outEQ2Freq Hz
        -7.05f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.3253f,  // stereoInvert
        0.3003f,  // resonance
        0.0000f,  // echoPingPong
        0.7505f,  // dynAmount
        0.5003f  // dynSpeed
    });

    // Lovely reverb with short decay (match: 97%)
    presets.push_back({
        "Vocal Magic",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.0442f,  // damping
        0.0f,  // predelay ms
        0.30f,  // mix
        1.67f,  // modRate Hz
        0.5437f,  // modDepth
        1.0000f,  // width
        0.6253f,  // earlyDiff
        0.4972f,  // lateDiff
        1.13f,  // bassMult
        902.3f,  // bassFreq Hz
        167.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6501f,  // roomSize
        0.9000f,  // earlyLateBal
        4.00f,  // highDecay
        0.97f,  // midDecay
        7877.5f,  // highFreq Hz
        0.0000f,  // erShape
        0.8338f,  // erSpread
        112.3f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1877f,  // stereoCoupling
        4052.0f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        736.4f,  // envHold ms
        502.6f,  // envRelease ms
        7.3f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        464.3f,  // outEQ1Freq Hz
        -4.80f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4125.1f,  // outEQ2Freq Hz
        -6.61f,  // outEQ2Gain dB
        1.32f,  // outEQ2Q
        0.6003f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.6003f,  // dynAmount
        0.2501f  // dynSpeed
    });

    // Close delays double the source, wide (match: 82%)
    presets.push_back({
        "Wide Vox",
        "Halls",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.0000f,  // damping
        1.0f,  // predelay ms
        0.30f,  // mix
        0.81f,  // modRate Hz
        0.2502f,  // modDepth
        0.8841f,  // width
        0.3744f,  // earlyDiff
        0.5031f,  // lateDiff
        0.38f,  // bassMult
        544.5f,  // bassFreq Hz
        21.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.8207f,  // roomSize
        0.5000f,  // earlyLateBal
        1.36f,  // highDecay
        1.04f,  // midDecay
        1824.8f,  // highFreq Hz
        0.5036f,  // erShape
        0.5764f,  // erSpread
        143.0f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1488f,  // stereoCoupling
        3058.8f,  // lowMidFreq Hz
        0.96f,  // lowMidDecay
        0,  // envMode (Off)
        500.5f,  // envHold ms
        500.5f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1453.0f,  // outEQ1Freq Hz
        -7.69f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4004.1f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2500f  // dynSpeed
    });


    // ==================== ROOMS (60) ====================

    // Bit of dry delay, sweet for vocals/instruments (match: 88%)
    presets.push_back({
        "Ambient Sustain",
        "Rooms",
        5,  // Ambience
        0,  // 1970s
        0.1733f,  // size
        0.1063f,  // damping
        42.3f,  // predelay ms
        0.22f,  // mix
        0.82f,  // modRate Hz
        0.2569f,  // modDepth
        0.7481f,  // width
        0.4945f,  // earlyDiff
        0.5217f,  // lateDiff
        1.03f,  // bassMult
        760.4f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5951f,  // roomSize
        0.6000f,  // earlyLateBal
        0.82f,  // highDecay
        0.53f,  // midDecay
        1000.7f,  // highFreq Hz
        0.7970f,  // erShape
        0.7530f,  // erSpread
        115.4f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1462f,  // stereoCoupling
        2644.7f,  // lowMidFreq Hz
        1.11f,  // lowMidDecay
        1,  // envMode (Gate)
        520.4f,  // envHold ms
        10.1f,  // envRelease ms
        89.4f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        446.3f,  // outEQ1Freq Hz
        -4.80f,  // outEQ1Gain dB
        0.63f,  // outEQ1Q
        4010.9f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6254f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Small bedroom with furniture and heavy curtains (match: 83%)
    presets.push_back({
        "Bedroom",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0000f,  // size
        0.9291f,  // damping
        1.4f,  // predelay ms
        0.22f,  // mix
        0.80f,  // modRate Hz
        0.2500f,  // modDepth
        1.0000f,  // width
        0.5973f,  // earlyDiff
        0.8625f,  // lateDiff
        1.55f,  // bassMult
        330.5f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.3750f,  // roomSize
        0.6000f,  // earlyLateBal
        1.57f,  // highDecay
        1.00f,  // midDecay
        12000.0f,  // highFreq Hz
        0.6250f,  // erShape
        0.3750f,  // erSpread
        80.0f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.0000f,  // stereoCoupling
        2667.5f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        500.0f,  // envHold ms
        500.0f,  // envRelease ms
        8.2f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        938.4f,  // outEQ1Freq Hz
        12.00f,  // outEQ1Gain dB
        1.09f,  // outEQ1Q
        6022.5f,  // outEQ2Freq Hz
        -12.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.5840f,  // stereoInvert
        0.1264f,  // resonance
        0.0000f,  // echoPingPong
        -0.1500f,  // dynAmount
        0.5500f  // dynSpeed
    });

    // Perfect for dreamy soundscapes, atmospheric (match: 82%)
    presets.push_back({
        "BeeBee Slapz",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.1250f,  // damping
        112.5f,  // predelay ms
        0.22f,  // mix
        0.10f,  // modRate Hz
        0.2447f,  // modDepth
        0.9978f,  // width
        0.6245f,  // earlyDiff
        0.1594f,  // lateDiff
        1.24f,  // bassMult
        216.5f,  // bassFreq Hz
        20.5f,  // lowCut Hz
        18451.4f,  // highCut Hz
        false,
        1.0000f,  // roomSize
        0.9000f,  // earlyLateBal
        1.52f,  // highDecay
        0.44f,  // midDecay
        2972.3f,  // highFreq Hz
        0.9482f,  // erShape
        1.0000f,  // erSpread
        80.0f,  // erBassCut Hz
        1.13f,  // trebleRatio
        0.1500f,  // stereoCoupling
        1692.0f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        500.0f,  // envHold ms
        500.0f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1000.0f,  // outEQ1Freq Hz
        8.70f,  // outEQ1Gain dB
        1.10f,  // outEQ1Q
        4000.0f,  // outEQ2Freq Hz
        -1.80f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        1.0000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Saturated bottom-heavy, dense reverb (match: 88%)
    presets.push_back({
        "Big Boom Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        0.76f,  // modRate Hz
        0.2512f,  // modDepth
        1.0000f,  // width
        0.5006f,  // earlyDiff
        0.6561f,  // lateDiff
        1.80f,  // bassMult
        100.1f,  // bassFreq Hz
        56.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.8498f,  // roomSize
        0.4000f,  // earlyLateBal
        2.18f,  // highDecay
        1.27f,  // midDecay
        3042.1f,  // highFreq Hz
        0.6476f,  // erShape
        0.9569f,  // erSpread
        176.2f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1517f,  // stereoCoupling
        2274.4f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        1,  // envMode (Gate)
        84.4f,  // envHold ms
        167.0f,  // envRelease ms
        10.0f,  // envDepth %
        62.4f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1000.7f,  // outEQ1Freq Hz
        -7.62f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        2835.5f,  // outEQ2Freq Hz
        1.80f,  // outEQ2Gain dB
        1.07f,  // outEQ2Q
        0.6008f,  // stereoInvert
        0.0593f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2502f  // dynSpeed
    });

    // Bit of predelay separates bright reverb from source (match: 93%)
    presets.push_back({
        "Bright Vocal",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.0000f,  // damping
        71.3f,  // predelay ms
        0.22f,  // mix
        0.59f,  // modRate Hz
        0.2443f,  // modDepth
        0.7121f,  // width
        0.5021f,  // earlyDiff
        0.3179f,  // lateDiff
        1.08f,  // bassMult
        551.3f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6210f,  // roomSize
        0.6000f,  // earlyLateBal
        1.65f,  // highDecay
        0.53f,  // midDecay
        5120.2f,  // highFreq Hz
        0.2015f,  // erShape
        0.3995f,  // erSpread
        175.6f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1500f,  // stereoCoupling
        4054.6f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        502.9f,  // envHold ms
        501.7f,  // envRelease ms
        0.0f,  // envDepth %
        124.8f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1001.7f,  // outEQ1Freq Hz
        0.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        100.1f,  // outEQ2Freq Hz
        1.80f,  // outEQ2Gain dB
        1.05f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.3003f,  // resonance
        0.0000f,  // echoPingPong
        0.0000f,  // dynAmount
        0.5006f  // dynSpeed
    });

    // Sounds like snowed in too long (match: 92%)
    presets.push_back({
        "Cabin Fever",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.2483f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        0.80f,  // modRate Hz
        0.2550f,  // modDepth
        1.0000f,  // width
        0.6250f,  // earlyDiff
        0.6307f,  // lateDiff
        0.90f,  // bassMult
        754.5f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6759f,  // roomSize
        0.7000f,  // earlyLateBal
        4.00f,  // highDecay
        1.00f,  // midDecay
        2168.8f,  // highFreq Hz
        0.4261f,  // erShape
        0.3750f,  // erSpread
        45.5f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1503f,  // stereoCoupling
        3484.0f,  // lowMidFreq Hz
        0.96f,  // lowMidDecay
        1,  // envMode (Gate)
        507.5f,  // envHold ms
        561.5f,  // envRelease ms
        70.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        509.8f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        2821.0f,  // outEQ2Freq Hz
        -8.85f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.6013f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Tight small space, open or closed casket (match: 67%)
    presets.push_back({
        "Coffin",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0000f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        0.80f,  // modRate Hz
        0.2517f,  // modDepth
        1.0000f,  // width
        0.6225f,  // earlyDiff
        0.8742f,  // lateDiff
        0.97f,  // bassMult
        325.0f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        19960.7f,  // highCut Hz
        false,
        0.8400f,  // roomSize
        0.6000f,  // earlyLateBal
        0.88f,  // highDecay
        1.00f,  // midDecay
        4967.2f,  // highFreq Hz
        0.6424f,  // erShape
        0.0000f,  // erSpread
        80.2f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.0000f,  // stereoCoupling
        100.6f,  // lowMidFreq Hz
        1.18f,  // lowMidDecay
        2,  // envMode (Reverse)
        1400.0f,  // envHold ms
        501.1f,  // envRelease ms
        100.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        961.3f,  // outEQ1Freq Hz
        12.00f,  // outEQ1Gain dB
        1.06f,  // outEQ1Q
        7601.6f,  // outEQ2Freq Hz
        -12.00f,  // outEQ2Gain dB
        0.37f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.6013f,  // resonance
        0.0000f,  // echoPingPong
        -0.9659f,  // dynAmount
        0.5011f  // dynSpeed
    });

    // Live sound with less dominating, punchier sound (match: 86%)
    presets.push_back({
        "Delay Space",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.2199f,  // size
        0.0000f,  // damping
        241.6f,  // predelay ms
        0.22f,  // mix
        0.69f,  // modRate Hz
        0.2549f,  // modDepth
        0.9450f,  // width
        1.0000f,  // earlyDiff
        0.5430f,  // lateDiff
        1.19f,  // bassMult
        325.0f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.3646f,  // roomSize
        0.8000f,  // earlyLateBal
        3.26f,  // highDecay
        0.81f,  // midDecay
        3332.6f,  // highFreq Hz
        0.1594f,  // erShape
        1.0000f,  // erSpread
        64.0f,  // erBassCut Hz
        1.79f,  // trebleRatio
        0.1500f,  // stereoCoupling
        1096.8f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        500.0f,  // envHold ms
        500.0f,  // envRelease ms
        0.0f,  // envDepth %
        62.5f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1047.5f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4000.0f,  // outEQ2Freq Hz
        -3.05f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6250f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        1.0000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Dark preset, dense saturated, for whole drum kit (match: 95%)
    presets.push_back({
        "Drum Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.0376f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        0.80f,  // modRate Hz
        0.3127f,  // modDepth
        1.0000f,  // width
        0.0000f,  // earlyDiff
        0.4469f,  // lateDiff
        1.18f,  // bassMult
        349.5f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5924f,  // roomSize
        0.9000f,  // earlyLateBal
        1.12f,  // highDecay
        0.54f,  // midDecay
        4117.2f,  // highFreq Hz
        0.7876f,  // erShape
        0.5805f,  // erSpread
        119.5f,  // erBassCut Hz
        1.99f,  // trebleRatio
        0.1548f,  // stereoCoupling
        3674.0f,  // lowMidFreq Hz
        1.24f,  // lowMidDecay
        1,  // envMode (Gate)
        501.3f,  // envHold ms
        1040.1f,  // envRelease ms
        96.1f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        435.7f,  // outEQ1Freq Hz
        -4.82f,  // outEQ1Gain dB
        0.85f,  // outEQ1Q
        3995.6f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0001f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2501f  // dynSpeed
    });

    // Split effect, empty and full closet (match: 92%)
    presets.push_back({
        "Dual Closets",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0000f,  // size
        0.9737f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        0.82f,  // modRate Hz
        0.2515f,  // modDepth
        0.9604f,  // width
        0.4202f,  // earlyDiff
        0.3792f,  // lateDiff
        1.56f,  // bassMult
        690.1f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        19097.4f,  // highCut Hz
        false,
        0.3461f,  // roomSize
        0.6000f,  // earlyLateBal
        0.53f,  // highDecay
        1.01f,  // midDecay
        3036.9f,  // highFreq Hz
        0.8020f,  // erShape
        0.4548f,  // erSpread
        164.4f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.4596f,  // stereoCoupling
        444.0f,  // lowMidFreq Hz
        1.01f,  // lowMidDecay
        2,  // envMode (Reverse)
        1248.6f,  // envHold ms
        182.9f,  // envRelease ms
        92.9f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1083.1f,  // outEQ1Freq Hz
        8.99f,  // outEQ1Gain dB
        1.01f,  // outEQ1Q
        3415.9f,  // outEQ2Freq Hz
        -3.60f,  // outEQ2Gain dB
        0.99f,  // outEQ2Q
        0.3228f,  // stereoInvert
        0.6258f,  // resonance
        0.0000f,  // echoPingPong
        -0.9985f,  // dynAmount
        0.4979f  // dynSpeed
    });

    // Syncopated echo delay inside small kitchen (match: 76%)
    presets.push_back({
        "Echo Kitchen",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.8926f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        0.82f,  // modRate Hz
        0.2506f,  // modDepth
        1.0000f,  // width
        0.3756f,  // earlyDiff
        0.4875f,  // lateDiff
        0.46f,  // bassMult
        551.8f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        1.0000f,  // roomSize
        0.6000f,  // earlyLateBal
        3.54f,  // highDecay
        0.95f,  // midDecay
        4067.2f,  // highFreq Hz
        0.5012f,  // erShape
        0.4995f,  // erSpread
        148.4f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.3017f,  // stereoCoupling
        3667.1f,  // lowMidFreq Hz
        0.95f,  // lowMidDecay
        0,  // envMode (Off)
        760.6f,  // envHold ms
        10.1f,  // envRelease ms
        43.2f,  // envDepth %
        135.6f,  // echoDelay ms
        0.0f,  // echoFeedback %
        898.6f,  // outEQ1Freq Hz
        7.50f,  // outEQ1Gain dB
        1.01f,  // outEQ1Q
        4056.1f,  // outEQ2Freq Hz
        -12.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.7511f,  // dynAmount
        0.2504f  // dynSpeed
    });

    // Generic ambience, starting place (match: 96%)
    presets.push_back({
        "Gen. Ambience",
        "Rooms",
        5,  // Ambience
        0,  // 1970s
        0.0752f,  // size
        0.0000f,  // damping
        28.3f,  // predelay ms
        0.22f,  // mix
        0.89f,  // modRate Hz
        0.2553f,  // modDepth
        1.0000f,  // width
        0.6999f,  // earlyDiff
        0.6455f,  // lateDiff
        1.39f,  // bassMult
        475.0f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.4999f,  // roomSize
        0.9000f,  // earlyLateBal
        4.00f,  // highDecay
        1.01f,  // midDecay
        7952.0f,  // highFreq Hz
        0.1063f,  // erShape
        0.7498f,  // erSpread
        119.1f,  // erBassCut Hz
        1.36f,  // trebleRatio
        0.1505f,  // stereoCoupling
        100.1f,  // lowMidFreq Hz
        1.06f,  // lowMidDecay
        1,  // envMode (Gate)
        386.5f,  // envHold ms
        384.0f,  // envRelease ms
        100.0f,  // envDepth %
        37.5f,  // echoDelay ms
        0.0f,  // echoFeedback %
        918.1f,  // outEQ1Freq Hz
        -10.73f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        3476.5f,  // outEQ2Freq Hz
        -8.18f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6254f,  // stereoInvert
        0.6254f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2502f  // dynSpeed
    });

    // Warm smooth reverb of Real Room with more decay (match: 92%)
    presets.push_back({
        "Great Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.2483f,  // size
        0.0000f,  // damping
        25.6f,  // predelay ms
        0.22f,  // mix
        0.83f,  // modRate Hz
        0.2913f,  // modDepth
        1.0000f,  // width
        0.5018f,  // earlyDiff
        0.5010f,  // lateDiff
        1.02f,  // bassMult
        1000.0f,  // bassFreq Hz
        23.5f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        1.0000f,  // roomSize
        0.6000f,  // earlyLateBal
        4.00f,  // highDecay
        0.93f,  // midDecay
        5283.6f,  // highFreq Hz
        0.7564f,  // erShape
        0.3746f,  // erSpread
        263.6f,  // erBassCut Hz
        1.58f,  // trebleRatio
        0.1495f,  // stereoCoupling
        4051.0f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        500.1f,  // envHold ms
        500.5f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1000.8f,  // outEQ1Freq Hz
        0.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4000.3f,  // outEQ2Freq Hz
        -0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.0000f,  // dynAmount
        0.2501f  // dynSpeed
    });

    // Tight and punchy ambience, combining small sizes (match: 95%)
    presets.push_back({
        "Guitar Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0000f,  // size
        0.8273f,  // damping
        10.9f,  // predelay ms
        0.22f,  // mix
        1.08f,  // modRate Hz
        0.2646f,  // modDepth
        1.0000f,  // width
        0.6470f,  // earlyDiff
        0.8926f,  // lateDiff
        1.56f,  // bassMult
        223.7f,  // bassFreq Hz
        49.7f,  // lowCut Hz
        14989.0f,  // highCut Hz
        false,
        0.5022f,  // roomSize
        0.6000f,  // earlyLateBal
        0.68f,  // highDecay
        0.88f,  // midDecay
        1003.3f,  // highFreq Hz
        0.8055f,  // erShape
        0.3297f,  // erSpread
        20.0f,  // erBassCut Hz
        1.88f,  // trebleRatio
        0.5000f,  // stereoCoupling
        100.1f,  // lowMidFreq Hz
        0.96f,  // lowMidDecay
        3,  // envMode (Swell)
        1374.8f,  // envHold ms
        1002.0f,  // envRelease ms
        80.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1091.2f,  // outEQ1Freq Hz
        10.98f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        3997.8f,  // outEQ2Freq Hz
        9.90f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.2503f,  // dynAmount
        0.6258f  // dynSpeed
    });

    // Designed to sound like hardwood floor room (match: 88%)
    presets.push_back({
        "Hardwood Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.1064f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        0.80f,  // modRate Hz
        0.2495f,  // modDepth
        0.2421f,  // width
        0.6244f,  // earlyDiff
        0.5511f,  // lateDiff
        1.91f,  // bassMult
        550.6f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.2573f,  // roomSize
        0.6000f,  // earlyLateBal
        1.29f,  // highDecay
        1.01f,  // midDecay
        2514.5f,  // highFreq Hz
        0.5022f,  // erShape
        0.1253f,  // erSpread
        158.4f,  // erBassCut Hz
        1.65f,  // trebleRatio
        0.1487f,  // stereoCoupling
        3657.9f,  // lowMidFreq Hz
        1.01f,  // lowMidDecay
        1,  // envMode (Gate)
        208.0f,  // envHold ms
        331.9f,  // envRelease ms
        79.4f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1088.3f,  // outEQ1Freq Hz
        -5.40f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        6227.4f,  // outEQ2Freq Hz
        -3.02f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.7506f,  // dynAmount
        0.2502f  // dynSpeed
    });

    // A dense concert hall (match: 84%)
    presets.push_back({
        "Hole Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.4674f,  // size
        0.1063f,  // damping
        122.6f,  // predelay ms
        0.22f,  // mix
        0.83f,  // modRate Hz
        0.2732f,  // modDepth
        1.0000f,  // width
        0.1525f,  // earlyDiff
        0.5394f,  // lateDiff
        0.81f,  // bassMult
        891.4f,  // bassFreq Hz
        73.9f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.9999f,  // roomSize
        0.6000f,  // earlyLateBal
        4.00f,  // highDecay
        0.99f,  // midDecay
        3749.4f,  // highFreq Hz
        0.9999f,  // erShape
        0.0000f,  // erSpread
        429.1f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1495f,  // stereoCoupling
        5700.6f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        764.8f,  // envHold ms
        836.2f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1725.6f,  // outEQ1Freq Hz
        0.62f,  // outEQ1Gain dB
        0.91f,  // outEQ1Q
        4003.3f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.0000f,  // dynAmount
        0.5002f  // dynSpeed
    });

    // Backwards effect, great as a special effect (match: 88%)
    presets.push_back({
        "Inverse Drums",
        "Rooms",
        2,  // Hall
        0,  // 1970s
        0.1895f,  // size
        0.4453f,  // damping
        180.1f,  // predelay ms
        0.22f,  // mix
        0.88f,  // modRate Hz
        0.1572f,  // modDepth
        0.9689f,  // width
        0.8752f,  // earlyDiff
        0.0375f,  // lateDiff
        0.10f,  // bassMult
        1000.0f,  // bassFreq Hz
        40.3f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.8717f,  // roomSize
        1.0000f,  // earlyLateBal
        0.25f,  // highDecay
        0.25f,  // midDecay
        2753.8f,  // highFreq Hz
        0.0000f,  // erShape
        1.0000f,  // erSpread
        38.0f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.4143f,  // stereoCoupling
        3710.9f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        501.4f,  // envHold ms
        501.6f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1000.7f,  // outEQ1Freq Hz
        0.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4642.6f,  // outEQ2Freq Hz
        3.05f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.3252f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -0.6000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Lots of options, backwards effect (match: 87%)
    presets.push_back({
        "Inverse Room 2",
        "Rooms",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        0.82f,  // modRate Hz
        0.1428f,  // modDepth
        1.0000f,  // width
        0.1293f,  // earlyDiff
        0.5523f,  // lateDiff
        0.84f,  // bassMult
        438.7f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.7502f,  // roomSize
        0.4000f,  // earlyLateBal
        4.00f,  // highDecay
        0.99f,  // midDecay
        7541.5f,  // highFreq Hz
        1.0000f,  // erShape
        0.7385f,  // erSpread
        125.5f,  // erBassCut Hz
        1.88f,  // trebleRatio
        0.5000f,  // stereoCoupling
        2077.2f,  // lowMidFreq Hz
        0.85f,  // lowMidDecay
        2,  // envMode (Reverse)
        1250.4f,  // envHold ms
        328.8f,  // envRelease ms
        100.0f,  // envDepth %
        225.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        397.3f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        1.30f,  // outEQ1Q
        4072.7f,  // outEQ2Freq Hz
        7.00f,  // outEQ2Gain dB
        0.98f,  // outEQ2Q
        0.3007f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.7517f,  // dynAmount
        0.2506f  // dynSpeed
    });

    // Smooth, large reverberant space using Shape and Spread (match: 97%)
    presets.push_back({
        "Large Chamber",
        "Rooms",
        3,  // Chamber
        0,  // 1970s
        0.3719f,  // size
        0.2865f,  // damping
        65.2f,  // predelay ms
        0.22f,  // mix
        0.81f,  // modRate Hz
        0.2616f,  // modDepth
        1.0000f,  // width
        0.1063f,  // earlyDiff
        0.5515f,  // lateDiff
        0.86f,  // bassMult
        487.8f,  // bassFreq Hz
        74.9f,  // lowCut Hz
        19957.1f,  // highCut Hz
        false,
        0.5692f,  // roomSize
        0.6000f,  // earlyLateBal
        2.97f,  // highDecay
        0.95f,  // midDecay
        12000.0f,  // highFreq Hz
        1.0000f,  // erShape
        0.0000f,  // erSpread
        258.7f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1488f,  // stereoCoupling
        4841.5f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        502.1f,  // envHold ms
        502.1f,  // envRelease ms
        0.0f,  // envDepth %
        79.7f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1002.3f,  // outEQ1Freq Hz
        1.80f,  // outEQ1Gain dB
        1.42f,  // outEQ1Q
        4009.0f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6002f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.7502f,  // dynAmount
        0.5002f  // dynSpeed
    });

    // Perfectly smooth listening room, high diffusion (match: 97%)
    presets.push_back({
        "Large Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.3132f,  // size
        0.6143f,  // damping
        2.4f,  // predelay ms
        0.22f,  // mix
        0.84f,  // modRate Hz
        0.2574f,  // modDepth
        1.0000f,  // width
        0.5542f,  // earlyDiff
        0.5489f,  // lateDiff
        0.75f,  // bassMult
        874.1f,  // bassFreq Hz
        20.6f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.4992f,  // roomSize
        0.6000f,  // earlyLateBal
        4.00f,  // highDecay
        1.00f,  // midDecay
        3130.0f,  // highFreq Hz
        0.3631f,  // erShape
        0.7509f,  // erSpread
        200.4f,  // erBassCut Hz
        1.80f,  // trebleRatio
        0.1480f,  // stereoCoupling
        1266.8f,  // lowMidFreq Hz
        0.99f,  // lowMidDecay
        0,  // envMode (Off)
        441.4f,  // envHold ms
        503.5f,  // envRelease ms
        3.1f,  // envDepth %
        122.6f,  // echoDelay ms
        4.4f,  // echoFeedback %
        1362.0f,  // outEQ1Freq Hz
        -3.02f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        100.4f,  // outEQ2Freq Hz
        -8.42f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0633f,  // resonance
        0.0000f,  // echoPingPong
        1.0000f,  // dynAmount
        0.2505f  // dynSpeed
    });

    // Designed for live sound reinforcement (match: 92%)
    presets.push_back({
        "Large Space",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        0.93f,  // modRate Hz
        0.2445f,  // modDepth
        1.0000f,  // width
        0.6705f,  // earlyDiff
        0.4636f,  // lateDiff
        0.75f,  // bassMult
        974.4f,  // bassFreq Hz
        21.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6202f,  // roomSize
        0.6000f,  // earlyLateBal
        1.63f,  // highDecay
        0.54f,  // midDecay
        3067.5f,  // highFreq Hz
        1.0000f,  // erShape
        0.9512f,  // erSpread
        20.0f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1480f,  // stereoCoupling
        1395.2f,  // lowMidFreq Hz
        1.18f,  // lowMidDecay
        1,  // envMode (Gate)
        1003.4f,  // envHold ms
        10.1f,  // envRelease ms
        89.4f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        457.2f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4006.8f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.0000f,  // dynAmount
        0.5002f  // dynSpeed
    });

    // More spacious version of S Vocal Amb (match: 95%)
    presets.push_back({
        "Lg Vocal Amb",
        "Rooms",
        5,  // Ambience
        0,  // 1970s
        0.0752f,  // size
        0.3750f,  // damping
        40.8f,  // predelay ms
        0.22f,  // mix
        0.86f,  // modRate Hz
        0.2469f,  // modDepth
        1.0000f,  // width
        0.7500f,  // earlyDiff
        0.5577f,  // lateDiff
        1.20f,  // bassMult
        775.0f,  // bassFreq Hz
        81.8f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.2493f,  // roomSize
        0.8000f,  // earlyLateBal
        1.66f,  // highDecay
        0.93f,  // midDecay
        3016.0f,  // highFreq Hz
        0.0000f,  // erShape
        0.0375f,  // erSpread
        224.0f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1495f,  // stereoCoupling
        4050.0f,  // lowMidFreq Hz
        1.04f,  // lowMidDecay
        2,  // envMode (Reverse)
        1104.5f,  // envHold ms
        1038.8f,  // envRelease ms
        80.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.7f,  // echoFeedback %
        938.8f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        0.30f,  // outEQ1Q
        4589.8f,  // outEQ2Freq Hz
        -6.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.7500f,  // dynAmount
        0.5000f  // dynSpeed
    });

    // More spacious version of S Vocal Space (match: 79%)
    presets.push_back({
        "Lg Vocal Space",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.3132f,  // size
        0.2231f,  // damping
        74.2f,  // predelay ms
        0.22f,  // mix
        0.82f,  // modRate Hz
        0.2203f,  // modDepth
        1.0000f,  // width
        0.0000f,  // earlyDiff
        0.5499f,  // lateDiff
        0.83f,  // bassMult
        1000.0f,  // bassFreq Hz
        27.5f,  // lowCut Hz
        18784.3f,  // highCut Hz
        false,
        0.6035f,  // roomSize
        0.6000f,  // earlyLateBal
        3.72f,  // highDecay
        0.93f,  // midDecay
        5131.1f,  // highFreq Hz
        1.0000f,  // erShape
        0.0000f,  // erSpread
        164.2f,  // erBassCut Hz
        1.56f,  // trebleRatio
        0.1398f,  // stereoCoupling
        2819.0f,  // lowMidFreq Hz
        0.99f,  // lowMidDecay
        1,  // envMode (Gate)
        501.5f,  // envHold ms
        501.5f,  // envRelease ms
        0.0f,  // envDepth %
        125.1f,  // echoDelay ms
        13.2f,  // echoFeedback %
        1665.3f,  // outEQ1Freq Hz
        -11.31f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4100.1f,  // outEQ2Freq Hz
        3.38f,  // outEQ2Gain dB
        0.67f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2503f  // dynSpeed
    });

    // Soft room with short RT, some stereo width (match: 89%)
    presets.push_back({
        "Living Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0000f,  // size
        0.8916f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        0.81f,  // modRate Hz
        0.2504f,  // modDepth
        0.4995f,  // width
        0.2249f,  // earlyDiff
        0.5556f,  // lateDiff
        1.62f,  // bassMult
        454.4f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.4973f,  // roomSize
        0.7000f,  // earlyLateBal
        2.58f,  // highDecay
        1.01f,  // midDecay
        3998.4f,  // highFreq Hz
        0.5002f,  // erShape
        0.6229f,  // erSpread
        271.3f,  // erBassCut Hz
        0.81f,  // trebleRatio
        0.5000f,  // stereoCoupling
        3066.5f,  // lowMidFreq Hz
        1.19f,  // lowMidDecay
        3,  // envMode (Swell)
        1303.9f,  // envHold ms
        1001.4f,  // envRelease ms
        81.9f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1743.7f,  // outEQ1Freq Hz
        12.00f,  // outEQ1Gain dB
        1.04f,  // outEQ1Q
        7590.1f,  // outEQ2Freq Hz
        -12.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6011f,  // stereoInvert
        0.3255f,  // resonance
        0.0000f,  // echoPingPong
        0.1502f,  // dynAmount
        0.2503f  // dynSpeed
    });

    // Ambience of a locker room (match: 95%)
    presets.push_back({
        "Locker Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.0000f,  // damping
        9.4f,  // predelay ms
        0.22f,  // mix
        0.80f,  // modRate Hz
        0.2552f,  // modDepth
        1.0000f,  // width
        0.7125f,  // earlyDiff
        0.5000f,  // lateDiff
        1.91f,  // bassMult
        212.5f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.3750f,  // roomSize
        0.7000f,  // earlyLateBal
        1.30f,  // highDecay
        1.00f,  // midDecay
        1000.0f,  // highFreq Hz
        0.5000f,  // erShape
        0.5000f,  // erSpread
        20.0f,  // erBassCut Hz
        0.94f,  // trebleRatio
        0.1500f,  // stereoCoupling
        396.2f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        500.0f,  // envHold ms
        500.0f,  // envRelease ms
        80.0f,  // envDepth %
        23.4f,  // echoDelay ms
        0.0f,  // echoFeedback %
        100.0f,  // outEQ1Freq Hz
        -3.21f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4000.0f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Smaller version of Large Room (match: 97%)
    presets.push_back({
        "Medium Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.1595f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        0.76f,  // modRate Hz
        0.2550f,  // modDepth
        1.0000f,  // width
        0.6164f,  // earlyDiff
        0.4929f,  // lateDiff
        1.00f,  // bassMult
        1000.0f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6585f,  // roomSize
        0.6000f,  // earlyLateBal
        3.87f,  // highDecay
        0.92f,  // midDecay
        12000.0f,  // highFreq Hz
        0.1891f,  // erShape
        0.2517f,  // erSpread
        20.0f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1467f,  // stereoCoupling
        7291.4f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        1005.7f,  // envHold ms
        3000.0f,  // envRelease ms
        37.5f,  // envDepth %
        59.1f,  // echoDelay ms
        8.7f,  // echoFeedback %
        908.5f,  // outEQ1Freq Hz
        -7.05f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        5040.9f,  // outEQ2Freq Hz
        -3.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0139f,  // echoPingPong
        1.0000f,  // dynAmount
        0.2502f  // dynSpeed
    });

    // Small, intimate setting, smooth reverb (match: 95%)
    presets.push_back({
        "Medium Space",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0474f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        2.54f,  // modRate Hz
        0.2550f,  // modDepth
        0.9990f,  // width
        0.8159f,  // earlyDiff
        0.3804f,  // lateDiff
        0.33f,  // bassMult
        145.8f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.4527f,  // roomSize
        0.7000f,  // earlyLateBal
        1.54f,  // highDecay
        0.52f,  // midDecay
        2928.6f,  // highFreq Hz
        0.2504f,  // erShape
        0.3922f,  // erSpread
        71.2f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.3505f,  // stereoCoupling
        100.4f,  // lowMidFreq Hz
        0.94f,  // lowMidDecay
        1,  // envMode (Gate)
        345.9f,  // envHold ms
        531.0f,  // envRelease ms
        74.5f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1004.1f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4611.2f,  // outEQ2Freq Hz
        -6.85f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.0000f,  // dynAmount
        0.3958f  // dynSpeed
    });

    // Hotel-like meeting room (match: 84%)
    presets.push_back({
        "Meeting Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.7969f,  // size
        0.1840f,  // damping
        26.7f,  // predelay ms
        0.22f,  // mix
        0.80f,  // modRate Hz
        0.2565f,  // modDepth
        0.1380f,  // width
        0.6835f,  // earlyDiff
        0.4655f,  // lateDiff
        0.84f,  // bassMult
        337.3f,  // bassFreq Hz
        48.4f,  // lowCut Hz
        19884.0f,  // highCut Hz
        false,
        0.5718f,  // roomSize
        0.6000f,  // earlyLateBal
        2.41f,  // highDecay
        1.42f,  // midDecay
        4310.7f,  // highFreq Hz
        0.4987f,  // erShape
        1.0000f,  // erSpread
        299.4f,  // erBassCut Hz
        1.94f,  // trebleRatio
        0.1492f,  // stereoCoupling
        2173.9f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        1,  // envMode (Gate)
        300.3f,  // envHold ms
        274.1f,  // envRelease ms
        99.7f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1002.6f,  // outEQ1Freq Hz
        0.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        6022.3f,  // outEQ2Freq Hz
        3.06f,  // outEQ2Gain dB
        1.55f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0990f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.5003f  // dynSpeed
    });

    // Resonant drum preset, very small Size/Mid RT (match: 85%)
    presets.push_back({
        "Metallic Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.0000f,  // damping
        9.3f,  // predelay ms
        0.22f,  // mix
        1.06f,  // modRate Hz
        0.2558f,  // modDepth
        0.0533f,  // width
        0.9661f,  // earlyDiff
        0.5222f,  // lateDiff
        1.55f,  // bassMult
        550.9f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.3724f,  // roomSize
        0.6000f,  // earlyLateBal
        1.35f,  // highDecay
        1.09f,  // midDecay
        1000.3f,  // highFreq Hz
        1.0000f,  // erShape
        0.2804f,  // erSpread
        122.0f,  // erBassCut Hz
        0.93f,  // trebleRatio
        0.1486f,  // stereoCoupling
        985.2f,  // lowMidFreq Hz
        1.02f,  // lowMidDecay
        1,  // envMode (Gate)
        991.5f,  // envHold ms
        10.1f,  // envRelease ms
        100.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        897.2f,  // outEQ1Freq Hz
        -9.51f,  // outEQ1Gain dB
        0.63f,  // outEQ1Q
        4007.2f,  // outEQ2Freq Hz
        -3.05f,  // outEQ2Gain dB
        1.01f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2501f  // dynSpeed
    });

    // Chamber/Room for organ and other keyboards (match: 91%)
    presets.push_back({
        "Organ Room",
        "Rooms",
        3,  // Chamber
        0,  // 1970s
        0.1733f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        1.00f,  // modRate Hz
        0.2487f,  // modDepth
        1.0000f,  // width
        0.3758f,  // earlyDiff
        0.5590f,  // lateDiff
        0.90f,  // bassMult
        100.1f,  // bassFreq Hz
        95.7f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.9316f,  // roomSize
        0.6000f,  // earlyLateBal
        4.00f,  // highDecay
        1.01f,  // midDecay
        6183.3f,  // highFreq Hz
        1.0000f,  // erShape
        0.0000f,  // erSpread
        86.2f,  // erBassCut Hz
        1.38f,  // trebleRatio
        0.1489f,  // stereoCoupling
        2078.6f,  // lowMidFreq Hz
        0.97f,  // lowMidDecay
        0,  // envMode (Off)
        505.0f,  // envHold ms
        505.2f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1563.6f,  // outEQ1Freq Hz
        -8.35f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        3929.5f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        0.50f,  // outEQ2Q
        0.3060f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.9989f,  // dynAmount
        0.2502f  // dynSpeed
    });

    // Takes you back to the good old days (match: 96%)
    presets.push_back({
        "PCM 60 Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.2483f,  // size
        0.0000f,  // damping
        23.5f,  // predelay ms
        0.22f,  // mix
        0.82f,  // modRate Hz
        0.2516f,  // modDepth
        1.0000f,  // width
        0.5770f,  // earlyDiff
        0.4850f,  // lateDiff
        0.87f,  // bassMult
        535.1f,  // bassFreq Hz
        21.6f,  // lowCut Hz
        19525.3f,  // highCut Hz
        false,
        0.5988f,  // roomSize
        0.6000f,  // earlyLateBal
        0.82f,  // highDecay
        1.00f,  // midDecay
        2104.4f,  // highFreq Hz
        0.3639f,  // erShape
        0.6817f,  // erSpread
        176.0f,  // erBassCut Hz
        1.98f,  // trebleRatio
        0.1494f,  // stereoCoupling
        100.0f,  // lowMidFreq Hz
        1.01f,  // lowMidDecay
        0,  // envMode (Off)
        501.3f,  // envHold ms
        501.3f,  // envRelease ms
        0.0f,  // envDepth %
        62.5f,  // echoDelay ms
        2.5f,  // echoFeedback %
        520.3f,  // outEQ1Freq Hz
        -6.05f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4618.4f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0076f,  // stereoInvert
        0.0281f,  // resonance
        0.0000f,  // echoPingPong
        1.0000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Full and resonant reverb, accentuates transients (match: 95%)
    presets.push_back({
        "Percussion Place",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.1459f,  // damping
        31.3f,  // predelay ms
        0.22f,  // mix
        0.83f,  // modRate Hz
        0.2523f,  // modDepth
        1.0000f,  // width
        0.8960f,  // earlyDiff
        0.4254f,  // lateDiff
        1.39f,  // bassMult
        620.0f,  // bassFreq Hz
        20.3f,  // lowCut Hz
        19214.3f,  // highCut Hz
        false,
        0.4419f,  // roomSize
        0.7000f,  // earlyLateBal
        1.62f,  // highDecay
        0.65f,  // midDecay
        1186.6f,  // highFreq Hz
        0.5032f,  // erShape
        0.5903f,  // erSpread
        176.5f,  // erBassCut Hz
        0.73f,  // trebleRatio
        0.1493f,  // stereoCoupling
        1099.3f,  // lowMidFreq Hz
        1.17f,  // lowMidDecay
        0,  // envMode (Off)
        501.4f,  // envHold ms
        501.4f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        919.7f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        6443.6f,  // outEQ2Freq Hz
        3.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2504f  // dynSpeed
    });

    // Similar to PercussPlace, slightly smaller (match: 96%)
    presets.push_back({
        "Percussion Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.1249f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        0.38f,  // modRate Hz
        0.3380f,  // modDepth
        0.7965f,  // width
        0.8980f,  // earlyDiff
        0.5034f,  // lateDiff
        0.96f,  // bassMult
        100.0f,  // bassFreq Hz
        22.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.4982f,  // roomSize
        0.6000f,  // earlyLateBal
        1.19f,  // highDecay
        1.04f,  // midDecay
        3485.0f,  // highFreq Hz
        0.5199f,  // erShape
        0.6246f,  // erSpread
        260.0f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1508f,  // stereoCoupling
        4593.4f,  // lowMidFreq Hz
        1.02f,  // lowMidDecay
        0,  // envMode (Off)
        499.0f,  // envHold ms
        500.7f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1002.4f,  // outEQ1Freq Hz
        0.00f,  // outEQ1Gain dB
        1.25f,  // outEQ1Q
        4050.0f,  // outEQ2Freq Hz
        -3.01f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6004f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        1.0000f,  // dynAmount
        0.2502f  // dynSpeed
    });

    // How much sound can you squeeze into a phone booth? (match: 80%)
    presets.push_back({
        "Phone Booth",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0000f,  // size
        0.0000f,  // damping
        21.0f,  // predelay ms
        0.22f,  // mix
        0.83f,  // modRate Hz
        0.2536f,  // modDepth
        0.2501f,  // width
        0.7383f,  // earlyDiff
        0.4747f,  // lateDiff
        3.00f,  // bassMult
        1000.0f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        18691.3f,  // highCut Hz
        false,
        1.0000f,  // roomSize
        0.6000f,  // earlyLateBal
        0.72f,  // highDecay
        1.29f,  // midDecay
        2250.1f,  // highFreq Hz
        0.2519f,  // erShape
        0.8530f,  // erSpread
        20.0f,  // erBassCut Hz
        1.93f,  // trebleRatio
        0.0188f,  // stereoCoupling
        1084.8f,  // lowMidFreq Hz
        0.25f,  // lowMidDecay
        1,  // envMode (Gate)
        10.0f,  // envHold ms
        327.5f,  // envRelease ms
        79.3f,  // envDepth %
        93.7f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1728.5f,  // outEQ1Freq Hz
        12.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4126.3f,  // outEQ2Freq Hz
        12.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6013f,  // stereoInvert
        0.3002f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2499f  // dynSpeed
    });

    // Natural reverb for a live setting (match: 95%)
    presets.push_back({
        "Real Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.0000f,  // damping
        1.0f,  // predelay ms
        0.22f,  // mix
        0.85f,  // modRate Hz
        0.2484f,  // modDepth
        1.0000f,  // width
        0.6250f,  // earlyDiff
        0.5048f,  // lateDiff
        1.39f,  // bassMult
        409.4f,  // bassFreq Hz
        60.5f,  // lowCut Hz
        19896.0f,  // highCut Hz
        false,
        0.6946f,  // roomSize
        0.6000f,  // earlyLateBal
        1.63f,  // highDecay
        1.03f,  // midDecay
        3750.1f,  // highFreq Hz
        0.5026f,  // erShape
        0.1278f,  // erSpread
        499.8f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1510f,  // stereoCoupling
        3672.0f,  // lowMidFreq Hz
        1.05f,  // lowMidDecay
        0,  // envMode (Off)
        501.1f,  // envHold ms
        502.4f,  // envRelease ms
        0.0f,  // envDepth %
        37.7f,  // echoDelay ms
        0.0f,  // echoFeedback %
        100.0f,  // outEQ1Freq Hz
        2.99f,  // outEQ1Gain dB
        1.01f,  // outEQ1Q
        5037.6f,  // outEQ2Freq Hz
        -3.03f,  // outEQ2Gain dB
        1.03f,  // outEQ2Q
        0.6000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Super-saturated, atmospheric quality (match: 90%)
    presets.push_back({
        "Reflect Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.1245f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        0.83f,  // modRate Hz
        0.2566f,  // modDepth
        1.0000f,  // width
        0.6996f,  // earlyDiff
        0.5773f,  // lateDiff
        0.66f,  // bassMult
        1000.0f,  // bassFreq Hz
        20.6f,  // lowCut Hz
        17119.1f,  // highCut Hz
        false,
        0.5172f,  // roomSize
        0.6000f,  // earlyLateBal
        1.66f,  // highDecay
        0.63f,  // midDecay
        3451.1f,  // highFreq Hz
        0.0000f,  // erShape
        0.9261f,  // erSpread
        122.2f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.2634f,  // stereoCoupling
        3074.7f,  // lowMidFreq Hz
        0.25f,  // lowMidDecay
        0,  // envMode (Off)
        209.1f,  // envHold ms
        663.0f,  // envRelease ms
        0.0f,  // envDepth %
        57.3f,  // echoDelay ms
        0.0f,  // echoFeedback %
        993.6f,  // outEQ1Freq Hz
        12.00f,  // outEQ1Gain dB
        1.07f,  // outEQ1Q
        4045.5f,  // outEQ2Freq Hz
        -7.09f,  // outEQ2Gain dB
        0.93f,  // outEQ2Q
        0.6017f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -0.2492f,  // dynAmount
        0.6250f  // dynSpeed
    });

    // Extremely bright live drum sound (match: 91%)
    presets.push_back({
        "Rock Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.2619f,  // size
        0.1063f,  // damping
        18.4f,  // predelay ms
        0.22f,  // mix
        0.79f,  // modRate Hz
        0.2500f,  // modDepth
        1.0000f,  // width
        0.6250f,  // earlyDiff
        0.5002f,  // lateDiff
        0.65f,  // bassMult
        330.6f,  // bassFreq Hz
        60.3f,  // lowCut Hz
        19740.7f,  // highCut Hz
        false,
        0.8885f,  // roomSize
        0.8000f,  // earlyLateBal
        2.14f,  // highDecay
        0.51f,  // midDecay
        3020.7f,  // highFreq Hz
        0.0441f,  // erShape
        0.5759f,  // erSpread
        146.0f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1500f,  // stereoCoupling
        933.2f,  // lowMidFreq Hz
        1.09f,  // lowMidDecay
        0,  // envMode (Off)
        500.0f,  // envHold ms
        500.0f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        508.8f,  // outEQ1Freq Hz
        -10.72f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4000.1f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.6000f,  // dynAmount
        0.5000f  // dynSpeed
    });

    // All you could ever want for drums — punch, attitude (match: 97%)
    presets.push_back({
        "Room 4 Drums",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.4891f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        0.87f,  // modRate Hz
        0.2515f,  // modDepth
        0.3793f,  // width
        0.6011f,  // earlyDiff
        0.5488f,  // lateDiff
        0.80f,  // bassMult
        357.3f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6180f,  // roomSize
        0.6000f,  // earlyLateBal
        0.96f,  // highDecay
        0.53f,  // midDecay
        1000.0f,  // highFreq Hz
        0.2488f,  // erShape
        0.9465f,  // erSpread
        163.9f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1487f,  // stereoCoupling
        2071.4f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        1,  // envMode (Gate)
        1156.1f,  // envHold ms
        10.1f,  // envRelease ms
        100.0f,  // envDepth %
        62.2f,  // echoDelay ms
        0.0f,  // echoFeedback %
        100.2f,  // outEQ1Freq Hz
        -9.59f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4000.2f,  // outEQ2Freq Hz
        -4.80f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.3251f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.5998f,  // dynAmount
        0.2499f  // dynSpeed
    });

    // Dark and wet reverb, medium room long reverb tail (match: 96%)
    presets.push_back({
        "Slap Place",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.2199f,  // size
        0.1064f,  // damping
        73.9f,  // predelay ms
        0.22f,  // mix
        0.81f,  // modRate Hz
        0.1536f,  // modDepth
        1.0000f,  // width
        1.0000f,  // earlyDiff
        0.5577f,  // lateDiff
        0.49f,  // bassMult
        325.4f,  // bassFreq Hz
        61.9f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.7501f,  // roomSize
        0.6000f,  // earlyLateBal
        0.89f,  // highDecay
        0.51f,  // midDecay
        4329.5f,  // highFreq Hz
        0.0000f,  // erShape
        0.1247f,  // erSpread
        111.3f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1490f,  // stereoCoupling
        2077.8f,  // lowMidFreq Hz
        1.10f,  // lowMidDecay
        1,  // envMode (Gate)
        757.3f,  // envHold ms
        1132.8f,  // envRelease ms
        96.4f,  // envDepth %
        80.1f,  // echoDelay ms
        13.0f,  // echoFeedback %
        1003.1f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        0.43f,  // outEQ1Q
        4599.2f,  // outEQ2Freq Hz
        -3.01f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -0.6008f,  // dynAmount
        0.2503f  // dynSpeed
    });

    // Unnatural room reverb, enhances any drum track (match: 95%)
    presets.push_back({
        "Sloppy Place",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.0000f,  // damping
        71.7f,  // predelay ms
        0.22f,  // mix
        0.64f,  // modRate Hz
        0.2497f,  // modDepth
        1.0000f,  // width
        0.6437f,  // earlyDiff
        0.1507f,  // lateDiff
        0.46f,  // bassMult
        438.3f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.9646f,  // roomSize
        0.6000f,  // earlyLateBal
        4.00f,  // highDecay
        1.00f,  // midDecay
        5639.7f,  // highFreq Hz
        0.1255f,  // erShape
        0.9606f,  // erSpread
        45.6f,  // erBassCut Hz
        1.36f,  // trebleRatio
        0.1885f,  // stereoCoupling
        4478.2f,  // lowMidFreq Hz
        0.95f,  // lowMidDecay
        1,  // envMode (Gate)
        747.7f,  // envHold ms
        121.5f,  // envRelease ms
        99.1f,  // envDepth %
        191.2f,  // echoDelay ms
        0.0f,  // echoFeedback %
        942.9f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        2283.2f,  // outEQ2Freq Hz
        -7.09f,  // outEQ2Gain dB
        1.01f,  // outEQ2Q
        0.2320f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -0.2494f,  // dynAmount
        0.5465f  // dynSpeed
    });

    // Spacious version of S Vocal Amb, set to Studio A (match: 68%)
    presets.push_back({
        "Sm Vocal Amb",
        "Rooms",
        5,  // Ambience
        0,  // 1970s
        0.0000f,  // size
        0.1876f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        1.62f,  // modRate Hz
        0.1654f,  // modDepth
        1.0000f,  // width
        0.6205f,  // earlyDiff
        0.4489f,  // lateDiff
        2.00f,  // bassMult
        103.1f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.7508f,  // roomSize
        0.6000f,  // earlyLateBal
        1.64f,  // highDecay
        0.87f,  // midDecay
        9998.0f,  // highFreq Hz
        0.4410f,  // erShape
        0.5683f,  // erSpread
        79.3f,  // erBassCut Hz
        1.91f,  // trebleRatio
        0.3969f,  // stereoCoupling
        1186.2f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        3,  // envMode (Swell)
        510.5f,  // envHold ms
        500.0f,  // envRelease ms
        81.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1000.2f,  // outEQ1Freq Hz
        12.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4132.4f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.7500f,  // dynAmount
        0.5000f  // dynSpeed
    });

    // Bigger version of S VocalSpace (match: 96%)
    presets.push_back({
        "Sm Vocal Space",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.6491f,  // damping
        26.5f,  // predelay ms
        0.22f,  // mix
        0.85f,  // modRate Hz
        0.2568f,  // modDepth
        0.7221f,  // width
        0.5158f,  // earlyDiff
        0.5585f,  // lateDiff
        0.94f,  // bassMult
        769.8f,  // bassFreq Hz
        20.5f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.7600f,  // roomSize
        0.6000f,  // earlyLateBal
        4.00f,  // highDecay
        0.91f,  // midDecay
        5130.7f,  // highFreq Hz
        0.5059f,  // erShape
        0.0000f,  // erSpread
        401.4f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1492f,  // stereoCoupling
        7322.4f,  // lowMidFreq Hz
        0.99f,  // lowMidDecay
        0,  // envMode (Off)
        505.5f,  // envHold ms
        199.6f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        918.4f,  // outEQ1Freq Hz
        -1.86f,  // outEQ1Gain dB
        0.39f,  // outEQ1Q
        3349.0f,  // outEQ2Freq Hz
        -1.80f,  // outEQ2Gain dB
        1.12f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.0000f,  // dynAmount
        0.5006f  // dynSpeed
    });

    // Similar to Large Chamber with tighter Mid RT/size (match: 96%)
    presets.push_back({
        "Small Chamber",
        "Rooms",
        3,  // Chamber
        0,  // 1970s
        0.0752f,  // size
        0.6274f,  // damping
        10.5f,  // predelay ms
        0.22f,  // mix
        1.11f,  // modRate Hz
        0.2570f,  // modDepth
        1.0000f,  // width
        0.5456f,  // earlyDiff
        0.5588f,  // lateDiff
        1.60f,  // bassMult
        593.2f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6226f,  // roomSize
        0.6000f,  // earlyLateBal
        1.36f,  // highDecay
        1.02f,  // midDecay
        1447.4f,  // highFreq Hz
        0.5009f,  // erShape
        0.0000f,  // erSpread
        440.1f,  // erBassCut Hz
        0.74f,  // trebleRatio
        0.1428f,  // stereoCoupling
        1099.5f,  // lowMidFreq Hz
        1.01f,  // lowMidDecay
        1,  // envMode (Gate)
        995.8f,  // envHold ms
        578.9f,  // envRelease ms
        100.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1005.1f,  // outEQ1Freq Hz
        0.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4015.1f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.01f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.0000f,  // dynAmount
        0.2504f  // dynSpeed
    });

    // Tight, smooth and natural sounding room (match: 97%)
    presets.push_back({
        "Small Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.6270f,  // damping
        0.9f,  // predelay ms
        0.22f,  // mix
        0.83f,  // modRate Hz
        0.2441f,  // modDepth
        0.9876f,  // width
        0.6566f,  // earlyDiff
        0.5012f,  // lateDiff
        0.90f,  // bassMult
        328.6f,  // bassFreq Hz
        20.4f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5473f,  // roomSize
        0.6000f,  // earlyLateBal
        0.79f,  // highDecay
        0.57f,  // midDecay
        1003.2f,  // highFreq Hz
        0.1973f,  // erShape
        0.3742f,  // erSpread
        108.9f,  // erBassCut Hz
        1.02f,  // trebleRatio
        0.1531f,  // stereoCoupling
        988.5f,  // lowMidFreq Hz
        1.01f,  // lowMidDecay
        1,  // envMode (Gate)
        618.2f,  // envHold ms
        2389.8f,  // envRelease ms
        87.8f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1004.9f,  // outEQ1Freq Hz
        0.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4019.6f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2507f  // dynSpeed
    });

    // Large room, short Mid RT, Spatial EQ bass boost (match: 93%)
    presets.push_back({
        "Snare Trash",
        "Rooms",
        9,  // Dirty Hall
        0,  // 1970s
        0.0752f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        0.76f,  // modRate Hz
        0.2607f,  // modDepth
        0.9169f,  // width
        0.8413f,  // earlyDiff
        0.6250f,  // lateDiff
        0.65f,  // bassMult
        1000.0f,  // bassFreq Hz
        139.6f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.2516f,  // roomSize
        0.5000f,  // earlyLateBal
        1.33f,  // highDecay
        0.48f,  // midDecay
        4013.6f,  // highFreq Hz
        0.5017f,  // erShape
        0.9558f,  // erSpread
        45.5f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1875f,  // stereoCoupling
        3062.8f,  // lowMidFreq Hz
        0.97f,  // lowMidDecay
        0,  // envMode (Off)
        503.1f,  // envHold ms
        503.1f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        897.4f,  // outEQ1Freq Hz
        -6.00f,  // outEQ1Gain dB
        1.26f,  // outEQ1Q
        5056.4f,  // outEQ2Freq Hz
        3.05f,  // outEQ2Gain dB
        0.47f,  // outEQ2Q
        0.3000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.0000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Spatial EQ bass boost enhances lower frequencies (match: 94%)
    presets.push_back({
        "Spatial Bass",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        0.29f,  // modRate Hz
        0.2564f,  // modDepth
        1.0000f,  // width
        0.2500f,  // earlyDiff
        0.5001f,  // lateDiff
        0.41f,  // bassMult
        1000.0f,  // bassFreq Hz
        20.3f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.7243f,  // roomSize
        0.9000f,  // earlyLateBal
        0.94f,  // highDecay
        0.41f,  // midDecay
        2969.7f,  // highFreq Hz
        0.2473f,  // erShape
        0.5208f,  // erSpread
        69.7f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.4001f,  // stereoCoupling
        1087.4f,  // lowMidFreq Hz
        1.36f,  // lowMidDecay
        0,  // envMode (Off)
        574.7f,  // envHold ms
        502.1f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1001.4f,  // outEQ1Freq Hz
        11.10f,  // outEQ1Gain dB
        1.04f,  // outEQ1Q
        4085.7f,  // outEQ2Freq Hz
        -9.00f,  // outEQ2Gain dB
        0.99f,  // outEQ2Q
        0.5999f,  // stereoInvert
        0.6001f,  // resonance
        0.0000f,  // echoPingPong
        1.0000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Similar to SpinningRoom with different parameters (match: 87%)
    presets.push_back({
        "Spatial Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.5003f,  // damping
        41.3f,  // predelay ms
        0.22f,  // mix
        0.80f,  // modRate Hz
        0.2528f,  // modDepth
        1.0000f,  // width
        0.3611f,  // earlyDiff
        0.5540f,  // lateDiff
        1.35f,  // bassMult
        195.4f,  // bassFreq Hz
        29.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.7496f,  // roomSize
        0.6000f,  // earlyLateBal
        1.11f,  // highDecay
        0.66f,  // midDecay
        1000.7f,  // highFreq Hz
        1.0000f,  // erShape
        0.0000f,  // erSpread
        200.1f,  // erBassCut Hz
        0.94f,  // trebleRatio
        0.1124f,  // stereoCoupling
        915.6f,  // lowMidFreq Hz
        1.19f,  // lowMidDecay
        0,  // envMode (Off)
        871.8f,  // envHold ms
        1161.1f,  // envRelease ms
        0.0f,  // envDepth %
        62.5f,  // echoDelay ms
        8.0f,  // echoFeedback %
        1088.3f,  // outEQ1Freq Hz
        -0.08f,  // outEQ1Gain dB
        0.30f,  // outEQ1Q
        2502.7f,  // outEQ2Freq Hz
        12.00f,  // outEQ2Gain dB
        1.08f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0101f,  // echoPingPong
        -0.1501f,  // dynAmount
        0.2502f  // dynSpeed
    });

    // Nice Ambience reverb with circular sweep of Out Width (match: 95%)
    presets.push_back({
        "Spinning Room",
        "Rooms",
        7,  // Chorus Space
        0,  // 1970s
        0.0752f,  // size
        0.0000f,  // damping
        27.3f,  // predelay ms
        0.22f,  // mix
        1.55f,  // modRate Hz
        0.2500f,  // modDepth
        0.7875f,  // width
        0.8227f,  // earlyDiff
        0.5621f,  // lateDiff
        1.53f,  // bassMult
        435.5f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.1287f,  // roomSize
        1.0000f,  // earlyLateBal
        1.34f,  // highDecay
        1.03f,  // midDecay
        1000.9f,  // highFreq Hz
        0.8757f,  // erShape
        0.0000f,  // erSpread
        20.0f,  // erBassCut Hz
        0.99f,  // trebleRatio
        0.1493f,  // stereoCoupling
        843.0f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        502.0f,  // envHold ms
        502.0f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1030.1f,  // outEQ1Freq Hz
        1.80f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4004.7f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        1.0000f,  // dynAmount
        0.2502f  // dynSpeed
    });

    // Chamber/Room where a small and big room are mixed (match: 96%)
    presets.push_back({
        "Split Rooms",
        "Rooms",
        3,  // Chamber
        0,  // 1970s
        0.3008f,  // size
        0.0000f,  // damping
        18.8f,  // predelay ms
        0.22f,  // mix
        0.80f,  // modRate Hz
        0.2492f,  // modDepth
        1.0000f,  // width
        0.5404f,  // earlyDiff
        0.5463f,  // lateDiff
        1.16f,  // bassMult
        815.5f,  // bassFreq Hz
        20.5f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        1.0000f,  // roomSize
        0.7000f,  // earlyLateBal
        4.00f,  // highDecay
        0.63f,  // midDecay
        1822.4f,  // highFreq Hz
        0.5033f,  // erShape
        0.4724f,  // erSpread
        152.2f,  // erBassCut Hz
        1.44f,  // trebleRatio
        0.1490f,  // stereoCoupling
        1600.8f,  // lowMidFreq Hz
        1.67f,  // lowMidDecay
        0,  // envMode (Off)
        788.5f,  // envHold ms
        911.8f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        501.5f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        0.99f,  // outEQ1Q
        8000.0f,  // outEQ2Freq Hz
        6.06f,  // outEQ2Gain dB
        0.61f,  // outEQ2Q
        0.6021f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -0.5996f,  // dynAmount
        0.2505f  // dynSpeed
    });

    // Metallic sound and bright resonance (match: 96%)
    presets.push_back({
        "Storage Tank",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.2483f,  // size
        0.0000f,  // damping
        51.3f,  // predelay ms
        0.22f,  // mix
        0.80f,  // modRate Hz
        0.2167f,  // modDepth
        0.7413f,  // width
        0.6349f,  // earlyDiff
        0.5149f,  // lateDiff
        0.90f,  // bassMult
        376.1f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6999f,  // roomSize
        0.6000f,  // earlyLateBal
        1.67f,  // highDecay
        0.95f,  // midDecay
        12000.0f,  // highFreq Hz
        0.9978f,  // erShape
        0.3468f,  // erSpread
        20.1f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1511f,  // stereoCoupling
        7460.4f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        1,  // envMode (Gate)
        1051.3f,  // envHold ms
        518.1f,  // envRelease ms
        80.2f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1000.0f,  // outEQ1Freq Hz
        0.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4000.0f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        1.0000f,  // dynAmount
        0.3967f  // dynSpeed
    });

    // Customize how empty or full this storeroom is (match: 95%)
    presets.push_back({
        "Storeroom",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.3253f,  // size
        0.0000f,  // damping
        31.7f,  // predelay ms
        0.22f,  // mix
        0.82f,  // modRate Hz
        0.2747f,  // modDepth
        1.0000f,  // width
        0.6248f,  // earlyDiff
        0.4391f,  // lateDiff
        1.39f,  // bassMult
        369.2f,  // bassFreq Hz
        20.4f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.7633f,  // roomSize
        0.6000f,  // earlyLateBal
        2.93f,  // highDecay
        0.78f,  // midDecay
        2413.1f,  // highFreq Hz
        0.6254f,  // erShape
        0.7503f,  // erSpread
        20.0f,  // erBassCut Hz
        1.83f,  // trebleRatio
        0.1483f,  // stereoCoupling
        2370.2f,  // lowMidFreq Hz
        1.07f,  // lowMidDecay
        0,  // envMode (Off)
        502.2f,  // envHold ms
        502.2f,  // envRelease ms
        0.0f,  // envDepth %
        106.3f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1730.4f,  // outEQ1Freq Hz
        -8.71f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4013.1f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.0000f,  // dynAmount
        0.2502f  // dynSpeed
    });

    // Super-tight concert hall with lots of spatial enhancement (match: 64%)
    presets.push_back({
        "Strange Place",
        "Rooms",
        7,  // Chorus Space
        0,  // 1970s
        0.0000f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        0.85f,  // modRate Hz
        0.1364f,  // modDepth
        0.9058f,  // width
        0.8707f,  // earlyDiff
        0.5029f,  // lateDiff
        0.82f,  // bassMult
        662.5f,  // bassFreq Hz
        20.6f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.4992f,  // roomSize
        0.6000f,  // earlyLateBal
        0.80f,  // highDecay
        0.50f,  // midDecay
        2999.1f,  // highFreq Hz
        0.2037f,  // erShape
        0.9630f,  // erSpread
        317.4f,  // erBassCut Hz
        1.10f,  // trebleRatio
        0.5000f,  // stereoCoupling
        1914.9f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        1,  // envMode (Gate)
        258.9f,  // envHold ms
        327.7f,  // envRelease ms
        100.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1000.7f,  // outEQ1Freq Hz
        1.80f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4002.8f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Vibrancy and attitude with a gated feel (match: 94%)
    presets.push_back({
        "Tight Space",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.0000f,  // damping
        165.0f,  // predelay ms
        0.22f,  // mix
        0.10f,  // modRate Hz
        0.2525f,  // modDepth
        1.0000f,  // width
        0.8344f,  // earlyDiff
        0.3528f,  // lateDiff
        0.69f,  // bassMult
        369.0f,  // bassFreq Hz
        48.9f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.2505f,  // roomSize
        0.7000f,  // earlyLateBal
        2.80f,  // highDecay
        0.28f,  // midDecay
        4483.1f,  // highFreq Hz
        1.0000f,  // erShape
        0.0000f,  // erSpread
        440.8f,  // erBassCut Hz
        1.92f,  // trebleRatio
        0.3268f,  // stereoCoupling
        1732.5f,  // lowMidFreq Hz
        0.95f,  // lowMidDecay
        1,  // envMode (Gate)
        501.3f,  // envHold ms
        520.3f,  // envRelease ms
        100.0f,  // envDepth %
        89.1f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1011.5f,  // outEQ1Freq Hz
        -6.02f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4017.8f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -0.6011f,  // dynAmount
        0.2504f  // dynSpeed
    });

    // Incredibly sibilant and bright reverberant space (match: 94%)
    presets.push_back({
        "Tiled Room",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.0000f,  // damping
        34.5f,  // predelay ms
        0.22f,  // mix
        0.78f,  // modRate Hz
        0.2495f,  // modDepth
        1.0000f,  // width
        0.4982f,  // earlyDiff
        0.3933f,  // lateDiff
        1.22f,  // bassMult
        333.4f,  // bassFreq Hz
        20.6f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        1.0000f,  // roomSize
        0.7000f,  // earlyLateBal
        1.69f,  // highDecay
        0.49f,  // midDecay
        3735.5f,  // highFreq Hz
        0.9957f,  // erShape
        0.0000f,  // erSpread
        260.5f,  // erBassCut Hz
        1.15f,  // trebleRatio
        0.1511f,  // stereoCoupling
        1374.2f,  // lowMidFreq Hz
        0.96f,  // lowMidDecay
        0,  // envMode (Off)
        419.5f,  // envHold ms
        502.6f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1000.7f,  // outEQ1Freq Hz
        0.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4001.3f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2501f  // dynSpeed
    });

    // Just like Vocal Amb, but smaller and tighter (match: 94%)
    presets.push_back({
        "Very Small Amb",
        "Rooms",
        5,  // Ambience
        0,  // 1970s
        0.0752f,  // size
        0.4518f,  // damping
        11.9f,  // predelay ms
        0.22f,  // mix
        0.82f,  // modRate Hz
        0.2552f,  // modDepth
        1.0000f,  // width
        0.7524f,  // earlyDiff
        0.5505f,  // lateDiff
        1.19f,  // bassMult
        437.5f,  // bassFreq Hz
        59.3f,  // lowCut Hz
        19543.7f,  // highCut Hz
        false,
        0.2500f,  // roomSize
        0.6000f,  // earlyLateBal
        0.82f,  // highDecay
        0.52f,  // midDecay
        1013.8f,  // highFreq Hz
        0.8750f,  // erShape
        0.2474f,  // erSpread
        464.0f,  // erBassCut Hz
        0.94f,  // trebleRatio
        0.1500f,  // stereoCoupling
        878.2f,  // lowMidFreq Hz
        1.02f,  // lowMidDecay
        3,  // envMode (Swell)
        1263.0f,  // envHold ms
        1000.0f,  // envRelease ms
        82.1f,  // envDepth %
        62.5f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1000.0f,  // outEQ1Freq Hz
        -1.80f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4247.5f,  // outEQ2Freq Hz
        -1.20f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -0.6000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Short and soft, very realistic small room (match: 86%)
    presets.push_back({
        "Vocal Ambience",
        "Rooms",
        5,  // Ambience
        0,  // 1970s
        0.0752f,  // size
        0.3559f,  // damping
        41.4f,  // predelay ms
        0.22f,  // mix
        0.81f,  // modRate Hz
        0.2548f,  // modDepth
        1.0000f,  // width
        0.8913f,  // earlyDiff
        0.4862f,  // lateDiff
        0.40f,  // bassMult
        325.0f,  // bassFreq Hz
        21.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.1064f,  // roomSize
        0.6000f,  // earlyLateBal
        0.67f,  // highDecay
        0.50f,  // midDecay
        4477.7f,  // highFreq Hz
        0.7886f,  // erShape
        0.4548f,  // erSpread
        41.3f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1250f,  // stereoCoupling
        3458.7f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        3,  // envMode (Swell)
        1005.0f,  // envHold ms
        1000.0f,  // envRelease ms
        80.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1010.0f,  // outEQ1Freq Hz
        3.02f,  // outEQ1Gain dB
        1.05f,  // outEQ1Q
        4008.4f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.0000f,  // dynAmount
        0.5000f  // dynSpeed
    });

    // Most confining of isolation booths (match: 89%)
    presets.push_back({
        "Vocal Booth",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0000f,  // size
        0.5891f,  // damping
        0.0f,  // predelay ms
        0.22f,  // mix
        0.82f,  // modRate Hz
        0.2555f,  // modDepth
        0.8081f,  // width
        0.5467f,  // earlyDiff
        0.7907f,  // lateDiff
        0.10f,  // bassMult
        187.7f,  // bassFreq Hz
        20.6f,  // lowCut Hz
        18476.2f,  // highCut Hz
        false,
        0.5745f,  // roomSize
        0.6000f,  // earlyLateBal
        1.18f,  // highDecay
        0.72f,  // midDecay
        2394.1f,  // highFreq Hz
        0.7205f,  // erShape
        0.9965f,  // erSpread
        140.1f,  // erBassCut Hz
        0.30f,  // trebleRatio
        0.4324f,  // stereoCoupling
        2355.2f,  // lowMidFreq Hz
        0.53f,  // lowMidDecay
        1,  // envMode (Gate)
        298.5f,  // envHold ms
        10.0f,  // envRelease ms
        72.5f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1323.5f,  // outEQ1Freq Hz
        12.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        3471.5f,  // outEQ2Freq Hz
        5.72f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6017f,  // stereoInvert
        0.6017f,  // resonance
        0.0000f,  // echoPingPong
        0.4019f,  // dynAmount
        0.6254f  // dynSpeed
    });

    // Short Mid RT and small Size — ideal for vocals (match: 89%)
    presets.push_back({
        "Vocal Space",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.1733f,  // size
        0.6278f,  // damping
        116.5f,  // predelay ms
        0.22f,  // mix
        0.79f,  // modRate Hz
        0.3352f,  // modDepth
        1.0000f,  // width
        1.0000f,  // earlyDiff
        0.5000f,  // lateDiff
        0.91f,  // bassMult
        442.2f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        17303.4f,  // highCut Hz
        false,
        0.4034f,  // roomSize
        1.0000f,  // earlyLateBal
        1.19f,  // highDecay
        0.64f,  // midDecay
        6643.5f,  // highFreq Hz
        0.0000f,  // erShape
        0.9115f,  // erSpread
        20.0f,  // erBassCut Hz
        1.88f,  // trebleRatio
        0.1500f,  // stereoCoupling
        2494.6f,  // lowMidFreq Hz
        1.03f,  // lowMidDecay
        1,  // envMode (Gate)
        1342.8f,  // envHold ms
        421.2f,  // envRelease ms
        62.5f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1488.5f,  // outEQ1Freq Hz
        4.81f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        5030.3f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.4811f,  // stereoInvert
        0.0000f,  // resonance
        0.0162f,  // echoPingPong
        1.0000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Big, wide space with dark, somber effect (match: 89%)
    presets.push_back({
        "Wide Chamber",
        "Rooms",
        3,  // Chamber
        0,  // 1970s
        0.2483f,  // size
        0.2518f,  // damping
        89.1f,  // predelay ms
        0.22f,  // mix
        0.82f,  // modRate Hz
        0.2582f,  // modDepth
        0.9821f,  // width
        0.7241f,  // earlyDiff
        0.5577f,  // lateDiff
        0.98f,  // bassMult
        371.4f,  // bassFreq Hz
        20.8f,  // lowCut Hz
        18206.9f,  // highCut Hz
        false,
        0.4977f,  // roomSize
        0.6000f,  // earlyLateBal
        0.80f,  // highDecay
        0.54f,  // midDecay
        1000.5f,  // highFreq Hz
        0.0000f,  // erShape
        0.8146f,  // erSpread
        38.5f,  // erBassCut Hz
        0.94f,  // trebleRatio
        0.1486f,  // stereoCoupling
        880.0f,  // lowMidFreq Hz
        1.03f,  // lowMidDecay
        0,  // envMode (Off)
        500.3f,  // envHold ms
        500.3f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1330.5f,  // outEQ1Freq Hz
        4.94f,  // outEQ1Gain dB
        1.04f,  // outEQ1Q
        4012.9f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6003f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        1.0000f,  // dynAmount
        0.2501f  // dynSpeed
    });

    // Special drum effect, narrow to wide, slap happy (match: 79%)
    presets.push_back({
        "Wide Slap Drum",
        "Rooms",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.1129f,  // damping
        250.0f,  // predelay ms
        0.22f,  // mix
        1.37f,  // modRate Hz
        0.2651f,  // modDepth
        1.0000f,  // width
        0.8771f,  // earlyDiff
        0.6230f,  // lateDiff
        0.10f,  // bassMult
        774.7f,  // bassFreq Hz
        20.8f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.2430f,  // roomSize
        0.7000f,  // earlyLateBal
        1.96f,  // highDecay
        0.51f,  // midDecay
        1651.9f,  // highFreq Hz
        1.0000f,  // erShape
        0.0000f,  // erSpread
        500.0f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1504f,  // stereoCoupling
        2077.5f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        501.1f,  // envHold ms
        501.5f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        955.7f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        5036.4f,  // outEQ2Freq Hz
        2.02f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -0.0000f,  // dynAmount
        0.2502f  // dynSpeed
    });


    // ==================== PLATES (46) ====================

    // Really smooth plate with slow reverb build (match: 94%)
    presets.push_back({
        "Acoustic Gtr Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.2483f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.81f,  // modRate Hz
        0.2544f,  // modDepth
        1.0000f,  // width
        0.5205f,  // earlyDiff
        0.5496f,  // lateDiff
        1.09f,  // bassMult
        203.2f,  // bassFreq Hz
        20.5f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.4527f,  // roomSize
        0.6000f,  // earlyLateBal
        3.24f,  // highDecay
        0.68f,  // midDecay
        1473.8f,  // highFreq Hz
        0.0051f,  // erShape
        0.2524f,  // erSpread
        20.3f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1430f,  // stereoCoupling
        2059.5f,  // lowMidFreq Hz
        1.12f,  // lowMidDecay
        0,  // envMode (Off)
        500.9f,  // envHold ms
        500.9f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        917.5f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        0.42f,  // outEQ1Q
        3063.0f,  // outEQ2Freq Hz
        -7.05f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.5009f  // dynSpeed
    });

    // Medium size plate, high diffusion, moderate decay (match: 96%)
    presets.push_back({
        "Big Drums",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.2343f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.62f,  // modRate Hz
        0.2888f,  // modDepth
        1.0000f,  // width
        0.5546f,  // earlyDiff
        0.5582f,  // lateDiff
        1.43f,  // bassMult
        104.6f,  // bassFreq Hz
        32.4f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.4798f,  // roomSize
        0.7000f,  // earlyLateBal
        3.66f,  // highDecay
        0.90f,  // midDecay
        7882.1f,  // highFreq Hz
        0.2556f,  // erShape
        0.3321f,  // erSpread
        20.1f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1543f,  // stereoCoupling
        100.1f,  // lowMidFreq Hz
        0.99f,  // lowMidDecay
        0,  // envMode (Off)
        638.2f,  // envHold ms
        502.2f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        449.0f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        3025.4f,  // outEQ2Freq Hz
        -9.25f,  // outEQ2Gain dB
        0.99f,  // outEQ2Q
        0.6011f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2502f  // dynSpeed
    });

    // Gives bongos and native drums thickness (match: 94%)
    presets.push_back({
        "Bongo Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.0752f,  // size
        0.0000f,  // damping
        31.2f,  // predelay ms
        0.28f,  // mix
        0.79f,  // modRate Hz
        0.2524f,  // modDepth
        1.0000f,  // width
        0.6240f,  // earlyDiff
        0.4250f,  // lateDiff
        1.38f,  // bassMult
        224.5f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5245f,  // roomSize
        1.0000f,  // earlyLateBal
        2.45f,  // highDecay
        1.06f,  // midDecay
        4004.1f,  // highFreq Hz
        0.5015f,  // erShape
        0.5018f,  // erSpread
        20.1f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1495f,  // stereoCoupling
        3661.8f,  // lowMidFreq Hz
        1.03f,  // lowMidDecay
        0,  // envMode (Off)
        501.7f,  // envHold ms
        501.9f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        637.3f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        0.98f,  // outEQ1Q
        3986.1f,  // outEQ2Freq Hz
        -7.52f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        1.0000f,  // dynAmount
        0.2505f  // dynSpeed
    });

    // Small bright plate, short decay, enhancing (match: 96%)
    presets.push_back({
        "Bright Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.0000f,  // size
        0.0000f,  // damping
        20.7f,  // predelay ms
        0.28f,  // mix
        0.16f,  // modRate Hz
        0.2775f,  // modDepth
        1.0000f,  // width
        0.7714f,  // earlyDiff
        0.5651f,  // lateDiff
        1.91f,  // bassMult
        211.0f,  // bassFreq Hz
        20.3f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5912f,  // roomSize
        0.6000f,  // earlyLateBal
        2.29f,  // highDecay
        1.57f,  // midDecay
        2370.1f,  // highFreq Hz
        0.1193f,  // erShape
        0.3545f,  // erSpread
        20.2f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1889f,  // stereoCoupling
        3064.4f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        1,  // envMode (Gate)
        504.8f,  // envHold ms
        314.4f,  // envRelease ms
        78.7f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1094.2f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        0.99f,  // outEQ1Q
        5329.4f,  // outEQ2Freq Hz
        -12.00f,  // outEQ2Gain dB
        0.99f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.3001f,  // resonance
        0.0000f,  // echoPingPong
        0.7501f,  // dynAmount
        0.5001f  // dynSpeed
    });

    // Large bright plate, long decay for various vocals (match: 97%)
    presets.push_back({
        "Bright Vox Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.2483f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.77f,  // modRate Hz
        0.3205f,  // modDepth
        0.9881f,  // width
        0.4116f,  // earlyDiff
        0.4206f,  // lateDiff
        1.36f,  // bassMult
        661.9f,  // bassFreq Hz
        94.7f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5009f,  // roomSize
        0.5000f,  // earlyLateBal
        2.31f,  // highDecay
        1.02f,  // midDecay
        4191.8f,  // highFreq Hz
        0.2788f,  // erShape
        0.2584f,  // erSpread
        20.0f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1463f,  // stereoCoupling
        4058.5f,  // lowMidFreq Hz
        1.01f,  // lowMidDecay
        0,  // envMode (Off)
        315.3f,  // envHold ms
        500.8f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1006.9f,  // outEQ1Freq Hz
        -9.47f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4047.8f,  // outEQ2Freq Hz
        -3.00f,  // outEQ2Gain dB
        1.01f,  // outEQ2Q
        0.6016f,  // stereoInvert
        0.0001f,  // resonance
        0.0000f,  // echoPingPong
        0.7509f,  // dynAmount
        0.5000f  // dynSpeed
    });

    // Large silky plate, long decay for background (match: 92%)
    presets.push_back({
        "Choir Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.3831f,  // size
        0.0000f,  // damping
        51.4f,  // predelay ms
        0.28f,  // mix
        0.81f,  // modRate Hz
        0.2593f,  // modDepth
        0.9539f,  // width
        0.5744f,  // earlyDiff
        0.5305f,  // lateDiff
        1.15f,  // bassMult
        778.5f,  // bassFreq Hz
        20.3f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5429f,  // roomSize
        0.4000f,  // earlyLateBal
        4.00f,  // highDecay
        0.73f,  // midDecay
        2376.0f,  // highFreq Hz
        0.2055f,  // erShape
        0.2563f,  // erSpread
        20.3f,  // erBassCut Hz
        1.38f,  // trebleRatio
        0.1493f,  // stereoCoupling
        2371.1f,  // lowMidFreq Hz
        1.13f,  // lowMidDecay
        0,  // envMode (Off)
        500.2f,  // envHold ms
        500.2f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        451.9f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        5039.7f,  // outEQ2Freq Hz
        3.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.5002f  // dynSpeed
    });

    // Clean plate with diffusion control (match: 94%)
    presets.push_back({
        "Clean Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.3008f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.80f,  // modRate Hz
        0.3186f,  // modDepth
        1.0000f,  // width
        0.3199f,  // earlyDiff
        0.5405f,  // lateDiff
        1.54f,  // bassMult
        326.1f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.3736f,  // roomSize
        0.9000f,  // earlyLateBal
        4.00f,  // highDecay
        1.00f,  // midDecay
        2132.6f,  // highFreq Hz
        0.5280f,  // erShape
        0.5004f,  // erSpread
        20.1f,  // erBassCut Hz
        1.37f,  // trebleRatio
        0.1488f,  // stereoCoupling
        389.6f,  // lowMidFreq Hz
        0.73f,  // lowMidDecay
        1,  // envMode (Gate)
        1024.0f,  // envHold ms
        928.2f,  // envRelease ms
        71.3f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        525.2f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        2268.5f,  // outEQ2Freq Hz
        -12.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.5006f,  // dynAmount
        0.6249f  // dynSpeed
    });

    // Short dull plate for percussion (match: 91%)
    presets.push_back({
        "Cool Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.2199f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.81f,  // modRate Hz
        0.2487f,  // modDepth
        1.0000f,  // width
        0.1834f,  // earlyDiff
        0.3769f,  // lateDiff
        1.54f,  // bassMult
        616.2f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.1515f,  // roomSize
        0.9000f,  // earlyLateBal
        3.77f,  // highDecay
        1.83f,  // midDecay
        2320.3f,  // highFreq Hz
        0.4999f,  // erShape
        0.5052f,  // erSpread
        20.1f,  // erBassCut Hz
        1.46f,  // trebleRatio
        0.1516f,  // stereoCoupling
        2076.1f,  // lowMidFreq Hz
        0.72f,  // lowMidDecay
        1,  // envMode (Gate)
        583.4f,  // envHold ms
        542.9f,  // envRelease ms
        74.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1084.0f,  // outEQ1Freq Hz
        -11.18f,  // outEQ1Gain dB
        0.99f,  // outEQ1Q
        101.6f,  // outEQ2Freq Hz
        -11.12f,  // outEQ2Gain dB
        0.99f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0002f,  // echoPingPong
        0.5009f,  // dynAmount
        0.2496f  // dynSpeed
    });

    // Classic! Dark, smooth, long decay, fatten percussion (match: 96%)
    presets.push_back({
        "Dark Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.3132f,  // size
        0.2092f,  // damping
        26.5f,  // predelay ms
        0.28f,  // mix
        0.80f,  // modRate Hz
        0.2510f,  // modDepth
        0.9838f,  // width
        0.1064f,  // earlyDiff
        0.5493f,  // lateDiff
        0.82f,  // bassMult
        383.5f,  // bassFreq Hz
        20.7f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.0000f,  // roomSize
        0.5000f,  // earlyLateBal
        1.02f,  // highDecay
        1.00f,  // midDecay
        1001.5f,  // highFreq Hz
        0.6051f,  // erShape
        0.5021f,  // erSpread
        100.0f,  // erBassCut Hz
        0.81f,  // trebleRatio
        0.1494f,  // stereoCoupling
        701.0f,  // lowMidFreq Hz
        0.72f,  // lowMidDecay
        1,  // envMode (Gate)
        327.6f,  // envHold ms
        2440.1f,  // envRelease ms
        78.6f,  // envDepth %
        75.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1902.7f,  // outEQ1Freq Hz
        0.32f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        2815.8f,  // outEQ2Freq Hz
        -12.00f,  // outEQ2Gain dB
        1.61f,  // outEQ2Q
        0.6009f,  // stereoInvert
        0.0580f,  // resonance
        0.1303f,  // echoPingPong
        1.0000f,  // dynAmount
        0.2730f  // dynSpeed
    });

    // Large dark plate, high diffusion, long decay (match: 96%)
    presets.push_back({
        "Drum Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.2483f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.80f,  // modRate Hz
        0.2573f,  // modDepth
        1.0000f,  // width
        0.2766f,  // earlyDiff
        0.4997f,  // lateDiff
        0.91f,  // bassMult
        1000.0f,  // bassFreq Hz
        85.4f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.7357f,  // roomSize
        0.5000f,  // earlyLateBal
        4.00f,  // highDecay
        0.94f,  // midDecay
        3090.2f,  // highFreq Hz
        0.2023f,  // erShape
        0.1770f,  // erSpread
        96.7f,  // erBassCut Hz
        1.56f,  // trebleRatio
        0.1492f,  // stereoCoupling
        4597.4f,  // lowMidFreq Hz
        0.99f,  // lowMidDecay
        0,  // envMode (Off)
        500.3f,  // envHold ms
        500.2f,  // envRelease ms
        13.6f,  // envDepth %
        0.0f,  // echoDelay ms
        6.6f,  // echoFeedback %
        1359.6f,  // outEQ1Freq Hz
        -7.45f,  // outEQ1Gain dB
        0.99f,  // outEQ1Q
        4010.3f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6007f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.7505f,  // dynAmount
        0.2501f  // dynSpeed
    });

    // Sweet combination of recirculating pre-echoes (match: 86%)
    presets.push_back({
        "Eko Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.4774f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.54f,  // modRate Hz
        0.2566f,  // modDepth
        1.0000f,  // width
        0.5506f,  // earlyDiff
        0.5606f,  // lateDiff
        1.19f,  // bassMult
        505.5f,  // bassFreq Hz
        88.6f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6399f,  // roomSize
        0.2000f,  // earlyLateBal
        2.77f,  // highDecay
        0.82f,  // midDecay
        1012.7f,  // highFreq Hz
        0.2059f,  // erShape
        0.3213f,  // erSpread
        20.1f,  // erBassCut Hz
        1.96f,  // trebleRatio
        0.1475f,  // stereoCoupling
        692.8f,  // lowMidFreq Hz
        0.99f,  // lowMidDecay
        0,  // envMode (Off)
        502.3f,  // envHold ms
        503.2f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        911.5f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        0.99f,  // outEQ1Q
        3428.7f,  // outEQ2Freq Hz
        -3.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.4999f,  // dynAmount
        0.6253f  // dynSpeed
    });

    // Mono level patched to Attack and Spread (match: 85%)
    presets.push_back({
        "Ever Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.0752f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.80f,  // modRate Hz
        0.2577f,  // modDepth
        0.6073f,  // width
        0.1067f,  // earlyDiff
        0.5479f,  // lateDiff
        1.28f,  // bassMult
        285.7f,  // bassFreq Hz
        85.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.9263f,  // roomSize
        0.8000f,  // earlyLateBal
        2.43f,  // highDecay
        0.97f,  // midDecay
        4962.4f,  // highFreq Hz
        0.5027f,  // erShape
        0.5019f,  // erSpread
        20.1f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1125f,  // stereoCoupling
        2199.2f,  // lowMidFreq Hz
        1.02f,  // lowMidDecay
        1,  // envMode (Gate)
        1036.2f,  // envHold ms
        3000.0f,  // envRelease ms
        20.6f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        902.6f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        0.99f,  // outEQ1Q
        4570.8f,  // outEQ2Freq Hz
        -12.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6020f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.0000f,  // dynAmount
        0.2501f  // dynSpeed
    });

    // Moderate sized, deep sounding plate, high attack (match: 92%)
    presets.push_back({
        "Fat Drums",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.1733f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.79f,  // modRate Hz
        0.2537f,  // modDepth
        0.9811f,  // width
        0.6625f,  // earlyDiff
        0.5483f,  // lateDiff
        1.55f,  // bassMult
        265.9f,  // bassFreq Hz
        89.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.3507f,  // roomSize
        0.3000f,  // earlyLateBal
        0.67f,  // highDecay
        1.02f,  // midDecay
        2637.0f,  // highFreq Hz
        0.5053f,  // erShape
        0.5052f,  // erSpread
        20.1f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1476f,  // stereoCoupling
        3130.2f,  // lowMidFreq Hz
        1.01f,  // lowMidDecay
        0,  // envMode (Off)
        503.1f,  // envHold ms
        504.8f,  // envRelease ms
        4.3f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        981.4f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        0.95f,  // outEQ1Q
        4029.0f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.01f,  // outEQ2Q
        0.6026f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.0000f,  // dynAmount
        0.5010f  // dynSpeed
    });

    // Big plate with long predelay and repeating echo (match: 88%)
    presets.push_back({
        "Floyd Wash",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.4774f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        1.41f,  // modRate Hz
        0.4266f,  // modDepth
        1.0000f,  // width
        0.6253f,  // earlyDiff
        0.5446f,  // lateDiff
        2.40f,  // bassMult
        437.9f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.0000f,  // roomSize
        0.5000f,  // earlyLateBal
        3.16f,  // highDecay
        0.93f,  // midDecay
        1000.7f,  // highFreq Hz
        0.5738f,  // erShape
        0.4701f,  // erSpread
        20.0f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1487f,  // stereoCoupling
        840.8f,  // lowMidFreq Hz
        0.53f,  // lowMidDecay
        0,  // envMode (Off)
        393.0f,  // envHold ms
        500.4f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        8000.0f,  // outEQ1Freq Hz
        6.06f,  // outEQ1Gain dB
        0.41f,  // outEQ1Q
        3064.6f,  // outEQ2Freq Hz
        -8.18f,  // outEQ2Gain dB
        0.40f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.6005f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.5602f  // dynSpeed
    });

    // Generic plate preset, starting place (match: 93%)
    presets.push_back({
        "Gen. Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.2049f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.80f,  // modRate Hz
        0.2188f,  // modDepth
        0.9865f,  // width
        0.5241f,  // earlyDiff
        0.5453f,  // lateDiff
        0.49f,  // bassMult
        373.3f,  // bassFreq Hz
        92.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.0000f,  // roomSize
        0.1000f,  // earlyLateBal
        4.00f,  // highDecay
        0.93f,  // midDecay
        6563.1f,  // highFreq Hz
        0.4568f,  // erShape
        0.6132f,  // erSpread
        20.2f,  // erBassCut Hz
        1.96f,  // trebleRatio
        0.1534f,  // stereoCoupling
        3078.2f,  // lowMidFreq Hz
        0.59f,  // lowMidDecay
        1,  // envMode (Gate)
        638.2f,  // envHold ms
        538.0f,  // envRelease ms
        61.1f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        934.1f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        0.30f,  // outEQ1Q
        3047.1f,  // outEQ2Freq Hz
        -8.95f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.6032f,  // dynAmount
        0.5003f  // dynSpeed
    });

    // Classic plate with long decay, medium high end (match: 95%)
    presets.push_back({
        "Gold Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.1733f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.34f,  // modRate Hz
        0.2474f,  // modDepth
        0.9697f,  // width
        0.7512f,  // earlyDiff
        0.5620f,  // lateDiff
        0.69f,  // bassMult
        1000.0f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.1811f,  // roomSize
        0.6000f,  // earlyLateBal
        2.95f,  // highDecay
        0.72f,  // midDecay
        1517.6f,  // highFreq Hz
        0.2180f,  // erShape
        0.3931f,  // erSpread
        20.4f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1461f,  // stereoCoupling
        2076.2f,  // lowMidFreq Hz
        1.31f,  // lowMidDecay
        0,  // envMode (Off)
        500.8f,  // envHold ms
        500.8f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1523.4f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        3127.4f,  // outEQ2Freq Hz
        -7.06f,  // outEQ2Gain dB
        1.03f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.0000f,  // dynAmount
        0.2504f  // dynSpeed
    });

    // Basic plate, not too dark and not too bright (match: 95%)
    presets.push_back({
        "Great Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.2483f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.81f,  // modRate Hz
        0.2655f,  // modDepth
        1.0000f,  // width
        0.7110f,  // earlyDiff
        0.5065f,  // lateDiff
        0.97f,  // bassMult
        101.9f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        19983.0f,  // highCut Hz
        false,
        0.9537f,  // roomSize
        0.9000f,  // earlyLateBal
        4.00f,  // highDecay
        0.96f,  // midDecay
        6286.5f,  // highFreq Hz
        0.2011f,  // erShape
        0.1246f,  // erSpread
        20.2f,  // erBassCut Hz
        1.37f,  // trebleRatio
        0.1496f,  // stereoCoupling
        1913.7f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        503.0f,  // envHold ms
        503.0f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1002.7f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        3012.6f,  // outEQ2Freq Hz
        -12.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2503f  // dynSpeed
    });

    // Basic guitar delay with plate reverb mixed in (match: 79%)
    presets.push_back({
        "Guitar Dly Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.3831f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.92f,  // modRate Hz
        0.2591f,  // modDepth
        0.4955f,  // width
        0.5400f,  // earlyDiff
        0.5204f,  // lateDiff
        0.10f,  // bassMult
        178.2f,  // bassFreq Hz
        45.3f,  // lowCut Hz
        19862.6f,  // highCut Hz
        false,
        0.9623f,  // roomSize
        0.5000f,  // earlyLateBal
        4.00f,  // highDecay
        0.63f,  // midDecay
        2376.1f,  // highFreq Hz
        0.2370f,  // erShape
        0.2509f,  // erSpread
        20.3f,  // erBassCut Hz
        1.37f,  // trebleRatio
        0.1456f,  // stereoCoupling
        1088.2f,  // lowMidFreq Hz
        1.28f,  // lowMidDecay
        3,  // envMode (Swell)
        1024.6f,  // envHold ms
        1001.5f,  // envRelease ms
        63.5f,  // envDepth %
        94.1f,  // echoDelay ms
        6.0f,  // echoFeedback %
        939.0f,  // outEQ1Freq Hz
        -9.31f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        3064.4f,  // outEQ2Freq Hz
        -3.01f,  // outEQ2Gain dB
        0.69f,  // outEQ2Q
        0.5066f,  // stereoInvert
        0.0000f,  // resonance
        0.6254f,  // echoPingPong
        0.7505f,  // dynAmount
        0.2502f  // dynSpeed
    });

    // Moderate size, dark plate reverb for guitar (match: 90%)
    presets.push_back({
        "Guitar Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.3831f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.80f,  // modRate Hz
        0.2513f,  // modDepth
        0.8775f,  // width
        0.7271f,  // earlyDiff
        0.5499f,  // lateDiff
        0.82f,  // bassMult
        505.9f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6222f,  // roomSize
        0.9000f,  // earlyLateBal
        4.00f,  // highDecay
        1.00f,  // midDecay
        3046.6f,  // highFreq Hz
        0.5043f,  // erShape
        0.5030f,  // erSpread
        20.2f,  // erBassCut Hz
        1.37f,  // trebleRatio
        0.1490f,  // stereoCoupling
        2278.5f,  // lowMidFreq Hz
        0.89f,  // lowMidDecay
        3,  // envMode (Swell)
        924.3f,  // envHold ms
        503.2f,  // envRelease ms
        64.3f,  // envDepth %
        75.2f,  // echoDelay ms
        0.0f,  // echoFeedback %
        925.0f,  // outEQ1Freq Hz
        -9.40f,  // outEQ1Gain dB
        1.02f,  // outEQ1Q
        6070.0f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.01f,  // outEQ2Q
        0.3265f,  // stereoInvert
        0.6037f,  // resonance
        0.0000f,  // echoPingPong
        0.7520f,  // dynAmount
        0.2507f  // dynSpeed
    });

    // Medium sizzling plate, optimized for live mixing (match: 93%)
    presets.push_back({
        "Hot Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.2483f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.81f,  // modRate Hz
        0.2589f,  // modDepth
        1.0000f,  // width
        0.7248f,  // earlyDiff
        0.4998f,  // lateDiff
        0.40f,  // bassMult
        191.7f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.2753f,  // roomSize
        0.4000f,  // earlyLateBal
        4.00f,  // highDecay
        0.99f,  // midDecay
        3745.0f,  // highFreq Hz
        0.5029f,  // erShape
        0.4941f,  // erSpread
        20.1f,  // erBassCut Hz
        1.87f,  // trebleRatio
        0.1486f,  // stereoCoupling
        1853.1f,  // lowMidFreq Hz
        0.72f,  // lowMidDecay
        1,  // envMode (Gate)
        1152.6f,  // envHold ms
        909.9f,  // envRelease ms
        41.2f,  // envDepth %
        0.0f,  // echoDelay ms
        8.5f,  // echoFeedback %
        1317.5f,  // outEQ1Freq Hz
        -11.85f,  // outEQ1Gain dB
        0.99f,  // outEQ1Q
        3067.4f,  // outEQ2Freq Hz
        -7.06f,  // outEQ2Gain dB
        1.01f,  // outEQ2Q
        0.6009f,  // stereoInvert
        0.0985f,  // resonance
        0.0000f,  // echoPingPong
        1.0000f,  // dynAmount
        0.2501f  // dynSpeed
    });

    // Basic plate for any kind of sound source (match: 89%)
    presets.push_back({
        "Just Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.3719f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.80f,  // modRate Hz
        0.2502f,  // modDepth
        1.0000f,  // width
        0.5748f,  // earlyDiff
        0.4972f,  // lateDiff
        1.00f,  // bassMult
        762.3f,  // bassFreq Hz
        33.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.9627f,  // roomSize
        0.6000f,  // earlyLateBal
        4.00f,  // highDecay
        1.01f,  // midDecay
        4152.4f,  // highFreq Hz
        0.5004f,  // erShape
        0.5004f,  // erSpread
        20.0f,  // erBassCut Hz
        1.58f,  // trebleRatio
        0.1499f,  // stereoCoupling
        902.9f,  // lowMidFreq Hz
        0.94f,  // lowMidDecay
        0,  // envMode (Off)
        500.4f,  // envHold ms
        500.4f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1000.6f,  // outEQ1Freq Hz
        -7.45f,  // outEQ1Gain dB
        0.83f,  // outEQ1Q
        4118.6f,  // outEQ2Freq Hz
        -1.80f,  // outEQ2Gain dB
        0.58f,  // outEQ2Q
        0.6004f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2502f  // dynSpeed
    });

    // Medium plate, short reverb time for full kit (match: 92%)
    presets.push_back({
        "Live Drums Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.1733f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.79f,  // modRate Hz
        0.2434f,  // modDepth
        1.0000f,  // width
        0.6723f,  // earlyDiff
        0.6223f,  // lateDiff
        0.73f,  // bassMult
        789.1f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.9291f,  // roomSize
        1.0000f,  // earlyLateBal
        2.61f,  // highDecay
        0.52f,  // midDecay
        5227.2f,  // highFreq Hz
        0.2026f,  // erShape
        0.2542f,  // erSpread
        20.4f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1472f,  // stereoCoupling
        4407.3f,  // lowMidFreq Hz
        1.08f,  // lowMidDecay
        0,  // envMode (Off)
        505.1f,  // envHold ms
        505.4f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        437.8f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        0.99f,  // outEQ1Q
        5143.2f,  // outEQ2Freq Hz
        -9.00f,  // outEQ2Gain dB
        1.02f,  // outEQ2Q
        0.6275f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.5005f  // dynSpeed
    });

    // Tight gate or crisp inverse sounds on the fly (match: 89%)
    presets.push_back({
        "Live Gate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.0000f,  // size
        0.6251f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.85f,  // modRate Hz
        0.2504f,  // modDepth
        1.0000f,  // width
        0.6922f,  // earlyDiff
        0.4585f,  // lateDiff
        1.24f,  // bassMult
        1000.0f,  // bassFreq Hz
        78.5f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.2581f,  // roomSize
        0.8000f,  // earlyLateBal
        2.08f,  // highDecay
        0.72f,  // midDecay
        12000.0f,  // highFreq Hz
        0.3125f,  // erShape
        0.6451f,  // erSpread
        20.0f,  // erBassCut Hz
        1.87f,  // trebleRatio
        0.2623f,  // stereoCoupling
        5468.3f,  // lowMidFreq Hz
        0.72f,  // lowMidDecay
        3,  // envMode (Swell)
        1005.8f,  // envHold ms
        917.7f,  // envRelease ms
        79.8f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        918.4f,  // outEQ1Freq Hz
        6.01f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        3406.9f,  // outEQ2Freq Hz
        -9.01f,  // outEQ2Gain dB
        1.25f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0400f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2502f  // dynSpeed
    });

    // Crisp clean basic plate, medium decay (match: 94%)
    presets.push_back({
        "Live Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.2483f,  // size
        0.1064f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.81f,  // modRate Hz
        0.2550f,  // modDepth
        1.0000f,  // width
        0.1741f,  // earlyDiff
        0.6289f,  // lateDiff
        0.39f,  // bassMult
        142.5f,  // bassFreq Hz
        20.3f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.7491f,  // roomSize
        0.7000f,  // earlyLateBal
        3.28f,  // highDecay
        0.51f,  // midDecay
        1246.7f,  // highFreq Hz
        0.2023f,  // erShape
        0.2519f,  // erSpread
        30.4f,  // erBassCut Hz
        1.80f,  // trebleRatio
        0.1484f,  // stereoCoupling
        2089.9f,  // lowMidFreq Hz
        1.06f,  // lowMidDecay
        0,  // envMode (Off)
        502.6f,  // envHold ms
        504.1f,  // envRelease ms
        0.0f,  // envDepth %
        55.3f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1518.3f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        0.97f,  // outEQ1Q
        4032.0f,  // outEQ2Freq Hz
        -3.02f,  // outEQ2Gain dB
        1.01f,  // outEQ2Q
        0.6001f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2501f  // dynSpeed
    });

    // General plate, run in mono, stereo, or 3 choices (match: 91%)
    presets.push_back({
        "Mono Or Stereo",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.2483f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.80f,  // modRate Hz
        0.2567f,  // modDepth
        1.0000f,  // width
        0.3737f,  // earlyDiff
        0.5508f,  // lateDiff
        0.70f,  // bassMult
        786.9f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.3729f,  // roomSize
        0.1000f,  // earlyLateBal
        4.00f,  // highDecay
        0.87f,  // midDecay
        4789.5f,  // highFreq Hz
        0.3552f,  // erShape
        0.3120f,  // erSpread
        90.7f,  // erBassCut Hz
        1.56f,  // trebleRatio
        0.1476f,  // stereoCoupling
        3074.2f,  // lowMidFreq Hz
        0.83f,  // lowMidDecay
        0,  // envMode (Off)
        501.1f,  // envHold ms
        503.1f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        918.4f,  // outEQ1Freq Hz
        -11.99f,  // outEQ1Gain dB
        0.99f,  // outEQ1Q
        4005.9f,  // outEQ2Freq Hz
        -1.80f,  // outEQ2Gain dB
        0.34f,  // outEQ2Q
        0.6000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2512f  // dynSpeed
    });

    // Multi-purpose plate delay with custom controls (match: 81%)
    presets.push_back({
        "Multi Plate Dly",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.2483f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.68f,  // modRate Hz
        0.4033f,  // modDepth
        0.9651f,  // width
        0.7002f,  // earlyDiff
        0.5594f,  // lateDiff
        1.01f,  // bassMult
        1000.0f,  // bassFreq Hz
        90.0f,  // lowCut Hz
        19487.6f,  // highCut Hz
        false,
        0.1251f,  // roomSize
        1.0000f,  // earlyLateBal
        3.44f,  // highDecay
        0.88f,  // midDecay
        8803.5f,  // highFreq Hz
        0.5045f,  // erShape
        0.5045f,  // erSpread
        20.0f,  // erBassCut Hz
        1.86f,  // trebleRatio
        0.1496f,  // stereoCoupling
        6871.8f,  // lowMidFreq Hz
        0.98f,  // lowMidDecay
        1,  // envMode (Gate)
        10.0f,  // envHold ms
        1396.7f,  // envRelease ms
        55.7f,  // envDepth %
        500.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        364.4f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        0.99f,  // outEQ1Q
        2228.9f,  // outEQ2Freq Hz
        -6.61f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.3003f,  // resonance
        0.0681f,  // echoPingPong
        -0.1001f,  // dynAmount
        0.2003f  // dynSpeed
    });

    // Small short plate for gang vocals (match: 92%)
    presets.push_back({
        "Multi Vox",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.0752f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.79f,  // modRate Hz
        0.2501f,  // modDepth
        1.0000f,  // width
        0.5006f,  // earlyDiff
        0.5502f,  // lateDiff
        1.54f,  // bassMult
        131.1f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.3748f,  // roomSize
        0.4000f,  // earlyLateBal
        3.78f,  // highDecay
        1.01f,  // midDecay
        4004.5f,  // highFreq Hz
        0.5007f,  // erShape
        0.5006f,  // erSpread
        20.0f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1497f,  // stereoCoupling
        3641.0f,  // lowMidFreq Hz
        1.07f,  // lowMidDecay
        0,  // envMode (Off)
        501.0f,  // envHold ms
        500.7f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        891.9f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4049.6f,  // outEQ2Freq Hz
        -7.06f,  // outEQ2Gain dB
        1.04f,  // outEQ2Q
        0.6008f,  // stereoInvert
        0.0000f,  // resonance
        0.0414f,  // echoPingPong
        0.0000f,  // dynAmount
        0.2502f  // dynSpeed
    });

    // Tempo-driven spatial effect for dramatic spatial effects (match: 60%)
    presets.push_back({
        "Patterns",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.5632f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.37f,  // modRate Hz
        0.2603f,  // modDepth
        0.5381f,  // width
        0.7018f,  // earlyDiff
        0.5496f,  // lateDiff
        1.53f,  // bassMult
        385.8f,  // bassFreq Hz
        22.7f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.0567f,  // roomSize
        1.0000f,  // earlyLateBal
        1.96f,  // highDecay
        0.52f,  // midDecay
        1001.5f,  // highFreq Hz
        0.0000f,  // erShape
        0.4164f,  // erSpread
        78.4f,  // erBassCut Hz
        0.30f,  // trebleRatio
        0.1488f,  // stereoCoupling
        397.6f,  // lowMidFreq Hz
        0.94f,  // lowMidDecay
        0,  // envMode (Off)
        500.9f,  // envHold ms
        501.0f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        2476.3f,  // outEQ1Freq Hz
        -6.01f,  // outEQ1Gain dB
        0.85f,  // outEQ1Q
        5048.8f,  // outEQ2Freq Hz
        -6.60f,  // outEQ2Gain dB
        0.99f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.0000f,  // dynAmount
        0.2505f  // dynSpeed
    });

    // General purpose, dark plate (match: 94%)
    presets.push_back({
        "Plate 90",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.1733f,  // size
        0.1306f,  // damping
        32.2f,  // predelay ms
        0.28f,  // mix
        0.80f,  // modRate Hz
        0.2533f,  // modDepth
        0.9894f,  // width
        0.4218f,  // earlyDiff
        0.4980f,  // lateDiff
        0.70f,  // bassMult
        1000.0f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        16218.2f,  // highCut Hz
        false,
        0.7695f,  // roomSize
        0.3000f,  // earlyLateBal
        4.00f,  // highDecay
        0.93f,  // midDecay
        7125.9f,  // highFreq Hz
        0.2916f,  // erShape
        0.2514f,  // erSpread
        66.4f,  // erBassCut Hz
        1.58f,  // trebleRatio
        0.1489f,  // stereoCoupling
        1088.1f,  // lowMidFreq Hz
        0.99f,  // lowMidDecay
        0,  // envMode (Off)
        503.2f,  // envHold ms
        503.1f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.9f,  // echoFeedback %
        332.7f,  // outEQ1Freq Hz
        9.03f,  // outEQ1Gain dB
        0.46f,  // outEQ1Q
        8000.0f,  // outEQ2Freq Hz
        -2.11f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2501f  // dynSpeed
    });

    // A good plate for brass sounds (match: 93%)
    presets.push_back({
        "Plate For Brass",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.2483f,  // size
        0.0516f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.81f,  // modRate Hz
        0.2491f,  // modDepth
        0.9566f,  // width
        0.5912f,  // earlyDiff
        0.5600f,  // lateDiff
        1.19f,  // bassMult
        100.0f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.2071f,  // roomSize
        0.7000f,  // earlyLateBal
        2.57f,  // highDecay
        1.00f,  // midDecay
        2447.0f,  // highFreq Hz
        0.5047f,  // erShape
        0.5685f,  // erSpread
        45.8f,  // erBassCut Hz
        1.89f,  // trebleRatio
        0.1533f,  // stereoCoupling
        100.9f,  // lowMidFreq Hz
        0.85f,  // lowMidDecay
        1,  // envMode (Gate)
        1258.0f,  // envHold ms
        2395.2f,  // envRelease ms
        37.6f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1857.4f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        0.99f,  // outEQ1Q
        8000.0f,  // outEQ2Freq Hz
        4.19f,  // outEQ2Gain dB
        0.99f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.6006f,  // dynAmount
        0.2503f  // dynSpeed
    });

    // Gate with tonal qualities of a plate (match: 94%)
    presets.push_back({
        "Plate Gate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.0000f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.57f,  // modRate Hz
        0.2421f,  // modDepth
        1.0000f,  // width
        0.7360f,  // earlyDiff
        0.5476f,  // lateDiff
        2.40f,  // bassMult
        550.6f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.2771f,  // roomSize
        1.0000f,  // earlyLateBal
        2.15f,  // highDecay
        1.01f,  // midDecay
        3759.8f,  // highFreq Hz
        0.5004f,  // erShape
        0.5029f,  // erSpread
        20.1f,  // erBassCut Hz
        1.38f,  // trebleRatio
        0.1561f,  // stereoCoupling
        3065.6f,  // lowMidFreq Hz
        1.19f,  // lowMidDecay
        2,  // envMode (Reverse)
        1006.0f,  // envHold ms
        1022.0f,  // envRelease ms
        80.2f,  // envDepth %
        62.3f,  // echoDelay ms
        0.0f,  // echoFeedback %
        440.4f,  // outEQ1Freq Hz
        -8.18f,  // outEQ1Gain dB
        1.03f,  // outEQ1Q
        4642.1f,  // outEQ2Freq Hz
        -11.11f,  // outEQ2Gain dB
        0.99f,  // outEQ2Q
        0.7016f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        1.0000f,  // dynAmount
        0.2503f  // dynSpeed
    });

    // Heavy, dense, short, nonlinear reverb (match: 87%)
    presets.push_back({
        "Plate Gate 2",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.0000f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.80f,  // modRate Hz
        0.2568f,  // modDepth
        1.0000f,  // width
        0.4155f,  // earlyDiff
        0.6361f,  // lateDiff
        1.84f,  // bassMult
        234.8f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.3220f,  // roomSize
        0.6000f,  // earlyLateBal
        3.53f,  // highDecay
        0.72f,  // midDecay
        4029.7f,  // highFreq Hz
        0.5012f,  // erShape
        0.5014f,  // erSpread
        20.1f,  // erBassCut Hz
        0.36f,  // trebleRatio
        0.1080f,  // stereoCoupling
        3065.8f,  // lowMidFreq Hz
        1.38f,  // lowMidDecay
        2,  // envMode (Reverse)
        506.4f,  // envHold ms
        312.3f,  // envRelease ms
        87.6f,  // envDepth %
        186.8f,  // echoDelay ms
        0.0f,  // echoFeedback %
        446.9f,  // outEQ1Freq Hz
        3.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4054.6f,  // outEQ2Freq Hz
        3.01f,  // outEQ2Gain dB
        1.01f,  // outEQ2Q
        0.3253f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.7498f,  // dynAmount
        0.2502f  // dynSpeed
    });

    // An old standard, bright and diffuse (match: 91%)
    presets.push_back({
        "Rich Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.1733f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.76f,  // modRate Hz
        0.2500f,  // modDepth
        1.0000f,  // width
        0.4789f,  // earlyDiff
        0.3787f,  // lateDiff
        0.97f,  // bassMult
        752.5f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6287f,  // roomSize
        0.5000f,  // earlyLateBal
        2.23f,  // highDecay
        1.01f,  // midDecay
        5067.2f,  // highFreq Hz
        0.5882f,  // erShape
        0.5020f,  // erSpread
        28.6f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1485f,  // stereoCoupling
        2272.5f,  // lowMidFreq Hz
        0.94f,  // lowMidDecay
        0,  // envMode (Off)
        1159.5f,  // envHold ms
        807.1f,  // envRelease ms
        80.3f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1105.2f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        0.99f,  // outEQ1Q
        102.3f,  // outEQ2Freq Hz
        -12.00f,  // outEQ2Gain dB
        0.66f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        1.0000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Big boomy dark plate, moderate reverb tail (match: 94%)
    presets.push_back({
        "Rock Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.2483f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.80f,  // modRate Hz
        0.2613f,  // modDepth
        0.9672f,  // width
        0.3515f,  // earlyDiff
        0.6537f,  // lateDiff
        1.04f,  // bassMult
        1000.0f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.7027f,  // roomSize
        0.7000f,  // earlyLateBal
        4.00f,  // highDecay
        0.93f,  // midDecay
        5722.1f,  // highFreq Hz
        0.2037f,  // erShape
        0.2533f,  // erSpread
        20.2f,  // erBassCut Hz
        1.52f,  // trebleRatio
        0.2086f,  // stereoCoupling
        3453.6f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        500.6f,  // envHold ms
        500.2f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        506.8f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        2259.9f,  // outEQ2Freq Hz
        -12.00f,  // outEQ2Gain dB
        1.02f,  // outEQ2Q
        0.6014f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.6016f,  // dynAmount
        0.5012f  // dynSpeed
    });

    // Short plate reverb, fairly short decay, good high end (match: 93%)
    presets.push_back({
        "Short Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.0752f,  // size
        0.1593f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.40f,  // modRate Hz
        0.2598f,  // modDepth
        1.0000f,  // width
        0.6628f,  // earlyDiff
        0.6222f,  // lateDiff
        0.52f,  // bassMult
        1000.0f,  // bassFreq Hz
        61.8f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.1911f,  // roomSize
        0.3000f,  // earlyLateBal
        0.86f,  // highDecay
        0.50f,  // midDecay
        4521.2f,  // highFreq Hz
        0.1980f,  // erShape
        0.2564f,  // erSpread
        20.1f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1268f,  // stereoCoupling
        4054.3f,  // lowMidFreq Hz
        0.99f,  // lowMidDecay
        0,  // envMode (Off)
        503.3f,  // envHold ms
        503.2f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        880.2f,  // outEQ1Freq Hz
        -8.69f,  // outEQ1Gain dB
        1.01f,  // outEQ1Q
        3058.2f,  // outEQ2Freq Hz
        3.01f,  // outEQ2Gain dB
        1.01f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        1.0000f,  // dynAmount
        0.2503f  // dynSpeed
    });

    // Small bright plate for vocals (match: 95%)
    presets.push_back({
        "Small Vox Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.0752f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        1.53f,  // modRate Hz
        0.2568f,  // modDepth
        0.9877f,  // width
        0.5705f,  // earlyDiff
        0.4783f,  // lateDiff
        0.46f,  // bassMult
        660.6f,  // bassFreq Hz
        81.5f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.1630f,  // roomSize
        0.1000f,  // earlyLateBal
        1.23f,  // highDecay
        0.50f,  // midDecay
        4220.0f,  // highFreq Hz
        0.1615f,  // erShape
        0.1903f,  // erSpread
        64.0f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1035f,  // stereoCoupling
        100.0f,  // lowMidFreq Hz
        1.22f,  // lowMidDecay
        1,  // envMode (Gate)
        995.1f,  // envHold ms
        10.0f,  // envRelease ms
        100.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        922.1f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        0.43f,  // outEQ1Q
        5057.7f,  // outEQ2Freq Hz
        -9.75f,  // outEQ2Gain dB
        0.90f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.6015f,  // resonance
        0.0000f,  // echoPingPong
        0.7500f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Plate reverb with two LFOs controlling InWidth/OutWidth (match: 91%)
    presets.push_back({
        "Spatial Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.0752f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.85f,  // modRate Hz
        0.2500f,  // modDepth
        0.1062f,  // width
        0.5750f,  // earlyDiff
        0.6250f,  // lateDiff
        1.38f,  // bassMult
        550.0f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6000f,  // roomSize
        0.7000f,  // earlyLateBal
        3.39f,  // highDecay
        1.00f,  // midDecay
        1000.0f,  // highFreq Hz
        0.5000f,  // erShape
        0.5000f,  // erSpread
        20.0f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1500f,  // stereoCoupling
        2703.8f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        500.0f,  // envHold ms
        666.2f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        891.7f,  // outEQ1Freq Hz
        9.88f,  // outEQ1Gain dB
        1.02f,  // outEQ1Q
        4000.0f,  // outEQ2Freq Hz
        -8.70f,  // outEQ2Gain dB
        1.01f,  // outEQ2Q
        0.6000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Medium bright plate with tempo delays for synth (match: 92%)
    presets.push_back({
        "Synth Lead",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.3132f,  // size
        0.0000f,  // damping
        18.8f,  // predelay ms
        0.28f,  // mix
        0.80f,  // modRate Hz
        0.2507f,  // modDepth
        0.8416f,  // width
        0.8649f,  // earlyDiff
        0.5009f,  // lateDiff
        0.83f,  // bassMult
        216.6f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        1.0000f,  // roomSize
        0.8000f,  // earlyLateBal
        2.13f,  // highDecay
        1.00f,  // midDecay
        5049.9f,  // highFreq Hz
        0.5014f,  // erShape
        0.5014f,  // erSpread
        20.1f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1496f,  // stereoCoupling
        2718.6f,  // lowMidFreq Hz
        0.99f,  // lowMidDecay
        3,  // envMode (Swell)
        501.4f,  // envHold ms
        501.4f,  // envRelease ms
        81.5f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        100.3f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        6367.3f,  // outEQ2Freq Hz
        11.00f,  // outEQ2Gain dB
        0.99f,  // outEQ2Q
        0.6017f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.5004f  // dynSpeed
    });

    // Small and tight, moderate diffusion, for percussion (match: 95%)
    presets.push_back({
        "Tight Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.0000f,  // size
        0.5043f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        1.48f,  // modRate Hz
        0.2550f,  // modDepth
        1.0000f,  // width
        0.5514f,  // earlyDiff
        0.6268f,  // lateDiff
        1.82f,  // bassMult
        926.5f,  // bassFreq Hz
        54.7f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.1243f,  // roomSize
        0.7000f,  // earlyLateBal
        2.72f,  // highDecay
        1.12f,  // midDecay
        12000.0f,  // highFreq Hz
        0.5055f,  // erShape
        0.4131f,  // erSpread
        20.1f,  // erBassCut Hz
        1.99f,  // trebleRatio
        0.1783f,  // stereoCoupling
        2678.2f,  // lowMidFreq Hz
        0.99f,  // lowMidDecay
        3,  // envMode (Swell)
        1006.0f,  // envHold ms
        1004.2f,  // envRelease ms
        81.1f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1083.8f,  // outEQ1Freq Hz
        -7.12f,  // outEQ1Gain dB
        1.45f,  // outEQ1Q
        6703.1f,  // outEQ2Freq Hz
        -7.05f,  // outEQ2Gain dB
        0.30f,  // outEQ2Q
        0.5872f,  // stereoInvert
        0.5152f,  // resonance
        0.0000f,  // echoPingPong
        -0.9992f,  // dynAmount
        0.2502f  // dynSpeed
    });

    // Silky smooth plate, moderate decay, recirculating (match: 93%)
    presets.push_back({
        "Vocal Echo",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.3008f,  // size
        0.0000f,  // damping
        26.5f,  // predelay ms
        0.28f,  // mix
        0.80f,  // modRate Hz
        0.2526f,  // modDepth
        1.0000f,  // width
        0.1250f,  // earlyDiff
        0.4993f,  // lateDiff
        0.96f,  // bassMult
        100.0f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.9258f,  // roomSize
        0.5000f,  // earlyLateBal
        4.00f,  // highDecay
        1.00f,  // midDecay
        3913.0f,  // highFreq Hz
        0.5023f,  // erShape
        0.5023f,  // erSpread
        20.1f,  // erBassCut Hz
        1.80f,  // trebleRatio
        0.1494f,  // stereoCoupling
        1375.0f,  // lowMidFreq Hz
        0.95f,  // lowMidDecay
        0,  // envMode (Off)
        586.6f,  // envHold ms
        502.3f,  // envRelease ms
        5.1f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        105.2f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        0.65f,  // outEQ1Q
        4018.1f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        0.42f,  // outEQ2Q
        0.6009f,  // stereoInvert
        0.0000f,  // resonance
        0.0038f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2501f  // dynSpeed
    });

    // Large dark plate, just the right amount of delay (match: 91%)
    presets.push_back({
        "Vocal Echo Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.3719f,  // size
        0.0000f,  // damping
        62.5f,  // predelay ms
        0.28f,  // mix
        0.80f,  // modRate Hz
        0.2500f,  // modDepth
        1.0000f,  // width
        0.0850f,  // earlyDiff
        0.5500f,  // lateDiff
        0.82f,  // bassMult
        217.6f,  // bassFreq Hz
        21.5f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.7875f,  // roomSize
        0.7000f,  // earlyLateBal
        3.72f,  // highDecay
        1.00f,  // midDecay
        5125.0f,  // highFreq Hz
        0.5000f,  // erShape
        0.5000f,  // erSpread
        20.0f,  // erBassCut Hz
        0.84f,  // trebleRatio
        0.1500f,  // stereoCoupling
        4050.0f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        500.0f,  // envHold ms
        500.0f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1000.0f,  // outEQ1Freq Hz
        -8.24f,  // outEQ1Gain dB
        1.02f,  // outEQ1Q
        4000.0f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.0000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Short plate, low diffusion, solo vocal track (match: 93%)
    presets.push_back({
        "Vocal Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.0752f,  // size
        0.0938f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.10f,  // modRate Hz
        0.2770f,  // modDepth
        1.0000f,  // width
        0.4196f,  // earlyDiff
        0.5066f,  // lateDiff
        0.89f,  // bassMult
        274.3f,  // bassFreq Hz
        120.5f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.3178f,  // roomSize
        0.5000f,  // earlyLateBal
        2.72f,  // highDecay
        0.92f,  // midDecay
        12000.0f,  // highFreq Hz
        0.2016f,  // erShape
        0.2492f,  // erSpread
        20.1f,  // erBassCut Hz
        1.99f,  // trebleRatio
        0.1464f,  // stereoCoupling
        3686.6f,  // lowMidFreq Hz
        1.01f,  // lowMidDecay
        0,  // envMode (Off)
        501.3f,  // envHold ms
        501.3f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        874.4f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        0.96f,  // outEQ1Q
        3067.2f,  // outEQ2Freq Hz
        -11.34f,  // outEQ2Gain dB
        0.99f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.0000f,  // dynAmount
        0.5013f  // dynSpeed
    });

    // Large plate, moderate decay for backing vocals (match: 93%)
    presets.push_back({
        "Vocal Plate 2",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.1733f,  // size
        0.0000f,  // damping
        27.4f,  // predelay ms
        0.28f,  // mix
        0.81f,  // modRate Hz
        0.2503f,  // modDepth
        1.0000f,  // width
        0.3750f,  // earlyDiff
        0.5634f,  // lateDiff
        0.90f,  // bassMult
        999.3f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        19920.2f,  // highCut Hz
        false,
        0.7845f,  // roomSize
        0.5000f,  // earlyLateBal
        3.39f,  // highDecay
        1.02f,  // midDecay
        6584.2f,  // highFreq Hz
        0.2011f,  // erShape
        0.1948f,  // erSpread
        20.0f,  // erBassCut Hz
        1.79f,  // trebleRatio
        0.1515f,  // stereoCoupling
        2957.6f,  // lowMidFreq Hz
        0.99f,  // lowMidDecay
        0,  // envMode (Off)
        501.3f,  // envHold ms
        500.8f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        105.1f,  // outEQ1Freq Hz
        -9.01f,  // outEQ1Gain dB
        1.01f,  // outEQ1Q
        1091.6f,  // outEQ2Freq Hz
        -3.81f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6009f,  // stereoInvert
        0.1187f,  // resonance
        0.0000f,  // echoPingPong
        0.7498f,  // dynAmount
        0.2510f  // dynSpeed
    });

    // Similar to VocalEcho with different delay taps (match: 92%)
    presets.push_back({
        "Vocal Tap",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.3831f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.97f,  // modRate Hz
        0.2511f,  // modDepth
        0.7232f,  // width
        0.1248f,  // earlyDiff
        0.5474f,  // lateDiff
        0.83f,  // bassMult
        133.5f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.1377f,  // roomSize
        0.9000f,  // earlyLateBal
        4.00f,  // highDecay
        1.03f,  // midDecay
        2111.8f,  // highFreq Hz
        0.5012f,  // erShape
        0.5029f,  // erSpread
        82.1f,  // erBassCut Hz
        1.36f,  // trebleRatio
        0.1488f,  // stereoCoupling
        100.0f,  // lowMidFreq Hz
        1.01f,  // lowMidDecay
        1,  // envMode (Gate)
        82.8f,  // envHold ms
        10.0f,  // envRelease ms
        87.5f,  // envDepth %
        375.1f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1094.8f,  // outEQ1Freq Hz
        -8.98f,  // outEQ1Gain dB
        1.03f,  // outEQ1Q
        4038.7f,  // outEQ2Freq Hz
        3.55f,  // outEQ2Gain dB
        1.05f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.7500f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Slightly warmer plate with less edge (match: 96%)
    presets.push_back({
        "Warm Plate",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.2199f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.81f,  // modRate Hz
        0.2499f,  // modDepth
        1.0000f,  // width
        0.1063f,  // earlyDiff
        0.4988f,  // lateDiff
        0.96f,  // bassMult
        596.6f,  // bassFreq Hz
        20.3f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.2761f,  // roomSize
        0.1000f,  // earlyLateBal
        4.00f,  // highDecay
        0.72f,  // midDecay
        3764.8f,  // highFreq Hz
        0.5022f,  // erShape
        0.5022f,  // erSpread
        20.2f,  // erBassCut Hz
        1.42f,  // trebleRatio
        0.1516f,  // stereoCoupling
        3566.0f,  // lowMidFreq Hz
        1.04f,  // lowMidDecay
        1,  // envMode (Gate)
        10.0f,  // envHold ms
        545.5f,  // envRelease ms
        70.2f,  // envDepth %
        0.0f,  // echoDelay ms
        2.3f,  // echoFeedback %
        516.3f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        2060.4f,  // outEQ2Freq Hz
        -7.00f,  // outEQ2Gain dB
        0.99f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.5979f,  // resonance
        0.0000f,  // echoPingPong
        -0.9971f,  // dynAmount
        0.2512f  // dynSpeed
    });

    // Tap tempo-controlled LFO1 modulates High Cut (match: 89%)
    presets.push_back({
        "What The Heck",
        "Plates",
        0,  // Plate
        0,  // 1970s
        0.1733f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.28f,  // mix
        0.35f,  // modRate Hz
        0.2789f,  // modDepth
        0.8423f,  // width
        0.7251f,  // earlyDiff
        0.5017f,  // lateDiff
        1.00f,  // bassMult
        243.5f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.7454f,  // roomSize
        0.3000f,  // earlyLateBal
        1.19f,  // highDecay
        0.95f,  // midDecay
        1483.7f,  // highFreq Hz
        0.5007f,  // erShape
        0.4774f,  // erSpread
        74.5f,  // erBassCut Hz
        1.96f,  // trebleRatio
        0.1279f,  // stereoCoupling
        100.0f,  // lowMidFreq Hz
        1.02f,  // lowMidDecay
        1,  // envMode (Gate)
        1011.8f,  // envHold ms
        1181.1f,  // envRelease ms
        91.4f,  // envDepth %
        293.5f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1087.7f,  // outEQ1Freq Hz
        12.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        2470.5f,  // outEQ2Freq Hz
        2.77f,  // outEQ2Gain dB
        1.51f,  // outEQ2Q
        0.6001f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.0000f,  // dynAmount
        0.2500f  // dynSpeed
    });


    // ==================== CREATIVE (27) ====================

    // Adjust compression/expansion and Custom 1 (match: 62%)
    presets.push_back({
        "Air Pressure",
        "Creative",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.6250f,  // damping
        9.4f,  // predelay ms
        0.35f,  // mix
        0.81f,  // modRate Hz
        0.2510f,  // modDepth
        0.5017f,  // width
        0.6238f,  // earlyDiff
        0.6276f,  // lateDiff
        1.30f,  // bassMult
        325.2f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.7503f,  // roomSize
        0.6000f,  // earlyLateBal
        0.53f,  // highDecay
        0.99f,  // midDecay
        1035.1f,  // highFreq Hz
        0.5151f,  // erShape
        1.0000f,  // erSpread
        20.1f,  // erBassCut Hz
        1.79f,  // trebleRatio
        0.1128f,  // stereoCoupling
        913.5f,  // lowMidFreq Hz
        0.94f,  // lowMidDecay
        0,  // envMode (Off)
        500.6f,  // envHold ms
        500.7f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1004.1f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4015.8f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        1.0000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Input signals reflect off brick buildings (match: 80%)
    presets.push_back({
        "Block Party",
        "Creative",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.3752f,  // damping
        25.9f,  // predelay ms
        0.35f,  // mix
        0.80f,  // modRate Hz
        0.2494f,  // modDepth
        1.0000f,  // width
        0.6254f,  // earlyDiff
        0.6219f,  // lateDiff
        1.63f,  // bassMult
        403.2f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        17613.9f,  // highCut Hz
        false,
        0.6223f,  // roomSize
        0.6000f,  // earlyLateBal
        0.25f,  // highDecay
        1.01f,  // midDecay
        3741.1f,  // highFreq Hz
        0.5042f,  // erShape
        0.0000f,  // erSpread
        204.7f,  // erBassCut Hz
        1.58f,  // trebleRatio
        0.1257f,  // stereoCoupling
        3652.8f,  // lowMidFreq Hz
        1.01f,  // lowMidDecay
        0,  // envMode (Off)
        511.9f,  // envHold ms
        1133.0f,  // envRelease ms
        90.7f,  // envDepth %
        223.9f,  // echoDelay ms
        0.0f,  // echoFeedback %
        2080.8f,  // outEQ1Freq Hz
        5.99f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        2490.6f,  // outEQ2Freq Hz
        1.80f,  // outEQ2Gain dB
        1.02f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        1.0000f,  // dynAmount
        0.2502f  // dynSpeed
    });

    // Varies Decay, Out Width, and High Cut (match: 96%)
    presets.push_back({
        "Bombay Club",
        "Creative",
        1,  // Room
        0,  // 1970s
        0.0752f,  // size
        0.2129f,  // damping
        0.0f,  // predelay ms
        0.35f,  // mix
        0.80f,  // modRate Hz
        0.2557f,  // modDepth
        0.3731f,  // width
        0.7641f,  // earlyDiff
        0.3761f,  // lateDiff
        1.58f,  // bassMult
        437.3f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.9254f,  // roomSize
        0.7000f,  // earlyLateBal
        1.30f,  // highDecay
        1.00f,  // midDecay
        1000.0f,  // highFreq Hz
        0.3968f,  // erShape
        0.4764f,  // erSpread
        152.1f,  // erBassCut Hz
        0.81f,  // trebleRatio
        0.1495f,  // stereoCoupling
        941.6f,  // lowMidFreq Hz
        0.94f,  // lowMidDecay
        0,  // envMode (Off)
        501.6f,  // envHold ms
        501.2f,  // envRelease ms
        80.3f,  // envDepth %
        53.1f,  // echoDelay ms
        0.0f,  // echoFeedback %
        2278.1f,  // outEQ1Freq Hz
        3.00f,  // outEQ1Gain dB
        1.01f,  // outEQ1Q
        5012.6f,  // outEQ2Freq Hz
        6.51f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6255f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -0.6012f,  // dynAmount
        0.5010f  // dynSpeed
    });

    // Dull backstage sound and large open space (match: 88%)
    presets.push_back({
        "Dull/Bright",
        "Creative",
        2,  // Hall
        0,  // 1970s
        0.0752f,  // size
        0.0000f,  // damping
        9.4f,  // predelay ms
        0.35f,  // mix
        0.85f,  // modRate Hz
        0.2519f,  // modDepth
        1.0000f,  // width
        0.2055f,  // earlyDiff
        0.4999f,  // lateDiff
        0.82f,  // bassMult
        1000.0f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6015f,  // roomSize
        0.6000f,  // earlyLateBal
        4.00f,  // highDecay
        0.99f,  // midDecay
        6501.6f,  // highFreq Hz
        0.4999f,  // erShape
        0.4486f,  // erSpread
        199.9f,  // erBassCut Hz
        1.58f,  // trebleRatio
        0.4311f,  // stereoCoupling
        2323.8f,  // lowMidFreq Hz
        0.94f,  // lowMidDecay
        1,  // envMode (Gate)
        200.0f,  // envHold ms
        300.0f,  // envRelease ms
        80.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1040.4f,  // outEQ1Freq Hz
        3.02f,  // outEQ1Gain dB
        1.01f,  // outEQ1Q
        5125.3f,  // outEQ2Freq Hz
        -8.18f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6001f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        1.0000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Echo, echo, echo. Master delays and outdoor echo (match: 92%)
    presets.push_back({
        "Echo Beach",
        "Creative",
        2,  // Hall
        0,  // 1970s
        0.0000f,  // size
        0.3730f,  // damping
        0.0f,  // predelay ms
        0.35f,  // mix
        0.79f,  // modRate Hz
        0.3560f,  // modDepth
        0.8988f,  // width
        0.5526f,  // earlyDiff
        0.3778f,  // lateDiff
        1.56f,  // bassMult
        745.4f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6214f,  // roomSize
        0.6000f,  // earlyLateBal
        1.34f,  // highDecay
        0.96f,  // midDecay
        2611.5f,  // highFreq Hz
        0.5029f,  // erShape
        0.5483f,  // erSpread
        96.9f,  // erBassCut Hz
        1.69f,  // trebleRatio
        0.5000f,  // stereoCoupling
        3064.5f,  // lowMidFreq Hz
        1.01f,  // lowMidDecay
        1,  // envMode (Gate)
        709.8f,  // envHold ms
        143.9f,  // envRelease ms
        100.0f,  // envDepth %
        102.2f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1360.0f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        3420.3f,  // outEQ2Freq Hz
        5.40f,  // outEQ2Gain dB
        1.01f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.6004f,  // resonance
        0.0000f,  // echoPingPong
        0.7505f,  // dynAmount
        0.5003f  // dynSpeed
    });

    // Medium chamber and an outdoor space (match: 91%)
    presets.push_back({
        "Indoors/Out",
        "Creative",
        3,  // Chamber
        0,  // 1970s
        0.0752f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.35f,  // mix
        0.84f,  // modRate Hz
        0.2550f,  // modDepth
        1.0000f,  // width
        0.0000f,  // earlyDiff
        0.5169f,  // lateDiff
        0.50f,  // bassMult
        191.4f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        1.0000f,  // roomSize
        0.7000f,  // earlyLateBal
        1.74f,  // highDecay
        1.00f,  // midDecay
        2650.0f,  // highFreq Hz
        0.7000f,  // erShape
        0.9250f,  // erSpread
        119.0f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1500f,  // stereoCoupling
        4050.0f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        1,  // envMode (Gate)
        756.2f,  // envHold ms
        313.7f,  // envRelease ms
        100.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        2552.5f,  // outEQ1Freq Hz
        -12.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        7741.7f,  // outEQ2Freq Hz
        9.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.0000f,  // dynAmount
        0.5000f  // dynSpeed
    });

    // Strange hall with input level controlling width (match: 56%)
    presets.push_back({
        "Inside-Out",
        "Creative",
        2,  // Hall
        0,  // 1970s
        0.2483f,  // size
        1.0000f,  // damping
        30.6f,  // predelay ms
        0.35f,  // mix
        0.80f,  // modRate Hz
        0.3172f,  // modDepth
        0.4834f,  // width
        0.0000f,  // earlyDiff
        0.5010f,  // lateDiff
        0.99f,  // bassMult
        100.0f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        19985.9f,  // highCut Hz
        false,
        0.8736f,  // roomSize
        0.6000f,  // earlyLateBal
        0.25f,  // highDecay
        1.01f,  // midDecay
        3015.2f,  // highFreq Hz
        0.5552f,  // erShape
        1.0000f,  // erSpread
        361.8f,  // erBassCut Hz
        1.06f,  // trebleRatio
        0.1266f,  // stereoCoupling
        1336.2f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        1,  // envMode (Gate)
        502.1f,  // envHold ms
        502.3f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1005.1f,  // outEQ1Freq Hz
        0.00f,  // outEQ1Gain dB
        1.01f,  // outEQ1Q
        4104.7f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.09f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.2507f  // dynSpeed
    });

    // Bipolar ADJUST to add Predelay or Dry Delay (match: 69%)
    presets.push_back({
        "Mic Location",
        "Creative",
        1,  // Room
        0,  // 1970s
        0.0000f,  // size
        0.9665f,  // damping
        56.7f,  // predelay ms
        0.35f,  // mix
        0.80f,  // modRate Hz
        0.2510f,  // modDepth
        0.0000f,  // width
        0.6222f,  // earlyDiff
        0.0000f,  // lateDiff
        1.19f,  // bassMult
        1000.0f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6251f,  // roomSize
        0.7000f,  // earlyLateBal
        0.43f,  // highDecay
        0.95f,  // midDecay
        1857.3f,  // highFreq Hz
        1.0000f,  // erShape
        0.0000f,  // erSpread
        443.3f,  // erBassCut Hz
        0.32f,  // trebleRatio
        0.0750f,  // stereoCoupling
        940.4f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        2,  // envMode (Reverse)
        496.5f,  // envHold ms
        501.0f,  // envRelease ms
        80.8f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        100.1f,  // outEQ1Freq Hz
        -6.02f,  // outEQ1Gain dB
        1.54f,  // outEQ1Q
        3886.5f,  // outEQ2Freq Hz
        6.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.6012f,  // resonance
        0.0000f,  // echoPingPong
        0.7502f,  // dynAmount
        0.5067f  // dynSpeed
    });

    // Select Buzzing or Modulated special effects (match: 85%)
    presets.push_back({
        "Mr. Vader",
        "Creative",
        9,  // Dirty Hall
        0,  // 1970s
        0.0752f,  // size
        0.0000f,  // damping
        13.3f,  // predelay ms
        0.35f,  // mix
        0.81f,  // modRate Hz
        0.2517f,  // modDepth
        0.0000f,  // width
        0.5382f,  // earlyDiff
        0.5552f,  // lateDiff
        1.01f,  // bassMult
        922.0f,  // bassFreq Hz
        34.6f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5020f,  // roomSize
        0.6000f,  // earlyLateBal
        4.00f,  // highDecay
        1.01f,  // midDecay
        5104.0f,  // highFreq Hz
        0.5026f,  // erShape
        0.0754f,  // erSpread
        20.2f,  // erBassCut Hz
        1.71f,  // trebleRatio
        0.1478f,  // stereoCoupling
        7979.3f,  // lowMidFreq Hz
        0.99f,  // lowMidDecay
        0,  // envMode (Off)
        594.4f,  // envHold ms
        501.8f,  // envRelease ms
        18.9f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1055.5f,  // outEQ1Freq Hz
        -3.63f,  // outEQ1Gain dB
        1.01f,  // outEQ1Q
        4094.2f,  // outEQ2Freq Hz
        -0.62f,  // outEQ2Gain dB
        0.83f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0263f,  // resonance
        0.0000f,  // echoPingPong
        -0.7943f,  // dynAmount
        0.4990f  // dynSpeed
    });

    // Size and Delay inversely proportionate, supernatural (match: 93%)
    presets.push_back({
        "Mythology",
        "Creative",
        2,  // Hall
        0,  // 1970s
        0.1733f,  // size
        0.0000f,  // damping
        0.0f,  // predelay ms
        0.35f,  // mix
        0.80f,  // modRate Hz
        0.3181f,  // modDepth
        0.9262f,  // width
        0.2461f,  // earlyDiff
        0.4995f,  // lateDiff
        1.55f,  // bassMult
        553.4f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        1.0000f,  // roomSize
        0.8000f,  // earlyLateBal
        1.70f,  // highDecay
        1.01f,  // midDecay
        3228.5f,  // highFreq Hz
        0.4882f,  // erShape
        1.0000f,  // erSpread
        20.1f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1506f,  // stereoCoupling
        3068.5f,  // lowMidFreq Hz
        1.01f,  // lowMidDecay
        0,  // envMode (Off)
        502.3f,  // envHold ms
        502.0f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        2077.2f,  // outEQ1Freq Hz
        -7.69f,  // outEQ1Gain dB
        0.98f,  // outEQ1Q
        4653.5f,  // outEQ2Freq Hz
        3.00f,  // outEQ2Gain dB
        1.01f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.3006f,  // resonance
        0.0000f,  // echoPingPong
        -0.6011f,  // dynAmount
        0.2505f  // dynSpeed
    });

    // Split simulating two automobile tunnels (match: 96%)
    presets.push_back({
        "NYC Tunnels",
        "Creative",
        2,  // Hall
        0,  // 1970s
        0.2199f,  // size
        0.7150f,  // damping
        74.4f,  // predelay ms
        0.35f,  // mix
        0.77f,  // modRate Hz
        0.2579f,  // modDepth
        1.0000f,  // width
        0.4961f,  // earlyDiff
        0.5459f,  // lateDiff
        1.30f,  // bassMult
        402.2f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        19962.1f,  // highCut Hz
        false,
        0.3766f,  // roomSize
        0.6000f,  // earlyLateBal
        0.74f,  // highDecay
        0.95f,  // midDecay
        3783.0f,  // highFreq Hz
        0.4311f,  // erShape
        0.2653f,  // erSpread
        20.2f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1480f,  // stereoCoupling
        3668.3f,  // lowMidFreq Hz
        0.98f,  // lowMidDecay
        0,  // envMode (Off)
        502.1f,  // envHold ms
        502.7f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        2268.5f,  // outEQ1Freq Hz
        -3.01f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4047.2f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.01f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.0000f,  // dynAmount
        0.2511f  // dynSpeed
    });

    // Open space, not much reflection, max DryDly (match: 79%)
    presets.push_back({
        "Outdoor PA",
        "Creative",
        1,  // Room
        0,  // 1970s
        0.0000f,  // size
        0.0000f,  // damping
        88.9f,  // predelay ms
        0.35f,  // mix
        0.80f,  // modRate Hz
        0.2500f,  // modDepth
        0.2076f,  // width
        0.4985f,  // earlyDiff
        0.5446f,  // lateDiff
        0.99f,  // bassMult
        816.5f,  // bassFreq Hz
        20.3f,  // lowCut Hz
        19772.5f,  // highCut Hz
        false,
        0.0000f,  // roomSize
        0.6000f,  // earlyLateBal
        1.04f,  // highDecay
        1.20f,  // midDecay
        6502.7f,  // highFreq Hz
        0.8181f,  // erShape
        0.2959f,  // erSpread
        500.0f,  // erBassCut Hz
        0.30f,  // trebleRatio
        0.2632f,  // stereoCoupling
        2205.3f,  // lowMidFreq Hz
        1.01f,  // lowMidDecay
        2,  // envMode (Reverse)
        512.5f,  // envHold ms
        10.0f,  // envRelease ms
        100.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1256.7f,  // outEQ1Freq Hz
        12.00f,  // outEQ1Gain dB
        1.47f,  // outEQ1Q
        6363.8f,  // outEQ2Freq Hz
        -12.00f,  // outEQ2Gain dB
        1.01f,  // outEQ2Q
        0.6254f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.7503f,  // dynAmount
        0.2508f  // dynSpeed
    });

    // Similar to Outdoor PA, 5 different settings (match: 67%)
    presets.push_back({
        "Outdoor PA 2",
        "Creative",
        1,  // Room
        0,  // 1970s
        0.0000f,  // size
        0.8787f,  // damping
        0.0f,  // predelay ms
        0.35f,  // mix
        0.82f,  // modRate Hz
        0.2513f,  // modDepth
        0.2492f,  // width
        0.5397f,  // earlyDiff
        0.5016f,  // lateDiff
        1.14f,  // bassMult
        438.5f,  // bassFreq Hz
        53.3f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.0000f,  // roomSize
        0.6000f,  // earlyLateBal
        0.25f,  // highDecay
        1.00f,  // midDecay
        3760.2f,  // highFreq Hz
        0.5004f,  // erShape
        0.0691f,  // erSpread
        236.3f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1718f,  // stereoCoupling
        987.8f,  // lowMidFreq Hz
        0.97f,  // lowMidDecay
        2,  // envMode (Reverse)
        507.0f,  // envHold ms
        502.6f,  // envRelease ms
        80.2f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        100.5f,  // outEQ1Freq Hz
        -4.22f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4003.4f,  // outEQ2Freq Hz
        -10.02f,  // outEQ2Gain dB
        1.02f,  // outEQ2Q
        0.5260f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.0000f,  // dynAmount
        0.5014f  // dynSpeed
    });

    // Tempo reflects Dry L/R from 0.292-32.49 sec (match: 86%)
    presets.push_back({
        "Reverse Taps",
        "Creative",
        2,  // Hall
        0,  // 1970s
        0.0752f,  // size
        0.8105f,  // damping
        231.4f,  // predelay ms
        0.35f,  // mix
        0.82f,  // modRate Hz
        0.4000f,  // modDepth
        1.0000f,  // width
        0.3750f,  // earlyDiff
        0.3010f,  // lateDiff
        1.55f,  // bassMult
        347.6f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.2499f,  // roomSize
        0.7000f,  // earlyLateBal
        0.90f,  // highDecay
        0.94f,  // midDecay
        7050.0f,  // highFreq Hz
        0.5037f,  // erShape
        1.0000f,  // erSpread
        38.0f,  // erBassCut Hz
        1.45f,  // trebleRatio
        0.1875f,  // stereoCoupling
        6321.2f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        2,  // envMode (Reverse)
        826.8f,  // envHold ms
        719.8f,  // envRelease ms
        81.3f,  // envDepth %
        150.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1087.5f,  // outEQ1Freq Hz
        12.00f,  // outEQ1Gain dB
        0.96f,  // outEQ1Q
        1093.5f,  // outEQ2Freq Hz
        12.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6001f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -0.6000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Imagine an empty hall from the perspective of stage (match: 96%)
    presets.push_back({
        "Sound Check",
        "Creative",
        2,  // Hall
        0,  // 1970s
        0.0752f,  // size
        0.6243f,  // damping
        0.0f,  // predelay ms
        0.35f,  // mix
        0.80f,  // modRate Hz
        0.2509f,  // modDepth
        1.0000f,  // width
        0.5493f,  // earlyDiff
        0.5564f,  // lateDiff
        0.84f,  // bassMult
        244.0f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.6228f,  // roomSize
        0.6000f,  // earlyLateBal
        2.31f,  // highDecay
        1.00f,  // midDecay
        12000.0f,  // highFreq Hz
        0.4966f,  // erShape
        0.2505f,  // erSpread
        20.1f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1746f,  // stereoCoupling
        7048.9f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        0,  // envMode (Off)
        502.0f,  // envHold ms
        280.2f,  // envRelease ms
        5.4f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1089.9f,  // outEQ1Freq Hz
        -3.83f,  // outEQ1Gain dB
        0.99f,  // outEQ1Q
        4015.8f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        1.0000f,  // dynAmount
        0.2503f  // dynSpeed
    });

    // Changes Pre Delay/Dry Delay mix (match: 90%)
    presets.push_back({
        "Sound Stage",
        "Creative",
        2,  // Hall
        0,  // 1970s
        0.0000f,  // size
        0.8998f,  // damping
        0.0f,  // predelay ms
        0.35f,  // mix
        0.80f,  // modRate Hz
        0.2556f,  // modDepth
        0.2880f,  // width
        0.9551f,  // earlyDiff
        0.3762f,  // lateDiff
        1.23f,  // bassMult
        956.9f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.0000f,  // roomSize
        0.5000f,  // earlyLateBal
        1.84f,  // highDecay
        1.00f,  // midDecay
        4616.9f,  // highFreq Hz
        0.2503f,  // erShape
        0.9588f,  // erSpread
        500.0f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.3635f,  // stereoCoupling
        6350.5f,  // lowMidFreq Hz
        0.95f,  // lowMidDecay
        4,  // envMode (Ducked)
        1259.1f,  // envHold ms
        1003.7f,  // envRelease ms
        80.3f,  // envDepth %
        126.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1332.2f,  // outEQ1Freq Hz
        -6.02f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4062.3f,  // outEQ2Freq Hz
        -6.01f,  // outEQ2Gain dB
        0.99f,  // outEQ2Q
        0.6009f,  // stereoInvert
        0.5998f,  // resonance
        0.0000f,  // echoPingPong
        0.1502f,  // dynAmount
        0.5500f  // dynSpeed
    });

    // Compress and Expand ratios are cranked (match: 73%)
    presets.push_back({
        "Spatializer",
        "Creative",
        7,  // Chorus Space
        0,  // 1970s
        0.0752f,  // size
        0.4194f,  // damping
        62.4f,  // predelay ms
        0.35f,  // mix
        0.80f,  // modRate Hz
        0.2497f,  // modDepth
        1.0000f,  // width
        0.5399f,  // earlyDiff
        0.5035f,  // lateDiff
        1.00f,  // bassMult
        415.5f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.4994f,  // roomSize
        0.6000f,  // earlyLateBal
        0.80f,  // highDecay
        0.76f,  // midDecay
        1588.3f,  // highFreq Hz
        0.4989f,  // erShape
        0.0000f,  // erSpread
        320.8f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.2760f,  // stereoCoupling
        2332.2f,  // lowMidFreq Hz
        1.01f,  // lowMidDecay
        1,  // envMode (Gate)
        198.0f,  // envHold ms
        398.1f,  // envRelease ms
        79.8f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        920.1f,  // outEQ1Freq Hz
        -6.03f,  // outEQ1Gain dB
        1.01f,  // outEQ1Q
        4005.7f,  // outEQ2Freq Hz
        5.94f,  // outEQ2Gain dB
        1.01f,  // outEQ2Q
        0.6022f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        1.0000f,  // dynAmount
        0.2506f  // dynSpeed
    });

    // Designed to simulate a large sports stadium (match: 84%)
    presets.push_back({
        "Stadium",
        "Creative",
        2,  // Hall
        0,  // 1970s
        0.0752f,  // size
        0.0533f,  // damping
        0.0f,  // predelay ms
        0.35f,  // mix
        0.84f,  // modRate Hz
        0.2470f,  // modDepth
        0.5097f,  // width
        0.0745f,  // earlyDiff
        0.6310f,  // lateDiff
        1.14f,  // bassMult
        780.1f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.8973f,  // roomSize
        0.6000f,  // earlyLateBal
        2.24f,  // highDecay
        0.95f,  // midDecay
        3752.7f,  // highFreq Hz
        0.3148f,  // erShape
        0.9559f,  // erSpread
        266.4f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1490f,  // stereoCoupling
        4153.3f,  // lowMidFreq Hz
        0.96f,  // lowMidDecay
        0,  // envMode (Off)
        505.5f,  // envHold ms
        501.9f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        927.3f,  // outEQ1Freq Hz
        5.99f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        3068.2f,  // outEQ2Freq Hz
        5.97f,  // outEQ2Gain dB
        1.02f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.3006f,  // resonance
        0.0000f,  // echoPingPong
        0.0000f,  // dynAmount
        0.2502f  // dynSpeed
    });

    // Places source within a very reflective tomb (match: 85%)
    presets.push_back({
        "The Tomb",
        "Creative",
        4,  // Cathedral
        0,  // 1970s
        0.3132f,  // size
        0.9505f,  // damping
        0.0f,  // predelay ms
        0.35f,  // mix
        0.80f,  // modRate Hz
        0.2450f,  // modDepth
        1.0000f,  // width
        0.6230f,  // earlyDiff
        0.5509f,  // lateDiff
        1.55f,  // bassMult
        782.2f,  // bassFreq Hz
        20.5f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.7514f,  // roomSize
        0.7000f,  // earlyLateBal
        0.72f,  // highDecay
        1.00f,  // midDecay
        1825.0f,  // highFreq Hz
        0.8941f,  // erShape
        0.5028f,  // erSpread
        86.5f,  // erBassCut Hz
        1.91f,  // trebleRatio
        0.1508f,  // stereoCoupling
        6672.8f,  // lowMidFreq Hz
        0.96f,  // lowMidDecay
        1,  // envMode (Gate)
        201.5f,  // envHold ms
        301.1f,  // envRelease ms
        80.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        3062.5f,  // outEQ1Freq Hz
        -3.77f,  // outEQ1Gain dB
        1.01f,  // outEQ1Q
        3444.8f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.01f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.6014f,  // resonance
        0.0000f,  // echoPingPong
        1.0000f,  // dynAmount
        0.2500f  // dynSpeed
    });

    // Inside of a VW van and inside of a VW bug (match: 94%)
    presets.push_back({
        "Two Autos",
        "Creative",
        1,  // Room
        0,  // 1970s
        0.0000f,  // size
        0.0000f,  // damping
        9.4f,  // predelay ms
        0.35f,  // mix
        0.80f,  // modRate Hz
        0.2555f,  // modDepth
        1.0000f,  // width
        0.5060f,  // earlyDiff
        0.8960f,  // lateDiff
        1.23f,  // bassMult
        573.3f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.3739f,  // roomSize
        0.6000f,  // earlyLateBal
        1.20f,  // highDecay
        0.72f,  // midDecay
        3723.6f,  // highFreq Hz
        0.9651f,  // erShape
        0.6245f,  // erSpread
        152.1f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.5000f,  // stereoCoupling
        4050.9f,  // lowMidFreq Hz
        0.93f,  // lowMidDecay
        1,  // envMode (Gate)
        212.3f,  // envHold ms
        386.7f,  // envRelease ms
        71.3f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        940.6f,  // outEQ1Freq Hz
        -9.26f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        5044.2f,  // outEQ2Freq Hz
        -7.45f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6008f,  // stereoInvert
        0.6008f,  // resonance
        0.0000f,  // echoPingPong
        1.0000f,  // dynAmount
        0.2501f  // dynSpeed
    });

    // Get lost in the crowd, produces multiple voices (match: 79%)
    presets.push_back({
        "Voices?",
        "Creative",
        8,  // Random Space
        0,  // 1970s
        0.2199f,  // size
        0.7449f,  // damping
        95.0f,  // predelay ms
        0.35f,  // mix
        0.80f,  // modRate Hz
        0.2500f,  // modDepth
        0.9633f,  // width
        0.1903f,  // earlyDiff
        0.6249f,  // lateDiff
        1.00f,  // bassMult
        1000.0f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.5007f,  // roomSize
        0.9000f,  // earlyLateBal
        1.28f,  // highDecay
        1.00f,  // midDecay
        12000.0f,  // highFreq Hz
        0.8892f,  // erShape
        0.0000f,  // erSpread
        74.2f,  // erBassCut Hz
        1.81f,  // trebleRatio
        0.1496f,  // stereoCoupling
        3082.6f,  // lowMidFreq Hz
        0.71f,  // lowMidDecay
        3,  // envMode (Swell)
        501.9f,  // envHold ms
        501.9f,  // envRelease ms
        81.9f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        101.8f,  // outEQ1Freq Hz
        2.40f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4014.1f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.0000f,  // dynAmount
        0.5010f  // dynSpeed
    });

    // Similar to Voices?, with LFO controlling OutWidth (match: 93%)
    presets.push_back({
        "Voices? 2",
        "Creative",
        8,  // Random Space
        0,  // 1970s
        0.2343f,  // size
        0.7751f,  // damping
        130.6f,  // predelay ms
        0.35f,  // mix
        0.80f,  // modRate Hz
        0.2500f,  // modDepth
        0.9624f,  // width
        0.1875f,  // earlyDiff
        0.6250f,  // lateDiff
        1.19f,  // bassMult
        100.0f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.3750f,  // roomSize
        0.6000f,  // earlyLateBal
        0.25f,  // highDecay
        1.00f,  // midDecay
        2375.2f,  // highFreq Hz
        0.0531f,  // erShape
        0.6250f,  // erSpread
        92.0f,  // erBassCut Hz
        0.30f,  // trebleRatio
        0.1498f,  // stereoCoupling
        100.0f,  // lowMidFreq Hz
        1.00f,  // lowMidDecay
        1,  // envMode (Gate)
        1036.4f,  // envHold ms
        1818.3f,  // envRelease ms
        62.5f,  // envDepth %
        79.7f,  // echoDelay ms
        0.0f,  // echoFeedback %
        3655.3f,  // outEQ1Freq Hz
        7.44f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        5202.6f,  // outEQ2Freq Hz
        9.65f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.5500f,  // stereoInvert
        0.6250f,  // resonance
        0.0000f,  // echoPingPong
        0.7500f,  // dynAmount
        0.5000f  // dynSpeed
    });

    // Decay level, predelay, dry delay, dry mix (match: 80%)
    presets.push_back({
        "Wall Slap",
        "Creative",
        1,  // Room
        0,  // 1970s
        0.0000f,  // size
        0.9023f,  // damping
        21.5f,  // predelay ms
        0.35f,  // mix
        1.34f,  // modRate Hz
        0.2507f,  // modDepth
        0.1144f,  // width
        0.4992f,  // earlyDiff
        0.5908f,  // lateDiff
        2.63f,  // bassMult
        636.4f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        17114.2f,  // highCut Hz
        false,
        0.2894f,  // roomSize
        0.6000f,  // earlyLateBal
        0.90f,  // highDecay
        0.73f,  // midDecay
        4033.4f,  // highFreq Hz
        0.4992f,  // erShape
        0.1243f,  // erSpread
        61.9f,  // erBassCut Hz
        1.79f,  // trebleRatio
        0.0000f,  // stereoCoupling
        3166.8f,  // lowMidFreq Hz
        1.19f,  // lowMidDecay
        2,  // envMode (Reverse)
        1006.8f,  // envHold ms
        799.1f,  // envRelease ms
        79.5f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        133.7f,  // outEQ1Freq Hz
        9.46f,  // outEQ1Gain dB
        0.72f,  // outEQ1Q
        5084.6f,  // outEQ2Freq Hz
        0.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.5995f,  // stereoInvert
        0.6009f,  // resonance
        0.0000f,  // echoPingPong
        -0.2498f,  // dynAmount
        0.5497f  // dynSpeed
    });

    // Opposite side of windows that can be opened (match: 88%)
    presets.push_back({
        "Window",
        "Creative",
        1,  // Room
        0,  // 1970s
        0.0000f,  // size
        0.2765f,  // damping
        2.8f,  // predelay ms
        0.35f,  // mix
        0.79f,  // modRate Hz
        0.2573f,  // modDepth
        1.0000f,  // width
        0.7453f,  // earlyDiff
        0.2015f,  // lateDiff
        1.18f,  // bassMult
        960.1f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.3968f,  // roomSize
        0.6000f,  // earlyLateBal
        0.80f,  // highDecay
        1.00f,  // midDecay
        1821.7f,  // highFreq Hz
        0.4995f,  // erShape
        0.5006f,  // erSpread
        20.1f,  // erBassCut Hz
        1.75f,  // trebleRatio
        0.0575f,  // stereoCoupling
        1282.8f,  // lowMidFreq Hz
        1.28f,  // lowMidDecay
        2,  // envMode (Reverse)
        507.9f,  // envHold ms
        502.4f,  // envRelease ms
        81.6f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1344.6f,  // outEQ1Freq Hz
        3.80f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4063.8f,  // outEQ2Freq Hz
        -12.00f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.6297f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.3153f,  // dynAmount
        0.0000f  // dynSpeed
    });

    // LFO drives OutWidth to make the room wobble (match: 85%)
    presets.push_back({
        "Wobble Room",
        "Creative",
        7,  // Chorus Space
        0,  // 1970s
        0.2199f,  // size
        0.1548f,  // damping
        11.9f,  // predelay ms
        0.35f,  // mix
        0.80f,  // modRate Hz
        0.2502f,  // modDepth
        0.6356f,  // width
        0.8881f,  // earlyDiff
        0.5504f,  // lateDiff
        1.55f,  // bassMult
        400.3f,  // bassFreq Hz
        20.2f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.2501f,  // roomSize
        0.9000f,  // earlyLateBal
        1.36f,  // highDecay
        0.72f,  // midDecay
        1590.8f,  // highFreq Hz
        0.0000f,  // erShape
        1.0000f,  // erSpread
        200.1f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1249f,  // stereoCoupling
        2076.0f,  // lowMidFreq Hz
        0.95f,  // lowMidDecay
        2,  // envMode (Reverse)
        995.5f,  // envHold ms
        752.9f,  // envRelease ms
        79.8f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        509.8f,  // outEQ1Freq Hz
        -11.11f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        4002.9f,  // outEQ2Freq Hz
        -1.80f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.3002f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        0.0000f,  // dynAmount
        0.5002f  // dynSpeed
    });

    // Custom Controls for variable equation (match: 94%)
    presets.push_back({
        "X Variable",
        "Creative",
        2,  // Hall
        0,  // 1970s
        0.2199f,  // size
        0.5094f,  // damping
        0.0f,  // predelay ms
        0.35f,  // mix
        0.81f,  // modRate Hz
        0.1460f,  // modDepth
        1.0000f,  // width
        0.7747f,  // earlyDiff
        0.5586f,  // lateDiff
        1.24f,  // bassMult
        511.9f,  // bassFreq Hz
        20.0f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.3804f,  // roomSize
        0.5000f,  // earlyLateBal
        0.62f,  // highDecay
        1.03f,  // midDecay
        5124.8f,  // highFreq Hz
        1.0000f,  // erShape
        0.0000f,  // erSpread
        20.2f,  // erBassCut Hz
        1.24f,  // trebleRatio
        0.1448f,  // stereoCoupling
        4896.0f,  // lowMidFreq Hz
        1.03f,  // lowMidDecay
        0,  // envMode (Off)
        504.9f,  // envHold ms
        500.0f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        440.5f,  // outEQ1Freq Hz
        -7.06f,  // outEQ1Gain dB
        1.02f,  // outEQ1Q
        3065.6f,  // outEQ2Freq Hz
        3.04f,  // outEQ2Gain dB
        1.01f,  // outEQ2Q
        0.0000f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -1.0000f,  // dynAmount
        0.5024f  // dynSpeed
    });

    // Random Hall version of X Variable (match: 97%)
    presets.push_back({
        "Y Variable",
        "Creative",
        2,  // Hall
        0,  // 1970s
        0.3132f,  // size
        0.7487f,  // damping
        0.0f,  // predelay ms
        0.35f,  // mix
        0.84f,  // modRate Hz
        0.2442f,  // modDepth
        0.9588f,  // width
        0.3789f,  // earlyDiff
        0.5027f,  // lateDiff
        1.04f,  // bassMult
        213.7f,  // bassFreq Hz
        20.1f,  // lowCut Hz
        20000.0f,  // highCut Hz
        false,
        0.8819f,  // roomSize
        0.4000f,  // earlyLateBal
        3.52f,  // highDecay
        1.01f,  // midDecay
        2164.5f,  // highFreq Hz
        0.5025f,  // erShape
        0.1597f,  // erSpread
        459.4f,  // erBassCut Hz
        2.00f,  // trebleRatio
        0.1511f,  // stereoCoupling
        3116.3f,  // lowMidFreq Hz
        1.01f,  // lowMidDecay
        0,  // envMode (Off)
        499.6f,  // envHold ms
        501.6f,  // envRelease ms
        0.0f,  // envDepth %
        0.0f,  // echoDelay ms
        0.0f,  // echoFeedback %
        1003.3f,  // outEQ1Freq Hz
        0.00f,  // outEQ1Gain dB
        1.00f,  // outEQ1Q
        3441.9f,  // outEQ2Freq Hz
        -4.86f,  // outEQ2Gain dB
        1.00f,  // outEQ2Q
        0.3002f,  // stereoInvert
        0.0000f,  // resonance
        0.0000f,  // echoPingPong
        -0.0000f,  // dynAmount
        0.5007f  // dynSpeed
    });

    return presets;
}

//==============================================================================
inline void applyPreset(juce::AudioProcessorValueTreeState& params, const Preset& preset)
{
    // Helper: clamp to parameter range before normalizing to 0-1.
    // convertTo0to1 does NOT clamp, so out-of-range preset values would
    // produce values outside [0,1] and trigger jassert in hosts.
    auto setRanged = [&](const char* id, float value)
    {
        if (auto* p = params.getParameter(id))
        {
            const auto range = params.getParameterRange(id);
            const auto clamped = range.snapToLegalValue(value);
            p->setValueNotifyingHost(range.convertTo0to1(clamped));
        }
    };

    // Mode (10 choices: normalize by 9.0)
    if (auto* p = params.getParameter("mode"))
        p->setValueNotifyingHost(preset.mode / 9.0f);

    // Color (3 choices: normalize by 2.0)
    if (auto* p = params.getParameter("color"))
        p->setValueNotifyingHost(preset.color / 2.0f);

    // Continuous parameters — already 0-1 normalized
    if (auto* p = params.getParameter("size"))
        p->setValueNotifyingHost(preset.size);

    if (auto* p = params.getParameter("damping"))
        p->setValueNotifyingHost(preset.damping);

    setRanged("predelay", preset.predelay);

    if (auto* p = params.getParameter("mix"))
        p->setValueNotifyingHost(preset.mix);

    setRanged("modrate", preset.modRate);

    if (auto* p = params.getParameter("moddepth"))
        p->setValueNotifyingHost(preset.modDepth);

    if (auto* p = params.getParameter("width"))
        p->setValueNotifyingHost(preset.width);

    if (auto* p = params.getParameter("earlydiff"))
        p->setValueNotifyingHost(preset.earlyDiff);

    if (auto* p = params.getParameter("latediff"))
        p->setValueNotifyingHost(preset.lateDiff);

    setRanged("bassmult", preset.bassMult);
    setRanged("bassfreq", preset.bassFreq);
    setRanged("lowcut", preset.lowCut);
    setRanged("highcut", preset.highCut);

    if (auto* p = params.getParameter("freeze"))
        p->setValueNotifyingHost(preset.freeze ? 1.0f : 0.0f);

    if (auto* p = params.getParameter("roomsize"))
        p->setValueNotifyingHost(preset.roomSize);

    if (auto* p = params.getParameter("erlatebal"))
        p->setValueNotifyingHost(preset.earlyLateBal);

    setRanged("highdecay", preset.highDecay);
    setRanged("middecay", preset.midDecay);
    setRanged("highfreq", preset.highFreq);

    if (auto* p = params.getParameter("ershape"))
        p->setValueNotifyingHost(preset.erShape);

    if (auto* p = params.getParameter("erspread"))
        p->setValueNotifyingHost(preset.erSpread);

    setRanged("erbasscut", preset.erBassCut);

    // Extended parameters
    setRanged("trebleratio", preset.trebleRatio);
    setRanged("stereocoupling", preset.stereoCoupling);
    setRanged("lowmidfreq", preset.lowMidFreq);
    setRanged("lowmiddecay", preset.lowMidDecay);

    // Envelope mode (5 choices: normalize by 4.0)
    if (auto* p = params.getParameter("envmode"))
        p->setValueNotifyingHost(preset.envMode / 4.0f);

    setRanged("envhold", preset.envHold);
    setRanged("envrelease", preset.envRelease);
    setRanged("envdepth", preset.envDepth);
    setRanged("echodelay", preset.echoDelay);
    setRanged("echofeedback", preset.echoFeedback);

    setRanged("outeq1freq", preset.outEQ1Freq);
    setRanged("outeq1gain", preset.outEQ1Gain);
    setRanged("outeq1q", preset.outEQ1Q);
    setRanged("outeq2freq", preset.outEQ2Freq);
    setRanged("outeq2gain", preset.outEQ2Gain);
    setRanged("outeq2q", preset.outEQ2Q);

    if (auto* p = params.getParameter("stereoinvert"))
        p->setValueNotifyingHost(preset.stereoInvert);

    if (auto* p = params.getParameter("resonance"))
        p->setValueNotifyingHost(preset.resonance);

    if (auto* p = params.getParameter("echopingpong"))
        p->setValueNotifyingHost(preset.echoPingPong);

    setRanged("dynamount", preset.dynAmount);

    if (auto* p = params.getParameter("dynspeed"))
        p->setValueNotifyingHost(preset.dynSpeed);
}

} // namespace Velvet90Presets
