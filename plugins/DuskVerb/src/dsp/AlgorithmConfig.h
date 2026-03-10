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
                           // ~0.25 matches VintageVerb's -9.4dB L→R coupling.
                           // Negative values disable stereo split (use full 16×16 Hadamard).

    float outputLowShelfDB;   // Low shelf EQ at 250Hz (dB). Negative = cut bass. 0 = bypassed.
    float outputHighShelfDB;  // High shelf EQ (dB). Positive = boost presence. 0 = bypassed.
    float outputHighShelfHz;  // High shelf frequency (Hz). Per-algorithm tunable.

    float outputMidEQHz;      // Mid parametric EQ center frequency (Hz). 0 = bypassed.
    float outputMidEQDB;      // Mid parametric EQ gain (dB). Negative = cut.
    float outputMidEQQ;       // Mid parametric EQ Q factor. Higher = narrower. 0.7 = gentle, 2.0 = surgical.
};

// ---------------------------------------------------------------------------
// Plate: Classic metal plate reverb character.
// Tight delay clustering (15-40ms), maximum diffusion, no ERs, bright.
static constexpr AlgorithmConfig kPlate = {
    "Plate",
    { 661, 709, 743, 787, 811, 853, 883, 919,
      947, 983, 1021, 1063, 1097, 1151, 1201, 1249 },
    { 0, 2, 5, 7, 9, 11, 13, 15 },
    { 1, 3, 4, 6, 8, 10, 12, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.58f, 0.58f,   // input diffusion: matched to VV early diffusion
    0.75f,           // output diffusion scale: reduced for crisper transient output
    20000.0f,        // bandwidth: full spectrum input (no HF attenuation before FDN)
    0.80f, 0.70f,    // ER: level=0.80, timeScale=0.70
    0.35f,           // late gain: 0.35
    0.25f, 1.0f,     // mod: reduced depth to match VV FM width (was 0.5 — 2.6x too wide), normal rate
    0.60f, 1.00f, 1.0f, // damping: trebleMultScaleMax=1.00 (0.70 overdamped decay_shape 40.2; use feedbackLP for HF control instead)
    20000.0f,        // high crossover: 20kHz (two-band — Plate trebleMult>1.0 needs uniform bright boost across all HF)
    1.0f,            // airDampingScale: 1.0 (no extra air damping — Plate needs full brightness)
    0.5f, 1.5f,      // size range
    0.0f,            // ER crossfeed: off
    0.0f,            // inline diffusion: off (preserve discrete early echoes for punch)
    1.0f,            // mod depth floor: 1.0 = uniform modulation
    0.0f,            // structural HF damping: off (Plate HF already too fast; trebleMultScale=1.30 handles HF)
    0.0f,            // structural LF damping: off
    9000.0f,         // feedback LP: 9kHz (8.5kHz overdarkened centroid_late 97→76.1)
    false,           // feedback LP 4th order: off (2nd order for gentler slope)
    0.3f,            // noise mod: 0.3
    0.12f,           // Hadamard perturbation: break symmetry
    1.0f,            // ER gain exponent: inverse distance (default)
    false,           // useDattorroTank: off (FDN)
    1.40f,           // decay time scale: 1.40 (1.20 wasn't enough — T30 still 1.386 vs VV 2.045)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    2.0f, 0.9f,     // short-decay boost: +2 dB at very short RT60, fading to 0 by effective 0.9s
    0.0f, 0.0f,     // gate: disabled
    12000.0f,        // ER air absorption ceiling: default
    2000.0f,         // ER air absorption floor: default
    0.0f,            // ER decorrelation: off (no ERs in Plate)
    1.00f,           // output gain: 1.00 (was 0.75; signal_level 72.5)
    0.20f,           // stereo coupling: 0.20 (was 0.35; DV +0.1dB vs VV -2.0dB — too mono)
    0.0f, 0.0f, 2500.0f, // output EQ: high shelf bypassed (was -1.5dB; darkened ERs too much — centroid_early 74.3)
    0.0f, 0.0f, 0.7f    // output mid EQ: bypassed
};

// ---------------------------------------------------------------------------
// Hall: Large concert hall reverb.
// Wide delay spread, moderate diffusion, full early reflections.
static constexpr AlgorithmConfig kHall = {
    "Hall",
    // delay lengths: 1801-5521 samples (41-125ms @ 44.1kHz) — doubled to match VV Concert Hall range (48-130ms)
    { 1801, 1949, 2111, 2293, 2503, 2741, 2999, 3251,
      3511, 3767, 4027, 4297, 4603, 4909, 5231, 5521 },
    { 0, 1, 2, 3, 4, 5, 6, 7 },       // leftTaps: L group (stereo split)
    { 8, 9, 10, 11, 12, 13, 14, 15 },  // rightTaps: R group (stereo split)
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },  // alternating signs for decorrelation
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion: 0.55/0.45 (was 0.75/0.625 — 50ms density 5300 vs VV 2450, need sparser early onset)
    0.5f,            // output diffusion: 0.5
    20000.0f,        // bandwidth: full spectrum input
    0.90f, 0.85f,    // ER: level=0.90, timeScale=0.85 (0.80 crashed C80 to 58.4)
    0.22f,           // late gain: 0.22 (0.25 crashed C80 to 52.5)
    2.0f, 1.0f,      // mod: modDepthScale=2.0 (3.0 dropped spectral_tilt 73.8→63.9, modulation still 0)
    0.50f, 1.50f, 1.0f, // damping: bassMultScale=1.0 (0.90 gains can't be compensated without crashing centroid_late)
    4000.0f,         // high crossover: 4kHz (3kHz regressed — pushed too much into air band, kurtosis diverged)
    0.70f,           // airDampingScale: 0.70 (0.60 dropped spectral_tilt 73.8→65.0)
    0.5f, 1.5f,      // size range
    0.25f,           // ER crossfeed: 0.25 (0.22 dropped MFCC/EDT — keep at 0.25)
    0.0f,            // inline diffusion: off (even 0.05 wrecked centroid_late 97.6→69.9, spectral_tilt 95.4→58.8)
    1.0f,            // mod depth floor: 1.0 = uniform modulation
    0.0f,            // structural HF damping: off (feedbackLP at 15kHz handles extreme HF instead)
    40.0f,           // structural LF damping: 40Hz HP (90Hz destroyed bass ratio and raised centroid — output EQ handles 80-250Hz instead)
    9500.0f,         // feedback LP: 9.5kHz 4th order (10kHz dropped centroid_late to 89.6, spectral_tilt to 69.4)
    true,            // feedback LP 4th order: on
    0.35f,           // noise mod: 0.35 (0.50 crashed MFCC/EDT — keep at 0.35)
    0.10f,           // Hadamard perturbation: 0.10 (0.05 wasn't enough — still audible repetition in tail)
    1.0f,            // ER gain exponent: 1.0 inverse distance (1.5 caused audible pulse after transients)
    false,           // useDattorroTank: off (FDN)
    1.50f,           // decay time scale: 1.50 (baseline)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    2.0f, 1.5f,     // short-decay boost: +2.0 dB (4.5 contributed to transient pulse)
    0.0f, 0.0f,     // gate: disabled
    20000.0f,        // ER air absorption ceiling: 20kHz (earliest taps: no filtering)
    18000.0f,        // ER air absorption floor: 18kHz
    0.55f,           // ER decorrelation: strong allpass decorrelation (IACC 0.747→target 0.321)
    3.0f,            // output gain: 3.0 (best total: 74.6, MFCC 64.5; signal_level 78.1 at 1% weight is acceptable)
    -1.0f,           // stereo coupling: -1.0 = full 16×16 Hadamard (was 0.15 split 8+8; coupling -21.6 dB vs VV -1.7 dB)
    -1.5f,           // output low shelf: -1.5dB at 250Hz (restored)
    -5.0f,           // output high shelf: -5dB at 1.1kHz (locked — -3dB@1.5kHz crashed centroid_late to 62.1)
    1100.0f,         // output high shelf freq: 1.1kHz (locked)
    630.0f,          // output mid EQ: 630Hz center
    -3.0f,           // output mid EQ: -3dB cut
    0.8f             // output mid EQ: Q=0.8
};

// ---------------------------------------------------------------------------
// Chamber: Rich chamber reverb.
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
    1.2f, 0.55f,     // ER: boosted level, very tight timing for C50
    0.50f,           // late gain: slightly boosted for EDT
    0.8f, 1.0f,      // mod: increased depth for mode blurring, normal rate
    0.90f, 0.90f, 1.0f, // damping: trebleMultScale=0.90, trebleMultScaleMax=0.90, bassMultScale=1.0
    20000.0f,        // high crossover: 20kHz (effectively two-band — mid and air bands unified)
    1.0f,            // airDampingScale: 1.0 (no extra air damping)
    0.5f, 1.5f,      // size range
    0.2f,            // ER crossfeed: medium
    0.30f,           // inline diffusion: moderate density for smoother spectral envelope
    1.0f,            // mod depth floor: 1.0 = uniform modulation
    0.0f,            // structural HF damping: off
    0.0f,            // structural LF damping: off
    12000.0f,        // feedback LP: 12kHz
    false,           // feedback LP 4th order: off
    1.5f,            // noise mod: mild jitter
    0.10f,           // Hadamard perturbation: break symmetry
    1.0f,            // ER gain exponent: inverse distance (default)
    false,           // useDattorroTank: off (FDN)
    1.0f,            // decay time scale: pass through
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled (Chamber has static lateGainScale tuning)
    0.0f, 0.0f,     // gate: disabled
    12000.0f,        // ER air absorption ceiling: default
    2000.0f,         // ER air absorption floor: default
    0.50f,           // ER decorrelation: strong (reduce early IACC from 0.617 toward 0.241)
    1.0f,            // output gain: unity
    0.15f,           // stereo coupling: moderate (wider stereo for IACC match)
    0.0f, 0.0f, 3000.0f, // output EQ: bypassed
    0.0f, 0.0f, 0.7f    // output mid EQ: bypassed
};

// ---------------------------------------------------------------------------
// Room: Sustaining room reverb with spatial character.
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
    18000.0f,        // bandwidth: wide input (structural HF damping handles per-loop rolloff)
    1.4f, 0.45f,     // ER: boosted level + tighter timing for C50
    0.55f,           // late gain: reduced for C50 ratio (more ER, less late)
    1.3f, 1.0f,      // mod: wider stereo (modDepthScale=1.3, modRateScale=1.0)
    0.45f, 0.75f, 0.85f, // damping: trebleMultScale=0.45 (dark), trebleMultScaleMax=0.75 (bright, was 0.95), bassMultScale=0.85
    6000.0f,         // high crossover: 6kHz (three-band: bass/mid/air — independent air band control)
    0.75f,           // airDampingScale: 0.75 (Room — faster air decay for natural room character)
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed: light
    0.0f,            // inline diffusion: off (0.20 crashed MFCC 56→17)
    1.0f,            // mod depth floor: uniform
    0.0f,            // structural HF damping: off (trebleMultScaleMax=0.75 handles HF decay)
    40.0f,           // structural LF damping: 40Hz HP to gently tame bass RT60 inflation
    9500.0f,         // feedback LP: 9.5kHz (balance late tail darkness — 7kHz too aggressive, target VV centroid ~1894Hz)
    false,           // feedback LP 4th order: off
    0.0f,            // noise mod: off (2.0 was 4x too aggressive, crashed MFCC)
    0.08f,           // Hadamard perturbation: mild symmetry breaking
    0.30f,           // ER gain exponent: gentle rolloff (more energy in later taps)
    false,           // useDattorroTank: off (FDN)
    1.4f,            // decay time scale: 1.4x to match reference RT60 at equivalent knob positions
    0.0f, 0, 1.0f,   // dual-slope: disabled (standard 16-channel FDN for matched tail energy)
    0.0f, 0.0f,      // short-decay boost: disabled (Room uses lateGainScale calibration)
    0.0f, 0.0f,      // gate: disabled
    10000.0f,         // ER air absorption ceiling: darken earliest taps slightly (centroid_early 6207→5521)
    2000.0f,          // ER air absorption floor: default
    0.35f,            // ER decorrelation: moderate (improve L/R spectral variation for MFCC)
    1.0f,             // output gain: unity
    0.20f,            // stereo coupling: moderate (rooms have natural coupling from walls)
    0.0f, 0.0f, 3000.0f, // output EQ: bypassed (output EQ consistently hurts MFCC/spectral_tilt)
    0.0f, 0.0f, 0.7f    // output mid EQ: bypassed
};

// ---------------------------------------------------------------------------
// Ambient: Infinite-style wash reverb.
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
    18000.0f,        // bandwidth: wide input (let full spectrum in for bright Ambient)
    0.0f, 1.0f,      // ER: forced off
    0.55f,           // late gain: calibrated (0.65 hurt centroid_late and MFCC)
    1.5f, 1.3f,      // mod: heavy depth and rate
    0.80f, 1.0f, 1.0f, // damping: trebleMultScale=0.80 (brighter), trebleMultScaleMax=1.0, bassMultScale=1.0
    20000.0f,        // high crossover: 20kHz (effectively two-band — mid and air bands unified)
    1.0f,            // airDampingScale: 1.0 (no extra air damping)
    0.5f, 1.5f,      // size range
    0.0f,            // ER crossfeed: off (no ERs)
    0.0f,            // inline diffusion: off (preserve ambient character)
    1.0f,            // mod depth floor: 1.0 = uniform modulation
    0.0f,            // structural HF damping: off (Ambient HF slightly fast; trebleMultScale=0.60 handles HF)
    0.0f,            // structural LF damping: off
    0.0f,            // feedback LP: off (12kHz overcorrected centroid_late 2708→1798, target 2322)
    false,           // feedback LP 4th order: off
    0.0f,            // noise mod: off (preserve ambient character)
    0.0f,            // Hadamard perturbation: off
    1.0f,            // ER gain exponent: inverse distance (default)
    false,           // useDattorroTank: off (FDN)
    1.0f,            // decay time scale: pass through
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled (Ambient already matched)
    0.0f, 0.0f,     // gate: disabled
    12000.0f,        // ER air absorption ceiling: default
    2000.0f,         // ER air absorption floor: default (no ERs in Ambient)
    0.0f,            // ER decorrelation: off
    1.0f,            // output gain: unity
    0.05f,           // stereo coupling: minimal (0.0 didn't help width enough)
    0.0f, 0.0f, 3000.0f, // output EQ: bypassed
    0.0f, 0.0f, 0.7f    // output mid EQ: bypassed
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
