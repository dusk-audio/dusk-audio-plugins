#pragma once

// Per-algorithm custom early reflection tap (mono-panned).
// Derived from VintageVerb Phase 11 OMP extraction.
// Each tap outputs to L only or R only (never both), matching VV's ER architecture.
struct CustomERTap
{
    float delayMs;     // Tap delay in milliseconds
    float amplitude;   // Linear amplitude (typically 0.003-0.025)
    int   channel;     // 0 = left only, 1 = right only
};

// Maximum number of custom ER taps per algorithm.
// VV typically produces 30-40 taps; 48 gives headroom.
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
                                 // Applied after TwoBandDamping, before feedbackLP. 0 = bypassed.
                                 // Effective frequency scales with treble_multiply: effectiveHz = baseHz * (0.5 + treble * 0.5).
                                 // Per-algorithm: higher values = gentler damping. Typical: 14000-19000.

    float structuralLFDampingHz; // First-order HP in FDN feedback reducing bass RT60 inflation (Hz).
                                 // Applied after structural HF damping. 0 = bypassed.
                                 // Room uses ~200Hz to tame 1.3-1.6x bass overshoot.

    float feedbackShelfHz;  // High shelf in FDN feedback loop (Hz). 0 = disabled.
                            // Cumulative per-recirculation spectral darkening ("in-loop" damping).
                            // -0.25 dB/pass at 1500Hz → ~12.5 dB total HF cut over a 2s tail.
    float feedbackShelfDB;  // High shelf gain per pass (dB). Negative = HF cut.
    float feedbackShelfQ;   // High shelf Q (0.707 = Butterworth default).

    float feedbackLPHz;  // Butterworth LP in FDN feedback path (Hz). 0 = bypassed.
                         // Order set by feedbackLP4thOrder: 12 dB/oct (2nd) or 24 dB/oct (4th).

    bool feedbackLP4thOrder; // true = 4th-order Linkwitz-Riley (24 dB/oct), false = 2nd-order Butterworth (12 dB/oct).
                             // 4th-order doubles per-pass HF attenuation — needed for short-delay Room modes.
                             // L-R alignment avoids the resonance peak of Butterworth 4th-order.

    float perChannelLPStrength;   // Per-channel feedback LP variation (0.0 = all identical, 1.0 = full spread).
                                   // Scales each channel's LP cutoff by its delay length ratio: shorter delays
                                   // get lower cutoff → more HF damping per pass. Spreads resonance peaks across
                                   // frequencies to enable stronger overall HF damping without ringing.
    float perChannelLPExponent;   // Power law exponent for delay-to-frequency mapping.
                                   // 0.5 = sqrt (gentle curve), 1.0 = linear. Default 0.5.
    float perChannelShelfStrength; // Per-channel feedback shelf gain variation (0.0 = all identical).
                                    // Shorter delays get proportionally more shelf cut per pass.

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
                           // ~0.25 matches VintageVerb's -9.4dB L→R coupling.
                           // Negative values disable stereo split (use full 16×16 Hadamard).

    float outputLowShelfDB;   // Low shelf EQ at 250Hz (dB). Negative = cut bass. 0 = bypassed.
    float outputHighShelfDB;  // High shelf EQ (dB). Positive = boost presence. 0 = bypassed.
    float outputHighShelfHz;  // High shelf frequency (Hz). Per-algorithm tunable.

    float outputMidEQHz;      // Mid parametric EQ center frequency (Hz). 0 = bypassed.
    float outputMidEQDB;      // Mid parametric EQ gain (dB). Negative = cut.
    float outputMidEQQ;       // Mid parametric EQ Q factor. Higher = narrower. 0.7 = gentle, 2.0 = surgical.

    // Custom ER tap table (mono-panned, derived from reference reverb extraction).
    // When numCustomERTaps > 0, EarlyReflections uses this fixed table instead of
    // computing taps from the exponential formula. Each tap goes to L or R only.
    int numCustomERTaps = 0;
    const CustomERTap* customERTaps = nullptr;
};

// ---------------------------------------------------------------------------
// VV-derived ER tap tables (Phase 11 OMP extraction).
// Each tap is mono-panned (L or R only), matching VV's ER architecture.
// Amplitudes are raw linear values from deconvolved IR (typically 0.003-0.023).
// ---------------------------------------------------------------------------

static constexpr CustomERTap kHallERTaps[] = {
    {  35.531f, -0.005597f, 1 }, {  35.590f, -0.004591f, 1 },
    {  39.419f,  0.004799f, 1 }, {  39.531f, -0.007254f, 1 }, {  39.590f, -0.005934f, 1 },
    {  40.114f, -0.006362f, 0 }, {  40.173f, -0.005215f, 0 },
    {  45.002f,  0.005277f, 0 }, {  45.114f, -0.008257f, 0 }, {  45.173f, -0.006778f, 0 },
    {  45.419f, -0.012791f, 1 }, {  45.495f,  0.005409f, 1 }, {  45.531f,  0.019925f, 1 },
    {  45.590f,  0.016349f, 1 }, {  45.651f,  0.008669f, 1 },
    {  51.419f, -0.005162f, 1 }, {  51.531f,  0.008103f, 1 }, {  51.590f,  0.006654f, 1 },
    {  52.002f, -0.014386f, 0 }, {  52.078f,  0.006136f, 0 }, {  52.114f,  0.022498f, 0 },
    {  52.173f,  0.018573f, 0 }, {  52.234f,  0.009898f, 0 },
    {  55.419f, -0.004941f, 1 }, {  55.531f,  0.007466f, 1 }, {  55.590f,  0.006106f, 1 },
    {  56.031f, -0.005273f, 0 },
    {  57.586f,  0.004293f, 1 },
    {  59.004f, -0.005838f, 0 }, {  59.065f,  0.005023f, 0 }, {  59.115f,  0.008755f, 0 },
    {  59.193f,  0.006046f, 0 },
    {  63.504f,  0.004601f, 1 }, {  63.618f, -0.004933f, 1 }, {  63.694f, -0.003736f, 1 },
    {  64.003f, -0.005984f, 0 }, {  64.066f,  0.004439f, 0 }, {  64.116f,  0.008183f, 0 },
    {  64.193f,  0.005562f, 0 },
    {  71.103f,  0.004935f, 0 },
};
static constexpr int kHallERTapCount = sizeof (kHallERTaps) / sizeof (kHallERTaps[0]);

static constexpr CustomERTap kRoomERTaps[] = {
    {  16.061f, -0.003596f, 1 }, {  16.998f, -0.003594f, 0 },
    {  19.477f, -0.005154f, 1 }, {  21.373f, -0.005838f, 0 },
    {  25.165f,  0.009528f, 1 }, {  27.434f, -0.003937f, 1 },
    {  27.998f,  0.010978f, 0 }, {  28.581f, -0.003321f, 1 },
    {  30.853f,  0.005693f, 1 }, {  32.373f, -0.003814f, 0 },
    {  34.269f,  0.005650f, 1 }, {  34.623f,  0.006617f, 0 },
    {  38.999f,  0.006935f, 0 }, {  39.758f, -0.003381f, 1 },
    {  39.956f,  0.003432f, 1 }, {  40.721f, -0.004036f, 0 },
    {  41.246f,  0.003748f, 0 }, {  42.226f,  0.004520f, 1 },
    {  44.129f, -0.004622f, 0 }, {  45.623f,  0.004015f, 0 },
    {  46.373f,  0.005626f, 1 }, {  49.810f,  0.007730f, 0 },
    {  52.412f,  0.004318f, 0 }, {  52.995f,  0.003953f, 1 },
    {  55.488f,  0.004263f, 0 }, {  58.896f,  0.004537f, 0 },
    {  61.403f,  0.004810f, 0 }, {  61.960f,  0.004256f, 1 },
    {  65.239f,  0.005247f, 1 }, {  66.746f,  0.005381f, 1 },
    {  68.022f, -0.006851f, 0 }, {  68.648f,  0.006908f, 1 },
    {  71.364f,  0.004416f, 1 }, {  74.331f, -0.010890f, 1 },
    {  74.636f, -0.004719f, 0 }, {  75.736f,  0.006202f, 1 },
    {  77.112f,  0.003924f, 0 }, {  77.744f,  0.005605f, 1 },
    {  77.871f, -0.004709f, 0 }, {  79.006f, -0.004120f, 0 },
};
static constexpr int kRoomERTapCount = sizeof (kRoomERTaps) / sizeof (kRoomERTaps[0]);

static constexpr CustomERTap kPlateERTaps[] = {
    {  15.517f,  0.004177f, 1 }, {  15.595f, -0.004670f, 1 },
    {  16.178f, -0.004240f, 0 },
    {  21.101f,  0.005626f, 0 }, {  21.178f, -0.006281f, 0 },
    {  21.517f, -0.011022f, 1 }, {  21.582f,  0.003710f, 1 },
    {  21.595f,  0.012567f, 1 }, {  21.671f,  0.007237f, 1 },
    {  25.595f, -0.003085f, 1 },
    {  27.517f, -0.005904f, 1 }, {  27.595f,  0.006755f, 1 }, {  27.671f,  0.003895f, 1 },
    {  28.101f, -0.014315f, 0 }, {  28.165f,  0.004767f, 0 },
    {  28.178f,  0.016148f, 0 }, {  28.255f,  0.009263f, 0 },
    {  31.517f, -0.005953f, 1 }, {  31.595f,  0.006660f, 1 }, {  31.671f,  0.003809f, 1 },
    {  33.178f, -0.003839f, 0 },
    {  33.517f, -0.003250f, 1 }, {  33.595f,  0.003496f, 1 },
    {  35.101f, -0.007546f, 0 }, {  35.178f,  0.008595f, 0 }, {  35.255f,  0.004948f, 0 },
    {  37.595f,  0.003487f, 1 },
    {  39.038f,  0.003846f, 0 }, {  39.116f, -0.004423f, 0 },
    {  40.101f, -0.007393f, 0 }, {  40.178f,  0.008683f, 0 }, {  40.255f,  0.005052f, 0 },
    {  41.595f,  0.003128f, 1 },
    {  42.101f, -0.004023f, 0 }, {  42.178f,  0.004221f, 0 },
    {  47.179f,  0.004380f, 0 },
    {  48.351f,  0.004595f, 1 }, {  48.428f, -0.005438f, 1 }, {  48.505f, -0.003172f, 1 },
    {  52.178f,  0.004153f, 0 },
};
static constexpr int kPlateERTapCount = sizeof (kPlateERTaps) / sizeof (kPlateERTaps[0]);

// Chamber: very few taps above noise floor (VV Chamber has very quiet, diffuse ERs)
static constexpr CustomERTap kChamberERTaps[] = {
    {  43.695f, -0.003569f, 0 }, {  43.760f,  0.003617f, 0 },
    {  47.250f, -0.003775f, 0 },
    {  50.648f, -0.003003f, 1 },
    {  54.442f, -0.003035f, 0 },
    {  57.933f,  0.003174f, 0 },
    {  62.179f, -0.003214f, 1 },
    {  65.066f,  0.004418f, 0 },
    {  72.033f, -0.003135f, 0 },
    {  72.386f, -0.003587f, 0 },
};
static constexpr int kChamberERTapCount = sizeof (kChamberERTaps) / sizeof (kChamberERTaps[0]);

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
    0.75f,           // output diffusion scale
    20000.0f,        // bandwidth: full spectrum input (no HF attenuation before FDN)
    0.90f, 0.70f,    // ER: level=0.90, timeScale=0.70
    0.20f,           // late gain: 0.20 (was 0.30; reduce to match VV Plate output level)
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
    0.0f,            // feedback shelf: disabled (adding shelf at 3kHz caused ringing spikes)
    0.0f,            // feedback shelf: disabled
    0.707f,          // feedback shelf Q: Butterworth
    10000.0f,        // feedback LP: 10kHz (2nd order)
    false,           // feedback LP 4th order: off (2nd order for gentler slope)
    0.15f,           // per-channel LP strength: gentle spread (Plate ringing-sensitive)
    0.5f,            // per-channel LP exponent: sqrt
    0.0f,            // per-channel shelf strength: off (shelf disabled for Plate)
    0.03f,           // noise mod: 0.03 (was 0.15; reduce delay jitter for MFCC 36→51)
    0.12f,           // Hadamard perturbation: break symmetry
    1.0f,            // ER gain exponent: inverse distance (default)
    false,           // useWeightedGains: off
    true,            // useHouseholderFeedback: on (Householder 68.4 vs Hadamard 66)
    false,           // useDattorroTank: off (FDN)
    1.72f,           // decay time scale: compensates FDN filter chain losses (measured 0.58x without)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    2.0f, 0.9f,     // short-decay boost: +2 dB
    0.0f, 0.0f,     // gate: disabled
    18000.0f,        // ER air absorption ceiling: 18kHz
    2000.0f,         // ER air absorption floor: default
    0.0f,            // ER decorrelation: off
    1.50f,           // output gain: 1.50 (was 1.40; +0.6dB to compensate for inline diffusion energy loss)
    0.20f,           // stereo coupling: 0.20
    0.0f, -2.5f, 3000.0f, // output EQ: -2.5dB high shelf at 3kHz (DV tail +3-4dB too bright at 3-6kHz vs VV)
    0.0f, 0.0f, 0.7f,   // output mid EQ: bypassed
    0, nullptr,  // custom ER: disabled
};

// ---------------------------------------------------------------------------
// Hall: Large concert hall reverb.
// Wide delay spread, moderate diffusion, full early reflections.
static constexpr AlgorithmConfig kHall = {
    "Hall",
    // delay lengths: 1801-5521 samples (41-125ms @ 44.1kHz) — doubled to match VV Concert Hall range (48-130ms)
    { 1801, 1933, 2089, 2251, 2423, 2617, 2819, 3037,
      3271, 3527, 3803, 4093, 4409, 4759, 5119, 5521 },  // logarithmic prime spacing for uniform modal density
    { 0, 3, 5, 7, 8, 10, 12, 15 },     // leftTaps: balanced partition (sum=26710, diff=52 samples ≈ 1ms)
    { 1, 2, 4, 6, 9, 11, 13, 14 },     // rightTaps: balanced partition (sum=26762)
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },  // alternating signs for decorrelation
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion: 0.55/0.45 (was 0.75/0.625 — 50ms density 5300 vs VV 2450, need sparser early onset)
    0.5f,            // output diffusion: 0.5
    20000.0f,        // bandwidth: full spectrum input
    0.90f, 0.85f,    // ER: level=0.90, timeScale=0.85 (0.95 made C50 worse at 42.1)
    0.22f,           // late gain: 0.22
    1.0f, 1.0f,      // mod: modDepthScale=1.0 (was 2.0; reduced to improve MFCC while retaining LFO character)
    0.50f, 1.50f, 1.0f, // damping: trebleMultScale=0.50
    4000.0f,         // high crossover: 4kHz (3kHz regressed — pushed too much into air band, kurtosis diverged)
    0.70f,           // airDampingScale: 0.70
    0.5f, 1.5f,      // size range
    0.25f,           // ER crossfeed: 0.25 (0.22 dropped MFCC/EDT — keep at 0.25)
    0.0f,            // inline diffusion: off (even 0.05 wrecked centroid_late 97.6→69.9, spectral_tilt 95.4→58.8)
    1.0f,            // mod depth floor: 1.0 = uniform modulation
    0.0f,            // structural HF damping: off (feedbackLP at 15kHz handles extreme HF instead)
    40.0f,           // structural LF damping: 40Hz HP (90Hz destroyed bass ratio and raised centroid — output EQ handles 80-250Hz instead)
    3000.0f,         // feedback shelf: 3kHz (was 1.5kHz; 1.5kHz caused -7 to -13dB tilt at 2-4kHz on dark presets)
    -0.10f,          // feedback shelf: -0.10 dB/pass (was -0.20; halved to reduce cumulative mid-range darkening)
    0.707f,          // feedback shelf Q: Butterworth
    9500.0f,         // feedback LP: 9.5kHz 4th order
    true,            // feedback LP 4th order: on (R2 revert: 2nd order crashed centroid_late 100→57.7, kurtosis 100→28.9)
    0.3f,            // per-channel LP strength: gentle (4th-order amplifies differences)
    0.5f,            // per-channel LP exponent: sqrt
    0.2f,            // per-channel shelf strength: gentle variation on -0.20dB/pass shelf
    0.10f,           // noise mod: 0.10 (was 0.35; reducing delay jitter improves MFCC consistency)
    0.10f,           // Hadamard perturbation: 0.10 (0.05 wasn't enough — still audible repetition in tail)
    1.0f,            // ER gain exponent: 1.0 inverse distance (1.5 caused audible pulse after transients)
    false,           // useWeightedGains: off (true regressed decay_shape 100→74.4, total 75.4→73.1)
    true,            // useHouseholderFeedback: on (keep true per instructions)
    false,           // useDattorroTank: off (FDN)
    1.85f,           // decay time scale: compensates FDN filter chain losses (measured 0.54x without)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    2.0f, 1.5f,     // short-decay boost: +2.0 dB (4.5 contributed to transient pulse)
    0.0f, 0.0f,     // gate: disabled
    20000.0f,        // ER air absorption ceiling: 20kHz (earliest taps: no filtering)
    18000.0f,        // ER air absorption floor: 18kHz
    0.55f,           // ER decorrelation: strong allpass decorrelation (IACC 0.747→target 0.321)
    1.9f,            // output gain: 1.9 (was 2.4; -2dB to center level after VV parameter formula corrections)
    -1.0f,           // stereo coupling: -1.0 = full 16×16 Hadamard (was 0.15 split 8+8; coupling -21.6 dB vs VV -1.7 dB)
    -1.5f,           // output low shelf: -1.5dB at 250Hz
    -3.5f,           // output high shelf: -3.5dB at 4kHz (was -5dB@1.1kHz; 1.1kHz killed mid-range on dark presets)
    4000.0f,         // output high shelf freq: 4kHz (was 1.1kHz)
    0.0f,            // output mid EQ: bypassed
    0.0f,            // output mid EQ: bypassed
    0.7f,            // output mid EQ: Q (bypassed)
    0, nullptr,  // custom ER: disabled (VV taps caused regression; keep infrastructure for future use)
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
    1.0f,            // output diffusion scale
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
    2000.0f,         // feedback shelf: 2kHz
    -0.20f,          // feedback shelf: -0.20 dB/pass
    0.707f,          // feedback shelf Q: Butterworth
    10000.0f,        // feedback LP: 10kHz (was 12kHz — DV tail 6-9dB too bright at 6.3-10kHz vs VV)
    false,           // feedback LP 4th order: off
    0.3f,            // per-channel LP strength: moderate spread (Chamber +6-9dB at 6.3-10kHz)
    0.5f,            // per-channel LP exponent: sqrt
    0.2f,            // per-channel shelf strength: gentle variation on -0.20dB/pass shelf
    0.05f,           // noise mod: 0.05 (was 0.5; reducing improved MFCC from 37→62)
    0.10f,           // Hadamard perturbation: break symmetry
    1.0f,            // ER gain exponent: inverse distance (default)
    false,           // useWeightedGains: off
    true,            // useHouseholderFeedback: on (Householder 65.0 vs Hadamard 63.4)
    false,           // useDattorroTank: off (FDN)
    1.67f,           // decay time scale: compensates FDN filter chain losses (measured 0.60x without)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled (Chamber has static lateGainScale tuning)
    0.0f, 0.0f,     // gate: disabled
    12000.0f,        // ER air absorption ceiling: default
    2000.0f,         // ER air absorption floor: default
    0.50f,           // ER decorrelation: strong (reduce early IACC from 0.617 toward 0.241)
    0.65f,           // output gain: 0.65
    0.15f,           // stereo coupling: moderate (wider stereo for IACC match)
    0.0f, -2.0f, 5000.0f, // output EQ: -2dB high shelf at 5kHz (DV tail 6-9dB too bright at 6.3-10kHz vs VV)
    0.0f, 0.0f, 0.7f,   // output mid EQ: bypassed
    0, nullptr,  // custom ER: disabled (VV taps caused regression; keep infrastructure for future use)
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
    1.0f,            // output diffusion scale
    18000.0f,        // bandwidth: wide input (structural HF damping handles per-loop rolloff)
    1.4f, 0.45f,     // ER: boosted level + tighter timing for C50
    0.38f,           // late gain: 0.38 (was 0.55; reduce to match VV Room output level)
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
    0.0f,            // feedback shelf: disabled (Room already at 77)
    0.0f,            // feedback shelf: disabled
    0.707f,          // feedback shelf Q: default
    9500.0f,         // feedback LP: 9.5kHz
    false,           // feedback LP 4th order: off (4th-order created phase resonance, worsened ringing 16→21dB)
    0.3f,            // per-channel LP strength: moderate spread (Room +3-5dB at 4-8kHz)
    0.5f,            // per-channel LP exponent: sqrt
    0.0f,            // per-channel shelf strength: off (shelf disabled for Room)
    1.0f,            // noise mod: 1.0 (stronger jitter for Exciting Snare room which has ModDepth=0.144 — barely any LFO smearing)
    0.20f,           // Hadamard perturbation: 0.20 (was 0.08→0.12; stronger to detune 11kHz mode in Exciting Snare)
    0.30f,           // ER gain exponent: gentle rolloff (more energy in later taps)
    false,           // useWeightedGains: off
    false,           // useHouseholderFeedback: off (Room — Householder regressed 75.8 vs Hadamard 77.1)
    false,           // useDattorroTank: off (FDN)
    1.92f,           // decay time scale: compensates FDN filter chain losses (measured 0.52x without)
    0.0f, 0, 1.0f,   // dual-slope: disabled (standard 16-channel FDN for matched tail energy)
    0.0f, 0.0f,      // short-decay boost: disabled (Room uses lateGainScale calibration)
    0.0f, 0.0f,      // gate: disabled
    10000.0f,         // ER air absorption ceiling: darken earliest taps slightly (centroid_early 6207→5521)
    2000.0f,          // ER air absorption floor: default
    0.35f,            // ER decorrelation: moderate (improve L/R spectral variation for MFCC)
    0.71f,            // output gain: 0.71 (-3dB; Room presets all +3-7dB hot after VV formula corrections)
    0.20f,            // stereo coupling: moderate (rooms have natural coupling from walls)
    0.0f, -3.0f, 4000.0f, // output EQ: -3dB high shelf at 4kHz (DV tail +3-5dB too bright at 4-8kHz vs VV)
    0.0f, 0.0f, 0.7f,   // output mid EQ: bypassed
    0, nullptr,  // custom ER: disabled (VV taps caused regression; keep infrastructure for future use)
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
    1.0f,            // output diffusion scale
    18000.0f,        // bandwidth: wide input (let full spectrum in for bright Ambient)
    0.0f, 1.0f,      // ER: forced off
    0.35f,           // late gain: 0.35 (was 0.55; reduce to match VV Ambient output level)
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
    0.0f,            // feedback shelf: disabled (Ambient already at 82)
    0.0f,            // feedback shelf: disabled
    0.707f,          // feedback shelf Q: default
    11000.0f,        // feedback LP: 11kHz (balanced HF control)
    false,           // feedback LP 4th order: off
    0.25f,           // per-channel LP strength: 0.25 (moderate diversity; stable across Ambient presets)
    0.5f,            // per-channel LP exponent: sqrt
    0.0f,            // per-channel shelf strength: off (shelf disabled for Ambient)
    0.1f,            // noise mod: 0.1 (minimal jitter to smooth LFO phase effects on ringing measurement)
    0.0f,            // Hadamard perturbation: off (Ambient uses Householder)
    1.0f,            // ER gain exponent: inverse distance (default)
    false,           // useWeightedGains: off
    true,            // useHouseholderFeedback: on (Householder 82.4 vs Hadamard 79.4; more stable across presets)
    false,           // useDattorroTank: off (FDN)
    1.79f,           // decay time scale: compensates FDN filter chain losses (measured 0.56x without)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled (Ambient already matched)
    0.0f, 0.0f,     // gate: disabled
    12000.0f,        // ER air absorption ceiling: default
    2000.0f,         // ER air absorption floor: default (no ERs in Ambient)
    0.0f,            // ER decorrelation: off
    0.71f,           // output gain: 0.71 (-3dB; Ambient presets +3-8dB hot after VV formula corrections)
    0.05f,           // stereo coupling: minimal (0.0 didn't help width enough)
    0.0f, -4.0f, 4000.0f, // output EQ: -4dB high shelf at 4kHz (DV tail +4-17dB too bright above 5kHz vs VV)
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
