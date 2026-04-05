// Circuit-modeled guitar amp tone stacks using the Yeh/Smith (2006) model.
// The transfer function H(s) is a 3rd-order rational polynomial derived from
// the actual circuit topology. All three pot positions (Bass/Mid/Treble)
// appear in every coefficient, so controls interact like real passive RC networks.
//
// Component values from published schematics:
//   American = Fender AB763 (Twin/Super/Deluxe Reverb)
//   British  = Marshall JTM45/1959
//   Modern   = Mesa Boogie Mark series
//
// The AC30 Top Boost uses a fundamentally different 2-knob circuit:
//   Treble + Bass only (no Mid), plus a separate Tone Cut control.
//   Implemented as a 2-band shelving EQ matching the real circuit response.

#pragma once

#include <cmath>
#include <algorithm>

class ToneStack
{
public:
    enum class Type { American = 0, British = 1, AC = 2, Modern = 3 };

    void prepare (double sampleRate);
    void reset();
    void setType (Type type);
    void setBass (float value01);
    void setMid (float value01);
    void setTreble (float value01);
    void setToneCut (float value01);
    void process (float* buffer, int numSamples);

private:
    Type currentType_ = Type::British;
    double sampleRate_ = 44100.0;
    float bass_ = 0.5f, mid_ = 0.5f, treble_ = 0.5f;
    float toneCut_ = 0.5f;  // AC30 Tone Cut: 0 = bright, 1 = dark

    // ---- Yeh/Smith TMB model (American, British, Modern) ----

    struct Components
    {
        float R1, R2, R3, R4;  // Resistors (ohms)
        float C1, C2, C3;      // Capacitors (farads)
    };
    Components comp_ {};

    // 3rd-order IIR filter (Transposed Direct Form II)
    float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f, b3_ = 0.0f;
    float a1_ = 0.0f, a2_ = 0.0f, a3_ = 0.0f;
    float z1_ = 0.0f, z2_ = 0.0f, z3_ = 0.0f;

    bool coeffsDirty_ = true;

    void selectComponents();
    void recomputeCoefficients();

    // ---- AC30 Top Boost 2-band shelving EQ ----

    // Low shelf biquad state (bass control)
    float lsB0_ = 1.0f, lsB1_ = 0.0f, lsB2_ = 0.0f;
    float lsA1_ = 0.0f, lsA2_ = 0.0f;
    float lsZ1_ = 0.0f, lsZ2_ = 0.0f;

    // High shelf biquad state (treble control)
    float hsB0_ = 1.0f, hsB1_ = 0.0f, hsB2_ = 0.0f;
    float hsA1_ = 0.0f, hsA2_ = 0.0f;
    float hsZ1_ = 0.0f, hsZ2_ = 0.0f;

    // Tone Cut: one-pole LPF (simulates the HF cut on the AC30 master section)
    float tcState_ = 0.0f;
    float tcCoeff_ = 1.0f;  // 1.0 = no cut (fully open)

    bool acCoeffsDirty_ = true;

    void recomputeACCoefficients();
    void computeShelf (float fc, float gainDB, float Q,
                       float& b0, float& b1, float& b2,
                       float& a1, float& a2, bool isHigh);
};
