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
#include "PowerSupply.h"

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
    PowerSupply powerSupply_;

    // Per-amp-type settings. stageGain/outputGain used to vary 80/35/8 and
    // 0.5/0.8/4 per amp to patch the preamp's net attenuation — that's now the
    // PreampDSP::outputMakeup_'s job. Here we use a single constant that
    // compensates the ~-30 dB tone-stack loss and represents the phase
    // inverter + power-tube voltage gain, same across all amps.
    AnalogEmulation::WaveshaperCurves::CurveType curveType_
        = AnalogEmulation::WaveshaperCurves::CurveType::Pentode;
    float biasAsymmetry_ = 0.0f;  // Class A offset (Vox only)
    float maxDriveGain_ = 2.5f;   // Maximum drive multiplier (amp-type-dependent character)

    // Phase-inverter + power-tube voltage gain. Picked to undo the tone-stack
    // loss at flat settings so a Lead-channel signal post-tone-stack arrives
    // at the waveshaper around ±1.
    static constexpr float kPreampMakeup = 35.0f;

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
    void updateAmpTypeParams();
};
