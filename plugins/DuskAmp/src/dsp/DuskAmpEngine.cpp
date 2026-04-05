#include "DuskAmpEngine.h"
#include "wdf/FenderDeluxeWDF.h"
#include "wdf/VoxAC30WDF.h"
#include "wdf/MarshallPlexiWDF.h"
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

    // Initialize WDF preamp at BASE rate (Koren solver doesn't need oversampling)
    if (!preamp_)
        createPreamp (currentAmpType_);
    preamp_->prepare (sampleRate);

    toneStack_.setType (static_cast<ToneStack::Type> (
        static_cast<int> (AmpModels::getToneStackTopology (currentAmpType_))));

    // All DSP runs at base rate (WDF doesn't need oversampling)
    powerAmp_.prepare (sampleRate);
    powerAmp_.setConfig (AmpModels::getPowerAmpConfig (currentAmpType_));

    cabinet_.prepare (sampleRate, maxBlockSize);
    proceduralCab_.prepare (sampleRate);
    proceduralCab_.setAmpType (currentAmpType_);
    postFx_.prepare (sampleRate, maxBlockSize);

    toneStack_.prepare (sampleRate);

#if DUSKAMP_NAM_SUPPORT
    nam_.prepare (sampleRate, maxBlockSize);
#endif
}

void DuskAmpEngine::process (float* left, float* right, int numSamples)
{
#if DUSKAMP_NAM_SUPPORT
    // =========================================================================
    // NAM FAST PATH — matches the official NAM plugin's ProcessBlock exactly.
    // Input gain → NAM inference → output gain → stereo broadcast.
    // No tone stack, no cabinet, no post-FX, no crossfade, no noise gate.
    // NAM models capture the full amp chain internally.
    // =========================================================================
    if (currentMode_ == AmpMode::NAM)
    {
        // 1. Sum stereo to mono
        for (int i = 0; i < numSamples; ++i)
            monoBuffer_[static_cast<size_t> (i)] = (left[i] + right[i]) * 0.5f;

        // 2. Apply input gain (no noise gate — NAM handles dynamics)
        for (int i = 0; i < numSamples; ++i)
            monoBuffer_[static_cast<size_t> (i)] *= inputGainLinear_;

        // 3. NAM inference — single call, full buffer
        nam_.process (monoBuffer_.data(), numSamples);

        // 4. Tone stack EQ (optional — user can shape tone after NAM)
        toneStack_.process (monoBuffer_.data(), numSamples);

        // 5. Cabinet simulation (optional — only if user has cab enabled)
        if (cabinet_.isLoaded())
        {
            cabBuffer_.copyFrom (0, 0, monoBuffer_.data(), numSamples);
            juce::AudioBuffer<float> cabView (cabBuffer_.getArrayOfWritePointers(), 1, numSamples);
            cabinet_.process (cabView);
            std::copy (cabBuffer_.getReadPointer (0),
                       cabBuffer_.getReadPointer (0) + numSamples,
                       monoBuffer_.data());
        }
        else if (proceduralCab_.isEnabled())
        {
            proceduralCab_.process (monoBuffer_.data(), numSamples);
        }

        // 6. Apply output gain and broadcast mono to stereo
        for (int i = 0; i < numSamples; ++i)
        {
            float out = monoBuffer_[static_cast<size_t> (i)] * outputGain_;
            left[i]  = out;
            right[i] = out;
        }

        // 7. Post FX: delay + reverb (stereo, only if enabled)
        postFx_.process (left, right, numSamples);

        return;
    }
#endif

    // =========================================================================
    // DSP MODE — full analog-modeled signal chain
    // =========================================================================

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

    // 3. WDF preamp → tone stack → power amp (all at base rate)
    preamp_->process (monoBuffer_.data(), numSamples);
    toneStack_.process (monoBuffer_.data(), numSamples);
    powerAmp_.process (monoBuffer_.data(), numSamples);

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

            if (crossfadeDirection_ == -1 && crossfadeGain_ <= 0.0f)
            {
                modeSwitchPending_ = true;
                crossfadeDirection_ = 1;
                crossfadeSamplesRemaining_ = kCrossfadeSamples / 2;
            }
        }
    }

    // 5. Cabinet simulation (runs at base rate)
    if (cabinet_.isLoaded())
    {
        cabBuffer_.copyFrom (0, 0, monoBuffer_.data(), numSamples);
        juce::AudioBuffer<float> cabView (cabBuffer_.getArrayOfWritePointers(), 1, numSamples);
        cabinet_.process (cabView);
        std::copy (cabBuffer_.getReadPointer (0),
                   cabBuffer_.getReadPointer (0) + numSamples,
                   monoBuffer_.data());
    }
    else
    {
        proceduralCab_.process (monoBuffer_.data(), numSamples);
    }

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
    if (preamp_) preamp_->reset();
    toneStack_.reset();
    powerAmp_.reset();
    cabinet_.reset();
    proceduralCab_.reset();
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
    if (mode == currentMode_)
        return;

    currentMode_ = mode;

    // All modes run at base rate
    toneStack_.prepare (sampleRate_);

    toneStack_.reset();
}

void DuskAmpEngine::createPreamp (AmpType type)
{
    switch (type)
    {
        case AmpType::FenderDeluxe:  preamp_ = std::make_unique<FenderDeluxeWDF>(); break;
        case AmpType::VoxAC30:       preamp_ = std::make_unique<VoxAC30WDF>(); break;
        case AmpType::MarshallPlexi: preamp_ = std::make_unique<MarshallPlexiWDF>(); break;
        default:                     preamp_ = std::make_unique<FenderDeluxeWDF>(); break;
    }
}

// --- Amp Type (unified control) ---

void DuskAmpEngine::setAmpType (int type)
{
    auto newType = static_cast<AmpType> (std::clamp (type, 0, kNumAmpTypes - 1));
    if (newType == currentAmpType_) return;
    currentAmpType_ = newType;

    // Create new WDF preamp for this amp model
    createPreamp (newType);
    preamp_->prepare (sampleRate_);

    // Switch tone stack topology
    auto topology = AmpModels::getToneStackTopology (newType);
    toneStack_.setType (static_cast<ToneStack::Type> (static_cast<int> (topology)));

    // Re-apply bright state to new preamp
    preamp_->setBright (currentBright_);

    // Switch power amp configuration
    powerAmp_.setConfig (AmpModels::getPowerAmpConfig (newType));

    // Update procedural cab for this amp type
    proceduralCab_.setAmpType (newType);
}

// --- Input ---

void DuskAmpEngine::setInputGain (float dB)
{
    input_.setInputGain (dB);
    inputGainLinear_ = std::pow (10.0f, dB / 20.0f);
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
    preamp_->setGain (gain01);
}

void DuskAmpEngine::setPreampBright (bool on)
{
    currentBright_ = on;
    preamp_->setBright (on);
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

void DuskAmpEngine::setToneCut (float value01)
{
    toneStack_.setToneCut (value01);
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
    proceduralCab_.setEnabled (on);
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

void DuskAmpEngine::setCabinetMicPosition (float pos01)
{
    proceduralCab_.setMicPosition (pos01);
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

    // At 4x, reduced aliasing means less intermodulation energy folds back into
    // the audio band. Compensate with a small gain boost to match perceived level.
    oversamplingGainComp_ = (factor >= 4) ? 1.12f : 1.0f; // ~1 dB

    // All DSP runs at base rate
    preamp_->prepare (sampleRate_);
    powerAmp_.prepare (sampleRate_);
    toneStack_.prepare (sampleRate_);
}

int DuskAmpEngine::getLatencyInSamples() const
{
    return oversampling_.getLatencyInSamples();
}
