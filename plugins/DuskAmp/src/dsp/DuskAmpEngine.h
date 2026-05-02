#pragma once

#include "InputSection.h"
#include "PreampDSP.h"
#include "ToneStack.h"
#include "PhaseInverter.h"
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

    // Input — per-mode (DSP and NAM keep independent settings to prevent
    // volume spikes when switching modes; e.g. a NAM model that's much
    // hotter than the DSP path needs lower input gain or it'll blast the
    // user when they toggle to NAM mid-playback).
    void setInputGain         (float dB);   // DSP
    void setGateThreshold     (float dB);   // DSP
    void setGateRelease       (float ms);   // DSP
    void setInputGainNam      (float dB);
    void setGateThresholdNam  (float dB);
    void setGateReleaseNam    (float ms);

    // Preamp (DSP mode)
    void setPreampGain (float gain01);
    void setPreampChannel (int channel); // 0=Clean, 1=Crunch, 2=Lead
    void setPreampBright (bool on);

    // Tone stack — DSP and NAM modes have INDEPENDENT tonestacks so EQ
    // settings persist per mode. setToneStackType (DSP) also couples the
    // power amp model + PI + Marshall voicing in lockstep; the NAM variant
    // only changes the EQ-circuit voicing since NAM has no DSP power amp.
    void setToneStackType (int type);    // DSP — 0=American, 1=British, 2=AC
    void setBass (float value01);        // DSP
    void setMid (float value01);         // DSP
    void setTreble (float value01);      // DSP
    void setToneStackTypeNam (int type); // NAM — EQ flavour only
    void setBassNam (float value01);
    void setMidNam (float value01);
    void setTrebleNam (float value01);

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
    void setCabinetNormalize (bool on);

    // Post FX
    void setDelayEnabled (bool on);
    void setDelayTime (float ms);
    void setDelayFeedback (float fb01);
    void setDelayMix (float mix01);
    void setReverbEnabled (bool on);
    void setReverbMix (float mix01);
    void setReverbDecay (float decay01);

    // Output — per-mode for the same volume-safety reason as input.
    void setOutputLevel    (float dB); // DSP
    void setOutputLevelNam (float dB);

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
    InputSection input_;        // DSP path
    InputSection inputNam_;     // NAM path
    PreampDSP preamp_;
    ToneStack toneStack_;       // DSP path (runs at oversampled rate)
    ToneStack toneStackNam_;    // NAM path (runs at base rate — NAM is non-oversampled)
    PhaseInverter phaseInverter_;

    // Three independent power-amp instances — one per amp model. Each owns
    // its own filter/envelope/transformer state so switching American↔British
    // ↔AC is glitch-free (a 256-oversampled-sample crossfade blends the OLD
    // amp's output into the NEW amp's output). Continuous knobs (drive,
    // presence, resonance, sag) are dispatched to all three so each amp is
    // always primed with the user's current settings.
    PowerAmp powerAmpFender_;
    PowerAmp powerAmpVox_;
    PowerAmp powerAmpMarshall_;
    PowerAmp& ampForType (PowerAmp::AmpType t);

    CabinetIR cabinet_;
    PostFX postFx_;
    DuskAudio::OversamplingManager oversampling_;

#if DUSKAMP_NAM_SUPPORT
    NAMProcessor nam_;
#endif

    AmpMode currentMode_ = AmpMode::DSP;
    AmpMode targetMode_  = AmpMode::DSP;
    float outputGain_    = 1.0f;  // DSP output gain (linear)
    float outputGainNam_ = 1.0f;  // NAM output gain (linear)

    // DSP-vs-NAM mode-switch crossfade state.
    static constexpr int kCrossfadeSamples = 128;
    float crossfadeGain_ = 1.0f;
    int crossfadeSamplesRemaining_ = 0;
    int crossfadeDirection_ = 0; // -1 = fading out, +1 = fading in
    bool modeSwitchPending_ = false;

    // Amp-model-switch crossfade state. Active when the user changes AMP
    // (American/British/AC). Runs at the oversampled rate (where the power
    // amps live) so the audible fade time stays at ~3 ms regardless of OS.
    PowerAmp::AmpType currentAmpType_ = PowerAmp::AmpType::Marshall;
    PowerAmp::AmpType targetAmpType_  = PowerAmp::AmpType::Marshall;
    int   ampCrossfadeSamples_       = 0;     // remaining oversampled samples in fade
    int   ampCrossfadeTotalSamples_  = 0;     // captured total at fade-start (avoids /0)
    juce::AudioBuffer<float> ampScratchBuffer_; // target amp's output during crossfade

    // Scratch buffer for mono processing
    std::vector<float> monoBuffer_;

    // Pre-allocated AudioBuffers (avoid heap allocation on audio thread)
    juce::AudioBuffer<float> oversamplingBuffer_;
    juce::AudioBuffer<float> cabBuffer_;

    double sampleRate_ = 44100.0;
    int maxBlockSize_ = 512;
    float prevOutputDB_    = -999.0f; // last DSP output dB (cache to avoid pow() on every set)
    float prevOutputDBNam_ = -999.0f;
};
