#pragma once

#include "AnalogEmulation/WaveshaperCurves.h"
#include "AnalogEmulation/TransformerEmulation.h"
#include "AnalogEmulation/DCBlocker.h"

class PowerAmp
{
public:
    enum class AmpModel { Round = 0, Chime = 1, Punch = 2 };

    void prepare (double sampleRate);
    void reset();
    void setDrive (float drive01);
    void setPresence (float value01);
    void setResonance (float value01);
    void setAmpModel (AmpModel model);
    void setSagMultiplier (float sag);  // from power supply model
    void process (float* buffer, int numSamples);

    // Expose current draw for power supply sag computation
    float getCurrentDraw() const { return currentDrawEnvelope_; }

private:
    double sampleRate_ = 44100.0;
    AmpModel currentModel_ = AmpModel::Round;

    float drive_ = 0.3f;
    float driveGain_ = 1.0f;
    float sagMultiplier_ = 1.0f;

    // Per-model power amp config
    AnalogEmulation::WaveshaperCurves::CurveType curveType_ = AnalogEmulation::WaveshaperCurves::CurveType::Triode;
    float driveRange_ = 2.0f;
    float presenceMaxDB_ = 4.0f;
    float resonanceMaxDB_ = 4.0f;

    // Negative Feedback Loop
    float nfbAmount_ = 0.25f;           // 0.0 = no NFB (AC30), 0.25 = moderate (Fender), 0.40 = strong (Marshall)
    float prevNFBOutput_ = 0.0f;        // z^-1 for 1-sample delay in feedback loop

    // Presence: 2nd-order resonant HPF biquad in feedback path
    // Removing HF from NFB = more HF in output. Resonant peak adds "bite."
    struct PresenceBiquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;

        float process (float x)
        {
            float out = b0 * x + z1;
            z1 = b1 * x - a1 * out + z2;
            z2 = b2 * x - a2 * out;
            if (std::abs (z1) < 1e-15f) z1 = 0.0f;
            if (std::abs (z2) < 1e-15f) z2 = 0.0f;
            return out;
        }

        void reset() { z1 = z2 = 0.0f; }
    };

    PresenceBiquad nfbPresenceHP_;       // resonant HPF for presence in feedback path
    float nfbResonanceLPState_ = 0.0f;   // resonance LPF in feedback path
    float nfbResonanceLPCoeff_ = 0.0f;
    float presenceAmount_ = 0.0f;        // 0-1, how much HF removed from NFB
    float resonanceAmount_ = 0.0f;       // 0-1, how much LF removed from NFB

    // Presence/resonance frequencies and Q
    float presenceFreq_ = 3500.0f;
    float presenceQ_ = 0.8f;            // resonance Q (higher = more bite)
    float resonanceFreq_ = 80.0f;

    // Current draw tracking (for power supply model)
    float currentDrawEnvelope_ = 0.0f;
    float currentDrawCoeff_ = 0.0f;

    AnalogEmulation::TransformerEmulation transformer_;
    AnalogEmulation::DCBlocker dcBlocker_;

    // Output transformer resonant peak (leakage inductance + winding capacitance)
    // Adds characteristic "air" and upper-mid presence of tube amps.
    struct OTResonanceBiquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;

        float process (float x)
        {
            float out = b0 * x + z1;
            z1 = b1 * x - a1 * out + z2;
            z2 = b2 * x - a2 * out;
            if (std::abs (z1) < 1e-15f) z1 = 0.0f;
            if (std::abs (z2) < 1e-15f) z2 = 0.0f;
            return out;
        }

        void reset() { z1 = z2 = 0.0f; }
    };

    OTResonanceBiquad otResonance_;
    float otResonanceFreq_ = 7000.0f;   // per-model resonant frequency
    float otResonanceGainDB_ = 2.5f;    // per-model peak amplitude
    float otResonanceQ_ = 1.0f;         // per-model Q

    void updatePresenceCoeff();
    void updateResonanceCoeff();
    void updateDriveGain();
    void updateCurrentDrawCoeff();
    void updateOTResonance();
};
