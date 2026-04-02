#include "ToneStack.h"

static constexpr float kPi = 3.14159265358979323846f;

// =========================================================================
// Component values from real amp schematics
// =========================================================================

// Fender Deluxe Reverb AB763 tone stack
const ToneStack::Components ToneStack::kRoundComponents = {
    100000.0f,    // R1 = 100k slope resistor (AB763 blackface)
    1000000.0f,   // R2 = 1M bass pot (log taper)
    6800.0f,      // R3 = 6.8k fixed mid resistor (AB763 has no mid pot)
    250000.0f,    // R4 = 250k treble pot (audio taper)
    250e-12f,     // C1 = 250pF treble cap
    0.1e-6f,      // C2 = 100nF (AB763 uses same value as bass cap)
    0.1e-6f,      // C3 = 100nF bass cap (blackface signature)
    false         // NO mid pot (AB763 Deluxe Reverb has fixed mid)
};

// Vox AC30 Top Boost (2-control tone stack, 1964 circuit)
const ToneStack::Components ToneStack::kChimeComponents = {
    100000.0f,    // R1 = 100k (treble/bass mixing resistor in Top Boost)
    1000000.0f,   // R2 = 1M bass pot (log taper)
    250000.0f,    // R3 = 250k (fixed internal mid resistance, no pot)
    1000000.0f,   // R4 = 1M treble pot (log taper, per AC30 schematic)
    56e-12f,      // C1 = 56pF treble cap (original TB coupling cap)
    0.022e-6f,    // C2 = 22nF
    0.1e-6f,      // C3 = 100nF bass cap
    false         // NO mid pot
};

// Marshall 1959 Super Lead Plexi tone stack (Bassman-derived)
const ToneStack::Components ToneStack::kPunchComponents = {
    33000.0f,     // R1 = 33k slope resistor
    1000000.0f,   // R2 = 1M bass pot (log taper)
    25000.0f,     // R3 = 25k mid pot (linear) -- Marshall's smaller mid pot value
    250000.0f,    // R4 = 250k treble pot (linear)
    500e-12f,     // C1 = 500pF treble cap (per 1959 schematic, some variants use 470pF)
    0.022e-6f,    // C2 = 22nF mid cap
    0.022e-6f,    // C3 = 22nF bass cap
    true          // has mid pot
};

// =========================================================================
// Public interface
// =========================================================================

void ToneStack::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;
    coeffsDirty_ = true;
    recomputeCoefficients();
    // On prepare, snap coefficients immediately (no smoothing)
    filter_.b0 = targetCoeffs_.b0;
    filter_.b1 = targetCoeffs_.b1;
    filter_.b2 = targetCoeffs_.b2;
    filter_.b3 = targetCoeffs_.b3;
    filter_.a1 = targetCoeffs_.a1;
    filter_.a2 = targetCoeffs_.a2;
    filter_.a3 = targetCoeffs_.a3;
    smoothBlocksRemaining_ = 0;
    reset();
}

void ToneStack::reset()
{
    filter_.reset();
    midOverlay_.reset();
}

void ToneStack::setModel (Model model)
{
    if (model == currentModel_) return;
    currentModel_ = model;
    coeffsDirty_ = true;
}

void ToneStack::setBass (float value01)
{
    float v = std::clamp (value01, 0.0f, 1.0f);
    if (v == bass_) return;
    bass_ = v;
    coeffsDirty_ = true;
}

void ToneStack::setMid (float value01)
{
    float v = std::clamp (value01, 0.0f, 1.0f);
    if (v == mid_) return;
    mid_ = v;
    coeffsDirty_ = true;
}

void ToneStack::setTreble (float value01)
{
    float v = std::clamp (value01, 0.0f, 1.0f);
    if (v == treble_) return;
    treble_ = v;
    coeffsDirty_ = true;
}

void ToneStack::process (float* buffer, int numSamples)
{
    if (coeffsDirty_)
    {
        recomputeCoefficients();
        coeffsDirty_ = false;
    }

    // Smooth coefficient interpolation: lerp filter coefficients toward target
    // over kSmoothBlocks blocks to prevent clicks from abrupt coefficient jumps
    if (smoothBlocksRemaining_ > 0)
    {
        float alpha = 1.0f / static_cast<float> (smoothBlocksRemaining_);
        filter_.b0 += (targetCoeffs_.b0 - filter_.b0) * alpha;
        filter_.b1 += (targetCoeffs_.b1 - filter_.b1) * alpha;
        filter_.b2 += (targetCoeffs_.b2 - filter_.b2) * alpha;
        filter_.b3 += (targetCoeffs_.b3 - filter_.b3) * alpha;
        filter_.a1 += (targetCoeffs_.a1 - filter_.a1) * alpha;
        filter_.a2 += (targetCoeffs_.a2 - filter_.a2) * alpha;
        filter_.a3 += (targetCoeffs_.a3 - filter_.a3) * alpha;
        --smoothBlocksRemaining_;
    }

    // Models without a mid pot (Round AB763, Chime AC30) use a peaking
    // EQ overlay so the mid knob still does something useful.
    bool useMidOverlay = (currentModel_ == Model::Chime || currentModel_ == Model::Round);

    if (useMidOverlay)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float sample = filter_.process (buffer[i]);
            buffer[i] = midOverlay_.process (sample);
        }
    }
    else
    {
        for (int i = 0; i < numSamples; ++i)
            buffer[i] = filter_.process (buffer[i]);
    }
}

// =========================================================================
// Coefficient computation
// =========================================================================

const ToneStack::Components& ToneStack::getCurrentComponents() const
{
    switch (currentModel_)
    {
        case Model::Round: return kRoundComponents;
        case Model::Chime: return kChimeComponents;
        case Model::Punch: return kPunchComponents;
    }
    return kRoundComponents;
}

void ToneStack::recomputeCoefficients()
{
    const auto& comp = getCurrentComponents();

    // Map knob positions to effective pot resistances.
    // Bass pot is inverted: user bass=1 (max) → low R2 to ground → more bass through.
    // No log taper — the circuit interaction already provides a natural taper.
    float b = std::clamp (1.0f - bass_, 0.01f, 0.99f);
    float t = std::clamp (treble_, 0.01f, 0.99f);
    float m = std::clamp (comp.hasMidPot ? mid_ : 0.5f, 0.01f, 0.99f);

    computeTMBCoefficients (comp, b, t, m);

    if (currentModel_ == Model::Chime || currentModel_ == Model::Round)
        computeChimeMidOverlay (mid_);
}

void ToneStack::computeTMBCoefficients (const Components& comp,
                                         float bass, float treble, float mid)
{
    // Use double precision for the polynomial and bilinear transform math.
    // The component values span 26+ orders of magnitude (250e-12 to 1e6) and
    // c^3 reaches 8.85e14, which overflows float32 precision when multiplied
    // by the RC products.
    double R1 = comp.R1;
    double C1 = comp.C1;
    double C2 = comp.C2;
    double C3 = comp.C3;

    double R2 = static_cast<double> (bass) * comp.R2;
    double R3 = static_cast<double> (mid) * comp.R3;
    double R4 = static_cast<double> (treble) * comp.R4;

    // Numerator coefficients (transfer function H(s) = N(s)/D(s))
    double b1v = C1 * R1 + C3 * R3 + C1 * R2 + C2 * R2 + C1 * R3 + C2 * R3;

    double b2v = C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4
               + C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4 + C1 * C3 * R2 * R4
               + C1 * C2 * R1 * R3 + C1 * C3 * R1 * R3 + C2 * C3 * R3 * R4;

    double b3v = C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4
               + C1 * C2 * C3 * R1 * R3 * R4 + C1 * C2 * C3 * R3 * R3 * R4
               + C1 * C2 * C3 * R1 * R2 * R4;

    // Denominator coefficients
    double a1v = C1 * R1 + C1 * R3 + C2 * R3 + C2 * R4 + C3 * R4
               + C3 * R3 + C1 * R2 + C2 * R2;

    double a2v = C1 * C3 * R1 * R3 + C2 * C3 * R3 * R3
               + C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3
               + C1 * C2 * R1 * R3 + C1 * C2 * R3 * R4 + C1 * C3 * R1 * R4
               + C2 * C3 * R3 * R4 + C1 * C2 * R1 * R4 + C1 * C3 * R3 * R4
               + C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4
               + C1 * C3 * R2 * R4 + C2 * C3 * R2 * R4;

    double a3v = C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4
               + C1 * C2 * C3 * R1 * R3 * R4 + C1 * C2 * C3 * R3 * R3 * R4
               + C1 * C2 * C3 * R1 * R2 * R4
               + C1 * C2 * C3 * R1 * R3 * R4;

    // Bilinear transform: s = c * (z-1)/(z+1) where c = 2 * sampleRate
    double c  = 2.0 * sampleRate_;
    double c2 = c * c;
    double c3 = c2 * c;

    // Denominator
    double A0 = 1.0 + a1v * c + a2v * c2 + a3v * c3;
    double A1 = 3.0 + a1v * c - a2v * c2 - 3.0 * a3v * c3;
    double A2 = 3.0 - a1v * c - a2v * c2 + 3.0 * a3v * c3;
    double A3 = 1.0 - a1v * c + a2v * c2 - a3v * c3;

    // Numerator (b0s = 0 — no DC path through capacitors)
    double B0 =       b1v * c + b2v * c2 + b3v * c3;
    double B1 =       b1v * c - b2v * c2 - 3.0 * b3v * c3;
    double B2 =      -b1v * c - b2v * c2 + 3.0 * b3v * c3;
    double B3 =      -b1v * c + b2v * c2 - b3v * c3;

    // Normalize and convert back to float for the per-sample filter
    if (std::abs (A0) < 1e-12)
    {
        targetCoeffs_.b0 = 1.0f;
        targetCoeffs_.b1 = targetCoeffs_.b2 = targetCoeffs_.b3 = 0.0f;
        targetCoeffs_.a1 = targetCoeffs_.a2 = targetCoeffs_.a3 = 0.0f;
        smoothBlocksRemaining_ = kSmoothBlocks;
        return;
    }

    double invA0 = 1.0 / A0;
    targetCoeffs_.b0 = static_cast<float> (B0 * invA0);
    targetCoeffs_.b1 = static_cast<float> (B1 * invA0);
    targetCoeffs_.b2 = static_cast<float> (B2 * invA0);
    targetCoeffs_.b3 = static_cast<float> (B3 * invA0);
    targetCoeffs_.a1 = static_cast<float> (A1 * invA0);
    targetCoeffs_.a2 = static_cast<float> (A2 * invA0);
    targetCoeffs_.a3 = static_cast<float> (A3 * invA0);

    // Safety clamp: if any feedback coefficient is unreasonably large,
    // the filter is near-unstable at this knob position — fall back to passthrough
    if (std::abs (targetCoeffs_.a1) > 100.0f ||
        std::abs (targetCoeffs_.a2) > 100.0f ||
        std::abs (targetCoeffs_.a3) > 100.0f)
    {
        targetCoeffs_.b0 = 1.0f;
        targetCoeffs_.b1 = targetCoeffs_.b2 = targetCoeffs_.b3 = 0.0f;
        targetCoeffs_.a1 = targetCoeffs_.a2 = targetCoeffs_.a3 = 0.0f;
    }

    smoothBlocksRemaining_ = kSmoothBlocks;
}

void ToneStack::computeChimeMidOverlay (float mid)
{
    // The Vox AC30 has no mid pot. We map the mid knob to a subtle
    // peaking EQ overlay at 800Hz to give users some mid control,
    // since the UI exposes 3 knobs for all models.
    // Range: -6 to +6 dB at 800 Hz, Q=1.0

    float gainDB = (mid - 0.5f) * 12.0f;
    float freq = 800.0f;
    float Q = 1.0f;

    float nyquist = static_cast<float> (sampleRate_) * 0.49f;
    freq = std::min (freq, nyquist);

    float A  = std::pow (10.0f, gainDB / 40.0f);
    float w0 = 2.0f * kPi * freq / static_cast<float> (sampleRate_);
    float cosw0 = std::cos (w0);
    float sinw0 = std::sin (w0);
    float alpha = sinw0 / (2.0f * Q);

    float a0 = 1.0f + alpha / A;
    if (std::abs (a0) < 1e-7f)
    {
        midOverlay_.b0 = 1.0f;
        midOverlay_.b1 = midOverlay_.b2 = midOverlay_.a1 = midOverlay_.a2 = 0.0f;
        return;
    }

    midOverlay_.b0 = (1.0f + alpha * A) / a0;
    midOverlay_.b1 = (-2.0f * cosw0) / a0;
    midOverlay_.b2 = (1.0f - alpha * A) / a0;
    midOverlay_.a1 = (-2.0f * cosw0) / a0;
    midOverlay_.a2 = (1.0f - alpha / A) / a0;
}
