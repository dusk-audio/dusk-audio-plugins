// ToneStack.cpp — Yeh/Smith circuit-derived tone stack (direct 3rd-order)
//
// The continuous-time transfer function for a Fender/Marshall tone stack is:
//   H(s) = (b1*s + b2*s^2 + b3*s^3) / (a0 + a1*s + a2*s^2 + a3*s^3)
//
// Note: b0 = 0 (no DC term in numerator) — the tone stack blocks DC.
// This is correct for a passive RC network with coupling capacitors.
//
// Discretized via bilinear transform: s = (2/T) * (1-z^-1)/(1+z^-1)
// Implemented as 3rd-order Transposed Direct Form II (3 state variables).

#include "ToneStack.h"
#include <cmath>
#include <algorithm>

// (no local constants needed — sampleRate used directly for bilinear transform)

// ============================================================================
// Component values from published schematics
// ============================================================================

ToneStack::Components ToneStack::getComponents (Type type)
{
    switch (type)
    {
        case Type::American:
            // Fender Twin Reverb / Bassman AB763
            return { 250e3, 1e6, 25e3, 56e3, 250e-12, 20e-9, 20e-9 };

        case Type::British:
            // Marshall JTM45 / 1959 Plexi
            return { 250e3, 1e6, 25e3, 33e3, 470e-12, 22e-9, 22e-9 };

        case Type::AC:
        default:
            // Vox AC30 approximation using the same topology
            return { 250e3, 1e6, 50e3, 100e3, 100e-12, 47e-9, 10e-9 };
    }
}

// ============================================================================
// Lifecycle
// ============================================================================

void ToneStack::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;
    coeffsDirty_ = true;
    reset();
}

void ToneStack::reset()
{
    w1_ = w2_ = w3_ = 0.0f;
}

void ToneStack::setType (Type type)
{
    if (type != currentType_)
    {
        currentType_ = type;
        coeffsDirty_ = true;
    }
}

void ToneStack::setBass (float value01)
{
    float v = std::clamp (value01, 0.01f, 0.99f);
    if (std::abs (v - bass_) > 1e-6f) { bass_ = v; coeffsDirty_ = true; }
}

void ToneStack::setMid (float value01)
{
    float v = std::clamp (value01, 0.01f, 0.99f);
    if (std::abs (v - mid_) > 1e-6f) { mid_ = v; coeffsDirty_ = true; }
}

void ToneStack::setTreble (float value01)
{
    float v = std::clamp (value01, 0.01f, 0.99f);
    if (std::abs (v - treble_) > 1e-6f) { treble_ = v; coeffsDirty_ = true; }
}

// ============================================================================
// Process — 3rd-order Transposed Direct Form II
// ============================================================================

void ToneStack::process (float* buffer, int numSamples)
{
    if (coeffsDirty_)
    {
        recomputeCoefficients();
        coeffsDirty_ = false;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        float x = buffer[i];

        // TDF-II: y = B0*x + w1
        float y = B0_ * x + w1_;

        // Update state variables
        w1_ = B1_ * x - A1_ * y + w2_;
        w2_ = B2_ * x - A2_ * y + w3_;
        w3_ = B3_ * x - A3_ * y;

        // Flush denormals
        if (std::abs (w1_) < 1e-15f) w1_ = 0.0f;
        if (std::abs (w2_) < 1e-15f) w2_ = 0.0f;
        if (std::abs (w3_) < 1e-15f) w3_ = 0.0f;

        buffer[i] = y;
    }
}

// ============================================================================
// Yeh/Smith coefficient computation + bilinear transform
// ============================================================================

void ToneStack::recomputeCoefficients()
{
    auto c = getComponents (currentType_);

    // Pot wiper positions (0-1 mapped to resistance)
    double R1 = std::max (c.R1 * treble_, 100.0);
    double R2 = std::max (c.R2 * bass_,   100.0);
    double R3 = std::max (c.R3 * mid_,    100.0);
    double R4 = c.R4;
    double C1 = c.C1, C2 = c.C2, C3 = c.C3;

    // --- Continuous-time coefficients ---
    // H(s) = (b1*s + b2*s^2 + b3*s^3) / (a0 + a1*s + a2*s^2 + a3*s^3)

    double b1 = C1*R1 + C3*R3;
    double b2 = C1*C2*R1*R4 + C1*C3*R1*R3 + C2*C3*R3*R4;
    double b3 = C1*C2*C3*R1*R3*R4;

    double a0 = 1.0;
    double a1 = C1*R1 + C1*R4 + C2*R4 + C3*R3 + C3*R4 + C2*R2;
    double a2 = C1*C2*R1*R4 + C1*C2*R2*R4 + C1*C3*R1*R3
              + C1*C3*R1*R4 + C1*C3*R3*R4 + C2*C3*R2*R4 + C2*C3*R3*R4;
    double a3 = C1*C2*C3*R1*R3*R4 + C1*C2*C3*R2*R3*R4;

    // --- Bilinear transform ---
    // s = c0 * (1-z^-1)/(1+z^-1),  c0 = 2*fs
    double c0 = 2.0 * sampleRate_;
    double c02 = c0 * c0;
    double c03 = c02 * c0;

    // Multiply N(s) and D(s) by (1+z^-1)^3 to clear denominators.
    // Use the polynomial expansions:
    //   (1+z^-1)^3                  = [1, 3, 3, 1]
    //   (1-z^-1)(1+z^-1)^2         = [1, 1, -1, -1]
    //   (1-z^-1)^2(1+z^-1)         = [1, -1, -1, 1]
    //   (1-z^-1)^3                  = [1, -3, 3, -1]

    // Numerator: b0=0, so only s^1, s^2, s^3 terms contribute
    double nB0 =        b1*c0*(1)  + b2*c02*(1)   + b3*c03*(1);
    double nB1 =        b1*c0*(1)  + b2*c02*(-1)  + b3*c03*(-3);
    double nB2 =        b1*c0*(-1) + b2*c02*(-1)  + b3*c03*(3);
    double nB3 =        b1*c0*(-1) + b2*c02*(1)   + b3*c03*(-1);

    // Denominator: a0*(1+z^-1)^3 + a1*c0*(1-z^-1)(1+z^-1)^2 + ...
    double dA0 = a0*(1)  + a1*c0*(1)  + a2*c02*(1)   + a3*c03*(1);
    double dA1 = a0*(3)  + a1*c0*(1)  + a2*c02*(-1)  + a3*c03*(-3);
    double dA2 = a0*(3)  + a1*c0*(-1) + a2*c02*(-1)  + a3*c03*(3);
    double dA3 = a0*(1)  + a1*c0*(-1) + a2*c02*(1)   + a3*c03*(-1);

    // Normalize by dA0
    double invA0 = 1.0 / (std::abs (dA0) > 1e-30 ? dA0 : 1e-30);

    B0_ = static_cast<float> (nB0 * invA0);
    B1_ = static_cast<float> (nB1 * invA0);
    B2_ = static_cast<float> (nB2 * invA0);
    B3_ = static_cast<float> (nB3 * invA0);
    A1_ = static_cast<float> (dA1 * invA0);
    A2_ = static_cast<float> (dA2 * invA0);
    A3_ = static_cast<float> (dA3 * invA0);
}
