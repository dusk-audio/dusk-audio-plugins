#pragma once

#include "AnalogEmulation/WaveshaperCurves.h"
#include "AnalogEmulation/TransformerEmulation.h"
#include "AnalogEmulation/DCBlocker.h"
#include <atomic>

class PowerAmp
{
public:
    enum class TubeType { EL34 = 0, Tube6L6 = 1, EL84 = 2, KT88 = 3 };

    void prepare (double sampleRate);
    void reset();
    void setDrive (float drive01);
    void setPresence (float value01);
    void setResonance (float value01);
    void setSag (float sag01);
    void setTubeType (TubeType t);
    void process (float* buffer, int numSamples);

private:
    double sampleRate_ = 44100.0;
    float drive_ = 0.3f;
    float driveGain_ = 1.0f;
    float sagAmount_ = 0.3f;
    float sagEnvelope_ = 0.0f;
    float sagAttackCoeff_ = 0.0f;
    float sagReleaseCoeff_ = 0.0f;

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

    std::atomic<TubeType> tubeType_ { TubeType::EL34 };

    void updatePresenceCoeff();
    void updateResonanceCoeff();
    void updateSagCoeffs();
};
