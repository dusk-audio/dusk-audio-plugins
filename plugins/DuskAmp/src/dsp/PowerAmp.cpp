#include "PowerAmp.h"
#include "AnalogEmulation/HardwareProfiles.h"
#include <cmath>
#include <algorithm>

static constexpr float kPi = 3.14159265358979323846f;

void PowerAmp::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;

    setAmpModel (currentModel_);

    transformer_.prepare (sampleRate, 1);
    dcBlocker_.prepare (sampleRate, 10.0f);

    updatePresenceCoeff();
    updateResonanceCoeff();
    updateCurrentDrawCoeff();
    updateSagCoeffs();
    updateSpeakerCoeffs();
    reset();
}

void PowerAmp::reset()
{
    prevNFBOutput_ = 0.0f;
    nfbPresenceHP_.reset();
    nfbResonanceLPState_ = 0.0f;
    currentDrawEnvelope_ = 0.0f;
    sagFastEnv_ = 0.0f;
    sagSlowEnv_ = 0.0f;
    speakerLFState_ = 0.0f;
    speakerHFState_ = 0.0f;
    transformer_.reset();
    otResonance_.reset();
    dcBlocker_.reset();
}

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

void PowerAmp::setSagMultiplier (float sag)
{
    sagMultiplier_ = std::clamp (sag, 0.5f, 1.0f);
}

void PowerAmp::setAmpModel (AmpModel model)
{
    currentModel_ = model;

    switch (model)
    {
        case AmpModel::Round:
        {
            // 2x 6V6GT, tube rectifier (GZ34)
            curveType_ = AnalogEmulation::WaveshaperCurves::CurveType::Triode;
            driveRange_ = 2.0f;
            nfbAmount_ = 0.25f;         // moderate NFB (Fender)
            presenceMaxDB_ = 4.0f;
            resonanceMaxDB_ = 4.0f;
            presenceFreq_ = 3500.0f;
            presenceQ_ = 0.7f;          // gentle resonance (Fender is smooth)
            resonanceFreq_ = 80.0f;

            otResonanceFreq_ = 6000.0f;   // Fender OT: lower resonance, warm
            otResonanceGainDB_ = 2.0f;
            otResonanceQ_ = 0.8f;

            auto profile = AnalogEmulation::TransformerProfile::createActive (
                0.70f, 0.14f, 1.4f, 12000.0f, 15.0f, 0.015f, 0.004f, 0.8f);
            transformer_.setProfile (profile);
            break;
        }

        case AmpModel::Chime:
        {
            // 4x EL84, tube rectifier — NO negative feedback
            // EL84 is a smaller pentode; use Pentode curve (closest match)
            curveType_ = AnalogEmulation::WaveshaperCurves::CurveType::Pentode;
            driveRange_ = 2.5f;
            nfbAmount_ = 0.0f;          // NO NFB — the AC30 signature
            presenceMaxDB_ = 6.0f;
            resonanceMaxDB_ = 5.0f;
            presenceFreq_ = 3500.0f;
            presenceQ_ = 0.7f;          // used for feedforward cut control
            resonanceFreq_ = 100.0f;

            otResonanceFreq_ = 7500.0f;   // Vox OT: bright, airy
            otResonanceGainDB_ = 3.0f;
            otResonanceQ_ = 1.0f;

            auto profile = AnalogEmulation::TransformerProfile::createActive (
                0.72f, 0.13f, 1.3f, 14000.0f, 15.0f, 0.012f, 0.008f, 0.6f);
            transformer_.setProfile (profile);
            break;
        }

        case AmpModel::Punch:
        {
            // 4x EL34, solid-state rectifier
            curveType_ = AnalogEmulation::WaveshaperCurves::CurveType::Pentode;
            driveRange_ = 3.0f;
            nfbAmount_ = 0.40f;         // strong NFB (Marshall)
            presenceMaxDB_ = 6.0f;
            resonanceMaxDB_ = 6.0f;
            presenceFreq_ = 3500.0f;
            presenceQ_ = 1.2f;          // more resonance (Marshall "bite")
            resonanceFreq_ = 80.0f;

            otResonanceFreq_ = 8000.0f;   // Marshall OT: tight, aggressive
            otResonanceGainDB_ = 2.5f;
            otResonanceQ_ = 1.2f;

            auto profile = AnalogEmulation::TransformerProfile::createActive (
                0.80f, 0.12f, 1.2f, 15000.0f, 15.0f, 0.008f, 0.006f, 0.5f);
            transformer_.setProfile (profile);
            break;
        }
    }

    updateDriveGain();
    updatePresenceCoeff();
    updateResonanceCoeff();
    updateOTResonance();
}

void PowerAmp::process (float* buffer, int numSamples)
{
    auto& waveshaper = AnalogEmulation::getWaveshaperCurves();

    for (int i = 0; i < numSamples; ++i)
    {
        float sample = buffer[i];

        // Track current draw (envelope of input signal for power supply model)
        float absInput = std::abs (sample);
        currentDrawEnvelope_ += (absInput - currentDrawEnvelope_) * currentDrawCoeff_;
        if (currentDrawEnvelope_ < 1e-15f) currentDrawEnvelope_ = 0.0f;

        // === Multi-stage sag envelope ===
        // Fast attack (~10ms), dual release: fast (~50ms) + slow bloom (~500ms)
        if (absInput > sagFastEnv_)
            sagFastEnv_ = sagAttackCoeff_ * sagFastEnv_ + (1.0f - sagAttackCoeff_) * absInput;
        else
            sagFastEnv_ = sagReleaseFastCoeff_ * sagFastEnv_;
        if (sagFastEnv_ < 1e-15f) sagFastEnv_ = 0.0f;

        if (sagFastEnv_ > sagSlowEnv_)
            sagSlowEnv_ = sagAttackCoeff_ * sagSlowEnv_ + (1.0f - sagAttackCoeff_) * sagFastEnv_;
        else
            sagSlowEnv_ = sagReleaseSlowCoeff_ * sagSlowEnv_;
        if (sagSlowEnv_ < 1e-15f) sagSlowEnv_ = 0.0f;

        // Combined sag: fast + slow bloom, weighted
        float sagEnv = sagFastEnv_ * 0.6f + sagSlowEnv_ * 0.4f;
        float sagFactor = 1.0f - std::min (sagEnv, 1.0f) * 0.15f;

        // === 1. Reactive NFB: subtract frequency-shaped previous output from input ===
        float effectiveNFB = prevNFBOutput_ / std::max (1.0f, driveGain_);

        // Reactive speaker impedance: impedance rises at resonance (~80Hz) and HF (~4kHz)
        // Higher impedance = less effective NFB (more coloration)
        speakerLFState_ += (absInput - speakerLFState_) * speakerLFCoeff_;
        if (std::abs (speakerLFState_) < 1e-15f) speakerLFState_ = 0.0f;
        float hfNfbAbs = std::abs (effectiveNFB);
        speakerHFState_ += (hfNfbAbs - speakerHFState_) * speakerHFCoeff_;
        if (std::abs (speakerHFState_) < 1e-15f) speakerHFState_ = 0.0f;
        float impedanceFactor = 1.0f + speakerLFState_ * 0.15f + speakerHFState_ * 0.1f;

        float inputToTubes = sample - effectiveNFB / impedanceFactor;

        // === 2. Apply drive gain, modulated by power supply sag ===
        float driven = inputToTubes * driveGain_ * sagMultiplier_ * sagFactor;

        // === 3. Phase Inverter (triode stage, clips more symmetrically) ===
        float piDriven = driven * 0.8f;
        float piSaturated = waveshaper.process (piDriven,
                                                 AnalogEmulation::WaveshaperCurves::CurveType::Triode);

        // === 4. Push-Pull Class AB power tubes ===
        // Split into push and pull halves from PI output
        float pushInput =  piSaturated + crossoverBias_;
        float pullInput = -piSaturated + crossoverBias_;

        // Clamp negative portions (tube cutoff in class B region)
        if (pushInput < 0.0f) pushInput = 0.0f;
        if (pullInput < 0.0f) pullInput = 0.0f;

        // Each half through power tube waveshaper
        float pushOut = waveshaper.process (pushInput, curveType_);
        float pullOut = waveshaper.process (pullInput, curveType_);

        // Sum push-pull (push positive, pull inverted back)
        float saturated = pushOut - pullOut;

        // === 5. Compute negative feedback signal ===
        if (nfbAmount_ > 0.001f)
        {
            float nfbSignal = saturated * nfbAmount_;

            // Presence: 2nd-order resonant HPF in feedback path.
            float nfbHPOut = nfbPresenceHP_.process (nfbSignal);
            nfbSignal -= nfbHPOut * presenceAmount_;

            // Resonance: LPF in feedback path
            nfbResonanceLPState_ += (nfbSignal - nfbResonanceLPState_) * nfbResonanceLPCoeff_;
            if (std::abs (nfbResonanceLPState_) < 1e-15f) nfbResonanceLPState_ = 0.0f;
            nfbSignal -= nfbResonanceLPState_ * resonanceAmount_;

            prevNFBOutput_ = nfbSignal;
        }
        else
        {
            // No NFB (AC30 mode) — presence/resonance as feedforward
            float preBoosted = saturated;

            float hpOut = nfbPresenceHP_.process (preBoosted);
            saturated -= hpOut * (1.0f - presenceAmount_) * 0.3f;

            nfbResonanceLPState_ += (preBoosted - nfbResonanceLPState_) * nfbResonanceLPCoeff_;
            if (std::abs (nfbResonanceLPState_) < 1e-15f) nfbResonanceLPState_ = 0.0f;
            saturated += nfbResonanceLPState_ * resonanceAmount_ * 0.3f;

            prevNFBOutput_ = 0.0f;
        }

        // === 6. Output transformer ===
        saturated = transformer_.processSample (saturated, 0);

        // 6b. Output transformer resonant peak (leakage inductance)
        saturated = otResonance_.process (saturated);

        // === 7. DC block ===
        saturated = dcBlocker_.processSample (saturated);

        buffer[i] = saturated;
    }
}

void PowerAmp::updatePresenceCoeff()
{
    // 2nd-order HPF (Audio EQ Cookbook) with resonant Q for presence "bite"
    float w0 = 2.0f * kPi * presenceFreq_ / static_cast<float> (sampleRate_);
    float cosw0 = std::cos (w0);
    float sinw0 = std::sin (w0);
    float alpha = sinw0 / (2.0f * presenceQ_);

    float a0 = 1.0f + alpha;
    float invA0 = 1.0f / a0;

    nfbPresenceHP_.b0 = ((1.0f + cosw0) * 0.5f) * invA0;
    nfbPresenceHP_.b1 = -(1.0f + cosw0) * invA0;
    nfbPresenceHP_.b2 = nfbPresenceHP_.b0;
    nfbPresenceHP_.a1 = (-2.0f * cosw0) * invA0;
    nfbPresenceHP_.a2 = (1.0f - alpha) * invA0;
}

void PowerAmp::updateResonanceCoeff()
{
    float w = 2.0f * kPi * resonanceFreq_ / static_cast<float> (sampleRate_);
    nfbResonanceLPCoeff_ = w / (w + 1.0f);
}

void PowerAmp::updateDriveGain()
{
    driveGain_ = 1.0f + drive_ * driveRange_;
}

void PowerAmp::updateCurrentDrawCoeff()
{
    // ~5ms envelope for current draw tracking
    float w = 2.0f * kPi * 200.0f / static_cast<float> (sampleRate_);
    currentDrawCoeff_ = w / (w + 1.0f);
}

void PowerAmp::updateOTResonance()
{
    // Peaking EQ biquad (Audio EQ Cookbook) for output transformer resonant peak.
    // Models the interaction of leakage inductance and winding capacitance.
    float A  = std::pow (10.0f, otResonanceGainDB_ / 40.0f);
    float w0 = 2.0f * kPi * otResonanceFreq_ / static_cast<float> (sampleRate_);
    float cosw0 = std::cos (w0);
    float sinw0 = std::sin (w0);
    float alpha = sinw0 / (2.0f * otResonanceQ_);

    float a0 = 1.0f + alpha / A;
    if (std::abs (a0) < 1e-7f)
    {
        otResonance_.b0 = 1.0f;
        otResonance_.b1 = otResonance_.b2 = 0.0f;
        otResonance_.a1 = otResonance_.a2 = 0.0f;
        return;
    }

    float invA0 = 1.0f / a0;
    otResonance_.b0 = (1.0f + alpha * A) * invA0;
    otResonance_.b1 = (-2.0f * cosw0) * invA0;
    otResonance_.b2 = (1.0f - alpha * A) * invA0;
    otResonance_.a1 = (-2.0f * cosw0) * invA0;
    otResonance_.a2 = (1.0f - alpha / A) * invA0;
}

void PowerAmp::updateSagCoeffs()
{
    float fs = static_cast<float> (sampleRate_);
    if (fs <= 0.0f) return;

    // Attack: ~10ms
    sagAttackCoeff_ = std::exp (-1000.0f / (10.0f * fs));
    // Fast release: ~50ms (power supply cap initial discharge)
    sagReleaseFastCoeff_ = std::exp (-1000.0f / (50.0f * fs));
    // Slow release: ~500ms (filter cap slow recovery / bloom)
    sagReleaseSlowCoeff_ = std::exp (-1000.0f / (500.0f * fs));
}

void PowerAmp::updateSpeakerCoeffs()
{
    float fs = static_cast<float> (sampleRate_);
    if (fs <= 0.0f) return;

    // Speaker LF resonance tracking: ~80Hz
    float wLF = 2.0f * kPi * 80.0f / fs;
    speakerLFCoeff_ = wLF / (wLF + 1.0f);

    // Speaker HF impedance rise: ~4kHz
    float wHF = 2.0f * kPi * 4000.0f / fs;
    speakerHFCoeff_ = wHF / (wHF + 1.0f);
}
