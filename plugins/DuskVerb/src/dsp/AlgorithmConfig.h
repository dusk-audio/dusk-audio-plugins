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

    bool useQuadTank;     // true = use QuadTank (4 cross-coupled allpass loops, 48 taps) instead
                          // of DattorroTank. Smoother onset, less peaky transients. Best for Hall.
                          // When true, useDattorroTank is ignored.

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

    bool enableSaturation;    // true = fastTanh soft clipper on output mix (adds warmth/harmonics).
                              // false = clean linear mix (transparent, matches VV's 0.01% THD).
                              // Hall/Plate/Chamber/Room: false (VV is clean).
                              // Could be true for "dirty" or "vintage" modes.

    float lateOnsetMs;           // Late reverb onset ramp time (ms). The FDN output is ramped from 0
                                  // to full over this duration after each new impulse/note, matching
                                  // VV's slower density buildup. 0 = no onset shaping (instant).

    float lateFeedForwardLevel;  // Mix level of pre-diffusion late reverb into output (0.0-1.0).
                                  // Bypasses output diffuser to put late energy into 20-50ms window.
                                  // 0.0 = all late reverb through diffuser (current behavior).
                                  // 0.3-0.5 = blend pre-diffusion signal for earlier late onset.

    float crestLimitRatio;       // Peak/RMS limiter: maximum instantaneous peak as multiple of 5ms RMS.
                                  // 0 = disabled. 3.0 = limit peaks to 3× RMS (~9.5dB crest).
                                  // Reduces FDN's inherent peak correlation from Hadamard mixing.
                                  // VV has ~9dB crest; FDN produces ~15dB. Setting 3.0-4.0 matches VV.

    // Custom ER tap table (mono-panned).
    // When numCustomERTaps > 0, EarlyReflections uses this fixed table instead of
    // computing taps from the exponential formula. Each tap goes to L or R only.
    int numCustomERTaps = 0;
    const CustomERTap* customERTaps = nullptr;

    // DattorroTank output tap positions (per-algorithm).
    // Format: {bufferIndex, positionFrac, sign} — 7 per channel.
    // Buffer indices: 0=leftDelay1, 1=leftDelay2, 2=leftAP2,
    //                 3=rightDelay1, 4=rightDelay2, 5=rightAP2
    // nullptr = use DattorroTank's built-in defaults (early taps).
    struct DattorroOutputTap { int buf; float pos; float sign; float gain = 1.0f; };
    static constexpr int kDattorroTapsPerChannel = 7;
    const DattorroOutputTap* dattorroLeftTaps = nullptr;
    const DattorroOutputTap* dattorroRightTaps = nullptr;

    float dattorroDelayScale = 1.0f;  // Multiplier for ALL DattorroTank base delays.
                                       // Controls loop length independently of user's Size knob.
                                       // Higher = longer loops = slower onset.
                                       // Hall needs ~3-4x to match VV's 200ms energy peak.

    float hybridBlend = 0.0f;       // Dual-engine blend: 0 = disabled (single engine).
                                     // >0 = blend primary engine output with secondary engine.
                                     // 0.35 = 65% primary + 35% secondary.
    int hybridSecondaryAlgo = -1;    // Algorithm index for secondary engine (-1 = disabled).
};

// DattorroTank output tap positions — shifted late in the loop to match VV's
// slow energy onset. VV shows zero energy in 0-20ms and gradual buildup through 200ms.
// Buffer indices: 0=leftDelay1, 1=leftDelay2, 2=leftAP2, 3=rightDelay1, 4=rightDelay2, 5=rightAP2

// Hall taps: very deep into delays (VV shows energy peak at 190-200ms)
// Hall uses hallScale so Delay1≈102ms. Taps at 0.90+ = 92ms+ into delay.
static constexpr AlgorithmConfig::DattorroOutputTap kHallDattorroLeftTaps[7] = {
    { 3, 0.88f,  1.0f },  // right delay1, very deep (≈90ms into 102ms delay)
    { 3, 0.97f,  1.0f },  // right delay1, near end
    { 5, 0.85f, -1.0f },  // right AP2, deep
    { 0, 0.92f,  1.0f },  // left delay1, very deep
    { 1, 0.90f, -1.0f },  // left delay2, deep
    { 2, 0.80f, -1.0f },  // left AP2, deep
    { 4, 0.88f, -1.0f },  // right delay2, deep
};
static constexpr AlgorithmConfig::DattorroOutputTap kHallDattorroRightTaps[7] = {
    { 0, 0.86f,  1.0f },  // left delay1, very deep
    { 0, 0.96f,  1.0f },  // left delay1, near end
    { 2, 0.83f, -1.0f },  // left AP2, deep
    { 3, 0.90f,  1.0f },  // right delay1, very deep
    { 4, 0.92f, -1.0f },  // right delay2, deep
    { 5, 0.82f, -1.0f },  // right AP2, deep
    { 1, 0.88f, -1.0f },  // left delay2, deep
};

// Plate taps: deep (Plate uses room-scale with size 0.3-0.8, so Delay1≈15-40ms)
// Taps at 0.75-0.95 = 11-38ms into delay
static constexpr AlgorithmConfig::DattorroOutputTap kPlateDattorroLeftTaps[7] = {
    { 3, 0.78f,  1.0f },  // right delay1, deep
    { 3, 0.94f,  1.0f },  // right delay1, very late
    { 5, 0.75f, -1.0f },  // right AP2, deep
    { 0, 0.82f,  1.0f },  // left delay1, deep
    { 1, 0.85f, -1.0f },  // left delay2, deep
    { 2, 0.70f, -1.0f },  // left AP2, deep
    { 4, 0.80f, -1.0f },  // right delay2, deep
};
static constexpr AlgorithmConfig::DattorroOutputTap kPlateDattorroRightTaps[7] = {
    { 0, 0.76f,  1.0f },  // left delay1, deep
    { 0, 0.92f,  1.0f },  // left delay1, very late
    { 2, 0.73f, -1.0f },  // left AP2, deep
    { 3, 0.80f,  1.0f },  // right delay1, deep
    { 4, 0.87f, -1.0f },  // right delay2, deep
    { 5, 0.72f, -1.0f },  // right AP2, deep
    { 1, 0.83f, -1.0f },  // left delay2, deep
};

// Room/Chamber/Ambient: mid-deep (balanced onset)
static constexpr AlgorithmConfig::DattorroOutputTap kRoomDattorroLeftTaps[7] = {
    { 3, 0.60f,  1.0f },  // right delay1
    { 3, 0.85f,  1.0f },  // right delay1, late
    { 5, 0.65f, -1.0f },  // right AP2
    { 0, 0.70f,  1.0f },  // left delay1
    { 1, 0.78f, -1.0f },  // left delay2
    { 2, 0.58f, -1.0f },  // left AP2
    { 4, 0.72f, -1.0f },  // right delay2
};
static constexpr AlgorithmConfig::DattorroOutputTap kRoomDattorroRightTaps[7] = {
    { 0, 0.58f,  1.0f },  // left delay1
    { 0, 0.83f,  1.0f },  // left delay1, late
    { 2, 0.67f, -1.0f },  // left AP2
    { 3, 0.65f,  1.0f },  // right delay1
    { 4, 0.80f, -1.0f },  // right delay2
    { 5, 0.62f, -1.0f },  // right AP2
    { 1, 0.75f, -1.0f },  // left delay2
};

// VV-extracted early reflection tap tables (from Hilbert/IR analysis)
static constexpr CustomERTap kHallERTaps[] = {
    { 39.35f, 0.020f, 0 }, { 32.77f, 0.017f, 1 }, { 32.35f, 0.013f, 0 },
    { 26.77f, 0.011f, 1 }, { 38.77f, 0.011f, 1 }, { 27.35f, 0.009f, 0 },
    { 22.77f, 0.008f, 1 }, { 34.06f, 0.006f, 0 }, { 20.35f, 0.006f, 0 },
    { 36.77f, 0.007f, 1 }, { 16.77f, 0.005f, 1 }, { 41.35f, 0.004f, 0 },
    { 28.77f, 0.005f, 1 }, { 39.06f, 0.005f, 0 }, { 46.06f, 0.012f, 0 },
    { 51.35f, 0.012f, 0 }, { 42.77f, 0.010f, 1 }, { 48.31f, 0.010f, 1 },
    { 53.06f, 0.011f, 0 }, { 58.31f, 0.011f, 1 }, { 55.40f, 0.009f, 0 },
    { 60.31f, 0.010f, 1 }, { 62.40f, 0.009f, 0 }, { 64.31f, 0.008f, 1 },
};

static constexpr CustomERTap kPlateERTaps[] = {
    { 28.19f, 0.025f, 0 }, { 21.60f, 0.019f, 1 }, { 28.10f, 0.022f, 0 },
    { 21.52f, 0.017f, 1 }, { 21.19f, 0.010f, 0 }, { 27.60f, 0.010f, 1 },
    { 31.60f, 0.010f, 1 }, { 16.19f, 0.007f, 0 }, { 33.19f, 0.006f, 0 },
    { 15.60f, 0.007f, 1 }, { 23.19f, 0.004f, 0 }, { 27.52f, 0.009f, 1 },
    { 31.52f, 0.009f, 1 }, { 35.19f, 0.013f, 0 }, { 40.19f, 0.013f, 0 },
    { 48.44f, 0.008f, 1 }, { 42.19f, 0.007f, 0 }, { 47.19f, 0.006f, 0 },
    { 52.19f, 0.006f, 0 }, { 15.52f, 0.006f, 1 }, { 9.19f, 0.002f, 0 },
    { 11.60f, 0.004f, 1 }, { 25.60f, 0.005f, 1 }, { 39.12f, 0.007f, 0 },
};

static constexpr CustomERTap kRoomERTaps[] = {
    { 28.00f, 0.011f, 0 }, { 25.17f, 0.010f, 1 }, { 39.00f, 0.007f, 0 },
    { 34.62f, 0.007f, 0 }, { 21.38f, 0.006f, 0 }, { 30.85f, 0.006f, 1 },
    { 19.48f, 0.005f, 1 }, { 17.00f, 0.004f, 0 }, { 32.38f, 0.004f, 0 },
    { 27.44f, 0.004f, 1 }, { 16.06f, 0.004f, 1 }, { 38.48f, 0.003f, 0 },
    { 10.38f, 0.002f, 0 }, { 23.62f, 0.003f, 0 }, { 28.58f, 0.003f, 1 },
    { 49.92f, 0.008f, 0 }, { 46.48f, 0.006f, 1 }, { 59.04f, 0.006f, 0 },
    { 44.23f, 0.005f, 0 }, { 52.42f, 0.005f, 0 }, { 68.12f, 0.007f, 0 },
    { 68.75f, 0.007f, 1 }, { 74.44f, 0.011f, 1 }, { 65.31f, 0.005f, 1 },
};

static constexpr CustomERTap kChamberERTaps[] = {
    { 32.46f, 0.003f, 1 }, { 32.54f, 0.003f, 1 }, { 37.33f, 0.002f, 0 },
    { 26.62f, 0.002f, 0 }, { 35.54f, 0.002f, 1 }, { 30.08f, 0.002f, 0 },
    { 33.92f, 0.002f, 0 }, { 34.21f, 0.002f, 1 }, { 29.71f, 0.001f, 1 },
    { 31.60f, 0.001f, 0 }, { 36.67f, 0.001f, 0 }, { 46.23f, 0.004f, 0 },
    { 42.67f, 0.003f, 0 }, { 49.62f, 0.003f, 1 }, { 53.42f, 0.003f, 0 },
    { 54.90f, 0.003f, 1 }, { 56.92f, 0.003f, 0 }, { 61.17f, 0.003f, 1 },
    { 64.04f, 0.005f, 0 }, { 64.90f, 0.003f, 1 }, { 67.62f, 0.003f, 0 },
    { 71.38f, 0.004f, 0 }, { 73.23f, 0.003f, 0 }, { 77.92f, 0.004f, 1 },
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
    0.50f,           // output diffusion scale
    20000.0f,        // bandwidth: full spectrum input (no HF attenuation before FDN)
    10.00f, 0.70f,   // ER: level=10.0 (Plate needs very strong ERs to match VV onset energy)
    2.95f,           // late gain: 2.95 (DattorroTank 1/7 output needs ~10x boost vs FDN)
    0.10f, 1.0f,     // mod: 0.10 (was 0.25; reduce spectral smearing for MFCC 36→51)
    0.65f, 1.05f, 1.0f, // damping: trebleMultScale=0.65, trebleMultScaleMax=1.05 (was 1.0; slight HF sustain boost for Vocal Plate 4kHz match)
    20000.0f,        // high crossover: 20kHz (two-band — Plate trebleMult>1.0 needs uniform bright boost across all HF)
    1.0f,            // airDampingScale: 1.0 (no extra air damping — Plate needs full brightness)
    0.5f, 1.5f,      // size range
    0.35f,           // ER crossfeed: 0.35
    0.03f,           // inline diffusion: 0.03 (minimal allpass phase smearing; higher values worsen small-size presets like Ambience Plate)
    1.0f,            // mod depth floor: 1.0 = uniform modulation
    0.0f,            // structural HF damping: off
    0.0f,            // structural LF damping: off
    1.5f,            // noise mod: 1.5 (plate ringing; jitter alone can't fix structural DattorroTank modes)
    0.12f,           // Hadamard perturbation: break symmetry
    1.0f,            // ER gain exponent: inverse distance (default)
    false,           // useWeightedGains: off
    true,            // useHouseholderFeedback: on (Householder 68.4 vs Hadamard 66)
    true,            // useDattorroTank: ON (with late tap positions matching VV onset)
    false,           // useQuadTank: OFF for Plate
    1.20f,           // decay time scale: increased to match VV decay length (was 0.92, DattorroTank decays ~30-50% faster)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    2.0f, 0.9f,     // short-decay boost: +2 dB
    0.0f, 0.0f,     // gate: disabled
    18000.0f,        // ER air absorption ceiling: 18kHz
    2000.0f,         // ER air absorption floor: default
    0.0f,            // ER decorrelation: off
    14.10f,          // output gain: 14.10 (recalibrated for delay_scale, was 16.95)
    0.20f,           // stereo coupling: 0.20
    0.0f, +2.5f, 3000.0f, // output EQ: +2.5dB high shelf at 3kHz (was +1.0; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ: bypassed
    false,           // enableSaturation: off (clean linear output, matching VV)
    30.0f,           // lateOnsetMs: 30ms ramp — separates ER onset from late reverb buildup
    0.0f,            // lateFeedForwardLevel: 0 (feed-forward doesn't match VV's tap timing)
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,  // custom ER: disabled
    kPlateDattorroLeftTaps, kPlateDattorroRightTaps,  // Dattorro taps: late onset
};

// ---------------------------------------------------------------------------
// PlateCrisp: Plate without onset ramp — for presets where DV is too smooth
// (negative crest). Steel Plate has crest -3.3dB because the 30ms ramp smooths
// transients too much. PlateCrisp preserves the sharp DattorroTank onset.
static constexpr AlgorithmConfig kPlateCrisp = {
    "PlateCrisp",
    { 661, 691, 719, 751, 787, 821, 853, 887,
      929, 967, 1009, 1051, 1097, 1151, 1201, 1249 },
    { 0, 1, 2, 6, 7, 13, 14, 15 },
    { 3, 4, 5, 8, 9, 10, 11, 12 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.65f, 0.65f,
    0.50f,
    20000.0f,
    10.00f, 0.70f,
    2.95f,
    0.10f, 1.0f,
    0.65f, 1.05f, 1.0f,
    20000.0f,
    1.0f,
    0.5f, 1.5f,
    0.35f,
    0.03f,
    1.0f,
    0.0f,
    0.0f,
    1.5f,
    0.12f,
    1.0f,
    false,
    true,            // useHouseholderFeedback
    true,            // useDattorroTank
    false,           // useQuadTank
    1.20f,           // decay time scale
    0.0f, 0, 1.0f,
    2.0f, 0.9f,
    0.0f, 0.0f,
    18000.0f, 2000.0f, 0.0f,
    14.10f, 0.20f,
    0.0f, -1.5f, 3000.0f,
    0.0f, 0.0f, 0.7f,
    false,
    0.0f,            // lateOnsetMs: 0 — NO ramp, preserves sharp onset for higher crest
    0.0f,
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,
    kPlateDattorroLeftTaps, kPlateDattorroRightTaps,
};

// ---------------------------------------------------------------------------
// PlateQuad: Plate variant using QuadTank for presets where DattorroTank's
// decay ratio is structurally too fast (Vocal Plate, Tight Plate).
// QuadTank's 4 cross-coupled allpass loops sustain longer and produce
// smoother onset, better matching VV's energy distribution for these presets.
static constexpr AlgorithmConfig kPlateQuad = {
    "PlateQuad",
    { 661, 691, 719, 751, 787, 821, 853, 887,
      929, 967, 1009, 1051, 1097, 1151, 1201, 1249 },
    { 0, 1, 2, 6, 7, 13, 14, 15 },
    { 3, 4, 5, 8, 9, 10, 11, 12 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.65f, 0.65f,    // input diffusion
    0.50f,           // output diffusion scale
    20000.0f,        // bandwidth
    10.00f, 0.70f,   // ER: same as Plate
    2.95f,           // late gain: same as DattorroTank Plate (QuadTank has similar output level)
    0.10f, 1.0f,     // mod
    0.65f, 1.05f, 1.0f, // damping
    20000.0f,        // high crossover
    1.0f,            // airDampingScale
    0.5f, 1.5f,      // size range
    0.35f,           // ER crossfeed
    0.03f,           // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    0.0f,            // structural LF damping
    1.5f,            // noise mod
    0.12f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON — key difference from kPlate
    1.40f,           // decay time scale: higher than DattorroTank (QuadTank sustains differently)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    2.0f, 0.9f,     // short-decay boost
    0.0f, 0.0f,     // gate
    18000.0f,        // ER air absorption ceiling
    2000.0f,         // ER air absorption floor
    0.0f,            // ER decorrelation
    14.10f,          // output gain: same as Plate initially
    0.20f,           // stereo coupling
    0.0f, +0.0f, 3000.0f, // output EQ (was -1.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ
    false,           // enableSaturation
    30.0f,           // lateOnsetMs
    0.0f,            // lateFeedForwardLevel
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,      // custom ER: disabled
    kPlateDattorroLeftTaps, kPlateDattorroRightTaps,  // taps (unused by QuadTank but required)
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
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth: full spectrum input
    0.65f, 0.85f,    // ER: level=0.65 (was 0.90; reduced post-ER-bypass for crest balance), timeScale=0.85
    1.24f,           // late gain: 1.24 (DattorroTank Hall ~13dB quieter than FDN)
    0.75f, 13.0f,    // mod: modDepthScale=0.75 (DV 33% too deep), modRateScale=13.0 (Hilbert: VV=5.4Hz vs DV=0.4Hz)
    0.50f, 1.50f, 1.0f, // damping: trebleMultScale=0.50
    4000.0f,         // high crossover: 4kHz (3kHz regressed — pushed too much into air band, kurtosis diverged)
    0.70f,           // airDampingScale: 0.70
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed: 0.10
    0.0f,            // inline diffusion: off (short allpasses tested at 0.20-0.50 — no measurable effect on peaks)
                     // Long allpasses (41-131) wrecked centroid_late. Multi-point output tapping handles density.
    1.0f,            // mod depth floor: 1.0 = uniform modulation
    0.0f,            // structural HF damping: off
    40.0f,           // structural LF damping: 40Hz HP (90Hz destroyed bass ratio and raised centroid — output EQ handles 80-250Hz instead)
    1.0f,            // noise mod: 1.0 (hall ringing)
    0.10f,           // Hadamard perturbation: 0.10 (0.05 wasn't enough — still audible repetition in tail)
    1.0f,            // ER gain exponent: 1.0 inverse distance (1.5 caused audible pulse after transients)
    false,           // useWeightedGains: off (true regressed decay_shape 100→74.4, total 75.4→73.1)
    true,            // useHouseholderFeedback: on (keep true per instructions)
    true,            // useDattorroTank: ON (with late tap positions matching VV onset)
    false,           // useQuadTank: OFF for Hall (tested — regressed Huge Synth Hall, no net improvement)
    0.79f,           // decay time scale: recalibrated for DattorroTank round 2
    0.0f, 0, 1.0f,  // dual-slope: disabled
    2.0f, 1.5f,     // short-decay boost: +2.0 dB (4.5 contributed to transient pulse)
    0.0f, 0.0f,     // gate: disabled
    20000.0f,        // ER air absorption ceiling: 20kHz (earliest taps: no filtering)
    18000.0f,        // ER air absorption floor: 18kHz
    0.55f,           // ER decorrelation: strong allpass decorrelation (IACC 0.747→target 0.321)
    8.53f,           // output gain: 8.53 (recalibrated for delay_scale, was 11.91)
    -1.0f,           // stereo coupling: -1.0 = full 16×16 Hadamard (was 0.15 split 8+8)
    0.0f,            // output low shelf: 0dB at 250Hz (neutral — sweeper shows DV already +2-4dB hotter in low end)
    +5.5f,           // output high shelf: +5.5dB at 5kHz (was +4.0; +1.5dB HF boost)
    5000.0f,         // output high shelf freq: 5kHz
    0.0f,            // output mid EQ: bypassed
    0.0f,            // output mid EQ: bypassed
    0.7f,            // output mid EQ: Q (bypassed)
    false,           // enableSaturation: off (clean linear output, matching VV)
    0.0f,            // lateOnsetMs: disabled
    0.20f,           // lateFeedForwardLevel: blend pre-diffusion late reverb
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,  // custom ER: disabled
    kHallDattorroLeftTaps, kHallDattorroRightTaps,  // Dattorro taps: deepest onset
};

// ---------------------------------------------------------------------------
// HallQuad: Hall variant using QuadTank for presets where DattorroTank
// oscillates/rings (Huge Synth Hall, Pad Hall, Concert Wave, Homestar).
// QuadTank's cross-coupled topology is more stable at extreme decay settings.
static constexpr AlgorithmConfig kHallQuad = {
    "HallQuad",
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },
    { 0, 3, 5, 7, 8, 10, 12, 15 },
    { 1, 2, 4, 6, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER
    1.24f,           // late gain: same as Hall DattorroTank initially
    0.75f, 13.0f,    // mod
    0.50f, 1.50f, 1.0f, // damping
    4000.0f,         // high crossover
    0.70f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping
    1.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON — key difference
    0.79f,           // decay time scale: same as Hall initially
    0.0f, 0, 1.0f,  // dual-slope
    2.0f, 1.5f,     // short-decay boost
    0.0f, 0.0f,     // gate
    20000.0f,        // ER air absorption ceiling
    18000.0f,        // ER air absorption floor
    0.55f,           // ER decorrelation
    8.53f,           // output gain
    -1.0f,           // stereo coupling
    0.0f, +4.0f, 5000.0f, // output EQ: +4.0dB high shelf (was +2.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ
    false,           // enableSaturation
    0.0f,            // lateOnsetMs
    0.20f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio: disabled (HallQuadSmooth handles crest-limited presets)
    0, nullptr,      // custom ER
    kHallDattorroLeftTaps, kHallDattorroRightTaps,
};

// ---------------------------------------------------------------------------
// HallQuadBright: Bright variant: higher HF sustain for dark presets
static constexpr AlgorithmConfig kHallQuadBright = {
    "HallQuadBright",
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },
    { 0, 3, 5, 7, 8, 10, 12, 15 },
    { 1, 2, 4, 6, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER
    1.24f,           // late gain: same as Hall DattorroTank initially
    0.75f, 13.0f,    // mod
    0.70f, 1.50f, 1.0f, // damping: trebleMultScale 0.50→0.70
    4000.0f,         // high crossover
    0.85f,           // airDampingScale: 0.70→0.85
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping
    1.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON — key difference
    0.79f,           // decay time scale: same as Hall initially
    0.0f, 0, 1.0f,  // dual-slope
    2.0f, 1.5f,     // short-decay boost
    0.0f, 0.0f,     // gate
    20000.0f,        // ER air absorption ceiling
    18000.0f,        // ER air absorption floor
    0.55f,           // ER decorrelation
    8.53f,           // output gain
    -1.0f,           // stereo coupling
    0.0f, +4.0f, 5000.0f, // output EQ: +4.0dB high shelf (was +2.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ
    false,           // enableSaturation
    0.0f,            // lateOnsetMs
    0.20f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio: disabled (HallQuadSmooth handles crest-limited presets)
    0, nullptr,      // custom ER
    kHallDattorroLeftTaps, kHallDattorroRightTaps,
};

// ---------------------------------------------------------------------------
// HallSlow: Hall variant with higher decay time scale for presets where
// DattorroTank decays too fast (Fat Snare Hall, Small Vocal Hall, Snare Hall).
static constexpr AlgorithmConfig kHallSlow = {
    "HallSlow",
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },
    { 0, 3, 5, 7, 8, 10, 12, 15 },
    { 1, 2, 4, 6, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER
    1.24f,           // late gain
    0.75f, 13.0f,    // mod
    0.50f, 1.50f, 1.0f, // damping
    4000.0f,         // high crossover
    0.70f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping
    1.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    true,            // useDattorroTank: ON (same topology, just slower decay)
    false,           // useQuadTank: OFF
    1.80f,           // decay time scale: 1.80 (was 1.50 — Small Vocal Hall still 0.63x at 1.50)
    0.0f, 0, 1.0f,  // dual-slope
    2.0f, 1.5f,     // short-decay boost
    0.0f, 0.0f,     // gate
    20000.0f,        // ER air absorption ceiling
    18000.0f,        // ER air absorption floor
    0.55f,           // ER decorrelation
    8.53f,           // output gain
    -1.0f,           // stereo coupling
    0.0f, +2.0f, 5000.0f, // output EQ
    0.0f, 0.0f, 0.7f,   // output mid EQ
    false,           // enableSaturation
    0.0f,            // lateOnsetMs
    0.20f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,      // custom ER
    kHallDattorroLeftTaps, kHallDattorroRightTaps,
};

// ---------------------------------------------------------------------------
// HallFDNDualSlopeBody: FDN with dual-slope for presets needing strong body.
// Huge Synth Hall: 115ms pre-delay means body (50-200ms) is very quiet.
// Dual-slope: 8 fast channels with high gain boost fill the body region,
// then 8 slow channels provide the sustained tail.
static constexpr AlgorithmConfig kHallFDNDualSlopeBody = {
    "HallFDNDualSlopeBody",
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },
    { 0, 3, 5, 7, 8, 10, 12, 15 },
    { 1, 2, 4, 6, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER
    0.22f,           // late gain: FDN native (no DattorroTank compensation)
    0.75f, 13.0f,    // mod
    0.50f, 1.50f, 1.0f, // damping
    4000.0f,         // high crossover
    0.70f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping
    1.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF — native FDN for dual-slope
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale: 1.50
    0.08f, 8, 3.5f,  // DUAL-SLOPE: fast channels get 8% of RT60, 8 channels, 3.5x gain boost
    2.0f, 1.5f,     // short-decay boost
    0.0f, 0.0f,     // gate
    20000.0f, 18000.0f, 0.55f,
    8.53f, -1.0f,
    0.0f, +2.0f, 5000.0f,
    0.0f, 0.0f, 0.7f,
    false, 0.0f, 0.20f,
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,
    kHallDattorroLeftTaps, kHallDattorroRightTaps,
};

// ---------------------------------------------------------------------------
// HallFDNSmooth: FDN with soft onset ramp for large hall presets where the
// standard FDN's instant onset creates too-high crest factor vs VV.
// Designed for Huge Synth Hall: long pre-delay + long decay + smooth buildup.
static constexpr AlgorithmConfig kHallFDNSmooth = {
    "HallFDNSmooth",
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },
    { 0, 3, 5, 7, 8, 10, 12, 15 },
    { 1, 2, 4, 6, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.65f, 0.55f,    // input diffusion: increased (0.65/0.55 vs 0.55/0.45) — more smearing
    0.50f,           // output diffusion scale: increased (0.50 vs 0.35) — smoother peaks
    20000.0f,        // bandwidth
    0.30f, 0.85f,    // ER: further reduced erLevelScale (0.30 vs 0.65) — minimal ER spike
    0.22f,           // late gain
    0.75f, 13.0f,    // mod
    0.50f, 1.50f, 1.0f, // damping
    4000.0f,         // high crossover
    0.70f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.05f,           // ER crossfeed: reduced (0.05 vs 0.10) — less ER→FDN bleed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping
    1.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank
    false,           // useQuadTank
    1.00f,           // decay time scale: 1.0 (no boost — preset controls decay directly)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    2.0f, 1.5f,     // short-decay boost
    0.0f, 0.0f,     // gate
    20000.0f, 18000.0f, 0.55f,
    8.53f, -1.0f,
    0.0f, +2.0f, 5000.0f,
    0.0f, 0.0f, 0.7f,
    false,           // enableSaturation: off (soft clipper doesn't help FDN crest at these levels)
    200.0f,          // lateOnsetMs: 200ms ramp — squared shape attenuates early FDN peaks
                      // For 115ms pre-delay: ramp is at 0.33 (squared) at crest window start
    0.10f,           // lateFeedForwardLevel: reduced (0.10 vs 0.20) — less pre-diffusion bleed
    1.5f,            // crestLimitRatio: limit peaks to 1.5× RMS (~3.5dB crest)
    0, nullptr,
    kHallDattorroLeftTaps, kHallDattorroRightTaps,
};

// ---------------------------------------------------------------------------
// HallQuadSustain: QuadTank with high decay scale for presets where all
// topologies decay too fast (Small Vocal Hall passes on this config).
static constexpr AlgorithmConfig kHallQuadSustain = {
    "HallQuadSustain",
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },
    { 0, 3, 5, 7, 8, 10, 12, 15 },
    { 1, 2, 4, 6, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER
    1.24f,           // late gain: DattorroTank-level (QuadTank needs same boost)
    0.75f, 13.0f,    // mod
    0.50f, 1.50f, 1.0f, // damping
    4000.0f,         // high crossover
    0.70f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping
    1.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON
    2.00f,           // decay time scale: 2.00 (QuadTank sustain for fast-decaying presets)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    2.0f, 1.5f,     // short-decay boost
    0.0f, 0.0f,     // gate
    20000.0f, 18000.0f, 0.55f,
    8.53f, -1.0f,
    0.0f, +2.0f, 5000.0f,
    0.0f, 0.0f, 0.7f,
    false, 0.0f, 0.20f,
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,
    kHallDattorroLeftTaps, kHallDattorroRightTaps,
};

// ---------------------------------------------------------------------------
// HallFDN: Hall variant using native 16-channel FDN for presets where both
// DattorroTank and QuadTank produce wrong decay/crest character.
// FDN has highest echo density and most controllable decay.
static constexpr AlgorithmConfig kHallFDN = {
    "HallFDN",
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },
    { 0, 3, 5, 7, 8, 10, 12, 15 },
    { 1, 2, 4, 6, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER
    0.22f,           // late gain: 0.22 (FDN native — no DattorroTank 13dB compensation)
    0.75f, 13.0f,    // mod
    0.50f, 1.50f, 1.0f, // damping
    4000.0f,         // high crossover
    0.70f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping
    1.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF — native FDN
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale: 1.50 (FDN decays faster than VV, needs boost)
    0.0f, 0, 1.0f,  // dual-slope
    2.0f, 1.5f,     // short-decay boost
    0.0f, 0.0f,     // gate
    20000.0f,        // ER air absorption ceiling
    18000.0f,        // ER air absorption floor
    0.55f,           // ER decorrelation
    8.53f,           // output gain
    -1.0f,           // stereo coupling
    0.0f, +7.5f, 5000.0f, // output EQ: +7.5dB high shelf (was +4.5; +3.0dB centroid boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ
    false,           // enableSaturation
    0.0f,            // lateOnsetMs
    0.20f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,      // custom ER
    kHallDattorroLeftTaps, kHallDattorroRightTaps,
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
    0.65f,           // output diffusion scale
    16000.0f,        // bandwidth: 16kHz
    0.55f, 0.55f,    // ER: level=0.55 (was 1.20; cut for +5.6dB peak excess), tight timing
    1.23f,           // late gain: 1.23 (DattorroTank Chamber ~9dB quieter)
    0.05f, 1.0f,     // mod: 0.05 (was 0.4; reducing modulation was the single biggest MFCC/spectral_eq win)
    0.85f, 1.00f, 1.0f, // damping: trebleMultScale=0.85 (was 0.90), trebleMultScaleMax=1.00 (was 0.90; 7/13 Chamber HF too dark)
    20000.0f,        // high crossover: 20kHz (effectively two-band — mid and air bands unified)
    1.0f,            // airDampingScale: 1.0 (no extra air damping)
    0.5f, 1.5f,      // size range
    0.2f,            // ER crossfeed: medium
    0.20f,           // inline diffusion: 0.20 (was 0.30; slight improvement)
    1.0f,            // mod depth floor: 1.0 = uniform modulation
    0.0f,            // structural HF damping: off
    0.0f,            // structural LF damping: off
    1.2f,            // noise mod: 1.2 (chamber ringing)
    0.10f,           // Hadamard perturbation: break symmetry
    1.0f,            // ER gain exponent: inverse distance (default)
    false,           // useWeightedGains: off
    true,            // useHouseholderFeedback: on (Householder 65.0 vs Hadamard 63.4)
    true,            // useDattorroTank: ON (with late tap positions matching VV onset)
    false,           // useQuadTank: OFF for Chamber
    0.88f,           // decay time scale: recalibrated for DattorroTank (was 0.99 for FDN)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled (Chamber has static lateGainScale tuning)
    0.0f, 0.0f,     // gate: disabled
    12000.0f,        // ER air absorption ceiling: default
    2000.0f,         // ER air absorption floor: default
    0.50f,           // ER decorrelation: strong (reduce early IACC from 0.617 toward 0.241)
    3.99f,           // output gain: 3.99 (recalibrated for delay_scale, was 8.15)
    0.15f,           // stereo coupling: moderate (wider stereo for IACC match)
    0.0f, +3.0f, 5000.0f, // output EQ: +3.0dB high shelf at 5kHz (was +1.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ: bypassed
    false,           // enableSaturation: off (clean linear output, matching VV)
    0.0f,            // lateOnsetMs: disabled
    0.15f,           // lateFeedForwardLevel: blend pre-diffusion late reverb
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,  // custom ER: disabled
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,  // Dattorro taps: mid-deep onset
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
    0.65f,           // output diffusion scale
    18000.0f,        // bandwidth: wide input (structural HF damping handles per-loop rolloff)
    0.80f, 0.45f,    // ER: level=0.80 (was 1.40; reduced for -6dB crest deficit), tighter timing
    0.63f,           // late gain: 0.63 (DattorroTank Room ~3dB quieter)
    1.3f, 1.0f,      // mod: wider stereo (modDepthScale=1.3, modRateScale=1.0)
    0.45f, 0.82f, 0.85f, // damping: trebleMultScale=0.45, trebleMultScaleMax=0.82 (was 0.75; 3 of 6 Room presets had HF too dark)
    6000.0f,         // high crossover: 6kHz (three-band: bass/mid/air — independent air band control)
    0.75f,           // airDampingScale: 0.75 (Room — faster air decay for natural room character)
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed: light
    0.0f,            // inline diffusion: off (0.20 crashed MFCC 56→17)
    1.0f,            // mod depth floor: uniform
    0.0f,            // structural HF damping: off (14kHz tried, decay calibration compensated by raising feedback → worse ringing)
    40.0f,           // structural LF damping: 40Hz HP to gently tame bass RT60 inflation
    1.5f,            // noise mod: 1.5 (room ringing)
    0.20f,           // Hadamard perturbation: 0.20 (was 0.08→0.12; stronger to detune 11kHz mode in Exciting Snare)
    0.30f,           // ER gain exponent: gentle rolloff (more energy in later taps)
    false,           // useWeightedGains: off
    false,           // useHouseholderFeedback: off (Room — Householder regressed 75.8 vs Hadamard 77.1)
    true,            // useDattorroTank: ON (with late tap positions matching VV onset)
    false,           // useQuadTank: OFF for Room
    1.37f,           // decay time scale: recalibrated for DattorroTank
    0.0f, 0, 1.0f,   // dual-slope: disabled (standard 16-channel FDN for matched tail energy)
    0.0f, 0.0f,      // short-decay boost: disabled (Room uses lateGainScale calibration)
    0.0f, 0.0f,      // gate: disabled
    10000.0f,         // ER air absorption ceiling: darken earliest taps slightly (centroid_early 6207→5521)
    2000.0f,          // ER air absorption floor: default
    0.35f,            // ER decorrelation: moderate (improve L/R spectral variation for MFCC)
    2.06f,            // output gain: 2.06 (recalibrated for delay_scale, was 1.20)
    0.20f,            // stereo coupling: moderate (rooms have natural coupling from walls)
    0.0f, +2.0f, 4000.0f, // output EQ: +2.0dB high shelf at 4kHz (was +0.5; +1.5dB centroid boost)
    2200.0f, -2.0f, 1.2f, // output mid EQ: -2dB notch at 2.2kHz (suppress measured 2215Hz ringing)
    false,           // enableSaturation: off (clean linear output, matching VV)
    0.0f,            // lateOnsetMs: disabled
    0.15f,           // lateFeedForwardLevel: blend pre-diffusion late reverb
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,  // custom ER: disabled
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,  // Dattorro taps: mid-deep onset
};

// ---------------------------------------------------------------------------
// RoomBright: Bright variant: higher HF sustain for dark presets
static constexpr AlgorithmConfig kRoomBright = {
    "RoomBright",
    { 1087, 1171, 1279, 1381, 1493, 1607, 1733, 1861,
      2003, 2153, 2309, 2467, 2633, 2801, 2963, 3137 },
    { 0, 1, 3, 7, 10, 11, 13, 14 },  // balanced partition (sum=16040, diff=2)
    { 2, 4, 5, 6, 8, 9, 12, 15 },
    { 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    0.75f, 0.70f,    // input diffusion: moderate-high
    0.65f,           // output diffusion scale
    18000.0f,        // bandwidth: wide input (structural HF damping handles per-loop rolloff)
    0.80f, 0.45f,    // ER: level=0.80 (was 1.40; reduced for -6dB crest deficit), tighter timing
    0.63f,           // late gain: 0.63 (DattorroTank Room ~3dB quieter)
    1.3f, 1.0f,      // mod: wider stereo (modDepthScale=1.3, modRateScale=1.0)
    0.65f, 0.82f, 0.85f, // damping: trebleMultScale 0.45→0.65
    6000.0f,         // high crossover: 6kHz (three-band: bass/mid/air — independent air band control)
    0.90f,           // airDampingScale: 0.75→0.90
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed: light
    0.0f,            // inline diffusion: off (0.20 crashed MFCC 56→17)
    1.0f,            // mod depth floor: uniform
    0.0f,            // structural HF damping: off (14kHz tried, decay calibration compensated by raising feedback → worse ringing)
    40.0f,           // structural LF damping: 40Hz HP to gently tame bass RT60 inflation
    1.5f,            // noise mod: 1.5 (room ringing)
    0.20f,           // Hadamard perturbation: 0.20 (was 0.08→0.12; stronger to detune 11kHz mode in Exciting Snare)
    0.30f,           // ER gain exponent: gentle rolloff (more energy in later taps)
    false,           // useWeightedGains: off
    false,           // useHouseholderFeedback: off (Room — Householder regressed 75.8 vs Hadamard 77.1)
    true,            // useDattorroTank: ON (with late tap positions matching VV onset)
    false,           // useQuadTank: OFF for Room
    1.37f,           // decay time scale: recalibrated for DattorroTank
    0.0f, 0, 1.0f,   // dual-slope: disabled (standard 16-channel FDN for matched tail energy)
    0.0f, 0.0f,      // short-decay boost: disabled (Room uses lateGainScale calibration)
    0.0f, 0.0f,      // gate: disabled
    10000.0f,         // ER air absorption ceiling: darken earliest taps slightly (centroid_early 6207→5521)
    2000.0f,          // ER air absorption floor: default
    0.35f,            // ER decorrelation: moderate (improve L/R spectral variation for MFCC)
    2.06f,            // output gain: 2.06 (recalibrated for delay_scale, was 1.20)
    0.20f,            // stereo coupling: moderate (rooms have natural coupling from walls)
    0.0f, +2.0f, 4000.0f, // output EQ: +2.0dB high shelf at 4kHz (was +0.5; +1.5dB centroid boost)
    2200.0f, -2.0f, 1.2f, // output mid EQ: -2dB notch at 2.2kHz (suppress measured 2215Hz ringing)
    false,           // enableSaturation: off (clean linear output, matching VV)
    0.0f,            // lateOnsetMs: disabled
    0.15f,           // lateFeedForwardLevel: blend pre-diffusion late reverb
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,  // custom ER: disabled
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,  // Dattorro taps: mid-deep onset
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
    0.70f,           // output diffusion scale
    18000.0f,        // bandwidth: wide input (let full spectrum in for bright Ambient)
    0.50f, 1.0f,     // ER: level=0.50 (was 0.0; enabled for custom ER taps — VV Ambient presets have early energy)
    0.51f,           // late gain: 0.51 (DattorroTank Ambient ~3dB quieter)
    1.5f, 1.3f,      // mod: heavy depth and rate
    0.80f, 1.50f, 1.0f, // damping: trebleMultScale=0.80, trebleMultScaleMax=1.50 (mid band identical to old 2-band)
    5000.0f,         // high crossover: 5kHz (3-band: bass < 241Hz, mid 241-5k, air > 5k)
    1.30f,           // airDampingScale: 1.30 (air > 5kHz decays slower — extra HF boost for Ambience match)
    0.5f, 1.5f,      // size range
    0.0f,            // ER crossfeed: off (no ERs)
    0.0f,            // inline diffusion: off (0.02 moved peak from 10kHz to 1kHz without reducing it)
    1.0f,            // mod depth floor: 1.0 = uniform modulation
    0.0f,            // structural HF damping: off (Ambient HF slightly fast; trebleMultScale=0.60 handles HF)
    0.0f,            // structural LF damping: off
    1.0f,            // noise mod: 1.0 (ambient ringing)
    0.0f,            // Hadamard perturbation: off (Ambient uses Householder)
    1.0f,            // ER gain exponent: inverse distance (default)
    false,           // useWeightedGains: off
    true,            // useHouseholderFeedback: on (Householder 82.4 vs Hadamard 79.4; more stable across presets)
    true,            // useDattorroTank: ON (with late tap positions matching VV onset)
    false,           // useQuadTank: OFF for Ambient
    0.98f,           // decay time scale: recalibrated for DattorroTank (was 0.99 for FDN)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled (Ambient already matched)
    0.0f, 0.0f,     // gate: disabled
    12000.0f,        // ER air absorption ceiling: default
    2000.0f,         // ER air absorption floor: default (no ERs in Ambient)
    0.0f,            // ER decorrelation: off
    1.24f,           // output gain: 1.24 (recalibrated for delay_scale, was 0.72)
    0.05f,           // stereo coupling: minimal (0.0 didn't help width enough)
    0.0f, +1.5f, 4000.0f, // output EQ: +1.5dB high shelf at 4kHz (was -0.5; +2.0dB centroid boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ: bypassed
    false,           // enableSaturation: off (clean linear output, matching VV)
    0.0f,            // lateOnsetMs: 0ms (Ambient onset already late — Ts=+32ms)
    0.15f,           // lateFeedForwardLevel: blend pre-diffusion late reverb
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,  // custom ER: disabled
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,  // Dattorro taps: mid-deep onset
};

// ---------------------------------------------------------------------------
// RoomFDN: Room variant using native 16-channel FDN for presets where
// DattorroTank decays too fast (decay ratios 0.29-0.73x). FDN's direct
// feedback control allows better matching of VV Room's sustain character.
static constexpr AlgorithmConfig kRoomFDN = {
    "RoomFDN",
    { 1087, 1171, 1279, 1381, 1493, 1607, 1733, 1861,
      2003, 2153, 2309, 2467, 2633, 2801, 2963, 3137 },  // same Room delay lengths
    { 0, 1, 3, 7, 10, 11, 13, 14 },  // same balanced partition as Room
    { 2, 4, 5, 6, 8, 9, 12, 15 },
    { 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    0.75f, 0.70f,    // input diffusion: same as Room
    0.65f,           // output diffusion scale
    18000.0f,        // bandwidth
    0.80f, 0.45f,    // ER: same as Room
    0.22f,           // late gain: FDN native (no DattorroTank compensation)
    1.3f, 1.0f,      // mod: same as Room
    0.45f, 0.82f, 0.85f, // damping: same as Room
    6000.0f,         // high crossover: same as Room (three-band)
    0.75f,           // airDampingScale: same as Room
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping: same as Room
    1.5f,            // noise mod: same as Room
    0.20f,           // Hadamard perturbation: same as Room
    0.30f,           // ER gain exponent: same as Room
    false,           // useWeightedGains
    false,           // useHouseholderFeedback: off (same as Room — Hadamard better)
    false,           // useDattorroTank: OFF — native FDN
    false,           // useQuadTank: OFF
    3.00f,           // decay time scale: 3.00 (1.80 gave same ratios as DattorroTank — FDN needs much more)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    2.0f, 1.5f,     // short-decay boost: same as Hall
    0.0f, 0.0f,     // gate: disabled
    10000.0f,        // ER air absorption ceiling: same as Room
    2000.0f,         // ER air absorption floor
    0.35f,           // ER decorrelation: same as Room
    2.06f,           // output gain: same as Room initially (will recalibrate)
    0.20f,           // stereo coupling: same as Room
    0.0f, +1.5f, 4000.0f, // output EQ: +1.5dB high shelf (was -2.0; HF brightness boost)
    2200.0f, -2.0f, 1.2f, // output mid EQ: same as Room
    false,           // enableSaturation
    0.0f,            // lateOnsetMs
    0.15f,           // lateFeedForwardLevel: same as Room
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,      // custom ER: disabled
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,  // taps (unused by FDN but required)
};

// ---------------------------------------------------------------------------
// RoomFDNBright: Bright variant: higher HF sustain for dark presets
static constexpr AlgorithmConfig kRoomFDNBright = {
    "RoomFDNBright",
    { 1087, 1171, 1279, 1381, 1493, 1607, 1733, 1861,
      2003, 2153, 2309, 2467, 2633, 2801, 2963, 3137 },  // same Room delay lengths
    { 0, 1, 3, 7, 10, 11, 13, 14 },  // same balanced partition as Room
    { 2, 4, 5, 6, 8, 9, 12, 15 },
    { 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    0.75f, 0.70f,    // input diffusion: same as Room
    0.65f,           // output diffusion scale
    18000.0f,        // bandwidth
    0.80f, 0.45f,    // ER: same as Room
    0.22f,           // late gain: FDN native (no DattorroTank compensation)
    1.3f, 1.0f,      // mod: same as Room
    0.65f, 0.82f, 0.85f, // damping: trebleMultScale 0.45→0.65
    6000.0f,         // high crossover: same as Room (three-band)
    0.90f,           // airDampingScale: 0.75→0.90
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping: same as Room
    1.5f,            // noise mod: same as Room
    0.20f,           // Hadamard perturbation: same as Room
    0.30f,           // ER gain exponent: same as Room
    false,           // useWeightedGains
    false,           // useHouseholderFeedback: off (same as Room — Hadamard better)
    false,           // useDattorroTank: OFF — native FDN
    false,           // useQuadTank: OFF
    3.00f,           // decay time scale: 3.00 (1.80 gave same ratios as DattorroTank — FDN needs much more)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    2.0f, 1.5f,     // short-decay boost: same as Hall
    0.0f, 0.0f,     // gate: disabled
    10000.0f,        // ER air absorption ceiling: same as Room
    2000.0f,         // ER air absorption floor
    0.35f,           // ER decorrelation: same as Room
    2.06f,           // output gain: same as Room initially (will recalibrate)
    0.20f,           // stereo coupling: same as Room
    0.0f, +1.5f, 4000.0f, // output EQ: +1.5dB high shelf (was -2.0; HF brightness boost)
    2200.0f, -2.0f, 1.2f, // output mid EQ: same as Room
    false,           // enableSaturation
    0.0f,            // lateOnsetMs
    0.15f,           // lateFeedForwardLevel: same as Room
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,      // custom ER: disabled
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,  // taps (unused by FDN but required)
};

// ---------------------------------------------------------------------------
// RoomQuad: Room variant using QuadTank for presets where both DattorroTank
// and FDN decay too fast (Fat Snare Room 0.51x, Long Dark 70s 0.30x).
// QuadTank's 4 cross-coupled allpass loops sustain longer, same pattern
// that solved Hall decay issues (HallQuad, HallQuadSustain).
static constexpr AlgorithmConfig kRoomQuad = {
    "RoomQuad",
    { 1087, 1171, 1279, 1381, 1493, 1607, 1733, 1861,
      2003, 2153, 2309, 2467, 2633, 2801, 2963, 3137 },  // same Room delay lengths
    { 0, 1, 3, 7, 10, 11, 13, 14 },
    { 2, 4, 5, 6, 8, 9, 12, 15 },
    { 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    0.75f, 0.70f,    // input diffusion: same as Room
    0.65f,           // output diffusion scale
    18000.0f,        // bandwidth
    0.80f, 0.45f,    // ER: same as Room
    0.63f,           // late gain: same as Room (QuadTank has similar output level to DattorroTank)
    1.3f, 1.0f,      // mod: same as Room
    0.45f, 0.82f, 0.85f, // damping: same as Room
    6000.0f,         // high crossover: same as Room
    0.75f,           // airDampingScale: same as Room
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping
    1.5f,            // noise mod
    0.20f,           // Hadamard perturbation
    0.30f,           // ER gain exponent: same as Room
    false,           // useWeightedGains
    false,           // useHouseholderFeedback: off (same as Room)
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON — key difference
    2.50f,           // decay time scale: 2.50 (QuadTank + high scale for max sustain)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    2.0f, 1.5f,     // short-decay boost
    0.0f, 0.0f,     // gate
    10000.0f,        // ER air absorption ceiling: same as Room
    2000.0f,         // ER air absorption floor
    0.35f,           // ER decorrelation: same as Room
    2.06f,           // output gain: same as Room initially
    0.20f,           // stereo coupling: same as Room
    0.0f, -2.0f, 4000.0f, // output EQ: same as Room
    2200.0f, -2.0f, 1.2f, // output mid EQ: same as Room
    false,           // enableSaturation
    0.0f,            // lateOnsetMs
    0.15f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,      // custom ER: disabled
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,  // taps (unused by QuadTank but required)
};

// ---------------------------------------------------------------------------
// RoomQuadSustain: RoomQuad with much higher decay scale for presets where
// even QuadTank at 2.50x decays too fast (Long Dark 70s 0.70x, Fat Snare 0.55x).
// Same pattern as HallQuadSustain (2.00) — extreme sustain boost.
static constexpr AlgorithmConfig kRoomQuadSustain = {
    "RoomQuadSustain",
    { 1087, 1171, 1279, 1381, 1493, 1607, 1733, 1861,
      2003, 2153, 2309, 2467, 2633, 2801, 2963, 3137 },
    { 0, 1, 3, 7, 10, 11, 13, 14 },
    { 2, 4, 5, 6, 8, 9, 12, 15 },
    { 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    0.75f, 0.70f,
    0.65f,
    18000.0f,
    0.80f, 0.45f,
    0.63f,           // late gain
    1.3f, 1.0f,
    0.45f, 0.82f, 0.85f,
    6000.0f,
    0.75f,
    0.5f, 1.5f,
    0.10f,
    0.0f,
    1.0f,
    0.0f,
    40.0f,
    1.5f,
    0.20f,
    0.30f,
    false,           // useWeightedGains
    false,           // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON
    4.00f,           // decay time scale: 4.00 (extreme sustain for hardest presets)
    0.0f, 0, 1.0f,
    2.0f, 1.5f,
    0.0f, 0.0f,
    10000.0f,
    2000.0f,
    0.35f,
    2.06f,           // output gain
    0.20f,
    0.0f, -0.5f, 4000.0f, // output EQ: -0.5dB high shelf (was -2.0; +1.5dB centroid boost)
    2200.0f, -2.0f, 1.2f,
    false,
    0.0f,
    0.15f,
    0.0f,  // crestLimitRatio: disabled
    0, nullptr,
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,
};

// ---------------------------------------------------------------------------
// ChamberQuad: Chamber variant using QuadTank for presets where DattorroTank
// decays too fast (Small Chamber1 0.65x, Tiled Room 0.57x).
// Same pattern as HallQuad — QuadTank sustains longer than DattorroTank.
static constexpr AlgorithmConfig kChamberQuad = {
    "ChamberQuad",
    { 751, 797, 839, 887, 947, 997, 1061, 1123,
      1187, 1259, 1327, 1409, 1493, 1583, 1669, 1777 },  // same Chamber delay lengths
    { 0, 1, 2, 7, 8, 11, 14, 15 },
    { 3, 4, 5, 6, 9, 10, 12, 13 },
    { 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f },
    0.75f, 0.625f,   // input diffusion: same as Chamber
    0.65f,           // output diffusion scale
    16000.0f,        // bandwidth
    0.55f, 0.55f,    // ER: same as Chamber
    1.23f,           // late gain: same as Chamber (QuadTank needs similar compensation)
    0.05f, 1.0f,     // mod: same as Chamber
    0.85f, 1.00f, 1.0f, // damping: same as Chamber
    20000.0f,        // high crossover
    1.0f,            // airDampingScale
    0.5f, 1.5f,      // size range
    0.2f,            // ER crossfeed
    0.20f,           // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    0.0f,            // structural LF damping
    1.2f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback: same as Chamber
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON — key difference
    1.80f,           // decay time scale: 1.80 (QuadTank + higher scale for sustain)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled
    0.0f, 0.0f,     // gate: disabled
    12000.0f,        // ER air absorption ceiling: same as Chamber
    2000.0f,         // ER air absorption floor
    0.50f,           // ER decorrelation: same as Chamber
    3.99f,           // output gain: same as Chamber
    0.15f,           // stereo coupling: same as Chamber
    0.0f, +4.5f, 5000.0f, // output EQ: +4.5dB high shelf (was +3.0; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ: bypassed
    false,           // enableSaturation
    0.0f,            // lateOnsetMs
    0.15f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,      // custom ER: disabled
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,  // taps (unused by QuadTank but required)
};

// ---------------------------------------------------------------------------
// ChamberQuadBright: Bright variant: higher HF sustain for dark presets
static constexpr AlgorithmConfig kChamberQuadBright = {
    "ChamberQuadBright",
    { 751, 797, 839, 887, 947, 997, 1061, 1123,
      1187, 1259, 1327, 1409, 1493, 1583, 1669, 1777 },  // same Chamber delay lengths
    { 0, 1, 2, 7, 8, 11, 14, 15 },
    { 3, 4, 5, 6, 9, 10, 12, 13 },
    { 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f },
    0.75f, 0.625f,   // input diffusion: same as Chamber
    0.65f,           // output diffusion scale
    16000.0f,        // bandwidth
    0.55f, 0.55f,    // ER: same as Chamber
    1.23f,           // late gain: same as Chamber (QuadTank needs similar compensation)
    0.05f, 1.0f,     // mod: same as Chamber
    1.05f, 1.00f, 1.0f, // damping: trebleMultScale 0.85→1.05
    20000.0f,        // high crossover
    1.15f,           // airDampingScale: 1.0→1.15
    0.5f, 1.5f,      // size range
    0.2f,            // ER crossfeed
    0.20f,           // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    0.0f,            // structural LF damping
    1.2f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback: same as Chamber
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON — key difference
    1.80f,           // decay time scale: 1.80 (QuadTank + higher scale for sustain)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled
    0.0f, 0.0f,     // gate: disabled
    12000.0f,        // ER air absorption ceiling: same as Chamber
    2000.0f,         // ER air absorption floor
    0.50f,           // ER decorrelation: same as Chamber
    3.99f,           // output gain: same as Chamber
    0.15f,           // stereo coupling: same as Chamber
    0.0f, +4.5f, 5000.0f, // output EQ: +4.5dB high shelf (was +3.0; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ: bypassed
    false,           // enableSaturation
    0.0f,            // lateOnsetMs
    0.15f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,      // custom ER: disabled
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,  // taps (unused by QuadTank but required)
};

// ---------------------------------------------------------------------------
// AmbientFDN: Ambient variant using native 16-channel FDN instead of DattorroTank.
// The 2-loop figure-of-eight creates too few modes for smooth ambient wash.
// 16-channel Hadamard FDN has ~64× more modal density (16 cross-coupled delays
// vs 2 loops). Should produce denser, smoother wash with better crest factor.
static constexpr AlgorithmConfig kAmbientFDN = {
    "AmbientFDN",
    { 971, 1049, 1153, 1277, 1399, 1523, 1667, 1811,
      1949, 2111, 2269, 2437, 2609, 2789, 2969, 3169 },  // same Ambient delay lengths
    { 0, 1, 2, 4, 11, 12, 13, 15 },
    { 3, 5, 6, 7, 8, 9, 10, 14 },
    { 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f },
    0.80f, 0.80f,    // input diffusion: maximum
    0.70f,           // output diffusion scale
    18000.0f,        // bandwidth
    0.50f, 1.0f,     // ER: same as Ambient
    0.22f,           // late gain: FDN native (no DattorroTank compensation)
    1.5f, 1.3f,      // mod: heavy depth and rate (same as Ambient)
    0.80f, 1.50f, 1.0f, // damping: same as Ambient
    5000.0f,         // high crossover
    1.30f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.0f,            // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    0.0f,            // structural LF damping
    1.0f,            // noise mod
    0.0f,            // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback: on
    false,           // useDattorroTank: OFF — native FDN
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale: 1.50 (FDN typically needs boost)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost
    0.0f, 0.0f,     // gate
    12000.0f,
    2000.0f,
    0.0f,            // ER decorrelation: off
    1.24f,           // output gain: same as Ambient initially
    0.05f,           // stereo coupling
    0.0f, +4.0f, 4000.0f, // output EQ: +4.0dB high shelf (was +2.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,
    false,
    0.0f,
    0.15f,           // lateFeedForwardLevel
    3.0f,            // crestLimitRatio: 3.0x — stronger peak limiting for Drum Air crest stability
    0, nullptr,
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,
};

// ---------------------------------------------------------------------------
// AmbientFDNBright: Bright variant: higher HF sustain for dark presets
static constexpr AlgorithmConfig kAmbientFDNBright = {
    "AmbientFDNBright",
    { 971, 1049, 1153, 1277, 1399, 1523, 1667, 1811,
      1949, 2111, 2269, 2437, 2609, 2789, 2969, 3169 },  // same Ambient delay lengths
    { 0, 1, 2, 4, 11, 12, 13, 15 },
    { 3, 5, 6, 7, 8, 9, 10, 14 },
    { 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f },
    0.80f, 0.80f,    // input diffusion: maximum
    0.70f,           // output diffusion scale
    18000.0f,        // bandwidth
    0.50f, 1.0f,     // ER: same as Ambient
    0.22f,           // late gain: FDN native (no DattorroTank compensation)
    1.5f, 1.3f,      // mod: heavy depth and rate (same as Ambient)
    1.00f, 1.50f, 1.0f, // damping: trebleMultScale 0.80→1.00
    5000.0f,         // high crossover
    1.45f,           // airDampingScale: 1.30→1.45
    0.5f, 1.5f,      // size range
    0.0f,            // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    0.0f,            // structural LF damping
    1.0f,            // noise mod
    0.0f,            // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback: on
    false,           // useDattorroTank: OFF — native FDN
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale: 1.50 (FDN typically needs boost)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost
    0.0f, 0.0f,     // gate
    12000.0f,
    2000.0f,
    0.0f,            // ER decorrelation: off
    1.24f,           // output gain: same as Ambient initially
    0.05f,           // stereo coupling
    0.0f, +4.0f, 4000.0f, // output EQ: +4.0dB high shelf (was +2.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,
    false,
    0.0f,
    0.15f,           // lateFeedForwardLevel
    3.0f,            // crestLimitRatio: 3.0x — stronger peak limiting for Drum Air crest stability
    0, nullptr,
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,
};

// ---------------------------------------------------------------------------
// AmbientQuad: Ambient variant using QuadTank (4 cross-coupled allpass loops).
// QuadTank has 4 loops × 12 taps = higher modal density than DattorroTank's 2 loops.
// Sustains longer (solved decay issues in Room/Chamber) and smoother onset.
static constexpr AlgorithmConfig kAmbientQuad = {
    "AmbientQuad",
    { 971, 1049, 1153, 1277, 1399, 1523, 1667, 1811,
      1949, 2111, 2269, 2437, 2609, 2789, 2969, 3169 },
    { 0, 1, 2, 4, 11, 12, 13, 15 },
    { 3, 5, 6, 7, 8, 9, 10, 14 },
    { 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f },
    0.80f, 0.80f,
    0.70f,
    18000.0f,
    0.50f, 1.0f,
    0.51f,           // late gain: same as Ambient DattorroTank (QuadTank similar output)
    1.5f, 1.3f,
    0.80f, 1.50f, 1.0f,
    5000.0f,
    1.30f,
    0.5f, 1.5f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    1.0f,
    false,
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON — 4 loops instead of 2
    2.00f,           // decay time scale: 2.00 (QuadTank needs higher scale for Ambient sustain)
    0.0f, 0, 1.0f,
    0.0f, 0.0f,
    0.0f, 0.0f,
    12000.0f,
    2000.0f,
    0.0f,
    1.24f,
    0.05f,
    0.0f, +3.0f, 4000.0f,  // output EQ: +3.0dB high shelf (was +1.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,
    false,
    0.0f,
    0.15f,
    0.0f,  // crestLimitRatio: disabled
    0, nullptr,
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,
};

// ---------------------------------------------------------------------------
// AmbientQuadBright: Bright variant: higher HF sustain for dark presets
static constexpr AlgorithmConfig kAmbientQuadBright = {
    "AmbientQuadBright",
    { 971, 1049, 1153, 1277, 1399, 1523, 1667, 1811,
      1949, 2111, 2269, 2437, 2609, 2789, 2969, 3169 },
    { 0, 1, 2, 4, 11, 12, 13, 15 },
    { 3, 5, 6, 7, 8, 9, 10, 14 },
    { 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f },
    0.80f, 0.80f,
    0.70f,
    18000.0f,
    0.50f, 1.0f,
    0.51f,           // late gain: same as Ambient DattorroTank (QuadTank similar output)
    1.5f, 1.3f,
    1.00f, 1.50f, 1.0f, // damping: trebleMultScale 0.80→1.00
    5000.0f,
    1.45f,           // airDampingScale: 1.30→1.45
    0.5f, 1.5f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    1.0f,
    false,
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON — 4 loops instead of 2
    2.00f,           // decay time scale: 2.00 (QuadTank needs higher scale for Ambient sustain)
    0.0f, 0, 1.0f,
    0.0f, 0.0f,
    0.0f, 0.0f,
    12000.0f,
    2000.0f,
    0.0f,
    1.24f,
    0.05f,
    0.0f, +3.0f, 4000.0f,  // output EQ: +3.0dB high shelf (was +1.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,
    false,
    0.0f,
    0.15f,
    0.0f,  // crestLimitRatio: disabled
    0, nullptr,
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,
};

// ---------------------------------------------------------------------------
// ChamberQuadSustain: ChamberQuad with much higher decay scale for presets
// where ChamberQuad (1.80) still decays too fast (Rich Chamber 0.30x,
// Fat Plate 0.38x, Small Chamber2 0.42x, A Plate 0.47x, etc.)
static constexpr AlgorithmConfig kChamberQuadSustain = {
    "ChamberQuadSustain",
    { 751, 797, 839, 887, 947, 997, 1061, 1123,
      1187, 1259, 1327, 1409, 1493, 1583, 1669, 1777 },  // Chamber delay lengths
    { 0, 1, 2, 7, 8, 11, 14, 15 },
    { 3, 4, 5, 6, 9, 10, 12, 13 },
    { 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f },
    0.75f, 0.625f,
    0.65f,
    16000.0f,
    0.55f, 0.55f,
    1.23f,
    0.05f, 1.0f,
    0.85f, 1.00f, 1.0f,
    20000.0f,
    1.0f,
    0.5f, 1.5f,
    0.2f,
    0.20f,
    1.0f,
    0.0f,
    0.0f,
    1.2f,
    0.10f,
    1.0f,
    false,
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON
    3.50f,           // decay time scale: 3.50 (extreme sustain for 0.30-0.50x presets)
    0.0f, 0, 1.0f,
    0.0f, 0.0f,
    0.0f, 0.0f,
    12000.0f,
    2000.0f,
    0.50f,
    3.99f,
    0.15f,
    0.0f, +3.5f, 5000.0f, // output EQ: +3.5dB high shelf (was +2.0; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,
    false,
    0.0f,
    0.15f,
    0.0f,  // crestLimitRatio: disabled
    0, nullptr,
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,
};

// ---------------------------------------------------------------------------
// ChamberQuadSustainHybrid: ChamberQuadSustain with hybrid dual-engine blending.
// Primary: ChamberQuadSustain (good envelope 0.91 + decay 0.78)
// Secondary: HallQuad index 6 (good centroid 1.06)
// Blend 35%: centroid ~0.71*0.65 + 1.06*0.35 = 0.83
// Designed for A Plate which no single topology can match on all metrics.
static constexpr AlgorithmConfig kChamberQuadSustainHybrid = {
    "ChamberQuadSustainHybrid",
    { 751, 797, 839, 887, 947, 997, 1061, 1123,
      1187, 1259, 1327, 1409, 1493, 1583, 1669, 1777 },
    { 0, 1, 2, 7, 8, 11, 14, 15 },
    { 3, 4, 5, 6, 9, 10, 12, 13 },
    { 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f },
    0.75f, 0.625f,
    0.65f,
    16000.0f,
    0.55f, 0.55f,
    1.23f,
    0.05f, 1.0f,
    0.85f, 1.00f, 1.0f,
    20000.0f,
    1.0f,
    0.5f, 1.5f,
    0.2f,
    0.20f,
    1.0f,
    0.0f,
    0.0f,
    1.2f,
    0.10f,
    1.0f,
    false,
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON
    3.50f,           // decay time scale
    0.0f, 0, 1.0f,
    0.0f, 0.0f,
    0.0f, 0.0f,
    12000.0f,
    2000.0f,
    0.50f,
    3.99f,
    0.15f,
    0.0f, +10.0f, 5000.0f, // output EQ: +10.0dB high shelf (very aggressive — compensates for dark QuadTank)
    0.0f, 0.0f, 0.7f,
    false,
    0.0f,
    0.15f,
    0.0f,  // crestLimitRatio: disabled
    0, nullptr,
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,
    1.0f,            // dattorroDelayScale
    0.0f,            // hybridBlend: disabled (output shelf approach instead)
    -1,              // hybridSecondaryAlgo: disabled
};

// ---------------------------------------------------------------------------
// ChamberFDN: Hall-scale FDN with Chamber damping for presets where QuadTank
// has poor envelope correlation (~0.73). Uses Hall delays (1801-5521) for sustain
// but Chamber damping/ER for tonal character. FDN envelope ~0.97 on Hall.
static constexpr AlgorithmConfig kChamberFDN = {
    "ChamberFDN",
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },  // Hall-scale delays for sustain
    { 0, 3, 5, 7, 8, 10, 12, 15 },   // Hall tap partition (balanced)
    { 1, 2, 4, 6, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion: FDN-style
    0.35f,           // output diffusion scale
    16000.0f,        // bandwidth: Chamber-style (16kHz vs Hall's 20kHz)
    0.55f, 0.55f,    // ER: Chamber-level
    0.22f,           // late gain: FDN-native
    0.05f, 1.0f,     // mod: Chamber-style (low modulation)
    0.85f, 1.00f, 1.0f, // damping: Chamber trebleMultScale
    20000.0f,        // high crossover
    1.0f,            // airDampingScale
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    0.0f,            // structural LF damping
    1.2f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF — native FDN
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale: 1.50 (FDN standard)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    2.0f, 1.5f,     // short-decay boost
    0.0f, 0.0f,     // gate
    12000.0f, 2000.0f, 0.50f,
    8.53f,           // output gain
    0.15f,           // stereo coupling
    0.0f, +1.5f, 5000.0f, // output EQ: +1.5dB high shelf (was -1.0; HF brightness boost)
    0.0f, 0.0f, 0.7f,
    false,           // no saturation
    0.0f,            // lateOnsetMs
    0.15f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,
    kHallDattorroLeftTaps, kHallDattorroRightTaps,
};

// ---------------------------------------------------------------------------
// AmbientQuadSustain: AmbientQuad with higher decay scale for decay-limited
// Ambient presets (Cross Stick 0.62x, Drum Air 0.54x, etc.)
static constexpr AlgorithmConfig kAmbientQuadSustain = {
    "AmbientQuadSustain",
    { 971, 1049, 1153, 1277, 1399, 1523, 1667, 1811,
      1949, 2111, 2269, 2437, 2609, 2789, 2969, 3169 },
    { 0, 1, 2, 4, 11, 12, 13, 15 },
    { 3, 5, 6, 7, 8, 9, 10, 14 },
    { 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f },
    0.80f, 0.80f,
    0.70f,
    18000.0f,
    0.50f, 1.0f,
    0.51f,
    1.5f, 1.3f,
    0.80f, 1.50f, 1.0f,
    5000.0f,
    1.30f,
    0.5f, 1.5f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    1.0f,
    false,
    true,
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON
    3.00f,           // decay time scale: 3.00 (vs AmbientQuad's 2.00)
    0.0f, 0, 1.0f,
    0.0f, 0.0f,
    0.0f, 0.0f,
    12000.0f,
    2000.0f,
    0.0f,
    1.24f,
    0.05f,
    0.0f, 0.0f, 4000.0f,  // output EQ: 0.0dB high shelf (was -1.5; +1.5dB HF brightness boost)
    0.0f, 0.0f, 0.7f,
    false,
    0.0f,
    0.15f,
    0.0f,  // crestLimitRatio: disabled
    0, nullptr,
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,
};

// ---------------------------------------------------------------------------
// RoomQuadSustainHigh: RoomQuadSustain with even higher decay scale (7.0x)
// for Fat Snare Room which still decays too fast at 4.0x (0.57x ratio).
static constexpr AlgorithmConfig kRoomQuadSustainHigh = {
    "RoomQuadSustainHigh",
    { 1087, 1171, 1279, 1381, 1493, 1607, 1733, 1861,
      2003, 2153, 2309, 2467, 2633, 2801, 2963, 3137 },
    { 0, 1, 3, 7, 10, 11, 13, 14 },
    { 2, 4, 5, 6, 8, 9, 12, 15 },
    { 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    0.75f, 0.70f,
    0.65f,
    18000.0f,
    0.80f, 0.45f,    // erLevelScale: original
    0.63f,           // late gain: original
    1.3f, 1.0f,
    0.45f, 0.82f, 0.85f,
    6000.0f,
    0.75f,
    0.5f, 1.5f,
    0.10f,           // erCrossfeed: original
    0.0f,
    1.0f,
    0.0f,
    40.0f,
    1.5f,
    0.20f,
    0.30f,
    false,           // useWeightedGains
    false,           // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON
    20.00f,          // decay time scale: 20.00 (extreme — QuadTank allpass loss needs large compensation)
    0.0f, 0, 1.0f,
    2.0f, 1.5f,
    0.0f, 0.0f,
    10000.0f,
    2000.0f,
    0.35f,
    2.06f,           // output gain
    0.20f,
    0.0f, -2.0f, 4000.0f,
    2200.0f, -2.0f, 1.2f,
    false,
    0.0f,
    0.15f,
    0.0f,  // crestLimitRatio: disabled
    0, nullptr,
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,
};

// ---------------------------------------------------------------------------
// AmbientQuadSustainHigh: AmbientQuadSustain with higher decay scale (4.50x)
// for Cross Stick Room (0.68x), Drum Air (0.65x), Very Small Ambience (0.71x).
static constexpr AlgorithmConfig kAmbientQuadSustainHigh = {
    "AmbientQuadSustainHigh",
    { 971, 1049, 1153, 1277, 1399, 1523, 1667, 1811,
      1949, 2111, 2269, 2437, 2609, 2789, 2969, 3169 },
    { 0, 1, 2, 4, 11, 12, 13, 15 },
    { 3, 5, 6, 7, 8, 9, 10, 14 },
    { 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f },
    0.80f, 0.80f,
    0.70f,
    18000.0f,
    0.50f, 1.0f,
    0.51f,
    1.5f, 1.3f,
    0.80f, 1.50f, 1.0f,
    5000.0f,
    1.30f,
    0.5f, 1.5f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    1.0f,
    false,
    true,
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON
    5.00f,           // decay time scale: 5.00 (moderate boost over AmbientQuadSustain's 3.00)
    0.0f, 0, 1.0f,
    0.0f, 0.0f,
    0.0f, 0.0f,
    12000.0f,
    2000.0f,
    0.0f,
    1.24f,
    0.05f,
    0.0f, 0.0f, 4000.0f,  // output EQ: 0.0dB high shelf (was -1.5; +1.5dB HF brightness boost)
    0.0f, 0.0f, 0.7f,
    false,
    0.0f,
    0.15f,
    0.0f,  // crestLimitRatio: disabled
    0, nullptr,
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,
};

// ---------------------------------------------------------------------------
// HallQuadSmooth: HallQuad with onset ramp + crest limiter for presets where
// QuadTank's structural onset creates too-high crest factor vs VV (+5.7dB).
// 100ms onset ramp reduces 100ms peak/RMS ratio by attenuating early energy.
// Crest limiter at 1.5× RMS catches remaining instantaneous peaks.
static constexpr AlgorithmConfig kHallQuadSmooth = {
    "HallQuadSmooth",
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },
    { 0, 3, 5, 7, 8, 10, 12, 15 },
    { 1, 2, 4, 6, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.65f, 0.55f,    // input diffusion: increased (more smearing for smoother onset)
    0.50f,           // output diffusion scale: increased (smoother peaks)
    20000.0f,        // bandwidth
    0.30f, 0.85f,    // ER: reduced erLevelScale (0.30 vs 0.65) to minimize ER spike
    1.24f,           // late gain
    0.75f, 13.0f,    // mod
    0.50f, 1.50f, 1.0f, // damping
    4000.0f,         // high crossover
    0.70f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.05f,           // ER crossfeed: reduced to limit ER→QuadTank bleed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping
    1.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON
    0.79f,           // decay time scale: same as HallQuad
    0.0f, 0, 1.0f,  // dual-slope
    2.0f, 1.5f,     // short-decay boost
    0.0f, 0.0f,     // gate
    20000.0f,        // ER air absorption ceiling
    18000.0f,        // ER air absorption floor
    0.55f,           // ER decorrelation
    8.53f,           // output gain
    -1.0f,           // stereo coupling
    0.0f, +2.0f, 5000.0f, // output EQ
    0.0f, 0.0f, 0.7f,   // output mid EQ
    false,           // enableSaturation
    100.0f,          // lateOnsetMs: 100ms ramp attenuates early QuadTank peaks
    0.10f,           // lateFeedForwardLevel: reduced (less pre-diffusion bleed)
    1.5f,            // crestLimitRatio: limit peaks to 1.5× RMS
    0, nullptr,      // custom ER
    kHallDattorroLeftTaps, kHallDattorroRightTaps,
};

// ---------------------------------------------------------------------------
static constexpr int kNumAlgorithms = 85;


// === AUTO-GENERATED PRESET ALGORITHMS ===

// Preset "Concert Wave" (cloned from HallFDN)
static constexpr AlgorithmConfig kPresetConcertWave = {
    "PresetConcertWave",
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },
    { 0, 3, 5, 7, 8, 10, 12, 15 },
    { 1, 2, 4, 6, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER
    0.22f,           // late gain: 0.22 (FDN native — no DattorroTank 13dB compensation)
    0.75f, 13.0f,    // mod
    0.50f, 1.50f, 1.0f, // damping
    4000.0f,         // high crossover
    0.70f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping
    1.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF — native FDN
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale: 1.50 (FDN decays faster than VV, needs boost)
    0.0f, 0, 1.0f,  // dual-slope
    2.0f, 1.5f,     // short-decay boost
    0.0f, 0.0f,     // gate
    20000.0f,        // ER air absorption ceiling
    18000.0f,        // ER air absorption floor
    0.55f,           // ER decorrelation
    8.53f,           // output gain
    -1.0f,           // stereo coupling
    0.0f, +7.5f, 5000.0f, // output EQ: +7.5dB high shelf (was +4.5; +3.0dB centroid boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ
    false,           // enableSaturation
    0.0f,            // lateOnsetMs
    0.20f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,      // custom ER
    kHallDattorroLeftTaps, kHallDattorroRightTaps,
};

// Preset "Fat Snare Hall" (cloned from Chamber)
static constexpr AlgorithmConfig kPresetFatSnareHall = {
    "PresetFatSnareHall",
    { 751, 797, 839, 887, 947, 997, 1061, 1123,
      1187, 1259, 1327, 1409, 1493, 1583, 1669, 1777 },  // logarithmic prime spacing for uniform modal density
    { 0, 1, 2, 7, 8, 11, 14, 15 },   // balanced partition (sum=9552, diff=2)
    { 3, 4, 5, 6, 9, 10, 12, 13 },
    { 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f },
    0.75f, 0.625f,   // input diffusion: Dattorro split
    0.65f,           // output diffusion scale
    16000.0f,        // bandwidth: 16kHz
    0.55f, 0.55f,    // ER: level=0.55 (was 1.20; cut for +5.6dB peak excess), tight timing
    1.23f,           // late gain: 1.23 (DattorroTank Chamber ~9dB quieter)
    0.05f, 1.0f,     // mod: 0.05 (was 0.4; reducing modulation was the single biggest MFCC/spectral_eq win)
    0.85f, 1.00f, 1.0f, // damping: trebleMultScale=0.85 (was 0.90), trebleMultScaleMax=1.00 (was 0.90; 7/13 Chamber HF too dark)
    20000.0f,        // high crossover: 20kHz (effectively two-band — mid and air bands unified)
    1.0f,            // airDampingScale: 1.0 (no extra air damping)
    0.5f, 1.5f,      // size range
    0.2f,            // ER crossfeed: medium
    0.20f,           // inline diffusion: 0.20 (was 0.30; slight improvement)
    1.0f,            // mod depth floor: 1.0 = uniform modulation
    0.0f,            // structural HF damping: off
    0.0f,            // structural LF damping: off
    1.2f,            // noise mod: 1.2 (chamber ringing)
    0.10f,           // Hadamard perturbation: break symmetry
    1.0f,            // ER gain exponent: inverse distance (default)
    false,           // useWeightedGains: off
    true,            // useHouseholderFeedback: on (Householder 65.0 vs Hadamard 63.4)
    true,            // useDattorroTank: ON (with late tap positions matching VV onset)
    false,           // useQuadTank: OFF for Chamber
    0.88f,           // decay time scale: recalibrated for DattorroTank (was 0.99 for FDN)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled (Chamber has static lateGainScale tuning)
    0.0f, 0.0f,     // gate: disabled
    12000.0f,        // ER air absorption ceiling: default
    2000.0f,         // ER air absorption floor: default
    0.50f,           // ER decorrelation: strong (reduce early IACC from 0.617 toward 0.241)
    3.99f,           // output gain: 3.99 (recalibrated for delay_scale, was 8.15)
    0.15f,           // stereo coupling: moderate (wider stereo for IACC match)
    0.0f, +3.0f, 5000.0f, // output EQ: +3.0dB high shelf at 5kHz (was +1.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ: bypassed
    false,           // enableSaturation: off (clean linear output, matching VV)
    0.0f,            // lateOnsetMs: disabled
    0.15f,           // lateFeedForwardLevel: blend pre-diffusion late reverb
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,  // custom ER: disabled
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,  // Dattorro taps: mid-deep onset
};

// Preset "Homestar Blade Runner" (cloned from HallQuadSmooth)
static constexpr AlgorithmConfig kPresetHomestarBladeRunner = {
    "PresetHomestarBladeRunner",
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },
    { 0, 3, 5, 7, 8, 10, 12, 15 },
    { 1, 2, 4, 6, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.65f, 0.55f,    // input diffusion: increased (more smearing for smoother onset)
    0.50f,           // output diffusion scale: increased (smoother peaks)
    20000.0f,        // bandwidth
    0.30f, 0.85f,    // ER: reduced erLevelScale (0.30 vs 0.65) to minimize ER spike
    1.24f,           // late gain
    0.75f, 13.0f,    // mod
    0.50f, 1.50f, 1.0f, // damping
    4000.0f,         // high crossover
    0.70f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.05f,           // ER crossfeed: reduced to limit ER→QuadTank bleed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping
    1.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON
    0.79f,           // decay time scale: same as HallQuad
    0.0f, 0, 1.0f,  // dual-slope
    2.0f, 1.5f,     // short-decay boost
    0.0f, 0.0f,     // gate
    20000.0f,        // ER air absorption ceiling
    18000.0f,        // ER air absorption floor
    0.55f,           // ER decorrelation
    8.53f,           // output gain
    -1.0f,           // stereo coupling
    0.0f, +2.0f, 5000.0f, // output EQ
    0.0f, 0.0f, 0.7f,   // output mid EQ
    false,           // enableSaturation
    100.0f,          // lateOnsetMs: 100ms ramp attenuates early QuadTank peaks
    0.10f,           // lateFeedForwardLevel: reduced (less pre-diffusion bleed)
    1.5f,            // crestLimitRatio: limit peaks to 1.5× RMS
    0, nullptr,      // custom ER
    kHallDattorroLeftTaps, kHallDattorroRightTaps,
};

// Preset "Huge Synth Hall" (cloned from HallQuadBright)
static constexpr AlgorithmConfig kPresetHugeSynthHall = {
    "PresetHugeSynthHall",
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },
    { 0, 3, 5, 7, 8, 10, 12, 15 },
    { 1, 2, 4, 6, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER
    1.24f,           // late gain: same as Hall DattorroTank initially
    0.75f, 13.0f,    // mod
    0.70f, 1.50f, 1.0f, // damping: trebleMultScale 0.50→0.70
    4000.0f,         // high crossover
    0.85f,           // airDampingScale: 0.70→0.85
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping
    1.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON — key difference
    0.79f,           // decay time scale: same as Hall initially
    0.0f, 0, 1.0f,  // dual-slope
    2.0f, 1.5f,     // short-decay boost
    0.0f, 0.0f,     // gate
    20000.0f,        // ER air absorption ceiling
    18000.0f,        // ER air absorption floor
    0.55f,           // ER decorrelation
    8.53f,           // output gain
    -1.0f,           // stereo coupling
    0.0f, +4.0f, 5000.0f, // output EQ: +4.0dB high shelf (was +2.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ
    false,           // enableSaturation
    0.0f,            // lateOnsetMs
    0.20f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio: disabled (HallQuadSmooth handles crest-limited presets)
    0, nullptr,      // custom ER
    kHallDattorroLeftTaps, kHallDattorroRightTaps,
};

// Preset "Long Synth Hall" (cloned from HallQuadBright)
static constexpr AlgorithmConfig kPresetLongSynthHall = {
    "PresetLongSynthHall",
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },
    { 0, 3, 5, 7, 8, 10, 12, 15 },
    { 1, 2, 4, 6, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER
    1.24f,           // late gain: same as Hall DattorroTank initially
    0.75f, 13.0f,    // mod
    0.70f, 1.50f, 1.0f, // damping: trebleMultScale 0.50→0.70
    4000.0f,         // high crossover
    0.85f,           // airDampingScale: 0.70→0.85
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping
    1.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON — key difference
    0.79f,           // decay time scale: same as Hall initially
    0.0f, 0, 1.0f,  // dual-slope
    2.0f, 1.5f,     // short-decay boost
    0.0f, 0.0f,     // gate
    20000.0f,        // ER air absorption ceiling
    18000.0f,        // ER air absorption floor
    0.55f,           // ER decorrelation
    8.53f,           // output gain
    -1.0f,           // stereo coupling
    0.0f, +4.0f, 5000.0f, // output EQ: +4.0dB high shelf (was +2.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ
    false,           // enableSaturation
    0.0f,            // lateOnsetMs
    0.20f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio: disabled (HallQuadSmooth handles crest-limited presets)
    0, nullptr,      // custom ER
    kHallDattorroLeftTaps, kHallDattorroRightTaps,
};

// Preset "Pad Hall" (cloned from HallQuadBright)
static constexpr AlgorithmConfig kPresetPadHall = {
    "PresetPadHall",
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },
    { 0, 3, 5, 7, 8, 10, 12, 15 },
    { 1, 2, 4, 6, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER
    1.24f,           // late gain: same as Hall DattorroTank initially
    0.75f, 13.0f,    // mod
    0.70f, 1.50f, 1.0f, // damping: trebleMultScale 0.50→0.70
    4000.0f,         // high crossover
    0.85f,           // airDampingScale: 0.70→0.85
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping
    1.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON — key difference
    0.79f,           // decay time scale: same as Hall initially
    0.0f, 0, 1.0f,  // dual-slope
    2.0f, 1.5f,     // short-decay boost
    0.0f, 0.0f,     // gate
    20000.0f,        // ER air absorption ceiling
    18000.0f,        // ER air absorption floor
    0.55f,           // ER decorrelation
    8.53f,           // output gain
    -1.0f,           // stereo coupling
    0.0f, +4.0f, 5000.0f, // output EQ: +4.0dB high shelf (was +2.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ
    false,           // enableSaturation
    0.0f,            // lateOnsetMs
    0.20f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio: disabled (HallQuadSmooth handles crest-limited presets)
    0, nullptr,      // custom ER
    kHallDattorroLeftTaps, kHallDattorroRightTaps,
};

// Preset "Small Vocal Hall" (cloned from Hall)
static constexpr AlgorithmConfig kPresetSmallVocalHall = {
    "PresetSmallVocalHall",
    // delay lengths: 1801-5521 samples (41-125ms @ 44.1kHz) — wide spread for concert hall character
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },  // logarithmic prime spacing for uniform modal density
    { 0, 3, 5, 7, 8, 10, 12, 15 },     // leftTaps: balanced partition (sum=26710, diff=52 samples ≈ 1ms)
    { 1, 2, 4, 6, 9, 11, 13, 14 },     // rightTaps: balanced partition (sum=26762)
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },  // alternating signs for decorrelation
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion: 0.55/0.45 (was 0.75/0.625 — reduced for sparser early onset)
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth: full spectrum input
    0.65f, 0.85f,    // ER: level=0.65 (was 0.90; reduced post-ER-bypass for crest balance), timeScale=0.85
    1.24f,           // late gain: 1.24 (DattorroTank Hall ~13dB quieter than FDN)
    0.75f, 13.0f,    // mod: modDepthScale=0.75 (DV 33% too deep), modRateScale=13.0 (Hilbert: VV=5.4Hz vs DV=0.4Hz)
    0.50f, 1.50f, 1.0f, // damping: trebleMultScale=0.50
    4000.0f,         // high crossover: 4kHz (3kHz regressed — pushed too much into air band, kurtosis diverged)
    0.70f,           // airDampingScale: 0.70
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed: 0.10
    0.0f,            // inline diffusion: off (short allpasses tested at 0.20-0.50 — no measurable effect on peaks)
                     // Long allpasses (41-131) wrecked centroid_late. Multi-point output tapping handles density.
    1.0f,            // mod depth floor: 1.0 = uniform modulation
    0.0f,            // structural HF damping: off
    40.0f,           // structural LF damping: 40Hz HP (90Hz destroyed bass ratio and raised centroid — output EQ handles 80-250Hz instead)
    1.0f,            // noise mod: 1.0 (hall ringing)
    0.10f,           // Hadamard perturbation: 0.10 (0.05 wasn't enough — still audible repetition in tail)
    1.0f,            // ER gain exponent: 1.0 inverse distance (1.5 caused audible pulse after transients)
    false,           // useWeightedGains: off (true regressed decay_shape 100→74.4, total 75.4→73.1)
    true,            // useHouseholderFeedback: on (keep true per instructions)
    true,            // useDattorroTank: ON (with late tap positions matching VV onset)
    false,           // useQuadTank: OFF for Hall (tested — regressed Huge Synth Hall, no net improvement)
    0.79f,           // decay time scale: recalibrated for DattorroTank round 2
    0.0f, 0, 1.0f,  // dual-slope: disabled
    2.0f, 1.5f,     // short-decay boost: +2.0 dB (4.5 contributed to transient pulse)
    0.0f, 0.0f,     // gate: disabled
    20000.0f,        // ER air absorption ceiling: 20kHz (earliest taps: no filtering)
    18000.0f,        // ER air absorption floor: 18kHz
    0.55f,           // ER decorrelation: strong allpass decorrelation (IACC 0.747→target 0.321)
    8.53f,           // output gain: 8.53 (recalibrated for delay_scale, was 11.91)
    -1.0f,           // stereo coupling: -1.0 = full 16×16 Hadamard (was 0.15 split 8+8)
    0.0f,            // output low shelf: 0dB at 250Hz (neutral — sweeper shows DV already +2-4dB hotter in low end)
    +5.5f,           // output high shelf: +5.5dB at 5kHz (was +4.0; +1.5dB HF boost)
    5000.0f,         // output high shelf freq: 5kHz
    0.0f,            // output mid EQ: bypassed
    0.0f,            // output mid EQ: bypassed
    0.7f,            // output mid EQ: Q (bypassed)
    false,           // enableSaturation: off (clean linear output, matching VV)
    0.0f,            // lateOnsetMs: disabled
    0.20f,           // lateFeedForwardLevel: blend pre-diffusion late reverb
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,  // custom ER: disabled
    kHallDattorroLeftTaps, kHallDattorroRightTaps,  // Dattorro taps: deepest onset
};

// Preset "Snare Hall" (cloned from HallSlow)
static constexpr AlgorithmConfig kPresetSnareHall = {
    "PresetSnareHall",
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },
    { 0, 3, 5, 7, 8, 10, 12, 15 },
    { 1, 2, 4, 6, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER
    1.24f,           // late gain
    0.75f, 13.0f,    // mod
    0.50f, 1.50f, 1.0f, // damping
    4000.0f,         // high crossover
    0.70f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping
    1.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    true,            // useDattorroTank: ON (same topology, just slower decay)
    false,           // useQuadTank: OFF
    1.80f,           // decay time scale: 1.80 (was 1.50 — Small Vocal Hall still 0.63x at 1.50)
    0.0f, 0, 1.0f,  // dual-slope
    2.0f, 1.5f,     // short-decay boost
    0.0f, 0.0f,     // gate
    20000.0f,        // ER air absorption ceiling
    18000.0f,        // ER air absorption floor
    0.55f,           // ER decorrelation
    8.53f,           // output gain
    -1.0f,           // stereo coupling
    0.0f, +2.0f, 5000.0f, // output EQ
    0.0f, 0.0f, 0.7f,   // output mid EQ
    false,           // enableSaturation
    0.0f,            // lateOnsetMs
    0.20f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,      // custom ER
    kHallDattorroLeftTaps, kHallDattorroRightTaps,
};

// Preset "Very Nice Hall" (cloned from HallFDN)
static constexpr AlgorithmConfig kPresetVeryNiceHall = {
    "PresetVeryNiceHall",
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },
    { 0, 3, 5, 7, 8, 10, 12, 15 },
    { 1, 2, 4, 6, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER
    0.22f,           // late gain: 0.22 (FDN native — no DattorroTank 13dB compensation)
    0.75f, 13.0f,    // mod
    0.50f, 1.50f, 1.0f, // damping
    4000.0f,         // high crossover
    0.70f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping
    1.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF — native FDN
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale: 1.50 (FDN decays faster than VV, needs boost)
    0.0f, 0, 1.0f,  // dual-slope
    2.0f, 1.5f,     // short-decay boost
    0.0f, 0.0f,     // gate
    20000.0f,        // ER air absorption ceiling
    18000.0f,        // ER air absorption floor
    0.55f,           // ER decorrelation
    8.53f,           // output gain
    -1.0f,           // stereo coupling
    0.0f, +7.5f, 5000.0f, // output EQ: +7.5dB high shelf (was +4.5; +3.0dB centroid boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ
    false,           // enableSaturation
    0.0f,            // lateOnsetMs
    0.20f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,      // custom ER
    kHallDattorroLeftTaps, kHallDattorroRightTaps,
};

// Preset "Vocal Hall" (cloned from ChamberQuadSustainHybrid)
static constexpr AlgorithmConfig kPresetVocalHall = {
    "PresetVocalHall",
    { 751, 797, 839, 887, 947, 997, 1061, 1123,
      1187, 1259, 1327, 1409, 1493, 1583, 1669, 1777 },
    { 0, 1, 2, 7, 8, 11, 14, 15 },
    { 3, 4, 5, 6, 9, 10, 12, 13 },
    { 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f },
    0.75f, 0.625f,
    0.65f,
    16000.0f,
    0.55f, 0.55f,
    1.23f,
    0.05f, 1.0f,
    0.85f, 1.00f, 1.0f,
    20000.0f,
    1.0f,
    0.5f, 1.5f,
    0.2f,
    0.20f,
    1.0f,
    0.0f,
    0.0f,
    1.2f,
    0.10f,
    1.0f,
    false,
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON
    3.50f,           // decay time scale
    0.0f, 0, 1.0f,
    0.0f, 0.0f,
    0.0f, 0.0f,
    12000.0f,
    2000.0f,
    0.50f,
    3.99f,
    0.15f,
    0.0f, +10.0f, 5000.0f, // output EQ: +10.0dB high shelf (very aggressive — compensates for dark QuadTank)
    0.0f, 0.0f, 0.7f,
    false,
    0.0f,
    0.15f,
    0.0f,  // crestLimitRatio: disabled
    0, nullptr,
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,
    1.0f,            // dattorroDelayScale
    0.0f,            // hybridBlend: disabled (output shelf approach instead)
    -1,              // hybridSecondaryAlgo: disabled
};

// Preset "Drum Plate" (cloned from HallQuadBright)
static constexpr AlgorithmConfig kPresetDrumPlate = {
    "PresetDrumPlate",
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },
    { 0, 3, 5, 7, 8, 10, 12, 15 },
    { 1, 2, 4, 6, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER
    1.24f,           // late gain: same as Hall DattorroTank initially
    0.75f, 13.0f,    // mod
    0.70f, 1.50f, 1.0f, // damping: trebleMultScale 0.50→0.70
    4000.0f,         // high crossover
    0.85f,           // airDampingScale: 0.70→0.85
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping
    1.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON — key difference
    0.79f,           // decay time scale: same as Hall initially
    0.0f, 0, 1.0f,  // dual-slope
    2.0f, 1.5f,     // short-decay boost
    0.0f, 0.0f,     // gate
    20000.0f,        // ER air absorption ceiling
    18000.0f,        // ER air absorption floor
    0.55f,           // ER decorrelation
    8.53f,           // output gain
    -1.0f,           // stereo coupling
    0.0f, +4.0f, 5000.0f, // output EQ: +4.0dB high shelf (was +2.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ
    false,           // enableSaturation
    0.0f,            // lateOnsetMs
    0.20f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio: disabled (HallQuadSmooth handles crest-limited presets)
    0, nullptr,      // custom ER
    kHallDattorroLeftTaps, kHallDattorroRightTaps,
};

// Preset "Fat Drums" (cloned from HallQuadSustain)
static constexpr AlgorithmConfig kPresetFatDrums = {
    "PresetFatDrums",
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },
    { 0, 3, 5, 7, 8, 10, 12, 15 },
    { 1, 2, 4, 6, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER
    1.24f,           // late gain: DattorroTank-level (QuadTank needs same boost)
    0.75f, 13.0f,    // mod
    0.50f, 1.50f, 1.0f, // damping
    4000.0f,         // high crossover
    0.70f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping
    1.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON
    2.00f,           // decay time scale: 2.00 (QuadTank sustain for fast-decaying presets)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    2.0f, 1.5f,     // short-decay boost
    0.0f, 0.0f,     // gate
    20000.0f, 18000.0f, 0.55f,
    8.53f, -1.0f,
    0.0f, +2.0f, 5000.0f,
    0.0f, 0.0f, 0.7f,
    false, 0.0f, 0.20f,
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,
    kHallDattorroLeftTaps, kHallDattorroRightTaps,
};

// Preset "Large Plate" (cloned from HallQuadBright)
static constexpr AlgorithmConfig kPresetLargePlate = {
    "PresetLargePlate",
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },
    { 0, 3, 5, 7, 8, 10, 12, 15 },
    { 1, 2, 4, 6, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER
    1.24f,           // late gain: same as Hall DattorroTank initially
    0.75f, 13.0f,    // mod
    0.70f, 1.50f, 1.0f, // damping: trebleMultScale 0.50→0.70
    4000.0f,         // high crossover
    0.85f,           // airDampingScale: 0.70→0.85
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping
    1.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON — key difference
    0.79f,           // decay time scale: same as Hall initially
    0.0f, 0, 1.0f,  // dual-slope
    2.0f, 1.5f,     // short-decay boost
    0.0f, 0.0f,     // gate
    20000.0f,        // ER air absorption ceiling
    18000.0f,        // ER air absorption floor
    0.55f,           // ER decorrelation
    8.53f,           // output gain
    -1.0f,           // stereo coupling
    0.0f, +4.0f, 5000.0f, // output EQ: +4.0dB high shelf (was +2.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ
    false,           // enableSaturation
    0.0f,            // lateOnsetMs
    0.20f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio: disabled (HallQuadSmooth handles crest-limited presets)
    0, nullptr,      // custom ER
    kHallDattorroLeftTaps, kHallDattorroRightTaps,
};

// Preset "Steel Plate" (cloned from PlateQuad)
static constexpr AlgorithmConfig kPresetSteelPlate = {
    "PresetSteelPlate",
    { 661, 691, 719, 751, 787, 821, 853, 887,
      929, 967, 1009, 1051, 1097, 1151, 1201, 1249 },
    { 0, 1, 2, 6, 7, 13, 14, 15 },
    { 3, 4, 5, 8, 9, 10, 11, 12 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.65f, 0.65f,    // input diffusion
    0.50f,           // output diffusion scale
    20000.0f,        // bandwidth
    10.00f, 0.70f,   // ER: same as Plate
    2.95f,           // late gain: same as DattorroTank Plate (QuadTank has similar output level)
    0.10f, 1.0f,     // mod
    0.65f, 1.05f, 1.0f, // damping
    20000.0f,        // high crossover
    1.0f,            // airDampingScale
    0.5f, 1.5f,      // size range
    0.35f,           // ER crossfeed
    0.03f,           // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    0.0f,            // structural LF damping
    1.5f,            // noise mod
    0.12f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON — key difference from kPlate
    1.40f,           // decay time scale: higher than DattorroTank (QuadTank sustains differently)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    2.0f, 0.9f,     // short-decay boost
    0.0f, 0.0f,     // gate
    18000.0f,        // ER air absorption ceiling
    2000.0f,         // ER air absorption floor
    0.0f,            // ER decorrelation
    14.10f,          // output gain: same as Plate initially
    0.20f,           // stereo coupling
    0.0f, +0.0f, 3000.0f, // output EQ (was -1.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ
    false,           // enableSaturation
    30.0f,           // lateOnsetMs
    0.0f,            // lateFeedForwardLevel
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,      // custom ER: disabled
    kPlateDattorroLeftTaps, kPlateDattorroRightTaps,  // taps (unused by QuadTank but required)
};

// Preset "Tight Plate" (cloned from PlateQuad)
static constexpr AlgorithmConfig kPresetTightPlate = {
    "PresetTightPlate",
    { 661, 691, 719, 751, 787, 821, 853, 887,
      929, 967, 1009, 1051, 1097, 1151, 1201, 1249 },
    { 0, 1, 2, 6, 7, 13, 14, 15 },
    { 3, 4, 5, 8, 9, 10, 11, 12 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.65f, 0.65f,    // input diffusion
    0.50f,           // output diffusion scale
    20000.0f,        // bandwidth
    10.00f, 0.70f,   // ER: same as Plate
    2.95f,           // late gain: same as DattorroTank Plate (QuadTank has similar output level)
    0.10f, 1.0f,     // mod
    0.65f, 1.05f, 1.0f, // damping
    20000.0f,        // high crossover
    1.0f,            // airDampingScale
    0.5f, 1.5f,      // size range
    0.35f,           // ER crossfeed
    0.03f,           // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    0.0f,            // structural LF damping
    1.5f,            // noise mod
    0.12f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON — key difference from kPlate
    1.40f,           // decay time scale: higher than DattorroTank (QuadTank sustains differently)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    2.0f, 0.9f,     // short-decay boost
    0.0f, 0.0f,     // gate
    18000.0f,        // ER air absorption ceiling
    2000.0f,         // ER air absorption floor
    0.0f,            // ER decorrelation
    14.10f,          // output gain: same as Plate initially
    0.20f,           // stereo coupling
    0.0f, +0.0f, 3000.0f, // output EQ (was -1.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ
    false,           // enableSaturation
    30.0f,           // lateOnsetMs
    0.0f,            // lateFeedForwardLevel
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,      // custom ER: disabled
    kPlateDattorroLeftTaps, kPlateDattorroRightTaps,  // taps (unused by QuadTank but required)
};

// Preset "Vocal Plate" (cloned from Plate)
static constexpr AlgorithmConfig kPresetVocalPlate = {
    "PresetVocalPlate",
    { 661, 691, 719, 751, 787, 821, 853, 887,
      929, 967, 1009, 1051, 1097, 1151, 1201, 1249 },  // logarithmic prime spacing for uniform modal density
    { 0, 1, 2, 6, 7, 13, 14, 15 },   // balanced partition (sum=7412, diff=0)
    { 3, 4, 5, 8, 9, 10, 11, 12 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.65f, 0.65f,   // input diffusion: 0.65
    0.50f,           // output diffusion scale
    20000.0f,        // bandwidth: full spectrum input (no HF attenuation before FDN)
    10.00f, 0.70f,   // ER: level=10.0 (Plate needs very strong ERs to match VV onset energy)
    2.95f,           // late gain: 2.95 (DattorroTank 1/7 output needs ~10x boost vs FDN)
    0.10f, 1.0f,     // mod: 0.10 (was 0.25; reduce spectral smearing for MFCC 36→51)
    0.65f, 1.05f, 1.0f, // damping: trebleMultScale=0.65, trebleMultScaleMax=1.05 (was 1.0; slight HF sustain boost for Vocal Plate 4kHz match)
    20000.0f,        // high crossover: 20kHz (two-band — Plate trebleMult>1.0 needs uniform bright boost across all HF)
    1.0f,            // airDampingScale: 1.0 (no extra air damping — Plate needs full brightness)
    0.5f, 1.5f,      // size range
    0.35f,           // ER crossfeed: 0.35
    0.03f,           // inline diffusion: 0.03 (minimal allpass phase smearing; higher values worsen small-size presets like Ambience Plate)
    1.0f,            // mod depth floor: 1.0 = uniform modulation
    0.0f,            // structural HF damping: off
    0.0f,            // structural LF damping: off
    1.5f,            // noise mod: 1.5 (plate ringing; jitter alone can't fix structural DattorroTank modes)
    0.12f,           // Hadamard perturbation: break symmetry
    1.0f,            // ER gain exponent: inverse distance (default)
    false,           // useWeightedGains: off
    true,            // useHouseholderFeedback: on (Householder 68.4 vs Hadamard 66)
    true,            // useDattorroTank: ON (with late tap positions matching VV onset)
    false,           // useQuadTank: OFF for Plate
    1.20f,           // decay time scale: increased to match VV decay length (was 0.92, DattorroTank decays ~30-50% faster)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    2.0f, 0.9f,     // short-decay boost: +2 dB
    0.0f, 0.0f,     // gate: disabled
    18000.0f,        // ER air absorption ceiling: 18kHz
    2000.0f,         // ER air absorption floor: default
    0.0f,            // ER decorrelation: off
    14.10f,          // output gain: 14.10 (recalibrated for delay_scale, was 16.95)
    0.20f,           // stereo coupling: 0.20
    0.0f, +2.5f, 3000.0f, // output EQ: +2.5dB high shelf at 3kHz (was +1.0; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ: bypassed
    false,           // enableSaturation: off (clean linear output, matching VV)
    30.0f,           // lateOnsetMs: 30ms ramp — separates ER onset from late reverb buildup
    0.0f,            // lateFeedForwardLevel: 0 (feed-forward doesn't match VV's tap timing)
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,  // custom ER: disabled
    kPlateDattorroLeftTaps, kPlateDattorroRightTaps,  // Dattorro taps: late onset
};

// Preset "Vox Plate" (cloned from Plate)
static constexpr AlgorithmConfig kPresetVoxPlate = {
    "PresetVoxPlate",
    { 661, 691, 719, 751, 787, 821, 853, 887,
      929, 967, 1009, 1051, 1097, 1151, 1201, 1249 },  // logarithmic prime spacing for uniform modal density
    { 0, 1, 2, 6, 7, 13, 14, 15 },   // balanced partition (sum=7412, diff=0)
    { 3, 4, 5, 8, 9, 10, 11, 12 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.65f, 0.65f,   // input diffusion: 0.65
    0.50f,           // output diffusion scale
    20000.0f,        // bandwidth: full spectrum input (no HF attenuation before FDN)
    10.00f, 0.70f,   // ER: level=10.0 (Plate needs very strong ERs to match VV onset energy)
    2.95f,           // late gain: 2.95 (DattorroTank 1/7 output needs ~10x boost vs FDN)
    0.10f, 1.0f,     // mod: 0.10 (was 0.25; reduce spectral smearing for MFCC 36→51)
    0.65f, 1.05f, 1.0f, // damping: trebleMultScale=0.65, trebleMultScaleMax=1.05 (was 1.0; slight HF sustain boost for Vocal Plate 4kHz match)
    20000.0f,        // high crossover: 20kHz (two-band — Plate trebleMult>1.0 needs uniform bright boost across all HF)
    1.0f,            // airDampingScale: 1.0 (no extra air damping — Plate needs full brightness)
    0.5f, 1.5f,      // size range
    0.35f,           // ER crossfeed: 0.35
    0.03f,           // inline diffusion: 0.03 (minimal allpass phase smearing; higher values worsen small-size presets like Ambience Plate)
    1.0f,            // mod depth floor: 1.0 = uniform modulation
    0.0f,            // structural HF damping: off
    0.0f,            // structural LF damping: off
    1.5f,            // noise mod: 1.5 (plate ringing; jitter alone can't fix structural DattorroTank modes)
    0.12f,           // Hadamard perturbation: break symmetry
    1.0f,            // ER gain exponent: inverse distance (default)
    false,           // useWeightedGains: off
    true,            // useHouseholderFeedback: on (Householder 68.4 vs Hadamard 66)
    true,            // useDattorroTank: ON (with late tap positions matching VV onset)
    false,           // useQuadTank: OFF for Plate
    1.20f,           // decay time scale: increased to match VV decay length (was 0.92, DattorroTank decays ~30-50% faster)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    2.0f, 0.9f,     // short-decay boost: +2 dB
    0.0f, 0.0f,     // gate: disabled
    18000.0f,        // ER air absorption ceiling: 18kHz
    2000.0f,         // ER air absorption floor: default
    0.0f,            // ER decorrelation: off
    14.10f,          // output gain: 14.10 (recalibrated for delay_scale, was 16.95)
    0.20f,           // stereo coupling: 0.20
    0.0f, +2.5f, 3000.0f, // output EQ: +2.5dB high shelf at 3kHz (was +1.0; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ: bypassed
    false,           // enableSaturation: off (clean linear output, matching VV)
    30.0f,           // lateOnsetMs: 30ms ramp — separates ER onset from late reverb buildup
    0.0f,            // lateFeedForwardLevel: 0 (feed-forward doesn't match VV's tap timing)
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,  // custom ER: disabled
    kPlateDattorroLeftTaps, kPlateDattorroRightTaps,  // Dattorro taps: late onset
};

// Preset "Dark Vocal Room" (cloned from RoomQuad)
static constexpr AlgorithmConfig kPresetDarkVocalRoom = {
    "PresetDarkVocalRoom",
    { 1087, 1171, 1279, 1381, 1493, 1607, 1733, 1861,
      2003, 2153, 2309, 2467, 2633, 2801, 2963, 3137 },  // same Room delay lengths
    { 0, 1, 3, 7, 10, 11, 13, 14 },
    { 2, 4, 5, 6, 8, 9, 12, 15 },
    { 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    0.75f, 0.70f,    // input diffusion: same as Room
    0.65f,           // output diffusion scale
    18000.0f,        // bandwidth
    0.80f, 0.45f,    // ER: same as Room
    0.63f,           // late gain: same as Room (QuadTank has similar output level to DattorroTank)
    1.3f, 1.0f,      // mod: same as Room
    0.45f, 0.82f, 0.85f, // damping: same as Room
    6000.0f,         // high crossover: same as Room
    0.75f,           // airDampingScale: same as Room
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping
    1.5f,            // noise mod
    0.20f,           // Hadamard perturbation
    0.30f,           // ER gain exponent: same as Room
    false,           // useWeightedGains
    false,           // useHouseholderFeedback: off (same as Room)
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON — key difference
    2.50f,           // decay time scale: 2.50 (QuadTank + high scale for max sustain)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    2.0f, 1.5f,     // short-decay boost
    0.0f, 0.0f,     // gate
    10000.0f,        // ER air absorption ceiling: same as Room
    2000.0f,         // ER air absorption floor
    0.35f,           // ER decorrelation: same as Room
    2.06f,           // output gain: same as Room initially
    0.20f,           // stereo coupling: same as Room
    0.0f, -2.0f, 4000.0f, // output EQ: same as Room
    2200.0f, -2.0f, 1.2f, // output mid EQ: same as Room
    false,           // enableSaturation
    0.0f,            // lateOnsetMs
    0.15f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,      // custom ER: disabled
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,  // taps (unused by QuadTank but required)
};

// Preset "Exciting Snare room" (cloned from Room)
static constexpr AlgorithmConfig kPresetExcitingSnareroom = {
    "PresetExcitingSnareroom",
    { 1087, 1171, 1279, 1381, 1493, 1607, 1733, 1861,
      2003, 2153, 2309, 2467, 2633, 2801, 2963, 3137 },
    { 0, 1, 3, 7, 10, 11, 13, 14 },  // balanced partition (sum=16040, diff=2)
    { 2, 4, 5, 6, 8, 9, 12, 15 },
    { 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    0.75f, 0.70f,    // input diffusion: moderate-high
    0.65f,           // output diffusion scale
    18000.0f,        // bandwidth: wide input (structural HF damping handles per-loop rolloff)
    0.80f, 0.45f,    // ER: level=0.80 (was 1.40; reduced for -6dB crest deficit), tighter timing
    0.63f,           // late gain: 0.63 (DattorroTank Room ~3dB quieter)
    1.3f, 1.0f,      // mod: wider stereo (modDepthScale=1.3, modRateScale=1.0)
    0.45f, 0.82f, 0.85f, // damping: trebleMultScale=0.45, trebleMultScaleMax=0.82 (was 0.75; 3 of 6 Room presets had HF too dark)
    6000.0f,         // high crossover: 6kHz (three-band: bass/mid/air — independent air band control)
    0.75f,           // airDampingScale: 0.75 (Room — faster air decay for natural room character)
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed: light
    0.0f,            // inline diffusion: off (0.20 crashed MFCC 56→17)
    1.0f,            // mod depth floor: uniform
    0.0f,            // structural HF damping: off (14kHz tried, decay calibration compensated by raising feedback → worse ringing)
    40.0f,           // structural LF damping: 40Hz HP to gently tame bass RT60 inflation
    1.5f,            // noise mod: 1.5 (room ringing)
    0.20f,           // Hadamard perturbation: 0.20 (was 0.08→0.12; stronger to detune 11kHz mode in Exciting Snare)
    0.30f,           // ER gain exponent: gentle rolloff (more energy in later taps)
    false,           // useWeightedGains: off
    false,           // useHouseholderFeedback: off (Room — Householder regressed 75.8 vs Hadamard 77.1)
    true,            // useDattorroTank: ON (with late tap positions matching VV onset)
    false,           // useQuadTank: OFF for Room
    1.37f,           // decay time scale: recalibrated for DattorroTank
    0.0f, 0, 1.0f,   // dual-slope: disabled (standard 16-channel FDN for matched tail energy)
    0.0f, 0.0f,      // short-decay boost: disabled (Room uses lateGainScale calibration)
    0.0f, 0.0f,      // gate: disabled
    10000.0f,         // ER air absorption ceiling: darken earliest taps slightly (centroid_early 6207→5521)
    2000.0f,          // ER air absorption floor: default
    0.35f,            // ER decorrelation: moderate (improve L/R spectral variation for MFCC)
    2.06f,            // output gain: 2.06 (recalibrated for delay_scale, was 1.20)
    0.20f,            // stereo coupling: moderate (rooms have natural coupling from walls)
    0.0f, +2.0f, 4000.0f, // output EQ: +2.0dB high shelf at 4kHz (was +0.5; +1.5dB centroid boost)
    2200.0f, -2.0f, 1.2f, // output mid EQ: -2dB notch at 2.2kHz (suppress measured 2215Hz ringing)
    false,           // enableSaturation: off (clean linear output, matching VV)
    0.0f,            // lateOnsetMs: disabled
    0.15f,           // lateFeedForwardLevel: blend pre-diffusion late reverb
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,  // custom ER: disabled
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,  // Dattorro taps: mid-deep onset
};

// Preset "Fat Snare Room" (cloned from HallQuadBright)
static constexpr AlgorithmConfig kPresetFatSnareRoom = {
    "PresetFatSnareRoom",
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },
    { 0, 3, 5, 7, 8, 10, 12, 15 },
    { 1, 2, 4, 6, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER
    1.24f,           // late gain: same as Hall DattorroTank initially
    0.75f, 13.0f,    // mod
    0.70f, 1.50f, 1.0f, // damping: trebleMultScale 0.50→0.70
    4000.0f,         // high crossover
    0.85f,           // airDampingScale: 0.70→0.85
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping
    1.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON — key difference
    0.79f,           // decay time scale: same as Hall initially
    0.0f, 0, 1.0f,  // dual-slope
    2.0f, 1.5f,     // short-decay boost
    0.0f, 0.0f,     // gate
    20000.0f,        // ER air absorption ceiling
    18000.0f,        // ER air absorption floor
    0.55f,           // ER decorrelation
    8.53f,           // output gain
    -1.0f,           // stereo coupling
    0.0f, +4.0f, 5000.0f, // output EQ: +4.0dB high shelf (was +2.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ
    false,           // enableSaturation
    0.0f,            // lateOnsetMs
    0.20f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio: disabled (HallQuadSmooth handles crest-limited presets)
    0, nullptr,      // custom ER
    kHallDattorroLeftTaps, kHallDattorroRightTaps,
};

// Preset "Lively Snare Room" (cloned from RoomBright)
static constexpr AlgorithmConfig kPresetLivelySnareRoom = {
    "PresetLivelySnareRoom",
    { 1087, 1171, 1279, 1381, 1493, 1607, 1733, 1861,
      2003, 2153, 2309, 2467, 2633, 2801, 2963, 3137 },
    { 0, 1, 3, 7, 10, 11, 13, 14 },  // balanced partition (sum=16040, diff=2)
    { 2, 4, 5, 6, 8, 9, 12, 15 },
    { 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    0.75f, 0.70f,    // input diffusion: moderate-high
    0.65f,           // output diffusion scale
    18000.0f,        // bandwidth: wide input (structural HF damping handles per-loop rolloff)
    0.80f, 0.45f,    // ER: level=0.80 (was 1.40; reduced for -6dB crest deficit), tighter timing
    0.63f,           // late gain: 0.63 (DattorroTank Room ~3dB quieter)
    1.3f, 1.0f,      // mod: wider stereo (modDepthScale=1.3, modRateScale=1.0)
    0.65f, 0.82f, 0.85f, // damping: trebleMultScale 0.45→0.65
    6000.0f,         // high crossover: 6kHz (three-band: bass/mid/air — independent air band control)
    0.90f,           // airDampingScale: 0.75→0.90
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed: light
    0.0f,            // inline diffusion: off (0.20 crashed MFCC 56→17)
    1.0f,            // mod depth floor: uniform
    0.0f,            // structural HF damping: off (14kHz tried, decay calibration compensated by raising feedback → worse ringing)
    40.0f,           // structural LF damping: 40Hz HP to gently tame bass RT60 inflation
    1.5f,            // noise mod: 1.5 (room ringing)
    0.20f,           // Hadamard perturbation: 0.20 (was 0.08→0.12; stronger to detune 11kHz mode in Exciting Snare)
    0.30f,           // ER gain exponent: gentle rolloff (more energy in later taps)
    false,           // useWeightedGains: off
    false,           // useHouseholderFeedback: off (Room — Householder regressed 75.8 vs Hadamard 77.1)
    true,            // useDattorroTank: ON (with late tap positions matching VV onset)
    false,           // useQuadTank: OFF for Room
    1.37f,           // decay time scale: recalibrated for DattorroTank
    0.0f, 0, 1.0f,   // dual-slope: disabled (standard 16-channel FDN for matched tail energy)
    0.0f, 0.0f,      // short-decay boost: disabled (Room uses lateGainScale calibration)
    0.0f, 0.0f,      // gate: disabled
    10000.0f,         // ER air absorption ceiling: darken earliest taps slightly (centroid_early 6207→5521)
    2000.0f,          // ER air absorption floor: default
    0.35f,            // ER decorrelation: moderate (improve L/R spectral variation for MFCC)
    2.06f,            // output gain: 2.06 (recalibrated for delay_scale, was 1.20)
    0.20f,            // stereo coupling: moderate (rooms have natural coupling from walls)
    0.0f, +2.0f, 4000.0f, // output EQ: +2.0dB high shelf at 4kHz (was +0.5; +1.5dB centroid boost)
    2200.0f, -2.0f, 1.2f, // output mid EQ: -2dB notch at 2.2kHz (suppress measured 2215Hz ringing)
    false,           // enableSaturation: off (clean linear output, matching VV)
    0.0f,            // lateOnsetMs: disabled
    0.15f,           // lateFeedForwardLevel: blend pre-diffusion late reverb
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,  // custom ER: disabled
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,  // Dattorro taps: mid-deep onset
};

// Preset "Long Dark 70s Snare Room" (cloned from AmbientQuadSustain)
static constexpr AlgorithmConfig kPresetLongDark70sSnareRoom = {
    "PresetLongDark70sSnareRoom",
    { 971, 1049, 1153, 1277, 1399, 1523, 1667, 1811,
      1949, 2111, 2269, 2437, 2609, 2789, 2969, 3169 },
    { 0, 1, 2, 4, 11, 12, 13, 15 },
    { 3, 5, 6, 7, 8, 9, 10, 14 },
    { 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f },
    0.80f, 0.80f,
    0.70f,
    18000.0f,
    0.50f, 1.0f,
    0.51f,
    1.5f, 1.3f,
    0.80f, 1.50f, 1.0f,
    5000.0f,
    1.30f,
    0.5f, 1.5f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    1.0f,
    false,
    true,
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON
    3.00f,           // decay time scale: 3.00 (vs AmbientQuad's 2.00)
    0.0f, 0, 1.0f,
    0.0f, 0.0f,
    0.0f, 0.0f,
    12000.0f,
    2000.0f,
    0.0f,
    1.24f,
    0.05f,
    0.0f, 0.0f, 4000.0f,  // output EQ: 0.0dB high shelf (was -1.5; +1.5dB HF brightness boost)
    0.0f, 0.0f, 0.7f,
    false,
    0.0f,
    0.15f,
    0.0f,  // crestLimitRatio: disabled
    0, nullptr,
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,
};

// Preset "Short Dark Snare Room" (cloned from AmbientFDN)
static constexpr AlgorithmConfig kPresetShortDarkSnareRoom = {
    "PresetShortDarkSnareRoom",
    { 971, 1049, 1153, 1277, 1399, 1523, 1667, 1811,
      1949, 2111, 2269, 2437, 2609, 2789, 2969, 3169 },  // same Ambient delay lengths
    { 0, 1, 2, 4, 11, 12, 13, 15 },
    { 3, 5, 6, 7, 8, 9, 10, 14 },
    { 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f },
    0.80f, 0.80f,    // input diffusion: maximum
    0.70f,           // output diffusion scale
    18000.0f,        // bandwidth
    0.50f, 1.0f,     // ER: same as Ambient
    0.22f,           // late gain: FDN native (no DattorroTank compensation)
    1.5f, 1.3f,      // mod: heavy depth and rate (same as Ambient)
    0.80f, 1.50f, 1.0f, // damping: same as Ambient
    5000.0f,         // high crossover
    1.30f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.0f,            // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    0.0f,            // structural LF damping
    1.0f,            // noise mod
    0.0f,            // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback: on
    false,           // useDattorroTank: OFF — native FDN
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale: 1.50 (FDN typically needs boost)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost
    0.0f, 0.0f,     // gate
    12000.0f,
    2000.0f,
    0.0f,            // ER decorrelation: off
    1.24f,           // output gain: same as Ambient initially
    0.05f,           // stereo coupling
    0.0f, +4.0f, 4000.0f, // output EQ: +4.0dB high shelf (was +2.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,
    false,
    0.0f,
    0.15f,           // lateFeedForwardLevel
    3.0f,            // crestLimitRatio: 3.0x — stronger peak limiting for Drum Air crest stability
    0, nullptr,
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,
};

// Preset "A Plate" (cloned from HallFDNSmooth)
static constexpr AlgorithmConfig kPresetAPlate = {
    "PresetAPlate",
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },
    { 0, 3, 5, 7, 8, 10, 12, 15 },
    { 1, 2, 4, 6, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.65f, 0.55f,    // input diffusion: increased (0.65/0.55 vs 0.55/0.45) — more smearing
    0.50f,           // output diffusion scale: increased (0.50 vs 0.35) — smoother peaks
    20000.0f,        // bandwidth
    0.30f, 0.85f,    // ER: further reduced erLevelScale (0.30 vs 0.65) — minimal ER spike
    0.22f,           // late gain
    0.75f, 13.0f,    // mod
    0.50f, 1.50f, 1.0f, // damping
    4000.0f,         // high crossover
    0.70f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.05f,           // ER crossfeed: reduced (0.05 vs 0.10) — less ER→FDN bleed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping
    1.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank
    false,           // useQuadTank
    1.00f,           // decay time scale: 1.0 (no boost — preset controls decay directly)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    2.0f, 1.5f,     // short-decay boost
    0.0f, 0.0f,     // gate
    20000.0f, 18000.0f, 0.55f,
    8.53f, -1.0f,
    0.0f, +2.0f, 5000.0f,
    0.0f, 0.0f, 0.7f,
    false,           // enableSaturation: off (soft clipper doesn't help FDN crest at these levels)
    200.0f,          // lateOnsetMs: 200ms ramp — squared shape attenuates early FDN peaks
                      // For 115ms pre-delay: ramp is at 0.33 (squared) at crest window start
    0.10f,           // lateFeedForwardLevel: reduced (0.10 vs 0.20) — less pre-diffusion bleed
    1.5f,            // crestLimitRatio: limit peaks to 1.5× RMS (~3.5dB crest)
    0, nullptr,
    kHallDattorroLeftTaps, kHallDattorroRightTaps,
};

// Preset "Clear Chamber" (cloned from Plate)
static constexpr AlgorithmConfig kPresetClearChamber = {
    "PresetClearChamber",
    { 661, 691, 719, 751, 787, 821, 853, 887,
      929, 967, 1009, 1051, 1097, 1151, 1201, 1249 },  // logarithmic prime spacing for uniform modal density
    { 0, 1, 2, 6, 7, 13, 14, 15 },   // balanced partition (sum=7412, diff=0)
    { 3, 4, 5, 8, 9, 10, 11, 12 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.65f, 0.65f,   // input diffusion: 0.65
    0.50f,           // output diffusion scale
    20000.0f,        // bandwidth: full spectrum input (no HF attenuation before FDN)
    10.00f, 0.70f,   // ER: level=10.0 (Plate needs very strong ERs to match VV onset energy)
    2.95f,           // late gain: 2.95 (DattorroTank 1/7 output needs ~10x boost vs FDN)
    0.10f, 1.0f,     // mod: 0.10 (was 0.25; reduce spectral smearing for MFCC 36→51)
    0.65f, 1.05f, 1.0f, // damping: trebleMultScale=0.65, trebleMultScaleMax=1.05 (was 1.0; slight HF sustain boost for Vocal Plate 4kHz match)
    20000.0f,        // high crossover: 20kHz (two-band — Plate trebleMult>1.0 needs uniform bright boost across all HF)
    1.0f,            // airDampingScale: 1.0 (no extra air damping — Plate needs full brightness)
    0.5f, 1.5f,      // size range
    0.35f,           // ER crossfeed: 0.35
    0.03f,           // inline diffusion: 0.03 (minimal allpass phase smearing; higher values worsen small-size presets like Ambience Plate)
    1.0f,            // mod depth floor: 1.0 = uniform modulation
    0.0f,            // structural HF damping: off
    0.0f,            // structural LF damping: off
    1.5f,            // noise mod: 1.5 (plate ringing; jitter alone can't fix structural DattorroTank modes)
    0.12f,           // Hadamard perturbation: break symmetry
    1.0f,            // ER gain exponent: inverse distance (default)
    false,           // useWeightedGains: off
    true,            // useHouseholderFeedback: on (Householder 68.4 vs Hadamard 66)
    true,            // useDattorroTank: ON (with late tap positions matching VV onset)
    false,           // useQuadTank: OFF for Plate
    1.20f,           // decay time scale: increased to match VV decay length (was 0.92, DattorroTank decays ~30-50% faster)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    2.0f, 0.9f,     // short-decay boost: +2 dB
    0.0f, 0.0f,     // gate: disabled
    18000.0f,        // ER air absorption ceiling: 18kHz
    2000.0f,         // ER air absorption floor: default
    0.0f,            // ER decorrelation: off
    14.10f,          // output gain: 14.10 (recalibrated for delay_scale, was 16.95)
    0.20f,           // stereo coupling: 0.20
    0.0f, +2.5f, 3000.0f, // output EQ: +2.5dB high shelf at 3kHz (was +1.0; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ: bypassed
    false,           // enableSaturation: off (clean linear output, matching VV)
    30.0f,           // lateOnsetMs: 30ms ramp — separates ER onset from late reverb buildup
    0.0f,            // lateFeedForwardLevel: 0 (feed-forward doesn't match VV's tap timing)
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,  // custom ER: disabled
    kPlateDattorroLeftTaps, kPlateDattorroRightTaps,  // Dattorro taps: late onset
};

// Preset "Fat Plate" (cloned from HallQuadBright)
static constexpr AlgorithmConfig kPresetFatPlate = {
    "PresetFatPlate",
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },
    { 0, 3, 5, 7, 8, 10, 12, 15 },
    { 1, 2, 4, 6, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER
    1.24f,           // late gain: same as Hall DattorroTank initially
    0.75f, 13.0f,    // mod
    0.70f, 1.50f, 1.0f, // damping: trebleMultScale 0.50→0.70
    4000.0f,         // high crossover
    0.85f,           // airDampingScale: 0.70→0.85
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping
    1.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON — key difference
    0.79f,           // decay time scale: same as Hall initially
    0.0f, 0, 1.0f,  // dual-slope
    2.0f, 1.5f,     // short-decay boost
    0.0f, 0.0f,     // gate
    20000.0f,        // ER air absorption ceiling
    18000.0f,        // ER air absorption floor
    0.55f,           // ER decorrelation
    8.53f,           // output gain
    -1.0f,           // stereo coupling
    0.0f, +4.0f, 5000.0f, // output EQ: +4.0dB high shelf (was +2.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ
    false,           // enableSaturation
    0.0f,            // lateOnsetMs
    0.20f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio: disabled (HallQuadSmooth handles crest-limited presets)
    0, nullptr,      // custom ER
    kHallDattorroLeftTaps, kHallDattorroRightTaps,
};

// Preset "Large Chamber" (cloned from AmbientQuadBright)
static constexpr AlgorithmConfig kPresetLargeChamber = {
    "PresetLargeChamber",
    { 971, 1049, 1153, 1277, 1399, 1523, 1667, 1811,
      1949, 2111, 2269, 2437, 2609, 2789, 2969, 3169 },
    { 0, 1, 2, 4, 11, 12, 13, 15 },
    { 3, 5, 6, 7, 8, 9, 10, 14 },
    { 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f },
    0.80f, 0.80f,
    0.70f,
    18000.0f,
    0.50f, 1.0f,
    0.51f,           // late gain: same as Ambient DattorroTank (QuadTank similar output)
    1.5f, 1.3f,
    1.00f, 1.50f, 1.0f, // damping: trebleMultScale 0.80→1.00
    5000.0f,
    1.45f,           // airDampingScale: 1.30→1.45
    0.5f, 1.5f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    1.0f,
    false,
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON — 4 loops instead of 2
    2.00f,           // decay time scale: 2.00 (QuadTank needs higher scale for Ambient sustain)
    0.0f, 0, 1.0f,
    0.0f, 0.0f,
    0.0f, 0.0f,
    12000.0f,
    2000.0f,
    0.0f,
    1.24f,
    0.05f,
    0.0f, +3.0f, 4000.0f,  // output EQ: +3.0dB high shelf (was +1.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,
    false,
    0.0f,
    0.15f,
    0.0f,  // crestLimitRatio: disabled
    0, nullptr,
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,
};

// Preset "Large Wood Room" (cloned from RoomFDN)
static constexpr AlgorithmConfig kPresetLargeWoodRoom = {
    "PresetLargeWoodRoom",
    { 1087, 1171, 1279, 1381, 1493, 1607, 1733, 1861,
      2003, 2153, 2309, 2467, 2633, 2801, 2963, 3137 },  // same Room delay lengths
    { 0, 1, 3, 7, 10, 11, 13, 14 },  // same balanced partition as Room
    { 2, 4, 5, 6, 8, 9, 12, 15 },
    { 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    0.75f, 0.70f,    // input diffusion: same as Room
    0.65f,           // output diffusion scale
    18000.0f,        // bandwidth
    0.80f, 0.45f,    // ER: same as Room
    0.22f,           // late gain: FDN native (no DattorroTank compensation)
    1.3f, 1.0f,      // mod: same as Room
    0.45f, 0.82f, 0.85f, // damping: same as Room
    6000.0f,         // high crossover: same as Room (three-band)
    0.75f,           // airDampingScale: same as Room
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping: same as Room
    1.5f,            // noise mod: same as Room
    0.20f,           // Hadamard perturbation: same as Room
    0.30f,           // ER gain exponent: same as Room
    false,           // useWeightedGains
    false,           // useHouseholderFeedback: off (same as Room — Hadamard better)
    false,           // useDattorroTank: OFF — native FDN
    false,           // useQuadTank: OFF
    3.00f,           // decay time scale: 3.00 (1.80 gave same ratios as DattorroTank — FDN needs much more)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    2.0f, 1.5f,     // short-decay boost: same as Hall
    0.0f, 0.0f,     // gate: disabled
    10000.0f,        // ER air absorption ceiling: same as Room
    2000.0f,         // ER air absorption floor
    0.35f,           // ER decorrelation: same as Room
    2.06f,           // output gain: same as Room initially (will recalibrate)
    0.20f,           // stereo coupling: same as Room
    0.0f, +1.5f, 4000.0f, // output EQ: +1.5dB high shelf (was -2.0; HF brightness boost)
    2200.0f, -2.0f, 1.2f, // output mid EQ: same as Room
    false,           // enableSaturation
    0.0f,            // lateOnsetMs
    0.15f,           // lateFeedForwardLevel: same as Room
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,      // custom ER: disabled
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,  // taps (unused by FDN but required)
};

// Preset "Live Vox Chamber" (cloned from ChamberQuadBright)
static constexpr AlgorithmConfig kPresetLiveVoxChamber = {
    "PresetLiveVoxChamber",
    { 751, 797, 839, 887, 947, 997, 1061, 1123,
      1187, 1259, 1327, 1409, 1493, 1583, 1669, 1777 },  // same Chamber delay lengths
    { 0, 1, 2, 7, 8, 11, 14, 15 },
    { 3, 4, 5, 6, 9, 10, 12, 13 },
    { 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f },
    0.75f, 0.625f,   // input diffusion: same as Chamber
    0.65f,           // output diffusion scale
    16000.0f,        // bandwidth
    0.55f, 0.55f,    // ER: same as Chamber
    1.23f,           // late gain: same as Chamber (QuadTank needs similar compensation)
    0.05f, 1.0f,     // mod: same as Chamber
    1.05f, 1.00f, 1.0f, // damping: trebleMultScale 0.85→1.05
    20000.0f,        // high crossover
    1.15f,           // airDampingScale: 1.0→1.15
    0.5f, 1.5f,      // size range
    0.2f,            // ER crossfeed
    0.20f,           // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    0.0f,            // structural LF damping
    1.2f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback: same as Chamber
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON — key difference
    1.80f,           // decay time scale: 1.80 (QuadTank + higher scale for sustain)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled
    0.0f, 0.0f,     // gate: disabled
    12000.0f,        // ER air absorption ceiling: same as Chamber
    2000.0f,         // ER air absorption floor
    0.50f,           // ER decorrelation: same as Chamber
    3.99f,           // output gain: same as Chamber
    0.15f,           // stereo coupling: same as Chamber
    0.0f, +4.5f, 5000.0f, // output EQ: +4.5dB high shelf (was +3.0; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ: bypassed
    false,           // enableSaturation
    0.0f,            // lateOnsetMs
    0.15f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,      // custom ER: disabled
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,  // taps (unused by QuadTank but required)
};

// Preset "Medium Gate" (cloned from AmbientQuadBright)
static constexpr AlgorithmConfig kPresetMediumGate = {
    "PresetMediumGate",
    { 971, 1049, 1153, 1277, 1399, 1523, 1667, 1811,
      1949, 2111, 2269, 2437, 2609, 2789, 2969, 3169 },
    { 0, 1, 2, 4, 11, 12, 13, 15 },
    { 3, 5, 6, 7, 8, 9, 10, 14 },
    { 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f },
    0.80f, 0.80f,
    0.70f,
    18000.0f,
    0.50f, 1.0f,
    0.51f,           // late gain: same as Ambient DattorroTank (QuadTank similar output)
    1.5f, 1.3f,
    1.00f, 1.50f, 1.0f, // damping: trebleMultScale 0.80→1.00
    5000.0f,
    1.45f,           // airDampingScale: 1.30→1.45
    0.5f, 1.5f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    1.0f,
    false,
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON — 4 loops instead of 2
    2.00f,           // decay time scale: 2.00 (QuadTank needs higher scale for Ambient sustain)
    0.0f, 0, 1.0f,
    0.0f, 0.0f,
    0.0f, 0.0f,
    12000.0f,
    2000.0f,
    0.0f,
    1.24f,
    0.05f,
    0.0f, +3.0f, 4000.0f,  // output EQ: +3.0dB high shelf (was +1.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,
    false,
    0.0f,
    0.15f,
    0.0f,  // crestLimitRatio: disabled
    0, nullptr,
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,
};

// Preset "Rich Chamber" (cloned from HallQuadSmooth)
static constexpr AlgorithmConfig kPresetRichChamber = {
    "PresetRichChamber",
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },
    { 0, 3, 5, 7, 8, 10, 12, 15 },
    { 1, 2, 4, 6, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.65f, 0.55f,    // input diffusion: increased (more smearing for smoother onset)
    0.50f,           // output diffusion scale: increased (smoother peaks)
    20000.0f,        // bandwidth
    0.30f, 0.85f,    // ER: reduced erLevelScale (0.30 vs 0.65) to minimize ER spike
    1.24f,           // late gain
    0.75f, 13.0f,    // mod
    0.50f, 1.50f, 1.0f, // damping
    4000.0f,         // high crossover
    0.70f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.05f,           // ER crossfeed: reduced to limit ER→QuadTank bleed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping
    1.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON
    0.79f,           // decay time scale: same as HallQuad
    0.0f, 0, 1.0f,  // dual-slope
    2.0f, 1.5f,     // short-decay boost
    0.0f, 0.0f,     // gate
    20000.0f,        // ER air absorption ceiling
    18000.0f,        // ER air absorption floor
    0.55f,           // ER decorrelation
    8.53f,           // output gain
    -1.0f,           // stereo coupling
    0.0f, +2.0f, 5000.0f, // output EQ
    0.0f, 0.0f, 0.7f,   // output mid EQ
    false,           // enableSaturation
    100.0f,          // lateOnsetMs: 100ms ramp attenuates early QuadTank peaks
    0.10f,           // lateFeedForwardLevel: reduced (less pre-diffusion bleed)
    1.5f,            // crestLimitRatio: limit peaks to 1.5× RMS
    0, nullptr,      // custom ER
    kHallDattorroLeftTaps, kHallDattorroRightTaps,
};

// Preset "Small Chamber1" (cloned from Hall)
static constexpr AlgorithmConfig kPresetSmallChamber1 = {
    "PresetSmallChamber1",
    // delay lengths: 1801-5521 samples (41-125ms @ 44.1kHz) — wide spread for concert hall character
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },  // logarithmic prime spacing for uniform modal density
    { 0, 3, 5, 7, 8, 10, 12, 15 },     // leftTaps: balanced partition (sum=26710, diff=52 samples ≈ 1ms)
    { 1, 2, 4, 6, 9, 11, 13, 14 },     // rightTaps: balanced partition (sum=26762)
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },  // alternating signs for decorrelation
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion: 0.55/0.45 (was 0.75/0.625 — reduced for sparser early onset)
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth: full spectrum input
    0.65f, 0.85f,    // ER: level=0.65 (was 0.90; reduced post-ER-bypass for crest balance), timeScale=0.85
    1.24f,           // late gain: 1.24 (DattorroTank Hall ~13dB quieter than FDN)
    0.75f, 13.0f,    // mod: modDepthScale=0.75 (DV 33% too deep), modRateScale=13.0 (Hilbert: VV=5.4Hz vs DV=0.4Hz)
    0.50f, 1.50f, 1.0f, // damping: trebleMultScale=0.50
    4000.0f,         // high crossover: 4kHz (3kHz regressed — pushed too much into air band, kurtosis diverged)
    0.70f,           // airDampingScale: 0.70
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed: 0.10
    0.0f,            // inline diffusion: off (short allpasses tested at 0.20-0.50 — no measurable effect on peaks)
                     // Long allpasses (41-131) wrecked centroid_late. Multi-point output tapping handles density.
    1.0f,            // mod depth floor: 1.0 = uniform modulation
    0.0f,            // structural HF damping: off
    40.0f,           // structural LF damping: 40Hz HP (90Hz destroyed bass ratio and raised centroid — output EQ handles 80-250Hz instead)
    1.0f,            // noise mod: 1.0 (hall ringing)
    0.10f,           // Hadamard perturbation: 0.10 (0.05 wasn't enough — still audible repetition in tail)
    1.0f,            // ER gain exponent: 1.0 inverse distance (1.5 caused audible pulse after transients)
    false,           // useWeightedGains: off (true regressed decay_shape 100→74.4, total 75.4→73.1)
    true,            // useHouseholderFeedback: on (keep true per instructions)
    true,            // useDattorroTank: ON (with late tap positions matching VV onset)
    false,           // useQuadTank: OFF for Hall (tested — regressed Huge Synth Hall, no net improvement)
    0.79f,           // decay time scale: recalibrated for DattorroTank round 2
    0.0f, 0, 1.0f,  // dual-slope: disabled
    2.0f, 1.5f,     // short-decay boost: +2.0 dB (4.5 contributed to transient pulse)
    0.0f, 0.0f,     // gate: disabled
    20000.0f,        // ER air absorption ceiling: 20kHz (earliest taps: no filtering)
    18000.0f,        // ER air absorption floor: 18kHz
    0.55f,           // ER decorrelation: strong allpass decorrelation (IACC 0.747→target 0.321)
    8.53f,           // output gain: 8.53 (recalibrated for delay_scale, was 11.91)
    -1.0f,           // stereo coupling: -1.0 = full 16×16 Hadamard (was 0.15 split 8+8)
    0.0f,            // output low shelf: 0dB at 250Hz (neutral — sweeper shows DV already +2-4dB hotter in low end)
    +5.5f,           // output high shelf: +5.5dB at 5kHz (was +4.0; +1.5dB HF boost)
    5000.0f,         // output high shelf freq: 5kHz
    0.0f,            // output mid EQ: bypassed
    0.0f,            // output mid EQ: bypassed
    0.7f,            // output mid EQ: Q (bypassed)
    false,           // enableSaturation: off (clean linear output, matching VV)
    0.0f,            // lateOnsetMs: disabled
    0.20f,           // lateFeedForwardLevel: blend pre-diffusion late reverb
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,  // custom ER: disabled
    kHallDattorroLeftTaps, kHallDattorroRightTaps,  // Dattorro taps: deepest onset
};

// Preset "Small Chamber2" (cloned from ChamberQuadSustain)
static constexpr AlgorithmConfig kPresetSmallChamber2 = {
    "PresetSmallChamber2",
    { 751, 797, 839, 887, 947, 997, 1061, 1123,
      1187, 1259, 1327, 1409, 1493, 1583, 1669, 1777 },  // Chamber delay lengths
    { 0, 1, 2, 7, 8, 11, 14, 15 },
    { 3, 4, 5, 6, 9, 10, 12, 13 },
    { 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f },
    0.75f, 0.625f,
    0.65f,
    16000.0f,
    0.55f, 0.55f,
    1.23f,
    0.05f, 1.0f,
    0.85f, 1.00f, 1.0f,
    20000.0f,
    1.0f,
    0.5f, 1.5f,
    0.2f,
    0.20f,
    1.0f,
    0.0f,
    0.0f,
    1.2f,
    0.10f,
    1.0f,
    false,
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON
    3.50f,           // decay time scale: 3.50 (extreme sustain for 0.30-0.50x presets)
    0.0f, 0, 1.0f,
    0.0f, 0.0f,
    0.0f, 0.0f,
    12000.0f,
    2000.0f,
    0.50f,
    3.99f,
    0.15f,
    0.0f, +3.5f, 5000.0f, // output EQ: +3.5dB high shelf (was +2.0; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,
    false,
    0.0f,
    0.15f,
    0.0f,  // crestLimitRatio: disabled
    0, nullptr,
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,
};

// Preset "Snare Plate" (cloned from ChamberQuadBright)
static constexpr AlgorithmConfig kPresetSnarePlate = {
    "PresetSnarePlate",
    { 751, 797, 839, 887, 947, 997, 1061, 1123,
      1187, 1259, 1327, 1409, 1493, 1583, 1669, 1777 },  // same Chamber delay lengths
    { 0, 1, 2, 7, 8, 11, 14, 15 },
    { 3, 4, 5, 6, 9, 10, 12, 13 },
    { 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f },
    0.75f, 0.625f,   // input diffusion: same as Chamber
    0.65f,           // output diffusion scale
    16000.0f,        // bandwidth
    0.55f, 0.55f,    // ER: same as Chamber
    1.23f,           // late gain: same as Chamber (QuadTank needs similar compensation)
    0.05f, 1.0f,     // mod: same as Chamber
    1.05f, 1.00f, 1.0f, // damping: trebleMultScale 0.85→1.05
    20000.0f,        // high crossover
    1.15f,           // airDampingScale: 1.0→1.15
    0.5f, 1.5f,      // size range
    0.2f,            // ER crossfeed
    0.20f,           // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    0.0f,            // structural LF damping
    1.2f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback: same as Chamber
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON — key difference
    1.80f,           // decay time scale: 1.80 (QuadTank + higher scale for sustain)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled
    0.0f, 0.0f,     // gate: disabled
    12000.0f,        // ER air absorption ceiling: same as Chamber
    2000.0f,         // ER air absorption floor
    0.50f,           // ER decorrelation: same as Chamber
    3.99f,           // output gain: same as Chamber
    0.15f,           // stereo coupling: same as Chamber
    0.0f, +4.5f, 5000.0f, // output EQ: +4.5dB high shelf (was +3.0; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ: bypassed
    false,           // enableSaturation
    0.0f,            // lateOnsetMs
    0.15f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,      // custom ER: disabled
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,  // taps (unused by QuadTank but required)
};

// Preset "Thin Plate" (cloned from ChamberQuadSustainHybrid)
static constexpr AlgorithmConfig kPresetThinPlate = {
    "PresetThinPlate",
    { 751, 797, 839, 887, 947, 997, 1061, 1123,
      1187, 1259, 1327, 1409, 1493, 1583, 1669, 1777 },
    { 0, 1, 2, 7, 8, 11, 14, 15 },
    { 3, 4, 5, 6, 9, 10, 12, 13 },
    { 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f },
    0.75f, 0.625f,
    0.65f,
    16000.0f,
    0.55f, 0.55f,
    1.23f,
    0.05f, 1.0f,
    0.85f, 1.00f, 1.0f,
    20000.0f,
    1.0f,
    0.5f, 1.5f,
    0.2f,
    0.20f,
    1.0f,
    0.0f,
    0.0f,
    1.2f,
    0.10f,
    1.0f,
    false,
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON
    3.50f,           // decay time scale
    0.0f, 0, 1.0f,
    0.0f, 0.0f,
    0.0f, 0.0f,
    12000.0f,
    2000.0f,
    0.50f,
    3.99f,
    0.15f,
    0.0f, +10.0f, 5000.0f, // output EQ: +10.0dB high shelf (very aggressive — compensates for dark QuadTank)
    0.0f, 0.0f, 0.7f,
    false,
    0.0f,
    0.15f,
    0.0f,  // crestLimitRatio: disabled
    0, nullptr,
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,
    1.0f,            // dattorroDelayScale
    0.0f,            // hybridBlend: disabled (output shelf approach instead)
    -1,              // hybridSecondaryAlgo: disabled
};

// Preset "Tiled Room" (cloned from Chamber)
static constexpr AlgorithmConfig kPresetTiledRoom = {
    "PresetTiledRoom",
    { 751, 797, 839, 887, 947, 997, 1061, 1123,
      1187, 1259, 1327, 1409, 1493, 1583, 1669, 1777 },  // logarithmic prime spacing for uniform modal density
    { 0, 1, 2, 7, 8, 11, 14, 15 },   // balanced partition (sum=9552, diff=2)
    { 3, 4, 5, 6, 9, 10, 12, 13 },
    { 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f },
    0.75f, 0.625f,   // input diffusion: Dattorro split
    0.65f,           // output diffusion scale
    16000.0f,        // bandwidth: 16kHz
    0.55f, 0.55f,    // ER: level=0.55 (was 1.20; cut for +5.6dB peak excess), tight timing
    1.23f,           // late gain: 1.23 (DattorroTank Chamber ~9dB quieter)
    0.05f, 1.0f,     // mod: 0.05 (was 0.4; reducing modulation was the single biggest MFCC/spectral_eq win)
    0.85f, 1.00f, 1.0f, // damping: trebleMultScale=0.85 (was 0.90), trebleMultScaleMax=1.00 (was 0.90; 7/13 Chamber HF too dark)
    20000.0f,        // high crossover: 20kHz (effectively two-band — mid and air bands unified)
    1.0f,            // airDampingScale: 1.0 (no extra air damping)
    0.5f, 1.5f,      // size range
    0.2f,            // ER crossfeed: medium
    0.20f,           // inline diffusion: 0.20 (was 0.30; slight improvement)
    1.0f,            // mod depth floor: 1.0 = uniform modulation
    0.0f,            // structural HF damping: off
    0.0f,            // structural LF damping: off
    1.2f,            // noise mod: 1.2 (chamber ringing)
    0.10f,           // Hadamard perturbation: break symmetry
    1.0f,            // ER gain exponent: inverse distance (default)
    false,           // useWeightedGains: off
    true,            // useHouseholderFeedback: on (Householder 65.0 vs Hadamard 63.4)
    true,            // useDattorroTank: ON (with late tap positions matching VV onset)
    false,           // useQuadTank: OFF for Chamber
    0.88f,           // decay time scale: recalibrated for DattorroTank (was 0.99 for FDN)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled (Chamber has static lateGainScale tuning)
    0.0f, 0.0f,     // gate: disabled
    12000.0f,        // ER air absorption ceiling: default
    2000.0f,         // ER air absorption floor: default
    0.50f,           // ER decorrelation: strong (reduce early IACC from 0.617 toward 0.241)
    3.99f,           // output gain: 3.99 (recalibrated for delay_scale, was 8.15)
    0.15f,           // stereo coupling: moderate (wider stereo for IACC match)
    0.0f, +3.0f, 5000.0f, // output EQ: +3.0dB high shelf at 5kHz (was +1.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ: bypassed
    false,           // enableSaturation: off (clean linear output, matching VV)
    0.0f,            // lateOnsetMs: disabled
    0.15f,           // lateFeedForwardLevel: blend pre-diffusion late reverb
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,  // custom ER: disabled
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,  // Dattorro taps: mid-deep onset
};

// Preset "Ambience" (cloned from Ambient)
static constexpr AlgorithmConfig kPresetAmbience = {
    "PresetAmbience",
    { 971, 1049, 1153, 1277, 1399, 1523, 1667, 1811,
      1949, 2111, 2269, 2437, 2609, 2789, 2969, 3169 },
    { 0, 1, 2, 4, 11, 12, 13, 15 },  // balanced partition (sum=15576, diff=0)
    { 3, 5, 6, 7, 8, 9, 10, 14 },
    { 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f },
    0.80f, 0.80f,    // input diffusion: maximum
    0.70f,           // output diffusion scale
    18000.0f,        // bandwidth: wide input (let full spectrum in for bright Ambient)
    0.50f, 1.0f,     // ER: level=0.50 (was 0.0; enabled for custom ER taps — VV Ambient presets have early energy)
    0.51f,           // late gain: 0.51 (DattorroTank Ambient ~3dB quieter)
    1.5f, 1.3f,      // mod: heavy depth and rate
    0.80f, 1.50f, 1.0f, // damping: trebleMultScale=0.80, trebleMultScaleMax=1.50 (mid band identical to old 2-band)
    5000.0f,         // high crossover: 5kHz (3-band: bass < 241Hz, mid 241-5k, air > 5k)
    1.30f,           // airDampingScale: 1.30 (air > 5kHz decays slower — extra HF boost for Ambience match)
    0.5f, 1.5f,      // size range
    0.0f,            // ER crossfeed: off (no ERs)
    0.0f,            // inline diffusion: off (0.02 moved peak from 10kHz to 1kHz without reducing it)
    1.0f,            // mod depth floor: 1.0 = uniform modulation
    0.0f,            // structural HF damping: off (Ambient HF slightly fast; trebleMultScale=0.60 handles HF)
    0.0f,            // structural LF damping: off
    1.0f,            // noise mod: 1.0 (ambient ringing)
    0.0f,            // Hadamard perturbation: off (Ambient uses Householder)
    1.0f,            // ER gain exponent: inverse distance (default)
    false,           // useWeightedGains: off
    true,            // useHouseholderFeedback: on (Householder 82.4 vs Hadamard 79.4; more stable across presets)
    true,            // useDattorroTank: ON (with late tap positions matching VV onset)
    false,           // useQuadTank: OFF for Ambient
    0.98f,           // decay time scale: recalibrated for DattorroTank (was 0.99 for FDN)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled (Ambient already matched)
    0.0f, 0.0f,     // gate: disabled
    12000.0f,        // ER air absorption ceiling: default
    2000.0f,         // ER air absorption floor: default (no ERs in Ambient)
    0.0f,            // ER decorrelation: off
    1.24f,           // output gain: 1.24 (recalibrated for delay_scale, was 0.72)
    0.05f,           // stereo coupling: minimal (0.0 didn't help width enough)
    0.0f, +1.5f, 4000.0f, // output EQ: +1.5dB high shelf at 4kHz (was -0.5; +2.0dB centroid boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ: bypassed
    false,           // enableSaturation: off (clean linear output, matching VV)
    0.0f,            // lateOnsetMs: 0ms (Ambient onset already late — Ts=+32ms)
    0.15f,           // lateFeedForwardLevel: blend pre-diffusion late reverb
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,  // custom ER: disabled
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,  // Dattorro taps: mid-deep onset
};

// Preset "Ambience Plate" (cloned from AmbientQuadBright)
static constexpr AlgorithmConfig kPresetAmbiencePlate = {
    "PresetAmbiencePlate",
    { 971, 1049, 1153, 1277, 1399, 1523, 1667, 1811,
      1949, 2111, 2269, 2437, 2609, 2789, 2969, 3169 },
    { 0, 1, 2, 4, 11, 12, 13, 15 },
    { 3, 5, 6, 7, 8, 9, 10, 14 },
    { 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f },
    0.80f, 0.80f,
    0.70f,
    18000.0f,
    0.50f, 1.0f,
    0.51f,           // late gain: same as Ambient DattorroTank (QuadTank similar output)
    1.5f, 1.3f,
    1.00f, 1.50f, 1.0f, // damping: trebleMultScale 0.80→1.00
    5000.0f,
    1.45f,           // airDampingScale: 1.30→1.45
    0.5f, 1.5f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    1.0f,
    false,
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON — 4 loops instead of 2
    2.00f,           // decay time scale: 2.00 (QuadTank needs higher scale for Ambient sustain)
    0.0f, 0, 1.0f,
    0.0f, 0.0f,
    0.0f, 0.0f,
    12000.0f,
    2000.0f,
    0.0f,
    1.24f,
    0.05f,
    0.0f, +3.0f, 4000.0f,  // output EQ: +3.0dB high shelf (was +1.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,
    false,
    0.0f,
    0.15f,
    0.0f,  // crestLimitRatio: disabled
    0, nullptr,
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,
};

// Preset "Ambience Tiled Room" (cloned from Ambient)
static constexpr AlgorithmConfig kPresetAmbienceTiledRoom = {
    "PresetAmbienceTiledRoom",
    { 971, 1049, 1153, 1277, 1399, 1523, 1667, 1811,
      1949, 2111, 2269, 2437, 2609, 2789, 2969, 3169 },
    { 0, 1, 2, 4, 11, 12, 13, 15 },  // balanced partition (sum=15576, diff=0)
    { 3, 5, 6, 7, 8, 9, 10, 14 },
    { 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f },
    0.80f, 0.80f,    // input diffusion: maximum
    0.70f,           // output diffusion scale
    18000.0f,        // bandwidth: wide input (let full spectrum in for bright Ambient)
    0.50f, 1.0f,     // ER: level=0.50 (was 0.0; enabled for custom ER taps — VV Ambient presets have early energy)
    0.51f,           // late gain: 0.51 (DattorroTank Ambient ~3dB quieter)
    1.5f, 1.3f,      // mod: heavy depth and rate
    0.80f, 1.50f, 1.0f, // damping: trebleMultScale=0.80, trebleMultScaleMax=1.50 (mid band identical to old 2-band)
    5000.0f,         // high crossover: 5kHz (3-band: bass < 241Hz, mid 241-5k, air > 5k)
    1.30f,           // airDampingScale: 1.30 (air > 5kHz decays slower — extra HF boost for Ambience match)
    0.5f, 1.5f,      // size range
    0.0f,            // ER crossfeed: off (no ERs)
    0.0f,            // inline diffusion: off (0.02 moved peak from 10kHz to 1kHz without reducing it)
    1.0f,            // mod depth floor: 1.0 = uniform modulation
    0.0f,            // structural HF damping: off (Ambient HF slightly fast; trebleMultScale=0.60 handles HF)
    0.0f,            // structural LF damping: off
    1.0f,            // noise mod: 1.0 (ambient ringing)
    0.0f,            // Hadamard perturbation: off (Ambient uses Householder)
    1.0f,            // ER gain exponent: inverse distance (default)
    false,           // useWeightedGains: off
    true,            // useHouseholderFeedback: on (Householder 82.4 vs Hadamard 79.4; more stable across presets)
    true,            // useDattorroTank: ON (with late tap positions matching VV onset)
    false,           // useQuadTank: OFF for Ambient
    0.98f,           // decay time scale: recalibrated for DattorroTank (was 0.99 for FDN)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled (Ambient already matched)
    0.0f, 0.0f,     // gate: disabled
    12000.0f,        // ER air absorption ceiling: default
    2000.0f,         // ER air absorption floor: default (no ERs in Ambient)
    0.0f,            // ER decorrelation: off
    1.24f,           // output gain: 1.24 (recalibrated for delay_scale, was 0.72)
    0.05f,           // stereo coupling: minimal (0.0 didn't help width enough)
    0.0f, +1.5f, 4000.0f, // output EQ: +1.5dB high shelf at 4kHz (was -0.5; +2.0dB centroid boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ: bypassed
    false,           // enableSaturation: off (clean linear output, matching VV)
    0.0f,            // lateOnsetMs: 0ms (Ambient onset already late — Ts=+32ms)
    0.15f,           // lateFeedForwardLevel: blend pre-diffusion late reverb
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,  // custom ER: disabled
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,  // Dattorro taps: mid-deep onset
};

// Preset "Big Ambience Gate" (cloned from HallQuadBright)
static constexpr AlgorithmConfig kPresetBigAmbienceGate = {
    "PresetBigAmbienceGate",
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },
    { 0, 3, 5, 7, 8, 10, 12, 15 },
    { 1, 2, 4, 6, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER
    1.24f,           // late gain: same as Hall DattorroTank initially
    0.75f, 13.0f,    // mod
    0.70f, 1.50f, 1.0f, // damping: trebleMultScale 0.50→0.70
    4000.0f,         // high crossover
    0.85f,           // airDampingScale: 0.70→0.85
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping
    1.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON — key difference
    0.79f,           // decay time scale: same as Hall initially
    0.0f, 0, 1.0f,  // dual-slope
    2.0f, 1.5f,     // short-decay boost
    0.0f, 0.0f,     // gate
    20000.0f,        // ER air absorption ceiling
    18000.0f,        // ER air absorption floor
    0.55f,           // ER decorrelation
    8.53f,           // output gain
    -1.0f,           // stereo coupling
    0.0f, +4.0f, 5000.0f, // output EQ: +4.0dB high shelf (was +2.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ
    false,           // enableSaturation
    0.0f,            // lateOnsetMs
    0.20f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio: disabled (HallQuadSmooth handles crest-limited presets)
    0, nullptr,      // custom ER
    kHallDattorroLeftTaps, kHallDattorroRightTaps,
};

// Preset "Cross Stick Room" (cloned from RoomBright)
static constexpr AlgorithmConfig kPresetCrossStickRoom = {
    "PresetCrossStickRoom",
    { 1087, 1171, 1279, 1381, 1493, 1607, 1733, 1861,
      2003, 2153, 2309, 2467, 2633, 2801, 2963, 3137 },
    { 0, 1, 3, 7, 10, 11, 13, 14 },  // balanced partition (sum=16040, diff=2)
    { 2, 4, 5, 6, 8, 9, 12, 15 },
    { 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    0.75f, 0.70f,    // input diffusion: moderate-high
    0.65f,           // output diffusion scale
    18000.0f,        // bandwidth: wide input (structural HF damping handles per-loop rolloff)
    0.80f, 0.45f,    // ER: level=0.80 (was 1.40; reduced for -6dB crest deficit), tighter timing
    0.63f,           // late gain: 0.63 (DattorroTank Room ~3dB quieter)
    1.3f, 1.0f,      // mod: wider stereo (modDepthScale=1.3, modRateScale=1.0)
    0.65f, 0.82f, 0.85f, // damping: trebleMultScale 0.45→0.65
    6000.0f,         // high crossover: 6kHz (three-band: bass/mid/air — independent air band control)
    0.90f,           // airDampingScale: 0.75→0.90
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed: light
    0.0f,            // inline diffusion: off (0.20 crashed MFCC 56→17)
    1.0f,            // mod depth floor: uniform
    0.0f,            // structural HF damping: off (14kHz tried, decay calibration compensated by raising feedback → worse ringing)
    40.0f,           // structural LF damping: 40Hz HP to gently tame bass RT60 inflation
    1.5f,            // noise mod: 1.5 (room ringing)
    0.20f,           // Hadamard perturbation: 0.20 (was 0.08→0.12; stronger to detune 11kHz mode in Exciting Snare)
    0.30f,           // ER gain exponent: gentle rolloff (more energy in later taps)
    false,           // useWeightedGains: off
    false,           // useHouseholderFeedback: off (Room — Householder regressed 75.8 vs Hadamard 77.1)
    true,            // useDattorroTank: ON (with late tap positions matching VV onset)
    false,           // useQuadTank: OFF for Room
    1.37f,           // decay time scale: recalibrated for DattorroTank
    0.0f, 0, 1.0f,   // dual-slope: disabled (standard 16-channel FDN for matched tail energy)
    0.0f, 0.0f,      // short-decay boost: disabled (Room uses lateGainScale calibration)
    0.0f, 0.0f,      // gate: disabled
    10000.0f,         // ER air absorption ceiling: darken earliest taps slightly (centroid_early 6207→5521)
    2000.0f,          // ER air absorption floor: default
    0.35f,            // ER decorrelation: moderate (improve L/R spectral variation for MFCC)
    2.06f,            // output gain: 2.06 (recalibrated for delay_scale, was 1.20)
    0.20f,            // stereo coupling: moderate (rooms have natural coupling from walls)
    0.0f, +2.0f, 4000.0f, // output EQ: +2.0dB high shelf at 4kHz (was +0.5; +1.5dB centroid boost)
    2200.0f, -2.0f, 1.2f, // output mid EQ: -2dB notch at 2.2kHz (suppress measured 2215Hz ringing)
    false,           // enableSaturation: off (clean linear output, matching VV)
    0.0f,            // lateOnsetMs: disabled
    0.15f,           // lateFeedForwardLevel: blend pre-diffusion late reverb
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,  // custom ER: disabled
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,  // Dattorro taps: mid-deep onset
};

// Preset "Drum Air" (cloned from Chamber)
static constexpr AlgorithmConfig kPresetDrumAir = {
    "PresetDrumAir",
    { 751, 797, 839, 887, 947, 997, 1061, 1123,
      1187, 1259, 1327, 1409, 1493, 1583, 1669, 1777 },  // logarithmic prime spacing for uniform modal density
    { 0, 1, 2, 7, 8, 11, 14, 15 },   // balanced partition (sum=9552, diff=2)
    { 3, 4, 5, 6, 9, 10, 12, 13 },
    { 1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, -1.0f },
    0.75f, 0.625f,   // input diffusion: Dattorro split
    0.65f,           // output diffusion scale
    16000.0f,        // bandwidth: 16kHz
    0.55f, 0.55f,    // ER: level=0.55 (was 1.20; cut for +5.6dB peak excess), tight timing
    1.23f,           // late gain: 1.23 (DattorroTank Chamber ~9dB quieter)
    0.05f, 1.0f,     // mod: 0.05 (was 0.4; reducing modulation was the single biggest MFCC/spectral_eq win)
    0.85f, 1.00f, 1.0f, // damping: trebleMultScale=0.85 (was 0.90), trebleMultScaleMax=1.00 (was 0.90; 7/13 Chamber HF too dark)
    20000.0f,        // high crossover: 20kHz (effectively two-band — mid and air bands unified)
    1.0f,            // airDampingScale: 1.0 (no extra air damping)
    0.5f, 1.5f,      // size range
    0.2f,            // ER crossfeed: medium
    0.20f,           // inline diffusion: 0.20 (was 0.30; slight improvement)
    1.0f,            // mod depth floor: 1.0 = uniform modulation
    0.0f,            // structural HF damping: off
    0.0f,            // structural LF damping: off
    1.2f,            // noise mod: 1.2 (chamber ringing)
    0.10f,           // Hadamard perturbation: break symmetry
    1.0f,            // ER gain exponent: inverse distance (default)
    false,           // useWeightedGains: off
    true,            // useHouseholderFeedback: on (Householder 65.0 vs Hadamard 63.4)
    true,            // useDattorroTank: ON (with late tap positions matching VV onset)
    false,           // useQuadTank: OFF for Chamber
    0.88f,           // decay time scale: recalibrated for DattorroTank (was 0.99 for FDN)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled (Chamber has static lateGainScale tuning)
    0.0f, 0.0f,     // gate: disabled
    12000.0f,        // ER air absorption ceiling: default
    2000.0f,         // ER air absorption floor: default
    0.50f,           // ER decorrelation: strong (reduce early IACC from 0.617 toward 0.241)
    3.99f,           // output gain: 3.99 (recalibrated for delay_scale, was 8.15)
    0.15f,           // stereo coupling: moderate (wider stereo for IACC match)
    0.0f, +3.0f, 5000.0f, // output EQ: +3.0dB high shelf at 5kHz (was +1.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ: bypassed
    false,           // enableSaturation: off (clean linear output, matching VV)
    0.0f,            // lateOnsetMs: disabled
    0.15f,           // lateFeedForwardLevel: blend pre-diffusion late reverb
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,  // custom ER: disabled
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,  // Dattorro taps: mid-deep onset
};

// Preset "Gated Snare" (cloned from AmbientFDNBright)
static constexpr AlgorithmConfig kPresetGatedSnare = {
    "PresetGatedSnare",
    { 971, 1049, 1153, 1277, 1399, 1523, 1667, 1811,
      1949, 2111, 2269, 2437, 2609, 2789, 2969, 3169 },  // same Ambient delay lengths
    { 0, 1, 2, 4, 11, 12, 13, 15 },
    { 3, 5, 6, 7, 8, 9, 10, 14 },
    { 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f },
    0.80f, 0.80f,    // input diffusion: maximum
    0.70f,           // output diffusion scale
    18000.0f,        // bandwidth
    0.50f, 1.0f,     // ER: same as Ambient
    0.22f,           // late gain: FDN native (no DattorroTank compensation)
    1.5f, 1.3f,      // mod: heavy depth and rate (same as Ambient)
    1.00f, 1.50f, 1.0f, // damping: trebleMultScale 0.80→1.00
    5000.0f,         // high crossover
    1.45f,           // airDampingScale: 1.30→1.45
    0.5f, 1.5f,      // size range
    0.0f,            // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    0.0f,            // structural LF damping
    1.0f,            // noise mod
    0.0f,            // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback: on
    false,           // useDattorroTank: OFF — native FDN
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale: 1.50 (FDN typically needs boost)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost
    0.0f, 0.0f,     // gate
    12000.0f,
    2000.0f,
    0.0f,            // ER decorrelation: off
    1.24f,           // output gain: same as Ambient initially
    0.05f,           // stereo coupling
    0.0f, +4.0f, 4000.0f, // output EQ: +4.0dB high shelf (was +2.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,
    false,
    0.0f,
    0.15f,           // lateFeedForwardLevel
    3.0f,            // crestLimitRatio: 3.0x — stronger peak limiting for Drum Air crest stability
    0, nullptr,
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,
};

// Preset "Large Ambience" (cloned from HallQuadBright)
static constexpr AlgorithmConfig kPresetLargeAmbience = {
    "PresetLargeAmbience",
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },
    { 0, 3, 5, 7, 8, 10, 12, 15 },
    { 1, 2, 4, 6, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER
    1.24f,           // late gain: same as Hall DattorroTank initially
    0.75f, 13.0f,    // mod
    0.70f, 1.50f, 1.0f, // damping: trebleMultScale 0.50→0.70
    4000.0f,         // high crossover
    0.85f,           // airDampingScale: 0.70→0.85
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping
    1.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON — key difference
    0.79f,           // decay time scale: same as Hall initially
    0.0f, 0, 1.0f,  // dual-slope
    2.0f, 1.5f,     // short-decay boost
    0.0f, 0.0f,     // gate
    20000.0f,        // ER air absorption ceiling
    18000.0f,        // ER air absorption floor
    0.55f,           // ER decorrelation
    8.53f,           // output gain
    -1.0f,           // stereo coupling
    0.0f, +4.0f, 5000.0f, // output EQ: +4.0dB high shelf (was +2.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ
    false,           // enableSaturation
    0.0f,            // lateOnsetMs
    0.20f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio: disabled (HallQuadSmooth handles crest-limited presets)
    0, nullptr,      // custom ER
    kHallDattorroLeftTaps, kHallDattorroRightTaps,
};

// Preset "Large Gated Snare" (cloned from Plate)
static constexpr AlgorithmConfig kPresetLargeGatedSnare = {
    "PresetLargeGatedSnare",
    { 661, 691, 719, 751, 787, 821, 853, 887,
      929, 967, 1009, 1051, 1097, 1151, 1201, 1249 },  // logarithmic prime spacing for uniform modal density
    { 0, 1, 2, 6, 7, 13, 14, 15 },   // balanced partition (sum=7412, diff=0)
    { 3, 4, 5, 8, 9, 10, 11, 12 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.65f, 0.65f,   // input diffusion: 0.65
    0.50f,           // output diffusion scale
    20000.0f,        // bandwidth: full spectrum input (no HF attenuation before FDN)
    10.00f, 0.70f,   // ER: level=10.0 (Plate needs very strong ERs to match VV onset energy)
    2.95f,           // late gain: 2.95 (DattorroTank 1/7 output needs ~10x boost vs FDN)
    0.10f, 1.0f,     // mod: 0.10 (was 0.25; reduce spectral smearing for MFCC 36→51)
    0.65f, 1.05f, 1.0f, // damping: trebleMultScale=0.65, trebleMultScaleMax=1.05 (was 1.0; slight HF sustain boost for Vocal Plate 4kHz match)
    20000.0f,        // high crossover: 20kHz (two-band — Plate trebleMult>1.0 needs uniform bright boost across all HF)
    1.0f,            // airDampingScale: 1.0 (no extra air damping — Plate needs full brightness)
    0.5f, 1.5f,      // size range
    0.35f,           // ER crossfeed: 0.35
    0.03f,           // inline diffusion: 0.03 (minimal allpass phase smearing; higher values worsen small-size presets like Ambience Plate)
    1.0f,            // mod depth floor: 1.0 = uniform modulation
    0.0f,            // structural HF damping: off
    0.0f,            // structural LF damping: off
    1.5f,            // noise mod: 1.5 (plate ringing; jitter alone can't fix structural DattorroTank modes)
    0.12f,           // Hadamard perturbation: break symmetry
    1.0f,            // ER gain exponent: inverse distance (default)
    false,           // useWeightedGains: off
    true,            // useHouseholderFeedback: on (Householder 68.4 vs Hadamard 66)
    true,            // useDattorroTank: ON (with late tap positions matching VV onset)
    false,           // useQuadTank: OFF for Plate
    1.20f,           // decay time scale: increased to match VV decay length (was 0.92, DattorroTank decays ~30-50% faster)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    2.0f, 0.9f,     // short-decay boost: +2 dB
    0.0f, 0.0f,     // gate: disabled
    18000.0f,        // ER air absorption ceiling: 18kHz
    2000.0f,         // ER air absorption floor: default
    0.0f,            // ER decorrelation: off
    14.10f,          // output gain: 14.10 (recalibrated for delay_scale, was 16.95)
    0.20f,           // stereo coupling: 0.20
    0.0f, +2.5f, 3000.0f, // output EQ: +2.5dB high shelf at 3kHz (was +1.0; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ: bypassed
    false,           // enableSaturation: off (clean linear output, matching VV)
    30.0f,           // lateOnsetMs: 30ms ramp — separates ER onset from late reverb buildup
    0.0f,            // lateFeedForwardLevel: 0 (feed-forward doesn't match VV's tap timing)
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,  // custom ER: disabled
    kPlateDattorroLeftTaps, kPlateDattorroRightTaps,  // Dattorro taps: late onset
};

// Preset "Med Ambience" (cloned from AmbientQuadBright)
static constexpr AlgorithmConfig kPresetMedAmbience = {
    "PresetMedAmbience",
    { 971, 1049, 1153, 1277, 1399, 1523, 1667, 1811,
      1949, 2111, 2269, 2437, 2609, 2789, 2969, 3169 },
    { 0, 1, 2, 4, 11, 12, 13, 15 },
    { 3, 5, 6, 7, 8, 9, 10, 14 },
    { 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f },
    0.80f, 0.80f,
    0.70f,
    18000.0f,
    0.50f, 1.0f,
    0.51f,           // late gain: same as Ambient DattorroTank (QuadTank similar output)
    1.5f, 1.3f,
    1.00f, 1.50f, 1.0f, // damping: trebleMultScale 0.80→1.00
    5000.0f,
    1.45f,           // airDampingScale: 1.30→1.45
    0.5f, 1.5f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    1.0f,
    false,
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON — 4 loops instead of 2
    2.00f,           // decay time scale: 2.00 (QuadTank needs higher scale for Ambient sustain)
    0.0f, 0, 1.0f,
    0.0f, 0.0f,
    0.0f, 0.0f,
    12000.0f,
    2000.0f,
    0.0f,
    1.24f,
    0.05f,
    0.0f, +3.0f, 4000.0f,  // output EQ: +3.0dB high shelf (was +1.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,
    false,
    0.0f,
    0.15f,
    0.0f,  // crestLimitRatio: disabled
    0, nullptr,
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,
};

// Preset "Short Vocal Ambience" (cloned from Ambient)
static constexpr AlgorithmConfig kPresetShortVocalAmbience = {
    "PresetShortVocalAmbience",
    { 971, 1049, 1153, 1277, 1399, 1523, 1667, 1811,
      1949, 2111, 2269, 2437, 2609, 2789, 2969, 3169 },
    { 0, 1, 2, 4, 11, 12, 13, 15 },  // balanced partition (sum=15576, diff=0)
    { 3, 5, 6, 7, 8, 9, 10, 14 },
    { 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f },
    0.80f, 0.80f,    // input diffusion: maximum
    0.70f,           // output diffusion scale
    18000.0f,        // bandwidth: wide input (let full spectrum in for bright Ambient)
    0.50f, 1.0f,     // ER: level=0.50 (was 0.0; enabled for custom ER taps — VV Ambient presets have early energy)
    0.51f,           // late gain: 0.51 (DattorroTank Ambient ~3dB quieter)
    1.5f, 1.3f,      // mod: heavy depth and rate
    0.80f, 1.50f, 1.0f, // damping: trebleMultScale=0.80, trebleMultScaleMax=1.50 (mid band identical to old 2-band)
    5000.0f,         // high crossover: 5kHz (3-band: bass < 241Hz, mid 241-5k, air > 5k)
    1.30f,           // airDampingScale: 1.30 (air > 5kHz decays slower — extra HF boost for Ambience match)
    0.5f, 1.5f,      // size range
    0.0f,            // ER crossfeed: off (no ERs)
    0.0f,            // inline diffusion: off (0.02 moved peak from 10kHz to 1kHz without reducing it)
    1.0f,            // mod depth floor: 1.0 = uniform modulation
    0.0f,            // structural HF damping: off (Ambient HF slightly fast; trebleMultScale=0.60 handles HF)
    0.0f,            // structural LF damping: off
    1.0f,            // noise mod: 1.0 (ambient ringing)
    0.0f,            // Hadamard perturbation: off (Ambient uses Householder)
    1.0f,            // ER gain exponent: inverse distance (default)
    false,           // useWeightedGains: off
    true,            // useHouseholderFeedback: on (Householder 82.4 vs Hadamard 79.4; more stable across presets)
    true,            // useDattorroTank: ON (with late tap positions matching VV onset)
    false,           // useQuadTank: OFF for Ambient
    0.98f,           // decay time scale: recalibrated for DattorroTank (was 0.99 for FDN)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled (Ambient already matched)
    0.0f, 0.0f,     // gate: disabled
    12000.0f,        // ER air absorption ceiling: default
    2000.0f,         // ER air absorption floor: default (no ERs in Ambient)
    0.0f,            // ER decorrelation: off
    1.24f,           // output gain: 1.24 (recalibrated for delay_scale, was 0.72)
    0.05f,           // stereo coupling: minimal (0.0 didn't help width enough)
    0.0f, +1.5f, 4000.0f, // output EQ: +1.5dB high shelf at 4kHz (was -0.5; +2.0dB centroid boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ: bypassed
    false,           // enableSaturation: off (clean linear output, matching VV)
    0.0f,            // lateOnsetMs: 0ms (Ambient onset already late — Ts=+32ms)
    0.15f,           // lateFeedForwardLevel: blend pre-diffusion late reverb
    0.0f,            // crestLimitRatio: disabled
    0, nullptr,  // custom ER: disabled
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,  // Dattorro taps: mid-deep onset
};

// Preset "Small Ambience" (cloned from AmbientFDN)
static constexpr AlgorithmConfig kPresetSmallAmbience = {
    "PresetSmallAmbience",
    { 971, 1049, 1153, 1277, 1399, 1523, 1667, 1811,
      1949, 2111, 2269, 2437, 2609, 2789, 2969, 3169 },  // same Ambient delay lengths
    { 0, 1, 2, 4, 11, 12, 13, 15 },
    { 3, 5, 6, 7, 8, 9, 10, 14 },
    { 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f },
    0.80f, 0.80f,    // input diffusion: maximum
    0.70f,           // output diffusion scale
    18000.0f,        // bandwidth
    0.50f, 1.0f,     // ER: same as Ambient
    0.22f,           // late gain: FDN native (no DattorroTank compensation)
    1.5f, 1.3f,      // mod: heavy depth and rate (same as Ambient)
    0.80f, 1.50f, 1.0f, // damping: same as Ambient
    5000.0f,         // high crossover
    1.30f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.0f,            // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    0.0f,            // structural LF damping
    1.0f,            // noise mod
    0.0f,            // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback: on
    false,           // useDattorroTank: OFF — native FDN
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale: 1.50 (FDN typically needs boost)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost
    0.0f, 0.0f,     // gate
    12000.0f,
    2000.0f,
    0.0f,            // ER decorrelation: off
    1.24f,           // output gain: same as Ambient initially
    0.05f,           // stereo coupling
    0.0f, +4.0f, 4000.0f, // output EQ: +4.0dB high shelf (was +2.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,
    false,
    0.0f,
    0.15f,           // lateFeedForwardLevel
    3.0f,            // crestLimitRatio: 3.0x — stronger peak limiting for Drum Air crest stability
    0, nullptr,
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,
};

// Preset "Small Drum Room" (cloned from HallQuad)
static constexpr AlgorithmConfig kPresetSmallDrumRoom = {
    "PresetSmallDrumRoom",
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },
    { 0, 3, 5, 7, 8, 10, 12, 15 },
    { 1, 2, 4, 6, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER
    1.24f,           // late gain: same as Hall DattorroTank initially
    0.75f, 13.0f,    // mod
    0.50f, 1.50f, 1.0f, // damping
    4000.0f,         // high crossover
    0.70f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.10f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping
    1.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON — key difference
    0.79f,           // decay time scale: same as Hall initially
    0.0f, 0, 1.0f,  // dual-slope
    2.0f, 1.5f,     // short-decay boost
    0.0f, 0.0f,     // gate
    20000.0f,        // ER air absorption ceiling
    18000.0f,        // ER air absorption floor
    0.55f,           // ER decorrelation
    8.53f,           // output gain
    -1.0f,           // stereo coupling
    0.0f, +4.0f, 5000.0f, // output EQ: +4.0dB high shelf (was +2.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,   // output mid EQ
    false,           // enableSaturation
    0.0f,            // lateOnsetMs
    0.20f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio: disabled (HallQuadSmooth handles crest-limited presets)
    0, nullptr,      // custom ER
    kHallDattorroLeftTaps, kHallDattorroRightTaps,
};

// Preset "Snare Ambience" (cloned from AmbientFDNBright)
static constexpr AlgorithmConfig kPresetSnareAmbience = {
    "PresetSnareAmbience",
    { 971, 1049, 1153, 1277, 1399, 1523, 1667, 1811,
      1949, 2111, 2269, 2437, 2609, 2789, 2969, 3169 },  // same Ambient delay lengths
    { 0, 1, 2, 4, 11, 12, 13, 15 },
    { 3, 5, 6, 7, 8, 9, 10, 14 },
    { 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f },
    0.80f, 0.80f,    // input diffusion: maximum
    0.70f,           // output diffusion scale
    18000.0f,        // bandwidth
    0.50f, 1.0f,     // ER: same as Ambient
    0.22f,           // late gain: FDN native (no DattorroTank compensation)
    1.5f, 1.3f,      // mod: heavy depth and rate (same as Ambient)
    1.00f, 1.50f, 1.0f, // damping: trebleMultScale 0.80→1.00
    5000.0f,         // high crossover
    1.45f,           // airDampingScale: 1.30→1.45
    0.5f, 1.5f,      // size range
    0.0f,            // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    0.0f,            // structural LF damping
    1.0f,            // noise mod
    0.0f,            // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback: on
    false,           // useDattorroTank: OFF — native FDN
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale: 1.50 (FDN typically needs boost)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost
    0.0f, 0.0f,     // gate
    12000.0f,
    2000.0f,
    0.0f,            // ER decorrelation: off
    1.24f,           // output gain: same as Ambient initially
    0.05f,           // stereo coupling
    0.0f, +4.0f, 4000.0f, // output EQ: +4.0dB high shelf (was +2.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,
    false,
    0.0f,
    0.15f,           // lateFeedForwardLevel
    3.0f,            // crestLimitRatio: 3.0x — stronger peak limiting for Drum Air crest stability
    0, nullptr,
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,
};

// Preset "Tight Ambience Gate" (cloned from AmbientQuadBright)
static constexpr AlgorithmConfig kPresetTightAmbienceGate = {
    "PresetTightAmbienceGate",
    { 971, 1049, 1153, 1277, 1399, 1523, 1667, 1811,
      1949, 2111, 2269, 2437, 2609, 2789, 2969, 3169 },
    { 0, 1, 2, 4, 11, 12, 13, 15 },
    { 3, 5, 6, 7, 8, 9, 10, 14 },
    { 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f },
    0.80f, 0.80f,
    0.70f,
    18000.0f,
    0.50f, 1.0f,
    0.51f,           // late gain: same as Ambient DattorroTank (QuadTank similar output)
    1.5f, 1.3f,
    1.00f, 1.50f, 1.0f, // damping: trebleMultScale 0.80→1.00
    5000.0f,
    1.45f,           // airDampingScale: 1.30→1.45
    0.5f, 1.5f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    1.0f,
    false,
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON — 4 loops instead of 2
    2.00f,           // decay time scale: 2.00 (QuadTank needs higher scale for Ambient sustain)
    0.0f, 0, 1.0f,
    0.0f, 0.0f,
    0.0f, 0.0f,
    12000.0f,
    2000.0f,
    0.0f,
    1.24f,
    0.05f,
    0.0f, +3.0f, 4000.0f,  // output EQ: +3.0dB high shelf (was +1.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,
    false,
    0.0f,
    0.15f,
    0.0f,  // crestLimitRatio: disabled
    0, nullptr,
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,
};

// Preset "Trip Hop Snare" (cloned from AmbientQuadBright)
static constexpr AlgorithmConfig kPresetTripHopSnare = {
    "PresetTripHopSnare",
    { 971, 1049, 1153, 1277, 1399, 1523, 1667, 1811,
      1949, 2111, 2269, 2437, 2609, 2789, 2969, 3169 },
    { 0, 1, 2, 4, 11, 12, 13, 15 },
    { 3, 5, 6, 7, 8, 9, 10, 14 },
    { 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f },
    0.80f, 0.80f,
    0.70f,
    18000.0f,
    0.50f, 1.0f,
    0.51f,           // late gain: same as Ambient DattorroTank (QuadTank similar output)
    1.5f, 1.3f,
    1.00f, 1.50f, 1.0f, // damping: trebleMultScale 0.80→1.00
    5000.0f,
    1.45f,           // airDampingScale: 1.30→1.45
    0.5f, 1.5f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    0.0f,
    1.0f,
    0.0f,
    1.0f,
    false,
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    true,            // useQuadTank: ON — 4 loops instead of 2
    2.00f,           // decay time scale: 2.00 (QuadTank needs higher scale for Ambient sustain)
    0.0f, 0, 1.0f,
    0.0f, 0.0f,
    0.0f, 0.0f,
    12000.0f,
    2000.0f,
    0.0f,
    1.24f,
    0.05f,
    0.0f, +3.0f, 4000.0f,  // output EQ: +3.0dB high shelf (was +1.5; +1.5dB HF boost)
    0.0f, 0.0f, 0.7f,
    false,
    0.0f,
    0.15f,
    0.0f,  // crestLimitRatio: disabled
    0, nullptr,
    kRoomDattorroLeftTaps, kRoomDattorroRightTaps,
};

// Preset "Very Small Ambience" (cloned from HallFDNSmooth)
static constexpr AlgorithmConfig kPresetVerySmallAmbience = {
    "PresetVerySmallAmbience",
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },
    { 0, 3, 5, 7, 8, 10, 12, 15 },
    { 1, 2, 4, 6, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.65f, 0.55f,    // input diffusion: increased (0.65/0.55 vs 0.55/0.45) — more smearing
    0.50f,           // output diffusion scale: increased (0.50 vs 0.35) — smoother peaks
    20000.0f,        // bandwidth
    0.30f, 0.85f,    // ER: further reduced erLevelScale (0.30 vs 0.65) — minimal ER spike
    0.22f,           // late gain
    0.75f, 13.0f,    // mod
    0.50f, 1.50f, 1.0f, // damping
    4000.0f,         // high crossover
    0.70f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.05f,           // ER crossfeed: reduced (0.05 vs 0.10) — less ER→FDN bleed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping
    40.0f,           // structural LF damping
    1.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank
    false,           // useQuadTank
    1.00f,           // decay time scale: 1.0 (no boost — preset controls decay directly)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    2.0f, 1.5f,     // short-decay boost
    0.0f, 0.0f,     // gate
    20000.0f, 18000.0f, 0.55f,
    8.53f, -1.0f,
    0.0f, +2.0f, 5000.0f,
    0.0f, 0.0f, 0.7f,
    false,           // enableSaturation: off (soft clipper doesn't help FDN crest at these levels)
    200.0f,          // lateOnsetMs: 200ms ramp — squared shape attenuates early FDN peaks
                      // For 115ms pre-delay: ramp is at 0.33 (squared) at crest window start
    0.10f,           // lateFeedForwardLevel: reduced (0.10 vs 0.20) — less pre-diffusion bleed
    1.5f,            // crestLimitRatio: limit peaks to 1.5× RMS (~3.5dB crest)
    0, nullptr,
    kHallDattorroLeftTaps, kHallDattorroRightTaps,
};

// === END AUTO-GENERATED ===

inline const AlgorithmConfig& getAlgorithmConfig (int index)
{
    static constexpr const AlgorithmConfig* kAlgorithms[kNumAlgorithms] = {
        &kPlate, &kHall, &kChamber, &kRoom, &kAmbient, &kPlateQuad, &kHallQuad, &kHallSlow, &kHallFDN, &kHallFDNDualSlopeBody, &kHallQuadSustain, &kRoomFDN, &kRoomQuad, &kRoomQuadSustain, &kChamberQuad, &kAmbientFDN, &kAmbientQuad, &kChamberQuadSustain, &kAmbientQuadSustain, &kHallFDNSmooth, &kRoomQuadSustainHigh, &kAmbientQuadSustainHigh, &kHallQuadSmooth, &kChamberFDN, &kPlateCrisp, &kHallQuadBright, &kRoomBright, &kRoomFDNBright, &kChamberQuadBright, &kAmbientFDNBright, &kAmbientQuadBright, &kChamberQuadSustainHybrid,
        // --- per-preset algorithms (auto-generated) ---
        &kPresetConcertWave,
        &kPresetFatSnareHall,
        &kPresetHomestarBladeRunner,
        &kPresetHugeSynthHall,
        &kPresetLongSynthHall,
        &kPresetPadHall,
        &kPresetSmallVocalHall,
        &kPresetSnareHall,
        &kPresetVeryNiceHall,
        &kPresetVocalHall,
        &kPresetDrumPlate,
        &kPresetFatDrums,
        &kPresetLargePlate,
        &kPresetSteelPlate,
        &kPresetTightPlate,
        &kPresetVocalPlate,
        &kPresetVoxPlate,
        &kPresetDarkVocalRoom,
        &kPresetExcitingSnareroom,
        &kPresetFatSnareRoom,
        &kPresetLivelySnareRoom,
        &kPresetLongDark70sSnareRoom,
        &kPresetShortDarkSnareRoom,
        &kPresetAPlate,
        &kPresetClearChamber,
        &kPresetFatPlate,
        &kPresetLargeChamber,
        &kPresetLargeWoodRoom,
        &kPresetLiveVoxChamber,
        &kPresetMediumGate,
        &kPresetRichChamber,
        &kPresetSmallChamber1,
        &kPresetSmallChamber2,
        &kPresetSnarePlate,
        &kPresetThinPlate,
        &kPresetTiledRoom,
        &kPresetAmbience,
        &kPresetAmbiencePlate,
        &kPresetAmbienceTiledRoom,
        &kPresetBigAmbienceGate,
        &kPresetCrossStickRoom,
        &kPresetDrumAir,
        &kPresetGatedSnare,
        &kPresetLargeAmbience,
        &kPresetLargeGatedSnare,
        &kPresetMedAmbience,
        &kPresetShortVocalAmbience,
        &kPresetSmallAmbience,
        &kPresetSmallDrumRoom,
        &kPresetSnareAmbience,
        &kPresetTightAmbienceGate,
        &kPresetTripHopSnare,
        &kPresetVerySmallAmbience
    };
    if (index < 0 || index >= kNumAlgorithms)
        index = 1; // Fall back to Hall
    return *kAlgorithms[index];
}
