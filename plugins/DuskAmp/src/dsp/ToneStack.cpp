// ToneStack.cpp — Per-amp parametric 3-band tone stack.
// Low shelf (bass), peaking EQ (mid), high shelf or peaking (treble).
// Each amp has its own voicing — see getVoicing() for the published-curve
// targets per Fender / Marshall / Vox.

#include "ToneStack.h"
#include <cmath>
#include <algorithm>

// ============================================================================
// Per-amp voicing — picked to match published frequency-response curves.
// Bass shelves the LF bump, mid peaks the band the user thinks of as "mid",
// treble shelves or peaks the HF.
// ============================================================================

ToneStack::Voicing ToneStack::getVoicing (Type type)
{
    switch (type)
    {
        case Type::American:
            // Fender black-panel voicing: bass shelf at 150 Hz (where most
            // of the perceived bass-knob effect lives), broad lower-mid
            // peak around 500 Hz, HF shelf at 4 kHz.
            return { 150.0f, 9.0f,   // bass shelf, ±dB
                     500.0f, 0.65f, 7.0f, // mid peak, Q, ±dB
                    4000.0f, 0.0f, 9.0f,  // treble: shelf, ±dB
                     true, false };
        case Type::British:
            // Marshall voicing: bass shelf a bit higher (the 1959/JCM has
            // a fuller LF corner), mid peak around 650 Hz with tighter Q
            // (the Marshall "honk"), HF shelf at 3.5 kHz to tame ice-pick.
            return { 180.0f, 9.0f,
                     650.0f, 0.85f, 6.0f,
                    3500.0f, 0.0f, 10.0f,
                     true, false };
        case Type::AC:
        default:
            // Vox AC30 Top Boost — no mid stage, peaking treble for the
            // signature "chime" resonance at 6.5 kHz, plus the cathode-
            // follower nonlinearity preserved from the prior implementation.
            return { 100.0f, 12.0f,
                       0.0f, 0.0f, 0.0f, // mid disabled
                    6500.0f, 1.4f, 15.0f, // peaking HF (chime)
                     false, true };
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
    bassBq_.clear();
    midBq_.clear();
    trebleBq_.clear();
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
    float v = std::clamp (value01, 0.0f, 1.0f);
    if (std::abs (v - bass_) > 1e-6f) { bass_ = v; coeffsDirty_ = true; }
}

void ToneStack::setMid (float value01)
{
    float v = std::clamp (value01, 0.0f, 1.0f);
    if (std::abs (v - mid_) > 1e-6f) { mid_ = v; coeffsDirty_ = true; }
}

void ToneStack::setTreble (float value01)
{
    float v = std::clamp (value01, 0.0f, 1.0f);
    if (std::abs (v - treble_) > 1e-6f) { treble_ = v; coeffsDirty_ = true; }
}

// ============================================================================
// Process — bass → mid → treble cascade
// ============================================================================

void ToneStack::process (float* buffer, int numSamples)
{
    if (coeffsDirty_)
    {
        recomputeCoefficients();
        coeffsDirty_ = false;
    }

    const auto v = getVoicing (currentType_);

    if (v.hasCathodeFollower)
    {
        // AC30 cathode-follower nonlinearity preserved from the prior AC
        // implementation. Asymmetric soft-clip (positive grid runs into
        // current-limit earlier than negative), 0.95× unity-region gain
        // applied to all branches so the transfer is C0-continuous at the
        // soft-clip thresholds.
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

            float y = bassBq_.processSample (cf);
            // AC has no mid stage — skip midBq_.
            y = trebleBq_.processSample (y);
            buffer[i] = y;
        }
        return;
    }

    if (v.hasMid)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float y = bassBq_.processSample (buffer[i]);
            y = midBq_.processSample (y);
            y = trebleBq_.processSample (y);
            buffer[i] = y;
        }
    }
    else
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float y = bassBq_.processSample (buffer[i]);
            y = trebleBq_.processSample (y);
            buffer[i] = y;
        }
    }
}

// ============================================================================
// Coefficient computation
// ============================================================================

void ToneStack::recomputeCoefficients()
{
    const auto v = getVoicing (currentType_);

    // Knob 0..1 → ±max dB, with knob=0.5 = flat (0 dB).
    const float bassDb   = (bass_   - 0.5f) * 2.0f * v.bassMaxDb;
    const float trebleDb = (treble_ - 0.5f) * 2.0f * v.trebleMaxDb;

    designLowShelf (bassBq_, v.bassHz, bassDb, sampleRate_);

    if (v.trebleQ > 0.0f)
        designPeakingEQ (trebleBq_, v.trebleHz, trebleDb, v.trebleQ, sampleRate_);
    else
        designHighShelf (trebleBq_, v.trebleHz, trebleDb, sampleRate_);

    if (v.hasMid)
    {
        const float midDb = (mid_ - 0.5f) * 2.0f * v.midMaxDb;
        designPeakingEQ (midBq_, v.midHz, midDb, v.midQ, sampleRate_);
    }
}

// ============================================================================
// RBJ biquad designs
// ============================================================================

void ToneStack::designLowShelf (Biquad& bq, float fc, float gainDb, double sr)
{
    constexpr double kPi = 3.14159265358979323846;
    const double A      = std::pow (10.0, static_cast<double> (gainDb) / 40.0);
    const double omega  = 2.0 * kPi * static_cast<double> (fc) / sr;
    const double cosw   = std::cos (omega);
    const double sinw   = std::sin (omega);
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
