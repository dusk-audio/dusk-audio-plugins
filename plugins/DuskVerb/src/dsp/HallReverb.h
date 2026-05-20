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
    // Damping (HF roll-off in tank feedback). Applied uniformly across
    // bands for now; per-band damping override lands in a later phase if
    // tuning calls for it.
    void setDamping           (float amount);
    // No-op kept for API parity with FDNReverb's TankDiffusion knob.
    // The 3-band parallel topology gets its density from Hadamard mixing
    // inside each SubTank — no inline-AP diffusion stage to tune.
    void setTankDiffusion     (float amount);

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
    static constexpr float kTapWeights[kNumPredelayTaps] =
        {  1.00f, 0.65f, 0.50f, 0.40f, 0.30f, 0.20f };

    duskverb::dsp::LR4BandSplit splitL_, splitR_;
    duskverb::dsp::HallSubTank  bassTank_, midTank_, trebleTank_;

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
    float  dampingAmount_      = 0.0f;

    void updateSubTankDecays();
    void updateCrossovers();
    void recomputePredelayTaps();
};
