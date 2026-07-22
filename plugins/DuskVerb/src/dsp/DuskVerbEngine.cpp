#include "DuskVerbEngine.h"
#include "DspUtils.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
    constexpr float kTwoPi = 6.283185307179586f;
}

void DuskVerbEngine::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;

    // SR-dependent shell coeffs are derived from stored corner freqs — recompute
    // here so a sample-rate change can't leave them stale even if the caller
    // doesn't re-install the preset config afterward (the sub-engines below
    // already recompute their own coeffs in prepare()).
    recomputeTankFeedCoeffs();
    setTankSplitHz (tankSplitHz_);

    // All engines stay prepared so setAlgorithm() never has to allocate.
    dattorro_.prepare (sampleRate, maxBlockSize);
    dattorroDenseField_.prepare (sampleRate);
    dynLowMid_.prepare (sampleRate);
    sixAPTank_.prepare (sampleRate, maxBlockSize);
    quad_.prepare (sampleRate, maxBlockSize);
    fdn_.prepare (sampleRate, maxBlockSize); accurateHall_.prepare (sampleRate, maxBlockSize);
    sparseField_.prepare (sampleRate, maxBlockSize);
    erDuck_.prepare (sampleRate);
    etapDuck_.prepare (sampleRate);
    tankDuck_.prepare (sampleRate);
    // Wet-input sustained duck: re-derive coeffs at the new sample rate + clear state.
    wetSusLimMidCfg_.updateCoeffs (sampleRate);
    wetSusLimLowCfg_.updateCoeffs (sampleRate);
    wetSusLimMidCutL_.design (wetSusLimMidCfg_.loHz, wetSusLimMidCfg_.hiHz, wetSusLimMidCfg_.maxCutDb, sampleRate);
    wetSusLimMidCutR_.design (wetSusLimMidCfg_.loHz, wetSusLimMidCfg_.hiHz, wetSusLimMidCfg_.maxCutDb, sampleRate);
    wetSusLimLowCutL_.design (wetSusLimLowCfg_.loHz, wetSusLimLowCfg_.hiHz, wetSusLimLowCfg_.maxCutDb, sampleRate);
    wetSusLimLowCutR_.design (wetSusLimLowCfg_.loHz, wetSusLimLowCfg_.hiHz, wetSusLimLowCfg_.maxCutDb, sampleRate);
    wetSusLimMidDet_.clear();  wetSusLimLowDet_.clear();
    wetSusLimKey_.prepare (sampleRate);
    diffuseER_.prepare (sampleRate, maxBlockSize);
    denseHall_.prepare (sampleRate, maxBlockSize);
    pmb_.prepare (sampleRate, maxBlockSize);
    buildupDiffuser_.prepare (sampleRate, maxBlockSize);
    buildupBufL_.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    buildupBufR_.assign (static_cast<size_t> (maxBlockSize), 0.0f);

    outputDiffusion_.prepare (sampleRate, maxBlockSize);
    matchEQL_.prepare (static_cast<float> (sampleRate));
    matchEQR_.prepare (static_cast<float> (sampleRate));
    designMatchEQ();   // redesign match-EQ coeffs from stored gains at the new sample rate
    setDiffuseERHighpass (diffuseERHpHz_);   // re-derive the LR4 corner (dfHpB_/dfHpA_) for the new sample rate
    {
        const int maxOnset = static_cast<int> (0.20 * sampleRate) + 8;   // up to 200 ms tail delay
        tankOnsetBufL_.assign (static_cast<size_t> (maxOnset), 0.0f);
        tankOnsetBufR_.assign (static_cast<size_t> (maxOnset), 0.0f);
        tankOnsetWrite_ = 0;
        recomputeTankOnsetSamples();   // recompute from stored ms at the new sample rate / buffer size
    }
    multibandFdn_.prepare (sampleRate, maxBlockSize);
    multibandFdn_.setCrossovers (300.0f, 5000.0f);
    spring_.prepare (sampleRate, maxBlockSize);
    nonLinear_.prepare (sampleRate, maxBlockSize);
    shimmer_.prepare (sampleRate, maxBlockSize);
    dattorroVintage_.prepare (sampleRate, maxBlockSize);

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
    sourceSide_.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    tankOutL_.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    tankOutR_.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    erOutL_.assign   (static_cast<size_t> (maxBlockSize), 0.0f);
    erOutR_.assign   (static_cast<size_t> (maxBlockSize), 0.0f);
    sparseOutL_.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    sparseOutR_.assign (static_cast<size_t> (maxBlockSize), 0.0f);

    // FORK A reflection tap — sized for the 300 ms max tap time accepted by
    // setEarlyTapBank()/setReflection (power of two).
    {
        const int wantLen = static_cast<int> (0.30 * sampleRate) + 4;
        int len = 1;
        while (len < wantLen) len <<= 1;
        reflBuf_.assign (static_cast<size_t> (len), 0.0f);
        reflDryMono_.assign (static_cast<size_t> (maxBlockSize), 0.0f);
        reflMask_ = len - 1;
        reflWritePos_ = 0;
        reflLpStateL_ = reflLpStateR_ = 0.0f;
        // one-pole LP ~11 kHz — a SHARP/bright reflection (a real early reflection
        // rolls off only the very top, not at 6 kHz which read dark/cloudy). Fed the
        // CLEAN pre-diffuser dry (reflDryMono_) so the tap is a defined discrete
        // arrival, not a smeared blob.
        const float fc = 11000.0f;
        reflLpCoeff_ = 1.0f - std::exp (-kTwoPi * fc / static_cast<float> (sampleRate));
        // Early-tap bank shares this buffer — re-realize its sample delays at the
        // new rate and clear its filter state.
        etapLpStateL_ = etapLpStateR_ = 0.0f;
        etapLpCoeff_  = 1.0f - std::exp (-kTwoPi * etapLpFcHz_ / static_cast<float> (sampleRate));
        if (etapCount_ > 0)
        {
            float ms[kMaxEarlyTaps], g[kMaxEarlyTaps];
            const int n = etapCount_;
            for (int i = 0; i < n; ++i) { ms[i] = etapMs_[i]; g[i] = etapGain_[i]; }
            setEarlyTapBank (ms, g, n, etapLpFcHz_);
        }
    }

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

    // Post-tank stereo image steer (issue #123): dry-balance follower +
    // applied-gain smoother one-pole coeffs. exp(-1/(tau·sr)): larger tau →
    // coeff nearer 1 → slower. State resets to unity gain / zero envelope. These
    // writes touch ONLY the new members, so the disabled path is unaffected.
    psAtkCoeff_  = std::exp (-1.0f / (0.080f * static_cast<float> (sampleRate)));   // 80 ms attack (softens the L<->R balance flip in the alternating-source case)
    psRelCoeff_  = std::exp (-1.0f / (0.350f * static_cast<float> (sampleRate)));   // 350 ms release (tail "remembers" the source side, then centers)
    psGainCoeff_ = std::exp (-1.0f / (0.020f * static_cast<float> (sampleRate)));   // 20 ms applied-gain smoother: click-free (a bounded one-pole, no zipper) yet fast enough that a single hard-pan reaches full tilt inside the measured tail window (preserves the ILD table)
    psEnvL_ = psEnvR_ = 0.0f;
    psGainL_ = psGainR_ = 1.0f;
    psProfileGainCoeff_ = std::exp (-1.0f / (0.002f * static_cast<float> (sampleRate)));
    psBandLp1Coeff_ = std::exp (-kTwoPi * 300.0f / static_cast<float> (sampleRate));
    psBandLp2Coeff_ = std::exp (-kTwoPi * 2000.0f / static_cast<float> (sampleRate));
    psProfileReleaseCoeff_ = psProfileReleaseMs_ > 0.0f
        ? std::exp (-1.0f / (0.001f * psProfileReleaseMs_ * static_cast<float> (sampleRate)))
        : 0.0f;
    psProfileSlowReleaseCoeff_ = psProfileSlowReleaseMs_ > 0.0f
        ? std::exp (-1.0f / (0.001f * psProfileSlowReleaseMs_ * static_cast<float> (sampleRate)))
        : 0.0f;
    psProfileHoldSamples_ = static_cast<int> (0.001f * psProfileHoldMs_ * static_cast<float> (sampleRate));
    psProfileHoldRemaining_ = 0;
    psProfileTimeEnv_ = psProfileSlowTimeEnv_ = 0.0f;
    for (int b = 0; b < 3; ++b)
        psProfileGainL_[b] = psProfileGainR_[b] = 1.0f;
    psBandLp1L_ = psBandLp1R_ = psBandLp2L_ = psBandLp2R_ = 0.0f;
    psWanderStarted_ = false;
    psWanderPhase_ = psWanderInitialPhase_;
    psWanderEnv_ = 0.0f;

    // Per-band Width tilt — one-pole LP coeffs at the 300 Hz / 5 kHz crossovers.
    wbLp1Coeff_ = std::exp (-kTwoPi *  300.0f / static_cast<float> (sampleRate));
    wbLp2Coeff_ = std::exp (-kTwoPi * 5000.0f / static_cast<float> (sampleRate));
    wbLp1State_ = 0.0f;
    wbLp2State_ = 0.0f;

    // Post-tank parametric EQ. Default state is all bands at gainDb=0 →
    // unity coefficients → bit-identical bypass. Per-preset overrides
    // come in via setPostTankEQBand() after the preset loads.
    postTankEQ_.prepare (static_cast<float> (sampleRate));
    postTankBandTrim_.prepare (static_cast<float> (sampleRate));
    perBandEDT_.prepare (static_cast<float> (sampleRate));

    // ER-bus spectral-correction shelves. Default 0 dB → unity → bit-identical.
    erBusLowShelf_.prepare (static_cast<float> (sampleRate));
    erBusHighShelf_.prepare (static_cast<float> (sampleRate));

    // Force-apply algorithm 0 on first prepare (don't bypass via early-return).
    currentAlgorithm_ = -1;
    setAlgorithm (0);

    // #123 calibration hooks. Run after every engine has been prepared and after
    // the post-steer state reset above. Preset application may subsequently
    // replace DUSKVERB_POSTSTEER with its baked value; PluginProcessor preserves
    // the environment override when applying a factory preset.
    applyStereoImageBiasOverride();
    applyPostSteerOverride();
}

void DuskVerbEngine::clearAllBuffers()
{
    dattorro_ .clearBuffers();
    dattorroDenseField_.clear();
    dynLowMid_.clear();
    wetSusLimMidDet_.clear();  wetSusLimLowDet_.clear();
    wetSusLimMidCutL_.clear(); wetSusLimMidCutR_.clear();
    wetSusLimLowCutL_.clear(); wetSusLimLowCutR_.clear();
    wetSusLimKey_.clear();
    sixAPTank_.clearBuffers();
    quad_     .clearBuffers();
    fdn_      .clearBuffers();
    accurateHall_.clearBuffers();   // FDNReverbT<true> — was missing here (setAlgorithm clears it, but that early-returns when the algo is unchanged → AccurateHall state could leak across same-algo preset swaps).
    sparseField_.clear();           // algo 11 early-field tap buffers
    diffuseER_.clear();             // diffused discrete-ER bus (DenseHall)
    denseHall_.clear();             // algo 14 dense hall tank state
    pmb_.clear();                   // algo 15 parallel multiband tank state
    buildupDiffuser_.clear();       // DenseHall tail-buildup cascade
 // algo 12 (32-line)
    outputDiffusion_.clear();       // per-preset post-tank diffuser (BH)
    multibandFdn_.clearBuffers();
    spring_   .clearBuffers();
    nonLinear_.clearBuffers();
    shimmer_  .clearBuffers();
    dattorroVintage_.clearBuffers();

    reverseRoom_.clearBuffers();

    // Transient-duck envelopes/holds (sparse-ER, early-tap, early-window tank) —
    // POD onset state that survives an idle engine; reset so a preset swap can't
    // reuse a stale gate position.
    erDuck_.reset();
    etapDuck_.reset();
    tankDuck_.reset();

    // Pre-tank input diffuser and early reflections — both retain
    // signal-carrying state (allpass buffers, multi-tap delay lines, per-tap
    // LP states) that survives setAlgorithm() and would bleed stale audio
    // into the new preset's tail when an idle engine is reused.
    diffuser_.clear();
    er_.clear();
    tfLowStateL_ = tfLowStateR_ = tfHighStateL_ = tfHighStateR_ = 0.0f;

    std::fill (preDelayBufL_.begin(), preDelayBufL_.end(), 0.0f);
    std::fill (preDelayBufR_.begin(), preDelayBufR_.end(), 0.0f);
    preDelayWritePos_ = 0;

    // Phase A tank-onset delay ring buffer — retains tail audio; clear so it
    // can't bleed across preset swaps / unfreeze.
    std::fill (tankOnsetBufL_.begin(), tankOnsetBufL_.end(), 0.0f);
    std::fill (tankOnsetBufR_.begin(), tankOnsetBufR_.end(), 0.0f);
    tankOnsetWrite_ = 0;

    // FORK A reflection-tap ring + LP state — retains a delayed dry copy; clear so
    // the "duh-duh" can't bleed across preset swaps / unfreeze.
    std::fill (reflBuf_.begin(), reflBuf_.end(), 0.0f);
    reflWritePos_ = 0;
    reflLpStateL_ = reflLpStateR_ = 0.0f;
    etapLpStateL_ = etapLpStateR_ = 0.0f;   // early-tap bank shares reflBuf_ — clear its LP state too

    loCutFilter_.reset();
    hiCutFilter_.reset();
    airShelfFilter_.reset();
    lowShelfFilter_.reset();
    postTankEQ_.reset();
    erBusLowShelf_.reset();
    erBusHighShelf_.reset();
    tankSplitLpL_ = tankSplitLpR_ = 0.0f;
    postTankBandTrim_.reset();
    perBandEDT_.reset();

    monoLPStateL_ = 0.0f;
    monoLPStateR_ = 0.0f;
    xtalkLpL_     = 0.0f;
    xtalkLpR_     = 0.0f;
    wbLp1State_   = 0.0f;
    wbLp2State_   = 0.0f;
    psEnvL_ = psEnvR_ = 0.0f;
    psGainL_ = psGainR_ = 1.0f;
    psProfileTimeEnv_ = psProfileSlowTimeEnv_ = 0.0f;
    psProfileHoldRemaining_ = 0;
    for (int b = 0; b < 3; ++b)
        psProfileGainL_[b] = psProfileGainR_[b] = 1.0f;
    psBandLp1L_ = psBandLp1R_ = psBandLp2L_ = psBandLp2R_ = 0.0f;
    psWanderStarted_ = false;
    psWanderPhase_ = psWanderInitialPhase_;
    psWanderEnv_ = 0.0f;
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
    // Tank-feed shelf integrator state (post-diffuser, pre-tank). Coeffs/params
    // are NOT copied — applyEngineConfig already set this engine's tank-feed
    // config for the new preset; only the one-pole state needs continuity to
    // avoid a transient pop when swapping into a tankFeedActive_ preset.
    tfLowStateL_  = other.tfLowStateL_;
    tfLowStateR_  = other.tfLowStateR_;
    tfHighStateL_ = other.tfHighStateL_;
    tfHighStateR_ = other.tfHighStateR_;
}

void DuskVerbEngine::setAlgorithm (int index)
{
    if (index == currentAlgorithm_)
        return;

    currentAlgorithm_ = index;
    currentEngine_ = getAlgorithmConfig (index).engine;

    dattorro_.clearBuffers();
    dattorroDenseField_.clear();
    dynLowMid_.clear();
    wetSusLimMidDet_.clear();  wetSusLimLowDet_.clear();
    wetSusLimMidCutL_.clear(); wetSusLimMidCutR_.clear();
    wetSusLimLowCutL_.clear(); wetSusLimLowCutR_.clear();
    wetSusLimKey_.clear();
    sixAPTank_.clearBuffers();
    quad_.clearBuffers();
    fdn_.clearBuffers(); accurateHall_.clearBuffers (); sparseField_.clear(); diffuseER_.clear(); outputDiffusion_.clear(); denseHall_.clear(); buildupDiffuser_.clear(); pmb_.clear();
    erDuck_.reset();
    etapDuck_.reset();
    tankDuck_.reset();
    multibandFdn_.clearBuffers();
    spring_.clearBuffers();
    nonLinear_.clearBuffers();
    shimmer_.clearBuffers();
    dattorroVintage_.clearBuffers();

    reverseRoom_.clearBuffers();

    // Top-level tank-onset + reflection-tap rings: the direct Algorithm-knob path
    // calls setAlgorithm WITHOUT clearAllBuffers (only the preset-swap path clears),
    // so clear them here too or stale delayed audio from the prior algorithm leaks.
    std::fill (tankOnsetBufL_.begin(), tankOnsetBufL_.end(), 0.0f);
    std::fill (tankOnsetBufR_.begin(), tankOnsetBufR_.end(), 0.0f);
    tankOnsetWrite_ = 0;
    std::fill (reflBuf_.begin(), reflBuf_.end(), 0.0f);
    reflWritePos_ = 0;
    reflLpStateL_ = reflLpStateR_ = 0.0f;
    etapLpStateL_ = etapLpStateR_ = 0.0f;   // early-tap bank shares reflBuf_ — clear its LP state too
}

void DuskVerbEngine::setFreeze (bool frozen)
{
    if (frozen == frozen_)
        return;
    frozen_ = frozen;
    dattorro_.setFreeze (frozen);
    sixAPTank_.setFreeze (frozen);
    quad_.setFreeze (frozen);
    fdn_.setFreeze (frozen); accurateHall_.setFreeze (frozen);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setFreeze (frozen); });
    spring_.setFreeze (frozen);
    nonLinear_.setFreeze (frozen);
    shimmer_.setFreeze (frozen);
    dattorroVintage_.setFreeze (frozen);
    reverseRoom_.setFreeze (frozen);
    denseHall_.setFreeze (frozen);
    pmb_.setFreeze (frozen);
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
    fdn_.setDecayTime (seconds); accurateHall_.setDecayTime (seconds);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setDecayTime (seconds); });
    spring_.setDecayTime (seconds);
    nonLinear_.setDecayTime (seconds);
    shimmer_.setDecayTime (seconds);
    dattorroVintage_.setDecayTime (seconds);

    reverseRoom_.setDecayTime (seconds);
    denseHall_.setDecayTime (seconds);
    pmb_.setDecayScale (seconds / 2.0f);   // 2 s = the band table's reference decay; knob scales the curve
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
    fdn_.setSize (size); accurateHall_.setSize (size);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setSize (size); });
    spring_.setSize (size);
    nonLinear_.setSize (size);
    shimmer_.setSize (size);
    dattorroVintage_.setSize (size);

    reverseRoom_.setSize (size);
    denseHall_.setSize (size);
}

void DuskVerbEngine::setBassMultiply (float mult)
{
    dattorro_.setBassMultiply (mult);
    sixAPTank_.setBassMultiply (mult);
    quad_.setBassMultiply (mult);
    fdn_.setBassMultiply (mult); accurateHall_.setBassMultiply (mult);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setBassMultiply (mult); });
    spring_.setBassMultiply (mult);
    nonLinear_.setBassMultiply (mult);
    shimmer_.setBassMultiply (mult);
    dattorroVintage_.setBassMultiply (mult);

    reverseRoom_.setBassMultiply (mult);
    denseHall_.setBassMultiply (mult);
}

void DuskVerbEngine::setMidMultiply (float mult)
{
    dattorro_.setMidMultiply (mult);
    sixAPTank_.setMidMultiply (mult);
    quad_.setMidMultiply (mult);
    fdn_.setMidMultiply (mult); accurateHall_.setMidMultiply (mult);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setMidMultiply (mult); });
    spring_.setMidMultiply (mult);
    nonLinear_.setMidMultiply (mult);
    shimmer_.setMidMultiply (mult);
    dattorroVintage_.setMidMultiply (mult);

    reverseRoom_.setMidMultiply (mult);
    denseHall_.setMidMultiply (mult);
}

void DuskVerbEngine::setTrebleMultiply (float mult)
{
    dattorro_.setTrebleMultiply (mult);
    sixAPTank_.setTrebleMultiply (mult);
    quad_.setTrebleMultiply (mult);
    fdn_.setTrebleMultiply (mult); accurateHall_.setTrebleMultiply (mult);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setTrebleMultiply (mult); });
    spring_.setTrebleMultiply (mult);
    nonLinear_.setTrebleMultiply (mult);
    shimmer_.setTrebleMultiply (mult);
    dattorroVintage_.setTrebleMultiply (mult);

    reverseRoom_.setTrebleMultiply (mult);
    denseHall_.setTrebleMultiply (mult);
}

void DuskVerbEngine::setAirTrebleMultiply (float mult)
{
    // FDN-specific bug-fix: feed the same damping value into the FDN's
    // airTrebleMultiply_ member that computeDecayCoefficients actually
    // reads. Without this, the APVTS damping knob is dead-code on the
    // FDN engine path because fdn_.setTrebleMultiply writes to a member
    // that is never consumed inside the per-line decay calc.
    fdn_.setAirTrebleMultiply (mult); accurateHall_.setAirTrebleMultiply (mult);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setAirTrebleMultiply (mult); });
}

// FiveBandDamping (Phase 2) — FDN-only; other engines have no five-band path.
// Forward to BOTH the legacy single tank and the parallel-multiband tanks so
// the multiband path (when mb_enable is on) receives identical voicing instead
// of silently running these axes at their defaults.
void DuskVerbEngine::setSubMultiply     (float mult) { fdn_.setSubMultiply (mult); accurateHall_.setSubMultiply (mult);      multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setSubMultiply (mult); }); }
void DuskVerbEngine::setHiMidMultiply   (float mult) { fdn_.setHiMidMultiply (mult); accurateHall_.setHiMidMultiply (mult);    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setHiMidMultiply (mult); }); }
// QuadTank 5-band split (hi-mid 4-8k / air >8k). Separate from the FDN path:
// QuadTank's transparency sentinel is -1, distinct from the FDN convention.
void DuskVerbEngine::setQuadHiMidMultiply (float mult) { quad_.setHiMidMultiply (mult); }
void DuskVerbEngine::setQuadAirMultiply   (float mult) { quad_.setAirMultiply (mult); }
void DuskVerbEngine::setSubCrossoverFreq (float hz)  { fdn_.setSubCrossoverFreq (hz); accurateHall_.setSubCrossoverFreq (hz);   multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setSubCrossoverFreq (hz); }); }
void DuskVerbEngine::setAirCrossoverFreq (float hz)  { fdn_.setAirCrossoverFreq (hz); accurateHall_.setAirCrossoverFreq (hz);   multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setAirCrossoverFreq (hz); }); }
void DuskVerbEngine::setShaperDepth     (float d)    { fdn_.setShaperDepth (d); accurateHall_.setShaperDepth (d);         multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setShaperDepth (d); }); }
void DuskVerbEngine::setShaperTimeMs    (float ms)   { fdn_.setShaperTimeMs (ms); accurateHall_.setShaperTimeMs (ms);       multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setShaperTimeMs (ms); }); }
void DuskVerbEngine::setShaperXoverHz   (float hz)   { fdn_.setShaperXoverHz (hz); accurateHall_.setShaperXoverHz (hz);      multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setShaperXoverHz (hz); }); }
void DuskVerbEngine::setShaperSens      (float s)    { fdn_.setShaperSens (s); accurateHall_.setShaperSens (s);          multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setShaperSens (s); }); }
void DuskVerbEngine::setInputSubGainDb  (float db)   { fdn_.setInputSubGainDb (db); accurateHall_.setInputSubGainDb (db);     multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setInputSubGainDb (db); }); }
void DuskVerbEngine::setInputMidGainDb  (float db)   { fdn_.setInputMidGainDb (db); accurateHall_.setInputMidGainDb (db);     multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setInputMidGainDb (db); }); }
void DuskVerbEngine::setInputHighGainDb (float db)   { fdn_.setInputHighGainDb (db); accurateHall_.setInputHighGainDb (db);    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setInputHighGainDb (db); }); }

void DuskVerbEngine::setCrossoverFreq (float hz)
{
    dattorro_.setCrossoverFreq (hz);
    sixAPTank_.setCrossoverFreq (hz);
    quad_.setCrossoverFreq (hz);
    fdn_.setCrossoverFreq (hz); accurateHall_.setCrossoverFreq (hz);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setCrossoverFreq (hz); });
    spring_.setCrossoverFreq (hz);
    nonLinear_.setCrossoverFreq (hz);
    shimmer_.setCrossoverFreq (hz);
    dattorroVintage_.setCrossoverFreq (hz);
    denseHall_.setCrossoverFreq (hz);

    reverseRoom_.setCrossoverFreq (hz);
}

void DuskVerbEngine::setHighCrossoverFreq (float hz)
{
    dattorro_.setHighCrossoverFreq (hz);
    sixAPTank_.setHighCrossoverFreq (hz);
    quad_.setHighCrossoverFreq (hz);
    fdn_.setHighCrossoverFreq (hz); accurateHall_.setHighCrossoverFreq (hz);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setHighCrossoverFreq (hz); });
    spring_.setHighCrossoverFreq (hz);
    nonLinear_.setHighCrossoverFreq (hz);
    shimmer_.setHighCrossoverFreq (hz);
    dattorroVintage_.setHighCrossoverFreq (hz);
    denseHall_.setHighCrossoverFreq (hz);
    reverseRoom_.setHighCrossoverFreq (hz);
}

void DuskVerbEngine::setSaturation (float amount)
{
    dattorro_.setSaturation (amount);
    sixAPTank_.setSaturation (amount);
    quad_.setSaturation (amount);
    fdn_.setSaturation (amount); accurateHall_.setSaturation (amount);
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
    fdn_.setModDepth (depth); accurateHall_.setModDepth (depth);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setModDepth (depth); });
    spring_.setModDepth (depth);
    nonLinear_.setModDepth (depth);
    shimmer_.setModDepth (depth);   // hijacked → PITCH (0..1 → 0..24 semitones)
    dattorroVintage_.setModDepth (depth);
    // VintageTank: depth knob ∈ [0, 1] → mod excursion in samples (~0..16
    // sample sweep range, the Lex/Griesinger lush-mode default).

    reverseRoom_.setModDepth (depth);
    denseHall_.setModDepth (depth);
}

void DuskVerbEngine::setModulationTopology (DspUtils::ModulationTopology t)
{
    // Phase 2: only FDN + QuadTank have the coherent topology implemented
    // for now. Other engines silently ignore (they use their own bespoke
    // modulators — DPV's structured tank, Shimmer's pitch loop, etc.).
    fdn_.setModulationTopology (t); accurateHall_.setModulationTopology (t);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setModulationTopology (t); });
    quad_.setModulationTopology (t);
}

void DuskVerbEngine::setPerLineDecayTilt (float shortLineScale, float longLineScale)
{
    // Phase α: only FDN has the per-line decay-tilt path. QuadTank uses
    // 4 cross-coupled tanks (different topology), Dattorro is a single
    // structured tank — neither has a per-line rank to tilt against.
    fdn_.setPerLineDecayTilt (shortLineScale, longLineScale); accurateHall_.setPerLineDecayTilt (shortLineScale, longLineScale);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setPerLineDecayTilt (shortLineScale, longLineScale); });
}

void DuskVerbEngine::setAccurateHallOctaveT60 (int band, float seconds)
{
    // AccurateHall only — the per-octave GEQ is compiled solely into
    // FDNReverbT<true> (accurateHall_). All other engines have no octave T60.
    accurateHall_.setOctaveT60 (band, seconds);
}

void DuskVerbEngine::setAccurateHallOctaveDecayRef (float seconds)
{
    accurateHall_.setOctaveDecayRef (seconds);
}

void DuskVerbEngine::setDenseHallOctaveT60 (int band, float seconds)
{
    // DenseHall (algo 14) per-octave GEQ — fork #2. No-op on every other engine.
    denseHall_.setOctaveT60 (band, seconds);
}

void DuskVerbEngine::setShimmerNoiseDuck (float amt)
{
    shimmer_.setNoiseSustainDuck (amt);
}

void DuskVerbEngine::setShimmerLoopHiLimiter (float loHz, float hiHz, float ratioThrDb, float maxCutDb,
                                              float atkMs, float relFastMs, float relSlowMs)
{
    shimmer_.setLoopHiLimiter (loHz, hiHz, ratioThrDb, maxCutDb, atkMs, relFastMs, relSlowMs);
}

void DuskVerbEngine::setQuadStereoMod (float rateHz, float depth)
{
    quad_.setStereoMod (rateHz, depth);
}

void DuskVerbEngine::setQuadStereoInput (float amount)
{
    quad_.setStereoInput (amount);
}

void DuskVerbEngine::setDattorroStereoInput (float amount)
{
    dattorro_.setStereoInput (amount);
    dattorroVintage_.setStereoInput (amount);
}

void DuskVerbEngine::setSparseStereoInput (float amount)
{
    sparseField_.setStereoInput (amount);
}

void DuskVerbEngine::setTankHFSustain (float db, float cornerHz)
{
    // Per-pass HF-sustain compensation (top-octave cliff fix) — Dattorro + DenseHall
    // cover all 7 spec_L1@12.9k presets. 0 dB = bit-null on both.
    dattorro_.setHFSustain  (db, cornerHz);
    denseHall_.setHFSustain (db, cornerHz);
}

void DuskVerbEngine::setSustainLimiterMid (float loHz, float hiHz, float threshDb, float maxCutDb,
                                           float atkMs, float relFastMs, float relSlowMs)
{
    // Sustained-energy WET-INPUT duck, MID slot (law A "sine regulator") — configures the
    // engine-level pre-ER/pre-tank duck in process() step 1b. Measurement (2026-07-07)
    // showed the sine1k steady-state excess is ~90% FIRST-PASS throughput (killing the
    // whole recirculation via Decay 0.1 s moved the sine only 0.2-1.2 dB), so the duck
    // sits on the wet FEED (engine-agnostic, one site) rather than inside the loops; the
    // per-engine in-loop instances from the same feature family remain available but
    // dormant. maxCutDb 0 -> inactive -> bit-null.
    wetSusLimMidCfg_.set (loHz, hiHz, threshDb, maxCutDb, atkMs, relFastMs, relSlowMs, sampleRate_);
    wetSusLimMidCutL_.design (wetSusLimMidCfg_.loHz, wetSusLimMidCfg_.hiHz, wetSusLimMidCfg_.maxCutDb, sampleRate_);
    wetSusLimMidCutR_.design (wetSusLimMidCfg_.loHz, wetSusLimMidCfg_.hiHz, wetSusLimMidCfg_.maxCutDb, sampleRate_);
    wetSusLimMidDet_.clear();
}

void DuskVerbEngine::setSustainLimiterLow (float loHz, float hiHz, float threshDb, float maxCutDb,
                                           float atkMs, float relFastMs, float relSlowMs)
{
    // LOW slot (law B "charge limiter") — same engine-level wet-input duck.
    wetSusLimLowCfg_.set (loHz, hiHz, threshDb, maxCutDb, atkMs, relFastMs, relSlowMs, sampleRate_);
    wetSusLimLowCutL_.design (wetSusLimLowCfg_.loHz, wetSusLimLowCfg_.hiHz, wetSusLimLowCfg_.maxCutDb, sampleRate_);
    wetSusLimLowCutR_.design (wetSusLimLowCfg_.loHz, wetSusLimLowCfg_.hiHz, wetSusLimLowCfg_.maxCutDb, sampleRate_);
    wetSusLimLowDet_.clear();
}

void DuskVerbEngine::setPmbBand (int b, float t60s, float level, float direct, float width)
{
    pmb_.setBand (b, t60s, level, direct, width);
}

void DuskVerbEngine::setPmbStereoImageBias (float amount)
{
    // Issue #123 — route the ParallelMultiband band taps' side onto the line that
    // actually carries the input side. 0 = off = bit-identical. No preset calls
    // this yet; enablement waits on the ear pass.
    pmb_.setStereoImageBias (amount);
}

void DuskVerbEngine::setPostSteer (float amount)
{
    // Issue #123 — engine-agnostic post-tank source-side ILD steer on the
    // FINAL wet. 0 = off = bit-identical. Factory presets provide anchor-matched
    // amounts; precompute psK_ so the hot loop only reads members.
    // amount ∈ [-1,+1]: negative leans AWAY from the source side (de-steers presets whose
    // engine already over-leans vs the anchor). psK_ tracks the sign.
    psAmount_        = std::clamp (amount, -1.0f, 1.0f);
    postSteerActive_ = std::abs (psAmount_) > 1.0e-6f || psProfileActive_;
    psK_             = kPostSteerKMax * psAmount_;
}

void DuskVerbEngine::setPostSteerProfile (float earlyLowK, float earlyMidK, float earlyHighK,
                                          float middleLowK, float middleMidK, float middleHighK,
                                          float lateLowK, float lateMidK, float lateHighK,
                                          float holdMs, float fastReleaseMs, float slowReleaseMs)
{
    psProfileK_[0] = std::clamp (earlyLowK,  -0.9999f, 0.9999f);
    psProfileK_[1] = std::clamp (earlyMidK,  -0.9999f, 0.9999f);
    psProfileK_[2] = std::clamp (earlyHighK, -0.9999f, 0.9999f);
    psProfileMiddleK_[0] = std::clamp (middleLowK,  -0.9999f, 0.9999f);
    psProfileMiddleK_[1] = std::clamp (middleMidK,  -0.9999f, 0.9999f);
    psProfileMiddleK_[2] = std::clamp (middleHighK, -0.9999f, 0.9999f);
    psProfileLateK_[0] = std::clamp (lateLowK,  -0.9999f, 0.9999f);
    psProfileLateK_[1] = std::clamp (lateMidK,  -0.9999f, 0.9999f);
    psProfileLateK_[2] = std::clamp (lateHighK, -0.9999f, 0.9999f);
    psProfileHoldMs_ = std::max (0.0f, holdMs);
    psProfileHoldSamples_ = sampleRate_ > 0.0
        ? static_cast<int> (0.001f * psProfileHoldMs_ * static_cast<float> (sampleRate_))
        : 0;
    psProfileReleaseMs_ = std::max (0.0f, fastReleaseMs);
    psProfileSlowReleaseMs_ = std::max (0.0f, slowReleaseMs);
    psProfileReleaseCoeff_ = psProfileReleaseMs_ > 0.0f && sampleRate_ > 0.0
        ? std::exp (-1.0f / (0.001f * psProfileReleaseMs_ * static_cast<float> (sampleRate_)))
        : 0.0f;
    psProfileSlowReleaseCoeff_ = psProfileSlowReleaseMs_ > 0.0f && sampleRate_ > 0.0
        ? std::exp (-1.0f / (0.001f * psProfileSlowReleaseMs_ * static_cast<float> (sampleRate_)))
        : 0.0f;
    psProfileActive_ = std::abs (psProfileK_[0]) > 1.0e-6f
                    || std::abs (psProfileK_[1]) > 1.0e-6f
                    || std::abs (psProfileK_[2]) > 1.0e-6f
                    || std::abs (psProfileMiddleK_[0]) > 1.0e-6f
                    || std::abs (psProfileMiddleK_[1]) > 1.0e-6f
                    || std::abs (psProfileMiddleK_[2]) > 1.0e-6f
                    || std::abs (psProfileLateK_[0]) > 1.0e-6f
                    || std::abs (psProfileLateK_[1]) > 1.0e-6f
                    || std::abs (psProfileLateK_[2]) > 1.0e-6f;
    postSteerActive_ = std::abs (psAmount_) > 1.0e-6f || psProfileActive_;
}

void DuskVerbEngine::setPostSteerPanRotation (float radians)
{
    psPanRotationRadians_ = std::clamp (radians, -0.25f * kTwoPi, 0.25f * kTwoPi);
}

void DuskVerbEngine::setPostSteerHardRightMirror (bool enabled)
{
    psMirrorHardRight_ = enabled;
}

void DuskVerbEngine::setPostSteerWander (float lowDepth, float midDepth, float highDepth,
                                         float rateHz, float decayMs, float phaseRadians)
{
    psWanderDepth_[0] = std::clamp (lowDepth,  -1.5f, 1.5f);
    psWanderDepth_[1] = std::clamp (midDepth,  -1.5f, 1.5f);
    psWanderDepth_[2] = std::clamp (highDepth, -1.5f, 1.5f);
    psWanderRateHz_ = std::clamp (rateHz, 0.0f, 5.0f);
    psWanderDecayMs_ = std::max (0.0f, decayMs);
    psWanderInitialPhase_ = phaseRadians;
    psWanderPhase_ = phaseRadians;
    psWanderPhaseInc_ = sampleRate_ > 0.0
        ? kTwoPi * psWanderRateHz_ / static_cast<float> (sampleRate_)
        : 0.0f;
    psWanderDecayCoeff_ = psWanderDecayMs_ > 0.0f && sampleRate_ > 0.0
        ? std::exp (-1.0f / (0.001f * psWanderDecayMs_ * static_cast<float> (sampleRate_)))
        : 0.0f;
    psWanderActive_ = psWanderRateHz_ > 0.0f && psWanderDecayMs_ > 0.0f
                   && (std::abs (psWanderDepth_[0]) > 1.0e-6f
                    || std::abs (psWanderDepth_[1]) > 1.0e-6f
                    || std::abs (psWanderDepth_[2]) > 1.0e-6f);
    psWanderStarted_ = false;
    psWanderEnv_ = 0.0f;
}

void DuskVerbEngine::applyPostSteerOverride()
{
    // Calibration hook (issue #123), same pattern as DUSKVERB_VELVET / DUSKVERB_REVERSE.
    // Factory-preset application preserves this override, allowing the render harness
    // to sweep the ILD calibration or explicitly disable it with a value of zero.
    if (const char* ov = std::getenv ("DUSKVERB_POSTSTEER"))
    {
        const float amount = std::clamp (static_cast<float> (std::atof (ov)), -1.0f, 1.0f);
        setPostSteer (amount);
    }
}

void DuskVerbEngine::setDenseHallOctaveDecayRef (float seconds)
{
    denseHall_.setOctaveDecayRef (seconds);
}

void DuskVerbEngine::setDenseHallTonalCorrection (bool enabled)
{
    // FORK B — DenseHall Jot output tonal-correction (decouple T60 from level).
    denseHall_.setTonalCorrection (enabled);
}

void DuskVerbEngine::setDenseHallStereoImageBias (float amount)
{
    // Issue #123 — restore the source-side energy lean a hard-panned input loses in
    // the DenseHall output taps. 0 = off = bit-identical. No preset calls this yet;
    // enablement waits on the ear pass.
    denseHall_.setStereoImageBias (amount);
}

void DuskVerbEngine::applyStereoImageBiasOverride()
{
    // Calibration hook (issue #123), same pattern as DUSKVERB_VELVET / DUSKVERB_REVERSE:
    // lets the render harness measure the ILD table without wiring the feature into
    // any preset path. Message thread only (prepare), read once per prepare, absent
    // env ⇒ nothing is called at all ⇒ the engines keep their bit-identical default.
    if (const char* ov = std::getenv ("DUSKVERB_STEREOBIAS"))
    {
        const float raw = static_cast<float> (std::atof (ov));
        if (raw > 0.0f)
        {
            // Tier-2 output-tap levers clamp to 0..1 here; the tank injection
            // levers (measured walls, env-only, no preset) take the raw value
            // and clamp to their own 0..4 range internally.
            const float amount = std::clamp (raw, 0.0f, 1.0f);
            denseHall_.setStereoImageBias (amount);
            pmb_.setStereoImageBias (amount);
            quad_.setStereoInput (raw);
        }
    }
}

void DuskVerbEngine::setReflectionTap (float ms, float gain, float lpFc)
{
    // FORK A — discrete early-reflection tap ("duh-duh"). Summed to wet in the
    // per-sample output loop. R offset +9 ms decorrelates the two arrivals so the
    // reflection has width (not a mono slap). gain 0 → reflActive_ false → the
    // whole block is skipped → bit-identical for every preset not opting in.
    // lpFc = the tap's one-pole rolloff: 11 kHz = a sharp/bright tick (79VC);
    // ~5-6 kHz = a darker, FULLER, softer reflection (Bright Hall — the ear wanted
    // VVV's "fuller, softer room reflection", not a snappy tick).
    const float sr = static_cast<float> (sampleRate_);
    const int   maxD = (reflMask_ > 1) ? reflMask_ - 1 : 1;
    reflDelayL_ = std::clamp (static_cast<int> (ms * 0.001f * sr), 1, maxD);
    reflDelayR_ = std::clamp (static_cast<int> ((ms + 9.0f) * 0.001f * sr), 1, maxD);
    reflGain_   = std::clamp (gain, 0.0f, 4.0f);   // >1 allowed: the anchor tap is LOUDER than the dry (+8.5dB rel onset), needs gain>1 to match the discrete-tap prominence
    reflActive_ = reflGain_ > 1.0e-6f;
    const float fc = std::clamp (lpFc, 1000.0f, 20000.0f);
    reflLpCoeff_ = 1.0f - std::exp (-6.283185307179586f * fc / sr);
}

void DuskVerbEngine::setEarlyTapBank (const float* timesMs, const float* gains, int count, float lpFc)
{
    // Multi-tap generalization of Fork A: N discrete reflections at the anchor's
    // measured arrival times. Shares reflBuf_ (clean pre-diffuser dry). Per-tap
    // alternating ±4 ms R offset gives each arrival width without smearing the
    // pattern. count 0 → etapActive_ false → the block is skipped → bit-null.
    const float sr = static_cast<float> (sampleRate_);
    const int   maxD = (reflMask_ > 1) ? reflMask_ - 1 : 1;
    etapCount_ = std::clamp (count, 0, kMaxEarlyTaps);
    for (int i = 0; i < etapCount_; ++i)
    {
        etapMs_[i]     = std::clamp (timesMs[i], 1.0f, 300.0f);
        etapGain_[i]   = std::clamp (gains[i], 0.0f, 4.0f);
        const float rOff = (i & 1) ? -4.0f : 4.0f;
        etapDelayL_[i] = std::clamp (static_cast<int> (etapMs_[i] * 0.001f * sr), 1, maxD);
        etapDelayR_[i] = std::clamp (static_cast<int> ((etapMs_[i] + rOff) * 0.001f * sr), 1, maxD);
    }
    bool any = false;
    for (int i = 0; i < etapCount_; ++i) any = any || (etapGain_[i] > 1.0e-6f);
    etapActive_ = any && etapCount_ > 0;
    etapLpFcHz_  = std::clamp (lpFc, 1000.0f, 20000.0f);
    etapLpCoeff_ = 1.0f - std::exp (-6.283185307179586f * etapLpFcHz_ / sr);
}

void DuskVerbEngine::setTonalCorrection (bool enabled)
{
    accurateHall_.setTonalCorrection (enabled);   // AccurateHall (algo 10) only; other engines have no Jot output GEQ
}

void DuskVerbEngine::setTankOnsetMs (float ms)
{
    // Store the requested ms so the sample count survives being set before
    // prepare() allocates the buffer, and is recomputed on sample-rate change.
    tankOnsetMs_ = std::max (0.0f, ms);
    recomputeTankOnsetSamples();
}

void DuskVerbEngine::recomputeTankOnsetSamples()
{
    const int sz = static_cast<int> (tankOnsetBufL_.size());
    const int s = static_cast<int> (std::round (tankOnsetMs_ * 0.001f * static_cast<float> (sampleRate_)));
    tankOnsetSamples_ = (sz > 1) ? std::min (s, sz - 1) : 0;
}

void DuskVerbEngine::setDattorroEarlyField (bool on)
{
    dattorroEarlyFieldOn_ = on;
}

void DuskVerbEngine::setDynamicLowMid (float threshDb, float maxCut, float splitHz, float atkMs, float relMs)
{
    dynLowMid_.setParams (threshDb, maxCut, splitHz, atkMs, relMs);
}

void DuskVerbEngine::setDattorroModeSmear (float depthSamples, float rateHz)
{
    // Applied to the Dattorro tank, the AccurateHall FDN, AND the ParallelMultiband tank
    // so a boing preset on any of them gets it (each is a no-op at depth 0 → bit-null).
    dattorro_.setModeSmear (depthSamples, rateHz);
    accurateHall_.setModeSmear (depthSamples, rateHz);
    pmb_.setModeSmear (depthSamples, rateHz);
    shimmer_.setModeSmear (depthSamples, rateHz);
}

void DuskVerbEngine::setAccurateHallEarlyField (bool on)
{
    accurateHallEarlyFieldOn_ = on;
}

// Shared early-field composite (Dattorro + AccurateHall): optional tank-onset delay
// that pushes the late tank back so the undelayed velvet sparse ER defines the attack/
// onset/early reflections, then MIXES tank·sparseTailGain_ + sparseER·(sparseERGain_ ×
// transient-duck). The duck (keyed off the clean dry mono) fires the ER on a hit —
// closing attack/onset/early_refl/early_tap — but ducks it to silence on a sustained
// tone/noise, so it adds NO steady energy (no noiseburst gain-match cascade, no sine1k
// pump). Per-tap velvet signs are decorrelated so a sustained tone cannot coherently
// comb at 1 kHz (the fixed-ERTAPS failure). Voicing from kCompositeERByName.
void DuskVerbEngine::applyEarlyFieldComposite (int numSamples)
{
    // Tank-onset delay: push the late tank back by tankOnsetSamples_ (0 → skipped). Same
    // ring-buffer delay the DenseHall case uses.
    if (tankOnsetSamples_ > 0)
    {
        const int sz = static_cast<int> (tankOnsetBufL_.size());
        for (int i = 0; i < numSamples; ++i)
        {
            int rd = tankOnsetWrite_ - tankOnsetSamples_; if (rd < 0) rd += sz;
            const float dl = tankOnsetBufL_[static_cast<size_t> (rd)];
            const float dr = tankOnsetBufR_[static_cast<size_t> (rd)];
            tankOnsetBufL_[static_cast<size_t> (tankOnsetWrite_)] = tankOutL_[static_cast<size_t> (i)];
            tankOnsetBufR_[static_cast<size_t> (tankOnsetWrite_)] = tankOutR_[static_cast<size_t> (i)];
            tankOutL_[static_cast<size_t> (i)] = dl;
            tankOutR_[static_cast<size_t> (i)] = dr;
            if (++tankOnsetWrite_ >= sz) tankOnsetWrite_ = 0;
        }
    }
    sparseField_.process (tankInL_.data(), tankInR_.data(),
                          sparseOutL_.data(), sparseOutR_.data(), numSamples,
                          sourceSide_.data());
    applyEarlyTankDuck (numSamples);
    if (sparseERDuckAmount_ > 0.0f)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float dg = erDuck_.process (reflDryMono_[static_cast<size_t> (i)]);
            const float g  = sparseERGain_ * ((1.0f - sparseERDuckAmount_) + sparseERDuckAmount_ * dg);
            tankOutL_[static_cast<size_t> (i)] =
                tankOutL_[static_cast<size_t> (i)] * sparseTailGain_ + sparseOutL_[static_cast<size_t> (i)] * g;
            tankOutR_[static_cast<size_t> (i)] =
                tankOutR_[static_cast<size_t> (i)] * sparseTailGain_ + sparseOutR_[static_cast<size_t> (i)] * g;
        }
    }
    else
        for (int i = 0; i < numSamples; ++i)
        {
            tankOutL_[static_cast<size_t> (i)] =
                tankOutL_[static_cast<size_t> (i)] * sparseTailGain_ + sparseOutL_[static_cast<size_t> (i)] * sparseERGain_;
            tankOutR_[static_cast<size_t> (i)] =
                tankOutR_[static_cast<size_t> (i)] * sparseTailGain_ + sparseOutR_[static_cast<size_t> (i)] * sparseERGain_;
        }
}

void DuskVerbEngine::setOutputMatchEQ (const float* corrLinear9)
{
    // Sanitize the per-octave gains before they reach designCoeffs: clamp into
    // the valid cut-only range [1e-3, 1] and replace any non-finite value with
    // unity. The sanitized copy is stored so prepare() can redesign coeffs at a
    // new sample rate without the original (possibly out-of-range) input.
    bool anyCut = false;
    for (int k = 0; k < OctaveBandDamping::kNumBands; ++k)
    {
        float g = corrLinear9[k];
        if (! std::isfinite (g)) g = 1.0f;
        g = std::clamp (g, 1.0e-3f, 1.0f);
        matchCorr_[k] = g;
        if (g < 0.999f) anyCut = true;
    }
    matchEQActive_ = anyCut;
    designMatchEQ();
}

void DuskVerbEngine::designMatchEQ()
{
    // 8 inter-octave crossovers (63 Hz..16 kHz octave centres) — same grid as
    // the AccurateHall loop GEQ. Identity (all gains ~1) → inactive → bit-null.
    static constexpr float kXoverHz[OctaveBandDamping::kNumShelves] = {
        88.4f, 176.8f, 353.6f, 707.1f, 1414.2f, 2828.4f, 5656.9f, 11313.7f };
    if (matchEQActive_)
        matchCoeffs_ = OctaveBandDamping::designCoeffs (
            matchCorr_, kXoverHz, static_cast<float> (sampleRate_));
    matchEQL_.reset();
    matchEQR_.reset();
}

// ── SparseField (algo 11) early-field generator + tail level ──────────────
// buildTaps() runs inside these setters (message thread, preset-apply); the
// audio-thread process() stays allocation-free.
void DuskVerbEngine::setSparseFieldSize     (float s)    { sparseField_.setSizeScale (s); }
void DuskVerbEngine::setSparseFieldOnsetMs  (float ms)   { sparseField_.setOnsetPeakMs (ms); }
void DuskVerbEngine::setSparseFieldDecayMs  (float ms)   { sparseField_.setDecayMs (ms); }
void DuskVerbEngine::setSparseFieldBurst2Ms (float ms)   { sparseField_.setBurst2Ms (ms); }
void DuskVerbEngine::setSparseFieldBurst2Gain (float g)  { sparseField_.setBurst2Gain (g); }
void DuskVerbEngine::setBuildupAmount        (float a)   { buildupDiffuser_.setAmount (a); }
void DuskVerbEngine::setBuildupTimeScale     (float s)   { buildupDiffuser_.setTimeScale (s); }
void DuskVerbEngine::setBuildupPostTank      (bool b)    { buildupPostTank_ = b; }
void DuskVerbEngine::setSparseFieldTailGain (float gain) { sparseTailGain_ = std::clamp (gain, 0.0f, 1.0f); }
void DuskVerbEngine::setSparseERGain        (float gain) { sparseERGain_   = std::clamp (gain, 0.0f, 2.0f); }
void DuskVerbEngine::setSparseERDuck (float amount, float holdMs, float thresh)
{
    sparseERDuckAmount_ = std::clamp (amount, 0.0f, 1.0f);
    erDuck_.setHoldMs (holdMs);
    erDuck_.setThreshold (thresh);
}
void DuskVerbEngine::setEarlyTapDuck (float amount, float holdMs, float thresh)
{
    etapDuckAmount_ = std::clamp (amount, 0.0f, 1.0f);
    etapDuck_.setHoldMs (holdMs);
    etapDuck_.setThreshold (thresh);
}
void DuskVerbEngine::setEarlyTankDuck (float amount, float holdMs, float thresh)
{
    tankDuckAmount_ = std::clamp (amount, 0.0f, 1.0f);
    tankDuck_.setHoldMs (holdMs);
    tankDuck_.setThreshold (thresh);
}
// Pre-ER pass: suppress the dense tank/wash for the early-reflection window (the
// hold after an onset) so the discrete sparse-ER taps STAND OUT instead of being
// buried in the wash. tankGain = 1 - amount·dg; the onset-gated dg holds ~1 across
// the early field then decays to 0 on sustain, so the tail restores to FULL — the
// sustained-window gates (ss/T60/tail/env_p2p) see an unchanged tank. Ducking the
// early wash also delays the tank's energy arrival (helps energy_t50/first50 on the
// too-front-loaded DenseHall rooms). Skipped entirely (tank untouched, tankDuck_
// not advanced) when off → bit-null. Call right after sparseField_.process, before
// the ER mix, at each engine's composite mix site.
void DuskVerbEngine::applyEarlyTankDuck (int numSamples)
{
    if (tankDuckAmount_ <= 0.0f) return;
    for (int i = 0; i < numSamples; ++i)
    {
        const float dg  = tankDuck_.process (reflDryMono_[static_cast<size_t> (i)]);
        const float tdg = 1.0f - tankDuckAmount_ * dg;
        tankOutL_[static_cast<size_t> (i)] *= tdg;
        tankOutR_[static_cast<size_t> (i)] *= tdg;
    }
}
void DuskVerbEngine::setDiffuseER (const float* timesMs, const float* gains, int n, float diffusion, float busGain)
{
    diffuseER_.setReflections (timesMs, gains, n);
    diffuseER_.setDiffusion (diffusion);
    diffuseERGain_ = std::clamp (busGain, 0.0f, 2.0f);
}

void DuskVerbEngine::setDiffuseERHighpass (float hz)
{
    diffuseERHpHz_ = std::clamp (hz, 0.0f, 12000.0f);
    if (diffuseERHpHz_ <= 0.0f)
        return;
    // LR4 highpass = two cascaded RBJ Butterworth-2 HP sections (Q 0.7071).
    const float w0 = 6.2831853f * diffuseERHpHz_ / static_cast<float> (sampleRate_);
    const float cw = std::cos (w0), sw = std::sin (w0);
    const float al = sw / (2.0f * 0.70710678f);
    const float a0 = 1.0f + al;
    dfHpB_[0] = ((1.0f + cw) * 0.5f) / a0;
    dfHpB_[1] = -(1.0f + cw) / a0;
    dfHpB_[2] = ((1.0f + cw) * 0.5f) / a0;
    dfHpA_[0] = (-2.0f * cw) / a0;
    dfHpA_[1] = (1.0f - al) / a0;
    for (int c = 0; c < 2; ++c)
        for (int st = 0; st < 2; ++st)
            dfHpZ_[c][st][0] = dfHpZ_[c][st][1] = 0.0f;
}

void DuskVerbEngine::setOutputDiffusion (bool enable, float amount, float lfoScale, float delayScale)
{
    outDiffActive_ = enable;
    if (enable)
    {
        outputDiffusion_.setDelayScale (delayScale);   // re-prepares stages
        outputDiffusion_.setDiffusion (amount);
        outputDiffusion_.setLfoDepthScale (lfoScale);
    }
}

void DuskVerbEngine::setFDNBaseDelays (const int* delays)
{
    // Phase β: per-preset FDN base-delay set. Only the FDN engine has a
    // 16-line tank; other engines ignore. Pass nullptr → preserve engine
    // default (kDefaultDelays — log-spaced primes 1151..6451 samples).
    if (delays == nullptr) return;
    fdn_.setBaseDelays (delays); accurateHall_.setBaseDelays (delays);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setBaseDelays (delays); });
}

void DuskVerbEngine::resetFDNBaseDelays()
{
    fdn_.resetBaseDelays(); accurateHall_.resetBaseDelays();
    multibandFdn_.forEachTank ([](FDNReverb& tk){ tk.resetBaseDelays(); });
}

void DuskVerbEngine::setTiledRoomVoicing (float erSize, float onsetMs, float erDecayMs,
                                          float burst2Ms, float sparseTailGain, float erGain)
{
    // Composite voicing: sparse ER front-end + the ER/tail mix. The 16-line
    // AccurateHall tail is configured separately via the preset's octave-T60 map
    // + decay/mod (frozen). erGain backs off the ER level for less-front-loaded
    // rooms (Tiled Room = 1.0; Medium Drum Room < 1.0 to hit first50 44.8%).
    setSparseFieldSize     (erSize);
    setSparseFieldOnsetMs  (onsetMs);
    setSparseFieldDecayMs  (erDecayMs);
    setSparseFieldBurst2Ms (burst2Ms);
    setSparseFieldTailGain (sparseTailGain);
    setSparseERGain        (erGain);
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
    resetFDNBaseDelays();
    // Tank-feed EQ → neutral shelves (0 dB → branch skipped → bit-identical).
    setTankFeedEQ (200.0f, 0.0f, 2500.0f, 0.0f);
    // Dattorro density-AP jitter → engine default (0.02).
    setDattorroDensityJitter (0.02f);
    // In-loop mode notch → off.
    setDattorroModeNotch (0.0f, 0.0f, 8.0f);
    // Output diffusion → disabled. applyEngineConfig's else-branch covers the
    // with-preset path, but the null-preset swap calls THIS alone — without it,
    // a prior preset's post-tank diffusion (e.g. Bright Hall) would leak.
    setOutputDiffusion (false, 0.0f, 0.0f, 1.0f);

    // ── Name-keyed engine stages (2026-06-23 review fix). applyEngineConfig
    //    self-neutralizes these on the WITH-preset path (fall-through defaults /
    //    map-miss), but the null/unknown-preset swap reaches them ONLY here. Without
    //    these resets, restoring a no-identity session (old/renamed preset) onto a
    //    REUSED engine instance leaks the prior preset's voicing/decay (e.g. Bright
    //    Hall's match-EQ + octave-T60 + tail buildup applied to the restored session). ──
    { const float flat9[9] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
      setOutputMatchEQ (flat9); }                                  // output match-EQ → flat
    setOutputAirShelf (8000.0f, 0.0f);                             // output air-shelf → inactive (bit-null)
    setOutputLowShelf (60.0f, 0.0f);                               // output low-shelf → inactive (bit-null)
    for (int b = 0; b < 9; ++b)                                    // per-octave T60 GEQ → inactive (legacy 3-band)
    { setDattorroOctaveT60 (b, 0.0f); setDenseHallOctaveT60 (b, 0.0f); setAccurateHallOctaveT60 (b, 0.0f);
      setDattorroTonalCorrDb (b, 0.0f); }                          // per-octave tonal-corr → 0 dB (identity)
    setDattorroOctaveDecayRef (0.0f); setDenseHallOctaveDecayRef (0.0f); setAccurateHallOctaveDecayRef (0.0f);
    setDenseHallTonalCorrection (false);                           // DenseHall Jot decouple → off
    setDattorroDensity (0.0f); setDattorroModReduction (1.0f); setDattorroInputDiffusion (0.0f);
    setDattorroDensityRoomFill (false); setDattorroMainLineDetune (1.0f, 1.0f, 1.0f, 1.0f);
    setDattorroSoftOnsetMs (0.0f); setDattorroBloomAttackMs (0.0f);
    setDattorroStereoInput (0.0f); setQuadStereoInput (0.0f); setSparseStereoInput (0.0f);
    setReflectionTap (0.0f, 0.0f); setTankOnsetMs (0.0f);          // discrete tap + tank-onset → off
    setEarlyTapBank  (nullptr, nullptr, 0);                        // tap bank → off (count 0 never dereferences)
    setTiledRoomVoicing (1.0f, 14.0f, 55.0f, 115.0f, 0.45f, 1.0f); // SparseEarlyField voicing → engine defaults
    setSparseFieldBurst2Gain (0.0f);
    setDiffuseER (nullptr, nullptr, 0, 0.6f, 1.0f);   // diffused discrete-ER → inactive (bit-null)
    setBuildupAmount (0.0f); setBuildupTimeScale (1.0f); setBuildupPostTank (false);  // DenseHall tail buildup → bypass
    setPostSteerProfile (0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                         0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    setPostSteerPanRotation (0.0f);
    setPostSteerHardRightMirror (false);
    setPostSteerWander (0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    setPostSteer (0.0f);                                         // stereo-image calibration → off
}

void DuskVerbEngine::setFDNInLoopPeaking (float freqHz, float qFactor, float gainDb)
{
    // Phase ε: only FDN has in-loop per-line peaking infrastructure.
    fdn_.setInLoopPeaking (freqHz, qFactor, gainDb); accurateHall_.setInLoopPeaking (freqHz, qFactor, gainDb);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setInLoopPeaking (freqHz, qFactor, gainDb); });
}

void DuskVerbEngine::setFDNTimeVaryingHiDamp (float earlyMult, float lateMult,
                                              float crossoverHz, float releaseSec,
                                              float refLevel)
{
    // Phase 3 (VH->0): FDN-only per-line energy-following hi-shelf.
    fdn_.setTimeVaryingHiDamp (earlyMult, lateMult, crossoverHz, releaseSec, refLevel); accurateHall_.setTimeVaryingHiDamp (earlyMult, lateMult, crossoverHz, releaseSec, refLevel);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){
        tk.setTimeVaryingHiDamp (earlyMult, lateMult, crossoverHz, releaseSec, refLevel); });
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
    fdn_.setDualBassShelf (fastFc, slowFc, fastGainDb, slowGainDb, transitionMs); accurateHall_.setDualBassShelf (fastFc, slowFc, fastGainDb, slowGainDb, transitionMs);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setDualBassShelf (fastFc, slowFc, fastGainDb, slowGainDb, transitionMs); });
}

void DuskVerbEngine::setModRate (float hz)
{
    dattorro_.setModRate (hz);
    sixAPTank_.setModRate (hz);
    quad_.setModRate (hz);
    fdn_.setModRate (hz); accurateHall_.setModRate (hz);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setModRate (hz); });
    spring_.setModRate (hz);
    nonLinear_.setModRate (hz);
    shimmer_.setModRate (hz);       // hijacked → FEEDBACK (0.1..10 Hz → 0..0.95 cascade gain)
    dattorroVintage_.setModRate (hz);

    reverseRoom_.setModRate (hz);
    denseHall_.setModRate (hz);
}

// Shimmer octave-DOWN voice level (the warm low). Shimmer engine only; 0 = bit-null.
void DuskVerbEngine::setShimmerDownOctaveMix (float mix) { shimmer_.setDownOctaveMix (mix); }
void DuskVerbEngine::setShimmerSubOctaveMix  (float mix) { shimmer_.setSubOctaveMix  (mix); }
void DuskVerbEngine::setShimmerFeedbackHpfHz (float hz)  { shimmer_.setFeedbackHpfHz (hz); }
void DuskVerbEngine::setShimmerStereoMod     (float hz, float d) { shimmer_.setStereoMod (hz, d); }
void DuskVerbEngine::setShimmerHFAir         (float mix) { shimmer_.setHFAir (mix); }
void DuskVerbEngine::setShimmerUseDenseReverb (bool on)  { shimmer_.setUseDenseReverb (on); }
void DuskVerbEngine::setShimmerUseTailSpin    (bool on)  { shimmer_.setUseTailSpin (on); }
void DuskVerbEngine::setShimmerUpVoiceScale   (float v1, float v2) { shimmer_.setUpVoiceScale (v1, v2); }
void DuskVerbEngine::setShimmerOctaveCascade  (const float gains[4]) { shimmer_.setOctaveCascade (gains); }
void DuskVerbEngine::setShimmerTailNoise      (float gain, float hpHz, float lpHz) { shimmer_.setTailNoise (gain, hpHz, lpHz); }
void DuskVerbEngine::setShimmerHFSustainDb    (float db, float cornerHz) { shimmer_.setHFSustainDb (db, cornerHz); }
void DuskVerbEngine::setShimmerOutputHeadroom (float h) { shimmer_.setOutputHeadroom (h); }

// Tail Spin/Wander (post-loop output AM) exists only on the FDN-based engines.
// Forward to the FDN tank and to ReverseRoom (which owns an FDN for its tail);
// the other engines have no such stage.
void DuskVerbEngine::setTailSpinDepth (float depth)
{
    fdn_.setTailSpinDepth (depth); accurateHall_.setTailSpinDepth (depth);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setTailSpinDepth (depth); });
    reverseRoom_.setTailSpinDepth (depth);
}

void DuskVerbEngine::setTailSpinRate (float hz)
{
    fdn_.setTailSpinRate (hz); accurateHall_.setTailSpinRate (hz);
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
    fdn_.setTankDiffusion         (amount); accurateHall_.setTankDiffusion (amount);
    multibandFdn_.forEachTank ([&](FDNReverb& tk){ tk.setTankDiffusion (amount); });
    spring_.setTankDiffusion      (amount);
    nonLinear_.setTankDiffusion   (amount);
    shimmer_.setTankDiffusion     (amount);   // no-op (FDN's mix IS the diffusion)
    dattorroVintage_.setTankDiffusion (amount);
    // VintageTank: route APVTS "Diffusion" knob to the input-AP coefficient
    // (per user spec). Tank-loop AP coefficient stays at preset default to
    // preserve the lush figure-8 modal density.

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

void DuskVerbEngine::setERStereoNeutral (bool enabled)
{
    er_.setStereoNeutral (enabled);
}

void DuskVerbEngine::setERDecorr (float coeff)
{
    er_.setDecorrCoeff (coeff);
}

void DuskVerbEngine::setOutputCrossTalk (float depth)
{
    xtalkDepth_  = std::clamp (depth, 0.0f, 1.0f);
    xtalkActive_ = xtalkDepth_ > 1.0e-6f;
}

void DuskVerbEngine::setWidthBands (float low, float mid, float hi)
{
    widthBandLow_ = std::clamp (low, 0.0f, 2.0f);
    widthBandMid_ = std::clamp (mid, 0.0f, 2.0f);
    widthBandHi_  = std::clamp (hi,  0.0f, 2.0f);
    // Active only when at least one band departs from unity — otherwise the
    // bit-identical legacy side*width path runs (fleet bit-null guarantee).
    bandWidthActive_ = std::abs (widthBandLow_ - 1.0f) > 1.0e-6f
                    || std::abs (widthBandMid_ - 1.0f) > 1.0e-6f
                    || std::abs (widthBandHi_  - 1.0f) > 1.0e-6f;
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

}

void DuskVerbEngine::setPostTankEQBand (int index, float freqHz, float qFactor, float gainDb)
{
    postTankEQ_.setBand (index, freqHz, qFactor, gainDb);
}

void DuskVerbEngine::setDattorroDensityJitter (float fraction)
{
    dattorro_.setDensityJitter (fraction);
}

void DuskVerbEngine::setDattorroModeNotch (float hz, float cutDb, float q)
{
    dattorro_.setModeNotch (hz, cutDb, q);
}

void DuskVerbEngine::setDattorroDensity (float depth01)
{
    dattorro_.setDensityDepth (depth01);
    dattorroVintage_.setDensityDepth (depth01);
}

void DuskVerbEngine::setDattorroModReduction (float reduction01)
{
    dattorro_.setModReduction (reduction01);
    dattorroVintage_.setModReduction (reduction01);
}

// #87 boing fix — DattorroTank (algo 0) only; the short-room rooms are algo 0.
// dattorroVintage_ (algo 1) is intentionally NOT touched → algo-1 presets bit-null.
void DuskVerbEngine::setDattorroDensityRoomFill (bool enable)
{
    dattorro_.setDensityRoomFill (enable);
}

void DuskVerbEngine::setDattorroMainLineDetune (float l1, float l2, float r1, float r2)
{
    dattorro_.setMainLineDetune (l1, l2, r1, r2);
}

void DuskVerbEngine::setDattorroInputDiffusion (float scale01)
{
    dattorro_.setInputDiffusionScale (scale01);
    dattorroVintage_.setInputDiffusionScale (scale01);
}

void DuskVerbEngine::setDattorroSoftOnsetMs (float ms)
{
    dattorro_.setSoftOnsetMs (ms);
    dattorroVintage_.setSoftOnsetMs (ms);
}

void DuskVerbEngine::setDattorroOctaveT60 (int band, float seconds)
{
    dattorro_.setOctaveT60 (band, seconds);
    dattorroVintage_.setOctaveT60 (band, seconds);
}

void DuskVerbEngine::setDattorroOctaveDecayRef (float seconds)
{
    dattorro_.setOctaveDecayRef (seconds);
    dattorroVintage_.setOctaveDecayRef (seconds);
}

void DuskVerbEngine::setDattorroTonalCorrDb (int band, float dB)
{
    dattorro_.setTonalCorrDb (band, dB);
    dattorroVintage_.setTonalCorrDb (band, dB);
}

void DuskVerbEngine::setDattorroBloomAttackMs (float ms)
{
    dattorro_.setBloomAttackMs (ms);
    dattorroVintage_.setBloomAttackMs (ms);
}

void DuskVerbEngine::setDattorroBloomExp (float e)
{
    dattorro_.setBloomExp (e);
    dattorroVintage_.setBloomExp (e);
}

void DuskVerbEngine::setTankFeedEQ (float lowFc, float lowGainDb, float highFc, float highGainDb)
{
    tankFeedLowFc_    = std::clamp (lowFc,  20.0f, 2000.0f);
    tankFeedHighFc_   = std::clamp (highFc, 500.0f, 16000.0f);
    tankFeedLowGain_  = std::pow (10.0f, std::clamp (lowGainDb,  -24.0f, 6.0f) / 20.0f);
    tankFeedHighGain_ = std::pow (10.0f, std::clamp (highGainDb, -24.0f, 6.0f) / 20.0f);
    recomputeTankFeedCoeffs();
    tankFeedActive_ = std::abs (lowGainDb) > 0.01f || std::abs (highGainDb) > 0.01f;
    if (! tankFeedActive_)
        tfLowStateL_ = tfLowStateR_ = tfHighStateL_ = tfHighStateR_ = 0.0f;
}

// One-pole shelf coeffs from the stored corner freqs at the current sampleRate_.
// Called by setTankFeedEQ and by prepare() so an SR change can't leave them
// stale (single source of truth — no duplicated formula).
void DuskVerbEngine::recomputeTankFeedCoeffs()
{
    const float sr = static_cast<float> (sampleRate_);
    tankFeedLowCoeff_  = std::exp (-2.0f * 3.14159265f * tankFeedLowFc_  / sr);
    tankFeedHighCoeff_ = std::exp (-2.0f * 3.14159265f * tankFeedHighFc_ / sr);
}

void DuskVerbEngine::setERBusShelves (float lowGainDb, float highGainDb)
{
    // Fixed corners chosen from the measured ER tilt: low-shelf lifts the weak
    // sub/low-mid (ER is −9..−11 dB there vs its 500-1k peak); high-shelf lifts
    // the rolled-off top (−8..−16 dB). 0 dB → unity → bit-identical bypass.
    erBusLowShelf_.setShelf  (400.0f,  lowGainDb);
    erBusHighShelf_.setShelf (3000.0f, highGainDb);
}

void DuskVerbEngine::setTankOutputLevel (float level)
{
    tankOutLevel_ = std::clamp (level, 0.0f, 2.0f);
}

void DuskVerbEngine::setTankSplitHz (float hz)
{
    tankSplitHz_ = std::max (0.0f, hz);
    if (tankSplitHz_ > 0.0f)
    {
        const float fc = std::clamp (tankSplitHz_, 20.0f, 0.49f * static_cast<float> (sampleRate_));
        tankSplitCoeff_ = 1.0f - std::exp (-6.283185307179586f * fc / static_cast<float> (sampleRate_));
    }
    else
        tankSplitCoeff_ = 0.0f;
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
    // (A VintageTank damping-LP cutoff was derived here from `clamped` but never
    // applied — dead calculation removed. VintageTank is hidden / used by no
    // shipping preset, so there's no routing target; re-add wiring if that
    // engine is reinstated.)
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
void DuskVerbEngine::setDpvFrontLoad (float erGain, float predelayMs, float tapMs, float lpHz)
{
    dattorroVintage_.setFrontLoad (erGain, predelayMs, tapMs, lpHz);
}

void DuskVerbEngine::setDpvPostMainTap (float ms, float gain, float lpHz)
{
    dattorroVintage_.setPostMainTap (ms, gain, lpHz);
}

void DuskVerbEngine::setDpvDenseField (float gain, float predelayMs, float t60Ms)
{
    dattorroVintage_.setDenseField (gain, predelayMs, t60Ms);
}

void DuskVerbEngine::setDattorroDenseField (float gain, float predelayMs, float t60Ms)
{
    dattorroDenseField_.setParams (gain, predelayMs, t60Ms);
}

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

void DuskVerbEngine::setOutputAirShelf (float freqHz, float gainDb)
{
    const float f  = std::clamp (freqHz, 1000.0f, 0.49f * static_cast<float> (sampleRate_));
    const float dB = std::clamp (gainDb, -18.0f, 18.0f);
    if (f == airShelfFreqHz_ && dB == airShelfGainDb_)
        return;
    airShelfFreqHz_ = f;
    airShelfGainDb_ = dB;
    airShelfActive_ = std::abs (dB) > 0.01f;
    updateAirShelfCoeffs();
}

void DuskVerbEngine::updateAirShelfCoeffs()
{
    // RBJ 2nd-order high-SHELF at Q = 1/√2 — identical form to updateHiCutCoeffs
    // but the gain is NOT sign-clamped, so positive dB BOOSTS the air band. This
    // stage is post-tank / feed-forward (NOT inside the FDN feedback loop), so a
    // shelf with |H| > 1 is unconditionally stable — the loop-stability |H| < 1
    // constraint that keeps the in-loop GEQ and hiCutFilter_ cut-only does not
    // apply here. Inactive (0 dB) → unity coeffs + cleared state → bit-null.
    if (! airShelfActive_)
    {
        airShelfFilter_ = Biquad{};
        return;
    }
    const float fs    = static_cast<float> (sampleRate_);
    const float A     = std::pow (10.0f, airShelfGainDb_ / 40.0f);
    const float sqrtA = std::sqrt (A);
    const float w0    = kTwoPi * airShelfFreqHz_ / fs;
    const float cosw  = std::cos (w0);
    const float sinw  = std::sin (w0);
    const float alpha = sinw / (2.0f * 0.7071067811865475f);
    const float twoSqrtAalpha = 2.0f * sqrtA * alpha;
    const float Aplus1  = A + 1.0f;
    const float Aminus1 = A - 1.0f;
    const float a0      = Aplus1 - Aminus1 * cosw + twoSqrtAalpha;

    // Preserve the running filter state across a coeff redesign (preset apply on
    // the message thread) — only b/a are rewritten, z1/z2 carry over.
    airShelfFilter_.b0 =  A * (Aplus1 + Aminus1 * cosw + twoSqrtAalpha) / a0;
    airShelfFilter_.b1 = -2.0f * A * (Aminus1 + Aplus1 * cosw)          / a0;
    airShelfFilter_.b2 =  A * (Aplus1 + Aminus1 * cosw - twoSqrtAalpha) / a0;
    airShelfFilter_.a1 =  2.0f * (Aminus1 - Aplus1 * cosw)              / a0;
    airShelfFilter_.a2 =        (Aplus1 - Aminus1 * cosw - twoSqrtAalpha) / a0;
}

void DuskVerbEngine::setOutputLowShelf (float freqHz, float gainDb)
{
    const float f  = std::clamp (freqHz, 20.0f, 500.0f);
    const float dB = std::clamp (gainDb, -18.0f, 18.0f);
    if (f == lowShelfFreqHz_ && dB == lowShelfGainDb_)
        return;
    lowShelfFreqHz_ = f;
    lowShelfGainDb_ = dB;
    lowShelfActive_ = std::abs (dB) > 0.01f;
    updateLowShelfCoeffs();
}

void DuskVerbEngine::updateLowShelfCoeffs()
{
    // RBJ 2nd-order low-SHELF at Q = 1/√2 — the deep-sub counterpart of the air-
    // shelf. Positive dB BOOSTS the 20-60Hz octave. Post-tank / feed-forward, so a
    // boost shelf is unconditionally stable. Inactive (0 dB) → unity coeffs + clear.
    if (! lowShelfActive_)
    {
        lowShelfFilter_ = Biquad{};
        return;
    }
    const float fs    = static_cast<float> (sampleRate_);
    const float A     = std::pow (10.0f, lowShelfGainDb_ / 40.0f);
    const float sqrtA = std::sqrt (A);
    const float w0    = kTwoPi * lowShelfFreqHz_ / fs;
    const float cosw  = std::cos (w0);
    const float sinw  = std::sin (w0);
    const float alpha = sinw / (2.0f * 0.7071067811865475f);
    const float twoSqrtAalpha = 2.0f * sqrtA * alpha;
    const float Aplus1  = A + 1.0f;
    const float Aminus1 = A - 1.0f;
    const float a0      = Aplus1 + Aminus1 * cosw + twoSqrtAalpha;

    lowShelfFilter_.b0 =  A * (Aplus1 - Aminus1 * cosw + twoSqrtAalpha) / a0;
    lowShelfFilter_.b1 =  2.0f * A * (Aminus1 - Aplus1 * cosw)          / a0;
    lowShelfFilter_.b2 =  A * (Aplus1 - Aminus1 * cosw - twoSqrtAalpha) / a0;
    lowShelfFilter_.a1 = -2.0f * (Aminus1 + Aplus1 * cosw)              / a0;
    lowShelfFilter_.a2 =        (Aplus1 + Aminus1 * cosw - twoSqrtAalpha) / a0;
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
        sourceSide_[static_cast<size_t> (i)] =
            0.5f * (tankInL_[static_cast<size_t> (i)] - tankInR_[static_cast<size_t> (i)]);

        // FORK A — snapshot the CLEAN post-predelay dry mono here, BEFORE the input
        // diffuser / tank-feed EQ mutate tankIn. The reflection tap reads this so it's
        // a clean discrete arrival, not the smeared/diffused tank input.
        reflDryMono_[static_cast<size_t> (i)] = 0.5f * (tankInL_[static_cast<size_t> (i)] + tankInR_[static_cast<size_t> (i)]);

        preDelayWritePos_ = (preDelayWritePos_ + 1) & preDelayMask_;
    }

    // ---- 1b) Sustained-energy wet-input duck (SustainBandLimiter.h) ----
    // Placed on the POST-predelay wet feed BEFORE the ER/tank split, because the
    // sine1k steady-state excess was measured to be ~90% FIRST-PASS throughput
    // (diffusers → pass-1 → taps, + the ER bus) — killing the entire recirculation
    // (Decay 0.1 s) moved the sine only 0.2-1.2 dB, so no in-loop mechanism can
    // reach it. Ducking the wet FEED scales first-pass + recirculation + ER
    // together (full sine1k reach), reduces piano-stem charge accumulation
    // equivalently (less injection = less charge = smaller tail), and is
    // inherently T60-safe: the release decay is a loop property, injection is
    // irrelevant after input-off. Detector is same-sample (feed-forward site).
    // Inactive → block skipped → bit-null.
    if (wetSusLimMidCfg_.active || wetSusLimLowCfg_.active)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float xl = tankInL_[static_cast<size_t> (i)];
            float xr = tankInR_[static_cast<size_t> (i)];
            const bool present = wetSusLimKey_.advance (std::fabs (xl) + std::fabs (xr));
            if (wetSusLimMidCfg_.active)
            {
                const float bl = wetSusLimMidCutL_.band (xl);
                const float br = wetSusLimMidCutR_.band (xr);
                const float red = wetSusLimMidDet_.advanceUnit (0.5f * (std::fabs (bl) + std::fabs (br)) * wetSusLimMidCfg_.detNorm,
                                                                wetSusLimMidCfg_, present);
                xl -= red * bl;
                xr -= red * br;
            }
            if (wetSusLimLowCfg_.active)
            {
                const float bl = wetSusLimLowCutL_.band (xl);
                const float br = wetSusLimLowCutR_.band (xr);
                const float red = wetSusLimLowDet_.advanceUnit (0.5f * (std::fabs (bl) + std::fabs (br)) * wetSusLimLowCfg_.detNorm,
                                                                wetSusLimLowCfg_, present);
                xl -= red * bl;
                xr -= red * br;
            }
            tankInL_[static_cast<size_t> (i)] = xl;
            tankInR_[static_cast<size_t> (i)] = xr;
        }
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
    //   • DenseHall — owns its own 10-stage input allpass diffusion per channel
    if (currentEngine_ != EngineType::SixAPTank
        && currentEngine_ != EngineType::Spring
        && currentEngine_ != EngineType::NonLinear
        && currentEngine_ != EngineType::DattorroVintage
        && currentEngine_ != EngineType::ReverseRoom
        && currentEngine_ != EngineType::DenseHall)
        diffuser_.process (tankInL_.data(), tankInR_.data(), numSamples);

    // ---- 3b) Tank-feed EQ (Progenitor inputdamp) ----
    // Tilts ONLY the tank feed (the ER branch tapped the signal at step 2).
    // Feed-forward: loop poles / per-band T60 untouched; the recirculating
    // field is darker from its first pass. Skipped at 0 dB → bit-identical.
    if (tankFeedActive_)
    {
        const float gL = tankFeedLowGain_  - 1.0f;   // shelf deltas
        const float gH = tankFeedHighGain_ - 1.0f;
        const float cL = tankFeedLowCoeff_, cH = tankFeedHighCoeff_;
        for (int i = 0; i < numSamples; ++i)
        {
            float xl = tankInL_[static_cast<size_t> (i)];
            float xr = tankInR_[static_cast<size_t> (i)];
            // Low shelf: y = x + (g-1)·LP(x)
            tfLowStateL_ = (1.0f - cL) * xl + cL * tfLowStateL_;
            tfLowStateR_ = (1.0f - cL) * xr + cL * tfLowStateR_;
            xl += gL * tfLowStateL_;
            xr += gL * tfLowStateR_;
            // High shelf: y = x + (g-1)·(x - LP(x))
            tfHighStateL_ = (1.0f - cH) * xl + cH * tfHighStateL_;
            tfHighStateR_ = (1.0f - cH) * xr + cH * tfHighStateR_;
            xl += gH * (xl - tfHighStateL_);
            xr += gH * (xr - tfHighStateR_);
            tankInL_[static_cast<size_t> (i)] = xl;
            tankInR_[static_cast<size_t> (i)] = xr;
        }
    }

    // ---- 4) Selected late tank ----
    switch (currentEngine_)
    {
        case EngineType::Dattorro:
            dattorro_.process (tankInL_.data(), tankInR_.data(),
                               tankOutL_.data(), tankOutR_.data(), numSamples,
                               sourceSide_.data());
            // Dense early-field: predelayed dry-mono → compact Schroeder reverb,
            // summed POST-tank to fill the thin post-onset shelf of the short rooms.
            // Off (gain 0) → skipped entirely → tankOut byte-identical (bit-null).
            // Fed 0 when frozen so the field decays out with the held tank.
            if (dattorroDenseField_.active())
            {
                for (int i = 0; i < numSamples; ++i)
                {
                    // Drive from the CLEAN dry-mono snapshot (captured before the
                    // diffuser + tank-feed EQ mutate tankIn), not the processed
                    // tank input, so the dense field is a defined discrete feed.
                    const float xin = frozen_ ? 0.0f
                        : reflDryMono_[static_cast<size_t> (i)];
                    float dl = 0.0f, dr = 0.0f;
                    dattorroDenseField_.processSample (xin, dl, dr);
                    tankOutL_[static_cast<size_t> (i)] += dl;
                    tankOutR_[static_cast<size_t> (i)] += dr;
                }
            }
            // Early-Field composite (opt-in, 2026-07-06) — the velvet SparseEarlyField
            // ER owns the early window while the tank tail is delayed behind it: the
            // same Phase-A composite as DenseHall (1637), ported to the Dattorro rooms
            // whose hard tank onset + missing ~10 ms first reflection no preset param
            // can fix (Small Drum Room). Gated by dattorroEarlyFieldOn_ (NOT
            // sparseERGain_, which the unmapped-preset reset pins to 1.0) → Drum Plate /
            // Vintage Gold Plate skip it entirely → tankOut byte-identical. Verified
            // 2026-07-06: block compiled in vs out → all 7 Drum Plate stimuli
            // sample-level identical (maxΔ 0.0) — it's post-tank, outside the recursive
            // loop, so trap #2's FP-feedback drift does not apply.
            if (dattorroEarlyFieldOn_)
                applyEarlyFieldComposite (numSamples);
            break;
        case EngineType::SixAPTank:
            sixAPTank_.process (tankInL_.data(), tankInR_.data(),
                                  tankOutL_.data(), tankOutR_.data(), numSamples);
            break;
        case EngineType::QuadTank:
            quad_.process (tankInL_.data(), tankInR_.data(),
                           tankOutL_.data(), tankOutR_.data(), numSamples,
                           sourceSide_.data());
            // FRONT-LOAD REDESIGN (2026-06-18): sparse velvet ER front-end + reduced
            // tank — the same composite as algo 11/13/14, which front-loads the early
            // field (the defined early arrival the FDN/QuadTank washy swell lacks →
            // "cloudy snare"). 79 Vocal Chamber is the LONE QuadTank preset; voiced
            // via kCompositeERByName + setTiledRoomVoicing (which sets sparseTailGain/
            // sparseERGain + the sparse field). The velvet field is the DEFINED early
            // arrival; the QuadTank tank is the late body (sparseTailGain × it).
            sparseField_.process (tankInL_.data(), tankInR_.data(),
                                  sparseOutL_.data(), sparseOutR_.data(), numSamples,
                                  sourceSide_.data());
            applyEarlyTankDuck (numSamples);
            for (int i = 0; i < numSamples; ++i)
            {
                tankOutL_[static_cast<size_t> (i)] =
                    tankOutL_[static_cast<size_t> (i)] * sparseTailGain_ + sparseOutL_[static_cast<size_t> (i)] * sparseERGain_;
                tankOutR_[static_cast<size_t> (i)] =
                    tankOutR_[static_cast<size_t> (i)] * sparseTailGain_ + sparseOutR_[static_cast<size_t> (i)] * sparseERGain_;
            }
            break;
        case EngineType::FDN:
            // Opt-in multiband (3 band-isolated tanks) when enabled, else the
            // single legacy tank. Hidden engine (no preset) but kept — the
            // FiveBand/multiband params still route here.
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
            // VintageTank engine removed 2026-06-13 (no factory preset). Old saved
            // sessions on this index fall back to AccurateHall.
            accurateHall_.process (tankInL_.data(), tankInR_.data(),
                                   tankOutL_.data(), tankOutR_.data(), numSamples);
            break;
        case EngineType::ReverseRoom:
            reverseRoom_.process (tankInL_.data(), tankInR_.data(),
                                  tankOutL_.data(), tankOutR_.data(), numSamples);
            break;
        case EngineType::AccurateHall:
            // P1: identical signal path to FDN (plain FDNReverb, GEQ added P2).
            accurateHall_.process (tankInL_.data(), tankInR_.data(),
                                   tankOutL_.data(), tankOutR_.data(), numSamples);
            // Early-field composite (opt-in, 2026-07-07): the FDN's slow washy onset
            // (~26 ms attack floor) can't reach a sharp front-loaded plate anchor
            // (Vocal Plate: attack 9.3 ms, onset_slope 4.3, first50 49 %, 6 reflections
            // 26-79 ms). The ducked velvet sparse ER supplies that sharp early field on
            // top of the FDN tail. Gated by accurateHallEarlyFieldOn_ → every other
            // AccurateHall preset skips it → tankOut byte-identical (bit-null).
            if (accurateHallEarlyFieldOn_)
                applyEarlyFieldComposite (numSamples);
            break;
        case EngineType::AccurateHall32:
            // 32-line variant removed 2026-06-13 (Bright Hall migrated to DenseHall).
            // Old saved sessions on this index fall back to the 16-line AccurateHall.
            accurateHall_.process (tankInL_.data(), tankInR_.data(),
                                   tankOutL_.data(), tankOutR_.data(), numSamples);
            break;
        case EngineType::SparseField:
        case EngineType::TiledRoom:
            // COMPOSITE: sparse tapped-delay ER front-end (owns the front-loaded
            // first ~50 ms) + the mature 16-line AccurateHall octave-GEQ tail
            // (dense -> no flutter; per-octave GEQ -> correct bright-early/dark-
            // late). sparseTailGain_ sets the tail/ER balance. The two stages are
            // independent, sidestepping the FDN's inseparable early-wash/late-body.
            //   - SparseField (algo 11): hall-voiced ER.
            //   - TiledRoom  (algo 13): tight-room ER + dark, frozen tail (the
            //     4-line hand-rolled tail was a kill-test: it conquered the front-
            //     load but a sparse tail flutters [osc P2P +39] + inverts the
            //     spectrum [cent] -> 39 fails. The 16-line FDN tail fixes both).
            // Both make their own early field -> excluded from the smooth-ER bus.
            accurateHall_.process (tankInL_.data(), tankInR_.data(),
                                   tankOutL_.data(), tankOutR_.data(), numSamples);
            sparseField_.process (tankInL_.data(), tankInR_.data(),
                                  sparseOutL_.data(), sparseOutR_.data(), numSamples,
                                  sourceSide_.data());
            applyEarlyTankDuck (numSamples);
            for (int i = 0; i < numSamples; ++i)
            {
                tankOutL_[static_cast<size_t> (i)] =
                    tankOutL_[static_cast<size_t> (i)] * sparseTailGain_ + sparseOutL_[static_cast<size_t> (i)] * sparseERGain_;
                tankOutR_[static_cast<size_t> (i)] =
                    tankOutR_[static_cast<size_t> (i)] * sparseTailGain_ + sparseOutR_[static_cast<size_t> (i)] * sparseERGain_;
            }
            break;

        case EngineType::DenseHall:
            // COMPOSITE: DuskVerb's diffused-FDN dense hall tail (primary) + the
            // sparse discrete-ER front (additive). The dense tank already builds
            // its own density/smoothness; sparseField_ supplies the early discrete
            // reflections the anchor halls have. sparseERGain_ sets the ER level
            // (0 -> pure tail). Makes its own early field -> off the smooth-ER bus.
            // Optional BUILDUP: a long allpass cascade makes the dense tail BUILD
            // gradually (quiet early) instead of dense from sample 0 — lets the sparse
            // ER own the early window with its dip + tap (the hall duh-DUH). Two modes:
            //  • PRE-tank (Bright Hall): diffuse the tank INPUT. Strongest build, but it
            //    alters the recirculating signal — fine for a smaller hall.
            //  • POST-tank (Blade Runner): diffuse the tank OUTPUT. Builds the onset
            //    while leaving the recirculation untouched, so T60/decay/spectral stay
            //    intact — needed for a huge hall where the input smear wrecks the tail.
            // Bypassed (amount 0) → tank fed/read directly → bit-null.
            if (buildupDiffuser_.active() && ! buildupPostTank_)
            {
                std::copy (tankInL_.begin(), tankInL_.begin() + numSamples, buildupBufL_.begin());
                std::copy (tankInR_.begin(), tankInR_.begin() + numSamples, buildupBufR_.begin());
                buildupDiffuser_.process (buildupBufL_.data(), buildupBufR_.data(), numSamples);
                denseHall_.process (buildupBufL_.data(), buildupBufR_.data(),
                                    tankOutL_.data(), tankOutR_.data(), numSamples);
            }
            else
            {
                denseHall_.process (tankInL_.data(), tankInR_.data(),
                                    tankOutL_.data(), tankOutR_.data(), numSamples);
                if (buildupDiffuser_.active())   // post-tank: build the OUTPUT (tail/T60 intact)
                    buildupDiffuser_.process (tankOutL_.data(), tankOutR_.data(), numSamples);
            }
            // Phase A early-field: delay the late tail by tankOnsetSamples_ so the
            // (undelayed) sparse ER below owns the early window. 0 → skipped → bit-null.
            if (tankOnsetSamples_ > 0)
            {
                const int sz = static_cast<int> (tankOnsetBufL_.size());
                for (int i = 0; i < numSamples; ++i)
                {
                    int rd = tankOnsetWrite_ - tankOnsetSamples_; if (rd < 0) rd += sz;
                    const float dl = tankOnsetBufL_[static_cast<size_t> (rd)];
                    const float dr = tankOnsetBufR_[static_cast<size_t> (rd)];
                    tankOnsetBufL_[static_cast<size_t> (tankOnsetWrite_)] = tankOutL_[static_cast<size_t> (i)];
                    tankOnsetBufR_[static_cast<size_t> (tankOnsetWrite_)] = tankOutR_[static_cast<size_t> (i)];
                    tankOutL_[static_cast<size_t> (i)] = dl;
                    tankOutR_[static_cast<size_t> (i)] = dr;
                    if (++tankOnsetWrite_ >= sz) tankOnsetWrite_ = 0;
                }
            }
            sparseField_.process (tankInL_.data(), tankInR_.data(),
                                  sparseOutL_.data(), sparseOutR_.data(), numSamples,
                                  sourceSide_.data());
            applyEarlyTankDuck (numSamples);
            if (sparseERDuckAmount_ > 0.0f)
            {
                // Transient-ducked ER (opt-in per DenseHall preset via kCompositeERByName
                // duck fields): fire the discrete reflections on the hit, silent on
                // sustain — closes early_tap/early_refl without the noiseburst-onset
                // cascade (the sustained-release T60/env_p2p gates protect the tail).
                for (int i = 0; i < numSamples; ++i)
                {
                    const float dg = erDuck_.process (reflDryMono_[static_cast<size_t> (i)]);
                    const float g  = sparseERGain_ * ((1.0f - sparseERDuckAmount_) + sparseERDuckAmount_ * dg);
                    tankOutL_[static_cast<size_t> (i)] += sparseOutL_[static_cast<size_t> (i)] * g;
                    tankOutR_[static_cast<size_t> (i)] += sparseOutR_[static_cast<size_t> (i)] * g;
                }
            }
            else
            for (int i = 0; i < numSamples; ++i)
            {
                tankOutL_[static_cast<size_t> (i)] += sparseOutL_[static_cast<size_t> (i)] * sparseERGain_;
                tankOutR_[static_cast<size_t> (i)] += sparseOutR_[static_cast<size_t> (i)] * sparseERGain_;
            }
            // Diffused discrete-ER bus (clarity/un-masking comb): diffuse-then-tap
            // the tank INPUT into discrete smooth reflections, summed on top. Reuses
            // the sparse scratch. Skipped (no reflections) → bit-null.
            if (diffuseER_.active())
            {
                diffuseER_.process (tankInL_.data(), tankInR_.data(),
                                    sparseOutL_.data(), sparseOutR_.data(), numSamples);
                for (int i = 0; i < numSamples; ++i)
                {
                    tankOutL_[static_cast<size_t> (i)] += sparseOutL_[static_cast<size_t> (i)] * diffuseERGain_;
                    tankOutR_[static_cast<size_t> (i)] += sparseOutR_[static_cast<size_t> (i)] * diffuseERGain_;
                }
            }
            break;

        case EngineType::ParallelMultiband:
            // Per-band decoupled tank (pilot). COMPOSITE like DenseHall: the
            // sparse discrete-ER front supplies the early field; the parallel
            // bands own the tail with independent level/decay/EDT/width.
            pmb_.process (tankInL_.data(), tankInR_.data(),
                          tankOutL_.data(), tankOutR_.data(), numSamples);
            sparseField_.process (tankInL_.data(), tankInR_.data(),
                                  sparseOutL_.data(), sparseOutR_.data(), numSamples,
                                  sourceSide_.data());
            applyEarlyTankDuck (numSamples);
            if (sparseERDuckAmount_ > 0.0f)
            {
                // Transient-ducked sparse ER (Small Drum Room): gate the ER by the
                // input's transient-ness (from the CLEAN dry mono, reflDryMono_) so it
                // fires on a hit — closing attack_time/early_refl/early_tap on the snare
                // — but ducks to silence while a tone/noise SUSTAINS, so it adds no
                // steady energy to inflate the noiseburst (→ no gain-match cascade) and
                // no sustained 1 kHz throughput (→ no sine1k pump). The plain additive
                // path (else) is untouched → bit-null on Vocal Hall / every other
                // composite (sparseERDuckAmount_ 0).
                for (int i = 0; i < numSamples; ++i)
                {
                    const float dg = erDuck_.process (reflDryMono_[static_cast<size_t> (i)]);
                    const float g  = sparseERGain_ * ((1.0f - sparseERDuckAmount_) + sparseERDuckAmount_ * dg);
                    tankOutL_[static_cast<size_t> (i)] += sparseOutL_[static_cast<size_t> (i)] * g;
                    tankOutR_[static_cast<size_t> (i)] += sparseOutR_[static_cast<size_t> (i)] * g;
                }
            }
            else
            for (int i = 0; i < numSamples; ++i)
            {
                tankOutL_[static_cast<size_t> (i)] += sparseOutL_[static_cast<size_t> (i)] * sparseERGain_;
                tankOutR_[static_cast<size_t> (i)] += sparseOutR_[static_cast<size_t> (i)] * sparseERGain_;
            }
            // Diffused-burst ER bus, same hookup as the DenseHall composite
            // (2026-07-06: the post-hit "whoosh" the VVV anchor carries lives
            // here — the parallel band tanks alone onset too cleanly).
            // Optionally highpassed (setDiffuseERHighpass) so the whoosh adds
            // NO steady-state energy at/below 1 kHz — an unfiltered bus pumped
            // sine1k +11 dB. Skipped (no reflections configured) → bit-null.
            if (diffuseER_.active())
            {
                diffuseER_.process (tankInL_.data(), tankInR_.data(),
                                    sparseOutL_.data(), sparseOutR_.data(), numSamples);
                if (diffuseERHpHz_ > 0.0f)
                    for (int i = 0; i < numSamples; ++i)
                    {
                        float* smp[2] = { &sparseOutL_[static_cast<size_t> (i)], &sparseOutR_[static_cast<size_t> (i)] };
                        for (int c = 0; c < 2; ++c)
                            for (int st = 0; st < 2; ++st)
                            {
                                const float x = *smp[c];
                                float* z = dfHpZ_[c][st];
                                const float y = dfHpB_[0] * x + z[0];
                                z[0] = dfHpB_[1] * x - dfHpA_[0] * y + z[1];
                                z[1] = dfHpB_[2] * x - dfHpA_[1] * y;
                                *smp[c] = y;
                            }
                    }
                for (int i = 0; i < numSamples; ++i)
                {
                    tankOutL_[static_cast<size_t> (i)] += sparseOutL_[static_cast<size_t> (i)] * diffuseERGain_;
                    tankOutR_[static_cast<size_t> (i)] += sparseOutR_[static_cast<size_t> (i)] * diffuseERGain_;
                }
            }
            break;
    }

    // ---- 4b) Per-preset post-tank OUTPUT diffusion (Bright Hall) ----
    // Smears the FDN's sparse HF tail modes into a dense wash (metallic-ring
    // fix). Post-tank, pre-mix. Skipped entirely when inactive → every other
    // preset's signal path is unchanged (bit-null verified).
    if (outDiffActive_)
        outputDiffusion_.process (tankOutL_.data(), tankOutR_.data(), numSamples);

    // ---- 4c) Phase 3 output match-EQ (per-preset): static per-octave GEQ that
    // shapes the wet steady-state envelope toward the anchor's measured octave
    // balance. Post-tank, pre-mix, engine-agnostic. Skipped → bit-null. ----
    if (matchEQActive_)
        for (int i = 0; i < numSamples; ++i)
        {
            tankOutL_[static_cast<size_t> (i)] = matchEQL_.process (tankOutL_[static_cast<size_t> (i)], matchCoeffs_);
            tankOutR_[static_cast<size_t> (i)] = matchEQR_.process (tankOutR_[static_cast<size_t> (i)], matchCoeffs_);
        }

    // Dynamic low-mid buildup limiter (piano-cluster fix) — cut the sustained low-mid
    // charge the recirculating tanks accumulate above the anchors. Post-tank, before the
    // ER sum. active() false (maxCut 0) → skipped → tankOut byte-identical (bit-null).
    if (dynLowMid_.active())
        for (int i = 0; i < numSamples; ++i)
            dynLowMid_.processSample (tankOutL_[static_cast<size_t> (i)], tankOutR_[static_cast<size_t> (i)]);

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
    // DenseHall RE-INCLUDED 2026-06-16: its 10-stage input allpass is a SMEAR,
    // not discrete reflections — the shared er_ supplies the parallel DISCRETE
    // early taps (the "duh-duh" the anchor halls produce, which the user heard
    // missing on Bright Hall + Blade Runner). The 2026-06-13 DenseHall migration
    // dropped these presets' (already-set) erLevel by excluding them here. The two
    // are complementary (discrete ER + diffuse tail = the anchor's early field),
    // NOT double-ER. Per-preset erLevel re-tuned after this change.
    const bool useSmoothER = (currentEngine_ != EngineType::DattorroVintage
                           && currentEngine_ != EngineType::ReverseRoom
                           && currentEngine_ != EngineType::SparseField
                           && currentEngine_ != EngineType::TiledRoom
                           && currentEngine_ != EngineType::QuadTank);  // 79VC: owns the sparse front-end now
    for (int i = 0; i < numSamples; ++i)
    {
        float erL   = useSmoothER ? erOutL_[static_cast<size_t> (i)] : 0.0f;
        float erR   = useSmoothER ? erOutR_[static_cast<size_t> (i)] : 0.0f;

        // ER-bus spectral correction. Default 0 dB shelves design unity
        // coefficients → bit-identical passthrough (verified bit-null on Drum
        // Plate). Lives on the ER bus only; the tank output is untouched.
        erL = erBusLowShelf_.processL (erL);  erL = erBusHighShelf_.processL (erL);
        erR = erBusLowShelf_.processR (erR);  erR = erBusHighShelf_.processR (erR);

        const float lateL = tankOutL_[static_cast<size_t> (i)];
        const float lateR = tankOutR_[static_cast<size_t> (i)];

        const float erLevel = erLevelSmoother_.next();

        // Phase 4 (option 2): early-field ER boost. erEarlyBoost_ default 1.0f →
        // ×1.0 exact → bit-identical. >1 lets the parallel ER run hot enough to
        // own the 0-26 ms transient (which the FDN tank, floored at ~26 ms by
        // its shortest delay line, structurally cannot supply). Post-tank linear
        // combine, NOT in the recursive loop → no feedback-codegen bit-null risk.
        // tankOutLevel_ defaults 1.0 → ×1.0 exact → bit-identical. <1 rebalances
        // energy from the late tank to the early ER without changing decay RATE.
        // Phase 3: when tankSplitHz_>0, only the MID/HIGH of the tank is scaled
        // (low stays unity → keeps the correlated low + body). splitHz=0 →
        // broadband path below, byte-identical for every non-opting preset.
        float scaledLateL, scaledLateR;
        if (tankSplitHz_ > 0.0f)
        {
            tankSplitLpL_ += tankSplitCoeff_ * (lateL - tankSplitLpL_);
            tankSplitLpR_ += tankSplitCoeff_ * (lateR - tankSplitLpR_);
            scaledLateL = tankSplitLpL_ + (lateL - tankSplitLpL_) * tankOutLevel_;
            scaledLateR = tankSplitLpR_ + (lateR - tankSplitLpR_) * tankOutLevel_;
        }
        else
        {
            scaledLateL = lateL * tankOutLevel_;
            scaledLateR = lateR * tankOutLevel_;
        }
        float wetL = erL * erLevel * erEarlyBoost_ + scaledLateL;
        float wetR = erR * erLevel * erEarlyBoost_ + scaledLateR;

        // FORK A — discrete early reflection ("duh-duh"). Reads the CLEAN pre-diffuser
        // dry (reflDryMono_, snapshot in the pre-delay loop) delayed ~90-110 ms (R
        // offset for width), gently rolled off at 11 kHz, summed to wet — a sharp,
        // bright, defined discrete arrival (NOT the old dark/smeared post-diffuser
        // blob). Whole block gated on reflActive_ (gain 0 → skipped → bit-identical;
        // kReflectionByName is empty/env-only today). Out of the recursive tank → no
        // codegen drift. (Note: a tap can't fix a late-energy/front-load clarity
        // problem — it's a small arrival vs the whole tail; it's for a discrete slap.)
        {
            // Write + advance EVERY sample so the ring history stays warm across
            // presets that disable taps — enabling them mid-flight must not read
            // ~250 ms of stale silence. Inactive presets stay bit-identical: only
            // the guarded reads below touch the wet signal.
            const float dryMono = reflDryMono_[static_cast<size_t> (i)];   // CLEAN pre-diffuser dry (snapshot above)
            reflBuf_[static_cast<size_t> (reflWritePos_)] = dryMono;
            if (reflActive_)
            {
                const float tapL = reflBuf_[static_cast<size_t> ((reflWritePos_ - reflDelayL_) & reflMask_)];
                const float tapR = reflBuf_[static_cast<size_t> ((reflWritePos_ - reflDelayR_) & reflMask_)];
                reflLpStateL_ += reflLpCoeff_ * (tapL - reflLpStateL_);
                reflLpStateR_ += reflLpCoeff_ * (tapR - reflLpStateR_);
                wetL += reflGain_ * reflLpStateL_;
                wetR += reflGain_ * reflLpStateR_;
            }
            // Early-tap BANK: N anchor-timed discrete arrivals, one shared LP pair
            // (real reflections are darker than the direct sound).
            if (etapActive_)
            {
                float sumL = 0.0f, sumR = 0.0f;
                for (int t = 0; t < etapCount_; ++t)
                {
                    sumL += etapGain_[t] * reflBuf_[static_cast<size_t> ((reflWritePos_ - etapDelayL_[t]) & reflMask_)];
                    sumR += etapGain_[t] * reflBuf_[static_cast<size_t> ((reflWritePos_ - etapDelayR_[t]) & reflMask_)];
                }
                etapLpStateL_ += etapLpCoeff_ * (sumL - etapLpStateL_);
                etapLpStateR_ += etapLpCoeff_ * (sumR - etapLpStateR_);
                // Transient duck: on a hit the taps fire (reproduce the anchor's
                // discrete reflection pattern); on sustain they close so the
                // feed-forward tap sum can't pump the steady-state spectrum
                // (sine1k/ripple). Keyed off the CLEAN dry-mono (same source the
                // taps read), so the gate tracks the input onset, not the tail.
                // amount 0 → g==1 → byte-identical to the un-ducked tap path.
                float g = 1.0f;
                if (etapDuckAmount_ > 0.0f)
                {
                    const float dg = etapDuck_.process (dryMono);
                    g = (1.0f - etapDuckAmount_) + etapDuckAmount_ * dg;
                }
                wetL += g * etapLpStateL_;
                wetR += g * etapLpStateR_;
            }
            reflWritePos_ = (reflWritePos_ + 1) & reflMask_;
        }

        wetL = loCutFilter_.processL (wetL);
        wetR = loCutFilter_.processR (wetR);
        wetL = hiCutFilter_.processL (wetL);
        wetR = hiCutFilter_.processR (wetR);

        // Output air-shelf (HF voicing boost/cut). Guarded → skipped entirely
        // when inactive (0 dB) so the whole fleet stays bit-identical. Post-tank
        // feed-forward, so a boost shelf is stable.
        if (airShelfActive_)
        {
            wetL = airShelfFilter_.processL (wetL);
            wetR = airShelfFilter_.processR (wetR);
        }
        // Output low-shelf (LF "fullness" boost/cut). Guarded → bit-null when 0 dB.
        if (lowShelfActive_)
        {
            wetL = lowShelfFilter_.processL (wetL);
            wetR = lowShelfFilter_.processR (wetR);
        }

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

        // ---- Post-tank stereo image steer (issue #123) ----
        // Constant-power L/R gain tilt on the FINAL wet, keyed off the CLEAN dry input
        // still held in left[i]/right[i] here (the engine writes wet back only at the
        // end of this loop; the pre-delay read and sus-limiter mutate their OWN buffers,
        // never left/right — verified). Applied HERE, downstream of every engine and
        // BEFORE the Mono Maker / width M/S stage, so the imposed ILD is then shaped by
        // width exactly like any other stereo content. Engine-agnostic: works even on
        // the mono-in tanks (Dattorro) where output-tap surgery cannot lean the image.
        //
        // Detector: per-sample amplitude-envelope followers on |dryL|,|dryR| (attack/
        // release one-poles), balance b=(EL-ER)/(EL+ER+eps) clamped [-1,1]. Steer:
        // gl=sqrt(1+k·b), gr=sqrt(1-k·b) (constant-power), k=kPostSteerKMax·amount,
        // smoothed by a one-pole gain smoother (anti-zipper — the Vocal Hall AttackRamp
        // distortion bug was an unsmoothed audio-rate gain; not repeated here). Time
        // constants (attack/release/gain-smooth) live in prepare().
        //
        // postSteerActive_ false → this block is skipped → the left[i]/right[i] final
        // write below is byte-identical to legacy (bit-null). Centered input →
        // EL==ER exactly → b==0 → gl==gr==sqrt(1.0f)==1.0f → wet × 1.0f → byte-identical
        // even with the feature ON (a stronger guarantee than the < -100 dB target).
        float steerBalance = 0.0f;
        if (postSteerActive_)
        {
            const float srcL = reflDryMono_[static_cast<size_t> (i)] + sourceSide_[static_cast<size_t> (i)];
            const float srcR = reflDryMono_[static_cast<size_t> (i)] - sourceSide_[static_cast<size_t> (i)];
            const float axl = std::fabs (srcL);
            const float axr = std::fabs (srcR);
            psEnvL_ = axl + ((axl > psEnvL_) ? psAtkCoeff_ : psRelCoeff_) * (psEnvL_ - axl);
            psEnvR_ = axr + ((axr > psEnvR_) ? psAtkCoeff_ : psRelCoeff_) * (psEnvR_ - axr);
            const float b   = std::clamp ((psEnvL_ - psEnvR_) / (psEnvL_ + psEnvR_ + 1.0e-9f), -1.0f, 1.0f);
            steerBalance = b;
            if (psProfileActive_)
            {
                if (axl + axr > 1.0e-6f)
                {
                    psProfileTimeEnv_ = psProfileSlowTimeEnv_ = 1.0f;
                    psProfileHoldRemaining_ = psProfileHoldSamples_;
                    if (psWanderActive_)
                    {
                        psWanderStarted_ = true;
                        psWanderPhase_ = psWanderInitialPhase_;
                        psWanderEnv_ = 1.0f;
                    }
                }
                else if (psProfileHoldRemaining_ > 0)
                {
                    --psProfileHoldRemaining_;
                }
                else if (psProfileReleaseCoeff_ > 0.0f)
                {
                    psProfileTimeEnv_ *= psProfileReleaseCoeff_;
                    psProfileSlowTimeEnv_ *= psProfileSlowReleaseCoeff_;
                }
                else
                    psProfileTimeEnv_ = psProfileSlowTimeEnv_ = 1.0f;
            }
            else
            {
                const float glT = std::sqrt (std::max (0.0f, 1.0f + psK_ * b));
                const float grT = std::sqrt (std::max (0.0f, 1.0f - psK_ * b));
                psGainL_ = glT + psGainCoeff_ * (psGainL_ - glT);
                psGainR_ = grT + psGainCoeff_ * (psGainR_ - grT);
                wetL *= psGainL_;
                wetR *= psGainR_;
            }
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
        float effSide;
        if (! bandWidthActive_)
        {
            effSide = side * width;   // legacy single-multiply path — bit-identical
        }
        else
        {
            // Complementary one-pole split of the side signal → low+mid+high == side.
            wbLp1State_ = (1.0f - wbLp1Coeff_) * side + wbLp1Coeff_ * wbLp1State_;  // LP 300
            wbLp2State_ = (1.0f - wbLp2Coeff_) * side + wbLp2Coeff_ * wbLp2State_;  // LP 5k
            const float lowB = wbLp1State_;
            const float midB = wbLp2State_ - wbLp1State_;
            const float hiB  = side - wbLp2State_;
            effSide = width * (lowB * widthBandLow_ + midB * widthBandMid_ + hiB * widthBandHi_);
        }
        wetL = mid + effSide;
        wetR = mid - effSide;

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

        // The calibrated profile is deliberately the final stereo operation.
        // This makes its anchor fit independent of Width/Mono Maker/cross-talk,
        // while the legacy scalar path above remains byte-compatible.
        if (psProfileActive_)
        {
            psBandLp1L_ = (1.0f - psBandLp1Coeff_) * wetL + psBandLp1Coeff_ * psBandLp1L_;
            psBandLp1R_ = (1.0f - psBandLp1Coeff_) * wetR + psBandLp1Coeff_ * psBandLp1R_;
            psBandLp2L_ = (1.0f - psBandLp2Coeff_) * wetL + psBandLp2Coeff_ * psBandLp2L_;
            psBandLp2R_ = (1.0f - psBandLp2Coeff_) * wetR + psBandLp2Coeff_ * psBandLp2R_;
            const float bandsL[3] = { psBandLp1L_, psBandLp2L_ - psBandLp1L_, wetL - psBandLp2L_ };
            const float bandsR[3] = { psBandLp1R_, psBandLp2R_ - psBandLp1R_, wetR - psBandLp2R_ };
            float sumL = 0.0f, sumR = 0.0f;
            const float wander = psWanderActive_ && psWanderStarted_
                ? std::sin (psWanderPhase_) * psWanderEnv_
                : 0.0f;
            for (int band = 0; band < 3; ++band)
            {
                const float k = std::clamp (
                    psProfileLateK_[band]
                        + (psProfileMiddleK_[band] - psProfileLateK_[band]) * psProfileSlowTimeEnv_
                        + (psProfileK_[band] - psProfileMiddleK_[band]) * psProfileTimeEnv_
                        + psWanderDepth_[band] * wander,
                    -0.9999f, 0.9999f);
                const float glT = std::sqrt (std::max (0.0f, 1.0f + k * steerBalance));
                const float grT = std::sqrt (std::max (0.0f, 1.0f - k * steerBalance));
                psProfileGainL_[band] = glT + psProfileGainCoeff_ * (psProfileGainL_[band] - glT);
                psProfileGainR_[band] = grT + psProfileGainCoeff_ * (psProfileGainR_[band] - grT);
                sumL += bandsL[band] * psProfileGainL_[band];
                sumR += bandsR[band] * psProfileGainR_[band];
            }
            if (std::abs (steerBalance) > 1.0e-9f)
            {
                wetL = sumL;
                wetR = sumR;
            }
            // A gain tilt alone cannot reproduce a mono-summing tank's
            // hard-left versus hard-right transfer without also forcing a
            // large ILD.  Rotate the final stereo vector in opposite
            // directions for genuinely hard-panned sources so response
            // identity and energy balance are independent.  The >0.999
            // source-balance gate keeps decorrelated stereo programme and the
            // fleet stimuli out of this path; centred input remains bit-null.
            const float absBalance = std::abs (steerBalance);
            if (std::abs (psPanRotationRadians_) > 1.0e-6f && absBalance > 0.999f)
            {
                const float hardPan = std::clamp ((absBalance - 0.999f) / 0.0009f, 0.0f, 1.0f);
                const float angle = std::copysign (psPanRotationRadians_ * hardPan, steerBalance);
                const float c = std::cos (angle);
                const float s = std::sin (angle);
                const float rotatedL = c * wetL - s * wetR;
                wetR = s * wetL + c * wetR;
                wetL = rotatedL;
            }
            // Some symmetric mono-summing tanks match the anchor by mirroring
            // their final field for a hard-right source. A full swap preserves
            // channel energy exactly, unlike a crossfade, and leaves the
            // calibrated hard-left transfer untouched.
            if (psMirrorHardRight_ && steerBalance < -0.999f)
                std::swap (wetL, wetR);
            if (psWanderActive_ && psWanderStarted_
                && std::abs (reflDryMono_[static_cast<size_t> (i)] + sourceSide_[static_cast<size_t> (i)])
                 + std::abs (reflDryMono_[static_cast<size_t> (i)] - sourceSide_[static_cast<size_t> (i)]) <= 1.0e-6f)
            {
                psWanderPhase_ += psWanderPhaseInc_;
                if (psWanderPhase_ >= kTwoPi)
                    psWanderPhase_ -= kTwoPi;
                psWanderEnv_ *= psWanderDecayCoeff_;
            }
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
