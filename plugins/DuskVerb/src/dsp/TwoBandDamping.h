#pragma once

// Two-band shelving damping filter for FDN feedback loops.
// Uses a first-order lowpass at the crossover frequency to split the signal,
// then applies independent gains below (g_low) and above (g_high) the crossover.
//
// This is the "Bass Multiply / Treble Multiply" architecture from the Lexicon 480L:
// lows can sustain longer than mids (bassMultiply > 1) while highs roll off faster
// (trebleMultiply < 1).
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
