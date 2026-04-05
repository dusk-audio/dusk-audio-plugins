#include "VoxAC30WDF.h"
#include <cmath>

VoxAC30WDF::VoxAC30WDF()
{
    // V1: 220k plate load + 0.047uF coupling per AC30 Silver Jubilee schematic
    v1_.setCircuit (0.047e-6f,    // Cin: 0.047uF coupling cap (Normal channel)
                    1.0e6f,       // Rg: grid leak
                    220.0e3f,     // Rp: 220k plate load (AC30 signature)
                    1500.0f,      // Rk: cathode resistor
                    25.0e-6f,     // Ck: fully bypassed
                    275.0f);      // B+ (~275V per schematic)

    // V2: Top Boost stage — drives the tone stack
    v2_.setCircuit (0.047e-6f,    // Cin: 0.047uF inter-stage coupling
                    1.0e6f,       // Rg: grid leak
                    220.0e3f,     // Rp: 220k plate load (both stages)
                    1500.0f,      // Rk: cathode resistor
                    25.0e-6f,     // Ck: fully bypassed
                    275.0f);      // B+
}

void VoxAC30WDF::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;
    v1_.prepare (sampleRate);
    v2_.prepare (sampleRate);
    reset();
}

void VoxAC30WDF::reset()
{
    v1_.reset();
    v2_.reset();
}

void VoxAC30WDF::setGain (float gain01)
{
    volumeAtten_ = gain01 * gain01;  // Audio taper
}

void VoxAC30WDF::setBright (bool /* on */)
{
    // AC30 Top Boost has Cut control (handled by tone stack), not a bright switch
}

void VoxAC30WDF::process (float* buffer, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        float sample = buffer[i] * kInputScale;

        // === Stage 1: V1 ===
        float stage1 = v1_.processSample (sample);

        // === Volume pot ===
        float attenuated = stage1 * volumeAtten_;

        // === Stage 2: V2 (220k plate load) ===
        float stage2 = v2_.processSample (attenuated);

        // Stage 2's outputNorm_ normalizes to ±1 regardless of drive level.
        // On a real amp, lower volume = quieter AND cleaner. Scale output by
        // how hard stage 2 is driven to restore gain-dependent dynamics.
        float driveLevel = std::sqrt (volumeAtten_);
        float outputGain = 0.15f + 0.85f * driveLevel;

        buffer[i] = stage2 * kOutputScale * outputGain;
    }
}
