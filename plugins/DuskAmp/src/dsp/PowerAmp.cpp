// PowerAmp.cpp — Power amplifier with per-amp-type behavior
//
// Push-pull (Fender/Marshall): signal split into two anti-phase halves,
//   each processed through the waveshaper, then summed differentially.
//   This cancels even harmonics (2nd, 4th, 6th) — the key push-pull signature.
//
// Class A (Vox): single-ended processing with bias offset that pushes the
//   signal into the asymmetric region of the waveshaper, creating even
//   harmonics and earlier compression.

#include "PowerAmp.h"
#include "AnalogEmulation/HardwareProfiles.h"
#include <cmath>
#include <algorithm>

static constexpr float kPi = 3.14159265358979323846f;

void PowerAmp::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;

    auto profile = AnalogEmulation::TransformerProfile::createActive (
        0.75f, 0.12f, 1.3f, 14000.0f, 15.0f, 0.01f, 0.005f, 0.7f);

    transformer_.setProfile (profile);
    transformer_.prepare (sampleRate, 1);
    dcBlocker_.prepare (sampleRate, 10.0f);

    powerSupply_.prepare (sampleRate);
    updateAmpTypeParams();
    updatePresenceCoeff();
    updateResonanceCoeff();
    reset();
}

void PowerAmp::reset()
{
    powerSupply_.reset();
    presenceState_ = 0.0f;
    resonanceState_ = 0.0f;
    transformer_.reset();
    dcBlocker_.reset();
}

void PowerAmp::setDrive (float drive01)
{
    drive_ = std::clamp (drive01, 0.0f, 1.0f);
    driveGain_ = 0.8f + drive_ * (maxDriveGain_ - 0.8f);
}

void PowerAmp::setPresence (float value01)
{
    presenceGain_ = std::clamp (value01, 0.0f, 1.0f) * 6.0f;
    updatePresenceCoeff();
}

void PowerAmp::setResonance (float value01)
{
    resonanceGain_ = std::clamp (value01, 0.0f, 1.0f) * 6.0f;
    updateResonanceCoeff();
}

void PowerAmp::setSag (float sag01)
{
    sagAmount_ = std::clamp (sag01, 0.0f, 1.0f);
    powerSupply_.setDepth (sagAmount_);
}

void PowerAmp::setAmpType (AmpType type)
{
    ampType_ = type;
    updateAmpTypeParams();
    driveGain_ = 0.8f + drive_ * (maxDriveGain_ - 0.8f);
}

void PowerAmp::updateAmpTypeParams()
{
    switch (ampType_)
    {
        case AmpType::Fender:
            curveType_ = AnalogEmulation::WaveshaperCurves::CurveType::Opto_Tube;
            biasAsymmetry_ = 0.0f;
            maxDriveGain_ = 2.0f;
            // Fender: 1 preamp stage (Clean) ≈ 35x gain, tone stack ≈ -20dB
            // Net preamp gain ≈ 35 * 0.1 = 3.5x, but we get ~0.004 peak from 0.25 input
            // Need ~80x to bring signal to ±0.3 at the waveshaper input
            stageGain_ = 80.0f;
            outputGain_ = 0.5f;  // Attenuate after waveshaper to stay < 0dBFS
            powerSupply_.setType (PowerSupply::Type::Tube5AR4);
            break;

        case AmpType::Vox:
            curveType_ = AnalogEmulation::WaveshaperCurves::CurveType::EL84;
            biasAsymmetry_ = 0.35f;  // Asymmetry for Class A even harmonics
            maxDriveGain_ = 2.5f;
            // Vox signal levels: input ~0.001 (weakest after preamp+tonestack)
            // Need stageGain to bring to ~0.5-1.0 at waveshaper input
            // 0.001 * 40 * 1.65 = 0.066 — still low, but Class A compression
            // won't squash it as badly
            stageGain_ = 8.0f;        // Very low — keep signal in waveshaper's linear region at clean
            outputGain_ = 4.0f;      // High post-waveshaper gain to compensate
            powerSupply_.setType (PowerSupply::Type::TubeGZ34);
            break;

        case AmpType::Marshall:
        default:
            curveType_ = AnalogEmulation::WaveshaperCurves::CurveType::Pentode;
            biasAsymmetry_ = 0.0f;
            maxDriveGain_ = 3.0f;
            stageGain_ = 35.0f;      // Pentode curve is very sensitive — low stageGain needed
            outputGain_ = 0.8f;
            powerSupply_.setType (PowerSupply::Type::Silicon);
            break;
    }
}

void PowerAmp::process (float* buffer, int numSamples)
{
    auto& waveshaper = AnalogEmulation::getWaveshaperCurves();
    bool isPushPull = (ampType_ != AmpType::Vox);

    for (int i = 0; i < numSamples; ++i)
    {
        float sample = buffer[i];

        // --- 1. Drive + stage gain ---
        // stageGain_ provides the missing voltage amplification that a real
        // preamp/phase-inverter delivers but our normalized chain doesn't.
        float driven = sample * stageGain_ * driveGain_;

        // --- 2. Power supply: RC-model B+ sag driven by power-tube load ---
        // Returns normalized rail voltage in [vFloor, 1.0]. Lower = more sag.
        float vBplus = powerSupply_.processSample (driven);

        // --- 3. Power tube stage ---
        float saturated;

        if (isPushPull)
        {
            // Push-pull: each tube handles one half of the waveform.
            // This produces only odd harmonics (even harmonics cancel).
            //
            // Model: sign(x) * f(|x|) guarantees odd symmetry.
            // Use processWithDrive to blend between linear (low drive)
            // and saturated (high drive) to control THD at clean settings.
            float sign = (driven >= 0.0f) ? 1.0f : -1.0f;
            float absDriven = std::abs (driven);

            // Remove DC offset from the curve by subtracting f(0)
            float curveAtZero = waveshaper.process (0.0f, curveType_);
            float absSaturated = waveshaper.process (absDriven, curveType_) - curveAtZero;

            saturated = absSaturated * sign;
        }
        else
        {
            // Class A (Vox): single-ended, no push-pull cancellation.
            // Class A tubes always conduct near max current; when B+ sags,
            // the tube can't swing as far, so the signal compresses.
            // vBplus (∈ [vFloor, 1.0]) drives this naturally — stiff rail =
            // no compression, sagged rail = proportional headroom loss.
            float compDriven = driven * vBplus;

            // Waveshaper then asymmetry
            saturated = waveshaper.process (compDriven, curveType_);

            // Even harmonics: asymmetric soft-clip where positive clips softer.
            // Use different tanh coefficients for positive and negative halves.
            // This ALWAYS creates even harmonics regardless of signal level.
            float k = biasAsymmetry_;
            float posScale = 1.0f - k;   // e.g. 0.9 — positive clips earlier
            float negScale = 1.0f + k;   // e.g. 1.1 — negative clips later
            if (saturated >= 0.0f)
                saturated = std::tanh (saturated * posScale) / posScale;
            else
                saturated = std::tanh (saturated * negScale) / negScale;
        }

        // --- 3b. Output scaling + supply sag ---
        // Real physics: power-tube plate swing is bounded by B+. When the
        // supply sags, maximum output amplitude drops in proportion. Per-amp
        // sag depth and time constants are baked into PowerSupply by ampType.
        saturated *= outputGain_ * vBplus;

        // --- 5. Presence + Resonance ---
        float hpOut = saturated - presenceState_;
        presenceState_ += hpOut * presenceCoeff_;
        if (std::abs (presenceState_) < 1e-15f) presenceState_ = 0.0f;

        resonanceState_ += (saturated - resonanceState_) * resonanceCoeff_;
        if (std::abs (resonanceState_) < 1e-15f) resonanceState_ = 0.0f;

        saturated += hpOut * (presenceGain_ / 6.0f) * 0.3f;
        saturated += resonanceState_ * (resonanceGain_ / 6.0f) * 0.3f;

        // --- 5. Output transformer ---
        saturated = transformer_.processSample (saturated, 0);

        // --- 6. DC block ---
        saturated = dcBlocker_.processSample (saturated);

        buffer[i] = saturated;
    }
}

void PowerAmp::updatePresenceCoeff()
{
    float w = 2.0f * kPi * presenceFreq_ / static_cast<float> (sampleRate_);
    presenceCoeff_ = w / (w + 1.0f);
}

void PowerAmp::updateResonanceCoeff()
{
    float w = 2.0f * kPi * resonanceFreq_ / static_cast<float> (sampleRate_);
    resonanceCoeff_ = w / (w + 1.0f);
}

