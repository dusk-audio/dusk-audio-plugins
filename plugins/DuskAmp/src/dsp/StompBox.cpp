// StompBox.cpp — Tube Screamer-style boost/overdrive pedal

#include "StompBox.h"
#include "AnalogEmulation/WaveshaperCurves.h"
#include <cmath>
#include <algorithm>

static constexpr float kPi = 3.14159265359f;

void StompBox::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;
    dcBlocker_.prepare (sampleRate, 15.0f);
    updateMidHump();
    updateToneCoeff();
    updateGains();
    reset();
}

void StompBox::reset()
{
    midHump_.reset();
    toneFilter_.reset();
    toneLPState_ = 0.0f;
    dcBlocker_.reset();
}

void StompBox::setGain (float gain01)
{
    gain_ = std::clamp (gain01, 0.0f, 1.0f);
    updateGains();
}

void StompBox::setTone (float tone01)
{
    tone_ = std::clamp (tone01, 0.0f, 1.0f);
    updateToneCoeff();
}

void StompBox::setLevel (float level01)
{
    level_ = std::clamp (level01, 0.0f, 1.0f);
    updateGains();
}

void StompBox::process (float* buffer, int numSamples)
{
    if (! enabled_) return;

    auto& waveshaper = AnalogEmulation::getWaveshaperCurves();

    for (int i = 0; i < numSamples; ++i)
    {
        float sample = buffer[i];

        // 1. Input gain (drive)
        sample *= driveGain_;

        // 2. Mid-hump bandpass — THE Tube Screamer character
        // Cuts bass below ~300Hz and treble above ~3kHz, emphasizes 720Hz
        // The mix of dry + filtered creates the classic TS curve
        float filtered = midHump_.process (sample);
        // Blend: at low gain mostly dry (clean boost), at high gain mostly filtered
        float blend = 0.3f + gain_ * 0.5f;
        sample = sample * (1.0f - blend) + filtered * blend;

        // 3. Asymmetric diode clipper
        // TS uses back-to-back diodes with slightly different characteristics.
        // Positive clips softer (silicon), negative clips harder.
        // Using FET curve for the soft clip characteristic.
        float clipped = waveshaper.process (sample,
            AnalogEmulation::WaveshaperCurves::CurveType::FET);

        // Mix clean and clipped based on drive (at low drive, more clean signal)
        float clipMix = 0.2f + gain_ * 0.8f;
        sample = sample * (1.0f - clipMix) + clipped * clipMix;

        // 4. Tone control (variable low-pass)
        // tone_ = 0: dark (low cutoff), tone_ = 1: bright (high cutoff, nearly bypassed)
        toneLPState_ += (sample - toneLPState_) * toneLPCoeff_;
        sample = toneLPState_;

        // 5. DC block + output level
        sample = dcBlocker_.processSample (sample);
        sample *= outputGain_;

        buffer[i] = sample;
    }
}

// ============================================================================
// Coefficient updates
// ============================================================================

void StompBox::updateMidHump()
{
    // 2nd-order BPF at 720Hz, Q=0.7 (Audio EQ Cookbook)
    float freq = 720.0f;
    float Q = 0.7f;
    float sr = static_cast<float> (sampleRate_);

    float w0 = 2.0f * kPi * freq / sr;
    float cosw0 = std::cos (w0);
    float sinw0 = std::sin (w0);
    float alpha = sinw0 / (2.0f * Q);

    float a0 = 1.0f + alpha;
    float invA0 = 1.0f / a0;

    midHump_.b0 = (sinw0 * 0.5f) * invA0;  // = alpha * invA0 for BPF (peak gain = 1)
    midHump_.b1 = 0.0f;
    midHump_.b2 = -(sinw0 * 0.5f) * invA0;
    midHump_.a1 = (-2.0f * cosw0) * invA0;
    midHump_.a2 = (1.0f - alpha) * invA0;
}

void StompBox::updateToneCoeff()
{
    // Map tone 0-1 to cutoff frequency 800Hz - 8000Hz
    float cutoff = 800.0f + tone_ * 7200.0f;
    float w = 2.0f * kPi * cutoff / static_cast<float> (sampleRate_);
    toneLPCoeff_ = w / (w + 1.0f);
}

void StompBox::updateGains()
{
    // Drive: map 0-1 to gain 1.0 - 20.0 (up to ~26dB of boost)
    driveGain_ = 1.0f + gain_ * 19.0f;
    // Output level: map 0-1 to 0.0 - 1.5 (allows slight boost or cut)
    outputGain_ = level_ * 1.5f;
}
