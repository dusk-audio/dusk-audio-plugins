#include "DuskAmpEngine.h"
#include "BinaryData.h"
#include <cmath>
#include <algorithm>

void DuskAmpEngine::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;

    monoBuffer_.resize (static_cast<size_t> (maxBlockSize * 8), 0.0f);

    oversamplingBuffer_.setSize (1, maxBlockSize, false, true, true);
    cabBuffer_.setSize (1, maxBlockSize, false, true, true);

    oversampling_.prepare (sampleRate, maxBlockSize, 1);

    double oversampledRate = oversampling_.getOversampledSampleRate();

    input_.prepare (sampleRate);
    preamp_.prepare (oversampledRate);
    cabinet_.prepare (sampleRate, maxBlockSize);
    postFx_.prepare (sampleRate, maxBlockSize);

    if (currentMode_ == AmpMode::NAM)
    {
        toneStack_.prepare (sampleRate);
        powerAmp_.prepare (sampleRate);
    }
    else
    {
        toneStack_.prepare (oversampledRate);
        powerAmp_.prepare (oversampledRate);
    }

#if DUSKAMP_NAM_SUPPORT
    nam_.prepare (sampleRate, maxBlockSize);
#endif

    // Load default factory cab if no IR loaded yet
    if (! cabinet_.isLoaded())
        setAmpModel (currentAmpModel_);
}

void DuskAmpEngine::process (float* left, float* right, int numSamples)
{
    // 1. Sum stereo input to mono
    for (int i = 0; i < numSamples; ++i)
        monoBuffer_[static_cast<size_t> (i)] = (left[i] + right[i]) * 0.5f;

    // 2. Input section: gain + noise gate
    input_.process (monoBuffer_.data(), numSamples);

    // Deferred mode switch from crossfade completion
    if (modeSwitchPending_)
    {
        currentMode_ = targetMode_;
        toneStack_.reset();
        modeSwitchPending_ = false;
    }

    // 3. Compute power supply sag from current draw and distribute to both stages
    {
        float currentDraw = powerAmp_.getCurrentDraw();
        float sagMult = powerSupply_.process (currentDraw);
        preamp_.setSagMultiplier (sagMult);
        powerAmp_.setSagMultiplier (sagMult);
    }

    // 4. Mode-dependent amp processing
    if (currentMode_ == AmpMode::DSP)
    {
        oversamplingBuffer_.copyFrom (0, 0, monoBuffer_.data(), numSamples);

        juce::dsp::AudioBlock<float> inputBlock (oversamplingBuffer_.getArrayOfWritePointers(),
                                                  1, static_cast<size_t> (numSamples));
        auto oversampledBlock = oversampling_.processSamplesUp (inputBlock);

        int oversampledNumSamples = static_cast<int> (oversampledBlock.getNumSamples());
        float* oversampledData = oversampledBlock.getChannelPointer (0);

        preamp_.process (oversampledData, oversampledNumSamples);
        toneStack_.process (oversampledData, oversampledNumSamples);

        if (powerAmpEnabled_)
            powerAmp_.process (oversampledData, oversampledNumSamples);

        oversampling_.processSamplesDown (inputBlock);

        auto* downsampled = oversamplingBuffer_.getReadPointer (0);
        std::copy (downsampled, downsampled + numSamples, monoBuffer_.data());
    }
#if DUSKAMP_NAM_SUPPORT
    else if (currentMode_ == AmpMode::NAM)
    {
        // NAM profiles model the full amp (preamp + power amp + often cab/mic).
        // We only apply: NAM → tone stack (with makeup gain) → output.
        // NO power amp (the NAM model IS the amp — re-amping it adds unwanted
        // double distortion, compression, and transformer coloring).

        nam_.process (monoBuffer_.data(), numSamples);

        // If no NAM model is loaded, output silence (not dry guitar)
        if (! nam_.hasModel())
        {
            std::fill (monoBuffer_.begin(),
                       monoBuffer_.begin() + numSamples, 0.0f);
        }
        else
        {
            // Apply tone stack as a post-NAM EQ shaper.
            // The TMB circuit model is passive (max 0dB, -10dB at noon).
            // Apply makeup gain so the tone controls have audible effect.
            toneStack_.process (monoBuffer_.data(), numSamples);

            constexpr float kNAMToneStackMakeup = 3.16f;  // ~10 dB
            for (int i = 0; i < numSamples; ++i)
                monoBuffer_[static_cast<size_t> (i)] *= kNAMToneStackMakeup;
        }
    }
#endif

    // 4. Crossfade gain for mode transitions
    if (crossfadeSamplesRemaining_ > 0)
    {
        for (int i = 0; i < numSamples && crossfadeSamplesRemaining_ > 0; ++i)
        {
            float fadeStep = 2.0f / static_cast<float> (kCrossfadeSamples);

            monoBuffer_[static_cast<size_t> (i)] *= crossfadeGain_;

            crossfadeGain_ += static_cast<float> (crossfadeDirection_) * fadeStep;
            crossfadeGain_ = std::clamp (crossfadeGain_, 0.0f, 1.0f);
            --crossfadeSamplesRemaining_;

            if (crossfadeDirection_ == -1 && crossfadeGain_ <= 0.0f)
            {
                // At zero-crossing: apply any pending switch
                if (pendingOversamplingFactor_ > 0)
                    applyOversamplingChange (pendingOversamplingFactor_);
                else
                    modeSwitchPending_ = true;

                crossfadeDirection_ = 1;
                crossfadeSamplesRemaining_ = kCrossfadeSamples / 2;
            }
        }
    }

    // 5. Cabinet IR
    cabBuffer_.copyFrom (0, 0, monoBuffer_.data(), numSamples);
    juce::AudioBuffer<float> cabView (cabBuffer_.getArrayOfWritePointers(), 1, numSamples);
    cabinet_.process (cabView);
    std::copy (cabBuffer_.getReadPointer (0),
               cabBuffer_.getReadPointer (0) + numSamples,
               monoBuffer_.data());

    // 6. Copy mono to stereo for post-FX
    for (int i = 0; i < numSamples; ++i)
    {
        left[i]  = monoBuffer_[static_cast<size_t> (i)];
        right[i] = monoBuffer_[static_cast<size_t> (i)];
    }

    // 7. Post FX: delay + reverb (stereo)
    postFx_.process (left, right, numSamples);

    // 8. Output gain
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
    powerSupply_.reset();
    std::fill (monoBuffer_.begin(), monoBuffer_.end(), 0.0f);
    crossfadeGain_ = 1.0f;
    crossfadeSamplesRemaining_ = 0;
    crossfadeDirection_ = 0;
    modeSwitchPending_ = false;
    pendingOversamplingFactor_ = 0;
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

        if (mode == AmpMode::NAM)
        {
            toneStack_.prepare (sampleRate_);
            powerAmp_.prepare (sampleRate_);
        }
        else
        {
            toneStack_.prepare (oversampling_.getOversampledSampleRate());
            powerAmp_.prepare (oversampling_.getOversampledSampleRate());
        }
    }
}

// --- Amp Model ---

void DuskAmpEngine::setAmpModel (int model)
{
    int clamped = std::clamp (model, 0, 2);
    if (clamped == currentAmpModel_)
        return;  // no change — skip expensive LUT rebuild

    currentAmpModel_ = clamped;
    preamp_.setAmpModel (static_cast<PreampDSP::AmpModel> (clamped));
    preamp_.initializeKorenLUTs();  // rebuild transfer function for new model
    toneStack_.setModel (static_cast<ToneStack::Model> (clamped));
    powerAmp_.setAmpModel (static_cast<PowerAmp::AmpModel> (clamped));

    // Configure power supply per rectifier type
    switch (clamped)
    {
        case 0: // Round — GZ34 tube rectifier (spongy bloom)
            powerSupply_.rectifierDrop = 0.03f;
            powerSupply_.rectifierR = 0.15f;
            powerSupply_.prepare (sampleRate_, 20.0f, 200.0f);
            break;
        case 1: // Chime — GZ34 tube rectifier (less spongy than Fender)
            powerSupply_.rectifierDrop = 0.03f;
            powerSupply_.rectifierR = 0.12f;
            powerSupply_.prepare (sampleRate_, 15.0f, 150.0f);
            break;
        case 2: // Punch — solid-state rectifier (fast/tight)
            powerSupply_.rectifierDrop = 0.003f;
            powerSupply_.rectifierR = 0.01f;
            powerSupply_.prepare (sampleRate_, 2.0f, 20.0f);
            break;
    }

    // Auto-load matched factory cabinet IR (unless user loaded a custom IR)
    if (cabinet_.isFactoryIR() || ! cabinet_.isLoaded())
    {
        switch (clamped)
        {
            case 0:
                cabinet_.loadIRFromBinaryData (BinaryData::round_1x12_jensen_wav,
                                                BinaryData::round_1x12_jensen_wavSize,
                                                "1x12 Jensen C12N");
                break;
            case 1:
                cabinet_.loadIRFromBinaryData (BinaryData::chime_2x12_blue_wav,
                                                BinaryData::chime_2x12_blue_wavSize,
                                                "2x12 Blue Alnico");
                break;
            case 2:
                cabinet_.loadIRFromBinaryData (BinaryData::punch_4x12_greenback_wav,
                                                BinaryData::punch_4x12_greenback_wavSize,
                                                "4x12 Greenback");
                break;
        }
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

// --- Drive ---

void DuskAmpEngine::setDrive (float drive01)
{
    drive_ = std::clamp (drive01, 0.0f, 1.0f);
    preamp_.setDrive (drive_);
    powerAmp_.setDrive (drive_);
}

// --- Tone Stack ---

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

void DuskAmpEngine::setPowerAmpEnabled (bool on)
{
    powerAmpEnabled_ = on;
}

void DuskAmpEngine::setPresence (float value01)
{
    powerAmp_.setPresence (value01);
}

void DuskAmpEngine::setResonance (float value01)
{
    powerAmp_.setResonance (value01);
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

void DuskAmpEngine::setCabinetAutoGain (bool on)
{
    cabinet_.setAutoGain (on);
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
    // If a crossfade is already active (mode or OS), apply immediately
    if (crossfadeSamplesRemaining_ > 0)
    {
        applyOversamplingChange (factor);
        return;
    }

    // Start fade-out, defer the actual switch
    pendingOversamplingFactor_ = factor;
    crossfadeSamplesRemaining_ = kCrossfadeSamples;
    crossfadeDirection_ = -1;
    crossfadeGain_ = 1.0f;
}

void DuskAmpEngine::applyOversamplingChange (int factor)
{
    oversampling_.setFactor (factor);

    double oversampledRate = oversampling_.getOversampledSampleRate();
    preamp_.prepare (oversampledRate);

    if (currentMode_ == AmpMode::NAM)
    {
        toneStack_.prepare (sampleRate_);
        powerAmp_.prepare (sampleRate_);
    }
    else
    {
        toneStack_.prepare (oversampledRate);
        powerAmp_.prepare (oversampledRate);
    }

    // Clear all state to prevent transient spike from stale oversampling data
    oversampling_.reset();
    std::fill (monoBuffer_.begin(), monoBuffer_.end(), 0.0f);
    powerSupply_.reset();
    pendingOversamplingFactor_ = 0;
}

int DuskAmpEngine::getLatencyInSamples() const
{
    return oversampling_.getLatencyInSamples();
}

int DuskAmpEngine::getMaxLatencyInSamples() const
{
    return oversampling_.getMaxLatencyInSamples();
}
