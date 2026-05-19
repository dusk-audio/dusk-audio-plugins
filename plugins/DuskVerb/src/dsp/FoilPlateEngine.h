#pragma once

#include "DspUtils.h"

#include <cstdint>
#include <vector>

// =====================================================================
// FoilPlateEngine — vintage foil-plate emulation, second-generation
// =====================================================================
//
// Purpose-built engine for the vintage foil-plate factory presets (Rich
// Plate and adjacent). Replaces PlateEngine for those targets after
// PlateEngine plateaued at 7/8 RT60 + Volume + stereo_correlation
// within JND but could NOT close on:
//
//   16 kHz RT60 Δ −0.16 s  — HF cascade roll-off
//   EDT Δ −0.44 s          — front-loaded onset
//   C80 Δ +3.85 dB         — too much energy in first 80 ms
//   D50 Δ +4.74 dB         — too much energy in first 50 ms
//   stereo_corr_stability  — random-walk LFO L/R drift over time
//
// All five remaining failures share root causes that are not tunable
// from preset parameters; FoilPlateEngine attacks them at the
// architecture level.
//
// =====================================================================
// Architectural pillars (replacing PlateEngine's failure modes)
// =====================================================================
//
//   Pillar 1.  ONSET ENVELOPE on the wet output.
//     The vintage foil plate's IR builds up rather than spikes — energy
//     accumulates over ~30-50 ms before peaking. PlateEngine's input
//     diffuser pushes the impulse straight into the density cascade, so
//     the engine's IR peaks at sample 0 (modulo predelay). Fix: detect
//     transients on the dry input, then ramp wet-output gain from
//     `kOnsetMinGain` (~0.30) up to 1.0 with τ ≈ 30 ms after each
//     transient. Sustained content stays at gain 1.0 (no audible
//     attack artifact during continuous music). On an isolated impulse,
//     the first 50 ms of wet output is attenuated by 6-12 dB → C80,
//     D50, EDT all drop toward the reference target.
//
//   Pillar 2.  DETERMINISTIC SINE LFOs (drop-in for RandomWalkLFO).
//     PlateEngine uses a per-AP xorshift random walk with smoothstep
//     interp. Independent L/R seeds give nice decorrelation but the two
//     walks drift apart over 100 ms windows → stereo_corr_stability
//     drifts above JND. Fix: each LFO is a phase accumulator + cosine
//     read. L gets phase 0°, R gets phase 180° — perfectly
//     anti-correlated modulation that NEVER drifts. Result:
//     stereo_corr_stability locked to ≤ 0.05 by construction.
//
//   Pillar 3.  FLAT DIFFUSION CASCADE (2 APs total, not 6).
//     PlateEngine cascades 6 density APs in series; each AP introduces
//     a small HF loss from its delay-line interpolation + an additional
//     resonant pole from its g coefficient. Compounded over 6 stages
//     this swallows ~3 dB at 16 kHz — exactly the band that won't close
//     to JND. Fix: drop to 2 input APs with low g = 0.55. Diffusion
//     density comes from the per-band feedback loop instead (which is
//     short and HF-flat). 16 kHz RT60 stays locked to whatever the
//     treble band's feedback gain is set to.
//
//   Pillar 4.  PER-BAND INDEPENDENT REVERBERATORS (LR4 split, 3 loops).
//     PlateEngine's figure-8 topology recirculates the FULL signal, so
//     bass mult lifts mid-band measured RT60 via LFO sidebands +
//     LR4 phase-leakage. Fix: split input into 3 LR4 bands UPSTREAM of
//     any feedback, then run 3 independent short-delay + feedback loops
//     (one per band). Per-band RT60 is set by per-band feedback gain
//     and is fully decoupled. Closes 250 Hz / 500 Hz / 16 kHz at once.
//
// =====================================================================
// Signal routing
// =====================================================================
//
//                                                       ┌─ envL ──┐
//   inputL ─► [predelay 8 ms] ─► [diffuser L: 2 APs] ──┬──► split L ──► bassRev L ──┐
//                  │                                   │                ►  midRev L ─┤
//                  ▼                                   │                ► trebleRev L ┤
//          [transient detect ─► attack/release ─►      │                              │
//           gain ramp τ=30ms ──────────────────────────┤                              ▼
//                                                      │                          sum ─► × envL ─► outputL
//                                                      │                              ▲
//                                                      │                              │
//                                                      │            cross-feed (−) ──┘
//                                                      │                              ▲
//                                                      ▼                              │
//                                                  (mirror chain on R, sine LFO phase
//                                                   offset 180°, cross-feed → +)
//
//   The peak detector watches the *predelayed dry input*, so the
//   envelope's gain ramp is time-aligned with the wet output's first
//   reflections — pulling the IR down where it would otherwise peak.
//
// =====================================================================
// Memory + CPU budget
// =====================================================================
//   Delay buffers   : 2 predelays + 2 input APs/branch + 3 band loops/branch
//                     ≈ 8 KiB total at 48 kHz (well under PlateEngine's 30 KiB)
//   Per-sample work : 4 biquads (LR4 split) + 2 AP reads + 3 delay reads
//                     + 3 sine LFO advances + 1 envelope tick per channel
//                     ≈ ⅔ the PlateEngine cost (no density cascade)

namespace foil_plate
{

// ----- Forward declarations of the small DSP primitives. Bodies live
//       in FoilPlateEngine.cpp so the header stays a contract surface. -----

// Plain non-recursive delay line — short ring buffer with integer reads.
struct DelayLine
{
    std::vector<float> buffer;
    int writePos = 0;
    int mask     = 0;

    void allocate (int maxSamples);
    void clear();
    void  write (float sample);
    float read  (int delaySamples) const;
    float readInterpolated (float delaySamples) const;   // 6-point Lagrange
};

// Schroeder allpass with optional sine-LFO modulated read for chorus-style
// jitter. Single instance, low g — see Pillar 3 above. No coefficient
// cascade compounding.
struct Allpass
{
    std::vector<float> buffer;
    int   writePos        = 0;
    int   mask            = 0;
    int   delaySamples    = 0;
    float modDepthSamples = 0.0f;        // 0 = no modulation (clean Schroeder AP)

    void  allocate (int maxSamples);
    void  clear();
    float process (float input, float g, float modValue);
};

// Deterministic sine LFO. Phase accumulator + std::sin (cheap at the
// sub-audio rates we use here: < 5 Hz per LFO). Pillar 2.
struct SineLFO
{
    float phase    = 0.0f;
    float phaseInc = 0.0f;
    float depth    = 0.0f;

    void  prepare (float sampleRate, float rateHz, float initialPhaseRad);
    void  setDepth (float d) { depth = d; }
    float next();                         // returns sin(phase) × depth
};

// LR4 3-band split (Linkwitz-Riley 24 dB/oct). The *sum-minus mid* mode
// used in PlateEngine is replaced here with an explicit band-pass
// (LP(fHigh) − LP(fLow)). Eliminates the phase-ripple leakage that bled
// bass band into mid RT60 in the old engine.
struct LR4BandSplit
{
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;
        void  designLP (float fcHz, float sr);
        void  designHP (float fcHz, float sr);
        float process (float x);
        void  reset() { z1 = z2 = 0.0f; }
    };

    Biquad lpA,   lpB;            // bass band: LP(fLow) cascaded
    Biquad hpA,   hpB;            // treble band: HP(fHigh) cascaded
    Biquad midLpA, midLpB;        // mid band: LP(fHigh) cascaded (then minus bass)

    void  prepare (float sr);
    void  setCrossovers (float fLowHz, float fHighHz, float sr);
    void  reset();
    void  split (float x, float& outBass, float& outMid, float& outTreble);
};

// Single per-band reverberator. One delay + feedback + optional sine-LFO
// modulated read + an optional pair of cascaded biquads in the feedback
// path. The biquad pair lets each band's loop re-filter its own
// recirculated signal so a bass loop's harmonics never bleed into mid
// or treble measurement bands (the LR4 split upstream can't catch them
// once they're in the loop — they have to be killed *inside* the loop).
//
//   Bass loop:    feedback biquads → LR4 LP at fLow  (passes only bass)
//   Mid loop:     feedback biquads → bypassed         (mid passband already shaped upstream)
//   Treble loop:  feedback biquads → LR4 HP at fHigh (passes only treble)
//
// The mid-band loop runs broadband because most of the mid octaves
// (1k-8k) sit comfortably between the bass and treble xovers, and a
// passive BP in the feedback would compromise pulse response.
struct BandReverberator
{
    DelayLine delay;
    SineLFO   modLFO;
    LR4BandSplit::Biquad fbBiquadA, fbBiquadB;   // 4th-order LR4 in feedback
    bool      fbBiquadEnabled = false;
    int       baseDelaySamples = 0;
    float     feedbackGain     = 0.0f;

    enum class FbFilterType { Bypass, LowPass, HighPass };

    void  prepare (double sr, int baseDelay, float modRateHz, float modPhaseRad);
    void  clear();
    void  setFeedbackFilter (FbFilterType type, float fcHz, float sr);
    float process (float input);
};

// Onset envelope follower — peak-detect + slow-attack gain ramp.
// Pillar 1.
struct OnsetEnvelope
{
    float peakHold       = 0.0f;
    float envelope       = 1.0f;
    float prevAbsInput   = 0.0f;
    float peakDecayCoeff = 0.0f;   // exponential decay of the peak hold
    float envRampCoeff   = 0.0f;   // exp coeff for the minGain → 1.0 ramp
    float minGain        = 0.15f;  // floor when a fresh transient fires
    float triggerThresh  = 0.05f;  // rising-edge threshold (linear amp)
    float triggerRatio   = 4.0f;   // input must exceed peakHold × this to fire
    int   holdSamples    = 0;      // samples to hold env at minGain after trigger
    int   holdRemaining  = 0;      // counter, decremented per sample
    float pendingTauSec_ = 0.0f;   // stashed by setShape, consumed in prepare
    float pendingHoldMs_ = 0.0f;   // stashed by setShape, consumed in prepare

    // Configure shape before the first call. holdMs holds env at minGain
    // for that duration after each fresh trigger; tauSec then ramps it
    // back toward 1.0 with that exponential time constant; minGainArg is
    // the floor (lowest wet-output gain during the hold period). The
    // hold-then-ramp shape suppresses the entire 0–50 ms D50 / C80
    // window without inflating RT60 (env fully recovered before the
    // T20 fit region opens).
    void  setShape (float holdMs, float tauSec, float minGainArg);
    void  prepare (float sr);
    void  clear();
    // input: the *dry* signal that's about to enter the tank. Returns the
    // gain to multiply against the wet output for this sample.
    float process (float input);
};

} // namespace foil_plate

// =====================================================================
// FoilPlateEngine — top-level engine class
// =====================================================================
class FoilPlateEngine
{
public:
    FoilPlateEngine();

    void prepare (double sampleRate, int maxBlockSize);
    void clearBuffers();
    void process (const float* inputL, const float* inputR,
                  float* outputL, float* outputR, int numSamples);

    // Contract matches PlateEngine / DattorroTank / SixAPTankEngine so
    // DuskVerbEngine's per-engine forwarders stay regular.
    void setDecayTime         (float seconds);
    void setBassMultiply      (float mult);
    void setMidMultiply       (float mult);
    void setTrebleMultiply    (float mult);
    void setCrossoverFreq     (float hz);          // bass ↔ mid
    void setHighCrossoverFreq (float hz);          // mid ↔ treble
    void setSaturation        (float amount);
    void setModDepth          (float depth);
    void setModRate           (float hz);
    void setSize              (float size);
    void setFreeze            (bool frozen);
    void setTankDiffusion     (float amount);
    void setInputHighShelf    (float gainDb, float fcHz);

private:
    // ─────────────────────────────────────────────────────────
    // Tunable constants — bass / mid / treble loop lengths and LFO
    // phase assignments. Drawn from a prime table to keep all delays
    // mutually distinct across L and R (no two paths share a modal
    // frequency).
    // ─────────────────────────────────────────────────────────

    // 2-AP input diffuser (flat cascade — Pillar 3). Low g = 0.55,
    // total smear ≈ 5 ms at 48 kHz.
    static constexpr int kLeftIn1Base   = 142;
    static constexpr int kLeftIn2Base   = 263;
    static constexpr int kRightIn1Base  = 167;
    static constexpr int kRightIn2Base  = 281;
    static constexpr float kInputAPGain = 0.55f;

    // Per-band reverberator delay base lengths. Designed so each band's
    // loop fundamental sits in its own LR4 passband (no out-of-band
    // resonance to pollute neighboring measurement bands).
    //
    // bass:    base 1543 / 1607 samples → fundamentals 15.6 / 14.9 Hz
    //          (DC ≪ 250 Hz; band is shaped by feedback gain, not modal)
    // mid:     base  541 /  571 samples → fundamentals 44 / 42 Hz
    // treble:  base  211 /  227 samples → fundamentals 114 / 106 Hz
    // (Treble fundamental in 125 Hz octave but treble band is HP at
    //  9 kHz so the LR4 split kills it before measurement.)
    static constexpr int kLeftBassBase    = 1543;
    static constexpr int kLeftMidBase     =  541;
    static constexpr int kLeftTrebleBase  =  211;
    static constexpr int kRightBassBase   = 1607;
    static constexpr int kRightMidBase    =  571;
    static constexpr int kRightTrebleBase =  227;

    // LFO rates per band (sub-audio modulation). Distinct rates so the
    // bass / mid / treble jitter never lines up periodically.
    static constexpr float kBassLFORateHz   = 0.31f;
    static constexpr float kMidLFORateHz    = 0.47f;
    static constexpr float kTrebleLFORateHz = 0.71f;

    // L vs R LFO phase offset = π (180°) for every band. Anti-correlated
    // modulation → stereo_corr_stability locked low by construction.
    static constexpr float kRightLFOPhaseRad = 3.14159265358979323846f;

    // Output L↔R cross-feed retired in favour of per-band figure-8
    // coupling (kFigure8Alpha) inside the dual-tank matrix — see below.
    // Kept at zero for API parity / fall-back.
    static constexpr float kCrossFeedGain = 0.00f;

    // Staggered multi-tap input injection. The dry signal is written into
    // a 64-ms ring buffer, then read at 4 different tap times and summed
    // into different nodes of the front-end all-pass network. Mimics a
    // Lexicon-style "specular reflections at predelay taps" topology —
    // distributes the impulse energy across the 0–32 ms window
    // *internally* (no output-side amplitude shaping required). C80, D50,
    // and EDT are shaped by this distribution instead of by a wet-output
    // envelope, which means the per-band RT60 measurements are no longer
    // disturbed by the energy redistribution.
    //
    // Tap times (at 48 kHz) and per-tap weights:
    //   tap0 20 ms  → AP1 input              w0 = 1.00 (primary path)
    //   tap1 27 ms  → AP1 output / AP2 in    w1 = 0.60
    //   tap2 37 ms  → AP2 output / split in  w2 = 0.40
    //   tap3 52 ms  → per-band input (sum)   w3 = 0.20 (still recirculates)
    //   tap4 85 ms  → AP1 input              w4 = 0.45 (secondary peak driver)
    //   tap5 165 ms → AP1 input              w5 = 0.30 (late-tail sustainer)
    //
    // Taps 0..3 are shifted +20 ms vs the dry impulse so the wet output
    // has a 20 ms silent predelay (the Lex anchor measures −71 dB RMS
    // in 0–20 ms then builds up over 20–60 ms).
    //
    // Tap 4 is the SECONDARY-PEAK DRIVER: at 85 ms it fires a second
    // strong primary-path injection that exits the AP / split / band
    // chain ≈ 110 ms after the dry impulse — exactly where the Lex
    // anchor has its 100–120 ms secondary energy bloom.
    //
    // Tap 5 is the LATE-TAIL SUSTAINER: at 165 ms it fires a third
    // injection (output at ≈ 190 ms) that prevents the engine's
    // natural exponential decay from getting 4 dB ahead of the Lex
    // anchor in the 150–250 ms region — closing the late-energy
    // deficit that drives C80 / D50 / EDT positive.
    static constexpr int   kPredelayMaxSamplesAt48k = 9600;   // 200 ms headroom
    static constexpr int   kTap0SamplesAt48k        =  960;   // 20 ms
    static constexpr int   kTap1SamplesAt48k        = 1296;   // 27 ms
    static constexpr int   kTap2SamplesAt48k        = 1776;   // 37 ms
    static constexpr int   kTap3SamplesAt48k        = 2496;   // 52 ms
    static constexpr int   kTap4SamplesAt48k        = 4080;   // 85 ms
    static constexpr int   kTap5SamplesAt48k        = 7920;   // 165 ms
    static constexpr float kTap0Weight              = 0.70f;
    static constexpr float kTap1Weight              = 0.55f;
    static constexpr float kTap2Weight              = 0.40f;
    static constexpr float kTap3Weight              = 0.40f;
    static constexpr float kTap4Weight              = 0.60f;
    static constexpr float kTap5Weight              = 0.50f;

    // Figure-8 cross-coupling — each band's L loop sums the previous
    // sample's R-loop output (scaled by α) into its input, and vice
    // versa. Builds true Dattorro-style figure-8 topology: the signal
    // alternates between the two tanks, so the L and R modal evolution
    // tracks each other (lex anchor's stereo_corr_stability ≈ 0.029 is
    // a consequence of this kind of strong coupling between the two
    // sides of a single physical plate).
    //
    // Strong uniform cross-coupling locks each band's modal evolution
    // L↔R, dropping stereo_corr_stability roughly in proportion to α.
    // α = 0.70 measured corr +0.13 stab 0.10 on Rich Plate — both still
    // out of JND but stab is much closer to lex anchor 0.029 than the
    // dual-independent-reverbs baseline (0.17–0.24).
    static constexpr float kFigure8AlphaBass    = 0.70f;
    static constexpr float kFigure8AlphaMid     = 0.70f;
    static constexpr float kFigure8AlphaTreble  = 0.70f;

    // Output decorrelation mixer. The strong figure-8 coupling drives
    // L/R correlation up (≈ +0.13 with the chosen α). This linear
    // mid-side widener subtracts a small fraction of the opposite-side
    // wet output from each channel:
    //   outL = a·lWet − b·rWet
    //   outR = a·rWet − b·lWet
    // Time-invariant linear → stereo_corr_stability is preserved
    // (linear matrix on a stably-correlated pair stays stably
    // correlated). b ≈ 0.05 brings the residual correlation from
    // ≈ +0.13 down to ≈ +0.03–0.04, matching the lex anchor.
    static constexpr float kOutMixA = 1.0f;
    static constexpr float kOutMixB = 0.05f;

    // Per-band comb DC normalisation factor (1 − g) drops total wet
    // amplitude by ~10 dB at typical RT60 targets — three bands each
    // scaled to unity DC sum to ≈ one full-amplitude impulse response.
    // Multiplying by an output gain restores the dry-equivalent level.
    // 3.0 puts a unity-input impulse at roughly the reference target's
    // measured A-weighted RMS; refine via the tuner round.
    static constexpr float kEngineOutputGain = 3.0f;

    // ─────────────────────────────────────────────────────────
    // Per-channel signal chain.
    // ─────────────────────────────────────────────────────────
    struct Branch
    {
        foil_plate::DelayLine        predelay;       // multi-tap, 64 ms ring
        foil_plate::Allpass          in1, in2;
        foil_plate::LR4BandSplit     split;
        foil_plate::BandReverberator bassRev, midRev, trebleRev;
        foil_plate::OnsetEnvelope    onsetEnv;       // retained, no longer applied

        // Per-band previous-sample outputs. The figure-8 cross-coupling
        // feeds each band's L-loop input from the prior sample's R-loop
        // output (scaled by α), and vice versa, so both tanks evolve
        // together rather than as two independent reverbs.
        float prevBassOut    = 0.0f;
        float prevMidOut     = 0.0f;
        float prevTrebleOut  = 0.0f;

        // Cross-feed sample no longer used (per-band coupling above
        // replaces the legacy output-level cross-feed). Retained at 0
        // so cleared-state semantics stay obvious.
        float crossFeedState = 0.0f;
    };

    Branch leftBranch_;
    Branch rightBranch_;

    // Musical parameters.
    double sampleRate_        = 44100.0;
    bool   prepared_          = false;
    bool   frozen_            = false;
    float  decayTime_         = 1.30f;
    float  bassMultiply_      = 1.00f;
    float  midMultiply_       = 1.00f;
    float  trebleMultiply_    = 1.00f;
    float  crossoverFreq_     = 500.0f;
    float  highCrossoverFreq_ = 9000.0f;
    float  saturationAmount_  = 0.0f;
    float  modDepth_          = 0.30f;
    float  modRate_           = 1.0f;
    float  sizeParam_         = 0.95f;

    // Recompute per-band feedback gains from decay × per-band multiplier.
    // Called whenever decay / mult / size / sampleRate changes.
    void updateBandFeedback();
};
