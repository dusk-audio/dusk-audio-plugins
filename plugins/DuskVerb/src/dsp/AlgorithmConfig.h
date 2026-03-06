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
    float trebleMultScaleMax; // Bright-end target for treble curve interpolation.
                               // At treble_multiply=1.0, scaledTreble interpolates to this value.
                               // >1.0 means no HF rolloff (flat, like VV at HighShelf=0).
    float bassMultScale;

    float sizeRangeMin;
    float sizeRangeMax;

    float erCrossfeed; // Fraction of ER output fed into FDN input (ValhallaRoom "Early Send")

    float inlineDiffusionCoeff; // Schroeder allpass gain inside FDN feedback (Dattorro "decay diffusion")

    float modDepthFloor; // Minimum modulation depth scaling for shortest delay (0.0-1.0)

    float structuralHFDampingHz; // First-order LP in FDN feedback modeling air absorption (Hz).
                                 // Applied after TwoBandDamping, before feedbackLP. 0 = bypassed.
                                 // Effective frequency scales with treble_multiply: effectiveHz = baseHz * (0.5 + treble * 0.5).
                                 // Per-algorithm: higher values = gentler damping. Typical: 14000-19000.

    float structuralLFDampingHz; // First-order HP in FDN feedback reducing bass RT60 inflation (Hz).
                                 // Applied after structural HF damping. 0 = bypassed.
                                 // Room uses ~200Hz to tame 1.3-1.6x bass overshoot.

    float feedbackLPHz;  // Butterworth LP in FDN feedback path (Hz). 0 = bypassed.
                         // Order set by feedbackLP4thOrder: 12 dB/oct (2nd) or 24 dB/oct (4th).

    bool feedbackLP4thOrder; // true = 4th-order Linkwitz-Riley (24 dB/oct), false = 2nd-order Butterworth (12 dB/oct).
                             // 4th-order doubles per-pass HF attenuation — needed for short-delay Room modes.
                             // L-R alignment avoids the resonance peak of Butterworth 4th-order.

    float noiseModDepth; // Per-sample random delay jitter (samples at 44.1kHz base rate).
                         // Complements the slow sinusoidal LFO with fast per-sample mode blurring.
                         // Higher values = more aggressive ringing suppression. 0 = off.

    float hadamardPerturbation; // Random perturbation of Hadamard matrix entries (0.0 = pure Hadamard).
                                // Breaks deterministic mode coupling. Range 0.0-0.15. 0 = off.

    float erGainExponent; // Exponent for ER distance-attenuation law.
                          // 1.0 = inverse distance (default — loud early, quiet late),
                          // 0.5 = sqrt rolloff (gentler — more even energy spread),
                          // 0.0 = flat (all taps equal level).

    bool useDattorroTank; // true = use DattorroTank (cross-coupled allpass loops) instead of
                          // Hadamard FDN for late reverb. Better for Room: allpasses embedded
                          // in the feedback loop smear modal energy rather than concentrating it.

    float decayTimeScale; // Multiplier for the user's decay time parameter.
                          // Allows per-algorithm scaling of the effective decay range.
                          // 1.0 = pass through (default). Room uses 3.0 to extend
                          // the 0.2-30s UI range to 0.6-90s effective RT60.

    float dualSlopeRatio;     // Fast-group RT60 as fraction of effective RT60 (0 = disabled).
                              // Room uses 0.08 → fast RT60 = 4.4s when effective = 55s.

    int   dualSlopeFastCount; // Number of fast-decay channels (0 = disabled, 8 = half).
                              // Channels [0, fastCount) get the shorter RT60.

    float dualSlopeFastGain;  // Output tap gain multiplier for fast channels.
                              // Boosts fast channels in output sum to create loud initial burst.
                              // 3.5 = +11 dB boost per tap. 1.0 = unity (disabled).

    float shortDecayBoostDB;  // Max boost in dB at short effective decay times (0 = disabled).
                              // Compensates for lower energy density at low feedback coefficients.
                              // Applied as a linear ramp: full boost at decayTime=0, zero at knee.

    float shortDecayBoostKnee; // Effective decay time (seconds) at which short-decay boost reaches zero.
                               // Must be > 0 when shortDecayBoostDB > 0.

    float gateHoldMs;      // Gate hold time (ms). 0 = gate disabled.
                           // How long the reverb tail plays at full level before the gate closes.
    float gateReleaseMs;   // Gate release time (ms). Exponential fade-out after hold expires.
                           // Only active when gateHoldMs > 0.
};

// ---------------------------------------------------------------------------
// Plate: EMT 140 / Lexicon 224 character.
// Tight delay clustering (15-40ms), maximum diffusion, no ERs, bright.
// Level-matched to VintageVerb Plate; mild ringing suppression.
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
    0.47f,           // late gain: reduced from 0.52 to fix Large Plate +5.0 dB
    0.5f, 1.0f,      // mod: moderate depth for mode blurring, normal rate
    1.30f, 1.4f, 1.0f, // damping: trebleMultScale=1.30 (dark), trebleMultScaleMax=1.4 (bright), bassMultScale=1.0
    0.5f, 1.5f,      // size range
    0.0f,            // ER crossfeed: off (no ERs)
    0.10f,           // inline diffusion: mild density boost
    1.0f,            // mod depth floor: 1.0 = uniform modulation
    0.0f,            // structural HF damping: off (Plate HF already too fast; trebleMultScale=1.30 handles HF)
    0.0f,            // structural LF damping: off
    0.0f,            // feedback LP: off
    false,           // feedback LP 4th order: off
    0.9f,            // noise mod: mild jitter (provides nonlinear HF correction matching VV Plate; 0.7→0.9 to fix Short ringing)
    0.12f,           // Hadamard perturbation: break symmetry
    1.0f,            // ER gain exponent: inverse distance (default)
    false,           // useDattorroTank: off (FDN)
    1.0f,            // decay time scale: pass through
    0.0f, 0, 1.0f,  // dual-slope: disabled
    2.0f, 0.9f,     // short-decay boost: +2 dB at very short RT60, fading to 0 by effective 0.9s
    0.0f, 0.0f      // gate: disabled
};

// ---------------------------------------------------------------------------
// Hall: Lexicon 480L "Random Hall" / 224 "Concert Hall".
// Level-matched to VintageVerb Concert Hall; HF decay matched.
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
    0.65f,           // late gain: level-matched to VV Concert Hall (~-9.5 dB wet gain)
    1.0f, 1.0f,      // mod: full
    0.65f, 0.70f, 1.0f, // damping: trebleMultScale=0.65 (dark), trebleMultScaleMax=0.70 (bright), bassMultScale=1.0
    0.5f, 1.5f,      // size range
    0.15f,           // ER crossfeed: subtle
    0.0f,            // inline diffusion: off (preserve hall character)
    1.0f,            // mod depth floor: 1.0 = uniform modulation
    18000.0f,        // structural HF damping: 18kHz base, inverted treble-scaling (treble=1.0→18kHz, treble=0.5→22.5kHz less damping)
    0.0f,            // structural LF damping: off
    0.0f,            // feedback LP: off
    false,           // feedback LP 4th order: off
    0.0f,            // noise mod: off (preserve hall character)
    0.0f,            // Hadamard perturbation: off
    1.0f,            // ER gain exponent: inverse distance (default)
    false,           // useDattorroTank: off (FDN)
    1.0f,            // decay time scale: pass through
    0.0f, 0, 1.0f,  // dual-slope: disabled
    4.5f, 1.5f,     // short-decay boost: +4.5 dB at very short RT60, fading to 0 by effective 1.5s
    0.0f, 0.0f      // gate: disabled
};

// ---------------------------------------------------------------------------
// Chamber: Lexicon 480L "Rich Chamber" / AMS RMX16 "Ambience".
// Medium delay spread, slightly brighter than hall, moderate ER.
// Level-matched to VintageVerb Chamber; mild ringing suppression.
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
    0.45f,           // late gain: level-matched to VV Chamber (~-10.5 dB wet gain)
    0.8f, 1.0f,      // mod: increased depth for mode blurring, normal rate
    1.20f, 1.1f, 1.0f, // damping: trebleMultScale=1.20 (dark), trebleMultScaleMax=1.1 (bright), bassMultScale=1.0
    0.5f, 1.5f,      // size range
    0.2f,            // ER crossfeed: medium
    0.10f,           // inline diffusion: mild density boost
    1.0f,            // mod depth floor: 1.0 = uniform modulation
    0.0f,            // structural HF damping: off (Chamber HF already too fast; trebleMultScale=1.20 handles HF)
    0.0f,            // structural LF damping: off
    0.0f,            // feedback LP: off
    false,           // feedback LP 4th order: off
    1.5f,            // noise mod: mild jitter for ringing suppression
    0.10f,           // Hadamard perturbation: break symmetry
    1.0f,            // ER gain exponent: inverse distance (default)
    false,           // useDattorroTank: off (FDN)
    1.0f,            // decay time scale: pass through
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled (Chamber has static lateGainScale tuning)
    0.0f, 0.0f      // gate: disabled
};

// ---------------------------------------------------------------------------
// Room: Clone of VintageVerb Room (0.500).
// Long-sustaining reverb with moderate ER character. Unlike Ambient (pure wash),
// Room retains some sense of enclosed space via early reflections.
static constexpr AlgorithmConfig kRoom = {
    "Room",
    { 1087, 1171, 1279, 1381, 1493, 1607, 1733, 1861,
      2003, 2153, 2309, 2467, 2633, 2801, 2963, 3137 },
    { 0, 3, 4, 7, 9, 10, 13, 15 },
    { 1, 2, 5, 6, 8, 11, 12, 14 },
    { 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    0.75f, 0.70f,    // input diffusion: moderate-high
    1.0f,            // output diffusion scale
    5000.0f,         // bandwidth: dark input to match VV Room's extreme HF rolloff
    0.5f, 0.90f,     // ER: moderate level, slightly tighter timing
    0.70f,           // late gain: calibrated to VV Room preset suite (avg level +4.7 at 0.90 → +2.5 at 0.70)
    1.0f, 1.0f,      // mod: neutral (calibrate from comparison)
    0.45f, 0.85f, 0.85f, // damping: trebleMultScale=0.45 (dark), trebleMultScaleMax=0.85 (bright), bassMultScale=0.85
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed: light
    0.0f,            // inline diffusion: off (long delays = sufficient density)
    1.0f,            // mod depth floor: uniform
    0.0f,            // structural HF damping: off (already very dark, trebleMultScale=0.45)
    40.0f,           // structural LF damping: 40Hz HP to gently tame bass RT60 inflation
    0.0f,            // feedback LP: off (trebleMultScale handles HF decay)
    false,           // feedback LP 4th order: off
    0.0f,            // noise mod: off (VV Room uses coherent sinusoidal LFO, not random jitter)
    0.08f,           // Hadamard perturbation: mild symmetry breaking
    0.75f,           // ER gain exponent: moderate rolloff
    false,           // useDattorroTank: off (FDN)
    3.0f,            // decay time scale: 3x (UI 0.2-30s → effective 0.6-90s; allows short RT60 matching)
    0.0f, 0, 1.0f,   // dual-slope: disabled (standard 16-channel FDN for matched tail energy)
    0.0f, 0.0f,      // short-decay boost: disabled (Room uses lateGainScale calibration)
    0.0f, 0.0f       // gate: disabled
};

// ---------------------------------------------------------------------------
// Ambient: Lexicon PCM96 "Infinite" / Strymon BigSky "Cloud".
// Widest delay spread, max diffusion, heavy modulation, no ERs.
// Level-matched to VintageVerb Room/Chorus Space; HF decay matched.
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
    0.60f,           // late gain: preset-suite avg was +1.9 dB at 0.70; 0.60 reduces ~1.3 dB
    1.5f, 1.3f,      // mod: heavy depth and rate
    0.60f, 1.0f, 1.0f, // damping: trebleMultScale=0.60 (dark), trebleMultScaleMax=1.0 (bright), bassMultScale=1.0
    0.5f, 1.5f,      // size range
    0.0f,            // ER crossfeed: off (no ERs)
    0.0f,            // inline diffusion: off (preserve ambient character)
    1.0f,            // mod depth floor: 1.0 = uniform modulation
    0.0f,            // structural HF damping: off (Ambient HF slightly fast; trebleMultScale=0.60 handles HF)
    0.0f,            // structural LF damping: off
    0.0f,            // feedback LP: off
    false,           // feedback LP 4th order: off
    0.0f,            // noise mod: off (preserve ambient character)
    0.0f,            // Hadamard perturbation: off
    1.0f,            // ER gain exponent: inverse distance (default)
    false,           // useDattorroTank: off (FDN)
    1.0f,            // decay time scale: pass through
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled (Ambient already matched)
    0.0f, 0.0f      // gate: disabled
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
