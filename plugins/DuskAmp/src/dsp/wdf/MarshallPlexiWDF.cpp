#include "MarshallPlexiWDF.h"
#include <cmath>

static constexpr float kPi = 3.14159265358979323846f;

MarshallPlexiWDF::MarshallPlexiWDF()
{
    // V1A: first preamp stage — Marshall 1959 values
    v1a_.setCircuit (0.022e-6f,   // Cin: input coupling cap
                     1.0e6f,      // Rg: grid leak
                     100.0e3f,    // Rp: plate load
                     820.0f,      // Rk: cathode resistor
                     0.68e-6f,    // Ck: cathode bypass (partial — 285Hz corner)
                     330.0f);     // B+

    // V1B: second preamp stage — identical tube circuit
    // Cin here acts as the inter-stage coupling cap (0.022uF into 1M grid leak ≈ 7.2Hz HPF)
    v1b_.setCircuit (0.022e-6f,   // Cin: inter-stage coupling
                     1.0e6f,      // Rg: grid leak
                     100.0e3f,    // Rp: plate load
                     820.0f,      // Rk: cathode resistor
                     0.68e-6f,    // Ck: cathode bypass
                     330.0f);     // B+
}

void MarshallPlexiWDF::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;
    v1a_.prepare (sampleRate);
    v1b_.prepare (sampleRate);

    updateBrightCap();
    reset();
}

void MarshallPlexiWDF::reset()
{
    v1a_.reset();
    v1b_.reset();
    brightCapState_ = 0.0f;
}

void MarshallPlexiWDF::setGain (float gain01)
{
    // Audio taper: quadratic approximation of log pot
    volumeAtten_ = gain01 * gain01;
    updateBrightCap();
}

void MarshallPlexiWDF::setBright (bool on)
{
    brightEnabled_ = on;
}

void MarshallPlexiWDF::process (float* buffer, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        // Scale input to realistic grid voltage level
        float sample = buffer[i] * kInputScale;

        // === Stage 1: V1A ===
        float stage1 = v1a_.processSample (sample);

        // === Volume pot: attenuate between stages ===
        float attenuated = stage1 * volumeAtten_;

        // === Bright cap: treble bypass around volume pot ===
        if (brightEnabled_ && volumeAtten_ < 0.9f)
        {
            // HPF extracts treble from pre-volume signal, adds it post-volume
            float hpOut = stage1 - brightCapState_;
            brightCapState_ += hpOut * brightCapCoeff_;

            // Bright effect strongest at low volume, diminishes toward full
            float brightAmount = (1.0f - volumeAtten_) * 0.4f;
            attenuated += hpOut * brightAmount;
        }

        // === Stage 2: V1B ===
        float stage2 = v1b_.processSample (attenuated);

        // Stage 2's outputNorm_ normalizes to ±1 regardless of drive level.
        // On a real amp, lower volume = quieter AND cleaner. Scale output by
        // how hard stage 2 is driven to restore gain-dependent dynamics.
        float driveLevel = std::sqrt (volumeAtten_);
        float outputGain = 0.15f + 0.85f * driveLevel;

        buffer[i] = stage2 * kOutputScale * outputGain;
    }
}

void MarshallPlexiWDF::updateBrightCap()
{
    // Bright cap impedance depends on volume pot wiper position.
    // At volume=1 (R≈0), bright cap is shorted — no effect.
    // At volume=0.5 (R≈500k), fc ≈ 1/(2π·500k·120pF) ≈ 2.65kHz.
    float potR = (1.0f - volumeAtten_) * 1.0e6f + 100.0f;  // +100 avoids division edge case
    float fc = 1.0f / (2.0f * kPi * potR * 470.0e-12f);  // 470pF bright cap per schematic
    float w = 2.0f * kPi * fc / static_cast<float> (sampleRate_);
    brightCapCoeff_ = w / (w + 1.0f);  // Bilinear-ish one-pole coefficient
}
