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

    mixSmoother_.reset (1.0f);
    erLevelSmoother_.reset (0.5f);
    widthSmoother_.reset (1.0f);
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

    // Late reverb path: InputDiffusion → FDN → OutputDiffusion
    if (! frozen_)
        diffuser_.process (scratchL_.data(), scratchR_.data(), numSamples);
    else
    {
        // When frozen, mute input to FDN (keep existing tail circulating)
        std::memset (scratchL_.data(), 0, static_cast<size_t> (numSamples) * sizeof (float));
        std::memset (scratchR_.data(), 0, static_cast<size_t> (numSamples) * sizeof (float));
    }

    fdn_.process (scratchL_.data(), scratchR_.data(),
                  scratchL_.data(), scratchR_.data(), numSamples);

    outputDiffuser_.process (scratchL_.data(), scratchR_.data(), numSamples);

    // Combine ER + late reverb, apply output EQ + width, then dry/wet mix.
    // All output-stage parameters are smoothed per-sample to prevent zipper noise.
    for (int i = 0; i < numSamples; ++i)
    {
        // Advance per-sample smoothers
        float mix = mixSmoother_.next();
        float er  = erLevelSmoother_.next();
        float w   = widthSmoother_.next();
        float wet = mix;
        float dry = 1.0f - mix;

        // Smooth filter cutoff and update coefficients when changed
        float loHz = loCutSmoother_.next();
        if (std::abs (loHz - loCutHz_) > 0.5f)
        {
            loCutHz_ = loHz;
            updateLoCutCoeffs();
        }
        float hiHz = hiCutSmoother_.next();
        if (std::abs (hiHz - hiCutHz_) > 1.0f)
        {
            hiCutHz_ = hiHz;
            updateHiCutCoeffs();
        }

        auto si = static_cast<size_t> (i);
        float wetL = scratchL_[si] * lateGainScale_ + erOutL_[si] * er;
        float wetR = scratchR_[si] * lateGainScale_ + erOutR_[si] * er;

        // DC blocker: y[n] = x[n] - x[n-1] + R*y[n-1]
        float dcOutL = wetL - dcX1L_ + dcCoeff_ * dcY1L_;
        dcX1L_ = wetL;
        dcY1L_ = dcOutL;

        float dcOutR = wetR - dcX1R_ + dcCoeff_ * dcY1R_;
        dcX1R_ = wetR;
        dcY1R_ = dcOutR;

        // Output EQ: lo cut (highpass) then hi cut (lowpass) on wet signal
        dcOutL = loCutFilter_.processL (dcOutL);
        dcOutR = loCutFilter_.processR (dcOutR);
        dcOutL = hiCutFilter_.processL (dcOutL);
        dcOutR = hiCutFilter_.processR (dcOutR);

        // Stereo width: mid/side encoding
        float mid  = (dcOutL + dcOutR) * 0.5f;
        float side = (dcOutL - dcOutR) * 0.5f;
        dcOutL = mid + side * w;
        dcOutR = mid - side * w;

        // Algorithm crossfade: ramp wet signal to avoid clicks on switch
        if (fadingOut_)
        {
            float fadeGain = static_cast<float> (fadeCounter_) / static_cast<float> (kFadeSamples);
            dcOutL *= fadeGain;
            dcOutR *= fadeGain;

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
            dcOutL *= fadeGain;
            dcOutR *= fadeGain;
            ++fadeCounter_;
        }

        left[i]  = left[i] * dry + dcOutL * wet;
        right[i] = right[i] * dry + dcOutR * wet;
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

    // Push structural config to FDN
    fdn_.setBaseDelays (config_->delayLengths);
    fdn_.setOutputTaps (config_->leftTaps, config_->rightTaps,
                        config_->leftSigns, config_->rightSigns);
    fdn_.setSizeRange (config_->sizeRangeMin, config_->sizeRangeMax);

    // Push diffusion max coefficients
    diffuser_.setMaxCoefficients (config_->inputDiffMaxCoeff12,
                                  config_->inputDiffMaxCoeff34);

    // Set ER time scale
    er_.setTimeScale (config_->erTimeScale);

    // Update bandwidth filter
    inputBwCoeff_ = std::exp (-6.283185307179586f * config_->bandwidthHz
                              / static_cast<float> (sampleRate_));

    // Store scaling factors
    erLevelScale_ = config_->erLevelScale;
    lateGainScale_ = config_->lateGainScale;

    // Re-apply current parameter values with new scaling
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
    fdn_.setDecayTime (seconds);
}

void DuskVerbEngine::setBassMultiply (float mult)
{
    lastBassMult_ = mult;
    fdn_.setBassMultiply (mult * config_->bassMultScale);
}

void DuskVerbEngine::setTrebleMultiply (float mult)
{
    lastTrebleMult_ = mult;
    fdn_.setTrebleMultiply (mult * config_->trebleMultScale);
}

void DuskVerbEngine::setCrossoverFreq (float hz)       { fdn_.setCrossoverFreq (hz); }

void DuskVerbEngine::setModDepth (float depth)
{
    lastModDepth_ = depth;
    fdn_.setModDepth (depth * config_->modDepthScale);
}

void DuskVerbEngine::setModRate (float hz)
{
    lastModRate_ = hz;
    fdn_.setModRate (hz * config_->modRateScale);
}

void DuskVerbEngine::setSize (float size)              { fdn_.setSize (size); }

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
    float decayFactor = std::clamp (5.0f / std::max (decayTime_, 0.2f), 0.4f, 1.0f);
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
    widthSmoother_.setTarget (std::clamp (width, 0.0f, 2.0f));
}

void DuskVerbEngine::setFreeze (bool frozen)
{
    if (frozen != frozen_)
    {
        frozen_ = frozen;
        fdn_.setFreeze (frozen);
    }
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
