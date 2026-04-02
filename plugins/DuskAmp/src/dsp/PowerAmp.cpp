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
    reset();
}

void PowerAmp::reset()
{
    prevNFBOutput_ = 0.0f;
    nfbPresenceHP_.reset();
    nfbResonanceLPState_ = 0.0f;
    currentDrawEnvelope_ = 0.0f;
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
            curveType_ = AnalogEmulation::WaveshaperCurves::CurveType::EL84;
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

        // 1. Subtract negative feedback from input (1-sample delay)
        //    Scale NFB by inverse of drive gain to keep loop gain < 1.
        //    In real amps, NFB is taken from the output transformer secondary
        //    (post-attenuation), so it naturally scales down at higher output.
        float effectiveNFB = prevNFBOutput_ / std::max (1.0f, driveGain_);
        float inputToTubes = sample - effectiveNFB;

        // 2. Apply drive gain, modulated by power supply sag
        float driven = inputToTubes * driveGain_ * sagMultiplier_;

        // 3. Push-pull power tube waveshaper
        //    In Class AB, one tube handles positive half, another handles negative.
        //    They clip at slightly different thresholds, generating even harmonics
        //    that make power amp distortion sound "musical."
        //    Asymmetry is in INPUT scaling only (not output) to avoid DC offset.
        float saturated;
        if (driven >= 0.0f)
            saturated = waveshaper.process (driven * 1.05f, curveType_);
        else
            saturated = waveshaper.process (driven * 0.95f, curveType_);

        // 4. Compute negative feedback signal
        if (nfbAmount_ > 0.001f)
        {
            float nfbSignal = saturated * nfbAmount_;

            // Presence: 2nd-order resonant HPF in feedback path.
            // Removing HF from NFB = more HF in output. The resonant peak
            // at presenceFreq_ creates the characteristic "bite" of cranked amps.
            float nfbHPOut = nfbPresenceHP_.process (nfbSignal);

            // Subtract HF content from feedback proportional to presence knob
            nfbSignal -= nfbHPOut * presenceAmount_;

            // Resonance: LPF in feedback path — removing LF from NFB = more LF in output
            nfbResonanceLPState_ += (nfbSignal - nfbResonanceLPState_) * nfbResonanceLPCoeff_;
            if (std::abs (nfbResonanceLPState_) < 1e-15f) nfbResonanceLPState_ = 0.0f;

            // Subtract LF content from feedback proportional to resonance knob
            nfbSignal -= nfbResonanceLPState_ * resonanceAmount_;

            prevNFBOutput_ = nfbSignal;
        }
        else
        {
            // No NFB (AC30 mode) — but still apply presence/resonance as feedforward
            // since the AC30's "Cut" control acts on the output directly
            float preBoosted = saturated;

            float hpOut = nfbPresenceHP_.process (preBoosted);

            // For no-NFB amps, presence acts as a cut control (reduces HF)
            // Invert: more "presence" knob = less HF cut = brighter
            saturated -= hpOut * (1.0f - presenceAmount_) * 0.3f;

            nfbResonanceLPState_ += (preBoosted - nfbResonanceLPState_) * nfbResonanceLPCoeff_;
            if (std::abs (nfbResonanceLPState_) < 1e-15f) nfbResonanceLPState_ = 0.0f;
            saturated += nfbResonanceLPState_ * resonanceAmount_ * 0.3f;

            prevNFBOutput_ = 0.0f;
        }

        // 5. Output transformer
        saturated = transformer_.processSample (saturated, 0);

        // 5b. Output transformer resonant peak (leakage inductance)
        saturated = otResonance_.process (saturated);

        // 6. DC block
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
