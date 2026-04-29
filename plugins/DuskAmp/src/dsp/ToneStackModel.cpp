// SPDX-License-Identifier: GPL-3.0-or-later

// ToneStackModel.cpp — Circuit-modeled tone stack coefficient computation
//
// Fender & Marshall: The classic 3-knob tone stack is a passive RC network
// analyzed by Yeh/Smith. The transfer function H(s) = B(s)/A(s) is a ratio
// of 3rd-degree polynomials in s, where the coefficients are functions of
// the pot positions t (treble), m (mid), b (bass) and fixed component values.
//
// The formulas below are derived from nodal analysis of the Fender/Marshall
// tone stack circuit. See Yeh 2009, Chapter 4, and the DAFx-06 paper
// "Automated Physical Modeling of Nonlinear Audio Circuits for Real-Time
// Audio Effects" for the complete derivation.
//
// Vox AC30 Top Boost: Different topology — a "cut" circuit with Bass and
// Treble controls plus a fixed midrange emphasis. Modeled as a 2nd-order
// filter with interactive controls.

#include "ToneStackModel.h"
#include <cmath>
#include <algorithm>

// ============================================================================
// Component values for each topology
// ============================================================================

ToneStackModel::FenderMarshallComponents ToneStackModel::getFenderComponents()
{
    // Fender Twin Reverb AB763
    return {
        250e3,  // R1 — Treble pot (250k)
        1e6,    // R2 — Bass pot (1M)
        25e3,   // R3 — Mid pot (25k)
        56e3,   // R4 — Slope resistor
        250e-12, // C1 — Treble cap (250pF)
        20e-9,   // C2 — Bass cap (20nF)
        20e-9    // C3 — Mid cap (20nF)
    };
}

ToneStackModel::FenderMarshallComponents ToneStackModel::getMarshallComponents()
{
    // Marshall JTM45 / Plexi 1959
    return {
        220e3,  // R1 — Treble pot (220k)
        1e6,    // R2 — Bass pot (1M)
        22e3,   // R3 — Mid pot (22k) — note: some schematics show 25k
        33e3,   // R4 — Slope resistor (33k — different from Fender's 56k)
        470e-12, // C1 — Treble cap (470pF — larger = more mid scoop)
        22e-9,   // C2 — Bass cap (22nF)
        22e-9    // C3 — Mid cap (22nF)
    };
}

ToneStackModel::VoxComponents ToneStackModel::getVoxComponents()
{
    // Vox AC30 Top Boost
    // Simplified model: volume pot interaction is omitted since this
    // tone-stack model runs independently of the volume control.
    return {
        1e6,    // R1 — Bass pot (1M)
        50e3,   // R2 — Treble pot (50k — "Cut" control)
        220e3,  // R3 — Mixing/load resistor
        100e-12, // C1 — Treble cap (100pF)
        47e-9,   // C2 — Bass cap (47nF)
        4.7e-9   // C3 — Mid coupling cap
    };
}

// ============================================================================
// Lifecycle
// ============================================================================

void ToneStackModel::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;
    coeffsDirty_ = true;
    reset();
}

void ToneStackModel::reset()
{
    z1_ = z2_ = z3_ = 0.0;
}

void ToneStackModel::setTopology (Topology t)
{
    if (t == topology_) return;
    topology_ = t;
    coeffsDirty_ = true;
}

void ToneStackModel::setBass (float value01)
{
    float clamped = std::clamp (value01, 0.01f, 0.99f);
    bass_ = clamped;
    coeffsDirty_ = true;
}

void ToneStackModel::setMid (float value01)
{
    float clamped = std::clamp (value01, 0.01f, 0.99f);
    mid_ = clamped;
    coeffsDirty_ = true;
}

void ToneStackModel::setTreble (float value01)
{
    float clamped = std::clamp (value01, 0.01f, 0.99f);
    treble_ = clamped;
    coeffsDirty_ = true;
}

// ============================================================================
// Process
// ============================================================================

void ToneStackModel::process (float* buffer, int numSamples)
{
    if (coeffsDirty_)
    {
        recomputeCoefficients();
        coeffsDirty_ = false;
    }

    // Transposed Direct Form II (3rd order)
    for (int i = 0; i < numSamples; ++i)
    {
        double x = static_cast<double> (buffer[i]);

        double y = b0_ * x + z1_;
        z1_ = b1_ * x - a1_ * y + z2_;
        z2_ = b2_ * x - a2_ * y + z3_;
        z3_ = b3_ * x - a3_ * y;

        // Flush denormals
        if (std::abs (z1_) < 1e-30) z1_ = 0.0;
        if (std::abs (z2_) < 1e-30) z2_ = 0.0;
        if (std::abs (z3_) < 1e-30) z3_ = 0.0;

        buffer[i] = static_cast<float> (y);
    }
}

// ============================================================================
// Coefficient computation
// ============================================================================

void ToneStackModel::recomputeCoefficients()
{
    switch (topology_)
    {
        case Topology::Fender:
            computeFenderMarshall (getFenderComponents());
            break;
        case Topology::Marshall:
            computeFenderMarshall (getMarshallComponents());
            break;
        case Topology::Vox:
            computeVox (getVoxComponents());
            break;
    }
}

// ----------------------------------------------------------------------------
// Fender/Marshall tone stack (Yeh/Smith derivation)
//
// The transfer function H(s) = B(s)/A(s) for the classic 3-knob tone stack:
//
//   B(s) = b1*s^3 + b2*s^2 + b3*s + b4
//   A(s) = a0*s^3 + a1*s^2 + a2*s + a3
//
// where the b/a coefficients are polynomial expressions of:
//   t = treble pot position [0,1]
//   m = mid pot position [0,1]
//   b = bass pot position [0,1]
//   R1..R4, C1..C3 = fixed component values
//
// The derivation follows from nodal analysis of the 4-node passive RC network.
// See Yeh 2009 Section 4.2 for the full symbolic expressions.
// ----------------------------------------------------------------------------

void ToneStackModel::computeFenderMarshall (const FenderMarshallComponents& comp)
{
    double t = static_cast<double> (treble_);
    double m = static_cast<double> (mid_);
    double b = static_cast<double> (bass_);

    double R1 = comp.R1, R2 = comp.R2, R3 = comp.R3, R4 = comp.R4;
    double C1 = comp.C1, C2 = comp.C2, C3 = comp.C3;

    // Denominator coefficients (A(s) = a0*s^3 + a1*s^2 + a2*s + 1)
    // Note: a3 is normalized to 1.0
    double a0s = C1*C2*C3*R1*R2*R3
               + C1*C2*C3*R1*R2*R4
               + C1*C2*C3*R1*R3*R4;

    double a1s = C1*C2*R1*R2
               + C1*C2*R1*R4
               + C1*C3*R1*R3
               + C1*C3*R1*R4
               + C2*C3*R2*R3
               + C2*C3*R2*R4
               + C2*C3*R3*R4
               + C1*C2*R3*R4*b
               + C1*C3*R2*R4*b;

    double a2s = C1*R1
               + C1*R4
               + C2*R2
               + C2*R4
               + C3*R3
               + C3*R4
               + C1*R3*b
               + C2*R4*b;

    // Numerator coefficients (B(s) = b0*s^3 + b1*s^2 + b2*s + b3)
    double b0s = C1*C2*C3*R1*R2*R4*t;

    double b1s = C1*C2*R1*R4*t
               + C1*C2*R2*R4*t
               + C1*C3*R1*R4*t*b
               + C2*C3*R2*R4*(1.0 - t);

    double b2s = C1*R1*t
               + C1*R4*t*b
               + C2*R4*(1.0 - t)
               + C3*R4*m;

    double b3s = m;

    // Bilinear transform: s = (2/T) * (z-1)/(z+1)
    // Pre-warp is not applied here because the tone stack operates across
    // the full audio band — there's no single frequency to warp to.
    // The bilinear transform maps the entire s-plane to z-plane.
    double T = 1.0 / sampleRate_;
    double c = 2.0 / T;  // bilinear constant
    double c2 = c * c;
    double c3 = c2 * c;

    // Transform: substitute s = c*(z-1)/(z+1) and collect powers of z
    // For a 3rd-order system:
    //   H(z) = (B0 + B1*z^-1 + B2*z^-2 + B3*z^-3) /
    //          (A0 + A1*z^-1 + A2*z^-2 + A3*z^-3)
    //
    // where:
    //   A0 = a0*c^3 + a1*c^2 + a2*c + a3   (a3 = 1.0)
    //   A1 = -3*a0*c^3 - a1*c^2 + a2*c + 3*a3
    //   A2 = 3*a0*c^3 - a1*c^2 - a2*c + 3*a3
    //   A3 = -a0*c^3 + a1*c^2 - a2*c + a3
    //   (same pattern for B)

    double A0 = a0s*c3 + a1s*c2 + a2s*c + 1.0;
    double A1 = -3.0*a0s*c3 - a1s*c2 + a2s*c + 3.0;
    double A2 = 3.0*a0s*c3 - a1s*c2 - a2s*c + 3.0;
    double A3 = -a0s*c3 + a1s*c2 - a2s*c + 1.0;

    double B0 = b0s*c3 + b1s*c2 + b2s*c + b3s;
    double B1 = -3.0*b0s*c3 - b1s*c2 + b2s*c + 3.0*b3s;
    double B2 = 3.0*b0s*c3 - b1s*c2 - b2s*c + 3.0*b3s;
    double B3 = -b0s*c3 + b1s*c2 - b2s*c + b3s;

    // Normalize by A0
    if (std::abs (A0) < 1e-30) A0 = 1e-30; // safety
    double invA0 = 1.0 / A0;

    b0_ = B0 * invA0;
    b1_ = B1 * invA0;
    b2_ = B2 * invA0;
    b3_ = B3 * invA0;
    a1_ = A1 * invA0;
    a2_ = A2 * invA0;
    a3_ = A3 * invA0;
}

// ----------------------------------------------------------------------------
// Vox AC30 Top Boost "Cut" circuit
//
// The Vox tone circuit is different from Fender/Marshall — it uses a simpler
// Bass + Treble ("Cut") 2-control topology. The Mid control adds a fixed
// resonant emphasis. This is modeled as a 2nd-order filter (b3_=0, a3_=0).
//
// The treble "Cut" control progressively removes high frequencies (it's
// subtractive, not additive). Bass control is a shelving network.
// We add a mid peaking section via the C3 coupling path.
// ----------------------------------------------------------------------------

void ToneStackModel::computeVox (const VoxComponents& comp)
{
    double t = static_cast<double> (treble_); // Cut control (1.0 = bright, 0.0 = dark)
    double m = static_cast<double> (mid_);
    double b = static_cast<double> (bass_);

    double R1 = comp.R1, R2 = comp.R2, R3 = comp.R3;
    double C1 = comp.C1, C2 = comp.C2, C3 = comp.C3;

    // Effective resistances with pot positions
    double Rt = R2 * t;          // Treble pot wiper
    double Rb = R1 * b;          // Bass pot wiper
    double Rm = R3 * (1.0 - m);  // Mid effect on load

    // 2nd-order analog prototype coefficients
    // H(s) = (b0 + b1*s + b2*s^2) / (a0 + a1*s + a2*s^2)
    double a0a = 1.0;
    double a1a = C2*Rb + C1*Rt + C3*Rm + C2*R3 + C1*R3;
    double a2a = C1*C2*Rb*Rt + C1*C3*Rt*Rm + C2*C3*Rb*Rm
               + C1*C2*R3*Rt + C2*C3*R3*Rb;

    double b0a = m;
    double b1a = C1*Rt*m + C2*Rb*(1.0 - t*0.5) + C3*Rm*m;
    double b2a = C1*C2*Rb*Rt*t + C2*C3*Rb*Rm*(1.0 - t);

    // Bilinear transform (2nd order)
    double T = 1.0 / sampleRate_;
    double c = 2.0 / T;
    double c2 = c * c;

    double A0 = a2a*c2 + a1a*c + a0a;
    double A1 = -2.0*a2a*c2 + 2.0*a0a;
    double A2 = a2a*c2 - a1a*c + a0a;

    double B0 = b2a*c2 + b1a*c + b0a;
    double B1 = -2.0*b2a*c2 + 2.0*b0a;
    double B2 = b2a*c2 - b1a*c + b0a;

    if (std::abs (A0) < 1e-30) A0 = 1e-30;
    double invA0 = 1.0 / A0;

    b0_ = B0 * invA0;
    b1_ = B1 * invA0;
    b2_ = B2 * invA0;
    b3_ = 0.0;  // 2nd order — no z^-3 term
    a1_ = A1 * invA0;
    a2_ = A2 * invA0;
    a3_ = 0.0;
}
