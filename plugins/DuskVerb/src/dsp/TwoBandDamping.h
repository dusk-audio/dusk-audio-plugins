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
        // Ensure crossover coefficients are positive and ordered (low < high)
        float lo = std::max (lowCrossoverCoeff, 1e-6f);
        float hi = std::max (highCrossoverCoeff, lo + 1e-6f);
        lpCoeff_[0] = lo;
        lpCoeff_[3] = hi;
        // Derive inner crossovers as geometric interpolation between outer pair.
        // sqrt(a*b) gives the geometric mean frequency between two LP coefficients.
        float midCoeff = std::sqrt (lo * hi);
        lpCoeff_[1] = std::sqrt (lo * midCoeff);
        lpCoeff_[2] = std::sqrt (midCoeff * hi);
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
        // Ensure crossover coefficients are positive and ordered (low < high)
        float lo = std::max (lowCoeff, 1e-6f);
        float hi = std::max (highCoeff, lo + 1e-6f);
        lpCoeff_[0] = lo;
        lpCoeff_[3] = hi;
        // Derive inner crossovers as geometric interpolation (same as 3-band setCoefficients)
        float midCoeff = std::sqrt (lo * hi);
        lpCoeff_[1] = std::sqrt (lo * midCoeff);
        lpCoeff_[2] = std::sqrt (midCoeff * hi);
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

// =====================================================================
// EightBandDamping — per-octave (1/1) damping with band edges fixed at
//   125 / 250 / 500 / 1000 / 2000 / 4000 / 8000 Hz
// (matches the analyze_ir.py octave bins so each per-band RT60 from
//  the script maps 1:1 to a band gain here.)
//
// Topology: 7 first-order LPFs chained Linkwitz-style. Each LPF passes
// the band's lower portion; subtracting from the input gives the upper
// portion which feeds the next LPF. Final residual after the 7th LPF
// is band 7 (above 8 kHz).
//
// Bands:
//   0:    63 –  125  Hz
//   1:   125 –  250  Hz
//   2:   250 –  500  Hz
//   3:   500 – 1000  Hz   (mid-band — engine "DECAY" maps here)
//   4:  1000 – 2000  Hz
//   5:  2000 – 4000  Hz
//   6:  4000 – 8000  Hz
//   7:  8000 – 16000 Hz
//
// Use:
//   damp.prepare (sampleRate);                 // sets fixed crossovers
//   damp.setRt60PerBand (rt60Sec, delaySamples); // updates per-band gain
//   float y = damp.process (x);                  // per-sample
// =====================================================================
class EightBandDamping
{
public:
    static constexpr int kNumBands = 8;

    // Crossover edges (upper bound of bands 0..6). Band 7 has no upper bound.
    static constexpr float kBandUpperHz[kNumBands - 1] = {
        125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f
    };

    void prepare (float sampleRate)
    {
        sampleRate_ = sampleRate;
        for (int i = 0; i < kNumBands - 1; ++i)
        {
            // First-order LPF coefficient: c = exp(-2pi * fc / sr)
            const float twoPi = 6.283185307179586f;
            lpCoeff_[i] = std::exp (-twoPi * kBandUpperHz[i] / sampleRate);
        }
        reset();
    }

    // Set per-band per-sample gain directly. Use when you have raw gains.
    void setGains (const float g[kNumBands])
    {
        for (int i = 0; i < kNumBands; ++i)
            g_[i] = std::clamp (g[i], 0.0f, 0.9999f);
    }

    // Set per-band gains derived from per-band RT60 in seconds + delay length.
    // gain[n] = exp(-3 * ln(10) * delay / (sr * RT60[n]))
    // Matches the standard Schroeder-FDN feedback-gain-from-RT60 formula.
    void setRt60PerBand (const float rt60Seconds[kNumBands], float delaySamples)
    {
        const float numerator = -3.0f * 2.302585092994046f * delaySamples / sampleRate_;
        for (int i = 0; i < kNumBands; ++i)
        {
            const float rt60 = std::max (rt60Seconds[i], 0.001f);
            g_[i] = std::clamp (std::exp (numerator / rt60), 0.0f, 0.9999f);
        }
    }

    // Single-band update — useful for the EQ tab updating one knob at a time.
    void setBandRt60 (int band, float rt60Seconds, float delaySamples)
    {
        if (band < 0 || band >= kNumBands) return;
        const float numerator = -3.0f * 2.302585092994046f * delaySamples / sampleRate_;
        const float rt60 = std::max (rt60Seconds, 0.001f);
        g_[band] = std::clamp (std::exp (numerator / rt60), 0.0f, 0.9999f);
    }

    float process (float input)
    {
        // Walk the LPF chain; each iteration extracts one band and forwards the
        // remainder to the next stage.
        float remainder = input;
        float bandSum = 0.0f;
        for (int i = 0; i < kNumBands - 1; ++i)
        {
            // Update the LPF state with the current remainder.
            lpState_[i] = (1.0f - lpCoeff_[i]) * remainder + lpCoeff_[i] * lpState_[i];
            // The LPF output is band i; what's left after subtracting it is
            // the next-higher band's input.
            bandSum += g_[i] * lpState_[i];
            remainder -= lpState_[i];
        }
        // Top band (no LPF — pure residual)
        bandSum += g_[kNumBands - 1] * remainder;
        return bandSum;
    }

    void reset()
    {
        for (int i = 0; i < kNumBands - 1; ++i)
            lpState_[i] = 0.0f;
    }

private:
    float sampleRate_ = 48000.0f;
    float g_[kNumBands]      = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    float lpCoeff_[kNumBands - 1] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float lpState_[kNumBands - 1] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
};

// =====================================================================
// EightBandPeakingEQ — 8 cascaded RBJ peaking biquads at fixed octave
// centers (89, 177, 354, 707, 1414, 2828, 5657, 11314 Hz) with Q=1.41
// (one-octave bandwidth). Per-band gain in dB.
//
// Use:
//   eq.prepare (sampleRate);
//   eq.setBandGainDb (band, dB);     // any band, any time
//   float yL = eq.processL (xL);     // per-sample, stereo state
//   float yR = eq.processR (xR);
// =====================================================================
class EightBandPeakingEQ
{
public:
    static constexpr int kNumBands = 8;

    // Geometric center of each octave band (sqrt(low * high)).
    static constexpr float kCenterHz[kNumBands] = {
        89.0f, 177.0f, 354.0f, 707.0f, 1414.0f, 2828.0f, 5657.0f, 11314.0f
    };
    static constexpr float kQ = 1.41f;  // one-octave bandwidth

    void prepare (float sampleRate)
    {
        sampleRate_ = sampleRate;
        for (int i = 0; i < kNumBands; ++i)
            recomputeBand (i);
        reset();
    }

    void setBandGainDb (int band, float dB)
    {
        if (band < 0 || band >= kNumBands) return;
        gainDb_[band] = std::clamp (dB, -24.0f, 24.0f);
        recomputeBand (band);
    }

    float processL (float x)
    {
        for (int i = 0; i < kNumBands; ++i)
        {
            const float y = b0_[i] * x + b1_[i] * xL1_[i] + b2_[i] * xL2_[i]
                            - a1_[i] * yL1_[i] - a2_[i] * yL2_[i];
            xL2_[i] = xL1_[i]; xL1_[i] = x;
            yL2_[i] = yL1_[i]; yL1_[i] = y;
            x = y;
        }
        return x;
    }

    float processR (float x)
    {
        for (int i = 0; i < kNumBands; ++i)
        {
            const float y = b0_[i] * x + b1_[i] * xR1_[i] + b2_[i] * xR2_[i]
                            - a1_[i] * yR1_[i] - a2_[i] * yR2_[i];
            xR2_[i] = xR1_[i]; xR1_[i] = x;
            yR2_[i] = yR1_[i]; yR1_[i] = y;
            x = y;
        }
        return x;
    }

    void reset()
    {
        for (int i = 0; i < kNumBands; ++i)
        {
            xL1_[i] = xL2_[i] = yL1_[i] = yL2_[i] = 0.0f;
            xR1_[i] = xR2_[i] = yR1_[i] = yR2_[i] = 0.0f;
        }
    }

private:
    // RBJ peaking-EQ biquad. https://www.w3.org/TR/audio-eq-cookbook/
    void recomputeBand (int i)
    {
        const float A     = std::pow (10.0f, gainDb_[i] / 40.0f);
        const float w0    = 6.283185307179586f * std::min (kCenterHz[i], 0.49f * sampleRate_) / sampleRate_;
        const float cosw  = std::cos (w0);
        const float sinw  = std::sin (w0);
        const float alpha = sinw / (2.0f * kQ);

        const float a0 = 1.0f + alpha / A;
        b0_[i] = (1.0f + alpha * A) / a0;
        b1_[i] = (-2.0f * cosw)     / a0;
        b2_[i] = (1.0f - alpha * A) / a0;
        a1_[i] = (-2.0f * cosw)     / a0;
        a2_[i] = (1.0f - alpha / A) / a0;
    }

    float sampleRate_ = 48000.0f;
    float gainDb_[kNumBands] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    // Biquad coefficients per band
    float b0_[kNumBands] {}, b1_[kNumBands] {}, b2_[kNumBands] {};
    float a1_[kNumBands] {}, a2_[kNumBands] {};

    // Per-channel direct-form-1 state per band
    float xL1_[kNumBands] {}, xL2_[kNumBands] {}, yL1_[kNumBands] {}, yL2_[kNumBands] {};
    float xR1_[kNumBands] {}, xR2_[kNumBands] {}, yR1_[kNumBands] {}, yR2_[kNumBands] {};
};
