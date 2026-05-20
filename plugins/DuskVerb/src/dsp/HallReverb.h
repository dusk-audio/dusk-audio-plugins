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
    // Multi-tap setters — index 0..5 for the 6 input-injection taps.
    // Out-of-range index is a silent no-op. Time changes recompute
    // tapSamples_ and tapNorm_; weight changes recompute tapNorm_ only.
    // Both produce an instantaneous read-position / amplitude change —
    // for clean automation the host should ramp these slowly OR the
    // editor should attach them through a smoothed parameter widget.
    void setTapTimeMs         (int index, float ms);
    void setTapWeight         (int index, float weight);
    // Direct specular output taps (P8b). 4 taps read from the same
    // predelay ring as the multi-tap injection but bypass the LR4 split
    // + sub-tank Hadamard matrix entirely — they mix straight into the
    // output between the M/S widener and the soft-clip stage. Lets
    // discrete 3-20 ms early reflections survive through to the IR
    // measurement window (sub-tank diffusion swallows them otherwise).
    // setSpecularHFCutHz controls the 1-pole LP guardrail that prevents
    // raw dry HF transients from inflating treble_ratio / spec_crest.
    void setSpecularTimeMs    (int index, float ms);
    void setSpecularWeight    (int index, float weight);
    void setSpecularHFCutHz   (float hz);
    // Wet-only Hi Cut (P8c). HallReverb owns its own LP on the band-summed
    // wet path so DuskVerbEngine can bypass its global Hi Cut filter for
    // the Hall engine — the specular path stays outside this filter and
    // gets only its dedicated 2-pole LP at hall_spec_hf_cut.
    void setWetHiCutHz        (float hz);
    // P10 — per-band post-tank peaking EQ. Fixed centre frequencies at
    // the mid-octave of each LR4 band (bass 250 Hz, mid 1500 Hz, treble
    // 8000 Hz). 6 APVTS-tunable axes total (3 bands × {gain, Q}) keep
    // optimiser dimensionality low while giving direct control over the
    // metrics that the existing per-band gain / damping can't reach:
    //   • box_ratio_db   ← mid-band cut centred on the 380 Hz hump
    //                      (bass-band peaking EQ targets the 250-500 Hz
    //                      slope; mid-band peaking EQ shapes 1-2 kHz)
    //   • centroid_drift ← per-band gain shapes the time-evolving
    //                      spectrum since each EQ only fires within its
    //                      own LR4 passband
    void setBassEQ            (float gainDb, float q);
    void setMidEQ             (float gainDb, float q);
    void setTrebleEQ          (float gainDb, float q);
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
    // Default tap pattern — early-heavy, dialed in during Phase 6 P3+P4
    // hand-iteration to push c80/d50 toward Lex Med Hall's +3.6 / +0.9
    // (early-loaded) signature. Each tap's time + weight is now exposed
    // via APVTS (hall_tap_N_ms / hall_tap_N_w) so the optimizer can
    // search this 12-axis space directly — peak_locations_ms is a
    // function of the delays, c80/d50 are functions of both delays and
    // weights, so leaving them as compile-time constants closed the
    // tuner's escape routes for those metrics.
    static constexpr float kDefaultTapTimesMs[kNumPredelayTaps] =
        { 22.0f, 35.0f, 55.0f, 90.0f, 140.0f, 200.0f };
    static constexpr float kDefaultTapWeights[kNumPredelayTaps] =
        {  1.50f, 0.80f, 0.30f, 0.10f, 0.05f, 0.02f };

    // P8b — direct specular output taps. 4 short delays read straight from
    // the predelay ring, no LR4 / sub-tank routing. Defaults targeted at
    // Lex Med Hall's measured 3 / 7 / 9 / sub-15 ms early-peak signature.
    // L/R signs alternate per tap so each tap contributes decorrelated
    // content to both channels (without needing per-tap L/R cross-feed
    // routing knobs — keeps the APVTS surface to 4 + 4 = 8 params + 1
    // shared HF cut).
    static constexpr int   kNumSpecularTaps = 4;
    static constexpr float kDefaultSpecularTimesMs[kNumSpecularTaps] =
        { 3.0f, 6.0f, 11.0f, 17.0f };
    static constexpr float kDefaultSpecularWeights[kNumSpecularTaps] =
        { 0.80f, 0.60f, 0.40f, 0.20f };
    static constexpr float kDefaultSpecularHFCutHz = 6000.0f;
    // Per-tap L/R routing — asymmetric so the mono sum (which the
    // peak_locations_ms metric operates on) sees each tap. Even taps go
    // L-only, odd taps go R-only — alternating channels decorrelates L
    // from R while each tap remains visible in the mono mix. Earlier
    // mirrored signs ({+1,-1,+1,-1} / {-1,+1,-1,+1}) produced perfectly
    // anti-correlated L/R → mono sum cancelled to zero → specular
    // invisible in IR measurements.
    static constexpr float kSpecularSignL[kNumSpecularTaps] = { +1.0f, 0.0f, +1.0f, 0.0f };
    static constexpr float kSpecularSignR[kNumSpecularTaps] = {  0.0f, +1.0f, 0.0f, +1.0f };

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

    // Predelay ring buffer (sized at prepare() to fit the longest possible
    // tap time + headroom, rounded up to next power of 2 for mask-and
    // addressing). The buffer is sized for the APVTS maximum tap time
    // (250 ms) so per-tap delay changes never force a reallocation on the
    // audio thread.
    std::vector<float> predelayL_, predelayR_;
    int                predelayWritePos_ = 0;
    int                predelayMask_     = 0;

    // Mutable per-tap state — replaces the old static constexpr arrays so
    // each tap is APVTS-tunable. tapSamples_ is recomputed in
    // recomputePredelayTaps() whenever a tap time changes; tapNorm_ is
    // 1 / Σ tapWeights_ for unity DC gain.
    float              tapTimesMs_   [kNumPredelayTaps] {};
    float              tapWeights_   [kNumPredelayTaps] {};
    int                tapSamples_   [kNumPredelayTaps] {};
    float              tapNorm_      = 1.0f;

    // Specular path state. Reuses predelayL_/R_ for the buffer reads;
    // owns only the per-tap delays + weights + the 1-pole LP guardrail
    // state (one filter per output channel, shared coefficient).
    float              specularTimesMs_[kNumSpecularTaps] {};
    float              specularWeights_[kNumSpecularTaps] {};
    int                specularTapSamples_[kNumSpecularTaps] {};
    float              specularHFCutHz_  = kDefaultSpecularHFCutHz;
    // Specular HF guardrail. Two cascaded 1-pole LPs per channel — 12 dB/oct
    // slope with a strictly monotonic impulse response (no rebound). RBJ
    // Butterworth (Q=0.707) was first tried but its impulse response has
    // small negative lobes at samples 5-7 which the peak_locations_ms
    // detector reads as additional peaks; cascaded 1-poles eliminate the
    // oscillation while keeping the steeper-than-1-pole rolloff that
    // preserves the early-reflection bite while damping HF "metallic"
    // transients.
    float              specularLPAlpha_   = 0.5f;     // shared per-stage coeff
    float              specularLP1StateL_ = 0.0f, specularLP1StateR_ = 0.0f;
    float              specularLP2StateL_ = 0.0f, specularLP2StateR_ = 0.0f;
    // Wet-path Hi Cut biquad (P8c). Replaces DuskVerbEngine's global Hi Cut
    // for the Hall engine — applied to the band-summed wet signal before
    // the specular taps mix in, so specular content stays unfiltered by
    // this LP.
    duskverb::dsp::LR4BandSplit::Biquad wetHiCutBiquadL_, wetHiCutBiquadR_;
    float              wetHiCutHz_      = 20000.0f;     // bypass-effective until set

    // P10 per-band peaking EQ. Fixed centre frequencies; gain + Q
    // tunable per band. Default gain 0 dB → bypass (peaking biquad with
    // 0 dB gain is unity passthrough at any Q).
    static constexpr float kBassEQFc   = 250.0f;
    static constexpr float kMidEQFc    = 1500.0f;
    static constexpr float kTrebleEQFc = 8000.0f;
    duskverb::dsp::LR4BandSplit::Biquad bassEQL_,   bassEQR_;
    duskverb::dsp::LR4BandSplit::Biquad midEQL_,    midEQR_;
    duskverb::dsp::LR4BandSplit::Biquad trebleEQL_, trebleEQR_;
    float              bassEQGainDb_   = 0.0f, bassEQQ_   = 0.707f;
    float              midEQGainDb_    = 0.0f, midEQQ_    = 0.707f;
    float              trebleEQGainDb_ = 0.0f, trebleEQQ_ = 0.707f;

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
    // Per-sample specular reads — captured in the injection loop (when
    // predelayWritePos_ still tracks the current input sample), consumed
    // in the band-sum/output loop after one-pole LP filtering.
    std::vector<float> specularInL_, specularInR_;

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
    void recomputeSpecularTaps();
    void recomputeSpecularLP();
};
