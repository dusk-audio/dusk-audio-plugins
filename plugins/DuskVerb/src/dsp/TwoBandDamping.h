#pragma once

#include <algorithm>
#include <cmath>
// Two-band shelving damping filter for FDN feedback loops.
// Uses a first-order lowpass at the crossover frequency to split the signal,
// then applies independent gains below (g_low) and above (g_high) the crossover.
//
// Classic "Bass Multiply / Treble Multiply" architecture: lows can sustain longer
// than mids (bassMultiply > 1) while highs roll off faster (trebleMultiply < 1).
class TwoBandDamping
{
public:
    // crossoverCoeff = exp(-2*pi*fc/sr), gLow/gHigh are per-delay-pass gains
    void setCoefficients (float gLow, float gHigh, float crossoverCoeff)
    {
        gLow_ = gLow;
        gHigh_ = gHigh;
        lpCoeff_ = crossoverCoeff;
    }

    float process (float input)
    {
        // First-order lowpass at crossover: lp[n] = (1-c)*x[n] + c*lp[n-1]
        lpState_ = (1.0f - lpCoeff_) * input + lpCoeff_ * lpState_;

        // output = gHigh * x + (gLow - gHigh) * lp
        // At DC: lp → x, so output → gHigh*x + (gLow-gHigh)*x = gLow*x
        // At Nyquist: lp → 0, so output → gHigh*x
        return gHigh_ * input + (gLow_ - gHigh_) * lpState_;
    }

    void reset() { lpState_ = 0.0f; }

private:
    float gLow_ = 1.0f;
    float gHigh_ = 1.0f;
    float lpCoeff_ = 0.0f;
    float lpState_ = 0.0f;
};

// Three-band shelving damping filter for FDN feedback loops.
// Extends TwoBandDamping with a second crossover to separate mid and high bands:
//   Low  (< lowCrossover):   gLow  = gBase^(1/bassMultiply)  — bass decay
//   Mid  (lowCrossover..highCrossover): gMid = gBase           — broadband reference
//   High (> highCrossover):  gHigh = gBase^(1/trebleMultiply) — treble decay
//
// This allows finer spectral control: mid frequencies (2.5-6kHz) decay at the
// natural rate while HF damping (treble multiply) only affects very high frequencies.
// When highCrossoverHz >= Nyquist, the high band is empty and this collapses to
// two-band behavior with gMid replacing gHigh.
class ThreeBandDamping
{
public:
    void setCoefficients (float gLow, float gMid, float gHigh,
                          float lowCrossoverCoeff, float highCrossoverCoeff)
    {
        gLow_ = gLow;
        gMid_ = gMid;
        gHigh_ = gHigh;
        lpCoeff1_ = lowCrossoverCoeff;
        lpCoeff2_ = highCrossoverCoeff;
    }

    void setLowCrossoverCoeff (float coeff)
    {
        constexpr float eps = 1e-6f;
        lpCoeff1_ = std::clamp (coeff, eps, 1.0f - eps);
    }

    void setHighCrossoverCoeff (float coeff)
    {
        constexpr float eps = 1e-6f;
        lpCoeff2_ = std::clamp (coeff, eps, 1.0f - eps);
    }

    float process (float input)
    {
        // LP1: split into low band and mid+high
        lp1State_ = (1.0f - lpCoeff1_) * input + lpCoeff1_ * lp1State_;
        float midHigh = input - lp1State_;

        // LP2: split mid+high into mid band and high band
        lp2State_ = (1.0f - lpCoeff2_) * midHigh + lpCoeff2_ * lp2State_;
        float high = midHigh - lp2State_;

        return gLow_ * lp1State_ + gMid_ * lp2State_ + gHigh_ * high;
    }

    void reset()
    {
        lp1State_ = 0.0f;
        lp2State_ = 0.0f;
    }

private:
    float gLow_ = 1.0f;
    float gMid_ = 1.0f;
    float gHigh_ = 1.0f;
    float lpCoeff1_ = 0.0f;  // Low crossover coefficient
    float lpCoeff2_ = 0.0f;  // High crossover coefficient
    float lp1State_ = 0.0f;
    float lp2State_ = 0.0f;
};

// Five-band shelving damping filter for per-preset FDN feedback loops.
// Extends ThreeBandDamping with two additional crossover splits:
//   Band 1: < f1  (sub-bass)   → g[0]
//   Band 2: f1..f2 (bass)      → g[1]
//   Band 3: f2..f3 (mid)       → g[2]
//   Band 4: f3..f4 (presence)  → g[3]
//   Band 5: > f4   (air)       → g[4]
//
// Per-band decay multipliers allow matching VV's frequency-dependent RT60
// profile. Used only in per-preset wrappers for tailSlope calibration.
class FiveBandDamping
{
public:
    void setCoefficients (const float g[5], const float lpCoeff[4])
    {
        for (int i = 0; i < 5; ++i)
            g_[i] = g[i];
        for (int i = 0; i < 4; ++i)
            lpCoeff_[i] = lpCoeff[i];
    }

    // Convenience: set gains only (crossovers unchanged)
    void setGains (const float g[5])
    {
        for (int i = 0; i < 5; ++i)
            g_[i] = g[i];
    }

    // Convenience: set crossover coefficients only (gains unchanged)
    void setCrossovers (const float lpCoeff[4])
    {
        for (int i = 0; i < 4; ++i)
            lpCoeff_[i] = lpCoeff[i];
    }

    // ThreeBandDamping-compatible setter (maps to bands 0, 2, 4; bands 1, 3 interpolated).
    // Inner crossovers (1, 2) are derived as geometric interpolation between the
    // outer pair when not explicitly set via setCrossovers().
    void setCoefficients (float gLow, float gMid, float gHigh,
                          float lowCrossoverCoeff, float highCrossoverCoeff)
    {
        g_[0] = gLow;
        g_[1] = gLow + (gMid - gLow) * bandBlend_[1];
        g_[2] = gMid;
        g_[3] = gMid + (gHigh - gMid) * bandBlend_[3];
        g_[4] = gHigh;
        lpCoeff_[0] = lowCrossoverCoeff;
        lpCoeff_[3] = highCrossoverCoeff;
        // Derive inner crossovers as geometric interpolation between outer pair.
        // sqrt(a*b) gives the geometric mean frequency between two LP coefficients.
        float midCoeff = std::sqrt (lowCrossoverCoeff * highCrossoverCoeff);
        lpCoeff_[1] = std::sqrt (lowCrossoverCoeff * midCoeff);
        lpCoeff_[2] = std::sqrt (midCoeff * highCrossoverCoeff);
    }

    // Set per-band blend factors for ThreeBandDamping-compatible fallback.
    // bandBlend[1] blends band 1 between gLow and gMid.
    // bandBlend[3] blends band 3 between gMid and gHigh.
    void setBandBlend (float blend1, float blend3)
    {
        bandBlend_[1] = std::clamp (blend1, 0.0f, 1.0f);
        bandBlend_[3] = std::clamp (blend3, 0.0f, 1.0f);
    }

    // Per-band decay multiplier overrides: g[band] = gBase^(1/multiplier[band])
    // Set by the per-preset wrapper from baked VV-derived constants.
    void setBandMultipliers (const float mult[5])
    {
        for (int i = 0; i < 5; ++i)
            bandMult_[i] = mult[i];
        hasBandMultipliers_ = true;
    }

    void clearBandMultipliers()
    {
        hasBandMultipliers_ = false;
    }

    // Compute per-band gains from gBase using band multipliers.
    // Call this from updateDecayCoefficients() instead of the standard formula.
    void computeGainsFromBase (float gBase, float lowCoeff, float highCoeff)
    {
        if (hasBandMultipliers_)
        {
            for (int i = 0; i < 5; ++i)
                g_[i] = std::clamp (std::pow (gBase, 1.0f / std::max (bandMult_[i], 0.01f)),
                                    0.001f, 0.9999f);
        }
        else
        {
            for (int i = 0; i < 5; ++i)
                g_[i] = std::clamp (gBase, 0.001f, 0.9999f);
        }
        lpCoeff_[0] = lowCoeff;
        lpCoeff_[3] = highCoeff;
        // Derive inner crossovers as geometric interpolation (same as 3-band setCoefficients)
        float midCoeff = std::sqrt (lowCoeff * highCoeff);
        lpCoeff_[1] = std::sqrt (lowCoeff * midCoeff);
        lpCoeff_[2] = std::sqrt (midCoeff * highCoeff);
    }
    float process (float input)
    {
        // LP1: split into band 0 (sub-bass) and remainder
        lpState_[0] = (1.0f - lpCoeff_[0]) * input + lpCoeff_[0] * lpState_[0];
        float rem1 = input - lpState_[0];

        // LP2: split remainder into band 1 (bass) and remainder
        lpState_[1] = (1.0f - lpCoeff_[1]) * rem1 + lpCoeff_[1] * lpState_[1];
        float rem2 = rem1 - lpState_[1];

        // LP3: split remainder into band 2 (mid) and remainder
        lpState_[2] = (1.0f - lpCoeff_[2]) * rem2 + lpCoeff_[2] * lpState_[2];
        float rem3 = rem2 - lpState_[2];

        // LP4: split remainder into band 3 (presence) and band 4 (air)
        lpState_[3] = (1.0f - lpCoeff_[3]) * rem3 + lpCoeff_[3] * lpState_[3];
        float air = rem3 - lpState_[3];

        return g_[0] * lpState_[0] + g_[1] * lpState_[1] + g_[2] * lpState_[2]
             + g_[3] * lpState_[3] + g_[4] * air;
    }

    void reset()
    {
        for (int i = 0; i < 4; ++i)
            lpState_[i] = 0.0f;
    }

    void setLowCrossoverCoeff (float coeff)
    {
        constexpr float eps = 1e-6f;
        lpCoeff_[0] = std::clamp (coeff, eps, 1.0f - eps);
        recomputeInnerCrossovers();
    }

    void setHighCrossoverCoeff (float coeff)
    {
        constexpr float eps = 1e-6f;
        lpCoeff_[3] = std::clamp (coeff, eps, 1.0f - eps);
        recomputeInnerCrossovers();
    }

private:
    void recomputeInnerCrossovers()
    {
        float midCoeff = std::sqrt (lpCoeff_[0] * lpCoeff_[3]);
        lpCoeff_[1] = std::sqrt (lpCoeff_[0] * midCoeff);
        lpCoeff_[2] = std::sqrt (midCoeff * lpCoeff_[3]);
    }

    float g_[5] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    float lpCoeff_[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float lpState_[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float bandMult_[5] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    float bandBlend_[5] = { 0.0f, 0.5f, 0.0f, 0.5f, 0.0f };
    bool hasBandMultipliers_ = false;
};
