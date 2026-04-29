// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "InputSection.h"
#include "StompBox.h"
#include "PreampModel.h"
#include "ToneStackModel.h"
#include "PowerAmp.h"
#include "CabinetIR.h"
#include "PostFX.h"
#include "Oversampling.h"  // from shared/

#if DUSKAMP_NAM_SUPPORT
 #include "NAMProcessor.h"
#endif

class DuskAmpEngine
{
public:
    enum class AmpMode { DSP, NAM };

    void prepare (double sampleRate, int maxBlockSize);
    void process (float* left, float* right, int numSamples);
    void reset();

    // Mode
    void setAmpMode (AmpMode mode);
    AmpMode getCurrentMode() const { return currentMode_; }
    bool isNAMModelLoaded() const;

    // Input
    void setInputGain (float dB);
    void setGateThreshold (float dB);
    void setGateRelease (float ms);

    // Amp type (controls preamp model + tone stack topology + power amp config)
    void setAmpType (int type); // 0=Fender, 1=Marshall, 2=Vox

    // Preamp (DSP mode)
    void setPreampGain (float gain01);
    void setPreampBright (bool on);

    // Tone stack (topology is set automatically by setAmpType)
    void setBass (float value01);
    void setMid (float value01);
    void setTreble (float value01);

    // Power amp
    void setPowerDrive (float drive01);
    void setPresence (float value01);
    void setResonance (float value01);
    void setSag (float sag01);

    // Cabinet
    void setCabinetEnabled (bool on);
    void setCabinetMix (float mix01);
    void setCabinetHiCut (float hz);
    void setCabinetLoCut (float hz);

    // Stomp box (before amp)
    void setBoostEnabled (bool on);
    void setBoostGain (float gain01);
    void setBoostTone (float tone01);
    void setBoostLevel (float level01);

    // Post FX — Delay
    void setDelayEnabled (bool on);
    void setDelayType (int type); // 0=Digital, 1=Analog, 2=Tape
    void setDelayTime (float ms);
    void setDelayFeedback (float fb01);
    void setDelayMix (float mix01);

    // Post FX — Reverb
    void setReverbEnabled (bool on);
    void setReverbMix (float mix01);
    void setReverbDecay (float decay01);
    void setReverbPreDelay (float ms);
    void setReverbDamping (float damping01);
    void setReverbSize (float size01);

    // Output
    void setOutputLevel (float dB);

    // NAM (guarded by DUSKAMP_NAM_SUPPORT)
#if DUSKAMP_NAM_SUPPORT
    void setNAMInputLevel (float dB);
    void setNAMOutputLevel (float dB);
    NAMProcessor& getNAMProcessor() { return nam_; }
#endif

    // Oversampling
    void setOversamplingFactor (int factor);
    int getLatencyInSamples() const;

    // Cabinet IR access (for editor to load IRs)
    CabinetIR& getCabinetIR() { return cabinet_; }

private:
    InputSection input_;
    StompBox stompBox_;
    std::unique_ptr<PreampModel> preampPool_[3];  // Pre-created models: [Fender, Marshall, Vox]
    PreampModel* preamp_ = nullptr;               // Active model (borrowed from pool, not owned)
    AmpType currentAmpType_ = AmpType::Marshall;
    ToneStackModel toneStack_;
    PowerAmp powerAmp_;
    CabinetIR cabinet_;
    PostFX postFx_;
    DuskAudio::OversamplingManager oversampling_;

#if DUSKAMP_NAM_SUPPORT
    NAMProcessor nam_;
#endif

    AmpMode currentMode_ = AmpMode::DSP;
    AmpMode targetMode_  = AmpMode::DSP;
    float outputGain_ = 1.0f;

    // Crossfade state for glitch-free mode switching
    static constexpr int kCrossfadeSamples = 128;
    float crossfadeGain_ = 1.0f;
    int crossfadeSamplesRemaining_ = 0;
    int crossfadeDirection_ = 0; // -1 = fading out, +1 = fading in
    bool modeSwitchPending_ = false;

    // Scratch buffer for mono processing
    std::vector<float> monoBuffer_;

    // Pre-allocated AudioBuffers (avoid heap allocation on audio thread)
    juce::AudioBuffer<float> oversamplingBuffer_;
    juce::AudioBuffer<float> cabBuffer_;

    double sampleRate_ = 44100.0;
    int maxBlockSize_ = 512;
    float prevOutputDB_ = -999.0f;
};
