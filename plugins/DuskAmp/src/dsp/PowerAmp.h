// PowerAmp.h — Power amplifier with per-amp-type waveshaper selection
//
// Three key differences per amp type:
//   Fender:  Triode curve (6V6 beam tetrode ≈ triode behavior), heavy NFB
//   Marshall: Pentode curve (EL34), moderate NFB
//   Vox:     EL84 curve, no NFB, Class A bias asymmetry

#pragma once

#include "AnalogEmulation/WaveshaperCurves.h"
#include "AnalogEmulation/TransformerEmulation.h"
#include "AnalogEmulation/DCBlocker.h"

class PowerAmp
{
public:
    // Amp type affects waveshaper curve and character
    enum class AmpType { Fender = 0, Marshall = 1, Vox = 2 };

    void prepare (double sampleRate);
    void reset();
    void setDrive (float drive01);
    void setPresence (float value01);
    void setResonance (float value01);
    void setSag (float sag01);
    void setAmpType (AmpType type);
    void process (float* buffer, int numSamples);

private:
    double sampleRate_ = 44100.0;
    AmpType ampType_ = AmpType::Marshall;

    float drive_ = 0.3f;
    float driveGain_ = 1.0f;
    float sagAmount_ = 0.3f;
    float sagEnvelope_ = 0.0f;       // Post-output sag envelope (for push-pull sag)
    float sagAttackCoeff_ = 0.0f;
    float sagReleaseCoeff_ = 0.0f;
    float inputEnvelope_ = 0.0f;     // Pre-waveshaper envelope (for Class A compression)

    // Per-amp-type settings
    AnalogEmulation::WaveshaperCurves::CurveType curveType_
        = AnalogEmulation::WaveshaperCurves::CurveType::Pentode;
    float biasAsymmetry_ = 0.0f;  // Class A offset (Vox only)
    float maxDriveGain_ = 2.5f;   // Maximum drive multiplier
    float stageGain_ = 100.0f;    // Makeup gain — represents missing voltage gain from normalized chain
    float outputGain_ = 1.0f;     // Post-waveshaper output scaling

    // Presence: high shelf in negative feedback
    float presenceFreq_ = 3500.0f;
    float presenceGain_ = 0.0f; // dB

    // Resonance: low shelf in negative feedback
    float resonanceFreq_ = 80.0f;
    float resonanceGain_ = 0.0f; // dB

    // Simple one-pole filters for presence/resonance
    float presenceState_ = 0.0f;
    float presenceCoeff_ = 0.0f;
    float resonanceState_ = 0.0f;
    float resonanceCoeff_ = 0.0f;

    AnalogEmulation::TransformerEmulation transformer_;
    AnalogEmulation::DCBlocker dcBlocker_;

    void updatePresenceCoeff();
    void updateResonanceCoeff();
    void updateSagCoeffs();
    void updateAmpTypeParams();
};
