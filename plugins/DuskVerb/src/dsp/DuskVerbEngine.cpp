#include "DuskVerbEngine.h"
#include "DspUtils.h"
#include "../VVERTapData.h"
#include "../PresetCorrectionFilters.h"
#include "../PresetTapPositions.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include "presets/PresetEngineRegistry.h"

// Forward declaration (defined below updateOutputEQCoeffs)
static void computeShelfCoeffs (float& b0, float& b1, float& b2,
                                 float& a1, float& a2,
                                 float sr, float freqHz, float gainDB, bool isHighShelf);

// ---------------------------------------------------------------------------
// Note: Hall tap tables replaced by dynamic generation via setMultiPointDensity().
// Static tables removed to reduce file size — the dynamic approach generates
// equivalent taps at any density (6-16 per channel) with automatic L/R offset.

void DuskVerbEngine::prepare (double sampleRate, int maxBlockSize)
{
    maxBlockSize_ = maxBlockSize;
    sampleRate_ = sampleRate;

    diffuser_.prepare (sampleRate, maxBlockSize);
    fdn_.prepare (sampleRate, maxBlockSize);
    dattorroTank_.prepare (sampleRate, maxBlockSize);
    quadTank_.prepare (sampleRate, maxBlockSize);
    hybridQuadTank_.prepare (sampleRate, maxBlockSize);
    tiledRoomReverb_.prepare (sampleRate, maxBlockSize);
    outputDiffuser_.prepare (sampleRate, maxBlockSize);
    er_.prepare (sampleRate, maxBlockSize);
    tailChorus_.prepare (sampleRate, maxBlockSize);

    // Pre-build every registered per-preset engine ONCE on this (non-audio)
    // thread so applyAlgorithm() can swap pointers without allocating.
    // Without this, applyAlgorithm() would call registry.create() and
    // engine->prepare() (both heap-allocating) on the audio thread during
    // the algorithm crossfade — a real-time safety violation.
    // LINKER FIX: directly populate the preset engine registry by calling
    // the generated forceLinkPresetEngines() stub. This bypasses the
    // unreliable static-init `PresetEngineRegistrar` mechanism, which
    // failed because the per-preset .cpp files were being dropped by the
    // linker (their .o files had no externally-referenced symbols when
    // compiled into a static library, so dead-strip removed them along
    // with their static initializers). The stub explicitly registers all
    // 53 factories on the first call.
    extern void forceLinkPresetEngines();
    forceLinkPresetEngines();

    if (prebuiltPresetEngines_.empty())
    {
        prebuiltPresetEngines_ = PresetEngineRegistry::instance().instantiateAll();
    }
    for (auto& kv : prebuiltPresetEngines_)
    {
        if (kv.second)
            kv.second->prepare (sampleRate, maxBlockSize);
    }
    presetEngine_ = nullptr;  // applyAlgorithm() will set the active pointer

    scratchL_.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    scratchR_.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    erOutL_.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    erOutR_.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    preDiffL_.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    preDiffR_.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    hybridL_.assign (static_cast<size_t> (maxBlockSize), 0.0f);
    hybridR_.assign (static_cast<size_t> (maxBlockSize), 0.0f);

    // Pre-delay: max 250ms at current sample rate
    int maxDelaySamples = static_cast<int> (std::ceil (0.250 * sampleRate));
    int bufSize = DspUtils::nextPowerOf2 (maxDelaySamples + 1);
    preDelayBufL_.assign (static_cast<size_t> (bufSize), 0.0f);
    preDelayBufR_.assign (static_cast<size_t> (bufSize), 0.0f);
    preDelayWritePos_ = 0;
    preDelayMask_ = bufSize - 1;
    preDelaySamples_ = 0;

    // Pre-allocate RMS tracker buffers so applyAlgorithm() never allocates on the audio thread
    rmsWindowSamples_ = std::max (1, static_cast<int> (std::round (
        static_cast<float> (sampleRate) * kRmsWindowMs / 1000.0f)));
    rmsBufferL_.assign (static_cast<size_t> (rmsWindowSamples_), 0.0f);
    rmsBufferR_.assign (static_cast<size_t> (rmsWindowSamples_), 0.0f);
    rmsSumL_ = 0.0f;
    rmsSumR_ = 0.0f;
    rmsIndex_ = 0;

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

    // Late onset envelope: reset ramp on prepare
    lateOnsetSamples_ = static_cast<int> (config_->lateOnsetMs * 0.001f
                                          * static_cast<float> (sampleRate));
    lateOnsetIncrement_ = (lateOnsetSamples_ > 0)
                        ? (1.0f / static_cast<float> (lateOnsetSamples_))
                        : 0.0f;
    lateOnsetRamp_ = (lateOnsetSamples_ > 0) ? 0.0f : 1.0f;

    // DC blocker: R = 1 - (2*pi*fc/sr), fc ~5Hz
    dcCoeff_ = 1.0f - (6.283185307179586f * 5.0f / static_cast<float> (sampleRate));
    dcX1L_ = 0.0f; dcY1L_ = 0.0f;
    dcX1R_ = 0.0f; dcY1R_ = 0.0f;

    // Output EQ
    loCutFilter_.reset();
    hiCutFilter_.reset();
    hiCutFilter2_.reset();
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

    // Fix 1 (decay_ratio): re-trigger onset envelope on silence→signal transition.
    // This ensures each new transient (note, snare hit) gets a fresh onset ramp
    // rather than only firing once after algorithm switch.
    if (useOnsetTable_ && onsetEnvelopePhase_ >= 1.0f)
    {
        float inputPeak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            float a = std::abs (left[i]) + std::abs (right[i]);
            if (a > inputPeak) inputPeak = a;
        }
        if (inputPeak > 0.001f && onsetInputWasQuiet_)
            onsetEnvelopePhase_ = 0.0f;  // re-trigger
        onsetInputWasQuiet_ = (inputPeak < 0.0001f);
    }

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

    // Input onset ramp: gradually feed energy into FDN to match VV's serial
    // tank onset. Applied to FDN INPUT (not output) so:
    // - ERs are unaffected (already captured above)
    // - Steady-state level preserved (FDN reaches same equilibrium)
    // - No tail energy loss (only initial transient is shaped)
    // Note: onset envelope table is applied to the OUTPUT (after engine), not input.
    // See "Fix 1 (decay_ratio)" block below the DC blocker.
    if (lateOnsetRamp_ < 1.0f)
    {
        // Default squared linear ramp for presets without a custom onset table
        for (int i = 0; i < numSamples; ++i)
        {
            auto si = static_cast<size_t> (i);
            float onset = lateOnsetRamp_ * lateOnsetRamp_;  // Squared: concave ramp
            scratchL_[si] *= onset;
            scratchR_[si] *= onset;
            lateOnsetRamp_ = std::min (1.0f, lateOnsetRamp_ + lateOnsetIncrement_);
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

    // Hybrid dual-engine: save input for secondary engine BEFORE primary processes it
    if (hybridBlend_ > 0.001f && hybridConfig_ != nullptr)
    {
        std::memcpy (hybridL_.data(), scratchL_.data(), static_cast<size_t> (numSamples) * sizeof (float));
        std::memcpy (hybridR_.data(), scratchR_.data(), static_cast<size_t> (numSamples) * sizeof (float));
    }

    // Late reverb: route to per-preset engine first (if registered),
    // then legacy TiledRoomReverb, then one of the shared engines.
    if (presetEngine_)
        presetEngine_->process (scratchL_.data(), scratchR_.data(),
                                scratchL_.data(), scratchR_.data(), numSamples);
    else if (useCustomPresetEngine_)
        tiledRoomReverb_.process (scratchL_.data(), scratchR_.data(),
                                  scratchL_.data(), scratchR_.data(), numSamples);
    else if (useQuadTank_)
        quadTank_.process (scratchL_.data(), scratchR_.data(),
                           scratchL_.data(), scratchR_.data(), numSamples);
    else if (useDattorroTank_)
        dattorroTank_.process (scratchL_.data(), scratchR_.data(),
                               scratchL_.data(), scratchR_.data(), numSamples);
    else
        fdn_.process (scratchL_.data(), scratchR_.data(),
                      scratchL_.data(), scratchR_.data(), numSamples);

    // Hybrid: run secondary engine on saved input and blend with primary output
    if (hybridBlend_ > 0.001f && hybridConfig_ != nullptr)
    {
        // Run DEDICATED secondary engine on the saved pre-primary input
        // Uses hybridQuadTank_ (configured with secondary algo's params)
        // to avoid conflicts with the primary QuadTank
        hybridQuadTank_.process (hybridL_.data(), hybridR_.data(),
                                 hybridL_.data(), hybridR_.data(), numSamples);

        // Blend: primary * (1-blend) + secondary * blend
        float a = 1.0f - hybridBlend_;
        float b = hybridBlend_;
        for (int i = 0; i < numSamples; ++i)
        {
            auto si = static_cast<size_t> (i);
            scratchL_[si] = scratchL_[si] * a + hybridL_[si] * b;
            scratchR_[si] = scratchR_[si] * a + hybridR_[si] * b;
        }
    }

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

    // Fix 1 (decay_ratio): per-preset onset OUTPUT envelope.
    // Applied to late reverb OUTPUT (not input) so it directly shapes the
    // energy that the decay_ratio metric measures. The FDN input envelope
    // doesn't work because feedback recirculation fills the FDN regardless.
    if (useOnsetTable_ && onsetEnvelopePhase_ < 1.0f)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            auto si = static_cast<size_t> (i);
            float idx = onsetEnvelopePhase_ * static_cast<float> (onsetEnvelopeTableSize_ - 1);
            int i0 = static_cast<int> (idx);
            int i1 = std::min (i0 + 1, onsetEnvelopeTableSize_ - 1);
            float frac = idx - static_cast<float> (i0);
            float env = onsetEnvelopeTable_[i0] * (1.0f - frac)
                      + onsetEnvelopeTable_[i1] * frac;
            scratchL_[si] *= env;
            scratchR_[si] *= env;
            onsetEnvelopePhase_ = std::min (1.0f, onsetEnvelopePhase_ + onsetEnvelopeInc_);
        }
    }

    // Tail chorus: stereo AM on late reverb (before scaling/diffusion).
    // NOTE: terminal decay is handled inside each reverb engine's feedback loop
    // (DattorroTank and FDN track peak/RMS internally). No engine-level post-
    // processing here — that would double-attenuate the already-decayed signal.
    tailChorus_.process (scratchL_.data(), scratchR_.data(), numSamples);

    // Scale late reverb and ER SEPARATELY. ERs bypass the output diffuser
    // to preserve their sharp transient peaks (matching VV's onset timing).
    // The output diffuser's ~29ms group delay was smearing ER energy from 20ms to 48ms.
    for (int i = 0; i < numSamples; ++i)
    {
        auto si = static_cast<size_t> (i);
        float er = erLevelSmoother_.next();
        float lateGain = lateGainScale_ * decayGainComp_;
        float outGain = outputGainSmoother_.next();

        // Late reverb only → goes through output diffuser
        scratchL_[si] = scratchL_[si] * lateGain * outGain;
        scratchR_[si] = scratchR_[si] * lateGain * outGain;

        // ER scaled separately → stored for post-diffusion addition
        erOutL_[si] = erOutL_[si] * er * outGain;
        erOutR_[si] = erOutR_[si] * er * outGain;
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

    // Per-preset spectral correction filter: shapes DattorroTank output to match VV.
    // Applied after output EQ, before output diffuser, to the late reverb only.
    if (correctionFilterActive_)
    {
        for (int s = 0; s < kNumCorrectionStages; ++s)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                auto si = static_cast<size_t> (i);
                scratchL_[si] = correctionFilter_[s].processL (scratchL_[si]);
                scratchR_[si] = correctionFilter_[s].processR (scratchR_[si]);
            }
        }
    }

    // Save pre-diffusion late reverb for feed-forward path
    if (lateFeedForwardLevel_ > 0.001f)
    {
        std::memcpy (preDiffL_.data(), scratchL_.data(), static_cast<size_t> (numSamples) * sizeof (float));
        std::memcpy (preDiffR_.data(), scratchR_.data(), static_cast<size_t> (numSamples) * sizeof (float));
    }

    outputDiffuser_.process (scratchL_.data(), scratchR_.data(), numSamples);

    // Fix 4 (spectral): apply per-preset corrective EQ to ER path before add-back.
    // The same 12-band peaking EQ that the wrapper applies to the late reverb is
    // now also applied to the ER signal so the combined output is spectrally matched.
    if (erCorrEQActive_)
    {
        for (int s = 0; s < erCorrEQBands_; ++s)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                auto si = static_cast<size_t> (i);
                erOutL_[si] = erCorrEQ_[s].processL (erOutL_[si]);
                erOutR_[si] = erCorrEQ_[s].processR (erOutR_[si]);
            }
        }
    }

    // Add un-diffused ER back into the signal AFTER output diffusion.
    // This preserves ER transient peaks while late reverb gets the full allpass smearing.
    for (int i = 0; i < numSamples; ++i)
    {
        auto si = static_cast<size_t> (i);
        scratchL_[si] += erOutL_[si];
        scratchR_[si] += erOutR_[si];
    }

    // RMS-tracking peak limiter: reduces FDN's inherent crest factor.
    // Tracks 5ms RMS and limits instantaneous peaks to crestLimitRatio × RMS.
    // This directly targets the peak/RMS ratio that the crest metric measures.
    if (crestLimitRatio_ > 0.0f)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            auto si = static_cast<size_t> (i);
            float sL = scratchL_[si];
            float sR = scratchR_[si];

            // Update RMS ring buffer
            float sqL = sL * sL;
            float sqR = sR * sR;
            rmsSumL_ += sqL - rmsBufferL_[static_cast<size_t> (rmsIndex_)];
            rmsSumR_ += sqR - rmsBufferR_[static_cast<size_t> (rmsIndex_)];
            rmsBufferL_[static_cast<size_t> (rmsIndex_)] = sqL;
            rmsBufferR_[static_cast<size_t> (rmsIndex_)] = sqR;
            rmsIndex_ = (rmsIndex_ + 1) % rmsWindowSamples_;

            // Compute RMS (max of L/R for stereo-linked limiting)
            float rmsL = std::sqrt (std::max (0.0f, rmsSumL_ / static_cast<float> (rmsWindowSamples_)));
            float rmsR = std::sqrt (std::max (0.0f, rmsSumR_ / static_cast<float> (rmsWindowSamples_)));
            float rms = std::max (rmsL, rmsR);

            // Limit peaks that exceed ratio × RMS
            float limit = rms * crestLimitRatio_;
            if (limit > 1e-10f)
            {
                float absL = std::abs (sL);
                float absR = std::abs (sR);
                float maxAbs = std::max (absL, absR);
                if (maxAbs > limit)
                {
                    float gain = limit / maxAbs;
                    scratchL_[si] = sL * gain;
                    scratchR_[si] = sR * gain;
                }
            }
        }
    }

    // Late feed-forward: add pre-diffusion late reverb for earlier onset
    if (lateFeedForwardLevel_ > 0.001f)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            auto si = static_cast<size_t> (i);
            scratchL_[si] += preDiffL_[si] * lateFeedForwardLevel_;
            scratchR_[si] += preDiffR_[si] * lateFeedForwardLevel_;
        }
    }

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
            wetL = hiCutFilter2_.processL (hiCutFilter_.processL (wetL));
            wetR = hiCutFilter2_.processR (hiCutFilter_.processR (wetR));

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

            float finalWetL = wetL * wet * gainTrimLinear_;
            float finalWetR = wetR * wet * gainTrimLinear_;

            // Per-preset peak limiter on final wet signal (after gainTrim)
            if (outputLimiterThreshold_ > 0.0f)
            {
                float pk = std::max (std::abs (finalWetL), std::abs (finalWetR));
                if (pk > outputLimiterEnv_)
                    outputLimiterEnv_ = pk;
                else
                    outputLimiterEnv_ = outputLimiterRelease_ * outputLimiterEnv_
                                      + (1.0f - outputLimiterRelease_) * pk;
                if (outputLimiterEnv_ > outputLimiterThreshold_)
                {
                    float g = outputLimiterThreshold_ / outputLimiterEnv_;
                    finalWetL *= g;
                    finalWetR *= g;
                }
            }

            left[i]  = left[i] * dry + finalWetL;
            right[i] = right[i] * dry + finalWetR;
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
    useQuadTank_ = config_->useQuadTank;

    // Per-preset engine lookup: every registered preset engine was already
    // instantiated and prepared in DuskVerbEngine::prepare() (off the audio
    // thread). Here we just look up the prebuilt instance and swap a raw
    // pointer — no allocation, no engine prepare(), so this is real-time
    // safe to call from process() during the algorithm crossfade.
    presetEngine_ = nullptr;
    {
        auto it = prebuiltPresetEngines_.find (config_->name);
        if (it != prebuiltPresetEngines_.end() && it->second)
        {
            presetEngine_ = it->second.get();
            // Reset the engine's internal state (LFO phase, filter buffers,
            // delay lines) so a switch always starts cleanly. clearBuffers()
            // is required to be allocation-free per PresetEngineBase's
            // contract.
            presetEngine_->clearBuffers();
            // Re-apply full cached runtime state to the engine so it picks up
            // any parameter changes or overrides that were in effect before the
            // algorithm switch. Missing setters would leave the engine at its
            // preset defaults until the next parameter change.
            presetEngine_->setDecayTime (decayTime_);
            presetEngine_->setBassMultiply (lastBassMult_);
            presetEngine_->setTrebleMultiply (lastTrebleMult_);
            presetEngine_->setModDepth (lastModDepth_);
            presetEngine_->setModRate (lastModRate_);
            presetEngine_->setSize (lastSize_);
            presetEngine_->setCrossoverFreq (lastCrossoverHz_);
            presetEngine_->setDecayBoost (lastDecayBoost_);
            presetEngine_->setFreeze (frozen_);
            presetEngine_->setTerminalDecay (terminalThresholdDb_, terminalFactor_);
        }
    }

    // Fix 4 (spectral): ER corrective EQ infrastructure.
    // The coefficients are populated from the active preset engine but
    // activation is controlled by a per-preset flag (not yet implemented).
    // The late-reverb corrEQ is NOT suitable for ERs (different spectral
    // profile), so this is disabled by default until per-preset ER-specific
    // corrections are derived.
    erCorrEQActive_ = false;
    erCorrEQBands_ = 0;

    // Fix 1 (decay_ratio): populate onset envelope table from preset engine.
    useOnsetTable_ = false;
    onsetEnvelopeTable_ = nullptr;
    onsetEnvelopeTableSize_ = 0;
    onsetEnvelopePhase_ = 0.0f;
    onsetEnvelopeInc_ = 0.0f;
    if (presetEngine_ != nullptr)
    {
        const float* table = presetEngine_->getOnsetEnvelopeTable();
        int tableSize = presetEngine_->getOnsetEnvelopeTableSize();
        float durationMs = presetEngine_->getOnsetDurationMs();
        if (table != nullptr && tableSize > 1 && durationMs > 0.0f)
        {
            onsetEnvelopeTable_ = table;
            onsetEnvelopeTableSize_ = tableSize;
            onsetEnvelopePhase_ = 0.0f;
            onsetEnvelopeInc_ = 1.0f / (durationMs * 0.001f
                                        * static_cast<float> (sampleRate_));
            useOnsetTable_ = true;
        }
    }

    // Legacy flag kept for backwards compatibility with TiledRoomReverb.
    useCustomPresetEngine_ = false;
    enableSaturation_ = config_->enableSaturation;
    lateFeedForwardLevel_ = config_->lateFeedForwardLevel;
    crestLimitRatio_ = config_->crestLimitRatio;
    hybridBlend_ = config_->hybridBlend;
    hybridConfig_ = (config_->hybridSecondaryAlgo >= 0)
                   ? &getAlgorithmConfig (config_->hybridSecondaryAlgo)
                   : nullptr;

    // Configure dedicated hybrid QuadTank with secondary algorithm's params
    if (hybridConfig_ != nullptr && hybridBlend_ > 0.001f)
    {
        hybridQuadTank_.setSizeRange (hybridConfig_->sizeRangeMin, hybridConfig_->sizeRangeMax);
        hybridQuadTank_.setHighCrossoverFreq (hybridConfig_->highCrossoverHz);
        hybridQuadTank_.setAirDampingScale (hybridConfig_->airDampingScale);
        hybridQuadTank_.setLateGainScale (hybridConfig_->lateGainScale);
        hybridQuadTank_.clearBuffers();
    }
    // Reset RMS tracker state (buffers already allocated in prepare())
    std::fill (rmsBufferL_.begin(), rmsBufferL_.end(), 0.0f);
    std::fill (rmsBufferR_.begin(), rmsBufferR_.end(), 0.0f);
    rmsSumL_ = 0.0f;
    rmsSumR_ = 0.0f;
    rmsIndex_ = 0;

    // Late onset envelope: ramp FDN output from 0→1 over lateOnsetMs
    lateOnsetSamples_ = static_cast<int> (config_->lateOnsetMs * 0.001f
                                          * static_cast<float> (sampleRate_));
    lateOnsetIncrement_ = (lateOnsetSamples_ > 0)
                        ? (1.0f / static_cast<float> (lateOnsetSamples_))
                        : 0.0f;
    lateOnsetRamp_ = (lateOnsetSamples_ > 0) ? 0.0f : 1.0f;

    // Hall uses Dattorro with hall-scale delays (~280ms loops vs room's ~135ms).
    // Must set scale and delayScale BEFORE prepare() so buffers are allocated correctly.
    dattorroTank_.setHallScale (config_ == &kHall);
    dattorroTank_.setDelayScale (config_->dattorroDelayScale);

    // Apply per-algorithm Dattorro output tap positions (late onset matching VV)
    if (config_->dattorroLeftTaps && config_->dattorroRightTaps)
    {
        // Convert AlgorithmConfig::DattorroOutputTap to DattorroTank::OutputTap
        DattorroTank::OutputTap lTaps[7], rTaps[7];
        for (int i = 0; i < 7; ++i)
        {
            lTaps[i] = { config_->dattorroLeftTaps[i].buf,
                         config_->dattorroLeftTaps[i].pos,
                         config_->dattorroLeftTaps[i].sign,
                         config_->dattorroLeftTaps[i].gain };
            rTaps[i] = { config_->dattorroRightTaps[i].buf,
                         config_->dattorroRightTaps[i].pos,
                         config_->dattorroRightTaps[i].sign,
                         config_->dattorroRightTaps[i].gain };
        }
        dattorroTank_.setOutputTaps (lTaps, rTaps);
    }

    if (useDattorroTank_)
        dattorroTank_.prepare (sampleRate_, maxBlockSize_);

    // Clear buffers to prevent noise burst from old delay structure
    fdn_.clearBuffers();
    dattorroTank_.clearBuffers();
    quadTank_.clearBuffers();

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
    // When a per-preset engine is active, it applies lateGainScale_ internally
    // in its own process() output path. DuskVerbEngine must NOT double-apply.
    lateGainScale_ = presetEngine_ ? 1.0f : config_->lateGainScale;
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

    // Hall: dynamic multi-point output tapping — 16 taps per channel (256 total)
    // gives maximum averaging to push delay recirculations below detection threshold.
    fdn_.setUseShortInlineAP (false);
    if (config_ == &kHall)
        fdn_.setMultiPointDensity (6);  // 6 taps × 16 channels = 96 total (diminishing returns beyond this)
    else
        fdn_.setMultiPointOutput (nullptr, 0, nullptr, 0);

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

    // Tank size range and gain
    dattorroTank_.setSizeRange (config_->sizeRangeMin, config_->sizeRangeMax);
    dattorroTank_.setLateGainScale (config_->lateGainScale);
    dattorroTank_.setNoiseModDepth (config_->noiseModDepth);
    dattorroTank_.setHighCrossoverFreq (config_->highCrossoverHz);
    dattorroTank_.setAirDampingScale (config_->airDampingScale);
    tiledRoomReverb_.setHighCrossoverFreq (config_->highCrossoverHz);
    tiledRoomReverb_.setAirDampingScale (config_->airDampingScale);
    // softOnsetMs is set per-preset via runtime parameter (soft_onset), not hardcoded here.
    // Peak limiter: now controlled per-preset via runtime parameter (limiter_thresh).
    quadTank_.setSizeRange (config_->sizeRangeMin, config_->sizeRangeMax);
    quadTank_.setHighCrossoverFreq (config_->highCrossoverHz);
    quadTank_.setAirDampingScale (config_->airDampingScale);
    quadTank_.setLateGainScale (config_->lateGainScale);

    // Re-apply current parameter values with new scaling
    setDecayTime (decayTime_);
    setModDepth (lastModDepth_);
    setModRate (lastModRate_);
    setTrebleMultiply (lastTrebleMult_);
    setBassMultiply (lastBassMult_);
    setERLevel (lastERLevel_);
    setDiffusion (lastDiffusion_);
    setOutputDiffusion (lastOutputDiffusion_);

    // Re-apply cached runtime overrides that would otherwise be stomped by the
    // config-baseline writes above. The preset engine already received decay /
    // treble / bass / modDepth / modRate / freeze / terminalDecay in the
    // lookup block earlier; the *non-preset* engines and the structural HF
    // override still need replay here.
    if (terminalThresholdDb_ != 0.0f || terminalFactor_ != 1.0f)
    {
        fdn_.setTerminalDecay (terminalThresholdDb_, terminalFactor_);
        dattorroTank_.setTerminalDecay (terminalThresholdDb_, terminalFactor_);
        quadTank_.setTerminalDecay (terminalThresholdDb_, terminalFactor_);
        tiledRoomReverb_.setTerminalDecay (terminalThresholdDb_, terminalFactor_);
        if (hybridConfig_) hybridQuadTank_.setTerminalDecay (terminalThresholdDb_, terminalFactor_);
    }
    if (structuralHFOverrideHz_ >= 0.0f)
    {
        fdn_.setStructuralHFDamping (structuralHFOverrideHz_, lastTrebleMult_);
        dattorroTank_.setStructuralHFDamping (structuralHFOverrideHz_);
        quadTank_.setStructuralHFDamping (structuralHFOverrideHz_);
        if (presetEngine_) presetEngine_->setStructuralHFDamping (structuralHFOverrideHz_);
        if (hybridConfig_) hybridQuadTank_.setStructuralHFDamping (structuralHFOverrideHz_);
    }
}

void DuskVerbEngine::setDecayTime (float seconds)
{
    decayTime_ = seconds;
    float dts = (decayTimeScaleOverride_ > 0.0f) ? decayTimeScaleOverride_ : config_->decayTimeScale;
    float scaledDecay = seconds * dts;
    fdn_.setDecayTime (scaledDecay);
    dattorroTank_.setDecayTime (scaledDecay);
    quadTank_.setDecayTime (scaledDecay);
    tiledRoomReverb_.setDecayTime (scaledDecay);
    // Per-preset wrappers internally multiply their setDecayTime() input by
    // their baked kBakedDecayTimeScale (= config_->decayTimeScale at the time
    // the engine was generated). Forwarding `scaledDecay` here would cause
    // double-scaling. Instead, normalize to wrapper input units:
    //   - If no override is active, just forward `seconds` (the wrapper will
    //     re-apply its baked scale and arrive at the same scaledDecay).
    //   - If a runtime override is active, forward `seconds * (override / baked)`
    //     so the wrapper's internal multiply lands on `seconds * override`.
    if (presetEngine_)
    {
        const float presetSeconds =
            (decayTimeScaleOverride_ > 0.0f && config_->decayTimeScale > 0.0f)
                ? seconds * (decayTimeScaleOverride_ / config_->decayTimeScale)
                : seconds;
        presetEngine_->setDecayTime (presetSeconds);
    }
    if (hybridConfig_ != nullptr)
        hybridQuadTank_.setDecayTime (seconds * hybridConfig_->decayTimeScale);

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
    quadTank_.setBassMultiply (mult * config_->bassMultScale);
    tiledRoomReverb_.setBassMultiply (mult * config_->bassMultScale);
    if (presetEngine_) presetEngine_->setBassMultiply (mult);
    if (hybridConfig_) hybridQuadTank_.setBassMultiply (mult * hybridConfig_->bassMultScale);
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
    quadTank_.setTrebleMultiply (scaledTreble);
    tiledRoomReverb_.setTrebleMultiply (scaledTreble);
    if (presetEngine_) presetEngine_->setTrebleMultiply (mult);
    if (hybridConfig_)
    {
        float hTreble = hybridConfig_->trebleMultScale * (1.0f - trebleCurve)
                      + hybridConfig_->trebleMultScaleMax * trebleCurve;
        hybridQuadTank_.setTrebleMultiply (hTreble);
    }
    // Re-compute structural HF damping with raw treble (not scaled by trebleMultScale).
    // Raw treble gives better differentiation: Concert Wave (raw=1.0) gets minimal
    // structural damping, while dark presets (raw=0.3) get significant damping.
    fdn_.setStructuralHFDamping (config_->structuralHFDampingHz, mult);
}

void DuskVerbEngine::setCrossoverFreq (float hz)
{
    lastCrossoverHz_ = hz;
    fdn_.setCrossoverFreq (hz);
    dattorroTank_.setCrossoverFreq (hz);
    quadTank_.setCrossoverFreq (hz);
    tiledRoomReverb_.setCrossoverFreq (hz);
    if (presetEngine_) presetEngine_->setCrossoverFreq (hz);
    if (hybridConfig_) hybridQuadTank_.setCrossoverFreq (hz);
}

void DuskVerbEngine::setModDepth (float depth)
{
    lastModDepth_ = depth;
    fdn_.setModDepth (depth * config_->modDepthScale);
    dattorroTank_.setModDepth (depth * config_->modDepthScale);
    quadTank_.setModDepth (depth * config_->modDepthScale);
    tiledRoomReverb_.setModDepth (depth * config_->modDepthScale);
    if (presetEngine_) presetEngine_->setModDepth (depth);
    if (hybridConfig_) hybridQuadTank_.setModDepth (depth * hybridConfig_->modDepthScale);
}

void DuskVerbEngine::setModRate (float hz)
{
    lastModRate_ = hz;
    fdn_.setModRate (hz * config_->modRateScale);
    dattorroTank_.setModRate (hz * config_->modRateScale);
    quadTank_.setModRate (hz * config_->modRateScale);
    tiledRoomReverb_.setModRate (hz * config_->modRateScale);
    if (presetEngine_) presetEngine_->setModRate (hz);
    if (hybridConfig_) hybridQuadTank_.setModRate (hz * hybridConfig_->modRateScale);
}

void DuskVerbEngine::setSize (float size)
{
    lastSize_ = size;
    fdn_.setSize (size);
    dattorroTank_.setSize (size);
    quadTank_.setSize (size);
    tiledRoomReverb_.setSize (size);
    if (presetEngine_) presetEngine_->setSize (size);
    if (hybridConfig_) hybridQuadTank_.setSize (size);
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
    float effectiveDecay = decayTime_ * config_->decayTimeScale;
    float decayFactor = std::clamp (8.0f / std::max (effectiveDecay, 0.2f), 0.65f, 1.0f);
    outputDiffuser_.setDiffusion (amount * decayFactor * config_->outputDiffScale);
}

void DuskVerbEngine::setERLevel (float level)
{
    lastERLevel_ = level;
    // Allow ER level above 1.0 — Plate algorithm needs 2-3x ER gain to match
    // VV's strong early onset relative to the DattorroTank's late reverb.
    erLevelSmoother_.setTarget (std::max (0.0f, level * erLevelScale_));
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
        quadTank_.setFreeze (frozen);
        tiledRoomReverb_.setFreeze (frozen);
        if (presetEngine_) presetEngine_->setFreeze (frozen);
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

void DuskVerbEngine::setInputOnset (float ms)
{
    float clamped = std::clamp (ms, 0.0f, 200.0f);
    lateOnsetSamples_ = static_cast<int> (clamped * 0.001f * static_cast<float> (sampleRate_));
    lateOnsetIncrement_ = (lateOnsetSamples_ > 0)
                        ? (1.0f / static_cast<float> (lateOnsetSamples_))
                        : 0.0f;
    // Reset ramp on parameter change (triggers new onset shaping)
    lateOnsetRamp_ = (lateOnsetSamples_ > 0) ? 0.0f : 1.0f;
}

void DuskVerbEngine::setDattorroDelayScale (float scale)
{
    dattorroTank_.setDelayScale (scale);
}

void DuskVerbEngine::setDattorroSoftOnsetMs (float ms)
{
    dattorroTank_.setSoftOnsetMs (ms);
}

void DuskVerbEngine::setLateFeedForwardLevel (float level)
{
    lateFeedForwardLevel_ = std::clamp (level, 0.0f, 1.0f);
}

void DuskVerbEngine::resetLateFeedForwardLevel()
{
    lateFeedForwardLevel_ = config_->lateFeedForwardLevel;
}

void DuskVerbEngine::setDattorroLimiter (float thresholdDb, float releaseMs)
{
    if (thresholdDb <= -60.0f || thresholdDb >= 0.0f)
    {
        outputLimiterThreshold_ = 0.0f;  // Disabled
        return;
    }
    outputLimiterThreshold_ = std::pow (10.0f, thresholdDb / 20.0f);
    float releaseSamples = releaseMs * 0.001f * static_cast<float> (sampleRate_);
    outputLimiterRelease_ = std::exp (-1.0f / std::max (releaseSamples, 1.0f));
}

// --- Optimizer-tunable overrides ---

void DuskVerbEngine::setAirDampingOverride (float scale)
{
    // Sentinel (<0): restore config default for shared engines. For preset
    // engines, restore the baked value (not config_->...) so the per-preset
    // calibrated air damping is recovered instead of the shared baseline.
    bool isSentinel = (scale < 0.0f);
    if (isSentinel && config_) scale = config_->airDampingScale;
    fdn_.setAirTrebleMultiply (scale);
    dattorroTank_.setAirDampingScale (scale);
    quadTank_.setAirDampingScale (scale);
    tiledRoomReverb_.setAirDampingScale (scale);
    if (presetEngine_)
    {
        if (isSentinel)
            presetEngine_->resetAirDampingToDefault();
        else
            presetEngine_->setAirDampingScale (scale);
    }
    if (hybridConfig_) hybridQuadTank_.setAirDampingScale (scale);
}

void DuskVerbEngine::setHighCrossoverOverride (float hz)
{
    bool isSentinel = (hz < 0.0f);
    if (isSentinel && config_) hz = config_->highCrossoverHz;
    fdn_.setHighCrossoverFreq (hz);
    dattorroTank_.setHighCrossoverFreq (hz);
    quadTank_.setHighCrossoverFreq (hz);
    tiledRoomReverb_.setHighCrossoverFreq (hz);
    if (presetEngine_)
    {
        if (isSentinel)
            presetEngine_->resetHighCrossoverToDefault();
        else
            presetEngine_->setHighCrossoverFreq (hz);
    }
    if (hybridConfig_) hybridQuadTank_.setHighCrossoverFreq (hz);
}

void DuskVerbEngine::setNoiseModOverride (float samples)
{
    bool isSentinel = (samples < 0.0f);
    if (isSentinel && config_) samples = config_->noiseModDepth;
    fdn_.setNoiseModDepth (samples);
    dattorroTank_.setNoiseModDepth (samples);
    quadTank_.setNoiseModDepth (samples);
    if (hybridConfig_) hybridQuadTank_.setNoiseModDepth (samples);
    if (presetEngine_)
    {
        if (isSentinel)
            presetEngine_->resetNoiseModToDefault();
        else
            presetEngine_->setNoiseModDepth (samples);
    }
}

void DuskVerbEngine::setInlineDiffusionOverride (float coeff)
{
    if (coeff < 0.0f && config_) coeff = config_->inlineDiffusionCoeff;
    fdn_.setInlineDiffusion (coeff);
}

void DuskVerbEngine::setStereoCouplingOverride (float amount)
{
    // stereo_coupling sentinel is <= -1.5 (valid range includes -1..+1).
    if (amount <= -1.5f && config_) amount = config_->stereoCoupling;
    fdn_.setStereoCoupling (amount);
}

void DuskVerbEngine::setChorusDepthOverride (float depth)
{
    if (depth < 0.0f) depth = 0.0f;  // Default: chorus off
    tailChorus_.setDepth (depth);
}

void DuskVerbEngine::setChorusRateOverride (float hz)
{
    if (hz < 0.0f) hz = 1.0f;  // Default rate
    tailChorus_.setRate (hz);
}

void DuskVerbEngine::setOutputGainOverride (float gain)
{
    if (gain < 0.0f && config_) gain = config_->outputGain;
    outputGainSmoother_.setTarget (gain);
}

void DuskVerbEngine::setERCrossfeedOverride (float amount)
{
    if (amount < 0.0f && config_) amount = config_->erCrossfeed;
    erCrossfeed_ = amount;
}

void DuskVerbEngine::setDecayTimeScaleOverride (float scale)
{
    decayTimeScaleOverride_ = scale;
    // Re-apply decay with new scale (setDecayTime checks the sentinel)
    setDecayTime (decayTime_);
}

void DuskVerbEngine::setDecayBoostOverride (float boost)
{
    if (boost < 0.0f) boost = 1.0f;  // Default: no boost
    lastDecayBoost_ = boost;
    fdn_.setDecayBoost (boost);
    dattorroTank_.setDecayBoost (boost);
    quadTank_.setDecayBoost (boost);
    tiledRoomReverb_.setDecayBoost (boost);
    if (presetEngine_) presetEngine_->setDecayBoost (boost);
    if (hybridConfig_) hybridQuadTank_.setDecayBoost (boost);
}

void DuskVerbEngine::setStructuralHFDampingOverride (float hz)
{
    // Negative hz means "revert to config default". We still cache -1.0f so
    // applyAlgorithm() knows there is no override to replay.
    if (hz < 0.0f && config_)
    {
        structuralHFOverrideHz_ = -1.0f;
        hz = config_->structuralHFDampingHz;
    }
    else
    {
        structuralHFOverrideHz_ = hz;
    }
    fdn_.setStructuralHFDamping (hz, lastTrebleMult_);
    dattorroTank_.setStructuralHFDamping (hz);
    quadTank_.setStructuralHFDamping (hz);
    if (presetEngine_) presetEngine_->setStructuralHFDamping (hz);
    if (hybridConfig_) hybridQuadTank_.setStructuralHFDamping (hz);
}

void DuskVerbEngine::setOutputLowShelfOverride (float dB)
{
    float sr = static_cast<float> (sampleRate_);
    lowShelfEnabled_ = (dB != 0.0f);
    if (lowShelfEnabled_)
        computeShelfCoeffs (lowShelfFilter_.b0, lowShelfFilter_.b1, lowShelfFilter_.b2,
                            lowShelfFilter_.a1, lowShelfFilter_.a2,
                            sr, 250.0f, dB, false);
    else
        lowShelfFilter_.reset();
}

void DuskVerbEngine::setOutputHighShelfOverride (float dB, float hz)
{
    float sr = static_cast<float> (sampleRate_);
    float nyquist = sr * 0.5f;
    hz = std::clamp (hz, 1.0f, nyquist);
    highShelfEnabled_ = (dB != 0.0f);
    if (highShelfEnabled_)
        computeShelfCoeffs (highShelfFilter_.b0, highShelfFilter_.b1, highShelfFilter_.b2,
                            highShelfFilter_.a1, highShelfFilter_.a2,
                            sr, hz, dB, true);
    else
        highShelfFilter_.reset();
}

void DuskVerbEngine::setOutputMidEQOverride (float dB, float hz)
{
    float sr = static_cast<float> (sampleRate_);
    float nyquist = sr * 0.5f;
    hz = std::clamp (hz, 1.0f, nyquist);
    midEQEnabled_ = (dB != 0.0f);
    if (midEQEnabled_)
    {
        float A = std::pow (10.0f, dB / 40.0f);
        float omega = 6.283185307179586f * hz / sr;
        float sn = std::sin (omega);
        float cs = std::cos (omega);
        float Q = 0.7f; // Default Q for override
        float alpha = sn / (2.0f * Q);

        float a0 = 1.0f + alpha / A;
        midEQFilter_.b0 = (1.0f + alpha * A) / a0;
        midEQFilter_.b1 = (-2.0f * cs) / a0;
        midEQFilter_.b2 = (1.0f - alpha * A) / a0;
        midEQFilter_.a1 = (-2.0f * cs) / a0;
        midEQFilter_.a2 = (1.0f - alpha / A) / a0;
    }
    else
    {
        midEQFilter_.reset();
    }
}

void DuskVerbEngine::setTerminalDecayOverride (float thresholdDb, float factor)
{
    // Terminal decay is applied inside each reverb engine's feedback loop
    // (not as post-processing) so it accelerates tail decay rather than just
    // gating the output. Each engine tracks its own peak/RMS envelope.
    // Sentinels: thresholdDb >= -0.1 OR factor <= 0.001 → restore config defaults.
    if ((thresholdDb >= -0.1f || factor <= 0.001f) && config_)
    {
        thresholdDb = -40.0f;
        factor = 1.0f;  // 1.0 disables extra decay
    }
    terminalThresholdDb_ = thresholdDb;
    terminalFactor_ = factor;
    fdn_.setTerminalDecay (thresholdDb, factor);
    dattorroTank_.setTerminalDecay (thresholdDb, factor);
    quadTank_.setTerminalDecay (thresholdDb, factor);
    tiledRoomReverb_.setTerminalDecay (thresholdDb, factor);
    if (presetEngine_) presetEngine_->setTerminalDecay (thresholdDb, factor);
    if (hybridConfig_) hybridQuadTank_.setTerminalDecay (thresholdDb, factor);
}

void DuskVerbEngine::setERAirCeilingOverride (float hz)
{
    if (hz <= 0.0f && config_) hz = config_->erAirAbsorptionCeilingHz;
    er_.setAirAbsorptionCeiling (hz);
}

void DuskVerbEngine::setERAirFloorOverride (float hz)
{
    if (hz <= 0.0f && config_) hz = config_->erAirAbsorptionFloorHz;
    er_.setAirAbsorptionFloor (hz);
}

void DuskVerbEngine::updateTapPositions (
    float l0, float l1, float l2, float l3, float l4, float l5, float l6,
    float r0, float r1, float r2, float r3, float r4, float r5, float r6)
{
    float defaultGains[14] = { 1,1,1,1,1,1,1, 1,1,1,1,1,1,1 };
    updateTapPositionsAndGains (l0,l1,l2,l3,l4,l5,l6, r0,r1,r2,r3,r4,r5,r6,
                                defaultGains[0],defaultGains[1],defaultGains[2],defaultGains[3],defaultGains[4],defaultGains[5],defaultGains[6],
                                defaultGains[7],defaultGains[8],defaultGains[9],defaultGains[10],defaultGains[11],defaultGains[12],defaultGains[13]);
}

void DuskVerbEngine::updateTapPositionsAndGains (
    float l0, float l1, float l2, float l3, float l4, float l5, float l6,
    float r0, float r1, float r2, float r3, float r4, float r5, float r6,
    float gl0, float gl1, float gl2, float gl3, float gl4, float gl5, float gl6,
    float gr0, float gr1, float gr2, float gr3, float gr4, float gr5, float gr6)
{
    if (! config_->dattorroLeftTaps || ! config_->dattorroRightTaps)
        return;

    float lPos[7] = { l0, l1, l2, l3, l4, l5, l6 };
    float rPos[7] = { r0, r1, r2, r3, r4, r5, r6 };
    float lGain[7] = { gl0, gl1, gl2, gl3, gl4, gl5, gl6 };
    float rGain[7] = { gr0, gr1, gr2, gr3, gr4, gr5, gr6 };

    DattorroTank::OutputTap lTaps[7], rTaps[7];
    for (int i = 0; i < 7; ++i)
    {
        lTaps[i] = { config_->dattorroLeftTaps[i].buf, lPos[i], config_->dattorroLeftTaps[i].sign, lGain[i] };
        rTaps[i] = { config_->dattorroRightTaps[i].buf, rPos[i], config_->dattorroRightTaps[i].sign, rGain[i] };
    }
    dattorroTank_.setOutputTaps (lTaps, rTaps);
}

void DuskVerbEngine::loadPresetTapPositions (int presetIndex)
{
    if (presetIndex >= 0 && presetIndex < kNumPresetTapConfigs)
    {
        const auto& config = getPresetTapConfig (presetIndex);
        // Convert PresetTapConfig taps to DattorroTank::OutputTap
        DattorroTank::OutputTap lTaps[7], rTaps[7];
        for (int i = 0; i < 7; ++i)
        {
            lTaps[i] = config.leftTaps[i];
            rTaps[i] = config.rightTaps[i];
        }
        dattorroTank_.setOutputTaps (lTaps, rTaps);
    }
}

void DuskVerbEngine::applyTapGains (
    float gl0, float gl1, float gl2, float gl3, float gl4, float gl5, float gl6,
    float gr0, float gr1, float gr2, float gr3, float gr4, float gr5, float gr6)
{
    float lGain[7] = { gl0, gl1, gl2, gl3, gl4, gl5, gl6 };
    float rGain[7] = { gr0, gr1, gr2, gr3, gr4, gr5, gr6 };
    dattorroTank_.applyTapGains (lGain, rGain);
}

void DuskVerbEngine::loadCorrectionFilter (int presetIndex)
{
    const auto& filter = getCorrectionFilter (presetIndex);
    for (int s = 0; s < kNumCorrectionStages; ++s)
    {
        correctionFilter_[s].b0 = filter.stages[s].b0;
        correctionFilter_[s].b1 = filter.stages[s].b1;
        correctionFilter_[s].b2 = filter.stages[s].b2;
        correctionFilter_[s].a1 = filter.stages[s].a1;
        correctionFilter_[s].a2 = filter.stages[s].a2;
        correctionFilter_[s].z1L = correctionFilter_[s].z2L = 0.0f;
        correctionFilter_[s].z1R = correctionFilter_[s].z2R = 0.0f;
    }
    // Check if any stage is non-identity (b0 != 1 or others != 0)
    correctionFilterActive_ = false;
    for (int s = 0; s < kNumCorrectionStages; ++s)
    {
        if (filter.stages[s].b0 != 1.0f || filter.stages[s].b1 != 0.0f
            || filter.stages[s].b2 != 0.0f || filter.stages[s].a1 != 0.0f
            || filter.stages[s].a2 != 0.0f)
        {
            correctionFilterActive_ = true;
            break;
        }
    }
}

void DuskVerbEngine::setCustomERTaps (const CustomERTap* taps, int numTaps)
{
    // Compute current pre-delay in ms for tap time adjustment.
    // VV-extracted tap times are absolute (from IR start), but the ER engine
    // receives already-pre-delayed input, so we subtract DV's pre-delay.
    float preDelayMs = static_cast<float> (preDelaySamples_)
                     / static_cast<float> (sampleRate_) * 1000.0f;

    if (taps && numTaps > 0)
        er_.setCustomTaps (taps, numTaps, preDelayMs);
    else
        er_.setCustomTaps (nullptr, 0);  // Revert to generated mode
}

void DuskVerbEngine::loadPresetERTaps (const char* presetName)
{
    int numTaps = 0;
    const CustomERTap* taps = getPresetERTaps (presetName, numTaps);
    setCustomERTaps (taps, numTaps);
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

// Cascaded 4th-order Butterworth lowpass (24 dB/oct).
// Two second-order stages with staggered Q values for maximally flat passband.
// Butterworth 4th-order pole angles: Q1 = 1/(2*cos(pi/8)) ≈ 0.541, Q2 = 1/(2*cos(3*pi/8)) ≈ 1.307
void DuskVerbEngine::updateHiCutCoeffs()
{
    float sr = static_cast<float> (sampleRate_);
    float omega = 6.283185307179586f * hiCutHz_ / sr;
    float sn = std::sin (omega);
    float cs = std::cos (omega);

    // Stage 1: Q = 0.5412 (wider, handles the gentle rolloff shoulder)
    float alpha1 = sn / (2.0f * 0.5412f);
    float a0_1 = 1.0f + alpha1;
    hiCutFilter_.b0 = ((1.0f - cs) * 0.5f) / a0_1;
    hiCutFilter_.b1 = (1.0f - cs) / a0_1;
    hiCutFilter_.b2 = ((1.0f - cs) * 0.5f) / a0_1;
    hiCutFilter_.a1 = (-2.0f * cs) / a0_1;
    hiCutFilter_.a2 = (1.0f - alpha1) / a0_1;

    // Stage 2: Q = 1.3066 (narrower, adds the steep skirt)
    float alpha2 = sn / (2.0f * 1.3066f);
    float a0_2 = 1.0f + alpha2;
    hiCutFilter2_.b0 = ((1.0f - cs) * 0.5f) / a0_2;
    hiCutFilter2_.b1 = (1.0f - cs) / a0_2;
    hiCutFilter2_.b2 = ((1.0f - cs) * 0.5f) / a0_2;
    hiCutFilter2_.a1 = (-2.0f * cs) / a0_2;
    hiCutFilter2_.a2 = (1.0f - alpha2) / a0_2;
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



