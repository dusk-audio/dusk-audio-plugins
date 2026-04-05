#include "FenderDeluxeWDF.h"
#include <cmath>

FenderDeluxeWDF::FenderDeluxeWDF()
{
    // V1A: Fender AB763 — fully bypassed cathode (25uF = short at audio freqs)
    // Large coupling cap (0.1uF) passes deep bass
    v1a_.setCircuit (0.047e-6f,   // Cin: 0.047uF coupling cap (AB763 Normal channel)
                     1.0e6f,      // Rg: grid leak
                     100.0e3f,    // Rp: plate load
                     1500.0f,     // Rk: cathode resistor
                     25.0e-6f,    // Ck: fully bypassed (corner ~4.2Hz)
                     420.0f);     // B+ (~417-420V per schematic)
}

void FenderDeluxeWDF::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;
    v1a_.prepare (sampleRate);
    reset();
}

void FenderDeluxeWDF::reset()
{
    v1a_.reset();
    cfEnvelope_ = 0.0f;
}

void FenderDeluxeWDF::setGain (float gain01)
{
    volumeAtten_ = gain01 * gain01;  // Audio taper
}

void FenderDeluxeWDF::setBright (bool /* on */)
{
    // Deluxe Reverb 65 normal channel has no bright switch
}

void FenderDeluxeWDF::process (float* buffer, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        float sample = buffer[i] * kInputScale;

        // === V1A preamp ===
        float stage1 = v1a_.processSample (sample);

        // === Volume pot ===
        float attenuated = stage1 * volumeAtten_;

        // === Cathode follower: soft tanh compression (unity gain buffer) ===
        // Real CF has near-unity gain with soft limiting from grid current
        float cfOut = std::tanh (attenuated * 1.5f) * 0.67f;

        buffer[i] = cfOut * kOutputScale;
    }
}
