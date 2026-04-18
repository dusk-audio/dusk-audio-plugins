#include "PowerAmp.h"
#include "AnalogEmulation/HardwareProfiles.h"
#include <cmath>
#include <algorithm>

static constexpr float kPi = 3.14159265358979323846f;

void PowerAmp::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;

    // Configure transformer for output transformer character
    auto profile = AnalogEmulation::TransformerProfile::createActive (
        0.75f,    // saturation threshold
        0.12f,    // saturation amount
        1.3f,     // low freq saturation
        14000.0f, // HF rolloff (output transformer rolls off earlier)
        15.0f,    // DC blocking freq
        0.01f,    // h2
        0.005f,   // h3
        0.7f);    // even/odd ratio

    transformer_.setProfile (profile);
    transformer_.prepare (sampleRate, 1); // mono

    dcBlocker_.prepare (sampleRate, 10.0f);

    updatePresenceCoeff();
    updateResonanceCoeff();
    updateSagCoeffs();
    reset();
}

void PowerAmp::reset()
{
    sagEnvelope_ = 0.0f;
    presenceState_ = 0.0f;
    resonanceState_ = 0.0f;
    transformer_.reset();
    dcBlocker_.reset();
}

void PowerAmp::setDrive (float drive01)
{
    drive_ = std::clamp (drive01, 0.0f, 1.0f);
    // Map 0-1 to gain multiplier 1.0 to 4.0
    driveGain_ = 1.0f + drive_ * 3.0f;
}

void PowerAmp::setPresence (float value01)
{
    // Map 0-1 to 0dB to +6dB presence boost
    presenceGain_ = std::clamp (value01, 0.0f, 1.0f) * 6.0f;
    updatePresenceCoeff();
}

void PowerAmp::setResonance (float value01)
{
    // Map 0-1 to 0dB to +6dB resonance boost
    resonanceGain_ = std::clamp (value01, 0.0f, 1.0f) * 6.0f;
    updateResonanceCoeff();
}

void PowerAmp::setSag (float sag01)
{
    sagAmount_ = std::clamp (sag01, 0.0f, 1.0f);
    updateSagCoeffs();
}

void PowerAmp::setTubeType (TubeType t)
{
    tubeType_.store (t, std::memory_order_relaxed);
}

static AnalogEmulation::WaveshaperCurves::CurveType curveForTube (PowerAmp::TubeType)
{
    return AnalogEmulation::WaveshaperCurves::CurveType::Pentode;
}

void PowerAmp::process (float* buffer, int numSamples)
{
    auto& waveshaper = AnalogEmulation::getWaveshaperCurves();
    const auto tubeCurve = curveForTube (tubeType_.load (std::memory_order_relaxed));

    for (int i = 0; i < numSamples; ++i)
    {
        float sample = buffer[i];

        // 1. Sag: envelope follows abs(input), reduces available headroom
        float absInput = std::abs (sample);

        if (absInput > sagEnvelope_)
            sagEnvelope_ = sagAttackCoeff_ * sagEnvelope_ + (1.0f - sagAttackCoeff_) * absInput;
        else
            sagEnvelope_ = sagReleaseCoeff_ * sagEnvelope_;
        if (sagEnvelope_ < 1e-15f) sagEnvelope_ = 0.0f;

        // Sag reduces drive gain proportionally
        float sagReduction = 1.0f - sagAmount_ * std::min (sagEnvelope_, 1.0f) * 0.3f;

        // 2. Apply drive gain, reduced by sag
        float driven = sample * driveGain_ * sagReduction;

        // 3. Power tube waveshaper
        float saturated = waveshaper.process (driven, tubeCurve);

        // 4. Presence + Resonance EQ (feedforward — filters track pre-boost signal)
        float preBoosted = saturated;

        float hpOut = preBoosted - presenceState_;
        presenceState_ += hpOut * presenceCoeff_;
        if (std::abs (presenceState_) < 1e-15f) presenceState_ = 0.0f;

        resonanceState_ += (preBoosted - resonanceState_) * resonanceCoeff_;
        if (std::abs (resonanceState_) < 1e-15f) resonanceState_ = 0.0f;

        saturated += hpOut * (presenceGain_ / 6.0f) * 0.3f;
        saturated += resonanceState_ * (resonanceGain_ / 6.0f) * 0.3f;

        // 6. Output transformer
        saturated = transformer_.processSample (saturated, 0);

        // 7. DC block
        saturated = dcBlocker_.processSample (saturated);

        buffer[i] = saturated;
    }
}

void PowerAmp::updatePresenceCoeff()
{
    // One-pole highpass coefficient for presence frequency
    float w = 2.0f * kPi * presenceFreq_ / static_cast<float> (sampleRate_);
    presenceCoeff_ = w / (w + 1.0f);
}

void PowerAmp::updateResonanceCoeff()
{
    // One-pole lowpass coefficient for resonance frequency
    float w = 2.0f * kPi * resonanceFreq_ / static_cast<float> (sampleRate_);
    resonanceCoeff_ = w / (w + 1.0f);
}

void PowerAmp::updateSagCoeffs()
{
    // Sag attack ~10ms, release ~100ms
    float attackMs = 10.0f;
    float releaseMs = 100.0f;

    if (sampleRate_ > 0.0)
    {
        sagAttackCoeff_ = std::exp (-1000.0f / (attackMs * static_cast<float> (sampleRate_)));
        sagReleaseCoeff_ = std::exp (-1000.0f / (releaseMs * static_cast<float> (sampleRate_)));
    }
}
