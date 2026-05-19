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
    spring_.prepare (sampleRate, maxBlockSize);
    nonLinear_.prepare (sampleRate, maxBlockSize);
    shimmer_.prepare (sampleRate, maxBlockSize);
    dattorroVintage_.prepare (sampleRate, maxBlockSize);
    plate_.prepare (sampleRate, maxBlockSize);
    foilPlate_.prepare (sampleRate, maxBlockSize);

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
    dattorro_ .clearBuffers();
    sixAPTank_.clearBuffers();
    quad_     .clearBuffers();
    fdn_      .clearBuffers();
    spring_   .clearBuffers();
    nonLinear_.clearBuffers();
    shimmer_  .clearBuffers();
    dattorroVintage_.clearBuffers();
    plate_.clearBuffers();
    foilPlate_.clearBuffers();

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

    dattorro_.clearBuffers();
    sixAPTank_.clearBuffers();
    quad_.clearBuffers();
    fdn_.clearBuffers();
    spring_.clearBuffers();
    nonLinear_.clearBuffers();
    shimmer_.clearBuffers();
    dattorroVintage_.clearBuffers();
    plate_.clearBuffers();
    foilPlate_.clearBuffers();
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
    spring_.setFreeze (frozen);
    nonLinear_.setFreeze (frozen);
    shimmer_.setFreeze (frozen);
    dattorroVintage_.setFreeze (frozen);
    plate_.setFreeze (frozen);
    foilPlate_.setFreeze (frozen);
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
    spring_.setDecayTime (seconds);
    nonLinear_.setDecayTime (seconds);
    shimmer_.setDecayTime (seconds);
    dattorroVintage_.setDecayTime (seconds);
    plate_.setDecayTime (seconds);
    foilPlate_.setDecayTime (seconds);
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
    spring_.setSize (size);
    nonLinear_.setSize (size);
    shimmer_.setSize (size);
    dattorroVintage_.setSize (size);
    plate_.setSize (size);
    foilPlate_.setSize (size);
}

void DuskVerbEngine::setBassMultiply (float mult)
{
    dattorro_.setBassMultiply (mult);
    sixAPTank_.setBassMultiply (mult);
    quad_.setBassMultiply (mult);
    fdn_.setBassMultiply (mult);
    spring_.setBassMultiply (mult);
    nonLinear_.setBassMultiply (mult);
    shimmer_.setBassMultiply (mult);
    dattorroVintage_.setBassMultiply (mult);
    plate_.setBassMultiply (mult);
    foilPlate_.setBassMultiply (mult);
}

void DuskVerbEngine::setMidMultiply (float mult)
{
    dattorro_.setMidMultiply (mult);
    sixAPTank_.setMidMultiply (mult);
    quad_.setMidMultiply (mult);
    fdn_.setMidMultiply (mult);
    spring_.setMidMultiply (mult);
    nonLinear_.setMidMultiply (mult);
    shimmer_.setMidMultiply (mult);
    dattorroVintage_.setMidMultiply (mult);
    plate_.setMidMultiply (mult);
    foilPlate_.setMidMultiply (mult);
}

void DuskVerbEngine::setTrebleMultiply (float mult)
{
    dattorro_.setTrebleMultiply (mult);
    sixAPTank_.setTrebleMultiply (mult);
    quad_.setTrebleMultiply (mult);
    fdn_.setTrebleMultiply (mult);
    spring_.setTrebleMultiply (mult);
    nonLinear_.setTrebleMultiply (mult);
    shimmer_.setTrebleMultiply (mult);
    dattorroVintage_.setTrebleMultiply (mult);
    plate_.setTrebleMultiply (mult);
    foilPlate_.setTrebleMultiply (mult);
}

void DuskVerbEngine::setCrossoverFreq (float hz)
{
    dattorro_.setCrossoverFreq (hz);
    sixAPTank_.setCrossoverFreq (hz);
    quad_.setCrossoverFreq (hz);
    fdn_.setCrossoverFreq (hz);
    spring_.setCrossoverFreq (hz);
    nonLinear_.setCrossoverFreq (hz);
    shimmer_.setCrossoverFreq (hz);
    dattorroVintage_.setCrossoverFreq (hz);
    plate_.setCrossoverFreq (hz);
    foilPlate_.setCrossoverFreq (hz);
}

void DuskVerbEngine::setHighCrossoverFreq (float hz)
{
    dattorro_.setHighCrossoverFreq (hz);
    sixAPTank_.setHighCrossoverFreq (hz);
    quad_.setHighCrossoverFreq (hz);
    fdn_.setHighCrossoverFreq (hz);
    spring_.setHighCrossoverFreq (hz);
    nonLinear_.setHighCrossoverFreq (hz);
    shimmer_.setHighCrossoverFreq (hz);
    dattorroVintage_.setHighCrossoverFreq (hz);
    plate_.setHighCrossoverFreq (hz);
    foilPlate_.setHighCrossoverFreq (hz);
}

void DuskVerbEngine::setSaturation (float amount)
{
    dattorro_.setSaturation (amount);
    sixAPTank_.setSaturation (amount);
    quad_.setSaturation (amount);
    fdn_.setSaturation (amount);
    spring_.setSaturation (amount);
    nonLinear_.setSaturation (amount);
    shimmer_.setSaturation (amount);
    dattorroVintage_.setSaturation (amount);
    plate_.setSaturation (amount);
    foilPlate_.setSaturation (amount);
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
    spring_.setModDepth (depth);
    nonLinear_.setModDepth (depth);
    shimmer_.setModDepth (depth);   // hijacked → PITCH (0..1 → 0..24 semitones)
    dattorroVintage_.setModDepth (depth);
    plate_.setModDepth (depth);
    foilPlate_.setModDepth (depth);
}

void DuskVerbEngine::setModRate (float hz)
{
    dattorro_.setModRate (hz);
    sixAPTank_.setModRate (hz);
    quad_.setModRate (hz);
    fdn_.setModRate (hz);
    spring_.setModRate (hz);
    nonLinear_.setModRate (hz);
    shimmer_.setModRate (hz);       // hijacked → FEEDBACK (0.1..10 Hz → 0..0.95 cascade gain)
    dattorroVintage_.setModRate (hz);
    plate_.setModRate (hz);
    foilPlate_.setModRate (hz);
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
    spring_.setTankDiffusion      (amount);
    nonLinear_.setTankDiffusion   (amount);
    shimmer_.setTankDiffusion     (amount);   // no-op (FDN's mix IS the diffusion)
    dattorroVintage_.setTankDiffusion (amount);
    plate_.setTankDiffusion        (amount);
    foilPlate_.setTankDiffusion    (amount);
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
    if (currentEngine_ != EngineType::SixAPTank
        && currentEngine_ != EngineType::Spring
        && currentEngine_ != EngineType::NonLinear
        && currentEngine_ != EngineType::DattorroVintage
        && currentEngine_ != EngineType::Plate)
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
        wetL = hiCutFilter_.processL (wetL);
        wetR = hiCutFilter_.processR (wetR);

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
}
