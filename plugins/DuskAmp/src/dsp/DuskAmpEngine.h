#pragma once

#include "InputSection.h"
#include "AmpModels.h"
#include "wdf/WDFPreamp.h"
#include "ToneStack.h"
#include <memory>
#include "PowerAmp.h"
#include "CabinetIR.h"
#include "ProceduralCab.h"
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

    // Amp Type (selects preamp chain, tone stack, power amp config)
    void setAmpType (int type);
    int getAmpType() const { return static_cast<int> (currentAmpType_); }

    // Preamp
    void setPreampGain (float gain01);
    void setPreampBright (bool on);

    // Tone stack
    void setBass (float value01);
    void setMid (float value01);
    void setTreble (float value01);
    void setToneCut (float value01);

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
    void setCabinetMicPosition (float pos01);

    // Post FX
    void setDelayEnabled (bool on);
    void setDelayTime (float ms);
    void setDelayFeedback (float fb01);
    void setDelayMix (float mix01);
    void setReverbEnabled (bool on);
    void setReverbMix (float mix01);
    void setReverbDecay (float decay01);

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
    std::unique_ptr<WDFPreamp> preamp_;
    ToneStack toneStack_;
    PowerAmp powerAmp_;
    CabinetIR cabinet_;
    ProceduralCab proceduralCab_;
    PostFX postFx_;
    DuskAudio::OversamplingManager oversampling_;

#if DUSKAMP_NAM_SUPPORT
    NAMProcessor nam_;
#endif

    AmpMode currentMode_ = AmpMode::DSP;
    AmpMode targetMode_  = AmpMode::DSP;
    AmpType currentAmpType_ = AmpType::FenderDeluxe;

    void createPreamp (AmpType type);
    bool currentBright_ = false;
    float outputGain_ = 1.0f;
    float inputGainLinear_ = 1.0f; // Linear input gain for NAM fast path

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

    // Oversampling gain compensation: 4x produces less in-band energy
    // (reduced aliasing means less intermodulation in the bass/mids)
    float oversamplingGainComp_ = 1.0f;
};
