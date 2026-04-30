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
    recomputeFlatMakeup();
    reset();
}

void ToneStack::reset()
{
    w1_ = w2_ = w3_ = 0.0f;
    tbBass_.clear();
    tbTreble_.clear();
}

void ToneStack::setType (Type type)
{
    if (type != currentType_)
    {
        currentType_ = type;
        recomputeFlatMakeup();
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

    if (currentType_ == Type::AC)
    {
        // Cathode follower (ECC83 triode wired as a buffer driving the Top
        // Boost network in the real AC30). Voltage gain ≈ 0.95, asymmetric
        // soft-clip — positive grid swing runs into grid-current limit
        // earlier than negative swing, contributing AC30's subtle even-
        // harmonic grit at clean drive levels.
        //
        // The 0.95 unity-region gain is applied to ALL branches (linear and
        // saturated alike) so the transfer function is C0-continuous at the
        // soft-clip thresholds. Without that, crossings at ±0.6/-0.85 would
        // produce step jumps of ~0.03-0.04 → audible clicks.
        for (int i = 0; i < numSamples; ++i)
        {
            const float x = buffer[i];
            float cf;
            if (x > 0.6f)
                cf = (0.6f + std::tanh ((x - 0.6f) * 1.5f) * 0.25f) * 0.95f;
            else if (x < -0.85f)
                cf = (-0.85f + std::tanh ((x + 0.85f) * 1.0f) * 0.20f) * 0.95f;
            else
                cf = x * 0.95f;

            float y = tbBass_.processSample (cf);
            y = tbTreble_.processSample (y);
            buffer[i] = y * flatMakeup_;
        }
        return;
    }

    // American / British: Yeh/Smith 3rd-order TDF-II.
    for (int i = 0; i < numSamples; ++i)
    {
        float x = buffer[i];
        float y = B0_ * x + w1_;

        w1_ = B1_ * x - A1_ * y + w2_;
        w2_ = B2_ * x - A2_ * y + w3_;
        w3_ = B3_ * x - A3_ * y;

        if (std::abs (w1_) < 1e-15f) w1_ = 0.0f;
        if (std::abs (w2_) < 1e-15f) w2_ = 0.0f;
        if (std::abs (w3_) < 1e-15f) w3_ = 0.0f;

        buffer[i] = y * flatMakeup_;
    }
}

// ============================================================================
// Yeh/Smith coefficient computation + bilinear transform
// ============================================================================

// ============================================================================
// Top Boost (AC) — RBJ shelving biquads
// ============================================================================

void ToneStack::designLowShelf (Biquad& bq, float fc, float gainDb, double sr)
{
    constexpr double kPi = 3.14159265358979323846;
    const double A      = std::pow (10.0, static_cast<double> (gainDb) / 40.0);
    const double omega  = 2.0 * kPi * static_cast<double> (fc) / sr;
    const double cosw   = std::cos (omega);
    const double sinw   = std::sin (omega);
    // RBJ shelf slope S=1: alpha = sin(w)/2 * sqrt(A + 1/A + 2)
    const double alpha  = sinw * 0.5 * std::sqrt (A + 1.0 / A + 2.0);
    const double sqrtA  = std::sqrt (A);

    const double b0 = A * ((A + 1.0) - (A - 1.0) * cosw + 2.0 * sqrtA * alpha);
    const double b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosw);
    const double b2 = A * ((A + 1.0) - (A - 1.0) * cosw - 2.0 * sqrtA * alpha);
    const double a0 = (A + 1.0) + (A - 1.0) * cosw + 2.0 * sqrtA * alpha;
    const double a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosw);
    const double a2 = (A + 1.0) + (A - 1.0) * cosw - 2.0 * sqrtA * alpha;

    const double inv = 1.0 / a0;
    bq.b0 = static_cast<float> (b0 * inv);
    bq.b1 = static_cast<float> (b1 * inv);
    bq.b2 = static_cast<float> (b2 * inv);
    bq.a1 = static_cast<float> (a1 * inv);
    bq.a2 = static_cast<float> (a2 * inv);
}

void ToneStack::designHighShelf (Biquad& bq, float fc, float gainDb, double sr)
{
    constexpr double kPi = 3.14159265358979323846;
    const double A      = std::pow (10.0, static_cast<double> (gainDb) / 40.0);
    const double omega  = 2.0 * kPi * static_cast<double> (fc) / sr;
    const double cosw   = std::cos (omega);
    const double sinw   = std::sin (omega);
    const double alpha  = sinw * 0.5 * std::sqrt (A + 1.0 / A + 2.0);
    const double sqrtA  = std::sqrt (A);

    const double b0 = A * ((A + 1.0) + (A - 1.0) * cosw + 2.0 * sqrtA * alpha);
    const double b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw);
    const double b2 = A * ((A + 1.0) + (A - 1.0) * cosw - 2.0 * sqrtA * alpha);
    const double a0 = (A + 1.0) - (A - 1.0) * cosw + 2.0 * sqrtA * alpha;
    const double a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosw);
    const double a2 = (A + 1.0) - (A - 1.0) * cosw - 2.0 * sqrtA * alpha;

    const double inv = 1.0 / a0;
    bq.b0 = static_cast<float> (b0 * inv);
    bq.b1 = static_cast<float> (b1 * inv);
    bq.b2 = static_cast<float> (b2 * inv);
    bq.a1 = static_cast<float> (a1 * inv);
    bq.a2 = static_cast<float> (a2 * inv);
}

void ToneStack::designPeakingEQ (Biquad& bq, float fc, float gainDb, float q, double sr)
{
    // RBJ peaking-EQ biquad: boost or cut at fc with bandwidth set by Q.
    // Used here for the AC Top Boost treble band — the real circuit has a
    // characteristic resonance peak around 6-8 kHz from cap-pot interaction
    // that a flat shelf can't reproduce; a peaking filter at fc=6.5k Q=1.4
    // gives the right "chime" character.
    constexpr double kPi = 3.14159265358979323846;
    const double A     = std::pow (10.0, static_cast<double> (gainDb) / 40.0);
    const double omega = 2.0 * kPi * static_cast<double> (fc) / sr;
    const double cosw  = std::cos (omega);
    const double sinw  = std::sin (omega);
    const double alpha = sinw / (2.0 * static_cast<double> (q));

    const double b0 = 1.0 + alpha * A;
    const double b1 = -2.0 * cosw;
    const double b2 = 1.0 - alpha * A;
    const double a0 = 1.0 + alpha / A;
    const double a1 = -2.0 * cosw;
    const double a2 = 1.0 - alpha / A;

    const double inv = 1.0 / a0;
    bq.b0 = static_cast<float> (b0 * inv);
    bq.b1 = static_cast<float> (b1 * inv);
    bq.b2 = static_cast<float> (b2 * inv);
    bq.a1 = static_cast<float> (a1 * inv);
    bq.a2 = static_cast<float> (a2 * inv);
}

void ToneStack::recomputeTopBoost()
{
    // Knob 0..1 → ±max dB, with knob=0.5 = flat (0 dB).
    // Bass: low-shelf at 100 Hz — straightforward shelving.
    // Treble: PEAKING filter at 6.5 kHz with Q=1.4. The real AC30 Top Boost
    // has an HF resonance peak (cap × pot × load impedance), not a flat
    // shelf — that's the source of the AC30 "chime". A peaking biquad
    // approximates this with one filter; the treble knob controls the peak
    // gain (boost above flat = chime, cut below = darker).
    const float bassDb   = (bass_   - 0.5f) * 2.0f * kTopBoostBassMaxDb;
    const float trebleDb = (treble_ - 0.5f) * 2.0f * kTopBoostTrebleMaxDb;

    designLowShelf  (tbBass_,   kTopBoostBassHz,   bassDb,   sampleRate_);
    designPeakingEQ (tbTreble_, kTopBoostTrebleHz, trebleDb, kTopBoostTrebleQ, sampleRate_);
}

void ToneStack::recomputeFlatMakeup()
{
    // Top Boost shelves are flat-by-design at knob=0.5 — no makeup needed.
    if (currentType_ == Type::AC)
    {
        flatMakeup_ = 1.0f;
        return;
    }

    // Temporarily pin knobs to 0.5 so we can measure the circuit's insertion
    // loss at flat settings, cache 1/|H(1kHz)| as the fixed makeup, then
    // restore user settings. This gain does not move with the knobs — so the
    // mid control actually scoops mids, and bass/treble produce the real
    // Yeh/Smith EQ curve (with its natural knob-dependent level variation)
    // rather than being flattened by a knob-tracking compensator.
    const float savedBass   = bass_;
    const float savedMid    = mid_;
    const float savedTreble = treble_;
    bass_ = mid_ = treble_ = 0.5f;

    recomputeCoefficients();

    const double omega = 2.0 * 3.14159265358979323846 * kCompensationFreqHz / sampleRate_;
    const double cosW  = std::cos (omega);
    const double sinW  = std::sin (omega);
    const double cos2  = 2.0 * cosW * cosW - 1.0;
    const double sin2  = 2.0 * sinW * cosW;
    const double cos3  = 4.0 * cosW * cosW * cosW - 3.0 * cosW;
    const double sin3  = 3.0 * sinW - 4.0 * sinW * sinW * sinW;

    const double nR = B0_ + B1_ * cosW + B2_ * cos2 + B3_ * cos3;
    const double nI = -(B1_ * sinW + B2_ * sin2 + B3_ * sin3);
    const double dR = 1.0 + A1_ * cosW + A2_ * cos2 + A3_ * cos3;
    const double dI = -(A1_ * sinW + A2_ * sin2 + A3_ * sin3);
    const double mag = std::sqrt ((nR * nR + nI * nI) / std::max (dR * dR + dI * dI, 1e-30));

    const float gain = (mag > 1.0e-6) ? static_cast<float> (1.0 / mag) : 1.0f;
    flatMakeup_ = std::clamp (gain, 0.1f, kFlatMakeupMaxGain);

    bass_   = savedBass;
    mid_    = savedMid;
    treble_ = savedTreble;
    coeffsDirty_ = true;
}

// ============================================================================
// Yeh/Smith 3-knob network (American / British)
// ============================================================================

void ToneStack::recomputeCoefficients()
{
    if (currentType_ == Type::AC)
    {
        recomputeTopBoost();
        return;
    }

    auto c = getComponents (currentType_);

    // Pot wiper positions (0-1 mapped to resistance).
    // Yeh/Smith convention: the bass pot's "R2" in the transfer function is
    // the resistance from the wiper to the top (signal side), so the knob
    // value is inverted relative to treble/mid. Knob 1.0 → wiper at top →
    // R2 = 0 (minimum series resistance) → more LF reaches output; knob
    // 0.0 → wiper at ground → R2 = full pot → low frequencies shunted.
    // Without this flip, the bass knob was backwards: bass=0 produced the
    // hottest, treble-heavy response and bass=1 produced the quiet one.
    double R1 = std::max (c.R1 * treble_,           100.0);
    double R2 = std::max (c.R2 * (1.0 - bass_),     100.0);
    double R3 = std::max (c.R3 * mid_,              100.0);
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
