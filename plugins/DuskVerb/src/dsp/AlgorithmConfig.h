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

    // Late-bloom envelope: transient-triggered gain curve that preserves the body
    // window (unity during lateBloomDelayMs) then ramps up over lateBloomAttackMs,
    // holds, then releases back to unity. Compensates for DV's monotonic-decay
    // architecture which can't reproduce VV's signature tail RMS ≥ body RMS.
    // Default 0.0 = disabled. Per-preset override via last init-list element.
    float lateBloomLevel = 0.0f;
    float lateBloomDelayMs = 150.0f;    // Unity hold after trigger (keeps body unchanged).
    float lateBloomAttackMs = 200.0f;   // Time to reach peak after delay elapses.
    float lateBloomHoldMs = 600.0f;     // Time to hold at peak before release.
    float lateBloomReleaseMs = 800.0f;  // Time to relax back to unity.
    // Per-band bloom offsets (added to lateBloomLevel per band). Band layout matches
    // audio metric's coarse_bands:
    //   [0] lo_mid 80-315 Hz, [1] mid 315-1250 Hz, [2] hi_mid 1250-5000 Hz, [3] hi 5000-12000 Hz.
    // Positive = extra boost in that band. Negative = reduce (or disable) bloom in band.
    // All 0 (default) = uniform bloom (scalar lateBloomLevel applied flat).
    float lateBloomBandOffset[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    // Body-bloom envelope (body-window counterpart to lateBloom). Triggered by the same
    // input transient; shaped to cover the window_buildup metric's body region
    // (100–500 ms, NOT the decay_ratio metric's 50–200 ms body — window_buildup is what
    // the body_vs_onset failures are measured against). Default timing: 100 ms delay
    // → 50 ms attack → 300 ms hold → 100 ms release (peak 150–450 ms, within the
    // 100–500 ms body window). bodyBloomLevel <0 dips body (cuts band RMS); >0 boosts.
    // Band offsets layer on top (same layout as lateBloomBandOffset).
    float bodyBloomLevel = 0.0f;
    float bodyBloomDelayMs = 100.0f;
    float bodyBloomAttackMs = 50.0f;
    float bodyBloomHoldMs = 300.0f;
    float bodyBloomReleaseMs = 100.0f;
    float bodyBloomBandOffset[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    // Tail-notch filter: narrow-band peaking biquad with envelope-modulated dry-wet blend.
    // Targets freqDecay metric failures where one octave band sustains too long in DV's
    // feedback loop. Coefficients computed once at algorithm-switch from (freq, Q, gainDb);
    // envelope modulates the blend: 0=bypass, 1=full-wet-filtered. Same attack/hold/release
    // timing semantics as lateBloom. When tailNotchFreqHz <= 0 the feature is disabled.
    float tailNotchFreqHz = 0.0f;        // 0 or negative = disabled
    float tailNotchQ = 2.0f;
    float tailNotchGainDb = 0.0f;        // negative = dip (cut that band), positive = boost
    float tailNotchDelayMs = 500.0f;
    float tailNotchAttackMs = 100.0f;
    float tailNotchHoldMs = 800.0f;
    float tailNotchReleaseMs = 400.0f;

    // Chorus AM defaults: applied at applyAlgorithm. Acts as a floor — if the user's
    // chorus_depth/rate APVTS knobs are above, user wins. Targets the `modulation`
    // metric (envelope variance in 1-4kHz band, 300-1500ms) which VV achieves via
    // chorus AM on the wet output. 0 = disabled.
    float chorusDepthDefault = 0.0f;
    float chorusRateDefault = 1.0f;

    // Tail floor gate: threshold-triggered hard-zero on wet output.
    // Used for the `modulation` metric which is secretly a tail-silence detector —
    // VV's 1-4kHz band hits digital silence (<1e-10) at 800ms+, DV's residual
    // feedback tail keeps the trend above the 1e-10 floor. This gate mimics VV's
    // natural silence: envelope-follower gate that only closes when signal has
    // already decayed below threshold.
    //  0 (default) = disabled.
    //  Set to e.g. -70 for heavy gate (matches digital noise floor).
    float tailFloorGateDb = 0.0f;
    // One-pole release time for the envelope follower (fast attack, exponential release).
    // Longer = gate stays open through brief dips in the signal envelope.
    float tailFloorGateReleaseMs = 30.0f;

    // Onset noise-burst generator: bandpass-filtered PRNG burst triggered on input
    // transient, additively mixed into the wet output. Targets `onsetRing` /
    // `tailRing` metrics where DV's FDN tank produces tonal onset content that
    // VV hides with broadband noise (ER density). Independent L/R PRNGs add
    // stereo decorrelation. peakDb <= -59 disables. Typical: -32 dB peak,
    // 20 ms delay / 2 ms attack / 20 ms hold / 80 ms release, 2000-12000 Hz band, Q=0.5.
    // `onsetBurstDelayMs` = silence-before-burst (matches tank natural onset latency;
    // too-early burst is flagged by onset_delta_ms metric).
    float onsetBurstPeakDb = -60.0f;       // disabled sentinel
    float onsetBurstDelayMs = 20.0f;
    float onsetBurstAttackMs = 2.0f;
    float onsetBurstHoldMs = 20.0f;
    float onsetBurstReleaseMs = 80.0f;
    float onsetBurstBandLoHz = 2000.0f;
    float onsetBurstBandHiHz = 12000.0f;
    float onsetBurstQ = 0.5f;

    // Post-FDN stereo decorrelator: nested allpass network with asymmetric L/R
    // delays. Spreads channels further than FDN Hadamard + width matrix can
    // achieve. Applied after FDN output scaling, before output EQ. Preserves
    // envelope / decay / RMS (allpass is magnitude-flat). amount 0 = disabled,
    // typical 0.5-0.8. baseDelayMs scales L/R delay templates.
    float stereoDecorrAmount = 0.0f;
    float stereoDecorrBaseDelayMs = 10.0f;

    // Time-varying tail damping envelope: 1-pole LPF on wet path whose cutoff
    // sweeps DOWN from tailDampingStartHz to tailDampingEndHz during the
    // mid-tail window, then sweeps BACK UP to start (transparent) for far-tail.
    // Triggered by input transient. Shape: delay → attack-down → hold-at-end →
    // release-up → back to start. Bends the decay curve steeper through the
    // mid-tail window without shortening absolute decay or darkening far-tail.
    // Use when decay_ratio fails but shortening decay_time_scale breaks farTail
    // or leaving HF damped permanently breaks centroid.
    // Disabled unless tailDampingEndHz > 0 AND tailDampingEndHz < tailDampingStartHz.
    float tailDampingStartHz = 20000.0f;  // Initial/recovery cutoff (transparent)
    float tailDampingEndHz   = 0.0f;      // Damped-window cutoff (0 = disabled)
    float tailDampingDelayMs = 200.0f;    // Unity hold before sweep begins
    float tailDampingAttackMs = 300.0f;   // Sweep-down time (start→end)
    float tailDampingHoldMs   = 500.0f;   // Hold at end cutoff
    float tailDampingReleaseMs = 400.0f;  // Sweep-up time (end→start)

    // Band-filtered late bloom: if > 0, the lateBloom boost is applied only to
    // signal above this high-pass cutoff (additive: output = dry + HPF(dry)*level*shape).
    // Leaves bands below the cutoff unchanged, so bloom cannot re-excite a
    // tail-notched band. 0 = disabled (full-spectrum bloom, original behavior).
    float lateBloomHighpassHz = 0.0f;
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
static constexpr int kNumAlgorithms = 25;




// === AUTO-GENERATED PRESET ALGORITHMS ===

// Preset "Gated" — short FDN tail with envelope gate for classic 80s drum gate sound.
static constexpr AlgorithmConfig kPresetGated = {
    "PresetGated",
    {   102,   260,   312,   317,   331,   336,   346,   443,
       1884,  2045,  2185,  2372,  2771,  2915,  4596,  5125 },
    { 1, 3, 5, 6, 8, 10, 12, 15 },
    { 0, 2, 4, 7, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER level, time scale
    0.22f,           // late gain
    0.75f, 13.0f,    // mod: depth, rate
    1.00f, 1.50f, 1.00f, // damping: treble, ceiling, bass
    6000.0f,         // high crossover
    0.95f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.15f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping (was 4000, killing the gated tail)
    0.0f,            // structural LF damping
    8.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF
    false,           // useQuadTank: OFF
    1.00f,           // decay time scale
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled
    0.0f, 0.0f,     // gate: AlgorithmConfig gate disabled (the user-facing gate_hold/release params handle the envelope gate)
    12000.0f, 2000.0f, 0.50f,  // ER air absorption, decorr
    8.00f,           // output gain
    -1.0f,           // stereo coupling
    0.0f, 0.0f, 5000.0f,  // output EQ
    0.0f, 0.0f, 0.7f,
    false,           // no saturation
    0.0f,            // lateOnsetMs (was 50, removed for clean gated transient)
    0.35f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio
    0, nullptr,      // custom ER
    nullptr, nullptr,
    1.0f, 0.0f, -1,  // dattorroDelayScale, hybridBlend, hybridSecondaryAlgo
    0.0f,            // lateBloomLevel disabled (was 0.55, Homestar bloom)
    500.0f, 100.0f, 1000.0f, 400.0f,
    { 0.0f, 0.0f, 0.0f, 0.0f },
    0.0f,            // bodyBloomLevel
    100.0f, 50.0f, 300.0f, 100.0f,
    { 0.0f, 0.0f, 0.0f, 0.0f },
};

// Preset "Pad Hall" (VV-derived FDN)
static constexpr AlgorithmConfig kPresetCathedral = {
    "PresetCathedral",
    {   124,   346,   668,  1715,  2040,  2175,  2372,  2708,
       2877,  3195,  3522,  3896,  4596,  5236,  5554,  5933 },
    { 0, 3, 4, 7, 8, 11, 12, 15 },
    { 1, 2, 5, 6, 9, 10, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER level, time scale
    0.22f,           // late gain (FDN native)
    0.75f, 13.0f,    // mod: depth=0.75 (mod_depth_db=-5.2)
    0.99f, 1.50f, 0.99f, // damping: treble=0.99, bass=0.99
    4000.0f,         // high crossover
    0.80f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.15f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping (runtime override)
    0.0f,            // structural LF damping
    8.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF -- native FDN
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled
    0.0f, 0.0f,     // gate: disabled
    12000.0f, 2000.0f, 0.50f,  // ER air absorption, decorr
    8.00f,           // output gain
    -1.0f,           // stereo coupling (full 16x16 Hadamard)
    0.0f, 0.0f, 5000.0f,  // output EQ (runtime overrides)
    0.0f, 0.0f, 0.7f,
    false,           // no saturation
    0.0f,            // lateOnsetMs
    0.35f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio
    0, nullptr,      // custom ER
    nullptr, nullptr,
    1.0f, 0.0f, -1,  // dattorroDelayScale, hybridBlend, hybridSecondaryAlgo
    0.00f,  // lateBloomLevel
    500.0f, 100.0f, 1000.0f, 400.0f,  // tail-only bloom (500-2000ms)
    { 0.00f, 0.00f, 1.15f, -0.25f },  // lateBloomBandOffset (hi dip for spillover)
    0.0f,  // bodyBloomLevel
    110.0f, 30.0f, 80.0f, 20.0f,  // post-onset sharp body
    { 0.00f, 1.80f, 0.00f, 0.00f },  // bodyBloomBandOffset (very strong mid)
};

// Preset "Concert Wave" (VV-derived FDN)

// Preset "Huge Synth Hall" (VV-derived FDN)
static constexpr AlgorithmConfig kPresetLargeHall = {
    "PresetLargeHall",
    {   336,   346,   898,  1504,  1978,  2375,  2606,  2921,
       3265,  3463,  3727,  4099,  4383,  5002,  5468,  6168 },
    { 0, 3, 4, 7, 9, 10, 12, 15 },
    { 1, 2, 5, 6, 8, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER level, time scale
    0.22f,           // late gain (FDN native)
    0.75f, 13.0f,    // mod: depth=0.75 (mod_depth_db=-5.2)
    0.99f, 1.50f, 1.00f, // damping: treble=0.99, bass=1.00
    4000.0f,         // high crossover
    0.80f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.15f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping (runtime override)
    0.0f,            // structural LF damping
    8.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF -- native FDN
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled
    0.0f, 0.0f,     // gate: disabled
    12000.0f, 2000.0f, 0.50f,  // ER air absorption, decorr
    8.00f,           // output gain
    -1.0f,           // stereo coupling (full 16x16 Hadamard)
    0.0f, 0.0f, 5000.0f,  // output EQ (runtime overrides)
    0.0f, 0.0f, 0.7f,
    false,           // no saturation
    0.0f,            // lateOnsetMs
    0.35f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio
    0, nullptr,      // custom ER
    nullptr, nullptr,
    1.0f, 0.0f, -1,  // dattorroDelayScale, hybridBlend, hybridSecondaryAlgo
    0.00f,  // lateBloomLevel (0 = no scalar bloom, only per-band)
    150.0f, 200.0f, 600.0f, 800.0f,  // bloom timing
    { 0.00f, 0.00f, 0.20f, 0.00f },  // lateBloomBandOffset
};

// Preset "Small Vocal Hall" (VV-derived FDN)

// Preset "Fat Snare Hall" (VV-derived FDN)

// Preset "Snare Hall" (VV-derived FDN)
static constexpr AlgorithmConfig kPresetMediumHall = {
    "PresetMediumHall",
    {   124,   280,   346,   413,   695,  1846,  1997,  2131,
       2288,  2506,  2700,  2843,  3262,  4400,  4809,  5231 },
    { 0, 1, 5, 7, 9, 11, 12, 15 },
    { 2, 3, 4, 6, 8, 10, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER level, time scale
    0.22f,           // late gain (FDN native)
    0.75f, 13.0f,    // mod: depth=0.75 (mod_depth_db=-4.0)
    0.99f, 1.50f, 1.08f, // damping: treble=0.99, bass=1.08
    4000.0f,         // high crossover
    0.80f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.15f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping (runtime override)
    0.0f,            // structural LF damping
    8.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF -- native FDN
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled
    0.0f, 0.0f,     // gate: disabled
    12000.0f, 2000.0f, 0.50f,  // ER air absorption, decorr
    8.00f,           // output gain
    -1.0f,           // stereo coupling (full 16x16 Hadamard)
    0.0f, 0.0f, 5000.0f,  // output EQ (runtime overrides)
    0.0f, 0.0f, 0.7f,
    false,           // no saturation
    0.0f,            // lateOnsetMs
    0.35f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio
    0, nullptr,      // custom ER
    nullptr, nullptr,
    1.0f, 0.0f, -1,  // dattorroDelayScale, hybridBlend, hybridSecondaryAlgo
    0.18f,           // lateBloomLevel — compensate DV decay_ratio shortfall vs VV
};

// Preset "Long Synth Hall" (VV-derived FDN)

// Preset "Very Nice Hall" (VV-derived FDN)

// Preset "Vocal Hall" (VV-derived FDN)
static constexpr AlgorithmConfig kPresetVocalHall = {
    "PresetVocalHall",
    {    99,   336,   346,   441,   788,  1715,  2174,  2412,
       2599,  2866,  3336,  3878,  4120,  4596,  5024,  6387 },
    { 0, 1, 5, 6, 8, 10, 12, 15 },
    { 2, 3, 4, 7, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    2.50f, 0.85f,    // ER level (boosted from 0.65 to strengthen onset), time scale
    0.22f,           // late gain (FDN native)
    0.75f, 13.0f,    // mod: depth=0.75 (mod_depth_db=-5.5)
    0.87f, 1.50f, 1.00f, // damping: treble=0.87, bass=1.00
    4000.0f,         // high crossover
    0.80f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.15f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping (runtime override)
    0.0f,            // structural LF damping
    8.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF -- native FDN
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled
    0.0f, 0.0f,     // gate: disabled
    12000.0f, 2000.0f, 0.50f,  // ER air absorption, decorr
    8.00f,           // output gain
    -1.0f,           // stereo coupling (full 16x16 Hadamard)
    0.0f, 0.0f, 5000.0f,  // output EQ (runtime overrides)
    0.0f, 0.0f, 0.7f,
    false,           // no saturation
    0.0f,            // lateOnsetMs
    0.35f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio
    0, nullptr,      // custom ER
    nullptr, nullptr,
};

// Preset "Drum Plate" (VV-derived FDN)
// Preset "Drum Plate" — DattorroTank, targeting EMT 140 "bright" damper, variant 1 (2.2s, ~7.3kHz centroid).
static constexpr AlgorithmConfig kPresetDrumPlate = {
    "PresetDrumPlate",
    {   125,   266,   346,   555,  1700,  2029,  2168,  2301,
       2444,  2576,  2708,  2845,  3262,  3671,  3929,  5967 },
    { 0, 1, 4, 6, 8, 10, 12, 15 },
    { 2, 3, 5, 7, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.05f, 0.85f,    // ER level (low — plates have minimal early reflections), time scale
    0.22f,           // late gain
    0.75f, 13.0f,    // mod: depth, rate
    0.95f, 1.50f, 0.90f, // damping: treble=0.95 (brighter than Vocal Plate), bass=0.90
    7000.0f,         // high crossover — bright target
    0.92f,           // airDampingScale (matches kVvAirDampingScale in DrumPlatePreset.cpp)
    0.5f, 1.5f,      // size range
    0.15f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping (runtime override)
    0.0f,            // structural LF damping
    8.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    true,            // useDattorroTank: ON
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled
    0.0f, 0.0f,     // gate: disabled
    12000.0f, 2000.0f, 0.50f,  // ER air absorption, decorr
    8.00f,           // output gain
    -1.0f,           // stereo coupling (full 16x16 Hadamard)
    0.0f, 0.0f, 5000.0f,  // output EQ (runtime overrides)
    0.0f, 0.0f, 0.7f,
    false,           // no saturation
    0.0f,            // lateOnsetMs
    0.35f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio
    0, nullptr,      // custom ER
    nullptr, nullptr,
    1.0f, 0.0f, -1,  // dattorroDelayScale, hybridBlend, hybridSecondaryAlgo
    0.10f,           // lateBloomLevel — compensate DV decay_ratio shortfall vs VV

    150.0f, 200.0f, 600.0f, 800.0f,  // bloom timing

    { 0.00f, 0.00f, 0.00f, 0.00f },  // lateBloomBandOffset

    0.0f,  // bodyBloomLevel (per-band only)

    100.0f, 50.0f, 300.0f, 100.0f,  // body bloom timing

    { -0.30f, 0.00f, 0.00f, 0.00f },  // bodyBloomBandOffset
};

// Preset "Fat Drums" (VV-derived FDN)

// Preset "Large Plate" (VV-derived FDN)
// Preset "Rich Plate" — DattorroTank, targeting EMT 140 "medium" damper, variant 4 (6.1s, ~4.3kHz centroid).
static constexpr AlgorithmConfig kPresetRichPlate = {
    "PresetRichPlate",
    {   103,   235,   336,   346,  1507,  1847,  2030,  2282,
       2423,  2567,  2704,  2845,  3265,  3442,  4999,  6260 },
    { 0, 2, 4, 6, 8, 10, 12, 15 },
    { 1, 3, 5, 7, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.05f, 0.85f,    // ER level (low — plate), time scale
    0.22f,           // late gain
    0.75f, 13.0f,    // mod: depth, rate
    0.78f, 1.50f, 1.10f, // damping: treble=0.78 (dark-balanced), bass=1.10 (warm body)
    3000.0f,         // high crossover (matches kVvHighCrossoverHz in RichPlatePreset.cpp)
    0.55f,           // airDampingScale (matches kVvAirDampingScale in RichPlatePreset.cpp)
    0.5f, 1.5f,      // size range
    0.15f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping (runtime override)
    0.0f,            // structural LF damping
    8.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    true,            // useDattorroTank: ON
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled
    0.0f, 0.0f,     // gate: disabled
    12000.0f, 2000.0f, 0.50f,  // ER air absorption, decorr
    8.00f,           // output gain
    -1.0f,           // stereo coupling (full 16x16 Hadamard)
    0.0f, 0.0f, 5000.0f,  // output EQ (runtime overrides)
    0.0f, 0.0f, 0.7f,
    false,           // no saturation
    0.0f,            // lateOnsetMs
    0.35f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio
    0, nullptr,      // custom ER
    nullptr, nullptr,
    1.0f, 0.0f, -1,  // dattorroDelayScale, hybridBlend, hybridSecondaryAlgo
    0.00f,  // lateBloomLevel (0 = no scalar bloom, only per-band)
    150.0f, 200.0f, 600.0f, 800.0f,  // bloom timing
    { 0.00f, 0.00f, 0.00f, -0.20f },  // lateBloomBandOffset (softer LR2)
};

// Preset "Steel Plate" (VV-derived FDN)

// Preset "Tight Plate" (VV-derived FDN)

// Preset "Vocal Plate" (VV-derived FDN)
// Preset "Vocal Plate" — DattorroTank, targeting EMT 140 "medium" damper (2.75s, ~5.4kHz centroid).
static constexpr AlgorithmConfig kPresetVocalPlate = {
    "PresetVocalPlate",
    {   124,   311,   312,   317,   331,   336,   346,   761,
       1095,  1805,  1945,  2649,  2988,  3750,  4827,  5716 },
    { 1, 3, 5, 6, 9, 10, 12, 15 },
    { 0, 2, 4, 7, 8, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.05f, 0.85f,    // ER level (low — plates have minimal early reflections), time scale
    0.22f,           // late gain
    0.75f, 13.0f,    // mod: depth, rate
    0.89f, 1.50f, 0.94f, // damping: treble, ceiling, bass
    5000.0f,         // high crossover — EMT 140 medium centroid target
    0.80f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.15f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping (runtime override)
    0.0f,            // structural LF damping
    8.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    true,            // useDattorroTank: ON — canonical plate algorithm
    false,           // useQuadTank: OFF
    1.00f,           // decay time scale (no multiplier — knob = literal RT60)
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled
    0.0f, 0.0f,     // gate: disabled
    12000.0f, 2000.0f, 0.50f,  // ER air absorption, decorr
    8.00f,           // output gain
    -1.0f,           // stereo coupling
    0.0f, 0.0f, 5000.0f,  // output EQ (runtime overrides)
    0.0f, 0.0f, 0.7f,
    false,           // no saturation
    0.0f,            // lateOnsetMs
    0.35f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio
    0, nullptr,      // custom ER
    nullptr, nullptr,
};

// Preset "Vox Plate" (VV-derived FDN)

// Preset "Dark Vocal Room" (VV-derived FDN)
static constexpr AlgorithmConfig kPresetDarkChamber = {
    "PresetDarkChamber",
    {   107,   263,   336,   346,   456,   588,  1094,  1243,
       1422,  1677,  1891,  2567,  2771,  2986,  4659,  5349 },
    { 0, 2, 5, 6, 9, 10, 12, 15 },
    { 1, 3, 4, 7, 8, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER level, time scale
    0.22f,           // late gain (FDN native)
    0.75f, 13.0f,    // mod: depth=0.75 (mod_depth_db=-5.2)
    0.90f, 1.50f, 0.97f, // damping: treble=0.90, bass=0.97
    4000.0f,         // high crossover
    0.80f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.15f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping (runtime override)
    0.0f,            // structural LF damping
    8.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF -- native FDN
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled
    0.0f, 0.0f,     // gate: disabled
    12000.0f, 2000.0f, 0.50f,  // ER air absorption, decorr
    8.00f,           // output gain
    -1.0f,           // stereo coupling (full 16x16 Hadamard)
    0.0f, 0.0f, 5000.0f,  // output EQ (runtime overrides)
    0.0f, 0.0f, 0.7f,
    false,           // no saturation
    0.0f,            // lateOnsetMs
    0.35f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio
    0, nullptr,      // custom ER
    nullptr, nullptr,
};

// Preset "Exciting Snare room" (VV-derived FDN)
static constexpr AlgorithmConfig kPresetLiveRoom = {
    "PresetLiveRoom",
    {   102,   317,   331,   336,   346,   452,   938,  1082,
       1502,  1977,  2437,  3520,  3805,  4749,  5769,  6169 },
    { 0, 2, 5, 6, 8, 11, 12, 15 },
    { 1, 3, 4, 7, 9, 10, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER level, time scale
    0.22f,           // late gain (FDN native)
    0.75f, 13.0f,    // mod: depth=0.75 (mod_depth_db=9.4)
    0.88f, 1.50f, 0.79f, // damping: treble=0.88, bass=0.79
    4000.0f,         // high crossover
    0.80f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.15f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping (runtime override)
    0.0f,            // structural LF damping
    8.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF -- native FDN
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled
    0.0f, 0.0f,     // gate: disabled
    12000.0f, 2000.0f, 0.50f,  // ER air absorption, decorr
    8.00f,           // output gain
    -1.0f,           // stereo coupling (full 16x16 Hadamard)
    0.0f, 0.0f, 5000.0f,  // output EQ (runtime overrides)
    0.0f, 0.0f, 0.7f,
    false,           // no saturation
    0.0f,            // lateOnsetMs
    0.35f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio
    0, nullptr,      // custom ER
    nullptr, nullptr,
    1.0f, 0.0f, -1,  // dattorroDelayScale, hybridBlend, hybridSecondaryAlgo
    0.00f,  // lateBloomLevel (0 = no scalar bloom, only per-band)
    150.0f, 200.0f, 600.0f, 800.0f,  // bloom timing
    { 0.00f, 0.00f, 0.95f, 0.00f },  // lateBloomBandOffset: hi_mid boost
    0.0f,  // bodyBloomLevel (disabled)
    100.0f, 50.0f, 300.0f, 100.0f,  // body bloom timing
    { 0.00f, 0.00f, 0.00f, 0.00f },  // bodyBloomBandOffset (disabled)
    6300.0f, 1.20f, -8.0f,  // tailNotch: freq, Q, gainDb (wide cut 5-8k)
    500.0f, 100.0f, 800.0f, 400.0f,  // tailNotch timing
};

// Preset "Fat Snare Room" (VV-derived FDN)

// Preset "Lively Snare Room" (VV-derived FDN)

// Preset "Long Dark 70s Snare Room" (VV-derived FDN)

// Preset "Short Dark Snare Room" (VV-derived FDN)
static constexpr AlgorithmConfig kPresetStudioRoom = {
    "PresetStudioRoom",
    {   170,   307,   311,   312,   317,   331,   336,   346,
       1046,  1565,  2640,  3749,  4808,  5697,  6034,  6599 },
    { 0, 2, 4, 6, 8, 11, 12, 15 },
    { 1, 3, 5, 7, 9, 10, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER level, time scale
    0.22f,           // late gain (FDN native)
    0.75f, 13.0f,    // mod: depth=0.75 (mod_depth_db=8.4)
    0.65f, 1.50f, 0.76f, // damping: treble=0.65, bass=0.76
    4000.0f,         // high crossover
    0.80f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.15f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping (runtime override)
    0.0f,            // structural LF damping
    8.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF -- native FDN
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled
    0.0f, 0.0f,     // gate: disabled
    12000.0f, 2000.0f, 0.50f,  // ER air absorption, decorr
    8.00f,           // output gain
    -1.0f,           // stereo coupling (full 16x16 Hadamard)
    0.0f, 0.0f, 5000.0f,  // output EQ (runtime overrides)
    0.0f, 0.0f, 0.7f,
    false,           // no saturation
    0.0f,            // lateOnsetMs
    0.35f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio
    0, nullptr,      // custom ER
    nullptr, nullptr,
};

// Preset "A Plate" (VV-derived FDN)

// Preset "Clear Chamber" (VV-derived FDN)
static constexpr AlgorithmConfig kPresetBrightChamber = {
    "PresetBrightChamber",
    {   126,   260,   346,   440,  1085,  2287,  2436,  2569,
       2704,  3408,  3680,  3886,  4996,  5658,  6002,  6316 },
    { 2, 3, 4, 6, 9, 11, 12, 15 },
    { 0, 1, 5, 7, 8, 10, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER level, time scale
    0.22f,           // late gain (FDN native)
    0.75f, 13.0f,    // mod: depth=0.75 (mod_depth_db=-5.3)
    0.84f, 1.50f, 0.99f, // damping: treble=0.84, bass=0.99
    4000.0f,         // high crossover
    0.80f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.15f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping (runtime override)
    0.0f,            // structural LF damping
    8.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF -- native FDN
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled
    0.0f, 0.0f,     // gate: disabled
    12000.0f, 2000.0f, 0.50f,  // ER air absorption, decorr
    8.00f,           // output gain
    -1.0f,           // stereo coupling (full 16x16 Hadamard)
    0.0f, 0.0f, 5000.0f,  // output EQ (runtime overrides)
    0.0f, 0.0f, 0.7f,
    false,           // no saturation
    0.0f,            // lateOnsetMs
    0.35f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio
    0, nullptr,      // custom ER
    nullptr, nullptr,
};

// Preset "Fat Plate" (VV-derived FDN)
// Preset "Dark Plate" — DattorroTank, targeting EMT 140 "dark" damper, variant 4 (6.1s, ~4.5kHz centroid).
static constexpr AlgorithmConfig kPresetDarkPlate = {
    "PresetDarkPlate",
    {   124,   261,   317,   331,   336,   346,   440,  1888,
       2061,  2225,  2560,  2735,  3004,  3194,  4991,  5767 },
    { 3, 4, 5, 6, 8, 10, 12, 15 },
    { 0, 1, 2, 7, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.05f, 0.85f,    // ER level (low — plate), time scale
    0.22f,           // late gain
    0.75f, 13.0f,    // mod: depth, rate
    0.75f, 1.50f, 1.05f, // damping: treble=0.75 (dark), bass=1.05 (slight bass emphasis)
    4000.0f,         // high crossover — air split lower for dark target
    0.70f,           // airDampingScale (matches kVvAirDampingScale in DarkPlatePreset.cpp)
    0.5f, 1.5f,      // size range
    0.15f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping (runtime override)
    0.0f,            // structural LF damping
    8.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    true,            // useDattorroTank: ON
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled
    0.0f, 0.0f,     // gate: disabled
    12000.0f, 2000.0f, 0.50f,  // ER air absorption, decorr
    8.00f,           // output gain
    -1.0f,           // stereo coupling (full 16x16 Hadamard)
    0.0f, 0.0f, 5000.0f,  // output EQ (runtime overrides)
    0.0f, 0.0f, 0.7f,
    false,           // no saturation
    0.0f,            // lateOnsetMs
    0.35f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio
    0, nullptr,      // custom ER
    nullptr, nullptr,
    1.0f, 0.0f, -1,  // dattorroDelayScale, hybridBlend, hybridSecondaryAlgo
    0.05f,           // lateBloomLevel (reduced to tame lateTail overshoot)

    150.0f, 200.0f, 600.0f, 800.0f,  // bloom timing

    { 0.00f, 0.00f, 0.00f, -0.30f },  // lateBloomBandOffset (dip 8k tail for freqDecay)

    0.0f,  // bodyBloomLevel (per-band only)

    100.0f, 50.0f, 300.0f, 100.0f,  // body bloom timing

    { 0.00f, 0.45f, 0.00f, 0.00f },  // bodyBloomBandOffset
};

// Preset "Large Chamber" (VV-derived FDN)
static constexpr AlgorithmConfig kPresetLiveChamber = {
    "PresetLiveChamber",
    {   130,   312,   317,   331,   336,   346,   477,  1263,
       2194,  2436,  3458,  3642,  3974,  5209,  5683,  6092 },
    { 0, 1, 3, 7, 9, 11, 12, 15 },
    { 2, 4, 5, 6, 8, 10, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER level, time scale
    0.22f,           // late gain (FDN native)
    0.75f, 13.0f,    // mod: depth=0.75 (mod_depth_db=-5.1)
    0.93f, 1.50f, 0.91f, // damping: treble=0.93, bass=0.91
    4000.0f,         // high crossover
    0.80f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.15f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping (runtime override)
    0.0f,            // structural LF damping
    8.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF -- native FDN
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled
    0.0f, 0.0f,     // gate: disabled
    12000.0f, 2000.0f, 0.50f,  // ER air absorption, decorr
    8.00f,           // output gain
    -1.0f,           // stereo coupling (full 16x16 Hadamard)
    0.0f, 0.0f, 5000.0f,  // output EQ (runtime overrides)
    0.0f, 0.0f, 0.7f,
    false,           // no saturation
    80.0f,           // lateOnsetMs: 80ms ramp to reduce body energy (decay_ratio 0.67x→0.80x)
    0.35f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio
    0, nullptr,      // custom ER
    nullptr, nullptr,
};

// Preset "Large Wood Room" (VV-derived FDN)

// Preset "Live Vox Chamber" (VV-derived FDN)

// Preset "Medium Gate" (VV-derived FDN)
static constexpr AlgorithmConfig kPresetReverse = {
    "PresetReverse",
    {   126,   270,   336,   346,   491,   648,   797,  1042,
       1191,  2183,  2685,  2830,  3490,  3746,  4573,  5003 },
    { 1, 4, 5, 7, 8, 10, 12, 15 },
    { 0, 2, 3, 6, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER level, time scale
    0.22f,           // late gain (FDN native)
    0.75f, 13.0f,    // mod: depth=0.75 (mod_depth_db=8.2)
    1.00f, 1.50f, 0.96f, // damping: treble=1.00, bass=0.96
    4000.0f,         // high crossover
    0.80f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.15f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping (runtime override)
    0.0f,            // structural LF damping
    8.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF -- native FDN
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled
    0.0f, 0.0f,     // gate: disabled
    12000.0f, 2000.0f, 0.50f,  // ER air absorption, decorr
    8.00f,           // output gain
    -1.0f,           // stereo coupling (full 16x16 Hadamard)
    0.0f, 0.0f, 5000.0f,  // output EQ (runtime overrides)
    0.0f, 0.0f, 0.7f,
    false,           // no saturation
    0.0f,            // lateOnsetMs
    0.35f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio
    0, nullptr,      // custom ER
    nullptr, nullptr,
    1.0f, 0.0f, -1,  // dattorroDelayScale, hybridBlend, hybridSecondaryAlgo
    0.20f,           // lateBloomLevel — compensate DV decay_ratio shortfall vs VV
};

// Preset "Rich Chamber" (VV-derived FDN)

// Preset "Small Chamber1" (VV-derived FDN)
static constexpr AlgorithmConfig kPresetSmallHall = {
    "PresetSmallHall",
    {   141,   312,   317,   331,   336,   346,   477,   731,
       1071,  1208,  1349,  1765,  2030,  2575,  3060,  3922 },
    { 1, 3, 5, 6, 9, 10, 12, 15 },
    { 0, 2, 4, 7, 8, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER level, time scale
    0.22f,           // late gain (FDN native)
    0.75f, 13.0f,    // mod: depth=0.75 (mod_depth_db=1.0)
    0.96f, 1.50f, 1.31f, // damping: treble=0.96, bass=1.31
    4000.0f,         // high crossover
    0.80f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.15f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping (runtime override)
    0.0f,            // structural LF damping
    8.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF -- native FDN
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled
    0.0f, 0.0f,     // gate: disabled
    12000.0f, 2000.0f, 0.50f,  // ER air absorption, decorr
    8.00f,           // output gain
    -1.0f,           // stereo coupling (full 16x16 Hadamard)
    0.0f, 0.0f, 5000.0f,  // output EQ (runtime overrides)
    0.0f, 0.0f, 0.7f,
    false,           // no saturation
    0.0f,            // lateOnsetMs
    0.35f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio
    0, nullptr,      // custom ER
    nullptr, nullptr,
    1.0f, 0.0f, -1,  // dattorroDelayScale, hybridBlend, hybridSecondaryAlgo
    0.25f,           // lateBloomLevel — compensate DV decay_ratio shortfall vs VV

    150.0f, 200.0f, 600.0f, 800.0f,  // bloom timing

    { 0.00f, 0.00f, 0.55f, 0.00f },  // lateBloomBandOffset
};

// Preset "Small Chamber2" (VV-derived FDN)

// Preset "Snare Plate" (VV-derived FDN)
static constexpr AlgorithmConfig kPresetDrumChamber = {
    "PresetDrumChamber",
    {    92,   232,   331,   336,   346,   427,   563,  1349,
       1975,  2170,  2489,  2622,  3184,  3521,  4745,  6071 },
    { 1, 3, 5, 6, 8, 10, 12, 15 },
    { 0, 2, 4, 7, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER level, time scale
    0.22f,           // late gain (FDN native)
    0.75f, 13.0f,    // mod: depth=0.75 (mod_depth_db=-5.3)
    0.98f, 1.50f, 1.00f, // damping: treble=0.98, bass=1.00
    4000.0f,         // high crossover
    0.80f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.15f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping (runtime override)
    0.0f,            // structural LF damping
    8.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF -- native FDN
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled
    0.0f, 0.0f,     // gate: disabled
    12000.0f, 2000.0f, 0.50f,  // ER air absorption, decorr
    8.00f,           // output gain
    -1.0f,           // stereo coupling (full 16x16 Hadamard)
    0.0f, 0.0f, 5000.0f,  // output EQ (runtime overrides)
    0.0f, 0.0f, 0.7f,
    false,           // no saturation
    0.0f,            // lateOnsetMs
    0.35f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio
    0, nullptr,      // custom ER
    nullptr, nullptr,
};

// Preset "Thin Plate" (VV-derived FDN)
// Preset "Bright Plate" — DattorroTank, targeting EMT 140 "bright" damper, variant 3 (4.6s, ~6.3kHz centroid).
static constexpr AlgorithmConfig kPresetBrightPlate = {
    "PresetBrightPlate",
    {   125,   336,   346,   544,   685,   820,  1007,  1538,
       1699,  1845,  2223,  2414,  2563,  2970,  3887,  4240 },
    { 0, 2, 4, 7, 8, 11, 12, 15 },
    { 1, 3, 5, 6, 9, 10, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.05f, 0.85f,    // ER level (low — plate), time scale
    0.22f,           // late gain
    0.75f, 13.0f,    // mod: depth, rate
    0.92f, 1.50f, 0.85f, // damping: treble=0.92 (extended bright), bass=0.85
    6000.0f,         // high crossover — air split for 6.3 kHz centroid
    0.85f,           // airDampingScale (matches kVvAirDampingScale in BrightPlatePreset.cpp)
    0.5f, 1.5f,      // size range
    0.15f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping (runtime override)
    0.0f,            // structural LF damping
    8.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    true,            // useDattorroTank: ON
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled
    0.0f, 0.0f,     // gate: disabled
    12000.0f, 2000.0f, 0.50f,  // ER air absorption, decorr
    8.00f,           // output gain
    -1.0f,           // stereo coupling (full 16x16 Hadamard)
    0.0f, 0.0f, 5000.0f,  // output EQ (runtime overrides)
    0.0f, 0.0f, 0.7f,
    false,           // no saturation
    60.0f,           // lateOnsetMs: 60ms ramp (decay_ratio 0.69x->0.80x)
    0.35f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio
    0, nullptr,      // custom ER
    nullptr, nullptr,
    1.0f, 0.0f, -1,  // dattorroDelayScale, hybridBlend, hybridSecondaryAlgo
    0.02f,           // lateBloomLevel — compensate DV decay_ratio shortfall vs VV
};

// Preset "Tiled Room" — QuadTank with Chamber delays (proven 18/20 baseline)
// QuadTank's 4 cross-coupled allpass loops give passable mod_depth, decay,
// centroid, and tail_slope. The 4kHz band freq_decay remains a structural
// limitation (~2.97x vs threshold 0.90).

// Preset "Ambience" (VV-derived FDN)

// Preset "Ambience Plate" (VV-derived FDN)

// Preset "Ambience Tiled Room" (VV-derived FDN)

// Preset "Big Ambience Gate" (VV-derived FDN)
static constexpr AlgorithmConfig kPresetModulated = {
    "PresetModulated",
    {   157,   311,   312,   317,   331,   336,   346,   876,
       1009,  1813,  1957,  2134,  2561,  2703,  2933,  5665 },
    { 0, 2, 4, 6, 8, 10, 11, 15 },
    { 1, 3, 5, 7, 9, 12, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER level, time scale
    0.22f,           // late gain (FDN native)
    0.75f, 13.0f,    // mod: depth=0.75 (mod_depth_db=8.9)
    1.00f, 1.50f, 1.07f, // damping: treble=1.00, bass=1.07
    4000.0f,         // high crossover
    0.80f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.15f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping (runtime override)
    0.0f,            // structural LF damping
    8.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF -- native FDN
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled
    0.0f, 0.0f,     // gate: disabled
    12000.0f, 2000.0f, 0.50f,  // ER air absorption, decorr
    8.00f,           // output gain
    -1.0f,           // stereo coupling (full 16x16 Hadamard)
    0.0f, 0.0f, 5000.0f,  // output EQ (runtime overrides)
    0.0f, 0.0f, 0.7f,
    false,           // no saturation
    0.0f,            // lateOnsetMs
    0.35f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio
    0, nullptr,      // custom ER
    nullptr, nullptr,
    1.0f, 0.0f, -1,  // dattorroDelayScale, hybridBlend, hybridSecondaryAlgo
    0.30f,           // lateBloomLevel
    150.0f, 200.0f, 600.0f, 800.0f,  // bloom timing
    { 0.75f, 0.30f, 0.15f, 0.20f },  // lateBloomBandOffset: lo_mid strong + mid moderate + hi support for centroid
};

// Preset "Cross Stick Room" (VV-derived FDN)

// Preset "Drum Air" (VV-derived FDN)
static constexpr AlgorithmConfig kPresetShimmer = {
    "PresetShimmer",
    {   206,   311,   312,   317,   331,   336,   346,   369,
        575,   939,  1302,  1457,  1593,  1824,  1971,  2182 },
    { 1, 3, 5, 7, 8, 11, 12, 15 },
    { 0, 2, 4, 6, 9, 10, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER level, time scale
    0.22f,           // late gain (FDN native)
    0.75f, 13.0f,    // mod: depth=0.75 (mod_depth_db=8.7)
    0.81f, 1.50f, 0.65f, // damping: treble=0.81, bass=0.65
    4000.0f,         // high crossover
    0.80f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.15f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping (runtime override)
    0.0f,            // structural LF damping
    8.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF -- native FDN
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled
    0.0f, 0.0f,     // gate: disabled
    12000.0f, 2000.0f, 0.50f,  // ER air absorption, decorr
    8.00f,           // output gain
    -1.0f,           // stereo coupling (full 16x16 Hadamard)
    0.0f, 0.0f, 5000.0f,  // output EQ (runtime overrides)
    0.0f, 0.0f, 0.7f,
    false,           // no saturation
    0.0f,            // lateOnsetMs
    0.35f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio
    0, nullptr,      // custom ER
    nullptr, nullptr,
};

// Preset "Gated Snare" (VV-derived FDN)
static constexpr AlgorithmConfig kPresetInfinite = {
    "PresetInfinite",
    {   133,   312,   317,   331,   336,   346,   375,   521,
        808,  1010,  2472,  2708,  2889,  3523,  3929,  4721 },
    { 0, 3, 5, 6, 9, 10, 12, 15 },
    { 1, 2, 4, 7, 8, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER level, time scale
    0.22f,           // late gain (FDN native)
    0.75f, 13.0f,    // mod: depth=0.75 (mod_depth_db=8.8)
    0.71f, 1.50f, 1.12f, // damping: treble=0.71, bass=1.12
    4000.0f,         // high crossover
    0.80f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.15f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping (runtime override)
    0.0f,            // structural LF damping
    8.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF -- native FDN
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled
    0.0f, 0.0f,     // gate: disabled
    12000.0f, 2000.0f, 0.50f,  // ER air absorption, decorr
    8.00f,           // output gain
    -1.0f,           // stereo coupling (full 16x16 Hadamard)
    0.0f, 0.0f, 5000.0f,  // output EQ (runtime overrides)
    0.0f, 0.0f, 0.7f,
    false,           // no saturation
    0.0f,            // lateOnsetMs
    0.35f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio
    0, nullptr,      // custom ER
    nullptr, nullptr,
    1.0f, 0.0f, -1,  // dattorroDelayScale, hybridBlend, hybridSecondaryAlgo
    0.18f,           // lateBloomLevel — compensate DV decay_ratio shortfall vs VV
};

// Preset "Large Ambience" (VV-derived FDN)

// Preset "Large Gated Snare" (VV-derived FDN)

// Preset "Med Ambience" (VV-derived FDN)
static constexpr AlgorithmConfig kPresetVocalChamber = {
    "PresetVocalChamber",
    {   123,   261,   346,   481,   662,  1072,  1552,  1700,
       2063,  2373,  2856,  3808,  4496,  4972,  5446,  6327 },
    { 0, 2, 5, 7, 9, 10, 12, 15 },
    { 1, 3, 4, 6, 8, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER level, time scale
    0.22f,           // late gain (FDN native)
    0.75f, 13.0f,    // mod: depth=0.75 (mod_depth_db=-5.3)
    0.87f, 1.50f, 0.93f, // damping: treble=0.87, bass=0.93
    4000.0f,         // high crossover
    0.80f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.15f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    5500.0f,         // structural HF damping — per-pass LP tames hi_tail buildup
    0.0f,            // structural LF damping
    8.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF -- native FDN
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled
    0.0f, 0.0f,     // gate: disabled
    12000.0f, 2000.0f, 0.50f,  // ER air absorption, decorr
    8.00f,           // output gain
    -1.0f,           // stereo coupling (full 16x16 Hadamard)
    0.0f, 0.0f, 5000.0f,  // output EQ (runtime overrides)
    0.0f, 0.0f, 0.7f,
    false,           // no saturation
    0.0f,            // lateOnsetMs
    0.35f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio
    0, nullptr,      // custom ER
    nullptr, nullptr,
};

// Preset "Short Vocal Ambience" (VV-derived FDN)

// Preset "Small Ambience" (VV-derived FDN)

// Preset "Small Drum Room" (VV-derived FDN)
static constexpr AlgorithmConfig kPresetDrumRoom = {
    "PresetDrumRoom",
    {   209,   346,   400,   721,  1079,  1222,  1830,  1974,
       2241,  2539,  2700,  2840,  3111,  3408,  3789,  4232 },
    { 0, 3, 5, 7, 8, 10, 12, 15 },
    { 1, 2, 4, 6, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER level, time scale
    0.22f,           // late gain (FDN native)
    0.75f, 13.0f,    // mod: depth=0.75 (mod_depth_db=8.4)
    1.00f, 1.50f, 1.00f, // damping: treble=1.00, bass=1.00
    4000.0f,         // high crossover
    0.80f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.15f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping (runtime override)
    0.0f,            // structural LF damping
    8.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF -- native FDN
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale
    0.0f, 0, 1.0f,  // dual-slope disabled
    0.0f, 0.0f,     // short-decay boost: disabled
    0.0f, 0.0f,     // gate: disabled
    12000.0f, 2000.0f, 0.50f,  // ER air absorption, decorr
    8.00f,           // output gain
    -1.0f,           // stereo coupling (full 16x16 Hadamard)
    0.0f, 0.0f, 5000.0f,  // output EQ (runtime overrides)
    0.0f, 0.0f, 0.7f,
    false,           // no saturation
    0.0f,            // lateOnsetMs
    0.35f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio
    0, nullptr,      // custom ER
    nullptr, nullptr,
    1.0f, 0.0f, -1,  // dattorroDelayScale, hybridBlend, hybridSecondaryAlgo
    0.48f,           // lateBloomLevel — tuned for decay_ratio/spectral balance
    150.0f, 200.0f, 1200.0f, 1200.0f,  // bloom timing — extended hold/release
    { 0.00f, 0.08f, 0.00f, -0.28f },  // lateBloomBandOffset: hi cut to keep spectral
    0.0f,            // bodyBloomLevel
    100.0f, 50.0f, 300.0f, 100.0f,
    { 0.00f, 0.00f, 0.00f, 0.00f },
    0.0f, 2.00f, 0.0f,  // tailNotch disabled
    500.0f, 100.0f, 800.0f, 400.0f,
    0.0f, 1.0f,      // chorusDepthDefault, chorusRateDefault
    0.0f, 30.0f,     // tailFloorGate disabled (was -30 for VV-matching, killed RT60)
    -60.0f,          // onsetBurstPeakDb: disabled
    20.0f, 2.0f, 20.0f, 80.0f,
    2000.0f, 12000.0f, 0.5f,
    0.40f, 0.8f,     // stereoDecorrAmount: light + ultra-short delay (best spectral/stereo balance)
};

// Preset "Snare Ambience" (VV-derived FDN)

// Preset "Tight Ambience Gate" (VV-derived FDN)
static constexpr AlgorithmConfig kPresetTightRoom = {
    "PresetTightRoom",
    {   102,   262,   317,   331,   336,   346,   662,   890,
       1377,  1893,  2051,  2188,  2379,  2844,  3249,  5216 },
    { 0, 1, 3, 6, 8, 10, 12, 15 },
    { 2, 4, 5, 7, 9, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER level, time scale
    0.22f,           // late gain (FDN native)
    0.75f, 13.0f,    // mod: depth=0.75 (mod_depth_db=8.8)
    0.51f, 1.50f, 0.69f, // damping: treble=0.51, bass=0.69
    4000.0f,         // high crossover
    0.80f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.15f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping (runtime override)
    0.0f,            // structural LF damping
    8.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF -- native FDN
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled
    0.0f, 0.0f,     // gate: disabled
    12000.0f, 2000.0f, 0.50f,  // ER air absorption, decorr
    8.00f,           // output gain
    -1.0f,           // stereo coupling (full 16x16 Hadamard)
    0.0f, 0.0f, 5000.0f,  // output EQ (runtime overrides)
    0.0f, 0.0f, 0.7f,
    false,           // no saturation
    0.0f,            // lateOnsetMs
    0.35f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio
    0, nullptr,      // custom ER
    nullptr, nullptr,
    1.0f, 0.0f, -1,  // dattorroDelayScale, hybridBlend, hybridSecondaryAlgo
    0.30f,           // lateBloomLevel — compensate DV decay_ratio shortfall vs VV
};

// Preset "Trip Hop Snare" (VV-derived FDN)

// Preset "Very Small Ambience" (VV-derived FDN)
static constexpr AlgorithmConfig kPresetVocalBooth = {
    "PresetVocalBooth",
    {   119,   292,   307,   311,   312,   317,   331,   336,
        346,   759,   997,  1136,  1285,  1824,  2182,  2783 },
    { 0, 1, 3, 6, 9, 10, 12, 15 },
    { 2, 4, 5, 7, 8, 11, 13, 14 },
    { 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f },
    0.55f, 0.45f,    // input diffusion
    0.35f,           // output diffusion scale
    20000.0f,        // bandwidth
    0.65f, 0.85f,    // ER level, time scale
    0.22f,           // late gain (FDN native)
    0.75f, 13.0f,    // mod: depth=0.75 (mod_depth_db=9.0)
    0.84f, 1.50f, 0.87f, // damping: treble=0.84, bass=0.87
    4000.0f,         // high crossover
    0.80f,           // airDampingScale
    0.5f, 1.5f,      // size range
    0.15f,           // ER crossfeed
    0.0f,            // inline diffusion
    1.0f,            // mod depth floor
    0.0f,            // structural HF damping (runtime override)
    0.0f,            // structural LF damping
    8.0f,            // noise mod
    0.10f,           // Hadamard perturbation
    1.0f,            // ER gain exponent
    false,           // useWeightedGains
    true,            // useHouseholderFeedback
    false,           // useDattorroTank: OFF -- native FDN
    false,           // useQuadTank: OFF
    1.50f,           // decay time scale
    0.0f, 0, 1.0f,  // dual-slope: disabled
    0.0f, 0.0f,     // short-decay boost: disabled
    0.0f, 0.0f,     // gate: disabled
    12000.0f, 2000.0f, 0.50f,  // ER air absorption, decorr
    8.00f,           // output gain
    -1.0f,           // stereo coupling (full 16x16 Hadamard)
    0.0f, 0.0f, 5000.0f,  // output EQ (runtime overrides)
    0.0f, 0.0f, 0.7f,
    false,           // no saturation
    0.0f,            // lateOnsetMs
    0.35f,           // lateFeedForwardLevel
    0.0f,            // crestLimitRatio
    0, nullptr,      // custom ER
    nullptr, nullptr,
    1.0f, 0.0f, -1,  // dattorroDelayScale, hybridBlend, hybridSecondaryAlgo
    0.25f,           // lateBloomLevel — compensate DV decay_ratio shortfall vs VV
};

// === END AUTO-GENERATED ===

inline const AlgorithmConfig& getAlgorithmConfig (int index)
{
    static constexpr const AlgorithmConfig* kAlgorithms[kNumAlgorithms] = {
        &kPresetVocalPlate,     //  0 Plates
        &kPresetDrumPlate,      //  1
        &kPresetBrightPlate,    //  2
        &kPresetDarkPlate,      //  3
        &kPresetRichPlate,      //  4
        &kPresetSmallHall,      //  5 Halls
        &kPresetMediumHall,     //  6
        &kPresetLargeHall,      //  7
        &kPresetVocalHall,      //  8
        &kPresetCathedral,      //  9
        &kPresetDrumRoom,       // 10 Rooms
        &kPresetVocalBooth,     // 11
        &kPresetStudioRoom,     // 12
        &kPresetLiveRoom,       // 13
        &kPresetTightRoom,      // 14
        &kPresetVocalChamber,   // 15 Chambers
        &kPresetDrumChamber,    // 16
        &kPresetBrightChamber,  // 17
        &kPresetDarkChamber,    // 18
        &kPresetLiveChamber,    // 19
        &kPresetShimmer,        // 20 Special
        &kPresetReverse,        // 21
        &kPresetModulated,      // 22
        &kPresetInfinite,       // 23
        &kPresetGated           // 24
    };
    if (index < 0 || index >= kNumAlgorithms)
        index = 0; // Fall back to first preset (PresetVocalPlate)
    return *kAlgorithms[index];
}
