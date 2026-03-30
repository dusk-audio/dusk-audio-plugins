#include "DuskAmpEngine.h"
#include <cmath>
#include <algorithm>

void DuskAmpEngine::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;

    // Resize mono scratch buffer
    // Account for oversampling: the upsampled block can be up to 8x larger
    monoBuffer_.resize (static_cast<size_t> (maxBlockSize * 8), 0.0f);

    // Pre-allocate AudioBuffers used in process() to avoid heap allocation on the audio thread
    oversamplingBuffer_.setSize (1, maxBlockSize, false, true, true);
    cabBuffer_.setSize (1, maxBlockSize, false, true, true);

    // Prepare oversampling (mono, 1 channel)
    oversampling_.prepare (sampleRate, maxBlockSize, 1);

    // Prepare all sub-components at the oversampled rate for the nonlinear stages
    double oversampledRate = oversampling_.getOversampledSampleRate();

    input_.prepare (sampleRate);
    preamp_.prepare (oversampledRate);
    powerAmp_.prepare (oversampledRate);
    cabinet_.prepare (sampleRate, maxBlockSize);
    postFx_.prepare (sampleRate, maxBlockSize);

    // Tone stack rate depends on amp mode: oversampled in DSP, base rate in NAM
    if (currentMode_ == AmpMode::NAM)
        toneStack_.prepare (sampleRate);
    else
        toneStack_.prepare (oversampledRate);

#if DUSKAMP_NAM_SUPPORT
    nam_.prepare (sampleRate, maxBlockSize);
#endif
}

void DuskAmpEngine::process (float* left, float* right, int numSamples)
{
    // 1. Sum stereo input to mono (guitar is mono anyway)
    for (int i = 0; i < numSamples; ++i)
        monoBuffer_[static_cast<size_t> (i)] = (left[i] + right[i]) * 0.5f;

    // 2. Input section: gain + noise gate (runs at base rate)
    input_.process (monoBuffer_.data(), numSamples);

    // Deferred mode switch from crossfade completion
    if (modeSwitchPending_)
    {
        currentMode_ = targetMode_;
        toneStack_.reset();
        modeSwitchPending_ = false;
    }

    // 3. Mode-dependent amp processing
    if (currentMode_ == AmpMode::DSP)
    {
        // DSP mode: upsample -> preamp -> tone stack -> power amp -> downsample
        oversamplingBuffer_.copyFrom (0, 0, monoBuffer_.data(), numSamples);

        juce::dsp::AudioBlock<float> inputBlock (oversamplingBuffer_);
        auto oversampledBlock = oversampling_.processSamplesUp (inputBlock);

        int oversampledNumSamples = static_cast<int> (oversampledBlock.getNumSamples());
        float* oversampledData = oversampledBlock.getChannelPointer (0);

        preamp_.process (oversampledData, oversampledNumSamples);
        toneStack_.process (oversampledData, oversampledNumSamples);
        powerAmp_.process (oversampledData, oversampledNumSamples);

        oversampling_.processSamplesDown (inputBlock);

        auto* downsampled = oversamplingBuffer_.getReadPointer (0);
        std::copy (downsampled, downsampled + numSamples, monoBuffer_.data());
    }
#if DUSKAMP_NAM_SUPPORT
    else if (currentMode_ == AmpMode::NAM)
    {
        // NAM mode: NAM processor at base rate (replaces preamp),
        // then tone stack + power amp at base rate (no oversampling)
        nam_.process (monoBuffer_.data(), numSamples);
        toneStack_.process (monoBuffer_.data(), numSamples);
        powerAmp_.process (monoBuffer_.data(), numSamples);
    }
#endif

    // 4. Apply crossfade gain if we're transitioning between modes
    if (crossfadeSamplesRemaining_ > 0)
    {
        for (int i = 0; i < numSamples && crossfadeSamplesRemaining_ > 0; ++i)
        {
            float fadeStep = 2.0f / static_cast<float> (kCrossfadeSamples);

            monoBuffer_[static_cast<size_t> (i)] *= crossfadeGain_;

            crossfadeGain_ += static_cast<float> (crossfadeDirection_) * fadeStep;
            crossfadeGain_ = std::clamp (crossfadeGain_, 0.0f, 1.0f);
            --crossfadeSamplesRemaining_;

            // At the midpoint (gain reached 0), defer mode switch to next process() call
            if (crossfadeDirection_ == -1 && crossfadeGain_ <= 0.0f)
            {
                modeSwitchPending_ = true;  // defer to next process() call
                crossfadeDirection_ = 1; // start fading in
                crossfadeSamplesRemaining_ = kCrossfadeSamples / 2;
            }
        }
    }

    // 5. Cabinet IR (runs at base rate, it's convolution)
    cabBuffer_.copyFrom (0, 0, monoBuffer_.data(), numSamples);
    cabinet_.process (cabBuffer_);
    std::copy (cabBuffer_.getReadPointer (0),
               cabBuffer_.getReadPointer (0) + numSamples,
               monoBuffer_.data());

    // 6. Copy mono to stereo for post-FX
    for (int i = 0; i < numSamples; ++i)
    {
        left[i]  = monoBuffer_[static_cast<size_t> (i)];
        right[i] = monoBuffer_[static_cast<size_t> (i)];
    }

    // 7. Post FX: delay + reverb (stereo processing)
    postFx_.process (left, right, numSamples);

    // 8. Apply output gain
    for (int i = 0; i < numSamples; ++i)
    {
        left[i]  *= outputGain_;
        right[i] *= outputGain_;
    }
}

void DuskAmpEngine::reset()
{
    input_.reset();
    preamp_.reset();
    toneStack_.reset();
    powerAmp_.reset();
    cabinet_.reset();
    postFx_.reset();
    oversampling_.reset();
#if DUSKAMP_NAM_SUPPORT
    nam_.reset();
#endif
    std::fill (monoBuffer_.begin(), monoBuffer_.end(), 0.0f);
    crossfadeGain_ = 1.0f;
    crossfadeSamplesRemaining_ = 0;
    crossfadeDirection_ = 0;
    modeSwitchPending_ = false;
}

// --- Mode ---

bool DuskAmpEngine::isNAMModelLoaded() const
{
#if DUSKAMP_NAM_SUPPORT
    return nam_.hasModel();
#else
    return false;
#endif
}

void DuskAmpEngine::setAmpMode (AmpMode mode)
{
    if (mode != currentMode_ && crossfadeSamplesRemaining_ == 0)
    {
        targetMode_ = mode;
        crossfadeSamplesRemaining_ = kCrossfadeSamples;
        crossfadeDirection_ = -1;
        crossfadeGain_ = 1.0f;

        // Pre-prepare tone stack for the target mode's sample rate
        // (safe here — called from processBlock's discrete param section, not per-sample)
        if (mode == AmpMode::NAM)
            toneStack_.prepare (sampleRate_);
        else
            toneStack_.prepare (oversampling_.getOversampledSampleRate());
    }
}

// --- Input ---

void DuskAmpEngine::setInputGain (float dB)
{
    input_.setInputGain (dB);
}

void DuskAmpEngine::setGateThreshold (float dB)
{
    input_.setGateThreshold (dB);
}

void DuskAmpEngine::setGateRelease (float ms)
{
    input_.setGateRelease (ms);
}

// --- Preamp ---

void DuskAmpEngine::setPreampGain (float gain01)
{
    preamp_.setGain (gain01);
}

void DuskAmpEngine::setPreampChannel (int channel)
{
    preamp_.setChannel (static_cast<PreampDSP::Channel> (std::clamp (channel, 0, 2)));
}

void DuskAmpEngine::setPreampBright (bool on)
{
    preamp_.setBright (on);
}

// --- Tone Stack ---

void DuskAmpEngine::setToneStackType (int type)
{
    toneStack_.setType (static_cast<ToneStack::Type> (std::clamp (type, 0, 2)));
}

void DuskAmpEngine::setBass (float value01)
{
    toneStack_.setBass (value01);
}

void DuskAmpEngine::setMid (float value01)
{
    toneStack_.setMid (value01);
}

void DuskAmpEngine::setTreble (float value01)
{
    toneStack_.setTreble (value01);
}

// --- Power Amp ---

void DuskAmpEngine::setPowerDrive (float drive01)
{
    powerAmp_.setDrive (drive01);
}

void DuskAmpEngine::setPresence (float value01)
{
    powerAmp_.setPresence (value01);
}

void DuskAmpEngine::setResonance (float value01)
{
    powerAmp_.setResonance (value01);
}

void DuskAmpEngine::setSag (float sag01)
{
    powerAmp_.setSag (sag01);
}

// --- Cabinet ---

void DuskAmpEngine::setCabinetEnabled (bool on)
{
    cabinet_.setEnabled (on);
}

void DuskAmpEngine::setCabinetMix (float mix01)
{
    cabinet_.setMix (mix01);
}

void DuskAmpEngine::setCabinetHiCut (float hz)
{
    cabinet_.setHiCut (hz);
}

void DuskAmpEngine::setCabinetLoCut (float hz)
{
    cabinet_.setLoCut (hz);
}

// --- Post FX ---

void DuskAmpEngine::setDelayEnabled (bool on)
{
    postFx_.setDelayEnabled (on);
}

void DuskAmpEngine::setDelayTime (float ms)
{
    postFx_.setDelayTime (ms);
}

void DuskAmpEngine::setDelayFeedback (float fb01)
{
    postFx_.setDelayFeedback (fb01);
}

void DuskAmpEngine::setDelayMix (float mix01)
{
    postFx_.setDelayMix (mix01);
}

void DuskAmpEngine::setReverbEnabled (bool on)
{
    postFx_.setReverbEnabled (on);
}

void DuskAmpEngine::setReverbMix (float mix01)
{
    postFx_.setReverbMix (mix01);
}

void DuskAmpEngine::setReverbDecay (float decay01)
{
    postFx_.setReverbDecay (decay01);
}

// --- NAM ---

#if DUSKAMP_NAM_SUPPORT
void DuskAmpEngine::setNAMInputLevel (float dB)
{
    nam_.setInputLevel (dB);
}

void DuskAmpEngine::setNAMOutputLevel (float dB)
{
    nam_.setOutputLevel (dB);
}
#endif

// --- Output ---

void DuskAmpEngine::setOutputLevel (float dB)
{
    if (dB == prevOutputDB_) return;
    prevOutputDB_ = dB;
    outputGain_ = std::pow (10.0f, dB / 20.0f);
}

// --- Oversampling ---

void DuskAmpEngine::setOversamplingFactor (int factor)
{
    oversampling_.setFactor (factor);

    double oversampledRate = oversampling_.getOversampledSampleRate();
    preamp_.prepare (oversampledRate);
    powerAmp_.prepare (oversampledRate);

    // Tone stack rate depends on current amp mode
    if (currentMode_ == AmpMode::NAM)
        toneStack_.prepare (sampleRate_);
    else
        toneStack_.prepare (oversampledRate);
}

int DuskAmpEngine::getLatencyInSamples() const
{
    return oversampling_.getLatencyInSamples();
}
