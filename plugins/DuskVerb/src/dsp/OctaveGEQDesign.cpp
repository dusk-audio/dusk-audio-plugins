// =====================================================================
// OctaveGEQDesign.cpp — design-time bodies for OctaveBandDamping
// (AccurateHall per-octave GEQ).
//
// These live in their OWN translation unit on purpose: when the iterative
// composite-exact design was written inline in TwoBandDamping.h, its mere
// presence in the FDNReverb.cpp TU perturbed the FP codegen of the plain
// FDNReverbT<false> hot loop (~4e-6 render drift through the feedback —
// the QuadTankT template lesson, memory duskverb_bitnull_codegen_limit).
// Out-of-line definitions keep the header (and every including TU) free of
// the design code; only the call site inside `if constexpr (WithOctaveGEQ)`
// references these symbols. Message-thread / preset-apply time only —
// never the audio thread.
// =====================================================================

#include "TwoBandDamping.h"

double OctaveBandDamping::magnitudeAt (const Coeffs& c, double fHz,
                                       double sampleRate)
{
    const double w  = 2.0 * 3.141592653589793
                    * std::min (fHz, 0.49 * sampleRate) / sampleRate;
    const double cr = std::cos (w),  ci = -std::sin (w);        // z^-1
    const double c2r = cr * cr - ci * ci, c2i = 2.0 * cr * ci;  // z^-2
    double mag = static_cast<double> (c.broadbandGain);
    for (int s = 0; s < kNumShelves; ++s)
    {
        const double nr = c.b0[s] + c.b1[s] * cr + c.b2[s] * c2r;
        const double ni =           c.b1[s] * ci + c.b2[s] * c2i;
        const double dr = 1.0     + c.a1[s] * cr + c.a2[s] * c2r;
        const double di =           c.a1[s] * ci + c.a2[s] * c2i;
        mag *= std::sqrt ((nr * nr + ni * ni)
                        / std::max (dr * dr + di * di, 1.0e-24));
    }
    return mag;
}

OctaveBandDamping::Coeffs OctaveBandDamping::buildCascade (
    const float* gBand, const float* xoverHz, float sampleRate)
{
    Coeffs c;
    ShelfBiquad bq;
    for (int k = 0; k < kMidBand; ++k)
    {
        const float ratio = gBand[k] / std::max (gBand[k + 1], 1.0e-12f);
        bq.designLowShelf (ratio, xoverHz[k], sampleRate);
        c.b0[k] = bq.b0; c.b1[k] = bq.b1; c.b2[k] = bq.b2; c.a1[k] = bq.a1; c.a2[k] = bq.a2;
    }
    for (int k = kMidBand; k < kNumShelves; ++k)
    {
        const float ratio = gBand[k + 1] / std::max (gBand[k], 1.0e-12f);
        bq.designHighShelf (ratio, xoverHz[k], sampleRate);
        c.b0[k] = bq.b0; c.b1[k] = bq.b1; c.b2[k] = bq.b2; c.a1[k] = bq.a1; c.a2[k] = bq.a2;
    }
    c.broadbandGain = gBand[kMidBand];
    return c;
}

OctaveBandDamping::Coeffs OctaveBandDamping::designCoeffs (
    const float* gBand, const float* xoverHz, float sampleRate)
{
    static constexpr double kCentresHz[kNumBands] = {
        63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0
    };
    double tdB[kNumBands], cdB[kNumBands];
    for (int k = 0; k < kNumBands; ++k)
    {
        const double g = std::clamp (static_cast<double> (gBand[k]), 1.0e-3, 0.9999);
        tdB[k] = 20.0 * std::log10 (g);
        cdB[k] = tdB[k];
    }

    Coeffs best = buildCascade (gBand, xoverHz, sampleRate);
    double bestErr = 1.0e30;
    double lambda  = 0.7;
    int    stall   = 0;
    for (int iter = 0; iter < 25; ++iter)
    {
        float g[kNumBands];
        for (int k = 0; k < kNumBands; ++k)
            g[k] = static_cast<float> (std::clamp (
                       std::pow (10.0, cdB[k] / 20.0), 1.0e-3, 0.9999));
        const Coeffs c = buildCascade (g, xoverHz, sampleRate);

        double mdB[kNumBands], err = 0.0;
        for (int k = 0; k < kNumBands; ++k)
        {
            mdB[k] = 20.0 * std::log10 (
                         std::max (magnitudeAt (c, kCentresHz[k], sampleRate), 1.0e-12));
            err = std::max (err, std::fabs (tdB[k] - mdB[k]));
        }
        if (err < bestErr) { best = c; bestErr = err; stall = 0; }
        else if (++stall >= 3) { lambda *= 0.5; stall = 0; }
        if (err < 1.0e-3)
            break;
        for (int k = 0; k < kNumBands; ++k)
            cdB[k] += lambda * (tdB[k] - mdB[k]);
    }

    // Stability guard: corrected commanded gains can crest ABOVE the centre
    // values BETWEEN centres (and beyond 16 kHz toward Nyquist). Loop
    // stability needs composite |H| < 1 everywhere, so sweep analytically
    // and scale the broadband down if anything exceeds the per-band clamp
    // ceiling. Rarely fires (the always-on AA/DC filters give real margin
    // at the extremes, conservatively ignored here).
    double maxMag = std::max (magnitudeAt (best, 0.0, sampleRate),
                              magnitudeAt (best, 0.5 * sampleRate, sampleRate));
    const double fLo = 40.0, fHi = 0.49 * sampleRate;
    for (int i = 0; i < 96; ++i)
    {
        const double f = fLo * std::pow (fHi / fLo, i / 95.0);
        maxMag = std::max (maxMag, magnitudeAt (best, f, sampleRate));
    }
    if (maxMag > 0.9999)
        best.broadbandGain = static_cast<float> (best.broadbandGain * 0.9998 / maxMag);
    return best;
}
