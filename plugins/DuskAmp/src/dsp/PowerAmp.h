#pragma once

#include "AmpModels.h"
#include "AnalogEmulation/WaveshaperCurves.h"
#include "AnalogEmulation/DCBlocker.h"

// Simplified power amp stage: drive → NFB → pentode waveshaper → sag → OT rolloff.
// Designed for reliable base-rate operation at any block size (including 32-sample sub-blocks).

class PowerAmp
{
public:
    void prepare (double sampleRate);
    void reset();
    void setConfig (const AmpModels::PowerAmpConfig& config);
    void setDrive (float drive01);
    void setPresence (float value01);
    void setResonance (float value01);
    void setSag (float sag01);
    void process (float* buffer, int numSamples);

private:
    AmpModels::PowerAmpConfig config_;
    double sampleRate_ = 44100.0;
    float drive_ = 0.3f;
    float driveGain_ = 1.0f;
    float sagAmount_ = 0.3f;

    // Simple envelope-based sag (replaces unstable RC model)
    float sagEnvelope_ = 0.0f;
    float sagAttackCoeff_ = 0.0f;
    float sagReleaseCoeff_ = 0.0f;

    // Negative feedback loop (one-sample delay)
    float nfbState_ = 0.0f;

    // Presence: one-pole highpass in NFB path
    float presenceAmount_ = 0.0f;  // 0..1
    float presenceState_ = 0.0f;
    float presenceCoeff_ = 0.0f;

    // Resonance: one-pole lowpass in NFB path
    float resonanceAmount_ = 0.0f;  // 0..1
    float resonanceState_ = 0.0f;
    float resonanceCoeff_ = 0.0f;

    // Output transformer: simple one-pole LPF for HF rolloff
    float otLpfState_ = 0.0f;
    float otLpfCoeff_ = 0.0f;

    AnalogEmulation::DCBlocker dcBlocker_;

    void updatePresenceCoeff();
    void updateResonanceCoeff();
    void updateSagCoeffs();
    void updateOTRolloff();
};
