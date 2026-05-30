#pragma once

#include "AlgorithmConfig.h"
#include "DattorroTank.h"
#include "DattorroPlateVintage.h"
#include "DiffusionStage.h"
#include "EarlyReflections.h"
#include "FDNReverb.h"
#include "SixAPTankEngine.h"
#include "NonLinearEngine.h"
#include "QuadTank.h"
#include "ShimmerEngine.h"
#include "SpringEngine.h"
#include "VintageTankEngine.h"

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
    void setCrossoverFreq (float hz);              // bass↔mid (legacy "crossover")
    void setHighCrossoverFreq (float hz);          // mid↔high (3-band)
    void setSaturation    (float amount);          // 0..1 drive-style softClip
    void setModDepth      (float depth);
    void setModRate       (float hz);
    void setDiffusion     (float amount);

    // Early reflections.
    void setERLevel (float level);
    void setERSize  (float size);

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

    // Phase β (2026-05-29): per-preset FDN base delays. Lets each preset
    // choose a 16-int delay-line set tuned to match a specific anchor's
    // per-band modal-beat pattern (Hilbert-FFT envelope peak frequency).
    // No-op for non-FDN engines. Pass nullptr to retain the engine default.
    void setFDNBaseDelays (const int* delays);

    // Phase ε (2026-05-29): in-loop narrow-Q peaking band on the FDN per-
    // line feedback path. Designed to reinforce a specific frequency inside
    // the loop so steady-state input at that frequency builds extra gain
    // (e.g. closes sine1k cold by boosting the 1 kHz mode). gainDb = 0 →
    // unity coefficients → bit-identical bypass on the damping output. No-
    // op for non-FDN engines.
    void setFDNInLoopPeaking (float freqHz, float qFactor, float gainDb);

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
    SpringEngine       spring_;
    NonLinearEngine    nonLinear_;
    ShimmerEngine      shimmer_;
    DattorroPlateVintage dattorroVintage_;  // re-pointed 2026-05-13: algo 7 slot now hosts DattorroPlateVintage (vintage-hardware post-EQ on Dattorro tank). Variable name retained so call sites stay stable.
    DspUtils::VintageTankEngine vintageTank_;  // algo 8 (2026-05-29): Griesinger/Lexicon figure-8 modulated AP loop. Built from first principles, replaces the FDN's unitary Hadamard scatter with a recirculating tank that builds modal density over time.

    // Pre-tank input diffuser, applied to every engine. Smears transients
    // before they hit the tank so onsets bloom into the tail rather than
    // arriving as discrete clicks.
    DiffusionStage diffuser_;
    EarlyReflections er_;

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

    // Per-sample smoothed shell parameters (consumed inside the per-sample loop).
    // mixSmoother lives on the processor (so the dry signal stays correlated
    // across the preset crossfade); engine outputs wet-only.
    OnePoleSmoother widthSmoother_;
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
