#pragma once

struct AlgorithmConfig
{
    const char* name;

    int delayLengths[16];

    int   leftTaps[8];
    int   rightTaps[8];
    float leftSigns[8];
    float rightSigns[8];

    float inputDiffMaxCoeff12;
    float inputDiffMaxCoeff34;
    float outputDiffScale;

    float bandwidthHz;

    float erLevelScale;
    float erTimeScale;

    float lateGainScale;

    float modDepthScale;
    float modRateScale;

    float trebleMultScale;
    float bassMultScale;

    float sizeRangeMin;
    float sizeRangeMax;
};

// ---------------------------------------------------------------------------
// Plate: EMT 140 / Lexicon 224 character.
// Tight delay clustering (15-40ms), maximum diffusion, no ERs, bright.
static constexpr AlgorithmConfig kPlate = {
    "Plate",
    { 661, 709, 743, 787, 811, 853, 883, 919,
      947, 983, 1021, 1063, 1097, 1151, 1201, 1249 },
    { 0, 2, 5, 7, 9, 11, 13, 15 },
    { 1, 3, 4, 6, 8, 10, 12, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.75f, 0.75f,   // input diffusion: uniform high
    1.0f,            // output diffusion scale
    14000.0f,        // bandwidth: bright
    0.0f, 1.0f,      // ER: forced off
    1.0f,            // late gain
    0.3f, 1.0f,      // mod: minimal depth, normal rate
    1.0f, 1.0f,      // damping: neutral (plates sustain treble)
    0.5f, 1.5f       // size range
};

// ---------------------------------------------------------------------------
// Hall: Lexicon 480L "Random Hall" / 224 "Concert Hall".
// This is the current DuskVerb — all scale factors are 1.0.
static constexpr AlgorithmConfig kHall = {
    "Hall",
    { 887, 953, 1039, 1151, 1277, 1399, 1549, 1699,
      1873, 2063, 2281, 2503, 2719, 2927, 3089, 3251 },
    { 0, 3, 5, 8, 10, 11, 14, 15 },
    { 1, 2, 4, 6, 7, 9, 12, 13 },
    { 1.0f, 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f },
    { -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f },
    0.75f, 0.625f,   // input diffusion: Dattorro split
    1.0f,            // output diffusion scale
    10000.0f,        // bandwidth: standard
    1.0f, 1.0f,      // ER: full
    1.0f,            // late gain
    1.0f, 1.0f,      // mod: full
    1.0f, 1.0f,      // damping: neutral
    0.5f, 1.5f       // size range
};

// ---------------------------------------------------------------------------
// Chamber: Lexicon 480L "Rich Chamber" / AMS RMX16 "Ambience".
// Medium delay spread, slightly brighter than hall, moderate ER.
static constexpr AlgorithmConfig kChamber = {
    "Chamber",
    { 751, 809, 863, 929, 997, 1061, 1129, 1193,
      1259, 1327, 1399, 1471, 1543, 1613, 1693, 1777 },
    { 0, 2, 5, 7, 9, 11, 13, 15 },
    { 1, 3, 4, 6, 8, 10, 12, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f },
    0.75f, 0.625f,   // input diffusion: Dattorro split
    1.0f,            // output diffusion scale
    10000.0f,        // bandwidth: standard
    0.8f, 0.85f,     // ER: slightly reduced level, tighter timing
    1.0f,            // late gain
    0.6f, 1.0f,      // mod: moderate depth, normal rate
    1.15f, 1.0f,     // damping: brighter treble
    0.5f, 1.5f       // size range
};

// ---------------------------------------------------------------------------
// Room: Lexicon PCM70 small rooms / 480L "Small Room".
// Geometrically-spaced delays (7-25ms), ER-dominant, moderate modulation.
// Wider delay ratio (3.56:1) eliminates flutter echo from the old arithmetic
// spacing. Modulation breaks up metallic ringing per Dattorro/Costello.
static constexpr AlgorithmConfig kRoom = {
    "Room",
    { 307, 331, 359, 389, 431, 461, 503, 547,
      599, 653, 719, 773, 857, 937, 1009, 1093 },
    { 0, 3, 5, 6, 9, 10, 12, 15 },
    { 1, 2, 4, 7, 8, 11, 13, 14 },
    { 1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, -1.0f },
    { -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f },
    0.65f, 0.55f,    // input diffusion: moderate (was 0.55/0.45)
    1.0f,            // output diffusion scale
    12000.0f,        // bandwidth: bright
    1.5f, 0.6f,      // ER: boosted level, shorter timing
    0.7f,            // late gain: reduced (ER-dominant)
    0.5f, 1.1f,      // mod: meaningful depth (was 0.15), slightly faster rate
    0.85f, 0.9f,     // damping: slightly darker, less bass buildup
    0.5f, 1.5f       // size range
};

// ---------------------------------------------------------------------------
// Ambient: Lexicon PCM96 "Infinite" / Strymon BigSky "Cloud".
// Widest delay spread, max diffusion, heavy modulation, no ERs.
static constexpr AlgorithmConfig kAmbient = {
    "Ambient",
    { 971, 1049, 1153, 1277, 1399, 1523, 1667, 1811,
      1949, 2111, 2269, 2437, 2609, 2789, 2969, 3169 },
    { 0, 2, 5, 7, 8, 11, 13, 15 },
    { 1, 3, 4, 6, 9, 10, 12, 14 },
    { 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f },
    0.80f, 0.80f,    // input diffusion: maximum
    1.0f,            // output diffusion scale
    8000.0f,         // bandwidth: soft input
    0.0f, 1.0f,      // ER: forced off
    1.0f,            // late gain
    1.5f, 1.3f,      // mod: heavy depth and rate
    1.1f, 1.2f,      // damping: extended treble and bass sustain
    0.5f, 1.5f       // size range
};

// ---------------------------------------------------------------------------
static constexpr int kNumAlgorithms = 5;

inline const AlgorithmConfig& getAlgorithmConfig (int index)
{
    static constexpr const AlgorithmConfig* kAlgorithms[kNumAlgorithms] = {
        &kPlate, &kHall, &kChamber, &kRoom, &kAmbient
    };
    if (index < 0 || index >= kNumAlgorithms)
        index = 1; // Fall back to Hall
    return *kAlgorithms[index];
}
