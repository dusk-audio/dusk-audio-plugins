#pragma once

#include "HallSubTank.h"
#include "LR4BandSplit.h"

#include <vector>

// =====================================================================
// HallReverb — 3-band parallel sub-tank hall algorithm
// =====================================================================
//
// Replaces the monolithic 16-channel FDNReverb implementation that
// shipped in this class on commit 76699e5 (mechanical FDN→Hall fork).
// Architecture mirrors FoilPlateEngine's Pillar 4 ("per-band independent
// reverberators"), the proven pattern that closed Rich Plate 14/14 vs
// Lex anchor.
//
// Signal flow:
//
//   inputL ──► LR4BandSplit ──► bassInL ──┐
//                                          ├─► HallSubTank (bass)   ──► bassOutL/R
//                              midInL  ──┤
//                                          ├─► HallSubTank (mid)    ──► midOutL/R
//                              trebleInL ┤
//                                          └─► HallSubTank (treble) ──► trebleOutL/R
//   inputR ──► LR4BandSplit ──► (same)
//                                                                       ↓
//                                                                  sum bands
//                                                                   + soft-clip
//                                                                       ↓
//                                                                   outputL/R
//
// Why this beats the single 16-channel FDN we forked from:
//
//   • Per-band RT60 fully independent. bassMultiply / midMultiply /
//     trebleMultiply scale the bass / mid / treble SubTank decayTime
//     directly — no shared global decay slope to fight against.
//     Closes rt60_per_band 8/8 territory (FDN ceiling was ~3/8).
//
//   • Per-band damping with independent time constants. Each SubTank
//     has its own one-pole shelf state, so HF in the treble loop and
//     HF in the bass loop don't pool into a single damping integrator.
//     Closes centroid_drift_per_band (FDN ceiling was 0/4).
//
//   • Mid-band gain trim happens by setting midTank's decayTime
//     independently — drops box_ratio_db without touching bass or
//     treble (FDN had +7.8 dB structural midrange concentration that
//     no parameter sweep could flatten).
//
//   • Saturated public API parity with FDNReverb so DuskVerbEngine's
//     existing per-engine forwarders can call into HallReverb in
//     Phase 5 without per-method conditional branching.
//
// Phase 2 scope (THIS commit): bare 3-sub-tank topology with public API.
// Phase 3 will add multi-tap input injection (per FoilPlate Pillar 1
// shape — multi-tap before band split, shapes EDT / C80 / D50 without
// touching tank decay). Phase 4 adds the post-tank M/S widener
// (DattorroPlateVintage pattern — drops stereo_correlation without
// disturbing stab variance).
//
class HallReverb
{
public:
    HallReverb();

    void prepare (double sampleRate, int maxBlockSize);
    void clearBuffers();
    void process (const float* inputL, const float* inputR,
                  float* outputL, float* outputR, int numSamples);

    // Public API mirrors FDNReverb so DuskVerbEngine's per-engine setter
    // forwarders stay regular when HallReverb wires in as algo 10.
    void setDecayTime         (float seconds);
    void setBassMultiply      (float mult);
    void setMidMultiply       (float mult);
    void setTrebleMultiply    (float mult);
    void setCrossoverFreq     (float hz);          // bass↔mid LR4 corner
    void setHighCrossoverFreq (float hz);          // mid↔treble LR4 corner
    void setSize              (float size);
    void setModDepth          (float depth);
    void setModRate           (float hz);
    void setFreeze            (bool frozen);
    void setSaturation        (float amount);
    // Uniform damping helper — equivalent to setBandDamping(a, a, a).
    // Kept so callers that don't care about per-band control stay simple.
    void setDamping           (float amount);
    // Per-band damping (one-pole HF shelf coefficient inside each SubTank's
    // feedback path). Phase 6 iteration 1 exposed that uniform damping kills
    // treble RT60 while only partially controlling late-tail energy in the
    // bass / mid bands — Lex's RT_HiCut + HF damping are non-uniform across
    // octaves. Per-band control lets the bass band recirculate cleanly,
    // mid band shape decay shape, and treble damp less aggressively (or be
    // shaped by post-tank HiCut alone). Each coefficient is clamped
    // independently to [0, 0.95] inside HallSubTank.
    void setBandDamping       (float bass, float mid, float treble);
    // Individual per-band damping setters — exposed for APVTS plumbing so
    // each band's damping has its own parameter slot. Update the local
    // state then forward to the corresponding sub-tank.
    void setBassDamping       (float coeff);
    void setMidDamping        (float coeff);
    void setTrebleDamping     (float coeff);
    // Per-band output gain. Each SubTank's wet output is multiplied by its
    // band gain before the 3-band sum. Phase 6 iteration 1 exposed that the
    // 8-channel Hadamard mixing concentrates midrange modal density,
    // producing box_ratio_db +13 dB vs the Lex Med Hall anchor. Mid-band
    // attenuation (default 0.40 ≈ −8 dB) is the structural lever that
    // closes that overshoot without touching tank decay or damping. Per-
    // band gain is also a clean way to trim bass_ratio (drop bass output
    // a little while leaving its RT60 intact). Linear scalars; range
    // unbounded but typical [0.0, 1.5].
    void setBandGain          (float bass, float mid, float treble);
    // Individual per-band gain setters — APVTS plumbing companion to the
    // per-band damping setters above. Linear scalars; typical range [0, 2].
    void setBassGain          (float gain);
    void setMidGain           (float gain);
    void setTrebleGain        (float gain);
    // Inline diffusion + stereo width also need APVTS exposure for the
    // Hall-engine advanced section. setStereoWidth already declared above;
    // setInlineDiffusion forwards uniformly to all 3 sub-tanks (per-band
    // override available via setBandInlineDiffusion if tuning needs it).
    void setInlineDiffusion   (float coeff);
    // No-op kept for API parity with FDNReverb's TankDiffusion knob.
    // The 3-band parallel topology gets its density from Hadamard mixing
    // inside each SubTank — no inline-AP diffusion stage to tune.
    void setTankDiffusion     (float amount);
    // Post-tank linear M/S widener coefficient `b`. Applied as a
    // time-invariant 2×2 matrix on the band-summed wet signal:
    //     outL = wetL − b · wetR
    //     outR = wetR − b · wetL
    // b > 0  → decorrelates (widens). b = 0 → bypass. b < 0 → narrows.
    // Linear matrix on a stably-correlated pair stays stably correlated, so
    // stereo_corr_stability is preserved — drops stereo_correlation cleanly
    // without disturbing the per-band modulation stability. Defaults to a
    // mild widening (matches FoilPlateEngine's kOutMixB = 0.05 baseline).
    void setStereoWidth       (float b);

private:
    // Multi-tap input injection (Phase 3 — FoilPlate Pillar 1 shape applied
    // to hall scale). A single predelay ring buffer feeds N taps at staggered
    // delays before the LR4 band split. The summed (and unity-DC-normalized)
    // tap output replaces the raw dry input as what reaches the SubTanks.
    //
    // Why: shaping ER density at the input gives clarity (C80, D50) and
    // onset (EDT) control independent of tank decay. Wet-output envelope
    // shaping (the alternative) breaks per-band RT60 measurements because
    // peak-relative T20 fit windows shift with amplitude — multi-tap
    // injection is the only Pillar-1-style EDT/C80/D50 lever that leaves
    // rt60_per_band measurements undisturbed.
    //
    // Tap times anchored to Lex Hall "Med Hall" early-reflection structure
    // (L_L_Rfl_Dly 22ms / L_R_Rfl_Dly 41ms / R_R_Rfl_Dly 26ms / R_L_Rfl_Dly
    // 39ms per the saved fxp's XML). Three primary reflections in 22-55 ms,
    // three sustainers in 90-200 ms keep the late-tail density up so the
    // 1-3 s window doesn't die 4-10 dB faster than the Lex anchor (the
    // failure we measured on the FDN Vocal Hall in the diagnosis pass).
    static constexpr int   kNumPredelayTaps = 6;
    static constexpr float kTapTimesMs[kNumPredelayTaps] =
        { 22.0f, 35.0f, 55.0f, 90.0f, 140.0f, 200.0f };
    // Phase 6 P3 retune: heavier on early taps, almost off on late taps,
    // to push c80/d50 positive. Inline AP diffusion (Sprint 1.5 P3) smears
    // energy later in time as a side-effect of modal smoothing; the
    // multi-tap injection has to compensate by front-loading more of the
    // dry signal into the 0-50 ms window so the LATE-half-vs-EARLY-half
    // energy ratio matches Lex (Lex Med Hall c80 = +3.6, d50 = +0.9 —
    // distinctly early-loaded). Sum still hits unity DC via tapNorm.
    static constexpr float kTapWeights[kNumPredelayTaps] =
        {  1.50f, 0.80f, 0.30f, 0.10f, 0.05f, 0.02f };

    // Pre-tank input band split — feeds each SubTank its assigned band of
    // dry input.
    duskverb::dsp::LR4BandSplit splitL_, splitR_;
    duskverb::dsp::HallSubTank  bassTank_, midTank_, trebleTank_;
    // Post-tank band isolation. Each SubTank's 8-channel Hadamard mixing
    // scatters input frequencies across all channels, so the wet output of
    // (e.g.) the BASS sub-tank includes 250-500 Hz harmonic content that
    // bleeds into the broadband sum. Without post-filtering, that upper-
    // bass harmonic packing was driving box_ratio_db +13 dB above the Lex
    // Med Hall anchor — and per-band gain or damping alone could only
    // close 2 dB of that gap (verified Phase 6 iteration 2 spectral
    // diagnosis). Post-filtering each band's output back into its LR4
    // passband makes per-band gain genuinely correspond to per-band
    // amplitude, and keeps the broadband sum from re-introducing the
    // overlap that the upstream split removed.
    duskverb::dsp::LR4BandSplit bassPostL_,   bassPostR_;
    duskverb::dsp::LR4BandSplit midPostL_,    midPostR_;
    duskverb::dsp::LR4BandSplit treblePostL_, treblePostR_;

    // Predelay ring buffer (sized at prepare() to fit the longest tap +
    // headroom, rounded up to next power of 2 for mask-and addressing).
    std::vector<float> predelayL_, predelayR_;
    int                predelayWritePos_ = 0;
    int                predelayMask_     = 0;
    int                tapSamples_[kNumPredelayTaps] {};
    float              tapNorm_          = 1.0f;   // = 1 / Σ kTapWeights

    // Per-block scratch buffers — sized at prepare() to maxBlockSize.
    // Inputs/outputs of each SubTank live here; the process() loop
    // chunks the incoming buffer to stay within these sizes (JUCE
    // allows blocks larger than the prepareToPlay maxBlockSize hint).
    std::vector<float> bassInL_,   bassInR_;
    std::vector<float> midInL_,    midInR_;
    std::vector<float> trebleInL_, trebleInR_;
    std::vector<float> bassOutL_,  bassOutR_;
    std::vector<float> midOutL_,   midOutR_;
    std::vector<float> trebleOutL_, trebleOutR_;

    double sampleRate_         = 44100.0;
    int    scratchBlockSize_   = 0;
    bool   prepared_           = false;

    float  decayTime_          = 1.5f;
    float  bassMultiply_       = 1.0f;
    float  midMultiply_        = 1.0f;
    float  trebleMultiply_     = 1.0f;
    float  crossoverFreq_      = 500.0f;
    float  highCrossoverFreq_  = 4000.0f;
    float  saturationAmount_   = 0.0f;
    // Phase 6 + Sprint 1.5 calibration defaults:
    //   dampingBass_  = 0.20 — gentle bass HF roll, lets bass band ring
    //     naturally for the long low-end decay halls characteristically
    //     hold (Lex BassRT 1.5x is preset-controlled, not engine-set).
    //   dampingMid_   = 0.35 — strongest damping in mids where late-tail
    //     energy overshoot was largest in the uniform-damping iteration.
    //   dampingTreble_ = 0.05 — minimal in-tank HF damping; trust the
    //     post-tank HiCut filter for HF shaping. The uniform damping=0.3
    //     of iteration 1 killed treble RT60 by 0.34→0.73s vs the Lex
    //     anchor; pulling treble damping near zero restores HF decay
    //     to natural-FDN rates while bass/mid damping still controls
    //     late-tail energy.
    //   stereoWidth_ = 0.0 — tap-sign decorrelation inside HallSubTank
    //     pulls broadband stereo_correlation toward -0.25 on bare output;
    //     widening must be applied per-preset, not as engine default.
    // Phase 6 iteration 2 spectral diagnosis: the +13 dB box_ratio_db
    // overshoot vs the Lex Med Hall anchor was driven by the BASS sub-
    // tank's harmonic packing at 250–500 Hz (long bass delays → dense
    // modal comb between fundamentals and the LR4 fLow crossover),
    // NOT the mid sub-tank. Cutting mid_gain in iteration 1 made things
    // worse because mid output was already 3-7 dB UNDER Lex in the 1-4 kHz
    // octaves. The fix is heavier bass damping (kills the upper-bass
    // harmonics riding the bass tank's recirculation) with mid/treble
    // gain restored to unity.
    float  dampingBass_        = 0.40f;     // restored — damping doesn't reach the 250-500 Hz box-ratio zone
    float  dampingMid_         = 0.25f;
    float  dampingTreble_      = 0.05f;
    float  stereoWidth_        = 0.0f;
    // Per-band gain defaults at unity — preset-tunable. Phase 6 spectral
    // diagnosis showed that bass tank gain alone can't close box_ratio_db
    // (the offending 380 Hz modal peak is 10 dB stronger than Lex's anchor;
    // cutting bass_gain proportionally drops the flanks too, ratio holds).
    // The structural fix is modal smoothing via inline allpass diffusion
    // inside HallSubTank — planned Sprint 1.5 P3.
    float  gainBass_           = 1.0f;
    float  gainMid_            = 1.0f;
    float  gainTreble_         = 1.0f;

    void updateSubTankDecays();
    void updateCrossovers();
    void recomputePredelayTaps();
};
