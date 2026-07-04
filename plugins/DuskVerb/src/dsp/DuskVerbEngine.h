#pragma once

#include "AlgorithmConfig.h"
#include "DattorroTank.h"
#include "DattorroPlateVintage.h"
#include "DenseEarlyField.h"
#include "DiffusionStage.h"
#include "EarlyReflections.h"
#include "FDNReverb.h"
#include "MultibandFDN.h"
#include "SixAPTankEngine.h"
#include "NonLinearEngine.h"
#include "QuadTank.h"
#include "ShimmerEngine.h"
#include "SpringEngine.h"
#include "ReverseRoomEngine.h"
#include "SparseEarlyField.h"
#include "DiffusedEarlyReflections.h"
#include "OutputDiffusion.h"
#include "DenseHallReverb.h"
#include "BuildupDiffuser.h"
#include "ParallelMultibandTank.h"

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
    void setShimmerDownOctaveMix (float mix);   // Shimmer warm-low voice (octave down); 0 = bit-null
    void setShimmerSubOctaveMix  (float mix);   // Shimmer deep-low voice (2 octaves down → 250 Hz); 0 = bit-null
    void setShimmerFeedbackHpfHz (float hz);    // Shimmer feedback-loop HPF corner (low-cascade survival); default 60
    void setShimmerStereoMod     (float rateHz, float depth); // Shimmer wet stereo chorus/ensemble; depth 0 = bit-null
    void setShimmerHFAir         (float mix);   // Shimmer post-loop +12st air voice (genuine >12k air); mix 0 = bit-null
    void setShimmerUseDenseReverb (bool on);    // Shimmer tank: DenseHall (smooth HF) vs legacy sparse FDN (metallic); false = bit-identical
    void setShimmerUseTailSpin    (bool on);    // Shimmer FDN output spin-comb (smears metallic, keeps FDN cascade/width/HF); false = untouched
    void setShimmerUpVoiceScale   (float v1, float v2);  // Shimmer +12/+24 up-voice scale (mid-tail fill); 1.0/1.0 = bit-identical
    void setShimmerOctaveCascade  (const float gains[4]); // Shimmer dry-fed even down-cascade (500/250/125/62); all 0 = bit-null
    void setShimmerTailNoise      (float gain);  // Shimmer envelope-tracked tail noise floor (dense noise-like fade); 0 = bit-null
    void setShimmerHFSustainDb    (float db, float cornerHz = 4000.0f);   // Shimmer feedback-loop HF compensation shelf (lift above cornerHz per pass, extends HF T60); 0 dB = bit-null
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
    // Per-band stereo Width tilt — independent width for the LOW (<300 Hz), MID
    // (300 Hz-5 kHz) and HIGH (>5 kHz) bands of the SIDE signal (crossovers match
    // the width_low/mid/hi gates). The global Width scalar is frequency-flat, so
    // matching one band's L/R correlation to the anchor mis-sets the others
    // (measured: a flat Width raise traded bands net-negative across the fleet).
    // 1.0/1.0/1.0 → bandWidthActive_ false → the original side*width path runs
    // (bit-identical for the fleet). >1 widens that band, <1 narrows it.
    void setWidthBands (float low, float mid, float hi);

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
    // Post-tank HF air-shelf (boost or cut). 0 dB → inactive (bit-null bypass).
    void setOutputAirShelf (float freqHz, float gainDb);
    // Post-tank LF low-shelf (deep-sub boost or cut). 0 dB → inactive (bit-null).
    void setOutputLowShelf (float freqHz, float gainDb);

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
    void setDattorroModeNotch (float hz, float cutDb, float q);   // in-loop mode notch (boing); cutDb 0 = off/bit-null
    // Plate density rework (algo 0 + algo 1). depth 0 / reduction 1.0 = legacy.
    void setDattorroDensity (float depth01);          // 0 legacy 3 APs -> >0 dense 6 APs
    void setDattorroModReduction (float reduction01); // 1.0 legacy mod -> <1.0 stiller tail
    void setDattorroDensityRoomFill (bool enable);    // #87: hall density bases on room lines (boing fix, algo 0)
    void setDattorroMainLineDetune (float l1, float l2, float r1, float r2); // #87: per-line detune
    void setDattorroInputDiffusion (float scale01);   // input-diffuser coeff scale (1.0=canonical)
    void setDattorroSoftOnsetMs (float ms);           // tank output soft-onset ramp (ms; 0=instant)
    void setDattorroOctaveT60 (int band, float seconds); // per-octave T60 GEQ (0..8 = 63..16k Hz)
    void setDattorroOctaveDecayRef (float seconds);   // Decay-knob reference for the octave curve
    void setDattorroTonalCorrDb (int band, float dB); // static output per-octave level trim (cut-only)
    void setDattorroBloomAttackMs (float ms);
    void setDattorroBloomExp (float e);         // input-onset slow-attack swell (0=off)

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

    // AccurateHall (algo 10/12): reference broadband decay at which the octave
    // T60 curve is realized 1:1 (= the preset's baked Decay). The live Decay
    // knob scales the curve by decayTime_/ref so the knob is never dead.
    void setAccurateHallOctaveDecayRef (float seconds);

    // DenseHall (algo 14) per-octave T60 GEQ — fork #2 (2026-06-16). Decoupled
    // 9-octave damping replacing the 3-band shelf that floored the DenseHall
    // presets (cent dark + T60-16k dies). band 0..8 = 63 Hz..16 kHz; seconds<=0
    // → that octave inherits the broadband Decay. No-op on every other engine.
    void setDenseHallOctaveT60 (int band, float seconds);
    void setDenseHallLowAccumLimiter (float threshDb, float maxCut, float splitHz);   // drive-following sub charge limiter (piano-gate defect); maxCut 0 = bit-null
    void setPmbBand (int b, float t60s, float level, float direct, float width);   // ParallelMultiband (algo 15) per-band config; no-op elsewhere
    void setDenseHallOctaveDecayRef (float seconds);
    void setDenseHallTonalCorrection (bool enabled);   // fork B: decouple T60 from level
    // FORK A: discrete early-reflection tap (the "duh-duh"). ms ~90-110, gain 0=off.
    void setReflectionTap (float ms, float gain, float lpFc = 11000.0f);  // lpFc: tap rolloff (11k=sharp tick, ~5-6k=fuller/softer)
    void setEarlyTapBank  (const float* timesMs, const float* gains, int count, float lpFc = 9000.0f);  // up to 8 discrete ER taps at anchor-measured times; count 0 = off/bit-null

    // Jot tonal correction (AccurateHall algo 10): flatten per-band steady-state
    // energy so decay and tone are decoupled. Default off = bit-null.
    void setTonalCorrection (bool enabled);

    // Phase 3 output match-EQ: shape the wet steady-state envelope toward the
    // anchor. corrLinear[0..8] = per-octave (63 Hz..16 kHz) output gains in
    // [1e-3,1] (cut-only; normalize the anchor/DV ratio to max=1 offline, then
    // re-gain-match). All ~1.0 → identity → bit-null. Engine-agnostic.
    void setOutputMatchEQ (const float* corrLinear9);

    // Phase A early-field: delay the late tail by `ms` relative to the ER so the
    // ER defines the early window. 0 = off = bit-null. DenseHall path (Phase A).
    void setTankOnsetMs (float ms);

    // SparseField (algo 11) only: the velvet-noise early-field generator dials
    // + the reduced-tail level. No-op routing on every other engine.
    void setSparseFieldSize       (float sizeScale);
    void setSparseFieldOnsetMs    (float ms);
    void setSparseFieldDecayMs    (float ms);
    void setSparseFieldBurst2Ms   (float ms);
    void setSparseFieldBurst2Gain (float g);       // GAIN bump at burst2Ms (the discrete late tap; 0 = bit-null)
    void setBuildupAmount         (float a);       // DenseHall tail buildup (0 = bypass/bit-null, 1 = full gradual build)
    void setBuildupTimeScale      (float s);       // buildup build-time = hall size (scales the cascade; 1.0 = ~84ms)
    void setBuildupPostTank       (bool b);        // true = diffuse tank OUTPUT (builds onset, tail/T60 intact); false = INPUT (Bright Hall)
    void setSparseFieldTailGain   (float gain);
    void setSparseERGain          (float gain);   // algo 13 composite: ER level in the mix
    // Diffused discrete early reflections (DenseHall clarity/un-masking comb).
    void setDiffuseER (const float* timesMs, const float* gains, int n, float diffusion, float busGain);

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

    // DattorroPlateVintage front-load early-reflection network (algo 1 only).
    // erGain 0 = bypassed (byte-identical). See DattorroPlateVintage::setFrontLoad.
    void setDpvFrontLoad         (float erGain, float predelayMs, float tapMs, float lpHz);
    // DPV post-main second reflection tap (the anchor's ~143 ms "duh-DUH").
    // gain 0 = off = byte-identical. Forwards to DattorroPlateVintage::setPostMainTap.
    void setDpvPostMainTap       (float ms, float gain, float lpHz);
    // DPV dense early-field (fills the post-onset 0.1-0.5s "shelf"). gain 0 = off.
    void setDpvDenseField        (float gain, float predelayMs, float t60Ms);
    // Dattorro (algo 0) dense early-field — the SAME compact Schroeder field as the
    // DPV one, but summed post-tank in this engine (DattorroTank.cpp stays untouched →
    // all algo-0 presets bit-null when off). Fills the thin tail of the short rooms
    // (Medium Drum Room: decay sub/low/mid -23..-30% vs anchor). gain 0 = off.
    void setDattorroDenseField   (float gain, float predelayMs, float t60Ms);

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
    FDNReverb          fdn_;             // algo 4 (Studio) — hidden, no preset; FiveBand/multiband params still forward here.
    MultibandFDN       multibandFdn_;    // opt-in multiband (mb_* params); no preset enables it.
    bool               multibandActive_ = false;
    SpringEngine       spring_;
    NonLinearEngine    nonLinear_;
    ShimmerEngine      shimmer_;
    DattorroPlateVintage dattorroVintage_;  // re-pointed 2026-05-13: algo 7 slot now hosts DattorroPlateVintage (vintage-hardware post-EQ on Dattorro tank). Variable name retained so call sites stay stable.
    DenseEarlyField    dattorroDenseField_; // algo 0 dense early-field, summed post-tank (off by default → bit-null). See setDattorroDenseField.
    ReverseRoomEngine  reverseRoom_;     // algo 9 (2026-05-31): causal rising-ER onset + dark FDN tail; replicates Lexicon PCM Room "Reverse 1".
    FDNReverbT<true>   accurateHall_;    // algo 10 (2026-06-09): FDN + per-octave GEQ. Also the fallback for the removed VintageTank(8)/AccurateHall32(12) engines on old saved sessions.
    SparseEarlyField   sparseField_;     // algo 11 (2026-06-10): velvet-noise front-loaded sparse early field. Summed with a reduced accurateHall_ tail in the SparseField process() case.
    DiffusedEarlyReflections diffuseER_; // 2026-06-25: DISCRETE-but-smooth early reflections (diffuse-then-tap) — the clarity/un-masking comb the velvet field can't do without going metallic. DenseHall additive bus; bit-null when no reflections set.
    // Removed 2026-06-13 (no factory preset; biggest dead RAM): VintageTankEngine
    // vintageTank_ (algo 8) + FDNReverbT<true,32> accurateHall32_ (algo 12). Those
    // enum values fall back to accurateHall_.
    // algo 13 (TiledRoom) is a COMPOSITE in the process() switch: sparseField_ ER
    // + accurateHall_ 16-line tail (shared with SparseField). No dedicated member
    // — the standalone 4-line TiledRoomEngine was a kill-test (flutter+spectral),
    // superseded by this composite. setTiledRoomVoicing() configures sparseField_.
    ParallelMultibandTank pmb_;          // algo 15 (2026-07-04): per-band decoupled tank (pilot)
    DenseHallReverb    denseHall_;       // algo 14 (2026-06-13): diffused-FDN dense hall — the
                                         // smooth dense late field the 16-line FDN can't reach.
                                         // COMPOSITE: sparseField_ ER + denseHall_ tail in the switch.
    BuildupDiffuser    buildupDiffuser_; // 2026-06-19: long allpass cascade that makes the DenseHall
                                         // tail BUILD gradually (quiet early → the SparseField ER owns
                                         // the early window with its dip + burst2 tap). Opt-in (amount
                                         // 0 → bypassed → composite feeds the tank directly → bit-null).
    bool               buildupPostTank_ = false;  // diffuse tank OUTPUT (Blade: builds onset, leaves the
                                                   // recirculation → T60/decay/spectral intact) vs INPUT.

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

    // Phase 3 output match-EQ: a static per-octave (9-band) GEQ on the wet output
    // that shapes DV's steady-state spectral envelope toward the anchor's measured
    // octave balance (closes the ss-energy gates — engine-agnostic, post-tank,
    // pre-mix). Per-preset gain table via setOutputMatchEQ(); identity / skipped
    // when inactive → bit-null. Stereo state (L/R), shared coeffs.
    OctaveBandDamping matchEQL_, matchEQR_;
    OctaveBandDamping::Coeffs matchCoeffs_ {};
    bool matchEQActive_ = false;
    // Sanitized per-octave gains (last set via setOutputMatchEQ) — retained so
    // prepare() can redesign matchCoeffs_ at a new sample rate. Identity default.
    float matchCorr_[OctaveBandDamping::kNumBands] =
        { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

    // Phase A (early-field engine): tank-onset delay. Pushes the late DenseHall
    // tail back by tankOnsetSamples_ RELATIVE to the undelayed sparse ER, so the
    // ER owns the early window (controls energy_t50 / first50 / attack — the
    // front-load the FDN-fast-fill tank can't). 0 = off = bit-null. DenseHall path
    // only for Phase A (the make-or-break proof on Cathedral + Vocal Hall).
    std::vector<float> tankOnsetBufL_, tankOnsetBufR_;
    int tankOnsetWrite_   = 0;
    int tankOnsetSamples_ = 0;
    float tankOnsetMs_    = 0.0f;   // requested onset (ms); sample count recomputed from this

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
    std::vector<float> buildupBufL_, buildupBufR_; // DenseHall buildup: diffused tank-input copy

    // FORK A — discrete EARLY-REFLECTION tap ("duh-duh"). A single delayed dry tap
    // (~90-110 ms, per-preset) summed to the wet, giving the prominent SECOND
    // arrival the VVV hall anchors have at that time (DV's tank decays smoothly
    // through it). NOT the smooth er_ cluster (8-80 ms) and NOT a tank-onset delay
    // (that left a silence gap — reverted). One discrete reflection + a slightly
    // offset R tap for width + a gentle LP (a real reflection is darker). reflGain_
    // 0 → off → bit-identical. Fed from tankIn (post-predelay dry).
    std::vector<float> reflBuf_;
    std::vector<float> reflDryMono_;   // clean pre-diffuser dry mono (the tap's CLEAN feed, snapshot in the pre-delay loop)
    int   reflMask_ = 0, reflWritePos_ = 0;
    int   reflDelayL_ = 0, reflDelayR_ = 0;   // samples
    float reflGain_ = 0.0f;                    // 0 = off (bit-null)
    bool  reflActive_ = false;
    float reflLpCoeff_ = 0.0f, reflLpStateL_ = 0.0f, reflLpStateR_ = 0.0f;  // reflection HF rolloff

    // Early-tap BANK (setEarlyTapBank) — up to 8 discrete reflections at the
    // ANCHOR's measured arrival times. Generalizes the single Fork-A tap: the
    // early-refl-count gate wants 2-10 discrete arrivals; one tap can't match a
    // pattern. Shares reflBuf_/reflDryMono_ (clean pre-diffuser dry). Per-tap
    // alternating ±4 ms R offset decorrelates arrivals (width without a mono
    // slap-train). etapCount_ 0 → skipped → bit-identical fleet.
    static constexpr int kMaxEarlyTaps = 8;
    int   etapDelayL_[kMaxEarlyTaps] = {};
    int   etapDelayR_[kMaxEarlyTaps] = {};
    float etapGain_[kMaxEarlyTaps] = {};
    float etapMs_[kMaxEarlyTaps] = {};         // requested times, re-realized on prepare()
    int   etapCount_ = 0;
    bool  etapActive_ = false;
    float etapLpCoeff_ = 1.0f, etapLpStateL_ = 0.0f, etapLpStateR_ = 0.0f;
    float etapLpFcHz_ = 9000.0f;

    // algo 11 SparseField: level of the reduced accurateHall_ tail under the
    // sparse early field (1.0 = full tail). Per-preset via kSparseFieldByName.
    float sparseTailGain_ = 0.45f;
    // algo 13 composite: ER front-end level in the mix (1.0 = full ER). Lets a
    // less-front-loaded room (Medium Drum Room, anchor first50 44.8%) back off the
    // ER so it doesn't overshoot. Default 1.0 = Tiled Room behaviour (bit-exact).
    float sparseERGain_ = 1.0f;
    float diffuseERGain_ = 1.0f;   // DiffusedEarlyReflections bus level (DenseHall)

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

    // Per-band Width tilt (setWidthBands). 3-band split of the SIDE signal via two
    // independent one-pole LPs (300 Hz, 5 kHz) — complementary so low+mid+high
    // reconstruct side exactly: low=LP300, mid=LP5k−LP300, high=side−LP5k. Each
    // band scaled independently, then ×global Width. bandWidthActive_ false (all
    // muls 1.0) → the legacy single-multiply path runs → bit-identical fleet.
    float widthBandLow_ = 1.0f, widthBandMid_ = 1.0f, widthBandHi_ = 1.0f;
    bool  bandWidthActive_ = false;
    float wbLp1Coeff_ = 0.0f, wbLp2Coeff_ = 0.0f;   // 300 Hz / 5 kHz one-pole coeffs
    float wbLp1State_ = 0.0f, wbLp2State_ = 0.0f;   // filter state (on the side signal)
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

    // Output air-shelf (HF voicing): a post-tank RBJ high-shelf that can BOOST
    // — unlike hiCutFilter_ (cut-only) and the cut-only output match-EQ — to
    // lift the HF-deficient fleet toward the bright references (the documented
    // HF-tilt wall: no engine had an HF up-tilt lever). Per-preset freq + gain
    // dB; 0 dB → inactive → bit-identical bypass. Feed-forward (not in any
    // feedback loop), so |H|>1 boost is unconditionally stable.
    Biquad airShelfFilter_;
    float  airShelfFreqHz_ = 8000.0f;
    float  airShelfGainDb_ = 0.0f;
    bool   airShelfActive_ = false;
    void   updateAirShelfCoeffs();

    // Output low-shelf (LF "fullness" voicing): the deep-sub counterpart of the
    // air-shelf — a post-tank RBJ low-shelf that can BOOST the 20-60Hz deep-sub
    // octave the boom gates (40Hz+) and the cut-only EQs don't cover, restoring the
    // weight a preset's Lo Cut strips vs the references. Per-preset freq + gain dB;
    // 0 dB → inactive → bit-identical bypass. Feed-forward → boost is stable.
    Biquad lowShelfFilter_;
    float  lowShelfFreqHz_ = 60.0f;
    float  lowShelfGainDb_ = 0.0f;
    bool   lowShelfActive_ = false;
    void   updateLowShelfCoeffs();

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
    void recomputeTankFeedCoeffs();   // tank-feed shelf coeffs from stored Fc at sampleRate_
    void recomputeTankOnsetSamples(); // tank-onset sample count from tankOnsetMs_ at sampleRate_
    void designMatchEQ();             // (re)design match-EQ coeffs from matchCorr_ at sampleRate_
};
