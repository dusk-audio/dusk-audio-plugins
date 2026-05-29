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
            // Fender Bassman / AB763 Twin tone-stack — Yeh & Smith DAFx-06
            // analytical reference: R1=250k R2=1M R3=25k R4=56k
            // C1=250pF C2=C3=20nF. Bass corner ~80-100 Hz; mid scoop
            // centred ~400 Hz; treble corner ~4-5 kHz. Noon (flat-knob)
            // character: mild mid scoop (-3 dB) + small treble lift
            // (+1 dB) — what a real AB763 measures at all-knobs-noon
            // before the user touches anything.
            return { 100.0f, 12.0f,
                     400.0f, 0.6f, 8.0f,
                    4500.0f, 0.0f, 9.0f,
                     true, false,
                    +1.0f, -3.0f, +1.0f }; // noon bias dB
        case Type::British:
            // Marshall JCM800 2203 — FMV stack with mods vs Fender:
            // R4=33k (Fender 56k) tightens mid coupling and pushes the
            // mid centre up to ~700-800 Hz; C1=470pF (Fender 250pF) drops
            // the treble corner to ~2-2.5 kHz. Treble at noon is REAL
            // JCM800's distinctive lift (+5 dB pre-fader) — measured on
            // production amps via Yeh/Smith stack. Mid scoop -3 dB.
            return {  80.0f, 10.0f,
                     750.0f, 1.0f, 10.0f,
                    2500.0f, 0.7f, 10.0f,  // moderate Q — JCM800 470pF cap +
                                            //  33k R4 produce a defined treble
                                            //  presence not a wide shelf
                     true, false,
                    +2.0f, -3.0f, +1.0f }; // noon bias dB — matches real FMV
                                            //  stack at 12-o'clock: mild LF
                                            //  bump, mid scoop, modest HF lift.
                                            //  Hz target left to user's TREBLE
                                            //  knob (real Marshall players
                                            //  tend to run 6-7/10 anyway).
        case Type::AC:
        default:
            // Vox AC30 Top Boost — no mid band. Treble peak centred at
            // 7 kHz with tight Q=2.2 for the unmistakable "chime" lift.
            // Noon bias: +6 dB treble pre-fader because real AC30 Top
            // Boost network has an inherent presence emphasis even with
            // the cut control at maximum cut — it's how the amp gets the
            // famous chime without HF boost from the user.
            return { 100.0f, 15.0f,
                       0.0f, 0.0f, 0.0f, // mid disabled
                    4500.0f, 1.0f, 15.0f, // peaking HF (chime) — wider Q + lower
                                          //  centre so the noon bias affects more
                                          //  octaves of HF energy (cab IR rolls
                                          //  off above 6 kHz, so the peak has to
                                          //  sit where the cab still passes audio)
                     false, true,
                    +2.0f, 0.0f, +10.0f }; // noon bias dB — strong HF lift for
                                            //  the AC30 chime signature
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

    // Knob 0..1 → ±max dB plus per-amp noon bias. Real tone stacks at
    // 12-o'clock knob position aren't flat — they carry the amp's
    // signature shape (Marshall scoop, AC30 chime, Fender mid dip).
    // noonBias terms encode that shape so the DSP shows the correct
    // character even when the user hasn't moved a knob.
    const float bassDb   = (bass_   - 0.5f) * 2.0f * v.bassMaxDb   + v.noonBassDb;
    const float trebleDb = (treble_ - 0.5f) * 2.0f * v.trebleMaxDb + v.noonTrebleDb;

    designLowShelf (bassBq_, v.bassHz, bassDb, sampleRate_);

    if (v.trebleQ > 0.0f)
        designPeakingEQ (trebleBq_, v.trebleHz, trebleDb, v.trebleQ, sampleRate_);
    else
        designHighShelf (trebleBq_, v.trebleHz, trebleDb, sampleRate_);

    if (v.hasMid)
    {
        const float midDb = (mid_ - 0.5f) * 2.0f * v.midMaxDb + v.noonMidDb;
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
