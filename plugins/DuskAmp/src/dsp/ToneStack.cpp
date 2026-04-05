#include "ToneStack.h"

void ToneStack::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;
    selectComponents();
    recomputeCoefficients();
    recomputeACCoefficients();
    reset();
}

void ToneStack::reset()
{
    z1_ = z2_ = z3_ = 0.0f;
    lsZ1_ = lsZ2_ = 0.0f;
    hsZ1_ = hsZ2_ = 0.0f;
    tcState_ = 0.0f;
}

void ToneStack::setType (Type type)
{
    if (type == currentType_) return;
    currentType_ = type;
    selectComponents();
    coeffsDirty_ = true;
    acCoeffsDirty_ = true;
}

void ToneStack::setBass (float value01)
{
    float v = std::clamp (value01, 0.01f, 0.99f);
    bass_ = v;
    coeffsDirty_ = true;
    acCoeffsDirty_ = true;
}

void ToneStack::setMid (float value01)
{
    float v = std::clamp (value01, 0.01f, 0.99f);
    mid_ = v;
    coeffsDirty_ = true;
    // Mid is ignored for AC30 — no acCoeffsDirty_ needed
}

void ToneStack::setTreble (float value01)
{
    float v = std::clamp (value01, 0.01f, 0.99f);
    treble_ = v;
    coeffsDirty_ = true;
    acCoeffsDirty_ = true;
}

void ToneStack::setToneCut (float value01)
{
    toneCut_ = std::clamp (value01, 0.0f, 1.0f);
    acCoeffsDirty_ = true;
}

void ToneStack::process (float* buffer, int numSamples)
{
    if (currentType_ == Type::AC)
    {
        // ================================================================
        // AC30 Top Boost: 2-band shelving EQ + Tone Cut
        // ================================================================
        if (acCoeffsDirty_)
        {
            recomputeACCoefficients();
            acCoeffsDirty_ = false;

            // Coefficient changes can cause biquad state transients → Inf → NaN.
            // Flush invalid state to prevent permanent silence from NaN propagation.
            if (!std::isfinite (lsZ1_) || !std::isfinite (lsZ2_)) lsZ1_ = lsZ2_ = 0.0f;
            if (!std::isfinite (hsZ1_) || !std::isfinite (hsZ2_)) hsZ1_ = hsZ2_ = 0.0f;
            if (!std::isfinite (tcState_)) tcState_ = 0.0f;
        }

        // Sanitize: snap state to zero if NaN/Inf/runaway (from coefficient transients)
        auto sanitize = [] (float& v) { if (!(v > -1e6f && v < 1e6f)) v = 0.0f; };

        for (int i = 0; i < numSamples; ++i)
        {
            float x = buffer[i];

            // Low shelf (bass control)
            float yLS = lsB0_ * x + lsZ1_;
            lsZ1_ = lsB1_ * x - lsA1_ * yLS + lsZ2_;
            lsZ2_ = lsB2_ * x - lsA2_ * yLS;
            sanitize (lsZ1_);
            sanitize (lsZ2_);

            // High shelf (treble control)
            float yHS = hsB0_ * yLS + hsZ1_;
            hsZ1_ = hsB1_ * yLS - hsA1_ * yHS + hsZ2_;
            hsZ2_ = hsB2_ * yLS - hsA2_ * yHS;
            sanitize (hsZ1_);
            sanitize (hsZ2_);

            // Tone Cut: one-pole LPF (0 = wide open, 1 = heavy cut)
            tcState_ += tcCoeff_ * (yHS - tcState_);
            sanitize (tcState_);

            buffer[i] = tcState_;
        }
    }
    else
    {
        // ================================================================
        // Yeh/Smith TMB model (American, British, Modern)
        // ================================================================
        if (coeffsDirty_)
        {
            recomputeCoefficients();
            coeffsDirty_ = false;
        }

        // 3rd-order IIR, Transposed Direct Form II
        for (int i = 0; i < numSamples; ++i)
        {
            float x = buffer[i];
            float y = b0_ * x + z1_;
            z1_ = b1_ * x - a1_ * y + z2_;
            z2_ = b2_ * x - a2_ * y + z3_;
            z3_ = b3_ * x - a3_ * y;
            buffer[i] = y;
        }
    }
}

// ============================================================================
// Component values from published schematics
// ============================================================================

void ToneStack::selectComponents()
{
    switch (currentType_)
    {
        case Type::American:
            // Fender AB763 Deluxe Reverb — NO mid pot, 6.8k fixed resistor
            comp_ = { 250e3f, 1e6f, 6.8e3f, 56e3f,
                      250e-12f, 20e-9f, 20e-9f };
            break;

        case Type::British:
            // Marshall JTM45 / 1959 Super Lead (per AmpBooks analysis)
            comp_ = { 33e3f, 1e6f, 25e3f, 56e3f,
                      500e-12f, 22e-9f, 22e-9f };
            break;

        case Type::AC:
            // AC30: handled by shelving EQ path, components not used
            break;

        case Type::Modern:
            // Mesa Boogie Mark series
            comp_ = { 250e3f, 250e3f, 25e3f, 100e3f,
                      250e-12f, 100e-9f, 47e-9f };
            break;
    }

    coeffsDirty_ = true;
}

// ============================================================================
// Yeh/Smith tone stack model — 3rd-order analog prototype, bilinear transform
// ============================================================================

void ToneStack::recomputeCoefficients()
{
    // Skip for AC30 (uses shelving EQ path)
    if (currentType_ == Type::AC) return;

    const float R1 = comp_.R1, R2 = comp_.R2, R3 = comp_.R3, R4 = comp_.R4;
    const float C1 = comp_.C1, C2 = comp_.C2, C3 = comp_.C3;
    // American (Deluxe Reverb): no mid pot — R3 is fixed 6.8k, m=1.0
    const float t = treble_;
    const float m = (currentType_ == Type::American) ? 1.0f : mid_;
    const float b = bass_;

    // Analog prototype coefficients (Yeh/Smith derivation)
    float b0s = 1.0f;

    float b1s = t * C1 * R1
              + m * C3 * R3
              + b * (C1 * R2 + C2 * R2)
              + (C1 * R3 + C2 * R3);

    float b2s = t * (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R3)
              - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
              + m * (C2 * C3 * R3 * R4 + C1 * C3 * R3 * R3)
              + b * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4 + C1 * C3 * R2 * R3)
              + (C1 * C2 * R1 * R3 + C1 * C2 * R3 * R4 + C1 * C3 * R3 * R3);

    float b3s = b * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4)
              + b * (C1 * C2 * C3 * R1 * R2 * R3)
              + m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
              - m * m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
              + t * (C1 * C2 * C3 * R1 * R3 * R4)
              + t * m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R1 * R3 * R4);

    // Denominator
    float a0s = 1.0f;

    float a1s = (C1 * R1 + C1 * R3 + C2 * R3 + C2 * R4 + C3 * R4)
              + m * C3 * R3
              + b * (C1 * R2 + C2 * R2);

    float a2s = (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4
              + C1 * C2 * R3 * R4 + C1 * C2 * R1 * R3
              + C1 * C3 * R3 * R4 + C2 * C3 * R3 * R4)
              + m * (C1 * C3 * R1 * R3 + C1 * C3 * R3 * R3
                   + C2 * C3 * R3 * R4)
              - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
              + b * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4
                   + C1 * C3 * R2 * R4 + C2 * C3 * R2 * R4);

    float a3s = (C1 * C2 * C3 * R1 * R3 * R4)
              + m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
              - m * m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
              + b * (C1 * C2 * C3 * R1 * R2 * R4 + C1 * C2 * C3 * R2 * R3 * R4)
              + b * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4);

    // Bilinear transform
    float c = 2.0f * static_cast<float> (sampleRate_);
    float c2 = c * c;
    float c3 = c2 * c;

    float B0 = b0s       + b1s * c     + b2s * c2     + b3s * c3;
    float B1 = 3.0f * b0s + b1s * c   - b2s * c2     - 3.0f * b3s * c3;
    float B2 = 3.0f * b0s - b1s * c   - b2s * c2     + 3.0f * b3s * c3;
    float B3 = b0s       - b1s * c     + b2s * c2     - b3s * c3;

    float A0 = a0s       + a1s * c     + a2s * c2     + a3s * c3;
    float A1 = 3.0f * a0s + a1s * c   - a2s * c2     - 3.0f * a3s * c3;
    float A2 = 3.0f * a0s - a1s * c   - a2s * c2     + 3.0f * a3s * c3;
    float A3 = a0s       - a1s * c     + a2s * c2     - a3s * c3;

    if (std::abs (A0) < 1e-20f)
    {
        b0_ = 1.0f; b1_ = b2_ = b3_ = 0.0f;
        a1_ = a2_ = a3_ = 0.0f;
        return;
    }

    float invA0 = 1.0f / A0;
    b0_ = B0 * invA0;
    b1_ = B1 * invA0;
    b2_ = B2 * invA0;
    b3_ = B3 * invA0;
    a1_ = A1 * invA0;
    a2_ = A2 * invA0;
    a3_ = A3 * invA0;
}

// ============================================================================
// AC30 Top Boost: 2-band shelving EQ
//
// The real AC30 tone circuit has Treble and Bass pots only. The response is:
//   Bass:   low shelf at ~200Hz, range ±12dB
//   Treble: high shelf at ~3kHz, range ±12dB
//   Tone Cut: one-pole LPF, 2kHz (fully dark) to 20kHz (fully bright)
// ============================================================================

void ToneStack::recomputeACCoefficients()
{
    // AC30 tone controls are semi-passive: they primarily CUT, not boost.
    // Real circuit response:
    //   Full CCW (0): heavy cut (~-15dB)
    //   Noon (0.5):   moderate scoop (~-3dB)
    //   Full CW (1):  flat to mild boost (~+2dB)
    //
    // Asymmetric mapping: 0→-15dB, 0.5→-3dB, 1.0→+2dB

    // Bass control: low shelf at 200Hz
    float bassDB = bass_ < 0.5f
        ? -15.0f + bass_ * 24.0f        // 0→-15, 0.5→-3
        : -3.0f + (bass_ - 0.5f) * 10.0f;  // 0.5→-3, 1.0→+2
    computeShelf (200.0f, bassDB, 0.7f, lsB0_, lsB1_, lsB2_, lsA1_, lsA2_, false);

    // Treble control: high shelf at 3kHz (same asymmetric range)
    float trebleDB = treble_ < 0.5f
        ? -15.0f + treble_ * 24.0f
        : -3.0f + (treble_ - 0.5f) * 10.0f;
    computeShelf (3000.0f, trebleDB, 0.7f, hsB0_, hsB1_, hsB2_, hsA1_, hsA2_, true);

    // Tone Cut: one-pole LPF (real AC30 uses treble-bleed cap on phase inverter)
    // toneCut_ 0 = wide open (20kHz), 1 = heavy cut (800Hz)
    float cutFC = 20000.0f * std::pow (0.04f, toneCut_);  // 20kHz → 800Hz
    float w = 6.283185f * cutFC / static_cast<float> (sampleRate_);
    tcCoeff_ = w / (w + 1.0f);
}

void ToneStack::computeShelf (float fc, float gainDB, float Q,
                               float& b0, float& b1, float& b2,
                               float& a1, float& a2, bool isHigh)
{
    // Audio EQ Cookbook shelving filter (Robert Bristow-Johnson)
    float A  = std::pow (10.0f, gainDB / 40.0f);  // sqrt of linear gain
    float w0 = 6.283185f * fc / static_cast<float> (sampleRate_);
    float cosw0 = std::cos (w0);
    float sinw0 = std::sin (w0);
    float alpha = sinw0 / (2.0f * Q);
    float sqrtA = std::sqrt (A);

    float a0_raw;
    if (isHigh)
    {
        // High shelf
        b0 =        A * ((A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha);
        b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw0);
        b2 =        A * ((A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha);
        a0_raw =         (A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha;
        a1 =  2.0f *    ((A - 1.0f) - (A + 1.0f) * cosw0);
        a2 =             (A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha;
    }
    else
    {
        // Low shelf
        b0 =        A * ((A + 1.0f) - (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha);
        b1 =  2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw0);
        b2 =        A * ((A + 1.0f) - (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha);
        a0_raw =         (A + 1.0f) + (A - 1.0f) * cosw0 + 2.0f * sqrtA * alpha;
        a1 = -2.0f *    ((A - 1.0f) + (A + 1.0f) * cosw0);
        a2 =             (A + 1.0f) + (A - 1.0f) * cosw0 - 2.0f * sqrtA * alpha;
    }

    if (std::abs (a0_raw) < 1e-20f) a0_raw = 1e-20f;
    float invA0 = 1.0f / a0_raw;
    b0 *= invA0;
    b1 *= invA0;
    b2 *= invA0;
    a1 *= invA0;
    a2 *= invA0;
}
