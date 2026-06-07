#include "DuskVerbEngine.h"
#include "DspUtils.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    constexpr float kTwoPi = 6.283185307179586f;
}

void DuskVerbEngine::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;

    // All engines stay prepared so setAlgorithm() never has to allocate.
    dattorro_.prepare (sampleRate, maxBlockSize);
    sixAPTank_.prepare (sampleRate, maxBlockSize);
    quad_.prepare (sampleRate, maxBlockSize);
    fdn_.prepare (sampleRate, maxBlockSize);
    multibandFdn_.prepare (sampleRate, maxBlockSize);
    multibandFdn_.setCrossovers (300.0f, 5000.0f);
    spring_.prepare (sampleRate, maxBlockSize);
    nonLinear_.prepare (sampleRate, maxBlockSize);
    shimmer_.prepare (sampleRate, maxBlockSize);
    dattorroVintage_.prepare (sampleRate, maxBlockSize);
    vintageTank_.prepare (sampleRate, maxBlockSize);
    reverseRoom_.prepare (sampleRate, maxBlockSize);

    diffuser_.prepare (sampleRate, maxBlockSize);
    er_.prepare (sampleRate, maxBlockSize);

    // Pre-delay buffer sized for 250 ms (max in APVTS layout).
    int maxPreDelaySamples =
        static_cast<int> (std::ceil (0.250f * static_cast<float> (sampleRate))) + 4;
    int bufSize = DspUtils::nextPowerOf2 (maxPreDelaySamples);
    preDelayBufL_.assign (static_cast<size_t> (bufSize), 0.0f);
    preDelayBufR_.assign (static_cast<size_t> (bufSize), 0.0f);
    preDelayWritePos_ = 0;
    preDelayMask_ = bufSize - 1;
    preDelaySamples_ = 0;

    tankInL_.assign  (static_cast<size_t> (maxBlockSize), 0.0f);
    tankInR_.assign  (static_cast<size_t> (maxBlockSize), 0.0f);
    tankOutL_.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    tankOutR_.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    erOutL_.assign   (static_cast<size_t> (maxBlockSize), 0.0f);
    erOutR_.assign   (static_cast<size_t> (maxBlockSize), 0.0f);

    // Per-sample smoothers — short time constants, advance once per sample.
    constexpr float kPerSampleSmoothMs = 2.0f;
    widthSmoother_   .setSmoothingTime (sampleRate, kPerSampleSmoothMs);
    erLevelSmoother_ .setSmoothingTime (sampleRate, kPerSampleSmoothMs);
    gainTrimSmoother_.setSmoothingTime (sampleRate, 5.0f);

    // Per-block smoothers — longer time constants so they evolve across
    // multiple blocks (typical buffer = 5–20 ms; 30 ms guarantees the
    // downstream recompute steps hit smooth intermediate values).
    constexpr float kPerBlockSmoothMs = 30.0f;
    sizeSmoother_     .setSmoothingTime (sampleRate, kPerBlockSmoothMs);
    loCutSmoother_    .setSmoothingTime (sampleRate, kPerBlockSmoothMs);
    hiCutSmoother_    .setSmoothingTime (sampleRate, kPerBlockSmoothMs);
    monoBelowSmoother_.setSmoothingTime (sampleRate, kPerBlockSmoothMs);

    widthSmoother_   .reset (1.0f);
    erLevelSmoother_ .reset (1.0f);
    gainTrimSmoother_.reset (1.0f);
    sizeSmoother_    .reset (0.5f);
    loCutSmoother_   .reset (20.0f);
    hiCutSmoother_   .reset (20000.0f);
    monoBelowSmoother_.reset (20.0f);

    // Force per-block consumers to apply the initial value on the first block.
    lastAppliedSize_   = -1.0f;
    lastAppliedLoCut_  = -1.0f;
    lastAppliedHiCut_  = -1.0f;
    lastAppliedMonoHz_ = -1.0f;

    monoLPStateL_     = 0.0f;
    monoLPStateR_     = 0.0f;

    // Phase 4 (Change 2): cross-talk HF split — 1st-order LP coeff at 1.5 kHz.
    xtalkHpCoeff_ = std::exp (-kTwoPi * 1500.0f / static_cast<float> (sampleRate));
    xtalkLpL_     = 0.0f;
    xtalkLpR_     = 0.0f;

    // Post-tank parametric EQ. Default state is all bands at gainDb=0 →
    // unity coefficients → bit-identical bypass. Per-preset overrides
    // come in via setPostTankEQBand() after the preset loads.
    postTankEQ_.prepare (static_cast<float> (sampleRate));
    postTankBandTrim_.prepare (static_cast<float> (sampleRate));
    perBandEDT_.prepare (static_cast<float> (sampleRate));

    // Force-apply algorithm 0 on first prepare (don't bypass via early-return).
    currentAlgorithm_ = -1;
    setAlgorithm (0);
}

void DuskVerbEngine::clearAllBuffers()
{
    dattorro_ .clearBuffers();
    sixAPTank_.clearBuffers();
    quad_     .clearBuffers();
    fdn_      .clearBuffers();
    multibandFdn_.clearBuffers();
    spring_   .clearBuffers();
    nonLinear_.clearBuffers();
    shimmer_  .clearBuffers();
    dattorroVintage_.clearBuffers();
    vintageTank_.clearBuffers();
    reverseRoom_.clearBuffers();

    // Pre-tank input diffuser and early reflections — both retain
    // signal-carrying state (allpass buffers, multi-tap delay lines, per-tap
    // LP states) that survives setAlgorithm() and would bleed stale audio
    // into the new preset's tail when an idle engine is reused.
    diffuser_.clear();
    er_.clear();

    std::fill (preDelayBufL_.begin(), preDelayBufL_.end(), 0.0f);
    std::fill (preDelayBufR_.begin(), preDelayBufR_.end(), 0.0f);
    preDelayWritePos_ = 0;

    loCutFilter_.reset();
    hiCutFilter_.reset();
    postTankEQ_.reset();
    postTankBandTrim_.reset();
    perBandEDT_.reset();

    monoLPStateL_ = 0.0f;
    monoLPStateR_ = 0.0f;
    xtalkLpL_     = 0.0f;
    xtalkLpR_     = 0.0f;
}

void DuskVerbEngine::snapSmoothersToTargets()
{
    // OnePoleSmoother::current and ::target are public struct fields. Setting
    // current = target collapses the per-sample/per-block glide so the engine
    // produces target-value output from the next sample onward — required when
    // an idle engine is being swapped in via crossfade and must not glide
    // through stale shell-parameter values.
    widthSmoother_    .current = widthSmoother_    .target;
    erLevelSmoother_  .current = erLevelSmoother_  .target;
    gainTrimSmoother_ .current = gainTrimSmoother_ .target;
    sizeSmoother_     .current = sizeSmoother_     .target;
    loCutSmoother_    .current = loCutSmoother_    .target;
    hiCutSmoother_    .current = hiCutSmoother_    .target;
    monoBelowSmoother_.current = monoBelowSmoother_.target;

    // Force per-block consumers (size→tank delays, lo/hi-cut biquad coeffs,
    // mono-maker coeff) to recompute on the first block after the snap.
    lastAppliedSize_   = -1.0f;
    lastAppliedLoCut_  = -1.0f;
    lastAppliedHiCut_  = -1.0f;
    lastAppliedMonoHz_ = -1.0f;
}

void DuskVerbEngine::copyInputHistoryFrom (const DuskVerbEngine& other)
{
    if (preDelayBufL_.size() == other.preDelayBufL_.size())
    {
        preDelayBufL_     = other.preDelayBufL_;
        preDelayBufR_     = other.preDelayBufR_;
        preDelayWritePos_ = other.preDelayWritePos_;
    }
    er_.copySignalStateFrom (other.er_);
    diffuser_.copyStateFrom (other.diffuser_);
}

void DuskVerbEngine::setAlgorithm (int index)
{
    if (index == currentAlgorithm_)
        return;

    currentAlgorithm_ = index;
    currentEngine_ = getAlgorithmConfig (index).engine;

    dattorro_.clearBuffers();
    sixAPTank_.clearBuffers();
    quad_.clearBuffers();
    fdn_.clearBuffers();
    multibandFdn_.clearBuffers();
    spring_.clearBuffers();
    nonLinear_.clearBuffers();
    shimmer_.clearBuffers();
    dattorroVintage_.clearBuffers();
    vintageTank_.clearBuffers();
    reverseRoom_.clearBuffers();
}

void DuskVerbEngine::setFreeze (bool frozen)
{
    if (frozen == frozen_)
        return;
    frozen_ = frozen;
    dattorro_.setFreeze (frozen);
    sixAPTank_.setFreeze (frozen);
    quad_.setFreeze (frozen);
    fdn_.setFreeze (frozen);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setFreeze (frozen); });
    spring_.setFreeze (frozen);
    nonLinear_.setFreeze (frozen);
    shimmer_.setFreeze (frozen);
    dattorroVintage_.setFreeze (frozen);
    reverseRoom_.setFreeze (frozen);
}

// Forward to the NonLinear engine — it's the only algorithm with a gate.
void DuskVerbEngine::setNonLinearGateEnabled (bool enabled)
{
    nonLinear_.setGateEnabled (enabled);
}

void DuskVerbEngine::setDecayTime (float seconds)
{
    dattorro_.setDecayTime (seconds);
    sixAPTank_.setDecayTime (seconds);
    quad_.setDecayTime (seconds);
    fdn_.setDecayTime (seconds);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setDecayTime (seconds); });
    spring_.setDecayTime (seconds);
    nonLinear_.setDecayTime (seconds);
    shimmer_.setDecayTime (seconds);
    dattorroVintage_.setDecayTime (seconds);
    vintageTank_.setDecayTime (seconds);
    reverseRoom_.setDecayTime (seconds);
}

void DuskVerbEngine::setSize (float size)
{
    // Target only — pushSizeToTanks() runs once per block at the top of process().
    sizeSmoother_.setTarget (std::clamp (size, 0.0f, 1.0f));
}

void DuskVerbEngine::pushSizeToTanks (float size)
{
    dattorro_.setSize (size);
    sixAPTank_.setSize (size);
    quad_.setSize (size);
    fdn_.setSize (size);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setSize (size); });
    spring_.setSize (size);
    nonLinear_.setSize (size);
    shimmer_.setSize (size);
    dattorroVintage_.setSize (size);
    vintageTank_.setSize (size);
    reverseRoom_.setSize (size);
}

void DuskVerbEngine::setBassMultiply (float mult)
{
    dattorro_.setBassMultiply (mult);
    sixAPTank_.setBassMultiply (mult);
    quad_.setBassMultiply (mult);
    fdn_.setBassMultiply (mult);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setBassMultiply (mult); });
    spring_.setBassMultiply (mult);
    nonLinear_.setBassMultiply (mult);
    shimmer_.setBassMultiply (mult);
    dattorroVintage_.setBassMultiply (mult);
    vintageTank_.setBassMultiply (mult);
    reverseRoom_.setBassMultiply (mult);
}

void DuskVerbEngine::setMidMultiply (float mult)
{
    dattorro_.setMidMultiply (mult);
    sixAPTank_.setMidMultiply (mult);
    quad_.setMidMultiply (mult);
    fdn_.setMidMultiply (mult);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setMidMultiply (mult); });
    spring_.setMidMultiply (mult);
    nonLinear_.setMidMultiply (mult);
    shimmer_.setMidMultiply (mult);
    dattorroVintage_.setMidMultiply (mult);
    vintageTank_.setMidMultiply (mult);
    reverseRoom_.setMidMultiply (mult);
}

void DuskVerbEngine::setTrebleMultiply (float mult)
{
    dattorro_.setTrebleMultiply (mult);
    sixAPTank_.setTrebleMultiply (mult);
    quad_.setTrebleMultiply (mult);
    fdn_.setTrebleMultiply (mult);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setTrebleMultiply (mult); });
    spring_.setTrebleMultiply (mult);
    nonLinear_.setTrebleMultiply (mult);
    shimmer_.setTrebleMultiply (mult);
    dattorroVintage_.setTrebleMultiply (mult);
    vintageTank_.setTrebleMultiply (mult);
    reverseRoom_.setTrebleMultiply (mult);
}

void DuskVerbEngine::setAirTrebleMultiply (float mult)
{
    // FDN-specific bug-fix: feed the same damping value into the FDN's
    // airTrebleMultiply_ member that computeDecayCoefficients actually
    // reads. Without this, the APVTS damping knob is dead-code on the
    // FDN engine path because fdn_.setTrebleMultiply writes to a member
    // that is never consumed inside the per-line decay calc.
    fdn_.setAirTrebleMultiply (mult);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setAirTrebleMultiply (mult); });
}

// FiveBandDamping (Phase 2) — FDN-only; other engines have no five-band path.
// Forward to BOTH the legacy single tank and the parallel-multiband tanks so
// the multiband path (when mb_enable is on) receives identical voicing instead
// of silently running these axes at their defaults.
void DuskVerbEngine::setSubMultiply     (float mult) { fdn_.setSubMultiply (mult);      multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setSubMultiply (mult); }); }
void DuskVerbEngine::setHiMidMultiply   (float mult) { fdn_.setHiMidMultiply (mult);    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setHiMidMultiply (mult); }); }
// QuadTank 5-band split (hi-mid 4-8k / air >8k). Separate from the FDN path:
// QuadTank's transparency sentinel is -1, distinct from the FDN convention.
void DuskVerbEngine::setQuadHiMidMultiply (float mult) { quad_.setHiMidMultiply (mult); }
void DuskVerbEngine::setQuadAirMultiply   (float mult) { quad_.setAirMultiply (mult); }
void DuskVerbEngine::setSubCrossoverFreq (float hz)  { fdn_.setSubCrossoverFreq (hz);   multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setSubCrossoverFreq (hz); }); }
void DuskVerbEngine::setAirCrossoverFreq (float hz)  { fdn_.setAirCrossoverFreq (hz);   multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setAirCrossoverFreq (hz); }); }
void DuskVerbEngine::setShaperDepth     (float d)    { fdn_.setShaperDepth (d);         multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setShaperDepth (d); }); }
void DuskVerbEngine::setShaperTimeMs    (float ms)   { fdn_.setShaperTimeMs (ms);       multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setShaperTimeMs (ms); }); }
void DuskVerbEngine::setShaperXoverHz   (float hz)   { fdn_.setShaperXoverHz (hz);      multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setShaperXoverHz (hz); }); }
void DuskVerbEngine::setShaperSens      (float s)    { fdn_.setShaperSens (s);          multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setShaperSens (s); }); }
void DuskVerbEngine::setInputSubGainDb  (float db)   { fdn_.setInputSubGainDb (db);     multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setInputSubGainDb (db); }); }
void DuskVerbEngine::setInputMidGainDb  (float db)   { fdn_.setInputMidGainDb (db);     multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setInputMidGainDb (db); }); }
void DuskVerbEngine::setInputHighGainDb (float db)   { fdn_.setInputHighGainDb (db);    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setInputHighGainDb (db); }); }

void DuskVerbEngine::setCrossoverFreq (float hz)
{
    dattorro_.setCrossoverFreq (hz);
    sixAPTank_.setCrossoverFreq (hz);
    quad_.setCrossoverFreq (hz);
    fdn_.setCrossoverFreq (hz);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setCrossoverFreq (hz); });
    spring_.setCrossoverFreq (hz);
    nonLinear_.setCrossoverFreq (hz);
    shimmer_.setCrossoverFreq (hz);
    dattorroVintage_.setCrossoverFreq (hz);
    vintageTank_.setLowCrossover (hz);
    reverseRoom_.setCrossoverFreq (hz);
}

void DuskVerbEngine::setHighCrossoverFreq (float hz)
{
    dattorro_.setHighCrossoverFreq (hz);
    sixAPTank_.setHighCrossoverFreq (hz);
    quad_.setHighCrossoverFreq (hz);
    fdn_.setHighCrossoverFreq (hz);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setHighCrossoverFreq (hz); });
    spring_.setHighCrossoverFreq (hz);
    nonLinear_.setHighCrossoverFreq (hz);
    shimmer_.setHighCrossoverFreq (hz);
    dattorroVintage_.setHighCrossoverFreq (hz);
    reverseRoom_.setHighCrossoverFreq (hz);
}

void DuskVerbEngine::setSaturation (float amount)
{
    dattorro_.setSaturation (amount);
    sixAPTank_.setSaturation (amount);
    quad_.setSaturation (amount);
    fdn_.setSaturation (amount);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setSaturation (amount); });
    spring_.setSaturation (amount);
    nonLinear_.setSaturation (amount);
    shimmer_.setSaturation (amount);
    dattorroVintage_.setSaturation (amount);
    reverseRoom_.setSaturation (amount);
}

void DuskVerbEngine::setModDepth (float depth)
{
    // ALL engines receive modulation. Even a microscopic LFO depth (5–10 %)
    // is required on QuadTank to break the static phase-locks that produce
    // metallic ringing — the perfectly-deterministic delay reads otherwise
    // line up modal energy into a fixed comb pattern.
    // Spring engine reinterprets this as "SPRING LEN" — read-position LFO
    // depth in samples (0 = static spring, 1 = full ±6 sample drip wobble).
    dattorro_.setModDepth (depth);
    sixAPTank_.setModDepth (depth);
    quad_.setModDepth (depth);
    fdn_.setModDepth (depth);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setModDepth (depth); });
    spring_.setModDepth (depth);
    nonLinear_.setModDepth (depth);
    shimmer_.setModDepth (depth);   // hijacked → PITCH (0..1 → 0..24 semitones)
    dattorroVintage_.setModDepth (depth);
    // VintageTank: depth knob ∈ [0, 1] → mod excursion in samples (~0..16
    // sample sweep range, the Lex/Griesinger lush-mode default).
    vintageTank_.setModDepth (depth * 16.0f);
    reverseRoom_.setModDepth (depth);
}

void DuskVerbEngine::setModulationTopology (DspUtils::ModulationTopology t)
{
    // Phase 2: only FDN + QuadTank have the coherent topology implemented
    // for now. Other engines silently ignore (they use their own bespoke
    // modulators — DPV's structured tank, Shimmer's pitch loop, etc.).
    fdn_.setModulationTopology (t);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setModulationTopology (t); });
    quad_.setModulationTopology (t);
}

void DuskVerbEngine::setPerLineDecayTilt (float shortLineScale, float longLineScale)
{
    // Phase α: only FDN has the per-line decay-tilt path. QuadTank uses
    // 4 cross-coupled tanks (different topology), Dattorro is a single
    // structured tank — neither has a per-line rank to tilt against.
    fdn_.setPerLineDecayTilt (shortLineScale, longLineScale);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setPerLineDecayTilt (shortLineScale, longLineScale); });
}

void DuskVerbEngine::setFDNBaseDelays (const int* delays)
{
    // Phase β: per-preset FDN base-delay set. Only the FDN engine has a
    // 16-line tank; other engines ignore. Pass nullptr → preserve engine
    // default (kDefaultDelays — log-spaced primes 1151..6451 samples).
    if (delays == nullptr) return;
    fdn_.setBaseDelays (delays);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setBaseDelays (delays); });
}

void DuskVerbEngine::reapplyNeutralEngineConfig()
{
    // PostTankEQ → all 4 bands flat (gain 0 → unity coefficients).
    for (int b = 0; b < DspUtils::PostTankEQ::kNumBands; ++b)
        postTankEQ_.setBand (b, 1000.0f, 1.0f, 0.0f);
    // Modulation topology → legacy RandomWalk default.
    setModulationTopology (DspUtils::ModulationTopology::RandomWalk);
    // Per-line decay tilt → flat (1.0 / 1.0 = no tilt).
    setPerLineDecayTilt (1.0f, 1.0f);
    // FDN base delays → engine default log-spaced primes.
    fdn_.resetBaseDelays();
    multibandFdn_.forEachTank ([](FDNReverb& tk){ tk.resetBaseDelays(); });
}

void DuskVerbEngine::setFDNInLoopPeaking (float freqHz, float qFactor, float gainDb)
{
    // Phase ε: only FDN has in-loop per-line peaking infrastructure.
    fdn_.setInLoopPeaking (freqHz, qFactor, gainDb);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setInLoopPeaking (freqHz, qFactor, gainDb); });
}

void DuskVerbEngine::setMultibandEnabled (bool enabled)
{
    multibandActive_ = enabled;
}

void DuskVerbEngine::setMultibandDecays (float lowSec, float midSec, float highSec)
{
    if (lowSec  > 0.0f) multibandFdn_.lowTank() .setDecayTime (lowSec);
    if (midSec  > 0.0f) multibandFdn_.midTank() .setDecayTime (midSec);
    if (highSec > 0.0f) multibandFdn_.highTank().setDecayTime (highSec);
}

void DuskVerbEngine::setFDNDualBassShelf (float fastFc, float slowFc,
                                            float fastGainDb, float slowGainDb,
                                            float transitionMs)
{
    // Phase η: only FDN has the per-line dual-time-constant bass shelf
    // infrastructure.
    fdn_.setDualBassShelf (fastFc, slowFc, fastGainDb, slowGainDb, transitionMs);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setDualBassShelf (fastFc, slowFc, fastGainDb, slowGainDb, transitionMs); });
}

void DuskVerbEngine::setModRate (float hz)
{
    dattorro_.setModRate (hz);
    sixAPTank_.setModRate (hz);
    quad_.setModRate (hz);
    fdn_.setModRate (hz);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setModRate (hz); });
    spring_.setModRate (hz);
    nonLinear_.setModRate (hz);
    shimmer_.setModRate (hz);       // hijacked → FEEDBACK (0.1..10 Hz → 0..0.95 cascade gain)
    dattorroVintage_.setModRate (hz);
    vintageTank_.setModRate (hz);
    reverseRoom_.setModRate (hz);
}

// Tail Spin/Wander (post-loop output AM) exists only on the FDN-based engines.
// Forward to the FDN tank and to ReverseRoom (which owns an FDN for its tail);
// the other engines have no such stage.
void DuskVerbEngine::setTailSpinDepth (float depth)
{
    fdn_.setTailSpinDepth (depth);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setTailSpinDepth (depth); });
    reverseRoom_.setTailSpinDepth (depth);
}

void DuskVerbEngine::setTailSpinRate (float hz)
{
    fdn_.setTailSpinRate (hz);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setTailSpinRate (hz); });
    reverseRoom_.setTailSpinRate (hz);
}

void DuskVerbEngine::setDiffusion (float amount)
{
    // Two-stage routing:
    //   1) Input DiffusionStage smears the transient before it hits the tank.
    //   2) Each engine's in-loop density coefficient scales around its
    //      baseline — this is what controls late-tail density (smooth vs
    //      tap-tap-tap). Without this second stage the knob only affects the
    //      first ~50 ms of attack and the tail character stays fixed.
    // Spring engine reinterprets setTankDiffusion as the "CHIRP" knob —
    // dispersion-AP coefficient magnitude (0 = no chirp, 1 = full boing).
    diffuser_.setDiffusion    (amount);
    dattorro_.setTankDiffusion    (amount);
    sixAPTank_.setTankDiffusion (amount);
    quad_.setTankDiffusion        (amount);
    fdn_.setTankDiffusion         (amount);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setTankDiffusion (amount); });
    spring_.setTankDiffusion      (amount);
    nonLinear_.setTankDiffusion   (amount);
    shimmer_.setTankDiffusion     (amount);   // no-op (FDN's mix IS the diffusion)
    dattorroVintage_.setTankDiffusion (amount);
    // VintageTank: route APVTS "Diffusion" knob to the input-AP coefficient
    // (per user spec). Tank-loop AP coefficient stays at preset default to
    // preserve the lush figure-8 modal density.
    vintageTank_.setInputDiffusion (amount);
    reverseRoom_.setTankDiffusion (amount);
}

void DuskVerbEngine::setBassChokeHz (float hz)
{
    // Only the DattorroVintage engine uses this. Other engines: no-op.
    dattorroVintage_.setBassChokeHz (hz);
}

void DuskVerbEngine::setERLevel (float level)
{
    erLevelSmoother_.setTarget (std::clamp (level, 0.0f, 1.0f));
    // DattorroVintage uses its own sparse-tap ER generator and reads
    // this level directly. Other engines ignore the call.
    dattorroVintage_.setSparseTapLevel (level);
}

void DuskVerbEngine::setERSize (float size)
{
    er_.setSize (size);
}

void DuskVerbEngine::setEREarlyBoost (float boost)
{
    erEarlyBoost_ = std::clamp (boost, 1.0f, 8.0f);
}

void DuskVerbEngine::setEROnsetRiseMs (float ms)
{
    er_.setOnsetRiseMs (ms);
}

void DuskVerbEngine::setOutputCrossTalk (float depth)
{
    xtalkDepth_  = std::clamp (depth, 0.0f, 1.0f);
    xtalkActive_ = xtalkDepth_ > 1.0e-6f;
}

void DuskVerbEngine::setPreDelay (float milliseconds)
{
    float clamped = std::clamp (milliseconds, 0.0f, 250.0f);
    int samples = static_cast<int> (clamped * 0.001f * static_cast<float> (sampleRate_));
    preDelaySamples_ = std::min (samples, preDelayMask_);
}

void DuskVerbEngine::setLoCut (float hz)
{
    // Target only — biquad coeffs recomputed once per block in process().
    loCutSmoother_.setTarget (std::clamp (hz, 5.0f, 500.0f));
}

void DuskVerbEngine::setHiCut (float hz)
{
    hiCutSmoother_.setTarget (std::clamp (hz, 1000.0f, 20000.0f));
    // VintageTank's in-loop 3-band damper high-shelf corner sits here too.
    vintageTank_.setHiCut (std::clamp (hz, 1000.0f, 20000.0f));
}

void DuskVerbEngine::setPostTankEQBand (int index, float freqHz, float qFactor, float gainDb)
{
    postTankEQ_.setBand (index, freqHz, qFactor, gainDb);
}

void DuskVerbEngine::setPostTankBandTrimGainDb (int region, float gainDb)
{
    postTankBandTrim_.setRegionGainDb (region, gainDb);
}

void DuskVerbEngine::setPostTankBandTrimCrossovers (float fLow, float fMid, float fHi)
{
    postTankBandTrim_.setCrossovers (fLow, fMid, fHi);
}

void DuskVerbEngine::setPerBandEDTShape (int region, float attackDb, float tauMs)
{
    perBandEDT_.setRegionShape (region, attackDb, tauMs);
}

void DuskVerbEngine::setPerBandEDTCrossovers (float fLow, float fMid, float fHi)
{
    perBandEDT_.setCrossovers (fLow, fMid, fHi);
}

void DuskVerbEngine::setHiCutShelfGainDb (float dB)
{
    const float clamped = std::clamp (dB, -24.0f, 0.0f);
    if (clamped == hiCutShelfGainDb_)
        return;
    hiCutShelfGainDb_ = clamped;
    // Force coefficient recompute at the current corner — the per-block
    // smoother won't re-run updateHiCutCoeffs unless the FREQUENCY moves.
    updateHiCutCoeffs (hiCutSmoother_.current);

    // VintageTank routes the Hi-Cut-Shelf knob into its in-loop damping LP
    // cutoff (per user spec). dB ∈ [-24, 0] mapped logarithmically to
    // [1500 Hz, 18000 Hz] — fully open = ~18 kHz (no damping), max cut =
    // ~1.5 kHz (dark, vintage-plate-like tail).
    const float t = (clamped + 24.0f) / 24.0f;             // 0 (full cut) .. 1 (no cut)
    const float dampHz = std::pow (1500.0f, 1.0f - t) * std::pow (18000.0f, t);
    vintageTank_.setLoopDamping (dampHz);
}

void DuskVerbEngine::setWidth (float width)
{
    widthSmoother_.setTarget (std::clamp (width, 0.0f, 2.0f));
}

void DuskVerbEngine::setGainTrim (float dB)
{
    float linear = std::pow (10.0f, std::clamp (dB, -48.0f, 48.0f) / 20.0f);
    gainTrimSmoother_.setTarget (linear);
}

void DuskVerbEngine::setMonoBelow (float hz)
{
    // Target only — biquad recomputed once per block in process().
    monoBelowSmoother_.setTarget (std::clamp (hz, 20.0f, 300.0f));
}

void DuskVerbEngine::setMonoBelowDepth (float depth)
{
    monoBelowDepth_ = std::clamp (depth, 0.0f, 1.0f);
}

// ── Per-preset SixAPTank brightness/density tunables ────────────────────────
// These forward unconditionally to sixAPTank_; they only become audible when
// SixAPTank is the active engine, but applying them at preset-load time is
// safe (and necessary so values are in place before processing starts).
// Defaults inside SixAPTankEngine preserve historical behavior so any preset
// that doesn't call these gets identical sound to before this refactor.
void DuskVerbEngine::setSixAPDensityBaseline (float v) { sixAPTank_.setDensityBaseline (v); }
void DuskVerbEngine::setSixAPBloomCeiling    (float v) { sixAPTank_.setBloomCeiling    (v); }
void DuskVerbEngine::setSixAPBloomStagger    (const float values[6]) { sixAPTank_.setBloomStagger (values); }
void DuskVerbEngine::setSixAPEarlyMix        (float v) { sixAPTank_.setEarlyMix        (v); }
void DuskVerbEngine::setSixAPOutputTrim      (float v) { sixAPTank_.setOutputTrim      (v); }

// DattorroPlateVintage (algo 1) per-preset brightness controls. Forwarded
// only to dattorroVintage_; other engines have their own HF handling.
void DuskVerbEngine::setDpvHfShelfGainDb    (float v) { dattorroVintage_.setHfShelfGainDb    (v); }
void DuskVerbEngine::setDpvHfShelfFreqHz    (float v) { dattorroVintage_.setHfShelfFreqHz    (v); }
void DuskVerbEngine::setDpvStructHfDampHz   (float v) { dattorroVintage_.setStructHfDampHz   (v); }
void DuskVerbEngine::setDpvBoxCutGainDb     (float v) { dattorroVintage_.setBoxCutGainDb     (v); }
void DuskVerbEngine::setDpvBoxCutFreqHz     (float v) { dattorroVintage_.setBoxCutFreqHz     (v); }
void DuskVerbEngine::setDpvBassShelfGainDb  (float v) { dattorroVintage_.setBassShelfGainDb  (v); }
void DuskVerbEngine::setDpvBassShelfFreqHz  (float v) { dattorroVintage_.setBassShelfFreqHz  (v); }

void DuskVerbEngine::updateLoCutCoeffs (float hz)
{
    // RBJ 2nd-order Butterworth high-pass.
    float fc = std::clamp (hz, 5.0f, 0.49f * static_cast<float> (sampleRate_));
    float w0 = kTwoPi * fc / static_cast<float> (sampleRate_);
    float cosw = std::cos (w0);
    float sinw = std::sin (w0);
    float alpha = sinw / (2.0f * 0.7071067811865475f);
    float a0 = 1.0f + alpha;

    loCutFilter_.b0 = (1.0f + cosw) * 0.5f / a0;
    loCutFilter_.b1 = -(1.0f + cosw) / a0;
    loCutFilter_.b2 = (1.0f + cosw) * 0.5f / a0;
    loCutFilter_.a1 = -2.0f * cosw / a0;
    loCutFilter_.a2 = (1.0f - alpha) / a0;
}

void DuskVerbEngine::updateHiCutCoeffs (float hz)
{
    // RBJ 2nd-order high-SHELF at Q = 1/√2 (Butterworth-aligned skirt).
    // Replaces the prior brick-wall biquad low-pass — content above the
    // corner is now ATTENUATED by hiCutShelfGainDb_ dB and retained
    // instead of decapitated. Solves the "cliff-drop" perceptual gap
    // we hit on Vocal Hall / Cathedral. Corner stays mapped to the user
    // APVTS Hi Cut knob exactly as before; shelf depth is a per-preset
    // field on FactoryPreset (default -12 dB).
    const float fc = std::clamp (hz, 1000.0f, 0.49f * static_cast<float> (sampleRate_));
    const float fs = static_cast<float> (sampleRate_);
    const float gainDb = std::clamp (hiCutShelfGainDb_, -24.0f, 0.0f);
    // RBJ uses A = sqrt(10^(dB/20)) = 10^(dB/40). At gainDb = 0 this is 1.0
    // (shelf flat), at -12 dB → 0.501 (so above-corner content drops -12 dB
    // toward DC ratio sqrt(A^2) → -12 dB peak attenuation).
    const float A     = std::max (std::pow (10.0f, gainDb / 40.0f), 1.0e-6f);
    const float sqrtA = std::sqrt (A);
    const float w0      = kTwoPi * fc / fs;
    const float cosw    = std::cos (w0);
    const float sinw    = std::sin (w0);
    const float alpha   = sinw / (2.0f * 0.7071067811865475f);
    const float twoSqrtAalpha = 2.0f * sqrtA * alpha;
    const float Aplus1  = A + 1.0f;
    const float Aminus1 = A - 1.0f;
    const float a0      = Aplus1 - Aminus1 * cosw + twoSqrtAalpha;

    hiCutFilter_.b0 =  A * (Aplus1 + Aminus1 * cosw + twoSqrtAalpha) / a0;
    hiCutFilter_.b1 = -2.0f * A * (Aminus1 + Aplus1 * cosw)          / a0;
    hiCutFilter_.b2 =  A * (Aplus1 + Aminus1 * cosw - twoSqrtAalpha) / a0;
    hiCutFilter_.a1 =  2.0f * (Aminus1 - Aplus1 * cosw)              / a0;
    hiCutFilter_.a2 =        (Aplus1 - Aminus1 * cosw - twoSqrtAalpha) / a0;
}

void DuskVerbEngine::process (float* left, float* right, int numSamples)
{
    if (numSamples <= 0)
        return;
    if (numSamples > maxBlockSize_)
        numSamples = maxBlockSize_;

    // ---- 0) Per-block parameter smoothing ----
    // Advance the block-rate smoothers in O(1) (skip = coeff^N), then
    // re-publish to the downstream consumers only if the value moved enough
    // to be audible. Filter coeffs and tank delay-length recomputes are too
    // expensive per-sample, so this gives smooth automation at block cadence.
    {
        const float sizeNow  = sizeSmoother_.skip (numSamples);
        const float loCutNow = loCutSmoother_.skip (numSamples);
        const float hiCutNow = hiCutSmoother_.skip (numSamples);
        const float monoNow  = monoBelowSmoother_.skip (numSamples);

        if (std::abs (sizeNow - lastAppliedSize_) > 0.001f)
        {
            lastAppliedSize_ = sizeNow;
            pushSizeToTanks (sizeNow);
        }
        if (std::abs (loCutNow - lastAppliedLoCut_) > 0.5f)
        {
            lastAppliedLoCut_ = loCutNow;
            updateLoCutCoeffs (loCutNow);
        }
        if (std::abs (hiCutNow - lastAppliedHiCut_) > 1.0f)
        {
            lastAppliedHiCut_ = hiCutNow;
            updateHiCutCoeffs (hiCutNow);
        }
        if (std::abs (monoNow - lastAppliedMonoHz_) > 0.5f)
        {
            lastAppliedMonoHz_ = monoNow;
            // 1st-order LP coefficient: exp(-2π·fc/sr).
            monoLPCoeff_ = std::exp (-kTwoPi * monoNow / static_cast<float> (sampleRate_));
            // Engage the mono branch only when the cutoff is meaningfully
            // above the disable sentinel (20 Hz = sub-audible → no-op).
            monoMakerEnabled_ = (monoNow > 22.0f);
        }
    }

    // ---- 1) Pre-delay ----
    for (int i = 0; i < numSamples; ++i)
    {
        preDelayBufL_[static_cast<size_t> (preDelayWritePos_)] = left[i];
        preDelayBufR_[static_cast<size_t> (preDelayWritePos_)] = right[i];

        const int readPos = (preDelayWritePos_ - preDelaySamples_) & preDelayMask_;
        tankInL_[static_cast<size_t> (i)] = preDelayBufL_[static_cast<size_t> (readPos)];
        tankInR_[static_cast<size_t> (i)] = preDelayBufR_[static_cast<size_t> (readPos)];

        preDelayWritePos_ = (preDelayWritePos_ + 1) & preDelayMask_;
    }

    // ---- 2) Early reflections ----
    er_.process (tankInL_.data(), tankInR_.data(),
                 erOutL_.data(),  erOutR_.data(), numSamples);

    // ---- 3) Tank input diffuser ----
    // Series 4-stage Schroeder cascade. ALL engines except 6-AP route
    // through it — for 6-AP it is bypassed because that engine now uses
    // its own internal ParallelDiffuser (parallel APs summed with
    // alternating ±) which scatters transients more densely than the
    // series cascade can. Running both stacked muddied the 6-AP output
    // and re-introduced the discrete-event perception we were trying to
    // kill, hence the explicit bypass here.
    // Bypass the global series diffuser for engines that own their own
    // input-side smearing OR shouldn't be smeared at all:
    //   • SixAPTank — has its own ParallelDiffuser (parallel-AP shatter)
    //   • Spring — 24-stage dispersion cascade IS the input-side processing,
    //              stacking the diffuser would smear the chirp character
    //   • NonLinear — feed-forward TDL with abrupt envelope edges; pre-
    //              smearing the input would round off the gate cliff
    //   • DattorroVintage — owns its own 6-stage lossless AP front-end
    if (currentEngine_ != EngineType::SixAPTank
        && currentEngine_ != EngineType::Spring
        && currentEngine_ != EngineType::NonLinear
        && currentEngine_ != EngineType::DattorroVintage
        && currentEngine_ != EngineType::ReverseRoom)
        diffuser_.process (tankInL_.data(), tankInR_.data(), numSamples);

    // ---- 4) Selected late tank ----
    switch (currentEngine_)
    {
        case EngineType::Dattorro:
            dattorro_.process (tankInL_.data(), tankInR_.data(),
                               tankOutL_.data(), tankOutR_.data(), numSamples);
            break;
        case EngineType::SixAPTank:
            sixAPTank_.process (tankInL_.data(), tankInR_.data(),
                                  tankOutL_.data(), tankOutR_.data(), numSamples);
            break;
        case EngineType::QuadTank:
            quad_.process (tankInL_.data(), tankInR_.data(),
                           tankOutL_.data(), tankOutR_.data(), numSamples);
            break;
        case EngineType::FDN:
            // Opt-in: parallel-multiband (3 band-isolated tanks) when enabled,
            // else the single legacy tank (bit-identical). Both consume the same
            // diffused tankIn and write tankOut, so the downstream ER/shell
            // chain is unchanged either way.
            if (multibandActive_)
                multibandFdn_.process (tankInL_.data(), tankInR_.data(),
                                       tankOutL_.data(), tankOutR_.data(), numSamples);
            else
                fdn_.process (tankInL_.data(), tankInR_.data(),
                              tankOutL_.data(), tankOutR_.data(), numSamples);
            break;
        case EngineType::Spring:
            spring_.process (tankInL_.data(), tankInR_.data(),
                             tankOutL_.data(), tankOutR_.data(), numSamples);
            break;
        case EngineType::NonLinear:
            nonLinear_.process (tankInL_.data(), tankInR_.data(),
                                tankOutL_.data(), tankOutR_.data(), numSamples);
            break;
        case EngineType::Shimmer:
            shimmer_.process (tankInL_.data(), tankInR_.data(),
                              tankOutL_.data(), tankOutR_.data(), numSamples);
            break;
        case EngineType::DattorroVintage:
            dattorroVintage_.process (tankInL_.data(), tankInR_.data(),
                                       tankOutL_.data(), tankOutR_.data(), numSamples);
            break;
        case EngineType::VintageTank:
        {
            // Stage the pre-tank input into a temporary stereo juce::AudioBuffer
            // so the new VintageTankEngine's juce::AudioBuffer<float>& process()
            // overload can run it through the figure-8 loop in place. Then
            // copy the tank output back into tankOutL_ / tankOutR_ so the
            // downstream sum-and-shell stage handles it identically to the
            // legacy tanks. No allocation on the audio thread — the staging
            // buffer is an alias of the pre-allocated tankOut storage.
            float* outPtrs[2] = { tankOutL_.data(), tankOutR_.data() };
            juce::AudioBuffer<float> alias (outPtrs, 2, numSamples);
            // Copy input → out, then process in place.
            std::memcpy (tankOutL_.data(), tankInL_.data(),
                         sizeof (float) * static_cast<size_t> (numSamples));
            std::memcpy (tankOutR_.data(), tankInR_.data(),
                         sizeof (float) * static_cast<size_t> (numSamples));
            vintageTank_.process (alias);
            break;
        }
        case EngineType::ReverseRoom:
            reverseRoom_.process (tankInL_.data(), tankInR_.data(),
                                  tankOutL_.data(), tankOutR_.data(), numSamples);
            break;
    }

    // ---- 5) Sum + Shell ----
    // DattorroVintage runs its own discrete sparse-tap ER generator and
    // already sums it into the late output. Skip the smooth EarlyReflections
    // contribution for that engine to avoid double-ER.
    //
    // Slot 7 (formerly DattorroVintage, now DattorroPlateVintage) stays
    // bypassed. Briefly re-enabled smooth-ER for it 2026-05-13 but the
    // erLevelSmoother (which resets to 1.0 in prepareToPlay) introduced
    // an audible 5-10 ms ER bleed on every processBlock attack while it
    // ramped down to the preset's er_level=0 target — heard as a tonal
    // shift on transients. Bypass keeps the engine's output pristine.
    // DattorroVintage and ReverseRoom both synthesise their own early
    // reflections (ReverseRoom's rising-onset FIR IS its ER), so adding the
    // shared ER bus on top would double-count the onset. Exclude both.
    const bool useSmoothER = (currentEngine_ != EngineType::DattorroVintage
                           && currentEngine_ != EngineType::ReverseRoom);
    for (int i = 0; i < numSamples; ++i)
    {
        const float erL   = useSmoothER ? erOutL_[static_cast<size_t> (i)] : 0.0f;
        const float erR   = useSmoothER ? erOutR_[static_cast<size_t> (i)] : 0.0f;
        const float lateL = tankOutL_[static_cast<size_t> (i)];
        const float lateR = tankOutR_[static_cast<size_t> (i)];

        const float erLevel = erLevelSmoother_.next();

        // Phase 4 (option 2): early-field ER boost. erEarlyBoost_ default 1.0f →
        // ×1.0 exact → bit-identical. >1 lets the parallel ER run hot enough to
        // own the 0-26 ms transient (which the FDN tank, floored at ~26 ms by
        // its shortest delay line, structurally cannot supply). Post-tank linear
        // combine, NOT in the recursive loop → no feedback-codegen bit-null risk.
        float wetL = erL * erLevel * erEarlyBoost_ + lateL;
        float wetR = erR * erLevel * erEarlyBoost_ + lateR;

        wetL = loCutFilter_.processL (wetL);
        wetR = loCutFilter_.processR (wetR);
        wetL = hiCutFilter_.processL (wetL);
        wetR = hiCutFilter_.processR (wetR);

        // Post-tank parametric EQ — sits AFTER the Hi Cut Shelf and BEFORE
        // the mono / width / gain-trim chain. All-zero-dB default → unity
        // coefficients → bit-identical bypass for presets that don't
        // configure it.
        wetL = postTankEQ_.processL (wetL);
        wetR = postTankEQ_.processR (wetR);

        // Phase γ: decoupled per-band linear gain trim. 3 cascaded high-
        // shelves over 4 regions (Sub/LowMid/MidHi/Air). All region gains
        // 0 dB → bit-identical bypass. Independent of FDN damping coeffs.
        wetL = postTankBandTrim_.processL (wetL);
        wetR = postTankBandTrim_.processR (wetR);

        // Phase δ: per-band attack-ramp envelope shaper. 1-pole crossover
        // split + onset-triggered exponential ramp per region. attackDb=0
        // → AttackRamp returns 1.0 → exact sum-flat bypass (LP + HP = x
        // by construction).
        wetL = perBandEDT_.processL (wetL);
        wetR = perBandEDT_.processR (wetR);

        // Mono Maker — sums L+R below the cutoff to mono before width
        // processing. 1st-order matched-phase complementary split: HP = input
        // − LP, so the HP+LP sum is exactly the input (perfect magnitude
        // reconstruction). Bypassed when monoMakerEnabled_ is false.
        if (monoMakerEnabled_)
        {
            monoLPStateL_ = (1.0f - monoLPCoeff_) * wetL + monoLPCoeff_ * monoLPStateL_;
            monoLPStateR_ = (1.0f - monoLPCoeff_) * wetR + monoLPCoeff_ * monoLPStateR_;
            const float monoLow = 0.5f * (monoLPStateL_ + monoLPStateR_);
            const float highL   = wetL - monoLPStateL_;
            const float highR   = wetR - monoLPStateR_;
            // Partial mono: blend the per-channel low toward the summed mono by
            // monoBelowDepth_. 1.0 = full mono (legacy, bit-identical); <1 leaves
            // the lows PARTIALLY decorrelated — VVV's lows sit ~-0.03 corr, not
            // mono, so full mono over-correlated the broadband stereo image.
            const float lowL = monoBelowDepth_ * monoLow + (1.0f - monoBelowDepth_) * monoLPStateL_;
            const float lowR = monoBelowDepth_ * monoLow + (1.0f - monoBelowDepth_) * monoLPStateR_;
            wetL = lowL + highL;
            wetR = lowR + highR;
        }

        const float width = widthSmoother_.next();
        const float mid   = 0.5f * (wetL + wetR);
        const float side  = 0.5f * (wetL - wetR);
        wetL = mid + side * width;
        wetR = mid - side * width;

        // Phase 4 (Change 2): output cross-talk shelving matrix. Splits each
        // channel at 1.5 kHz (1st-order complementary HP = x − LP), then cross-
        // bleeds the OTHER channel's HF with a 180° inversion scaled by
        // xtalkDepth_. Decorrelates the top-end air per band WITHOUT the global
        // anti-phase overshoot the macro Width knob causes (LF is left fully
        // intact → mono-safe, center-image stable). xtalkActive_ false → wetL/R
        // untouched → bit-identical; post-tank (non-recursive) so no codegen
        // bit-null risk.
        if (xtalkActive_)
        {
            xtalkLpL_ = (1.0f - xtalkHpCoeff_) * wetL + xtalkHpCoeff_ * xtalkLpL_;
            xtalkLpR_ = (1.0f - xtalkHpCoeff_) * wetR + xtalkHpCoeff_ * xtalkLpR_;
            const float hiL = wetL - xtalkLpL_;
            const float hiR = wetR - xtalkLpR_;
            wetL -= xtalkDepth_ * hiR;     // R's HF, inverted, into L
            wetR -= xtalkDepth_ * hiL;     // L's HF, inverted, into R
        }

        // gain_trim is a WET-PATH gain — baked into the engine's wet output so
        // each preset's calibrated wet level is preserved through the
        // processor-side dry/wet crossfade (the dry signal is applied AFTER
        // the engine, so trim never touches it).
        const float trim = gainTrimSmoother_.next();
        left[i]  = wetL * trim;
        right[i] = wetR * trim;
    }
}
