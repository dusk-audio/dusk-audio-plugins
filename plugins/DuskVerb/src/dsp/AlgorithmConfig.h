#pragma once

// Per-algorithm custom early reflection tap (mono-panned).
// Each tap outputs to L only or R only (never both).
struct CustomERTap
{
    float delayMs;     // Tap delay in milliseconds
    float amplitude;   // Linear amplitude (typically 0.003-0.025)
    int   channel;     // 0 = left only, 1 = right only
};

// Maximum number of custom ER taps per algorithm.
static constexpr int kMaxCustomERTaps = 48;

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
                               // >1.0 means no HF rolloff (flat, as if high shelf is fully open).
    float bassMultScale;

    float highCrossoverHz; // Second crossover for ThreeBandDamping (Hz).
                           // Splits "treble" into mid (lowCrossover..highCrossover) and air (>highCrossover).
                           // Room=6000 (independent air band control). Others=20000 (effectively two-band).

    float airDampingScale; // Independent air band (>highCrossover) damping multiplier.
                           // Applied directly as gHigh = gBase^(1/airDampingScale).
                           // Lower = faster air decay (darker tail). 1.0 = same as base decay.
                           // Independent of user's treble knob. Per-algorithm tunable.

    float sizeRangeMin;
    float sizeRangeMax;

    float erCrossfeed; // Fraction of ER output fed into FDN input ("Early Send")

    float inlineDiffusionCoeff; // Schroeder allpass gain inside FDN feedback (Dattorro "decay diffusion")

    float modDepthFloor; // Minimum modulation depth scaling for shortest delay (0.0-1.0)

    float structuralHFDampingHz; // First-order LP in FDN feedback modeling air absorption (Hz).
                                 // Applied after ThreeBandDamping in feedback loop. 0 = bypassed.
                                 // Effective frequency scales with treble_multiply: effectiveHz = baseHz * (0.5 + treble * 0.5).
                                 // Per-algorithm: higher values = gentler damping. Typical: 14000-19000.

    float structuralLFDampingHz; // First-order HP in FDN feedback reducing bass RT60 inflation (Hz).
                                 // Applied after structural HF damping. 0 = bypassed.
                                 // Room uses ~200Hz to tame 1.3-1.6x bass overshoot.

    float noiseModDepth; // Per-sample random delay jitter (samples at 44.1kHz base rate).
                         // Complements the slow sinusoidal LFO with fast per-sample mode blurring.
                         // Higher values = more aggressive ringing suppression. 0 = off.

    float hadamardPerturbation; // Random perturbation of Hadamard matrix entries (0.0 = pure Hadamard).
                                // Breaks deterministic mode coupling. Range 0.0-0.15. 0 = off.

    float erGainExponent; // Exponent for ER distance-attenuation law.
                          // 1.0 = inverse distance (default — loud early, quiet late),
                          // 0.5 = sqrt rolloff (gentler — more even energy spread),
                          // 0.0 = flat (all taps equal level).

    bool useWeightedGains;  // true = use delay-length-weighted input/output gains for uniform
                            // modal excitation. Weights by 1/sqrt(delay/min_delay) so shorter
                            // delays get higher gain, flattening the spectral envelope.

    bool useHouseholderFeedback; // true = use Householder reflection matrix instead of Hadamard
                                 // for FDN feedback mixing. Householder provides moderate inter-channel
                                 // mixing (H = I - 2/N * ones*ones^T) which avoids the eigentone
                                 // clustering that maximum-mixing Hadamard causes. Recommended for
                                 // plate algorithms where spectral smoothness matters most.

    bool useDattorroTank; // true = use DattorroTank (cross-coupled allpass loops) instead of
                          // Hadamard FDN for late reverb. Better for Room: allpasses embedded
                          // in the feedback loop smear modal energy rather than concentrating it.

    float decayTimeScale; // Multiplier for the user's decay time parameter.
                          // Allows per-algorithm scaling of the effective decay range.
                          // 1.0 = pass through (default, all algorithms currently use 1.0).

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

    float erAirAbsorptionCeilingHz; // Ceiling frequency for ER air absorption LP sweep (Hz).
                                     // Per-tap LP sweeps from this value (earliest tap) down to floor (latest).
                                     // 12000 = default, 18000 = brighter early taps.

    float erAirAbsorptionFloorHz; // Floor frequency for ER air absorption LP sweep (Hz).
                                   // 2000 = aggressive darkening (default), 6000 = brighter late taps.

    float erDecorrCoeff; // Post-tap Schroeder allpass decorrelation coefficient (0.0-0.7).
                          // Two cascaded allpasses per channel with different prime delays
                          // create phase differences for stereo widening.
                          // 0.0 = bypassed (default), 0.55 = strong decorrelation.

    float outputGain;      // Per-algorithm output gain multiplier applied to combined ER+late signal
                           // before dry/wet crossfade. Compensates for differences in internal gain
                           // structure vs reference reverbs. 1.0 = unity (default).

    float stereoCoupling;  // Stereo architecture: splits 16 FDN channels into L (0-7) and R (8-15)
                           // groups with two independent 8×8 Hadamard transforms.
                           // 0.0 = fully independent L/R (widest stereo), 0.5 = fully mixed (mono).
                           // ~0.25 gives moderate L→R coupling (-9.4dB).
                           // Negative values disable stereo split (use full 16×16 Hadamard).

    float outputLowShelfDB;   // Low shelf EQ at 250Hz (dB). Negative = cut bass. 0 = bypassed.
    float outputHighShelfDB;  // High shelf EQ (dB). Positive = boost presence. 0 = bypassed.
    float outputHighShelfHz;  // High shelf frequency (Hz). Per-algorithm tunable.

    float outputMidEQHz;      // Mid parametric EQ center frequency (Hz). 0 = bypassed.
    float outputMidEQDB;      // Mid parametric EQ gain (dB). Negative = cut.
    float outputMidEQQ;       // Mid parametric EQ Q factor. Higher = narrower. 0.7 = gentle, 2.0 = surgical.

    // Custom ER tap table (mono-panned).
    // When numCustomERTaps > 0, EarlyReflections uses this fixed table instead of
    // computing taps from the exponential formula. Each tap goes to L or R only.
    int numCustomERTaps = 0;
    const CustomERTap* customERTaps = nullptr;
};

// ---------------------------------------------------------------------------
// Plate: Classic metal plate reverb character.
// Tight delay clustering (15-40ms), maximum diffusion, no ERs, bright.
static constexpr AlgorithmConfig kPlate = {
    "Plate",
    { 661, 691, 719, 751, 787, 821, 853, 887,
      929, 967, 1009, 1051, 1097, 1151, 1201, 1249 },  // logarithmic prime spacing for uniform modal density
    { 0, 1, 2, 6, 7, 13, 14, 15 },   // balanced partition (sum=7412, diff=0)
    { 3, 4, 5, 8, 9, 10, 11, 12 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.65f, 0.65f,   // input diffusion: 0.65
    0.50f,           // output diffusion scale (was 0.75; reduced for cleaner tail density)
    20000.0f,        // bandwidth: full spectrum input (no HF attenuation before FDN)
    0.90f, 0.70f,    // ER: level=0.90, timeScale=0.70
    0.20f,           // late gain: 0.20 (was 0.30; reduced for output level calibration)
    0.10f, 1.0f,     // mod: 0.10 (was 0.25; reduce spectral smearing for MFCC 36→51)
    0.65f, 1.00f, 1.0f, // damping: trebleMultScale=0.65
    20000.0f,        // high crossover: 20kHz (two-band — Plate trebleMult>1.0 needs uniform bright boost across all HF)
    1.0f,            // airDampingScale: 1.0 (no extra air damping — Plate needs full brightness)
    0.5f, 1.5f,      // size range
    0.35f,           // ER crossfeed: 0.35
    0.03f,           // inline diffusion: 0.03 (minimal allpass phase smearing; higher values worsen small-size presets like Ambience Plate)
    1.0f,            // mod depth floor: 1.0 = uniform modulation
    0.0f,            // structural HF damping: off
    0.0f,            // structural LF damping: off
    0.03f,           // noise mod: 0.03 (was 0.15; reduce delay jitter for MFCC 36→51)
    0.12f,           // Hadamard perturbation: break symmetry
    1.0f,            // ER gain exponent: inverse distance (default)
    false,           // useWeightedGains: off
    true,            // useHouseholderFeedback: on (Householder 68.4 vs Hadamard 66)
    false,           // useDattorroTank: off (FDN)
    0.94f,           // decay time scale: calibrated via calibrate_decay_scale.py (was 1.20)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    2.0f, 0.9f,     // short-decay boost: +2 dB
    0.0f, 0.0f,     // gate: disabled
    18000.0f,        // ER air absorption ceiling: 18kHz
    2000.0f,         // ER air absorption floor: default
    0.0f,            // ER decorrelation: off
    1.50f,           // output gain: 1.50 (was 1.40; +0.6dB to compensate for inline diffusion energy loss)
    0.20f,           // stereo coupling: 0.20
    0.0f, -2.5f, 3000.0f, // output EQ: -2.5dB high shelf at 3kHz (tame 3-6kHz brightness)
    0.0f, 0.0f, 0.7f,   // output mid EQ: bypassed
    0, nullptr,  // custom ER: disabled
};

// ---------------------------------------------------------------------------
// Hall: Large concert hall reverb.
// Wide delay spread, moderate diffusion, full early reflections.
static constexpr AlgorithmConfig kHall = {
    "Hall",
    // delay lengths: 1801-5521 samples (41-125ms @ 44.1kHz) — wide spread for concert hall character
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },  // logarithmic prime spacing for uniform modal density
    { 0, 3, 5, 7, 8, 10, 12, 15 },     // leftTaps: balanced partition (sum=26710, diff=52 samples ≈ 1ms)
    { 1, 2, 4, 6, 9, 11, 13, 14 },     // rightTaps: balanced partition (sum=26762)
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },  // alternating signs for decorrelation
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion: 0.55/0.45 (was 0.75/0.625 — reduced for sparser early onset)
    0.35f,           // output diffusion scale (was 0.50; reduced for cleaner tail density)
    20000.0f,        // bandwidth: full spectrum input
    0.90f, 0.85f,    // ER: level=0.90, timeScale=0.85 (0.95 made C50 worse at 42.1)
    0.22f,           // late gain: 0.22
    1.0f, 1.0f,      // mod: modDepthScale=1.0 (was 2.0; reduced to improve MFCC while retaining LFO character)
    0.50f, 1.50f, 1.0f, // damping: trebleMultScale=0.50
    4000.0f,         // high crossover: 4kHz (3kHz regressed — pushed too much into air band, kurtosis diverged)
    0.70f,           // airDampingScale: 0.70
    0.5f, 1.5f,      // size range
    0.25f,           // ER crossfeed: 0.25 (0.22 dropped MFCC/EDT — keep at 0.25)
    0.0f,            // inline diffusion: off (short allpasses tested at 0.20-0.50 — no measurable effect on peaks)
                     // Long allpasses (41-131) wrecked centroid_late. Multi-point output tapping handles density.
    1.0f,            // mod depth floor: 1.0 = uniform modulation
    0.0f,            // structural HF damping: off
    40.0f,           // structural LF damping: 40Hz HP (90Hz destroyed bass ratio and raised centroid — output EQ handles 80-250Hz instead)
    0.10f,           // noise mod: 0.10 (was 0.35; reducing delay jitter improves MFCC consistency)
    0.10f,           // Hadamard perturbation: 0.10 (0.05 wasn't enough — still audible repetition in tail)
    1.0f,            // ER gain exponent: 1.0 inverse distance (1.5 caused audible pulse after transients)
    false,           // useWeightedGains: off (true regressed decay_shape 100→74.4, total 75.4→73.1)
    true,            // useHouseholderFeedback: on (keep true per instructions)
    false,           // useDattorroTank: off (FDN with multi-point taps outperforms Dattorro 3.1x vs 4.6x)
    0.79f,           // decay time scale: calibrated via calibrate_decay_scale.py (was 1.25)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    2.0f, 1.5f,     // short-decay boost: +2.0 dB (4.5 contributed to transient pulse)
    0.0f, 0.0f,     // gate: disabled
    20000.0f,        // ER air absorption ceiling: 20kHz (earliest taps: no filtering)
    18000.0f,        // ER air absorption floor: 18kHz
    0.55f,           // ER decorrelation: strong allpass decorrelation (IACC 0.747→target 0.321)
    1.9f,            // output gain: 1.9 (was 2.4; -2dB for output level calibration)
    -1.0f,           // stereo coupling: -1.0 = full 16×16 Hadamard (was 0.15 split 8+8)
    -1.5f,           // output low shelf: -1.5dB at 250Hz
    -3.5f,           // output high shelf: -3.5dB at 4kHz (was -5dB@1.1kHz; 1.1kHz killed mid-range on dark presets)
    4000.0f,         // output high shelf freq: 4kHz (was 1.1kHz)
    0.0f,            // output mid EQ: bypassed
    0.0f,            // output mid EQ: bypassed
    0.7f,            // output mid EQ: Q (bypassed)
    0, nullptr,  // custom ER: disabled
};

// ---------------------------------------------------------------------------
// Chamber: Rich chamber reverb.
// Medium delay spread, slightly brighter than hall, moderate ER.
static constexpr AlgorithmConfig kChamber = {
    "Chamber",
    { 751, 797, 839, 887, 947, 997, 1061, 1123,
      1187, 1259, 1327, 1409, 1493, 1583, 1669, 1777 },  // logarithmic prime spacing for uniform modal density
    { 0, 1, 2, 7, 8, 11, 14, 15 },   // balanced partition (sum=9552, diff=2)
    { 3, 4, 5, 6, 9, 10, 12, 13 },
    { 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f },
    0.75f, 0.625f,   // input diffusion: Dattorro split
    0.65f,           // output diffusion scale (was 1.0; reduced for cleaner tail density)
    16000.0f,        // bandwidth: 16kHz
    1.2f, 0.55f,     // ER: level=1.2 (R7: boost ER for C50 recovery), tight timing
    0.38f,           // late gain: 0.38 (R7: reduce late for C50 — was 0.42)
    0.05f, 1.0f,     // mod: 0.05 (was 0.4; reducing modulation was the single biggest MFCC/spectral_eq win)
    0.90f, 0.90f, 1.0f, // damping: trebleMultScale=0.90 (R5 revert: 1.0 crashed T30/decay_shape/MFCC)
    20000.0f,        // high crossover: 20kHz (effectively two-band — mid and air bands unified)
    1.0f,            // airDampingScale: 1.0 (no extra air damping)
    0.5f, 1.5f,      // size range
    0.2f,            // ER crossfeed: medium
    0.20f,           // inline diffusion: 0.20 (was 0.30; slight improvement)
    1.0f,            // mod depth floor: 1.0 = uniform modulation
    0.0f,            // structural HF damping: off
    0.0f,            // structural LF damping: off
    0.05f,           // noise mod: 0.05 (was 0.5; reducing improved MFCC from 37→62)
    0.10f,           // Hadamard perturbation: break symmetry
    1.0f,            // ER gain exponent: inverse distance (default)
    false,           // useWeightedGains: off
    true,            // useHouseholderFeedback: on (Householder 65.0 vs Hadamard 63.4)
    false,           // useDattorroTank: off (FDN)
    0.99f,           // decay time scale: calibrated via calibrate_decay_scale.py (was 1.15)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled (Chamber has static lateGainScale tuning)
    0.0f, 0.0f,     // gate: disabled
    12000.0f,        // ER air absorption ceiling: default
    2000.0f,         // ER air absorption floor: default
    0.50f,           // ER decorrelation: strong (reduce early IACC from 0.617 toward 0.241)
    0.65f,           // output gain: 0.65
    0.15f,           // stereo coupling: moderate (wider stereo for IACC match)
    0.0f, -2.0f, 5000.0f, // output EQ: -2dB high shelf at 5kHz (tame 6.3-10kHz brightness)
    0.0f, 0.0f, 0.7f,   // output mid EQ: bypassed
    0, nullptr,  // custom ER: disabled
};

// ---------------------------------------------------------------------------
// Room: Sustaining room reverb with spatial character.
// Long-sustaining reverb with moderate ER character. Unlike Ambient (pure wash),
// Room retains some sense of enclosed space via early reflections.
static constexpr AlgorithmConfig kRoom = {
    "Room",
    { 1087, 1171, 1279, 1381, 1493, 1607, 1733, 1861,
      2003, 2153, 2309, 2467, 2633, 2801, 2963, 3137 },
    { 0, 1, 3, 7, 10, 11, 13, 14 },  // balanced partition (sum=16040, diff=2)
    { 2, 4, 5, 6, 8, 9, 12, 15 },
    { 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    0.75f, 0.70f,    // input diffusion: moderate-high
    0.65f,           // output diffusion scale (was 1.0; reduced for cleaner tail density)
    18000.0f,        // bandwidth: wide input (structural HF damping handles per-loop rolloff)
    1.4f, 0.45f,     // ER: boosted level + tighter timing for C50
    0.38f,           // late gain: 0.38 (was 0.55; reduced for output level calibration)
    1.3f, 1.0f,      // mod: wider stereo (modDepthScale=1.3, modRateScale=1.0)
    0.45f, 0.75f, 0.85f, // damping: trebleMultScale=0.45 (dark), trebleMultScaleMax=0.75 (bright, was 0.95), bassMultScale=0.85
    6000.0f,         // high crossover: 6kHz (three-band: bass/mid/air — independent air band control)
    0.75f,           // airDampingScale: 0.75 (Room — faster air decay for natural room character)
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed: light
    0.0f,            // inline diffusion: off (0.20 crashed MFCC 56→17)
    1.0f,            // mod depth floor: uniform
    0.0f,            // structural HF damping: off (14kHz tried, decay calibration compensated by raising feedback → worse ringing)
    40.0f,           // structural LF damping: 40Hz HP to gently tame bass RT60 inflation
    1.0f,            // noise mod: 1.0 (stronger jitter for Exciting Snare room which has ModDepth=0.144 — barely any LFO smearing)
    0.20f,           // Hadamard perturbation: 0.20 (was 0.08→0.12; stronger to detune 11kHz mode in Exciting Snare)
    0.30f,           // ER gain exponent: gentle rolloff (more energy in later taps)
    false,           // useWeightedGains: off
    false,           // useHouseholderFeedback: off (Room — Householder regressed 75.8 vs Hadamard 77.1)
    false,           // useDattorroTank: off (FDN)
    1.28f,           // decay time scale: calibrated via calibrate_decay_scale.py (was 1.30)
    0.0f, 0, 1.0f,   // dual-slope: disabled (standard 16-channel FDN for matched tail energy)
    0.0f, 0.0f,      // short-decay boost: disabled (Room uses lateGainScale calibration)
    0.0f, 0.0f,      // gate: disabled
    10000.0f,         // ER air absorption ceiling: darken earliest taps slightly (centroid_early 6207→5521)
    2000.0f,          // ER air absorption floor: default
    0.35f,            // ER decorrelation: moderate (improve L/R spectral variation for MFCC)
    0.71f,            // output gain: 0.71 (-3dB; calibrated for output level)
    0.20f,            // stereo coupling: moderate (rooms have natural coupling from walls)
    0.0f, -3.0f, 4000.0f, // output EQ: -3dB high shelf at 4kHz (tame 4-8kHz brightness)
    2200.0f, -2.0f, 1.2f, // output mid EQ: -2dB notch at 2.2kHz (suppress measured 2215Hz ringing)
    0, nullptr,  // custom ER: disabled
};

// ---------------------------------------------------------------------------
// Ambient: Infinite-style wash reverb.
// Widest delay spread, max diffusion, heavy modulation, no ERs.
static constexpr AlgorithmConfig kAmbient = {
    "Ambient",
    { 971, 1049, 1153, 1277, 1399, 1523, 1667, 1811,
      1949, 2111, 2269, 2437, 2609, 2789, 2969, 3169 },
    { 0, 1, 2, 4, 11, 12, 13, 15 },  // balanced partition (sum=15576, diff=0)
    { 3, 5, 6, 7, 8, 9, 10, 14 },
    { 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f },
    0.80f, 0.80f,    // input diffusion: maximum
    0.70f,           // output diffusion scale (was 1.0; reduced for cleaner tail density)
    18000.0f,        // bandwidth: wide input (let full spectrum in for bright Ambient)
    0.0f, 1.0f,      // ER: forced off
    0.35f,           // late gain: 0.35 (was 0.55; reduced for output level calibration)
    1.5f, 1.3f,      // mod: heavy depth and rate
    0.80f, 1.0f, 1.0f, // damping: trebleMultScale=0.80 (brighter), trebleMultScaleMax=1.0, bassMultScale=1.0
    20000.0f,        // high crossover: 20kHz (effectively two-band — mid and air bands unified)
    1.0f,            // airDampingScale: 1.0 (no extra air damping)
    0.5f, 1.5f,      // size range
    0.0f,            // ER crossfeed: off (no ERs)
    0.0f,            // inline diffusion: off (0.02 moved peak from 10kHz to 1kHz without reducing it)
    1.0f,            // mod depth floor: 1.0 = uniform modulation
    0.0f,            // structural HF damping: off (Ambient HF slightly fast; trebleMultScale=0.60 handles HF)
    0.0f,            // structural LF damping: off
    0.1f,            // noise mod: 0.1 (minimal jitter to smooth LFO phase effects on ringing measurement)
    0.0f,            // Hadamard perturbation: off (Ambient uses Householder)
    1.0f,            // ER gain exponent: inverse distance (default)
    false,           // useWeightedGains: off
    true,            // useHouseholderFeedback: on (Householder 82.4 vs Hadamard 79.4; more stable across presets)
    false,           // useDattorroTank: off (FDN)
    0.99f,           // decay time scale: calibrated via calibrate_decay_scale.py (was 1.20)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled (Ambient already matched)
    0.0f, 0.0f,     // gate: disabled
    12000.0f,        // ER air absorption ceiling: default
    2000.0f,         // ER air absorption floor: default (no ERs in Ambient)
    0.0f,            // ER decorrelation: off
    0.71f,           // output gain: 0.71 (-3dB; calibrated for output level)
    0.05f,           // stereo coupling: minimal (0.0 didn't help width enough)
    0.0f, -4.0f, 4000.0f, // output EQ: -4dB high shelf at 4kHz (tame brightness above 5kHz)
    0.0f, 0.0f, 0.7f,   // output mid EQ: bypassed
    0, nullptr,  // custom ER: disabled
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
