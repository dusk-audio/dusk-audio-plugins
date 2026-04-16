// ToneStack.h — Circuit-derived tone stack using Yeh/Smith transfer functions
//
// Implements the 3rd-order transfer function derived from real passive RC
// tone stack circuits. All three controls interact — turning up bass changes
// the treble response — because the transfer function is derived from the
// actual circuit equations, not independent parametric EQ bands.
//
// Discretized via bilinear transform, implemented as a direct 3rd-order
// Transposed Direct Form II filter (3 state variables, no biquad factoring).
//
// Reference: Yeh & Smith, "Discretization of the '59 Fender Bassman Tone Stack"
//            Proceedings of DAFx-06, Montreal, Canada.

#pragma once

#include <cmath>
#include <algorithm>

class ToneStack
{
public:
    enum class Type { American = 0, British = 1, AC = 2 };

    void prepare (double sampleRate);
    void reset();
    void setType (Type type);
    void setBass (float value01);
    void setMid (float value01);
    void setTreble (float value01);
    void process (float* buffer, int numSamples);

private:
    Type currentType_ = Type::British;
    double sampleRate_ = 44100.0;
    float bass_ = 0.5f, mid_ = 0.5f, treble_ = 0.5f;
    bool coeffsDirty_ = true;

    // Component values per topology
    struct Components
    {
        double R1;   // Treble pot max (Ω)
        double R2;   // Bass pot max (Ω)
        double R3;   // Mid pot max (Ω)
        double R4;   // Fixed resistor (Ω)
        double C1;   // Treble cap (F)
        double C2;   // Bass cap (F)
        double C3;   // Mid cap (F)
    };

    static Components getComponents (Type type);

    // 3rd-order IIR coefficients (after bilinear transform)
    // H(z) = (B0 + B1*z^-1 + B2*z^-2 + B3*z^-3) / (1 + A1*z^-1 + A2*z^-2 + A3*z^-3)
    float B0_ = 0, B1_ = 0, B2_ = 0, B3_ = 0;
    float A1_ = 0, A2_ = 0, A3_ = 0;

    // Transposed Direct Form II state variables
    float w1_ = 0, w2_ = 0, w3_ = 0;

    void recomputeCoefficients();
};
