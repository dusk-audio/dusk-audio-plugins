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
    dattorro_.prepare(sampleRate, maxBlockSize); lexFigure8_.tank.prepare(sampleRate, maxBlockSize);
    sixAPTank_.prepare (sampleRate, maxBlockSize);
    quad_.prepare (sampleRate, maxBlockSize);
    fdn_.prepare(sampleRate, maxBlockSize); hallTrueLex16_.tank.prepare(sampleRate, maxBlockSize);
    spring_.prepare (sampleRate, maxBlockSize);
    nonLinear_.prepare (sampleRate, maxBlockSize);
    shimmer_.prepare (sampleRate, maxBlockSize);
    dattorroVintage_.prepare (sampleRate, maxBlockSize);
    plate_.prepare (sampleRate, maxBlockSize);
    foilPlate_.prepare (sampleRate, maxBlockSize);
    hall_.prepare(sampleRate, maxBlockSize); hallTrueLex_.tank.prepare(sampleRate, maxBlockSize);
    hallRing_.prepare (sampleRate, maxBlockSize);
    hallHybrid_.prepare (sampleRate, maxBlockSize);
    hallTrueLex_.prepare (sampleRate, maxBlockSize);
    hallTrueLex16_.prepare (sampleRate, maxBlockSize);
    lexFigure8_.prepare (sampleRate, maxBlockSize);
    lexMTDL_.prepare (sampleRate, maxBlockSize);

    diffuser_.prepare (sampleRate, maxBlockSize);
    er_.prepare (sampleRate, maxBlockSize);
    firstRefl_.prepare (sampleRate, maxBlockSize);

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
    hybridMtdlOutL_.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    hybridMtdlOutR_.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    erOutL_.assign   (static_cast<size_t> (maxBlockSize), 0.0f);
    erOutR_.assign   (static_cast<size_t> (maxBlockSize), 0.0f);
    firstReflOutL_.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    firstReflOutR_.assign (static_cast<size_t> (maxBlockSize), 0.0f);

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

    // Force-apply algorithm 0 on first prepare (don't bypass via early-return).
    currentAlgorithm_ = -1;
    setAlgorithm (0);
}

void DuskVerbEngine::clearAllBuffers()
{
    dattorro_.clearBuffers(); lexFigure8_.tank.clearBuffers();
    sixAPTank_.clearBuffers();
    quad_     .clearBuffers();
    fdn_      .clearBuffers();
    spring_   .clearBuffers();
    nonLinear_.clearBuffers();
    shimmer_  .clearBuffers();
    dattorroVintage_.clearBuffers();
    plate_.clearBuffers();
    foilPlate_.clearBuffers();
    hall_.clearBuffers(); hallTrueLex_.tank.clearBuffers();
    hallRing_.clearBuffers();
    hallHybrid_.clearBuffers();
    hallTrueLex16_.clearBuffers();
    lexFigure8_.clearBuffers();
    lexMTDL_.clearBuffers();

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

    monoLPStateL_ = 0.0f;
    monoLPStateR_ = 0.0f;
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

    dattorro_.clearBuffers(); lexFigure8_.tank.clearBuffers();
    sixAPTank_.clearBuffers();
    quad_.clearBuffers();
    fdn_.clearBuffers(); hallTrueLex16_.tank.clearBuffers();
    spring_.clearBuffers();
    nonLinear_.clearBuffers();
    shimmer_.clearBuffers();
    dattorroVintage_.clearBuffers();
    plate_.clearBuffers();
    foilPlate_.clearBuffers();
    hall_.clearBuffers(); hallTrueLex_.tank.clearBuffers();
    hallRing_.clearBuffers();
    hallHybrid_.clearBuffers();
    hallTrueLex16_.clearBuffers();
    lexFigure8_.clearBuffers();
    lexMTDL_.clearBuffers();
}

void DuskVerbEngine::setFreeze (bool frozen)
{
    if (frozen == frozen_)
        return;
    frozen_ = frozen;
    dattorro_.setFreeze(frozen); lexFigure8_.tank.setFreeze(frozen);
    sixAPTank_.setFreeze (frozen);
    quad_.setFreeze (frozen);
    fdn_.setFreeze(frozen); hallTrueLex16_.tank.setFreeze(frozen);
    spring_.setFreeze (frozen);
    nonLinear_.setFreeze (frozen);
    shimmer_.setFreeze (frozen);
    dattorroVintage_.setFreeze (frozen);
    plate_.setFreeze (frozen);
    foilPlate_.setFreeze (frozen);
    hall_.setFreeze(frozen); hallTrueLex_.tank.setFreeze(frozen);
    hallRing_.setFreeze (frozen);
    hallHybrid_.setFreeze (frozen);
    hallTrueLex16_.setFreeze (frozen);
}

// Forward to the NonLinear engine — it's the only algorithm with a gate.
void DuskVerbEngine::setNonLinearGateEnabled (bool enabled)
{
    nonLinear_.setGateEnabled (enabled);
}

void DuskVerbEngine::setDecayTime (float seconds)
{
    dattorro_.setDecayTime(seconds); lexFigure8_.tank.setDecayTime(seconds);
    sixAPTank_.setDecayTime (seconds);
    quad_.setDecayTime (seconds);
    fdn_.setDecayTime(seconds); hallTrueLex16_.tank.setDecayTime(seconds);
    spring_.setDecayTime (seconds);
    nonLinear_.setDecayTime (seconds);
    shimmer_.setDecayTime (seconds);
    dattorroVintage_.setDecayTime (seconds);
    plate_.setDecayTime (seconds);
    foilPlate_.setDecayTime (seconds);
    hall_.setDecayTime(seconds); hallTrueLex_.tank.setDecayTime(seconds);
    hallRing_.setDecayTime (seconds);
    hallHybrid_.setDecayTime (seconds);
    lexMTDL_.setDecayTime (seconds);
}

void DuskVerbEngine::setLexMTDLFeedbackScale (float scale) { lexMTDL_.setFDNFeedbackScale (scale); }
void DuskVerbEngine::setLexMTDLFeedbackAt   (int idx, float scale) { lexMTDL_.setFDNFeedbackAt  (idx, scale); }
void DuskVerbEngine::setLexMTDLDampingHz    (float hz)    { lexMTDL_.setBandDampingHz   (hz); }
void DuskVerbEngine::setLexMTDLDampingHzAt  (int idx, float hz) { lexMTDL_.setBandDampingHzAt (idx, hz); }
void DuskVerbEngine::setLexMTDLERLevel      (float lin)   { lexMTDL_.setERLevel         (lin); }
void DuskVerbEngine::setLexMTDLERTapGainDbAt (int tapIdx, float db) { lexMTDL_.setERTapGainDbAt (tapIdx, db); }
void DuskVerbEngine::setLexMTDLLateLevel    (float lin)   { lexMTDL_.setLateLevel       (lin); }
void DuskVerbEngine::setLexMTDLLineModDepthMsAt (int idx, float ms) { lexMTDL_.setLineModDepthMsAt (idx, ms); }
void DuskVerbEngine::setLexMTDLSchroederCoeff (float coeff) { lexMTDL_.setSchroederCoeff (coeff); }
void DuskVerbEngine::setLexMTDLTiltDb         (float db)    { lexMTDL_.setTiltDb         (db); }

void DuskVerbEngine::setLexHybridWashLevel    (float lin) { hybridWashLevel_    = std::max (0.0f, lin); }
void DuskVerbEngine::setLexHybridChatterLevel (float lin) { hybridChatterLevel_ = std::max (0.0f, lin); }

void DuskVerbEngine::setSize (float size)
{
    // Target only — pushSizeToTanks() runs once per block at the top of process().
    sizeSmoother_.setTarget (std::clamp (size, 0.0f, 1.0f));
}

void DuskVerbEngine::pushSizeToTanks (float size)
{
    dattorro_.setSize(size); lexFigure8_.tank.setSize(size);
    sixAPTank_.setSize (size);
    quad_.setSize (size);
    fdn_.setSize(size); hallTrueLex16_.tank.setSize(size);
    spring_.setSize (size);
    nonLinear_.setSize (size);
    shimmer_.setSize (size);
    dattorroVintage_.setSize (size);
    plate_.setSize (size);
    foilPlate_.setSize (size);
    hall_.setSize(size); hallTrueLex_.tank.setSize(size);
    hallRing_.setSize (size);
    hallHybrid_.setSize (size);
}

void DuskVerbEngine::setBassMultiply (float mult)
{
    dattorro_.setBassMultiply(mult); lexFigure8_.tank.setBassMultiply(mult);
    sixAPTank_.setBassMultiply (mult);
    quad_.setBassMultiply (mult);
    fdn_.setBassMultiply(mult); hallTrueLex16_.tank.setBassMultiply(mult);
    spring_.setBassMultiply (mult);
    nonLinear_.setBassMultiply (mult);
    shimmer_.setBassMultiply (mult);
    dattorroVintage_.setBassMultiply (mult);
    plate_.setBassMultiply (mult);
    foilPlate_.setBassMultiply (mult);
    hall_.setBassMultiply(mult); hallTrueLex_.tank.setBassMultiply(mult);
}

void DuskVerbEngine::setMidMultiply (float mult)
{
    dattorro_.setMidMultiply(mult); lexFigure8_.tank.setMidMultiply(mult);
    sixAPTank_.setMidMultiply (mult);
    quad_.setMidMultiply (mult);
    fdn_.setMidMultiply(mult); hallTrueLex16_.tank.setMidMultiply(mult);
    spring_.setMidMultiply (mult);
    nonLinear_.setMidMultiply (mult);
    shimmer_.setMidMultiply (mult);
    dattorroVintage_.setMidMultiply (mult);
    plate_.setMidMultiply (mult);
    foilPlate_.setMidMultiply (mult);
    hall_.setMidMultiply(mult); hallTrueLex_.tank.setMidMultiply(mult);
}

void DuskVerbEngine::setTrebleMultiply (float mult)
{
    dattorro_.setTrebleMultiply(mult); lexFigure8_.tank.setTrebleMultiply(mult);
    sixAPTank_.setTrebleMultiply (mult);
    quad_.setTrebleMultiply (mult);
    fdn_.setTrebleMultiply(mult); hallTrueLex16_.tank.setTrebleMultiply(mult);
    spring_.setTrebleMultiply (mult);
    nonLinear_.setTrebleMultiply (mult);
    shimmer_.setTrebleMultiply (mult);
    dattorroVintage_.setTrebleMultiply (mult);
    plate_.setTrebleMultiply (mult);
    foilPlate_.setTrebleMultiply (mult);
    hall_.setTrebleMultiply(mult); hallTrueLex_.tank.setTrebleMultiply(mult);
}

void DuskVerbEngine::setCrossoverFreq (float hz)
{
    dattorro_.setCrossoverFreq(hz); lexFigure8_.tank.setCrossoverFreq(hz);
    sixAPTank_.setCrossoverFreq (hz);
    quad_.setCrossoverFreq (hz);
    fdn_.setCrossoverFreq(hz); hallTrueLex16_.tank.setCrossoverFreq(hz);
    spring_.setCrossoverFreq (hz);
    nonLinear_.setCrossoverFreq (hz);
    shimmer_.setCrossoverFreq (hz);
    dattorroVintage_.setCrossoverFreq (hz);
    plate_.setCrossoverFreq (hz);
    foilPlate_.setCrossoverFreq (hz);
    hall_.setCrossoverFreq(hz); hallTrueLex_.tank.setCrossoverFreq(hz);
}

void DuskVerbEngine::setHighCrossoverFreq (float hz)
{
    dattorro_.setHighCrossoverFreq(hz); lexFigure8_.tank.setHighCrossoverFreq(hz);
    sixAPTank_.setHighCrossoverFreq (hz);
    quad_.setHighCrossoverFreq (hz);
    fdn_.setHighCrossoverFreq(hz); hallTrueLex16_.tank.setHighCrossoverFreq(hz);
    spring_.setHighCrossoverFreq (hz);
    nonLinear_.setHighCrossoverFreq (hz);
    shimmer_.setHighCrossoverFreq (hz);
    dattorroVintage_.setHighCrossoverFreq (hz);
    plate_.setHighCrossoverFreq (hz);
    foilPlate_.setHighCrossoverFreq (hz);
    hall_.setHighCrossoverFreq(hz); hallTrueLex_.tank.setHighCrossoverFreq(hz);
}

void DuskVerbEngine::setSaturation (float amount)
{
    dattorro_.setSaturation(amount); lexFigure8_.tank.setSaturation(amount);
    sixAPTank_.setSaturation (amount);
    quad_.setSaturation (amount);
    fdn_.setSaturation(amount); hallTrueLex16_.tank.setSaturation(amount);
    spring_.setSaturation (amount);
    nonLinear_.setSaturation (amount);
    shimmer_.setSaturation (amount);
    dattorroVintage_.setSaturation (amount);
    plate_.setSaturation (amount);
    foilPlate_.setSaturation (amount);
    hall_.setSaturation(amount); hallTrueLex_.tank.setSaturation(amount);
}

void DuskVerbEngine::setModDepth (float depth)
{
    // ALL engines receive modulation. Even a microscopic LFO depth (5–10 %)
    // is required on QuadTank to break the static phase-locks that produce
    // metallic ringing — the perfectly-deterministic delay reads otherwise
    // line up modal energy into a fixed comb pattern.
    // Spring engine reinterprets this as "SPRING LEN" — read-position LFO
    // depth in samples (0 = static spring, 1 = full ±6 sample drip wobble).
    dattorro_.setModDepth(depth); lexFigure8_.tank.setModDepth(depth);
    sixAPTank_.setModDepth (depth);
    quad_.setModDepth (depth);
    fdn_.setModDepth(depth); hallTrueLex16_.tank.setModDepth(depth);
    spring_.setModDepth (depth);
    nonLinear_.setModDepth (depth);
    shimmer_.setModDepth (depth);   // hijacked → PITCH (0..1 → 0..24 semitones)
    dattorroVintage_.setModDepth (depth);
    plate_.setModDepth (depth);
    foilPlate_.setModDepth (depth);
    hall_.setModDepth(depth); hallTrueLex_.tank.setModDepth(depth);
}

void DuskVerbEngine::setModRate (float hz)
{
    dattorro_.setModRate(hz); lexFigure8_.tank.setModRate(hz);
    sixAPTank_.setModRate (hz);
    quad_.setModRate (hz);
    fdn_.setModRate(hz); hallTrueLex16_.tank.setModRate(hz);
    spring_.setModRate (hz);
    nonLinear_.setModRate (hz);
    shimmer_.setModRate (hz);       // hijacked → FEEDBACK (0.1..10 Hz → 0..0.95 cascade gain)
    dattorroVintage_.setModRate (hz);
    plate_.setModRate (hz);
    foilPlate_.setModRate (hz);
    hall_.setModRate(hz); hallTrueLex_.tank.setModRate(hz);
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
    dattorro_.setTankDiffusion(amount); lexFigure8_.tank.setTankDiffusion(amount);
    sixAPTank_.setTankDiffusion (amount);
    quad_.setTankDiffusion        (amount);
    fdn_.setTankDiffusion(amount); hallTrueLex16_.tank.setTankDiffusion(amount);
    spring_.setTankDiffusion      (amount);
    nonLinear_.setTankDiffusion   (amount);
    shimmer_.setTankDiffusion     (amount);   // no-op (FDN's mix IS the diffusion)
    dattorroVintage_.setTankDiffusion (amount);
    plate_.setTankDiffusion        (amount);
    foilPlate_.setTankDiffusion    (amount);
    hall_.setTankDiffusion(amount); hallTrueLex_.tank.setTankDiffusion(amount);     // no-op in HallReverb
}

void DuskVerbEngine::setBassChokeHz (float hz)
{
    // Only the DattorroVintage engine uses this. Other engines: no-op.
    dattorroVintage_.setBassChokeHz (hz);
}

// ───── HallReverb advanced controls ─────────────────────────────────
// All eight setters forward only to hall_ — they have no effect on the
// other engines (which don't expose per-band damping / gain / inline
// diffusion / output widener as separate knobs). The editor hides their
// UI knobs when the active algorithm isn't "Hall (Lex)" (algo 10).
void DuskVerbEngine::setHallBassDamping     (float coeff) { hall_.setBassDamping(coeff); hallTrueLex_.tank.setBassDamping(coeff); }
void DuskVerbEngine::setHallMidDamping      (float coeff) { hall_.setMidDamping(coeff); hallTrueLex_.tank.setMidDamping(coeff); }
void DuskVerbEngine::setHallTrebleDamping   (float coeff) { hall_.setTrebleDamping(coeff); hallTrueLex_.tank.setTrebleDamping(coeff); }
void DuskVerbEngine::setHallBassGain        (float gain)  { hall_.setBassGain(gain); hallTrueLex_.tank.setBassGain(gain);  }
void DuskVerbEngine::setHallMidGain         (float gain)  { hall_.setMidGain(gain); hallTrueLex_.tank.setMidGain(gain);  }
void DuskVerbEngine::setHallTrebleGain      (float gain)  { hall_.setTrebleGain(gain); hallTrueLex_.tank.setTrebleGain(gain);  }
void DuskVerbEngine::setHallInlineDiffusion (float coeff) { hall_.setInlineDiffusion(coeff); hallTrueLex_.tank.setInlineDiffusion(coeff); }
void DuskVerbEngine::setHallStereoWidth     (float b)     { hall_.setStereoWidth(b); hallTrueLex_.tank.setStereoWidth(b);     }

void DuskVerbEngine::setHallTap0Ms     (float ms) { hall_.setTapTimeMs(0, ms); hallTrueLex_.tank.setTapTimeMs(0, ms); }
void DuskVerbEngine::setHallTap1Ms     (float ms) { hall_.setTapTimeMs(1, ms); hallTrueLex_.tank.setTapTimeMs(1, ms); }
void DuskVerbEngine::setHallTap2Ms     (float ms) { hall_.setTapTimeMs(2, ms); hallTrueLex_.tank.setTapTimeMs(2, ms); }
void DuskVerbEngine::setHallTap3Ms     (float ms) { hall_.setTapTimeMs(3, ms); hallTrueLex_.tank.setTapTimeMs(3, ms); }
void DuskVerbEngine::setHallTap4Ms     (float ms) { hall_.setTapTimeMs(4, ms); hallTrueLex_.tank.setTapTimeMs(4, ms); }
void DuskVerbEngine::setHallTap5Ms     (float ms) { hall_.setTapTimeMs(5, ms); hallTrueLex_.tank.setTapTimeMs(5, ms); }
void DuskVerbEngine::setHallTap0Weight (float w)  { hall_.setTapWeight(0, w); hallTrueLex_.tank.setTapWeight(0, w);  }
void DuskVerbEngine::setHallTap1Weight (float w)  { hall_.setTapWeight(1, w); hallTrueLex_.tank.setTapWeight(1, w);  }
void DuskVerbEngine::setHallTap2Weight (float w)  { hall_.setTapWeight(2, w); hallTrueLex_.tank.setTapWeight(2, w);  }
void DuskVerbEngine::setHallTap3Weight (float w)  { hall_.setTapWeight(3, w); hallTrueLex_.tank.setTapWeight(3, w);  }
void DuskVerbEngine::setHallTap4Weight (float w)  { hall_.setTapWeight(4, w); hallTrueLex_.tank.setTapWeight(4, w);  }
void DuskVerbEngine::setHallTap5Weight (float w)  { hall_.setTapWeight(5, w); hallTrueLex_.tank.setTapWeight(5, w);  }

void DuskVerbEngine::setHallSpec0Ms     (float ms) { hall_.setSpecularTimeMs(0, ms); hallTrueLex_.tank.setSpecularTimeMs(0, ms); }
void DuskVerbEngine::setHallSpec1Ms     (float ms) { hall_.setSpecularTimeMs(1, ms); hallTrueLex_.tank.setSpecularTimeMs(1, ms); }
void DuskVerbEngine::setHallSpec2Ms     (float ms) { hall_.setSpecularTimeMs(2, ms); hallTrueLex_.tank.setSpecularTimeMs(2, ms); }
void DuskVerbEngine::setHallSpec3Ms     (float ms) { hall_.setSpecularTimeMs(3, ms); hallTrueLex_.tank.setSpecularTimeMs(3, ms); }
void DuskVerbEngine::setHallSpec0Weight (float w)  { hall_.setSpecularWeight(0, w); hallTrueLex_.tank.setSpecularWeight(0, w);  }
void DuskVerbEngine::setHallSpec1Weight (float w)  { hall_.setSpecularWeight(1, w); hallTrueLex_.tank.setSpecularWeight(1, w);  }
void DuskVerbEngine::setHallSpec2Weight (float w)  { hall_.setSpecularWeight(2, w); hallTrueLex_.tank.setSpecularWeight(2, w);  }
void DuskVerbEngine::setHallSpec3Weight (float w)  { hall_.setSpecularWeight(3, w); hallTrueLex_.tank.setSpecularWeight(3, w);  }
void DuskVerbEngine::setHallSpecHFCutHz (float hz) { hall_.setSpecularHFCutHz(hz); hallTrueLex_.tank.setSpecularHFCutHz(hz);   }

// P10 per-band peaking EQ. Each gain/Q pair routed to its band's
// setBassEQ/setMidEQ/setTrebleEQ which redesigns the biquad coefs.
// We cache the OTHER axis locally in HallReverb so an isolated gain
// update doesn't reset Q (and vice versa).
namespace {
    // Module-scope shadow caches per HallReverb instance would be ideal,
    // but DuskVerbEngine only owns one hall_ — a single static is OK
    // here. Defaults track HallReverb defaults.
    float sHallBassEQGain = 0.0f, sHallBassEQQ = 0.707f;
    float sHallMidEQGain  = 0.0f, sHallMidEQQ  = 0.707f;
    float sHallTrebleEQGain = 0.0f, sHallTrebleEQQ = 0.707f;
}
void DuskVerbEngine::setHallBassEQGain   (float gainDb) { sHallBassEQGain = gainDb;  hall_.setBassEQ(sHallBassEQGain,   sHallBassEQQ); hallTrueLex_.tank.setBassEQ(sHallBassEQGain,   sHallBassEQQ);   }
void DuskVerbEngine::setHallBassEQQ      (float q)      { sHallBassEQQ    = q;       hall_.setBassEQ(sHallBassEQGain,   sHallBassEQQ); hallTrueLex_.tank.setBassEQ(sHallBassEQGain,   sHallBassEQQ);   }
void DuskVerbEngine::setHallMidEQGain    (float gainDb) { sHallMidEQGain  = gainDb;  hall_.setMidEQ(sHallMidEQGain,    sHallMidEQQ); hallTrueLex_.tank.setMidEQ(sHallMidEQGain,    sHallMidEQQ);    }
void DuskVerbEngine::setHallMidEQQ       (float q)      { sHallMidEQQ     = q;       hall_.setMidEQ(sHallMidEQGain,    sHallMidEQQ); hallTrueLex_.tank.setMidEQ(sHallMidEQGain,    sHallMidEQQ);    }
void DuskVerbEngine::setHallTrebleEQGain (float gainDb) { sHallTrebleEQGain = gainDb; hall_.setTrebleEQ(sHallTrebleEQGain, sHallTrebleEQQ); hallTrueLex_.tank.setTrebleEQ(sHallTrebleEQGain, sHallTrebleEQQ); }
void DuskVerbEngine::setHallTrebleEQQ    (float q)      { sHallTrebleEQQ    = q;     hall_.setTrebleEQ(sHallTrebleEQGain, sHallTrebleEQQ); hallTrueLex_.tank.setTrebleEQ(sHallTrebleEQGain, sHallTrebleEQQ); }
void DuskVerbEngine::setHallBassEQFc     (float hz)     { hall_.setBassEQFc(hz); hallTrueLex_.tank.setBassEQFc(hz); }
void DuskVerbEngine::setHallMidEQFc      (float hz)     { hall_.setMidEQFc(hz); hallTrueLex_.tank.setMidEQFc(hz); }
void DuskVerbEngine::setHallTrebleEQFc   (float hz)     { hall_.setTrebleEQFc(hz); hallTrueLex_.tank.setTrebleEQFc(hz); }
void DuskVerbEngine::setHallBassDampingFc   (float hz)  { hall_.setBassDampingFc(hz); hallTrueLex_.tank.setBassDampingFc(hz); }
void DuskVerbEngine::setHallMidDampingFc    (float hz)  { hall_.setMidDampingFc(hz); hallTrueLex_.tank.setMidDampingFc(hz); }
void DuskVerbEngine::setHallTrebleDampingFc (float hz)  { hall_.setTrebleDampingFc(hz); hallTrueLex_.tank.setTrebleDampingFc(hz); }
void DuskVerbEngine::setHallBassModDepth    (float s)   { hall_.setBassModDepth(s); hallTrueLex_.tank.setBassModDepth(s); }
void DuskVerbEngine::setHallBassModRate     (float hz)  { hall_.setBassModRate(hz); hallTrueLex_.tank.setBassModRate(hz); }
void DuskVerbEngine::setHallMidModDepth     (float s)   { hall_.setMidModDepth(s); hallTrueLex_.tank.setMidModDepth(s); }
void DuskVerbEngine::setHallMidModRate      (float hz)  { hall_.setMidModRate(hz); hallTrueLex_.tank.setMidModRate(hz); }
void DuskVerbEngine::setHallTrebleModDepth  (float s)   { hall_.setTrebleModDepth(s); hallTrueLex_.tank.setTrebleModDepth(s); }
void DuskVerbEngine::setHallTrebleModRate   (float hz)  { hall_.setTrebleModRate(hz); hallTrueLex_.tank.setTrebleModRate(hz); }
void DuskVerbEngine::setHallBassModShape    (float sh)  { hall_.setBassModShape(sh); hallTrueLex_.tank.setBassModShape(sh); }
void DuskVerbEngine::setHallMidModShape     (float sh)  { hall_.setMidModShape(sh); hallTrueLex_.tank.setMidModShape(sh); }
void DuskVerbEngine::setHallTrebleModShape  (float sh)  { hall_.setTrebleModShape(sh); hallTrueLex_.tank.setTrebleModShape(sh); }
void DuskVerbEngine::setHallBassChannelGainSpread   (float s) { hall_.setBassChannelGainSpread(s); hallTrueLex_.tank.setBassChannelGainSpread(s); }
void DuskVerbEngine::setHallMidChannelGainSpread    (float s) { hall_.setMidChannelGainSpread(s); hallTrueLex_.tank.setMidChannelGainSpread(s); }
void DuskVerbEngine::setHallTrebleChannelGainSpread (float s) { hall_.setTrebleChannelGainSpread(s); hallTrueLex_.tank.setTrebleChannelGainSpread(s); }
void DuskVerbEngine::setHallBassShelfGain   (float dB) { hall_.setBassShelfGain(dB); hallTrueLex_.tank.setBassShelfGain(dB); }
void DuskVerbEngine::setHallBassShelfFc     (float hz) { hall_.setBassShelfFc(hz); hallTrueLex_.tank.setBassShelfFc(hz); }
void DuskVerbEngine::setHallMidShelfGain    (float dB) { hall_.setMidShelfGain(dB); hallTrueLex_.tank.setMidShelfGain(dB); }
void DuskVerbEngine::setHallMidShelfFc      (float hz) { hall_.setMidShelfFc(hz); hallTrueLex_.tank.setMidShelfFc(hz); }
void DuskVerbEngine::setHallTrebleShelfGain (float dB) { hall_.setTrebleShelfGain(dB); hallTrueLex_.tank.setTrebleShelfGain(dB); }
void DuskVerbEngine::setHallTrebleShelfFc   (float hz) { hall_.setTrebleShelfFc(hz); hallTrueLex_.tank.setTrebleShelfFc(hz); }
void DuskVerbEngine::setHallInputDiffusion  (float g)  { hall_.setInputDiffusion(g); hallTrueLex_.tank.setInputDiffusion(g); }

void DuskVerbEngine::setRingDecayTime   (float s)   { hallRing_.setDecayTime    (s); }
void DuskVerbEngine::setRingSize        (float sc)  { hallRing_.setSize         (sc); }
void DuskVerbEngine::setRingDamping     (float c)   { hallRing_.setDamping      (c); }
void DuskVerbEngine::setRingDampingFc   (float hz)  { hallRing_.setDampingFc    (hz); }
void DuskVerbEngine::setRingSpread      (float m)   { hallRing_.setSpread       (m); }
void DuskVerbEngine::setRingShape       (float c)   { hallRing_.setShape        (c); }
void DuskVerbEngine::setRingSpin        (float hz)  { hallRing_.setSpin         (hz); }
void DuskVerbEngine::setRingWander      (float s)   { hallRing_.setWander       (s); }
void DuskVerbEngine::setRingStereoWidth (float w)   { hallRing_.setStereoWidth  (w); }

void DuskVerbEngine::setHybridERTapWeight (int idx, float w) { hallHybrid_.setERTapWeight (idx, w); }
void DuskVerbEngine::setHybridERLevel     (float l)          { hallHybrid_.setERLevel     (l); }
void DuskVerbEngine::setHybridRingLevel   (float l)          { hallHybrid_.setRingLevel   (l); }
void DuskVerbEngine::setHybridLowShelf    (float g, float f) { hallHybrid_.setLowShelf    (g, f); }
void DuskVerbEngine::setHybridHighShelf   (float g, float f) { hallHybrid_.setHighShelf   (g, f); }
void DuskVerbEngine::setHybridRingDamping     (float c)  { hallHybrid_.setRingDamping     (c); }
void DuskVerbEngine::setHybridRingDampingFc   (float hz) { hallHybrid_.setRingDampingFc   (hz); }
void DuskVerbEngine::setHybridRingSpread      (float m)  { hallHybrid_.setRingSpread      (m); }
void DuskVerbEngine::setHybridRingShape       (float c)  { hallHybrid_.setRingShape       (c); }
void DuskVerbEngine::setHybridRingSpin        (float hz) { hallHybrid_.setRingSpin        (hz); }
void DuskVerbEngine::setHybridRingWander      (float s)  { hallHybrid_.setRingWander      (s); }
void DuskVerbEngine::setHybridRingStereoWidth (float w)  { hallHybrid_.setRingStereoWidth (w); }

void DuskVerbEngine::setTrueLexERTapWeight (int idx, float w) { hallTrueLex_.setERWeight  (idx, w); }
void DuskVerbEngine::setTrueLexERLevel     (float l)          { hallTrueLex_.setERLevel   (l); }
void DuskVerbEngine::setTrueLexTankLevel   (float l)          { hallTrueLex_.setTankLevel (l); }
void DuskVerbEngine::setTrueLexAPCoeff     (float g)          { hallTrueLex_.setAPCoeff   (g); }

void DuskVerbEngine::setTrueLex16ERTapWeight (int idx, float w) { hallTrueLex16_.setERWeight  (idx, w); }
void DuskVerbEngine::setTrueLex16ERLevel     (float l)          { hallTrueLex16_.setERLevel   (l); }
void DuskVerbEngine::setTrueLex16TankLevel   (float l)          { hallTrueLex16_.setTankLevel (l); }
void DuskVerbEngine::setTrueLex16APCoeff     (float g)          { hallTrueLex16_.setAPCoeff   (g); }

void DuskVerbEngine::setLexFig8StructuralHFDamping (float hz) { lexFigure8_.setStructuralHFDamping (hz); }
void DuskVerbEngine::setLexFig8ERTapDelay     (int idx, float ms) { lexFigure8_.setERTapDelay (idx, ms); }
void DuskVerbEngine::setLexFig8ERTapGainDb    (int idx, float db) { lexFigure8_.setERTapGainDb (idx, db); }
void DuskVerbEngine::setLexFig8ERStereoOffset (float ms)          { lexFigure8_.setERStereoOffset (ms); }
void DuskVerbEngine::setLexFig8TankAtten      (float scale)       { lexFigure8_.setTankAtten (scale); }
void DuskVerbEngine::setLexFig8TankInputScale (float scale)       { lexFigure8_.setTankInputScale (scale); }
void DuskVerbEngine::setLexFig8TankPreDelay   (float ms)          { lexFigure8_.setTankPreDelay (ms); }
void DuskVerbEngine::setLexFig8DensityJitterDepth (float frac)    { lexFigure8_.setDensityJitterDepth (frac); }
void DuskVerbEngine::setLexFig8DensityJitterRate  (float hz)      { lexFigure8_.setDensityJitterRate  (hz); }
void DuskVerbEngine::setLexFig8SubBassMultiply  (float mult)      { lexFigure8_.setSubBassMultiply  (mult); }
void DuskVerbEngine::setLexFig8SubBassCrossover (float hz)        { lexFigure8_.setSubBassCrossover (hz); }
void DuskVerbEngine::setLexFig8StructuralTilt   (float db)        { lexFigure8_.setStructuralTilt (db); }
void DuskVerbEngine::setLexFig8AirMultiply      (float mult)      { lexFigure8_.setAirMultiply  (mult); }
void DuskVerbEngine::setLexFig8AirCrossover     (float hz)        { lexFigure8_.setAirCrossover (hz); }
void DuskVerbEngine::setLexFig8BandMultiply     (int idx, float mult) { lexFigure8_.setBandMultiply (idx, mult); }
void DuskVerbEngine::setLexFig8DensityAPDelayMs (int stageIdx, float ms) { lexFigure8_.setDensityAPDelayMs (stageIdx, ms); }
void DuskVerbEngine::setLexFig8OutputTapFraction (int channel, int tapIdx, float frac) { lexFigure8_.setOutputTapFraction (channel, tapIdx, frac); }
void DuskVerbEngine::setLexFig8DelayBaseMs       (int channel, int delayIdx, float ms) { lexFigure8_.setDelayBaseMs       (channel, delayIdx, ms); }
void DuskVerbEngine::setLexFig8APBaseMs          (int channel, int apIdx, float ms)    { lexFigure8_.setAPBaseMs          (channel, apIdx, ms); }
void DuskVerbEngine::setLexFig8CrossFeedCoeff    (int channel, float coeff)            { lexFigure8_.setCrossFeedCoeff    (channel, coeff); }
void DuskVerbEngine::setLexFig8BypassDiffuser    (bool bypass)                         { lexFig8BypassDiffuser_ = bypass; }
void DuskVerbEngine::setLexFig8DuckerThreshold  (float thresh)    { lexFigure8_.setDuckerThreshold (thresh); }
void DuskVerbEngine::setLexFig8DuckerAttackMs   (float ms)        { lexFigure8_.setDuckerAttackMs  (ms); }
void DuskVerbEngine::setLexFig8DuckerReleaseMs  (float ms)        { lexFigure8_.setDuckerReleaseMs (ms); }
void DuskVerbEngine::setLexFig8DuckerDepth      (float depth)     { lexFigure8_.setDuckerDepth     (depth); }

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
void DuskVerbEngine::setSixAPEarlyHighpassHz (float v) { sixAPTank_.setEarlyHighpassHz (v); }

void DuskVerbEngine::setFirstReflLDelayMs (float ms) { firstRefl_.setLeftDelayMs  (std::clamp (ms, 1.0f, 50.0f)); }
void DuskVerbEngine::setFirstReflRDelayMs (float ms) { firstRefl_.setRightDelayMs (std::clamp (ms, 1.0f, 50.0f)); }
void DuskVerbEngine::setFirstReflLGainDb  (float db) { firstRefl_.setLeftGainDb   (std::clamp (db, -60.0f, 6.0f)); }
void DuskVerbEngine::setFirstReflRGainDb  (float db) { firstRefl_.setRightGainDb  (std::clamp (db, -60.0f, 6.0f)); }
void DuskVerbEngine::setFirstReflHFCutHz  (float hz) { firstRefl_.setHFCutHz      (std::clamp (hz, 100.0f, 20000.0f)); }

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
    // RBJ 2nd-order Butterworth low-pass.
    float fc = std::clamp (hz, 1000.0f, 0.49f * static_cast<float> (sampleRate_));
    float w0 = kTwoPi * fc / static_cast<float> (sampleRate_);
    float cosw = std::cos (w0);
    float sinw = std::sin (w0);
    float alpha = sinw / (2.0f * 0.7071067811865475f);
    float a0 = 1.0f + alpha;

    hiCutFilter_.b0 = (1.0f - cosw) * 0.5f / a0;
    hiCutFilter_.b1 = (1.0f - cosw) / a0;
    hiCutFilter_.b2 = (1.0f - cosw) * 0.5f / a0;
    hiCutFilter_.a1 = -2.0f * cosw / a0;
    hiCutFilter_.a2 = (1.0f - alpha) / a0;
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
            // P8c — HallReverb owns its own wet Hi Cut so its specular path
            // stays outside this filter. Pushing the same value here keeps
            // the wet-tail timbre identical to other engines' post-tank
            // Hi Cut response. The engine-level hiCutFilter_ is bypassed
            // for the Hall engine below.
            hall_.setWetHiCutHz(hiCutNow); hallTrueLex_.tank.setWetHiCutHz(hiCutNow);
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

    // ---- 1b) Specular first-reflection injector (sparse 2-tap) ----
    // Reads the RAW input (pre-predelay) so the specular taps arrive at
    // their configured ms timings absolutely — independent of the user's
    // predelay knob. Matches Lex PCM Native behaviour: L_Rfl/R_Rfl fire at
    // their delay times measured from t=0, regardless of Predelay setting.
    // Output buffered for step-5 sum. Default gains = 0 (muted) for any
    // preset that doesn't opt in.
    std::memset (firstReflOutL_.data(), 0, sizeof (float) * static_cast<size_t> (numSamples));
    std::memset (firstReflOutR_.data(), 0, sizeof (float) * static_cast<size_t> (numSamples));
    firstRefl_.processAdd (left, right,
                            firstReflOutL_.data(), firstReflOutR_.data(), numSamples);

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
    const bool lexFig8BypassActive = (currentEngine_ == EngineType::LexFigure8
                                      && lexFig8BypassDiffuser_);
    if (currentEngine_ != EngineType::SixAPTank
        && currentEngine_ != EngineType::Spring
        && currentEngine_ != EngineType::NonLinear
        && currentEngine_ != EngineType::DattorroVintage
        && currentEngine_ != EngineType::Plate
        && currentEngine_ != EngineType::FoilPlate
        && currentEngine_ != EngineType::Hall          // Hall owns multi-tap injection + LR4 split internally
        && ! lexFig8BypassActive)                      // v31 — LexFigure8 may bypass
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
        case EngineType::Plate:
            plate_.process (tankInL_.data(), tankInR_.data(),
                            tankOutL_.data(), tankOutR_.data(), numSamples);
            break;
        case EngineType::FoilPlate:
            foilPlate_.process (tankInL_.data(), tankInR_.data(),
                                tankOutL_.data(), tankOutR_.data(), numSamples);
            break;
        case EngineType::Hall:
            hall_.process (tankInL_.data(), tankInR_.data(),
                           tankOutL_.data(), tankOutR_.data(), numSamples);
            break;
        case EngineType::HallRing:
            hallRing_.process (tankInL_.data(), tankInR_.data(),
                               tankOutL_.data(), tankOutR_.data(), numSamples);
            break;
        case EngineType::HallHybrid:
            hallHybrid_.process (tankInL_.data(), tankInR_.data(),
                                 tankOutL_.data(), tankOutR_.data(), numSamples);
            break;
        case EngineType::HallTrueLex:
            hallTrueLex_.process (tankInL_.data(), tankInR_.data(),
                                  tankOutL_.data(), tankOutR_.data(), numSamples);
            break;
        case EngineType::HallTrueLex16:
            hallTrueLex16_.process (tankInL_.data(), tankInR_.data(),
                                    tankOutL_.data(), tankOutR_.data(), numSamples);
            break;
        case EngineType::LexFigure8:
            // Pass RAW input (left, right) for ER + filtered (tankInL_/R) for tank.
            // ER captures raw audio bypassing pre-delay + input AP cascade.
            // Tank still gets fully processed signal.
            lexFigure8_.process (left, right,
                                 tankInL_.data(), tankInR_.data(),
                                 tankOutL_.data(), tankOutR_.data(), numSamples);
            break;
        case EngineType::LexMTDL:
            // Stage-1 FIR ER reads RAW (left/right) for clean specular taps.
            // Stage-2 FDN does its own input distribution + Hadamard mix internally,
            // so we feed the dry signal directly. Output goes to tankOutL_/R_ and
            // flows through the shared shell pipeline (gain trim + lo/hi cut + width).
            lexMTDL_.process (left, right,
                              tankOutL_.data(), tankOutR_.data(), numSamples);
            break;
        case EngineType::LexHybrid:
        {
            // v38b — time-multiplex synthesis. Both engines run in
            // parallel on full input (delay lines fully primed). Gate
            // switches OUTPUT between them by sample index:
            //   t < 90 ms  → 100 % Engine 15 (peak_locations + spectral wash)
            //   90..110 ms → equal-power crossfade
            //   t ≥ 110 ms → 100 % Engine 16 (tdc chatter post-100ms)
            // Counter resets on |x| > 0.95 after silence so impulse and
            // noiseburst renders both start at t=0 cleanly.
            lexFigure8_.process (left, right,
                                 tankInL_.data(), tankInR_.data(),
                                 tankOutL_.data(), tankOutR_.data(), numSamples);
            lexMTDL_.process (left, right,
                              hybridMtdlOutL_.data(), hybridMtdlOutR_.data(), numSamples);

            const float gateStart = static_cast<float> (0.090 * sampleRate_);  // 90 ms
            const float gateEnd   = static_cast<float> (0.110 * sampleRate_);  // 110 ms
            const float gateRange = gateEnd - gateStart;
            const float halfPi    = 1.5707963f;

            for (int i = 0; i < numSamples; ++i)
            {
                const float absIn = std::max (std::abs (left[i]), std::abs (right[i]));
                if (absIn > 0.95f && hybridPrevAbsIn_ < 0.01f)
                    hybridSampleCount_ = 0;
                hybridPrevAbsIn_ = absIn;

                const float t = static_cast<float> (hybridSampleCount_);
                float wash, chatter;
                if (t < gateStart)        { wash = 1.0f;                 chatter = 0.0f; }
                else if (t < gateEnd)
                {
                    const float x = (t - gateStart) / gateRange;
                    const float a = x * halfPi;
                    wash    = std::cos (a);
                    chatter = std::sin (a);
                }
                else                       { wash = 0.0f;                 chatter = 1.0f; }

                const float wMul = wash    * hybridWashLevel_;
                const float cMul = chatter * hybridChatterLevel_;
                tankOutL_[(size_t) i] = wMul * tankOutL_[(size_t) i] + cMul * hybridMtdlOutL_[(size_t) i];
                tankOutR_[(size_t) i] = wMul * tankOutR_[(size_t) i] + cMul * hybridMtdlOutR_[(size_t) i];
                ++hybridSampleCount_;
            }
            break;
        }
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
    const bool useSmoothER = (currentEngine_ != EngineType::DattorroVintage);
    for (int i = 0; i < numSamples; ++i)
    {
        const float erL   = useSmoothER ? erOutL_[static_cast<size_t> (i)] : 0.0f;
        const float erR   = useSmoothER ? erOutR_[static_cast<size_t> (i)] : 0.0f;
        const float lateL = tankOutL_[static_cast<size_t> (i)];
        const float lateR = tankOutR_[static_cast<size_t> (i)];
        // FirstReflections specular taps — pre-summed during step 1b. Zero
        // when both per-channel gains are 0, so this is a no-op cost for
        // presets that don't opt in.
        const float frL   = firstReflOutL_[static_cast<size_t> (i)];
        const float frR   = firstReflOutR_[static_cast<size_t> (i)];

        const float erLevel = erLevelSmoother_.next();

        float wetL = erL * erLevel + lateL + frL;
        float wetR = erR * erLevel + lateR + frR;

        wetL = loCutFilter_.processL (wetL);
        wetR = loCutFilter_.processR (wetR);
        // P8c — Hall engine owns its own wet Hi Cut (applied inside
        // HallReverb's process() before specular mix), so the engine-level
        // hi-cut filter is bypassed here for Hall. Specular taps then
        // route to output past the global Hi Cut, fixing the gain-staging
        // bottleneck that forced spec weights to crank to 6-8.
        if (currentEngine_ != EngineType::Hall)
        {
            wetL = hiCutFilter_.processL (wetL);
            wetR = hiCutFilter_.processR (wetR);
        }

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
            wetL = monoLow + highL;
            wetR = monoLow + highR;
        }

        const float width = widthSmoother_.next();
        const float mid   = 0.5f * (wetL + wetR);
        const float side  = 0.5f * (wetL - wetR);
        wetL = mid + side * width;
        wetR = mid - side * width;

        // gain_trim is a WET-PATH gain — baked into the engine's wet output so
        // each preset's calibrated wet level is preserved through the
        // processor-side dry/wet crossfade (the dry signal is applied AFTER
        // the engine, so trim never touches it).
        const float trim = gainTrimSmoother_.next();
        left[i]  = wetL * trim;
        right[i] = wetR * trim;
    }

    // ---- 6) LexFigure8 ER — added AFTER all shell processing ----
    // The ER TDL inside LexFigure8 captures RAW input (pre-delay free,
    // input-AP-cascade free). Adding it here, after lo_cut/hi_cut/M-S
    // width filters and gain_trim, keeps ER taps as pristine impulses.
    // ER is NOT multiplied by gain_trim because each ER tap's gain
    // setting already specifies absolute output amplitude (vs trim's
    // role of compensating tank's internal attenuation). Apply trim
    // to ER would over-energize the early window.
    if (currentEngine_ == EngineType::LexFigure8)
    {
        const float* erL = lexFigure8_.getERBufferL();
        const float* erR = lexFigure8_.getERBufferR();
        for (int i = 0; i < numSamples; ++i)
        {
            left[i]  += erL[i];
            right[i] += erR[i];
        }
    }
    else if (currentEngine_ == EngineType::LexHybrid)
    {
        // v38 — Engine 15's ER buffer scaled by wash_level for Hybrid.
        // Engine 16's ER is already inside its mix-summed output.
        const float* erL = lexFigure8_.getERBufferL();
        const float* erR = lexFigure8_.getERBufferR();
        const float wash = hybridWashLevel_;
        for (int i = 0; i < numSamples; ++i)
        {
            left[i]  += wash * erL[i];
            right[i] += wash * erR[i];
        }
    }

    // v38c — Analog noise floor exploit. -85 dBFS continuous stereo
    // white noise injected to emulate PCM-era ADC/DAC dither. Lifts
    // the RMS denominator across the silence pad past T60, closing
    // the time_domain_crest gap that pure-digital silence creates.
    if (analogNoiseFloor_ > 0.0f)
    {
        const float amp = analogNoiseFloor_;
        for (int i = 0; i < numSamples; ++i)
        {
            uint32_t s = analogNoiseRngL_;
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            analogNoiseRngL_ = s;
            const float nL = (static_cast<float> (s & 0xFFFFu) * (1.0f / 32768.0f) - 1.0f) * amp;
            s = analogNoiseRngR_;
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            analogNoiseRngR_ = s;
            const float nR = (static_cast<float> (s & 0xFFFFu) * (1.0f / 32768.0f) - 1.0f) * amp;
            left[i]  += nL;
            right[i] += nR;
        }
    }
}
