#pragma once

#include "AlgorithmConfig.h"
#include "DattorroTank.h"
#include "DattorroPlateVintage.h"
#include "DiffusionStage.h"
#include "EarlyReflections.h"
#include "FDNReverb.h"
#include "MultibandFDN.h"
#include "SixAPTankEngine.h"
#include "NonLinearEngine.h"
#include "QuadTank.h"
#include "ShimmerEngine.h"
#include "SpringEngine.h"
#include "VintageTankEngine.h"
#include "ReverseRoomEngine.h"
#include "SparseEarlyField.h"
#include "OutputDiffusion.h"

#include <algorithm>
#include <cmath>
#include <vector>

// One-pole exponential smoother for per-sample parameter interpolation.
struct OnePoleSmoother
{
    float current = 0.0f;
    float target = 0.0f;
    float coeff = 0.0f;

    void reset (float value) { current = target = value; }

    void setSmoothingTime (double sampleRate, float timeMs)
    {
        coeff = std::exp (-1000.0f / (std::max (timeMs, 0.1f)
                                      * static_cast<float> (sampleRate)));
    }

    void setTarget (float t) { target = t; }

    float next()
    {
        current = target + coeff * (current - target);
        return current;
    }

    // Advance the smoother by `n` samples in O(1) using coeff^n. Used at the
    // top of each process block for parameters whose downstream consumers
    // (filter coefficients, tank delay lengths) are too expensive to
    // recompute per-sample.
    float skip (int n)
    {
        if (n <= 0) return current;
        float multiplier = std::pow (coeff, static_cast<float> (n));
        current = target + multiplier * (current - target);
        return current;
    }
};

// DuskVerb Shell engine.
//
// Routing (every algorithm):
//
//   in (L,R)
//     ↓ pre-delay
//     ├──► EarlyReflections ── (er_size, er_level)
//     │
//     └──► [DiffusionStage] (engines: Dattorro / QuadTank / FDN)
//           ↓
//          selected late tank (Dattorro / 6-AP / QuadTank / FDN / Spring / NonLinear / Shimmer)
//           ↓
//          erL+lateL, erR+lateR
//           ↓
//          Lo Cut → Hi Cut → Width → Mix (equal-power) → Gain Trim
//           ↓
//          out (L, R)
//
// All four sub-engines are owned (never allocated on the audio thread). The
// `algorithm` parameter selects which one consumes the post-predelay signal;
// the others sleep but stay prepared so a switch only takes a buffer-clear
// + setAlgorithm pointer flip.
class DuskVerbEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void process (float* left, float* right, int numSamples);

    // Discrete (non-smoothed) controls.
    void setAlgorithm (int index);
    void setFreeze (bool frozen);
    // Engine-specific: only the NonLinear engine has a gate. For other
    // engines this is a no-op (the call is forwarded but ignored).
    void setNonLinearGateEnabled (bool enabled);

    // Tank parameters — propagated to whichever engine is currently active.
    void setDecayTime     (float seconds);
    void setSize          (float size);
    void setBassMultiply  (float mult);
    void setMidMultiply   (float mult);            // 3-band mid multiplier
    void setTrebleMultiply(float mult);
    // FDN-specific: forwards into FDNReverb::setAirTrebleMultiply, which is
    // the field actually consumed in computeDecayCoefficients to produce the
    // gHigh feedback gain. Bug-fix 2026-05-30: the APVTS damping → engine
    // chain previously only hit setTrebleMultiply, whose trebleMultiply_
    // member is unread inside the loop. Push to this method alongside so
    // damping edits actually choke HF feedback in the FDN engine.
    void setAirTrebleMultiply (float mult);
    // FDN-only FiveBandDamping (Phase 2): sub + hi-mid decay plateaus + their
    // crossovers. Forward only to fdn_; other engines have no five-band path.
    void setSubMultiply (float mult);
    void setHiMidMultiply (float mult);
    void setSubCrossoverFreq (float hz);
    void setAirCrossoverFreq (float hz);
    // QuadTank 5-band split (separate sentinel convention from the FDN above).
    void setQuadHiMidMultiply (float mult);
    void setQuadAirMultiply (float mult);
    // Low-Band Transient Shaper (FDN only, Phase A).
    void setShaperDepth (float depth);
    void setShaperTimeMs (float ms);
    void setShaperXoverHz (float hz);
    void setShaperSens (float sens);
    // Block 2 feed-forward input makeup (FDN only).
    void setInputSubGainDb (float db);
    void setInputMidGainDb (float db);
    void setInputHighGainDb (float db);
    void setCrossoverFreq (float hz);              // bass↔mid (legacy "crossover")
    void setHighCrossoverFreq (float hz);          // mid↔high (3-band)
    void setSaturation    (float amount);          // 0..1 drive-style softClip
    void setModDepth      (float depth);
    void setModRate       (float hz);
    void setTailSpinDepth (float depth);   // post-loop output AM; FDN/ReverseRoom only
    void setTailSpinRate  (float hz);
    void setDiffusion     (float amount);

    // Early reflections.
    void setERLevel (float level);
    void setERSize  (float size);
    // Phase 4 (option 2): early-field ER boost. Multiplies the parallel-ER
    // contribution in the post-tank combine so it can run hotter than the
    // [0,1] er_level cap and own the 0-26 ms attack the FDN tank can't reach.
    // 1.0 = ×1.0 exact = bit-identical bypass. FDN-relevant (other engines that
    // use smooth-ER also honor it; DattorroVintage/ReverseRoom zero the ER anyway).
    void setEREarlyBoost (float boost);
    // Rising-onset ER envelope: tap gains peak at this many ms (gentle swell)
    // instead of at the first tap. 0 = legacy rolloff = bit-identical.
    void setEROnsetRiseMs (float ms);
    // Stereo-neutral early-field mode (Phase 2): independent R taps → uniform
    // ~0 L/R correlation (VVV-like), no anti-phase low. false → bit-identical.
    void setERStereoNeutral (bool enabled);
    // ER decorrelation allpass depth (0 = bypassed → bit-identical). Different
    // prime delays per channel → pushes L/R correlation toward 0 (VVV-like).
    void setERDecorr (float coeff);
    // Phase 4 (Change 2): output cross-talk shelving matrix. Decorrelates only
    // the HF air (>1.5 kHz) by cross-bleeding each channel's high band into the
    // other with a 180° inversion; LF untouched (mono-safe). Dedicated depth
    // (NOT coupled to Width) so 0 = no cross-feed = bit-identical for the fleet.
    void setOutputCrossTalk (float depth);

    // Shell parameters (smoothed in process()).
    void setPreDelay  (float milliseconds);
    void setLoCut     (float hz);
    void setHiCut     (float hz);
    // Post-tank high-shelf attenuation depth (dB, range [-24, 0], 0 = flat).
    // Replaces the prior brick-wall LP behavior of the Hi Cut filter — the
    // corner (set via setHiCut) is now a SHELF, content above the corner
    // is attenuated by this much instead of decapitated. Per-preset on
    // FactoryPreset. Default -12 dB.
    void setHiCutShelfGainDb (float dB);

    // Post-tank parametric EQ (4-band). Each band is freq + Q + gainDb.
    // Lives AFTER the Hi Cut Shelf and BEFORE the dry/wet mix matrix.
    // gainDb == 0 designs unity coefficients → bit-identical bypass.
    // Default state for all bands is gainDb=0, so presets that don't
    // call this leave the EQ stage transparent.
    void setPostTankEQBand (int index, float freqHz, float qFactor, float gainDb);

    // Tank-feed EQ (Progenitor-style 'inputdamp', 2026-06-11). First-order
    // low + high shelves applied to the TANK FEED (post-diffuser, pre-tank),
    // NOT the wet sum — the parallel ER branch taps the signal earlier and
    // stays bright. Feed-forward into the loop: the loop poles (per-band T60)
    // are untouched; only the recirculating field's spectrum is tilted. This
    // is the decoupled lever for the bright-attack -> dark-tail temporal
    // signature (anchor cent 2214 -> 1346 Hz over 50 -> 500 ms) that in-loop
    // damping (pole-coupled) and post-sum trims (time-uniform) both failed
    // to express. 0 dB gains -> branch skipped -> bit-identical.
    void setTankFeedEQ (float lowFc, float lowGainDb, float highFc, float highGainDb);

    // Dattorro density-AP wander depth (see DattorroTank::setDensityJitter).
    // The in-loop time-varying wander FM-scatters tail energy broadband each
    // pass — the source of the late-window HF plateau + pitch-chorus on dark
    // rooms. 0.02 (engine default) = bit-identical; forwarded to the Dattorro
    // tank only.
    void setDattorroDensityJitter (float fraction);

    // ER-bus spectral correction (2026-06-08, energy-arrival campaign). The
    // parallel ER field is a 500 Hz-2 kHz midrange bump (measured: −11.5 dB @
    // 250-500, −9 @ sub, −16 @ 8-16k relative to its 500-1k peak). Boosting it
    // to front-load energy therefore dumps a mid hump and adds no low/HF, so a
    // flat Gain-Trim compensation craters the low end. Two shelves on the ER
    // bus ONLY (low-shelf ~400 Hz, high-shelf ~3 kHz) flatten the ER to match
    // the tank's full spectrum so a boosted ER front-loads cleanly. Applied to
    // erOutL_/erOutR_ before the ER+tank sum; both gains 0 dB → unity → the ER
    // bus is bit-identical, so every non-opting preset is byte-for-byte
    // unchanged. Per-preset via kERBusEQByName.
    void setERBusShelves (float lowGainDb, float highGainDb);

    // Tank-output level scalar (2026-06-08, energy-arrival campaign). Multiplies
    // the late (FDN tank) contribution at the ER+tank sum, OUTSIDE the recursive
    // loop, so the decay RATE (RT60) is untouched — only the tank's LEVEL moves.
    // Lets a preset REBALANCE energy early→late: raise ER (front-load) while
    // lowering the tank, keeping total wet energy (and ss-band levels) constant.
    // Adding ER alone inflates steady-state; rebalancing does not. Default 1.0 →
    // exact ×1.0 → bit-identical. Per-preset via kTankLevelByName.
    void setTankOutputLevel (float level);

    // Tank-level crossover (Phase 3). tankOutLevel_ applied BROADBAND by default
    // (splitHz = 0 → current behavior, bit-identical). When splitHz > 0, the
    // tank's LOW band (below splitHz) stays at unity while only MID/HIGH is
    // scaled by tankOutLevel_ — so front-loading the washy mid/high bloom does
    // NOT sacrifice the tank's correlated low (body + VVV-matched low image).
    // One-pole split on the tank output, OUTSIDE the recursive loop → bit-safe.
    void setTankSplitHz (float hz);

    // Phase γ (2026-05-29): decoupled per-band linear gain trim that sits
    // AFTER PostTankEQ and BEFORE the dry/wet mix matrix. Independent of
    // the FDN loop damping — lets a preset sculpt EDT band-shape + late
    // bass boom without warping in-loop coefficients (which couple decay
    // AND transient response). Realized as a cascade of 3 high-shelves
    // (sum-flat at unity gain) over 4 fixed crossover regions:
    //   region 0 = Sub        ≤ fLow
    //   region 1 = Low-Mid    [fLow, fMid]
    //   region 2 = Mid-High   [fMid, fHi]
    //   region 3 = Air        ≥ fHi
    // All 4 region gains default 0 dB → bit-identical bypass.
    void setPostTankBandTrimGainDb (int region, float gainDb);
    void setPostTankBandTrimCrossovers (float fLow, float fMid, float fHi);

    // Phase δ (2026-05-29): per-band attack-ramp envelope shaping post-tank.
    // 4 regions (Sub / LowMid / MidHi / Air) split via 1-pole crossovers
    // (constant coefficients, sum-flat by construction). Each region has
    // an onset-triggered AttackRamp that JUMPS by attackDb at signal-rise
    // and exponentially returns to 0 dB over tauMs. Default attackDb = 0
    // → unity gain on every region → bit-identical bypass.
    void setPerBandEDTShape (int region, float attackDb, float tauMs);
    void setPerBandEDTCrossovers (float fLow, float fMid, float fHi);

    void setWidth     (float width);
    void setGainTrim  (float dB);
    void setMonoBelow (float hz);             // 20 = bypass; up = sums lows to mono
    void setMonoBelowDepth (float depth);     // 1.0 = full mono (legacy); <1 partial

    // DattorroVintage-specific: in-loop bass choke HPF cutoff. Other
    // engines ignore this call. Forwarded from APVTS so it shows up in
    // host / preset state like any other parameter.
    void setBassChokeHz (float hz);

    // Phase 2: route modulation topology to FDN + QuadTank.
    // RandomWalk = legacy independent per-line LFOs (default).
    // CoherentLoop = single master sine + per-line phase-paired offsets.
    void setModulationTopology (DspUtils::ModulationTopology t);

    // Phase α: per-line frequency-indexed decay scaling on the FDN engine.
    // No-op for non-FDN engines. Defaults 1.0 / 1.0 = backward identical.
    void setPerLineDecayTilt (float shortLineScale, float longLineScale);

    // AccurateHall (algo 10) only: per-octave T60 target (band 0..8 = 63 Hz..
    // 16 kHz). Routes to accurateHall_.setOctaveT60. No-op on every other
    // engine (the GEQ is compiled only into FDNReverbT<true>). seconds<=0 →
    // that octave inherits the broadband Decay Time.
    void setAccurateHallOctaveT60 (int band, float seconds);

    // SparseField (algo 11) only: the velvet-noise early-field generator dials
    // + the reduced-tail level. No-op routing on every other engine.
    void setSparseFieldSize       (float sizeScale);
    void setSparseFieldOnsetMs    (float ms);
    void setSparseFieldDecayMs    (float ms);
    void setSparseFieldBurst2Ms   (float ms);
    void setSparseFieldTailGain   (float gain);
    void setSparseERGain          (float gain);   // algo 13 composite: ER level in the mix

    // Per-preset post-tank output diffusion (Bright Hall metallic-ring fix).
    // enable=false → bypassed (bit-null on every other preset). amount maps to
    // the allpass coefficient, lfoScale tames the built-in wobble, delayScale
    // spreads the allpass delays for denser HF smearing.
    void setOutputDiffusion (bool enable, float amount, float lfoScale, float delayScale);

    // Phase β (2026-05-29): per-preset FDN base delays. Lets each preset
    // choose a 16-int delay-line set tuned to match a specific anchor's
    // per-band modal-beat pattern (Hilbert-FFT envelope peak frequency).
    // No-op for non-FDN engines. Pass nullptr to retain the engine default.
    void setFDNBaseDelays (const int* delays);
    // Restore the engine-default log-spaced-prime base-delay table. Used by the
    // preset-swap path so a preset with NO custom delay set does not inherit the
    // previous preset's custom delays (setFDNBaseDelays(nullptr) only PRESERVES,
    // it does not reset).
    void resetFDNBaseDelays();

    // TiledRoom (algo 13) COMPOSITE voicing — configures the sparse ER front-end
    // + the ER/tail mix (the 16-line AccurateHall tail is configured via the
    // preset octave-T60 map + decay/mod). Used by the DUSKVERB_TILEDROOM env
    // override (tuning sweep) and the per-preset bake. Params:
    // erSize, onsetMs, erDecayMs, burst2Ms, sparseTailGain, (spare).
    void setTiledRoomVoicing (float erSize, float onsetMs, float erDecayMs,
                              float burst2Ms, float sparseTailGain, float spare);

    // Reset the name-keyed engine config (PostTankEQ bands, modulation topology,
    // per-line decay tilt, FDN base delays) — the parts NOT carried by APVTS /
    // forcePushAllParametersTo — back to engine defaults. Called when a session
    // is loaded with no/unknown preset identity so a prior preset's config does
    // not leak onto the restored session.
    void reapplyNeutralEngineConfig();

    // Phase ε (2026-05-29): in-loop narrow-Q peaking band on the FDN per-
    // line feedback path. Designed to reinforce a specific frequency inside
    // the loop so steady-state input at that frequency builds extra gain
    // (e.g. closes sine1k cold by boosting the 1 kHz mode). gainDb = 0 →
    // unity coefficients → bit-identical bypass on the damping output. No-
    // op for non-FDN engines.
    void setFDNInLoopPeaking (float freqHz, float qFactor, float gainDb);
    void setFDNTimeVaryingHiDamp (float earlyMult, float lateMult, float crossoverHz,
                                  float releaseSec, float refLevel);
    // Parallel-multiband FDN opt-in. false (default) = single legacy tank =
    // bit-identical. true = 3 band-isolated tanks (decouples per-band T60).
    void setMultibandEnabled (bool enabled);
    // Per-band decay override (seconds) for the 3 multiband tanks — only when
    // multiband is enabled. Lets the optimizer set Low/Mid/High RT60 apart.
    void setMultibandDecays (float lowSec, float midSec, float highSec);

    // Phase η (2026-05-29): per-line dual-time-constant bass shelf. Both
    // gains 0 dB → bit-identical bypass. No-op for non-FDN engines.
    void setFDNDualBassShelf (float fastFc, float slowFc,
                               float fastGainDb, float slowGainDb,
                               float transitionMs);

    // Per-preset SixAPTank brightness/density tunables. Forwarded directly to
    // sixAPTank_ regardless of currentEngine_ — they're only audible when the
    // SixAPTank is the active engine, but pre-applying them at preset-load
    // time is harmless (and necessary so the values are in place before the
    // engine starts processing).
    void setSixAPDensityBaseline (float v);
    void setSixAPBloomCeiling    (float v);
    void setSixAPBloomStagger    (const float values[6]);
    void setSixAPEarlyMix        (float v);
    void setSixAPOutputTrim      (float v);

    // DattorroPlateVintage (algo 1) per-preset brightness controls. Forwarded
    // only to dattorroVintage_; audible only when algo=1 is active but
    // applied at preset-load time so the values are in place before swap.
    void setDpvHfShelfGainDb     (float v);
    void setDpvHfShelfFreqHz     (float v);
    void setDpvStructHfDampHz    (float v);

    // DattorroPlateVintage corrective EQ controls. boxCut is post-tank notch
    // (configurable depth + corner); bassShelf is pre-tank low-shelf (depth +
    // corner). Used by dark plates to compensate the 200-500 Hz scoop the
    // boxCut creates without disabling the corrective filter entirely.
    void setDpvBoxCutGainDb      (float v);
    void setDpvBoxCutFreqHz      (float v);
    void setDpvBassShelfGainDb   (float v);
    void setDpvBassShelfFreqHz   (float v);

    // Reset all delay buffers, biquad state, pre-delay, and mono-maker LP state
    // to silence. Used by the processor to bring an idle engine to a clean
    // start before a preset crossfade swaps it in.
    void clearAllBuffers();

    // Snap every shell smoother (mix, width, erLevel, gainTrim, size, loCut,
    // hiCut, monoBelow) to its current target so the next process() call uses
    // the target values immediately instead of gliding from a stale state.
    // Call after force-pushing parameters to a freshly-cleared engine.
    void snapSmoothersToTargets();

    // Pre-fill THIS engine's pre-tank state (pre-delay buffer + early
    // reflections signal state) from `other`. Used at preset-swap time so
    // the new engine has real input history at sample 0 — without this,
    // ER taps fire from silence over their 8-80 ms delay range, producing
    // audible discrete onsets that the equal-power crossfade can't mask.
    // Late-tank state stays cleared (it's algorithm-specific; pre-filling
    // would feed wrong-coefficient history into a different topology).
    void copyInputHistoryFrom (const DuskVerbEngine& other);

private:
    // Engines (all owned; only one runs at a time).
    DattorroTank       dattorro_;
    SixAPTankEngine    sixAPTank_;
    QuadTank           quad_;
    FDNReverb          fdn_;
    // Parallel-multiband FDN (3 band-isolated tanks). Opt-in per preset; when
    // off, the single fdn_ above runs untouched → bit-identical for the fleet.
    // Decouples per-band T60 from steady-state level (see MultibandFDN.h).
    MultibandFDN       multibandFdn_;
    bool               multibandActive_ = false;
    SpringEngine       spring_;
    NonLinearEngine    nonLinear_;
    ShimmerEngine      shimmer_;
    DattorroPlateVintage dattorroVintage_;  // re-pointed 2026-05-13: algo 7 slot now hosts DattorroPlateVintage (vintage-hardware post-EQ on Dattorro tank). Variable name retained so call sites stay stable.
    DspUtils::VintageTankEngine vintageTank_;  // algo 8 (2026-05-29): Griesinger/Lexicon figure-8 modulated AP loop. Built from first principles, replaces the FDN's unitary Hadamard scatter with a recirculating tank that builds modal density over time.
    ReverseRoomEngine  reverseRoom_;     // algo 9 (2026-05-31): causal rising-ER onset + dark FDN tail; replicates Lexicon PCM Room "Reverse 1".
    FDNReverbT<true>   accurateHall_;    // algo 10 (2026-06-09): FDN + per-octave GEQ in the feedback loop (Jot/Schlecht accurate-RT). P2: templated FDNReverbT<true>; GEQ scaffold inert (flat) → still renders identical to FDN. P3 fills the per-octave GEQ.
    SparseEarlyField   sparseField_;     // algo 11 (2026-06-10): velvet-noise front-loaded sparse early field. Summed with a reduced accurateHall_ tail in the SparseField process() case.
    FDNReverbT<true, 32> accurateHall32_; // algo 12 (2026-06-10): 32-line AccurateHall (double lines + order-32 Hadamard) for HF modal density — Bright Hall metallic-ring fix. Gets every accurateHall_ setter forwarded alongside.
    // algo 13 (TiledRoom) is a COMPOSITE in the process() switch: sparseField_ ER
    // + accurateHall_ 16-line tail (shared with SparseField). No dedicated member
    // — the standalone 4-line TiledRoomEngine was a kill-test (flutter+spectral),
    // superseded by this composite. setTiledRoomVoicing() configures sparseField_.

    // Pre-tank input diffuser, applied to every engine. Smears transients
    // before they hit the tank so onsets bloom into the tail rather than
    // arriving as discrete clicks.
    DiffusionStage diffuser_;

    // Tank-feed EQ state (Progenitor inputdamp). One-pole LP cores; shelves
    // realized as y = x + (g-1)*LP(x) (low) and y = x + (g-1)*(x - LP(x))
    // (high) — exact unity bypass at g=1. Stereo state per shelf.
    bool  tankFeedActive_   = false;
    float tankFeedLowFc_    = 200.0f,  tankFeedLowGain_  = 1.0f;   // linear g
    float tankFeedHighFc_   = 2500.0f, tankFeedHighGain_ = 1.0f;
    float tankFeedLowCoeff_ = 0.0f,    tankFeedHighCoeff_ = 0.0f;  // LP coeffs
    float tfLowStateL_ = 0.0f, tfLowStateR_ = 0.0f;
    float tfHighStateL_ = 0.0f, tfHighStateR_ = 0.0f;
    EarlyReflections er_;

    // Post-tank OUTPUT diffuser — per-preset (Bright Hall). Smears the FDN's
    // sparse HF tail modes into a dense wash (kills the metallic ring). OFF by
    // default: when outDiffActive_ is false the process() call is skipped
    // entirely → every other preset is bit-null. Set via setOutputDiffusion().
    OutputDiffusion outputDiffusion_;
    bool outDiffActive_ = false;

    EngineType currentEngine_ = EngineType::Dattorro;
    int currentAlgorithm_ = 0;

    // Pre-delay ring buffer.
    std::vector<float> preDelayBufL_;
    std::vector<float> preDelayBufR_;
    int preDelayWritePos_ = 0;
    int preDelayMask_ = 0;
    int preDelaySamples_ = 0;

    // Scratch buffers (sized by prepare()).
    std::vector<float> tankInL_, tankInR_;
    std::vector<float> tankOutL_, tankOutR_;
    std::vector<float> erOutL_, erOutR_;
    std::vector<float> sparseOutL_, sparseOutR_;   // algo 11: sparse early-field scratch

    // algo 11 SparseField: level of the reduced accurateHall_ tail under the
    // sparse early field (1.0 = full tail). Per-preset via kSparseFieldByName.
    float sparseTailGain_ = 0.45f;
    // algo 13 composite: ER front-end level in the mix (1.0 = full ER). Lets a
    // less-front-loaded room (Medium Drum Room, anchor first50 44.8%) back off the
    // ER so it doesn't overshoot. Default 1.0 = Tiled Room behaviour (bit-exact).
    float sparseERGain_ = 1.0f;

    // Per-sample smoothed shell parameters (consumed inside the per-sample loop).
    // mixSmoother lives on the processor (so the dry signal stays correlated
    // across the preset crossfade); engine outputs wet-only.
    OnePoleSmoother widthSmoother_;
    float erEarlyBoost_ = 1.0f;   // Phase 4 (option 2): ER early-field boost (1.0 = bypass)
    // Phase 4 (Change 2): output HF cross-talk decorrelation. depth 0 = bypass.
    float xtalkDepth_   = 0.0f;
    bool  xtalkActive_  = false;
    float xtalkHpCoeff_ = 0.0f;   // 1st-order LP coeff for the 1.5 kHz HF split
    float xtalkLpL_     = 0.0f;
    float xtalkLpR_     = 0.0f;
    OnePoleSmoother erLevelSmoother_;
    OnePoleSmoother gainTrimSmoother_;

    // Per-BLOCK smoothed parameters (advanced once per process() call). These
    // drive expensive recomputes (filter biquad coeffs / tank delay lengths)
    // that would cost too much per-sample, so we cap their evolution at the
    // block boundary instead.
    OnePoleSmoother sizeSmoother_;
    OnePoleSmoother loCutSmoother_;
    OnePoleSmoother hiCutSmoother_;
    OnePoleSmoother monoBelowSmoother_;
    float lastAppliedSize_      = -1.0f;
    float lastAppliedLoCut_     = -1.0f;
    float lastAppliedHiCut_     = -1.0f;
    float lastAppliedMonoHz_    = -1.0f;

    // Mono Maker — 1st-order matched-phase complementary split. Below the
    // cutoff, L+R are summed to mono; above stays stereo. Magnitude-flat
    // because we use input − lowpass for the high band (only 1st-order
    // satisfies perfect reconstruction).
    bool  monoMakerEnabled_ = false;
    float monoLPCoeff_      = 0.0f;
    float monoBelowDepth_   = 1.0f;   // 1.0 = full mono (legacy); <1 partial
    float monoLPStateL_     = 0.0f;
    float monoLPStateR_     = 0.0f;

    double sampleRate_ = 44100.0;
    int    maxBlockSize_ = 0;

    bool   frozen_ = false;

    void pushSizeToTanks (float size);  // helper — internal use only

    // Output IIR filters (Butterworth biquad cookbook).
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0;
        float a1 = 0, a2 = 0;
        float z1L = 0, z2L = 0;
        float z1R = 0, z2R = 0;

        float processL (float x)
        {
            float y = b0 * x + z1L;
            z1L = b1 * x - a1 * y + z2L;
            z2L = b2 * x - a2 * y;
            return y;
        }
        float processR (float x)
        {
            float y = b0 * x + z1R;
            z1R = b1 * x - a1 * y + z2R;
            z2R = b2 * x - a2 * y;
            return y;
        }
        void reset() { z1L = z2L = z1R = z2R = 0.0f; }
    };

    Biquad loCutFilter_;
    Biquad hiCutFilter_;

    // Post-tank parametric EQ (4 bands, series). Sits between Hi Cut Shelf
    // and the mono-below + Width + Gain Trim output chain. Default state is
    // all bands at 0 dB gain → unity coefficients → bit-identical bypass
    // for any preset that doesn't opt in.
    DspUtils::PostTankEQ postTankEQ_;

    // ER-bus spectral-correction shelves (energy-arrival campaign). Applied to
    // the parallel ER output ONLY, before the ER+tank sum. Both gains default
    // 0 dB → unity coefficients → ER bus bit-identical → whole fleet unchanged.
    DspUtils::LowShelfBand  erBusLowShelf_;
    DspUtils::HighShelfBand erBusHighShelf_;

    // Tank-output level scalar (energy-arrival rebalance). 1.0 → bit-identical.
    float tankOutLevel_ = 1.0f;
    // Tank-level crossover (Phase 3). 0 → broadband (bit-identical). >0 → one-
    // pole split: low band unity, mid/high × tankOutLevel_. State per channel.
    float tankSplitHz_   = 0.0f;
    float tankSplitCoeff_ = 0.0f;
    float tankSplitLpL_  = 0.0f;
    float tankSplitLpR_  = 0.0f;

    // Phase γ (2026-05-29): decoupled per-band linear gain trim. Cascade of
    // 3 high-shelves spanning 4 regions (Sub/LowMid/MidHi/Air). Sits AFTER
    // postTankEQ_ and BEFORE the mono-below + Width + Gain Trim chain. All
    // 4 region gains default 0 dB → unity coefficients → bit-identical
    // bypass on legacy presets.
    DspUtils::PostTankBandTrim postTankBandTrim_;

    // Phase δ (2026-05-29): per-band attack-ramp envelope shaper. 4 regions
    // split via 1-pole low-pass cascade (constant coefficients, sum-flat
    // by construction). Sits AFTER postTankBandTrim_, BEFORE the mono +
    // Width + Gain Trim chain. All 4 region attackDb default 0 → AttackRamp
    // gains stay at unity → bit-identical bypass.
    DspUtils::PerBandEDTShape perBandEDT_;
    // High-shelf attenuation depth feeding into updateHiCutCoeffs(). Stored
    // here (not just smoothed in real time) because it's per-preset, not
    // exposed in APVTS, and only changes when the Processor loads a preset.
    float hiCutShelfGainDb_ = -12.0f;

    void updateLoCutCoeffs (float hz);
    void updateHiCutCoeffs (float hz);
};
