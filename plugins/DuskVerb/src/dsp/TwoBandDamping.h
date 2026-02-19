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
