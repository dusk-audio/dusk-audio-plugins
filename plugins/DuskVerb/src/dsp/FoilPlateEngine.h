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
    float envRampCoeff   = 0.0f;   // exp coeff for the 0.30 → 1.0 ramp
    float minGain        = 0.30f;  // floor when a fresh transient fires
    float triggerThresh  = 0.05f;  // rising-edge threshold (linear amp)
    float triggerRatio   = 4.0f;   // input must exceed peakHold × this to fire

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

    // Cross-feed gain on each branch's late tap → other branch's input.
    // Polarity-flipped on the right channel (R = +lFeedback, L = −rFeedback)
    // for the same decorrelation strategy that worked in PlateEngine.
    static constexpr float kCrossFeedGain = 0.62f;

    // Internal predelay — short (only the onset-envelope correction is
    // structural; predelay just keeps the dry-input peak detector
    // sample-aligned with the diffuser output).
    static constexpr int kPredelaySamplesAt48k = 384;     // 8 ms

    // Onset envelope shape constants. The reference target's IR peaks
    // ~30 ms in (front of tail is energy-buildup, not energy-spike).
    // τ = 30 ms, min gain 0.30 = −10.5 dB floor at transient onset,
    // recovery to unity by ~80 ms (one C80 window).
    static constexpr float kOnsetTauMs   = 30.0f;
    static constexpr float kOnsetMinGain = 0.30f;

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
        foil_plate::DelayLine        predelay;
        foil_plate::Allpass          in1, in2;
        foil_plate::LR4BandSplit     split;
        foil_plate::BandReverberator bassRev, midRev, trebleRev;
        foil_plate::OnsetEnvelope    onsetEnv;

        // Cross-feed sample carried over to the next iteration —
        // identical mechanism to PlateEngine for stable feedback.
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
