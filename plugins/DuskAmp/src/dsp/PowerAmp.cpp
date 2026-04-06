// PowerAmp.cpp — Per-amp power amplifier with negative feedback loop
//
// The key innovation over the old implementation is the negative feedback loop:
//
//   feedback = nfbRatio * presenceFilter(previousOutput)
//   driven = (input - feedback) * driveGain * sagReduction
//   output = waveshaper(driven) → transformer
//
// Presence/Resonance modify the frequency content of the feedback signal.
// Higher Presence → less HF feedback → more HF distortion (brighter breakup).
// Higher Resonance → less LF feedback → more LF energy (tighter low end).

#include "PowerAmp.h"
#include <cmath>
#include <algorithm>

static constexpr float kPi = 3.14159265359f;

// ============================================================================
// Per-amp configurations
// ============================================================================

PowerAmp::PowerAmpConfig PowerAmp::getConfigForAmpType (AmpType type)
{
    switch (type)
    {
        case AmpType::Fender:
            return {
                AnalogEmulation::WaveshaperCurves::CurveType::Triode,
                0.7f,       // Heavy NFB → clean headroom, lots of control
                20.0f,      // Slow sag attack (20ms) — 6L6 tubes are beefy
                200.0f,     // Slow sag release — gentle recovery
                3.0f,       // Max drive gain (less aggressive than Marshall)
                0.0f,       // Symmetric push-pull (Class AB)
                0.80f,      // Transformer: high saturation threshold
                0.10f,      // Moderate saturation amount
                1.2f,       // LF saturation multiplier
                12000.0f    // Output transformer HF rolloff
            };

        case AmpType::Vox:
            return {
                AnalogEmulation::WaveshaperCurves::CurveType::EL84,
                0.0f,       // NO NFB — raw, harmonically rich (Class A)
                5.0f,       // Fast sag attack (5ms) — EL84s compress quickly
                60.0f,      // Fast sag release — bouncy feel
                3.5f,       // More drive available (single-ended needs it)
                0.15f,      // Class A bias asymmetry → 2nd harmonic content
                0.65f,      // Lower saturation threshold (EL84 clips earlier)
                0.15f,      // More saturation
                1.4f,       // More LF saturation (loose bottom end)
                10000.0f    // Earlier HF rolloff (darker transformer)
            };

        case AmpType::Marshall:
        default:
            return {
                AnalogEmulation::WaveshaperCurves::CurveType::Pentode,
                0.3f,       // Moderate NFB → controlled bark
                10.0f,      // Moderate sag attack (10ms)
                100.0f,     // Moderate sag release
                4.0f,       // More drive headroom (for high gain)
                0.0f,       // Symmetric push-pull (Class AB)
                0.70f,      // Moderate saturation threshold
                0.12f,      // Moderate saturation
                1.3f,       // Moderate LF saturation
                14000.0f    // Later HF rolloff (brighter transformer)
            };
    }
}

// ============================================================================
// Lifecycle
// ============================================================================

void PowerAmp::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;
    config_ = getConfigForAmpType (currentType_);

    // Configure output transformer for this amp type
    auto profile = AnalogEmulation::TransformerProfile::createActive (
        config_.xfmrSatThreshold,
        config_.xfmrSatAmount,
        config_.xfmrLFSat,
        config_.xfmrHFRolloff,
        15.0f,    // DC blocking freq
        0.01f,    // h2 harmonic
        0.005f,   // h3 harmonic
        0.7f);    // even/odd ratio

    transformer_.setProfile (profile);
    transformer_.prepare (sampleRate, 1); // mono

    dcBlocker_.prepare (sampleRate, 10.0f);

    updateDriveGain();
    updateSagCoeffs();
    updateFilterCoeffs();
    reset();
}

void PowerAmp::reset()
{
    sagEnvelope_ = 0.0f;
    presenceFilterState_ = 0.0f;
    resonanceFilterState_ = 0.0f;
    previousOutput_ = 0.0f;
    transformer_.reset();
    dcBlocker_.reset();
}

// ============================================================================
// Setters
// ============================================================================

void PowerAmp::setDrive (float drive01)
{
    drive_ = std::clamp (drive01, 0.0f, 1.0f);
    updateDriveGain();
}

void PowerAmp::setPresence (float value01)
{
    presenceAmount_ = std::clamp (value01, 0.0f, 1.0f);
}

void PowerAmp::setResonance (float value01)
{
    resonanceAmount_ = std::clamp (value01, 0.0f, 1.0f);
}

void PowerAmp::setSag (float sag01)
{
    sagAmount_ = std::clamp (sag01, 0.0f, 1.0f);
}

void PowerAmp::setAmpType (AmpType type)
{
    if (type == currentType_) return;
    currentType_ = type;
    // Re-prepare with new config (safe — called from processBlock param section)
    prepare (sampleRate_);
}

// ============================================================================
// Process — the core power amp with negative feedback loop
// ============================================================================

void PowerAmp::process (float* buffer, int numSamples)
{
    auto& waveshaper = AnalogEmulation::getWaveshaperCurves();

    float nfb = config_.nfbRatio;
    float biasOffset = config_.biasAsymmetry;

    for (int i = 0; i < numSamples; ++i)
    {
        float input = buffer[i];

        // --- 1. Compute negative feedback signal from previous output ---
        // Presence filter: HPF in feedback path
        // Higher presenceAmount_ → less HF in feedback → more HF distortion
        float hpFb = previousOutput_ - presenceFilterState_;
        presenceFilterState_ += hpFb * presenceFilterCoeff_;

        // Resonance filter: LPF in feedback path
        // Higher resonanceAmount_ → less LF in feedback → more bass energy
        resonanceFilterState_ += (previousOutput_ - resonanceFilterState_) * resonanceFilterCoeff_;

        // Construct the feedback signal:
        // Start with full-band feedback, then subtract the controlled bands
        float feedbackSignal = previousOutput_;
        // Presence: remove HF from feedback (adds HF distortion)
        feedbackSignal -= hpFb * presenceAmount_ * 0.5f;
        // Resonance: remove LF from feedback (adds LF energy)
        feedbackSignal -= resonanceFilterState_ * resonanceAmount_ * 0.5f;

        // Apply NFB ratio (0 for Vox = no feedback, 0.7 for Fender = heavy)
        float feedback = nfb * feedbackSignal;

        // --- 2. Sag: envelope follows input level, reduces headroom ---
        float absInput = std::abs (input);
        if (absInput > sagEnvelope_)
            sagEnvelope_ = sagAttackCoeff_ * sagEnvelope_ + (1.0f - sagAttackCoeff_) * absInput;
        else
            sagEnvelope_ = sagReleaseCoeff_ * sagEnvelope_;
        if (sagEnvelope_ < 1e-15f) sagEnvelope_ = 0.0f;

        float sagReduction = 1.0f - sagAmount_ * std::min (sagEnvelope_, 1.0f) * 0.3f;

        // --- 3. Drive stage with feedback subtraction ---
        float driven = (input - feedback) * driveGain_ * sagReduction;

        // Class A bias asymmetry (Vox): offset the signal to create
        // asymmetric clipping → even harmonics (2nd, 4th)
        driven += biasOffset;

        // --- 4. Power tube saturation (waveshaper) ---
        float saturated = waveshaper.process (driven, config_.curveType);

        // Remove bias offset after saturation (keeps the harmonic content,
        // removes DC offset)
        saturated -= biasOffset * 0.5f;

        // --- 5. Output transformer ---
        saturated = transformer_.processSample (saturated, 0);

        // --- 6. DC block ---
        saturated = dcBlocker_.processSample (saturated);

        // Flush denormals
        if (std::abs (presenceFilterState_) < 1e-15f) presenceFilterState_ = 0.0f;
        if (std::abs (resonanceFilterState_) < 1e-15f) resonanceFilterState_ = 0.0f;

        // Store for next sample's feedback loop
        previousOutput_ = saturated;

        buffer[i] = saturated;
    }
}

// ============================================================================
// Coefficient updates
// ============================================================================

void PowerAmp::updateDriveGain()
{
    driveGain_ = 1.0f + drive_ * (config_.maxDriveGain - 1.0f);
}

void PowerAmp::updateSagCoeffs()
{
    float sr = static_cast<float> (sampleRate_);
    if (sr > 0.0f)
    {
        sagAttackCoeff_ = std::exp (-1000.0f / (config_.sagAttackMs * sr));
        sagReleaseCoeff_ = std::exp (-1000.0f / (config_.sagReleaseMs * sr));
    }
}

void PowerAmp::updateFilterCoeffs()
{
    float sr = static_cast<float> (sampleRate_);
    // Presence HPF coefficient
    float wP = 2.0f * kPi * kPresenceFreq / sr;
    presenceFilterCoeff_ = wP / (wP + 1.0f);
    // Resonance LPF coefficient
    float wR = 2.0f * kPi * kResonanceFreq / sr;
    resonanceFilterCoeff_ = wR / (wR + 1.0f);
}
