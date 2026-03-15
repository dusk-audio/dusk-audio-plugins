#include "DuskVerbEngine.h"
#include "DspUtils.h"

#include <algorithm>
#include <cmath>
#include <cstring>

void DuskVerbEngine::prepare (double sampleRate, int maxBlockSize)
{
    maxBlockSize_ = maxBlockSize;
    sampleRate_ = sampleRate;

    diffuser_.prepare (sampleRate, maxBlockSize);
    fdn_.prepare (sampleRate, maxBlockSize);
    dattorroTank_.prepare (sampleRate, maxBlockSize);
    outputDiffuser_.prepare (sampleRate, maxBlockSize);
    er_.prepare (sampleRate, maxBlockSize);

    scratchL_.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    scratchR_.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    erOutL_.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    erOutR_.assign (static_cast<size_t> (maxBlockSize), 0.0f);

    // Pre-delay: max 250ms at current sample rate
    int maxDelaySamples = static_cast<int> (std::ceil (0.250 * sampleRate));
    int bufSize = DspUtils::nextPowerOf2 (maxDelaySamples + 1);
    preDelayBufL_.assign (static_cast<size_t> (bufSize), 0.0f);
    preDelayBufR_.assign (static_cast<size_t> (bufSize), 0.0f);
    preDelayWritePos_ = 0;
    preDelayMask_ = bufSize - 1;
    preDelaySamples_ = 0;

    // Per-sample smoothers: 5ms time constant (~99% settled in 25ms)
    constexpr float kSmoothTimeMs = 5.0f;
    mixSmoother_.setSmoothingTime (sampleRate, kSmoothTimeMs);
    erLevelSmoother_.setSmoothingTime (sampleRate, kSmoothTimeMs);
    widthSmoother_.setSmoothingTime (sampleRate, kSmoothTimeMs);
    loCutSmoother_.setSmoothingTime (sampleRate, kSmoothTimeMs);
    hiCutSmoother_.setSmoothingTime (sampleRate, kSmoothTimeMs);
    outputGainSmoother_.setSmoothingTime (sampleRate, kSmoothTimeMs);

    mixSmoother_.reset (1.0f);
    erLevelSmoother_.reset (0.5f);
    widthSmoother_.reset (1.0f);
    outputGainSmoother_.reset (config_->outputGain);
    loCutSmoother_.reset (loCutHz_);
    hiCutSmoother_.reset (hiCutHz_);

    // Input bandwidth filter: use config bandwidth (default Hall = 10kHz)
    inputBwCoeff_ = std::exp (-6.283185307179586f * config_->bandwidthHz
                              / static_cast<float> (sampleRate));
    inputBwStateL_ = 0.0f;
    inputBwStateR_ = 0.0f;

    // DC blocker: R = 1 - (2*pi*fc/sr), fc ~5Hz
    dcCoeff_ = 1.0f - (6.283185307179586f * 5.0f / static_cast<float> (sampleRate));
    dcX1L_ = 0.0f; dcY1L_ = 0.0f;
    dcX1R_ = 0.0f; dcY1R_ = 0.0f;

    // Output EQ
    loCutFilter_.reset();
    hiCutFilter_.reset();
    updateLoCutCoeffs();
    updateHiCutCoeffs();

    // Reset freeze
    frozen_ = false;

    // Reset gate
    gateEnabled_ = false;
    gateHoldSamples_ = 0;
    gateReleaseCoeff_ = 0.0f;
    gateEnvelope_ = 0.0f;
    gateHoldCounter_ = 0;
    gateTriggered_ = false;

    // Reset algorithm crossfade state so first setAlgorithm applies immediately
    pendingAlgorithm_ = -1;
    fadeCounter_ = kFadeSamples;
    fadingOut_ = false;
    firstAlgorithmSet_ = true;
}

void DuskVerbEngine::process (float* left, float* right, int numSamples)
{
    // Copy input to scratch for the wet processing path.
    // The original left/right buffers are preserved as the dry signal for mixing.
    std::memcpy (scratchL_.data(), left,  static_cast<size_t> (numSamples) * sizeof (float));
    std::memcpy (scratchR_.data(), right, static_cast<size_t> (numSamples) * sizeof (float));

    // Pre-delay
    if (preDelaySamples_ > 0)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            auto wp = static_cast<size_t> (preDelayWritePos_);
            preDelayBufL_[wp] = scratchL_[static_cast<size_t> (i)];
            preDelayBufR_[wp] = scratchR_[static_cast<size_t> (i)];

            auto rp = static_cast<size_t> ((preDelayWritePos_ - preDelaySamples_) & preDelayMask_);
            scratchL_[static_cast<size_t> (i)] = preDelayBufL_[rp];
            scratchR_[static_cast<size_t> (i)] = preDelayBufR_[rp];

            preDelayWritePos_ = (preDelayWritePos_ + 1) & preDelayMask_;
        }
    }

    // Input bandwidth filter: gentle LP to soften transient attacks (Dattorro)
    for (int i = 0; i < numSamples; ++i)
    {
        auto si = static_cast<size_t> (i);
        inputBwStateL_ = (1.0f - inputBwCoeff_) * scratchL_[si]
                       + inputBwCoeff_ * inputBwStateL_;
        scratchL_[si] = inputBwStateL_;

        inputBwStateR_ = (1.0f - inputBwCoeff_) * scratchR_[si]
                       + inputBwCoeff_ * inputBwStateR_;
        scratchR_[si] = inputBwStateR_;
    }

    // Early reflections: pre-delayed input → separate ER output
    // (reads scratch before diffusion modifies it)
    if (! frozen_)
    {
        er_.process (scratchL_.data(), scratchR_.data(),
                     erOutL_.data(), erOutR_.data(), numSamples);
    }
    else
    {
        // When frozen, mute new early reflections
        std::memset (erOutL_.data(), 0, static_cast<size_t> (numSamples) * sizeof (float));
        std::memset (erOutR_.data(), 0, static_cast<size_t> (numSamples) * sizeof (float));
    }

    // ER→FDN crossfeed: feed early reflections into the late reverb input
    // so the FDN tail "grows out of" the ER pattern
    if (erCrossfeed_ > 0.0f && ! frozen_)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            auto si = static_cast<size_t> (i);
            scratchL_[si] += erOutL_[si] * erCrossfeed_;
            scratchR_[si] += erOutR_[si] * erCrossfeed_;
        }
    }

    // Late reverb path: InputDiffusion → FDN → OutputDiffusion
    if (! frozen_)
        diffuser_.process (scratchL_.data(), scratchR_.data(), numSamples);
    else
    {
        // When frozen, mute input to FDN (keep existing tail circulating)
        std::memset (scratchL_.data(), 0, static_cast<size_t> (numSamples) * sizeof (float));
        std::memset (scratchR_.data(), 0, static_cast<size_t> (numSamples) * sizeof (float));
    }

    // Late reverb: route to Dattorro tank (Room) or Hadamard FDN (all other modes)
    if (useDattorroTank_)
        dattorroTank_.process (scratchL_.data(), scratchR_.data(),
                               scratchL_.data(), scratchR_.data(), numSamples);
    else
        fdn_.process (scratchL_.data(), scratchR_.data(),
                      scratchL_.data(), scratchR_.data(), numSamples);

    // DC blocker: apply before output diffusion so allpass filters don't accumulate DC
    for (int i = 0; i < numSamples; ++i)
    {
        auto si = static_cast<size_t> (i);

        float dcOutL = scratchL_[si] - dcX1L_ + dcCoeff_ * dcY1L_;
        dcX1L_ = scratchL_[si];
        dcY1L_ = dcOutL;
        scratchL_[si] = dcOutL;

        float dcOutR = scratchR_[si] - dcX1R_ + dcCoeff_ * dcY1R_;
        dcX1R_ = scratchR_[si];
        dcY1R_ = dcOutR;
        scratchR_[si] = dcOutR;
    }

    // Mix ER into late reverb BEFORE output diffusion so the allpass network
    // smears discrete ER taps into a smooth buildup (prevents slapback artifacts).
    // Then apply per-algorithm output gain to compensate for internal gain differences.
    for (int i = 0; i < numSamples; ++i)
    {
        auto si = static_cast<size_t> (i);
        float er = erLevelSmoother_.next();
        float lateGain = lateGainScale_ * decayGainComp_;
        float outGain = outputGainSmoother_.next();
        scratchL_[si] = DspUtils::fastTanh ((scratchL_[si] * lateGain + erOutL_[si] * er) * outGain / 16.0f) * 16.0f;
        scratchR_[si] = DspUtils::fastTanh ((scratchR_[si] * lateGain + erOutR_[si] * er) * outGain / 16.0f) * 16.0f;
    }

    // Output EQ: low shelf at 250Hz + mid parametric + high shelf
    if (lowShelfEnabled_ || highShelfEnabled_ || midEQEnabled_)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            auto si = static_cast<size_t> (i);
            if (lowShelfEnabled_)
            {
                scratchL_[si] = lowShelfFilter_.processL (scratchL_[si]);
                scratchR_[si] = lowShelfFilter_.processR (scratchR_[si]);
            }
            if (midEQEnabled_)
            {
                scratchL_[si] = midEQFilter_.processL (scratchL_[si]);
                scratchR_[si] = midEQFilter_.processR (scratchR_[si]);
            }
            if (highShelfEnabled_)
            {
                scratchL_[si] = highShelfFilter_.processL (scratchL_[si]);
                scratchR_[si] = highShelfFilter_.processR (scratchR_[si]);
            }
        }
    }

    // Anti-alias filtering is handled inside the FDN feedback loop (first-order LP at ~17kHz).

    outputDiffuser_.process (scratchL_.data(), scratchR_.data(), numSamples);

    // Apply output EQ + width, then dry/wet mix.
    // All output-stage parameters are smoothed per-sample to prevent zipper noise.
    // Filter coefficients are updated at sub-block boundaries (every 32 samples)
    // to avoid calling sin/cos/division in the inner sample loop.
    constexpr int kSubBlock = 32;
    for (int blockStart = 0; blockStart < numSamples; blockStart += kSubBlock)
    {
        int blockEnd = std::min (blockStart + kSubBlock, numSamples);

        // Update filter coefficients once per sub-block (skip trig in inner loop)
        if (std::abs (loCutSmoother_.current - loCutHz_) > 0.5f)
        {
            loCutHz_ = loCutSmoother_.current;
            updateLoCutCoeffs();
        }
        if (std::abs (hiCutSmoother_.current - hiCutHz_) > 1.0f)
        {
            hiCutHz_ = hiCutSmoother_.current;
            updateHiCutCoeffs();
        }

        for (int i = blockStart; i < blockEnd; ++i)
        {
            // Advance per-sample smoothers
            float mix = mixSmoother_.next();
            float w   = widthSmoother_.next();
            float wet = mix;
            float dry = 1.0f - mix;
            loCutSmoother_.next();
            hiCutSmoother_.next();

            auto si = static_cast<size_t> (i);
            float wetL = scratchL_[si];
            float wetR = scratchR_[si];

            // Output EQ: lo cut (highpass) then hi cut (lowpass) on wet signal
            wetL = loCutFilter_.processL (wetL);
            wetR = loCutFilter_.processR (wetR);
            wetL = hiCutFilter_.processL (wetL);
            wetR = hiCutFilter_.processR (wetR);

            // Stereo width: mid/side encoding
            float mid  = (wetL + wetR) * 0.5f;
            float side = (wetL - wetR) * 0.5f;
            wetL = mid + side * w;
            wetR = mid - side * w;

            // Algorithm crossfade: ramp wet signal to avoid clicks on switch
            if (fadingOut_)
            {
                float fadeGain = static_cast<float> (fadeCounter_) / static_cast<float> (kFadeSamples);
                wetL *= fadeGain;
                wetR *= fadeGain;

                if (--fadeCounter_ <= 0)
                {
                    // At zero crossing, apply the new algorithm config
                    if (pendingAlgorithm_ >= 0)
                    {
                        applyAlgorithm (pendingAlgorithm_);
                        pendingAlgorithm_ = -1;
                    }
                    fadingOut_ = false;
                    fadeCounter_ = 0; // Start fade-in from silence
                }
            }
            else if (fadeCounter_ < kFadeSamples)
            {
                // Fade back in after algorithm switch
                float fadeGain = static_cast<float> (fadeCounter_) / static_cast<float> (kFadeSamples);
                wetL *= fadeGain;
                wetR *= fadeGain;
                ++fadeCounter_;
            }

            // Gate envelope: truncate reverb tail for gated reverb presets
            if (gateEnabled_)
            {
                float inputLevel = std::abs (left[i]) + std::abs (right[i]);
                if (inputLevel > 1e-6f)
                {
                    gateTriggered_ = true;
                    gateHoldCounter_ = gateHoldSamples_;
                    gateEnvelope_ = 1.0f;
                }

                if (gateTriggered_)
                {
                    if (gateHoldCounter_ > 0)
                        --gateHoldCounter_;
                    else
                        gateEnvelope_ *= gateReleaseCoeff_;
                }

                wetL *= gateEnvelope_;
                wetR *= gateEnvelope_;
            }

            left[i]  = left[i] * dry + wetL * wet * gainTrimLinear_;
            right[i] = right[i] * dry + wetR * wet * gainTrimLinear_;
        }
    }
}

void DuskVerbEngine::setAlgorithm (int index)
{
    // During initial setup (right after prepare), apply immediately with no fade.
    // During playback, defer to process() with crossfade to avoid clicks.
    if (firstAlgorithmSet_)
    {
        firstAlgorithmSet_ = false;
        applyAlgorithm (index);
        return;
    }

    if (pendingAlgorithm_ < 0 && ! fadingOut_)
    {
        pendingAlgorithm_ = index;
        fadingOut_ = true;
        fadeCounter_ = kFadeSamples;
    }
}

void DuskVerbEngine::applyAlgorithm (int index)
{
    config_ = &getAlgorithmConfig (index);
    useDattorroTank_ = config_->useDattorroTank;

    // Clear buffers to prevent noise burst from old delay structure
    fdn_.clearBuffers();
    dattorroTank_.clearBuffers();

    // Push structural config to FDN (always configure it, even if inactive,
    // so switching away from Dattorro mode produces correct FDN output)
    fdn_.setBaseDelays (config_->delayLengths);
    fdn_.setOutputTaps (config_->leftTaps, config_->rightTaps,
                        config_->leftSigns, config_->rightSigns);
    fdn_.setSizeRange (config_->sizeRangeMin, config_->sizeRangeMax);

    // Push diffusion max coefficients
    diffuser_.setMaxCoefficients (config_->inputDiffMaxCoeff12,
                                  config_->inputDiffMaxCoeff34);

    // Set ER time scale, gain distribution, brightness, and stereo decorrelation
    er_.setTimeScale (config_->erTimeScale);
    er_.setGainExponent (config_->erGainExponent);
    er_.setAirAbsorptionCeiling (config_->erAirAbsorptionCeilingHz);
    er_.setAirAbsorptionFloor (config_->erAirAbsorptionFloorHz);
    er_.setDecorrCoeff (config_->erDecorrCoeff);

    // Load custom mono-panned ER tap table if provided by this algorithm
    if (config_->numCustomERTaps > 0)
        er_.setCustomTaps (config_->customERTaps, config_->numCustomERTaps);
    else
        er_.setCustomTaps (nullptr, 0); // Revert to generated mode

    // Update bandwidth filter
    inputBwCoeff_ = std::exp (-6.283185307179586f * config_->bandwidthHz
                              / static_cast<float> (sampleRate_));

    // Store scaling factors
    erLevelScale_ = config_->erLevelScale;
    lateGainScale_ = config_->lateGainScale;
    erCrossfeed_ = config_->erCrossfeed;
    outputGainSmoother_.setTarget (config_->outputGain);

    // FDN-specific config (only applies when FDN is active, but set anyway)
    fdn_.setInlineDiffusion (config_->inlineDiffusionCoeff);
    fdn_.setModDepthFloor (config_->modDepthFloor);
    fdn_.setNoiseModDepth (config_->noiseModDepth);
    fdn_.setHadamardPerturbation (config_->hadamardPerturbation);
    fdn_.setUseHouseholder (config_->useHouseholderFeedback);
    fdn_.setUseWeightedGains (config_->useWeightedGains);
    fdn_.setHighCrossoverFreq (config_->highCrossoverHz);
    fdn_.setAirTrebleMultiply (config_->airDampingScale);
    fdn_.setStructuralHFDamping (config_->structuralHFDampingHz, lastTrebleMult_);
    fdn_.setStructuralLFDamping (config_->structuralLFDampingHz);
    setGateParams (config_->gateHoldMs, config_->gateReleaseMs);
    fdn_.setDualSlope (config_->dualSlopeRatio, config_->dualSlopeFastCount,
                       config_->dualSlopeFastGain);
    fdn_.setStereoCoupling (config_->stereoCoupling);

    // Output EQ (low shelf at 250Hz + mid parametric + high shelf)
    lowShelfEnabled_ = (config_->outputLowShelfDB != 0.0f);
    highShelfEnabled_ = (config_->outputHighShelfDB != 0.0f);
    midEQEnabled_ = (config_->outputMidEQHz > 0.0f && config_->outputMidEQDB != 0.0f);
    if (lowShelfEnabled_ || highShelfEnabled_ || midEQEnabled_)
        updateOutputEQCoeffs();
    if (!lowShelfEnabled_)
        lowShelfFilter_.reset();
    if (!highShelfEnabled_)
        highShelfFilter_.reset();
    if (!midEQEnabled_)
        midEQFilter_.reset();

    // DattorroTank size range
    dattorroTank_.setSizeRange (config_->sizeRangeMin, config_->sizeRangeMax);
    dattorroTank_.setLateGainScale (config_->lateGainScale);

    // Re-apply current parameter values with new scaling
    setDecayTime (decayTime_);
    setModDepth (lastModDepth_);
    setModRate (lastModRate_);
    setTrebleMultiply (lastTrebleMult_);
    setBassMultiply (lastBassMult_);
    setERLevel (lastERLevel_);
    setDiffusion (lastDiffusion_);
    setOutputDiffusion (lastOutputDiffusion_);
}

void DuskVerbEngine::setDecayTime (float seconds)
{
    decayTime_ = seconds;
    float scaledDecay = seconds * config_->decayTimeScale;
    fdn_.setDecayTime (scaledDecay);
    dattorroTank_.setDecayTime (scaledDecay);

    // Decay-dependent output compensation: boost at short decay times to match
    // reference reverb's higher energy density at low feedback coefficients.
    // Linear ramp: full boost at decayTime=0, zero at knee.
    if (config_->shortDecayBoostDB > 0.0f && config_->shortDecayBoostKnee > 0.0f)
    {
        float t = std::clamp (scaledDecay / config_->shortDecayBoostKnee, 0.0f, 1.0f);
        float boostDB = config_->shortDecayBoostDB * (1.0f - t);
        decayGainComp_ = std::pow (10.0f, boostDB / 20.0f);
    }
    else
    {
        decayGainComp_ = 1.0f;
    }
}

void DuskVerbEngine::setBassMultiply (float mult)
{
    lastBassMult_ = mult;
    fdn_.setBassMultiply (mult * config_->bassMultScale);
    dattorroTank_.setBassMultiply (mult * config_->bassMultScale);
}

void DuskVerbEngine::setTrebleMultiply (float mult)
{
    lastTrebleMult_ = mult;
    // Nonlinear treble scaling: squared curve interpolates between dark-end
    // (trebleMultScale) and bright-end (trebleMultScaleMax) targets.
    // At treble=1.0: scaledTreble = trebleMultScaleMax (flat/bright, as if high shelf is fully open)
    // At treble=0.0: scaledTreble = trebleMultScale (steep HF rolloff)
    float trebleCurve = mult * mult;
    float scaledTreble = config_->trebleMultScale * (1.0f - trebleCurve)
                       + config_->trebleMultScaleMax * trebleCurve;
    fdn_.setTrebleMultiply (scaledTreble);
    dattorroTank_.setTrebleMultiply (scaledTreble);
    // Re-compute structural HF damping with raw treble (not scaled by trebleMultScale).
    // Raw treble gives better differentiation: Concert Wave (raw=1.0) gets minimal
    // structural damping, while dark presets (raw=0.3) get significant damping.
    fdn_.setStructuralHFDamping (config_->structuralHFDampingHz, mult);
}

void DuskVerbEngine::setCrossoverFreq (float hz)
{
    fdn_.setCrossoverFreq (hz);
    dattorroTank_.setCrossoverFreq (hz);
}

void DuskVerbEngine::setModDepth (float depth)
{
    lastModDepth_ = depth;
    fdn_.setModDepth (depth * config_->modDepthScale);
    dattorroTank_.setModDepth (depth * config_->modDepthScale);
}

void DuskVerbEngine::setModRate (float hz)
{
    lastModRate_ = hz;
    fdn_.setModRate (hz * config_->modRateScale);
    dattorroTank_.setModRate (hz * config_->modRateScale);
}

void DuskVerbEngine::setSize (float size)
{
    fdn_.setSize (size);
    dattorroTank_.setSize (size);
}

void DuskVerbEngine::setPreDelay (float milliseconds)
{
    float ms = std::clamp (milliseconds, 0.0f, 250.0f);
    preDelaySamples_ = static_cast<int> (ms * 0.001f * static_cast<float> (sampleRate_));
}

void DuskVerbEngine::setDiffusion (float amount)
{
    lastDiffusion_ = amount;
    diffuser_.setDiffusion (amount);
}

void DuskVerbEngine::setOutputDiffusion (float amount)
{
    lastOutputDiffusion_ = amount;
    // Decay-linked limiting: reduce output diffusion at long decay times
    // to prevent allpass ringing (inspired by Dattorro's decay_diffusion_2 coupling)
    // Gentler curve: full density up to 8s, floor at 0.5 (was 5s/0.4)
    // Ambient/Pad users running 8-15s decays retain more output density
    float effectiveDecay = decayTime_ * config_->decayTimeScale;
    float decayFactor = std::clamp (8.0f / std::max (effectiveDecay, 0.2f), 0.65f, 1.0f);
    outputDiffuser_.setDiffusion (amount * decayFactor * config_->outputDiffScale);
}

void DuskVerbEngine::setERLevel (float level)
{
    lastERLevel_ = level;
    erLevelSmoother_.setTarget (std::clamp (level * erLevelScale_, 0.0f, 1.0f));
}

void DuskVerbEngine::setERSize (float size)              { er_.setSize (size); }

void DuskVerbEngine::setMix (float dryWet)
{
    mixSmoother_.setTarget (std::clamp (dryWet, 0.0f, 1.0f));
}

void DuskVerbEngine::setLoCut (float hz)
{
    loCutSmoother_.setTarget (std::clamp (hz, 20.0f, 500.0f));
}

void DuskVerbEngine::setHiCut (float hz)
{
    hiCutSmoother_.setTarget (std::clamp (hz, 1000.0f, 20000.0f));
}

void DuskVerbEngine::setWidth (float width)
{
    widthSmoother_.setTarget (std::clamp (width, 0.0f, 1.5f));
}

void DuskVerbEngine::setFreeze (bool frozen)
{
    if (frozen != frozen_)
    {
        frozen_ = frozen;
        fdn_.setFreeze (frozen);
        dattorroTank_.setFreeze (frozen);
    }
}

void DuskVerbEngine::setGateParams (float holdMs, float releaseMs)
{
    if (holdMs <= 0.0f)
    {
        gateEnabled_ = false;
        return;
    }
    gateEnabled_ = true;
    gateHoldSamples_ = static_cast<int> (holdMs * 0.001f * static_cast<float> (sampleRate_));
    // Release coefficient: reach -60dB in releaseMs
    float releaseSamples = std::max (1.0f, releaseMs * 0.001f * static_cast<float> (sampleRate_));
    gateReleaseCoeff_ = std::exp (-6.908f / releaseSamples);
}

void DuskVerbEngine::setGainTrim (float dB)
{
    gainTrimLinear_ = std::pow (10.0f, dB / 20.0f);
}

// Second-order Butterworth highpass coefficients (12 dB/oct)
void DuskVerbEngine::updateLoCutCoeffs()
{
    float sr = static_cast<float> (sampleRate_);
    float omega = 6.283185307179586f * loCutHz_ / sr;
    float sn = std::sin (omega);
    float cs = std::cos (omega);
    float alpha = sn / 1.4142135623730951f; // sqrt(2) for Butterworth Q

    float a0 = 1.0f + alpha;
    loCutFilter_.b0 = ((1.0f + cs) * 0.5f) / a0;
    loCutFilter_.b1 = -(1.0f + cs) / a0;
    loCutFilter_.b2 = ((1.0f + cs) * 0.5f) / a0;
    loCutFilter_.a1 = (-2.0f * cs) / a0;
    loCutFilter_.a2 = (1.0f - alpha) / a0;
}

// Second-order Butterworth lowpass coefficients (12 dB/oct)
void DuskVerbEngine::updateHiCutCoeffs()
{
    float sr = static_cast<float> (sampleRate_);
    float omega = 6.283185307179586f * hiCutHz_ / sr;
    float sn = std::sin (omega);
    float cs = std::cos (omega);
    float alpha = sn / 1.4142135623730951f; // sqrt(2) for Butterworth Q

    float a0 = 1.0f + alpha;
    hiCutFilter_.b0 = ((1.0f - cs) * 0.5f) / a0;
    hiCutFilter_.b1 = (1.0f - cs) / a0;
    hiCutFilter_.b2 = ((1.0f - cs) * 0.5f) / a0;
    hiCutFilter_.a1 = (-2.0f * cs) / a0;
    hiCutFilter_.a2 = (1.0f - alpha) / a0;
}

// Shelf biquad coefficients: Audio EQ Cookbook (Robert Bristow-Johnson), Q=0.707.
static void computeShelfCoeffs (float& b0, float& b1, float& b2,
                                 float& a1, float& a2,
                                 float sr, float freqHz, float gainDB, bool isHighShelf)
{
    float A = std::pow (10.0f, gainDB / 40.0f);
    float omega = 6.283185307179586f * freqHz / sr;
    float sn = std::sin (omega);
    float cs = std::cos (omega);
    float alpha = sn / (2.0f * 0.707f);
    float sqA = 2.0f * std::sqrt (A) * alpha;

    if (isHighShelf)
    {
        float a0 = (A + 1.0f) - (A - 1.0f) * cs + sqA;
        b0 = (A * ((A + 1.0f) + (A - 1.0f) * cs + sqA)) / a0;
        b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cs)) / a0;
        b2 = (A * ((A + 1.0f) + (A - 1.0f) * cs - sqA)) / a0;
        a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cs)) / a0;
        a2 = ((A + 1.0f) - (A - 1.0f) * cs - sqA) / a0;
    }
    else
    {
        float a0 = (A + 1.0f) + (A - 1.0f) * cs + sqA;
        b0 = (A * ((A + 1.0f) - (A - 1.0f) * cs + sqA)) / a0;
        b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cs)) / a0;
        b2 = (A * ((A + 1.0f) - (A - 1.0f) * cs - sqA)) / a0;
        a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cs)) / a0;
        a2 = ((A + 1.0f) + (A - 1.0f) * cs - sqA) / a0;
    }
}

void DuskVerbEngine::updateOutputEQCoeffs()
{
    float sr = static_cast<float> (sampleRate_);
    if (lowShelfEnabled_)
        computeShelfCoeffs (lowShelfFilter_.b0, lowShelfFilter_.b1, lowShelfFilter_.b2,
                            lowShelfFilter_.a1, lowShelfFilter_.a2,
                            sr, 250.0f, config_->outputLowShelfDB, false);
    if (highShelfEnabled_)
        computeShelfCoeffs (highShelfFilter_.b0, highShelfFilter_.b1, highShelfFilter_.b2,
                            highShelfFilter_.a1, highShelfFilter_.a2,
                            sr, config_->outputHighShelfHz, config_->outputHighShelfDB, true);
    if (midEQEnabled_)
    {
        // Peaking EQ (Audio EQ Cookbook)
        float A = std::pow (10.0f, config_->outputMidEQDB / 40.0f);
        float omega = 6.283185307179586f * config_->outputMidEQHz / sr;
        float sn = std::sin (omega);
        float cs = std::cos (omega);
        float alpha = sn / (2.0f * config_->outputMidEQQ);

        float a0 = 1.0f + alpha / A;
        midEQFilter_.b0 = (1.0f + alpha * A) / a0;
        midEQFilter_.b1 = (-2.0f * cs) / a0;
        midEQFilter_.b2 = (1.0f - alpha * A) / a0;
        midEQFilter_.a1 = (-2.0f * cs) / a0;
        midEQFilter_.a2 = (1.0f - alpha / A) / a0;
    }
}



