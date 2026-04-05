#include "PowerAmp.h"
#include <cmath>
#include <algorithm>

static constexpr float kPi = 3.14159265358979323846f;

void PowerAmp::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;
    dcBlocker_.prepare (sampleRate, 10.0f);
    updatePresenceCoeff();
    updateResonanceCoeff();
    updateSagCoeffs();
    updateOTRolloff();
    reset();
}

void PowerAmp::reset()
{
    sagEnvelope_ = 0.0f;
    nfbState_ = 0.0f;
    presenceState_ = 0.0f;
    resonanceState_ = 0.0f;
    otLpfState_ = 0.0f;
    dcBlocker_.reset();
}

void PowerAmp::setConfig (const AmpModels::PowerAmpConfig& config)
{
    config_ = config;
    driveGain_ = 1.0f + drive_ * (config_.maxDriveGain - 1.0f);
    updateSagCoeffs();
    updateOTRolloff();
}

void PowerAmp::setDrive (float drive01)
{
    drive_ = std::clamp (drive01, 0.0f, 1.0f);
    driveGain_ = 1.0f + drive_ * (config_.maxDriveGain - 1.0f);
}

void PowerAmp::setPresence (float value01)
{
    presenceAmount_ = std::clamp (value01, 0.0f, 1.0f);
    updatePresenceCoeff();
}

void PowerAmp::setResonance (float value01)
{
    resonanceAmount_ = std::clamp (value01, 0.0f, 1.0f);
    updateResonanceCoeff();
}

void PowerAmp::setSag (float sag01)
{
    sagAmount_ = std::clamp (sag01, 0.0f, 1.0f);
}

void PowerAmp::process (float* buffer, int numSamples)
{
    auto& waveshaper = AnalogEmulation::getWaveshaperCurves();
    const float nfbRatio = config_.nfbRatio;
    const float classABias = config_.classABias;
    const float otGain = config_.outputTransformerGain;

    for (int i = 0; i < numSamples; ++i)
    {
        float sample = buffer[i];

        // 1. Sag envelope: track signal RMS, reduce effective B+ under load
        float absIn = std::abs (sample) * driveGain_;
        sagEnvelope_ += (absIn > sagEnvelope_ ? sagAttackCoeff_ : sagReleaseCoeff_)
                        * (absIn - sagEnvelope_);
        if (sagEnvelope_ < 1e-15f) sagEnvelope_ = 0.0f;

        // sagRatio: 1.0 = clean supply, drops toward 0 under heavy load
        float sagRatio = 1.0f / (1.0f + sagEnvelope_ * sagAmount_ * config_.psuSagDepth * 4.0f);

        // 2. Negative feedback: subtract filtered output from input
        float feedback = 0.0f;
        if (nfbRatio > 0.0f)
        {
            // Split NFB signal into bands for presence/resonance shaping.
            // Presence: remove HF from feedback → HF passes through with more distortion.
            float hpOut = nfbState_ - presenceState_;
            presenceState_ += hpOut * presenceCoeff_;
            if (std::abs (presenceState_) < 1e-15f) presenceState_ = 0.0f;

            // Resonance: lowpass extracts LF content in feedback.
            resonanceState_ += (nfbState_ - resonanceState_) * resonanceCoeff_;
            if (std::abs (resonanceState_) < 1e-15f) resonanceState_ = 0.0f;

            // Base feedback + shaped HF/LF contributions
            feedback = nfbState_ * nfbRatio;
            feedback += hpOut * presenceAmount_ * nfbRatio * 0.3f;
            feedback += resonanceState_ * resonanceAmount_ * nfbRatio * 0.3f;
        }

        // 3. Apply drive with NFB subtraction and sag compression
        float driven = (sample - feedback) * driveGain_ * sagRatio;

        // 4. Class A bias offset (AC30: asymmetric clipping, more even harmonics)
        if (classABias > 0.0f)
            driven += classABias;

        // 5. Power tube waveshaper
        float saturated = waveshaper.process (driven, config_.powerTubeCurve);

        // 6. Output transformer: HF rolloff (inductance limiting)
        otLpfState_ += otLpfCoeff_ * (saturated - otLpfState_);
        saturated = otLpfState_;

        // 7. DC block
        saturated = dcBlocker_.processSample (saturated);

        // 8. OT turns ratio gain
        saturated *= otGain;

        // Store pre-OT-gain signal for next sample's NFB
        nfbState_ = saturated / otGain;

        buffer[i] = saturated;
    }
}

void PowerAmp::updatePresenceCoeff()
{
    // Presence shelf corner: ~3.5kHz
    float w = 2.0f * kPi * 3500.0f / static_cast<float> (sampleRate_);
    presenceCoeff_ = w / (w + 1.0f);
}

void PowerAmp::updateResonanceCoeff()
{
    // Resonance shelf corner: ~80Hz
    float w = 2.0f * kPi * 80.0f / static_cast<float> (sampleRate_);
    resonanceCoeff_ = w / (w + 1.0f);
}

void PowerAmp::updateSagCoeffs()
{
    float fs = static_cast<float> (sampleRate_);
    if (fs <= 0.0f) return;

    // Attack: fast (~5-12ms depending on config)
    float attackMs = std::max (config_.sagAttackMs, 1.0f);
    sagAttackCoeff_ = 1.0f - std::exp (-1000.0f / (attackMs * fs));

    // Release: slow (~30-120ms depending on config)
    float releaseMs = std::max (config_.sagReleaseMs, 10.0f);
    sagReleaseCoeff_ = 1.0f - std::exp (-1000.0f / (releaseMs * fs));
}

void PowerAmp::updateOTRolloff()
{
    // One-pole LPF at the transformer's HF rolloff frequency
    float fc = config_.transformerHFRolloff;
    float w = 2.0f * kPi * fc / static_cast<float> (sampleRate_);
    otLpfCoeff_ = w / (w + 1.0f);
}
