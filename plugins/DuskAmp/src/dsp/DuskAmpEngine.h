#pragma once

#include "InputSection.h"
#include "PreampDSP.h"
#include "ToneStack.h"
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

    // Amp model (Round/Chime/Punch — propagates to preamp, tone stack, power amp)
    void setAmpModel (int model); // 0=Round, 1=Chime, 2=Punch

    // Input
    void setInputGain (float dB);
    void setGateThreshold (float dB);
    void setGateRelease (float ms);

    // Drive (single knob — distributes to preamp + power amp per model)
    void setDrive (float drive01);

    // Tone stack
    void setBass (float value01);
    void setMid (float value01);
    void setTreble (float value01);

    // Power amp
    void setPowerAmpEnabled (bool on);
    void setPresence (float value01);
    void setResonance (float value01);

    // Cabinet
    void setCabinetEnabled (bool on);
    void setCabinetMix (float mix01);
    void setCabinetHiCut (float hz);
    void setCabinetLoCut (float hz);
    void setCabinetAutoGain (bool on);

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
    int getMaxLatencyInSamples() const;  // latency at 8x (for buffer pre-allocation)

    // Cabinet IR access (for editor to load IRs)
    CabinetIR& getCabinetIR() { return cabinet_; }

    // Power supply sag level (0.6-1.0, for UI indicator)
    float getSagLevel() const { return powerSupply_.bPlusVoltage; }

private:
    InputSection input_;
    PreampDSP preamp_;
    ToneStack toneStack_;
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
    float drive_ = 0.5f;
    bool powerAmpEnabled_ = true;

    // Power supply sag model — shared B+ rail affects both preamp and power amp
    struct PowerSupply
    {
        float bPlusVoltage = 1.0f;     // current B+ (normalized 0-1)
        float rectifierDrop = 0.03f;   // voltage drop (normalized)
        float rectifierR = 0.15f;      // internal resistance (normalized)
        float chargeCoeff = 0.0f;      // capacitor charge rate through rectifier
        float dischargeCoeff = 0.0f;   // capacitor discharge rate through load

        void prepare (double sampleRate, float chargeMs, float dischargeMs)
        {
            if (sampleRate > 0.0)
            {
                chargeCoeff = std::exp (-1000.0f / (chargeMs * static_cast<float> (sampleRate)));
                dischargeCoeff = std::exp (-1000.0f / (dischargeMs * static_cast<float> (sampleRate)));
            }
        }

        void reset() { bPlusVoltage = 1.0f; }

        float process (float currentDraw)
        {
            float target = 1.0f - rectifierDrop - currentDraw * rectifierR;
            target = std::max (target, 0.6f);

            if (target < bPlusVoltage)
                bPlusVoltage = dischargeCoeff * bPlusVoltage + (1.0f - dischargeCoeff) * target;
            else
                bPlusVoltage = chargeCoeff * bPlusVoltage + (1.0f - chargeCoeff) * target;

            return bPlusVoltage;
        }
    };

    PowerSupply powerSupply_;
    int currentAmpModel_ = -1;  // -1 = uninitialized, forces first setAmpModel to run

    // Crossfade state for glitch-free mode/oversampling switching
    static constexpr int kCrossfadeSamples = 128;
    float crossfadeGain_ = 1.0f;
    int crossfadeSamplesRemaining_ = 0;
    int crossfadeDirection_ = 0; // -1 = fading out, +1 = fading in
    bool modeSwitchPending_ = false;
    int pendingOversamplingFactor_ = 0;  // >0 when OS change is pending

    // Scratch buffer for mono processing
    std::vector<float> monoBuffer_;

    // Pre-allocated AudioBuffers (avoid heap allocation on audio thread)
    juce::AudioBuffer<float> oversamplingBuffer_;
    juce::AudioBuffer<float> cabBuffer_;

    void applyOversamplingChange (int factor);

    double sampleRate_ = 44100.0;
    int maxBlockSize_ = 512;
    float prevOutputDB_ = -999.0f;
};
