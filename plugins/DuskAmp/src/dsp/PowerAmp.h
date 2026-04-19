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

    // Per-amp input scaler into the waveshaper. Reflects different tube
    // transconductances: EL34 is ~3× more sensitive (Gm ≈ 11 mA/V) than 6V6
    // (Gm ≈ 4.1 mA/V), so Marshall's Pentode curve saturates earlier at
    // equivalent pre-stage signal. Without this, Marshall would slam the
    // waveshaper at every drive level even with preamp Clean selected.
    float inputScale_ = 1.0f;

    // Phase-inverter + power-tube voltage gain. The tone stack now
    // self-compensates to unity midband (see ToneStack::outputGain_), so this
    // is purely the PI voltage gain — no longer has to undo 30 dB of network
    // loss. Calibrated so Lead-channel cranked drive peaks near ±1.5 into
    // the waveshaper (heavy saturation), Clean-channel clean drive stays
    // near ±0.3 (mild saturation).
    static constexpr float kPreampMakeup = 1.0f;

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
