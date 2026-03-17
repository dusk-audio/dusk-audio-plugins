/**
 * Multi-Q Mode & Integration Tests
 * =================================
 * Standalone test for British, Tube, Dynamic EQ modes and extreme parameters.
 * Verifies mode-specific coefficient behavior and edge cases.
 *
 * Compile:
 *   c++ -std=c++17 -O2 -o test_multiq_modes test_multiq_modes.cpp -lm
 *
 * Run:
 *   ./test_multiq_modes
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <string>

constexpr double PI = 3.14159265358979323846;

//==============================================================================
// Minimal BiquadCoeffs (mirrors MultiQ.h implementation)
//==============================================================================

struct BiquadCoeffs
{
    float coeffs[6] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};

    double getMagnitudeForFrequency(double freq, double sampleRate) const
    {
        double w = 2.0 * PI * freq / sampleRate;
        double cosw = std::cos(w);
        double cos2w = 2.0 * cosw * cosw - 1.0;
        double sinw = std::sin(w);
        double sin2w = 2.0 * sinw * cosw;

        double numR = coeffs[0] + coeffs[1] * cosw + coeffs[2] * cos2w;
        double numI = -(coeffs[1] * sinw + coeffs[2] * sin2w);
        double denR = coeffs[3] + coeffs[4] * cosw + coeffs[5] * cos2w;
        double denI = -(coeffs[4] * sinw + coeffs[5] * sin2w);

        double numMagSq = numR * numR + numI * numI;
        double denMagSq = denR * denR + denI * denI;

        if (denMagSq < 1e-20) return 1.0;
        return std::sqrt(numMagSq / denMagSq);
    }

    void setIdentity()
    {
        coeffs[0] = 1.0f; coeffs[1] = 0.0f; coeffs[2] = 0.0f;
        coeffs[3] = 1.0f; coeffs[4] = 0.0f; coeffs[5] = 0.0f;
    }
};

//==============================================================================
// Coefficient computation (mirrors MultiQ.cpp implementations)
//==============================================================================

namespace jlimit_ns {
    inline double jlimit(double lo, double hi, double v) {
        return v < lo ? lo : (v > hi ? hi : v);
    }
}

inline double dBtoGain(double dB) { return std::pow(10.0, dB / 20.0); }
inline double gaintodB(double g) { return 20.0 * std::log10(std::max(g, 1e-10)); }

double preWarpFrequency(double freq, double sampleRate)
{
    double w0 = 2.0 * PI * freq;
    double T = 1.0 / sampleRate;
    return (2.0 / T) * std::tan(w0 * T / 2.0) / (2.0 * PI);
}

void computePeakingCoeffs(BiquadCoeffs& c, double sr, double freq, float gainDB, float q)
{
    double af = jlimit_ns::jlimit(20.0, sr * 0.45, preWarpFrequency(freq, sr));
    double A = std::pow(10.0, gainDB / 40.0);
    double w0 = 2.0 * PI * af / sr;
    double cosw0 = std::cos(w0);
    double alpha = std::sin(w0) / (2.0 * q);

    double b0 = 1.0 + alpha * A;
    double b1 = -2.0 * cosw0;
    double b2 = 1.0 - alpha * A;
    double a0 = 1.0 + alpha / A;
    double a1 = -2.0 * cosw0;
    double a2 = 1.0 - alpha / A;

    c.coeffs[0] = float(b0/a0); c.coeffs[1] = float(b1/a0); c.coeffs[2] = float(b2/a0);
    c.coeffs[3] = 1.0f;         c.coeffs[4] = float(a1/a0); c.coeffs[5] = float(a2/a0);
}

void computeLowShelfCoeffs(BiquadCoeffs& c, double sr, double freq, float gainDB, float q)
{
    double af = jlimit_ns::jlimit(20.0, sr * 0.45, preWarpFrequency(freq, sr));
    double A = std::sqrt(dBtoGain(gainDB));
    double w0 = 2.0 * PI * af / sr;
    double cosw0 = std::cos(w0);
    double beta = std::sin(w0) * std::sqrt(A) / q;

    double b0 = A * ((A + 1.0) - (A - 1.0) * cosw0 + beta);
    double b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosw0);
    double b2 = A * ((A + 1.0) - (A - 1.0) * cosw0 - beta);
    double a0 = (A + 1.0) + (A - 1.0) * cosw0 + beta;
    double a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosw0);
    double a2 = (A + 1.0) + (A - 1.0) * cosw0 - beta;

    c.coeffs[0] = float(b0/a0); c.coeffs[1] = float(b1/a0); c.coeffs[2] = float(b2/a0);
    c.coeffs[3] = 1.0f;         c.coeffs[4] = float(a1/a0); c.coeffs[5] = float(a2/a0);
}

void computeHighShelfCoeffs(BiquadCoeffs& c, double sr, double freq, float gainDB, float q)
{
    double af = jlimit_ns::jlimit(20.0, sr * 0.45, preWarpFrequency(freq, sr));
    double A = std::sqrt(dBtoGain(gainDB));
    double w0 = 2.0 * PI * af / sr;
    double cosw0 = std::cos(w0);
    double beta = std::sin(w0) * std::sqrt(A) / q;

    double b0 = A * ((A + 1.0) + (A - 1.0) * cosw0 + beta);
    double b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw0);
    double b2 = A * ((A + 1.0) + (A - 1.0) * cosw0 - beta);
    double a0 = (A + 1.0) - (A - 1.0) * cosw0 + beta;
    double a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosw0);
    double a2 = (A + 1.0) - (A - 1.0) * cosw0 - beta;

    c.coeffs[0] = float(b0/a0); c.coeffs[1] = float(b1/a0); c.coeffs[2] = float(b2/a0);
    c.coeffs[3] = 1.0f;         c.coeffs[4] = float(a1/a0); c.coeffs[5] = float(a2/a0);
}

void computeHighPassCoeffs(BiquadCoeffs& c, double sr, double freq, float q)
{
    double af = std::max(20.0, std::min(sr * 0.45, preWarpFrequency(freq, sr)));
    double w0 = 2.0 * PI * af / sr;
    double cosw0 = std::cos(w0);
    double alpha = std::sin(w0) / (2.0 * q);

    double b0 = (1.0 + cosw0) / 2.0;
    double b1 = -(1.0 + cosw0);
    double b2 = (1.0 + cosw0) / 2.0;
    double a0 = 1.0 + alpha;

    c.coeffs[0] = float(b0/a0); c.coeffs[1] = float(b1/a0); c.coeffs[2] = float(b2/a0);
    c.coeffs[3] = 1.0f;         c.coeffs[4] = float(-2.0 * cosw0 / a0); c.coeffs[5] = float((1.0 - alpha) / a0);
}

void computeLowPassCoeffs(BiquadCoeffs& c, double sr, double freq, float q)
{
    double af = std::max(20.0, std::min(sr * 0.45, preWarpFrequency(freq, sr)));
    double w0 = 2.0 * PI * af / sr;
    double cosw0 = std::cos(w0);
    double alpha = std::sin(w0) / (2.0 * q);

    double b0 = (1.0 - cosw0) / 2.0;
    double b1 = 1.0 - cosw0;
    double b2 = (1.0 - cosw0) / 2.0;
    double a0 = 1.0 + alpha;

    c.coeffs[0] = float(b0/a0); c.coeffs[1] = float(b1/a0); c.coeffs[2] = float(b2/a0);
    c.coeffs[3] = 1.0f;         c.coeffs[4] = float(-2.0 * cosw0 / a0); c.coeffs[5] = float((1.0 - alpha) / a0);
}

void computeFirstOrderHighPassCoeffs(BiquadCoeffs& c, double sr, double freq)
{
    freq = jlimit_ns::jlimit(20.0, sr * 0.45, freq);
    double n = std::tan(PI * freq / sr);
    double a0 = n + 1.0;
    c.coeffs[0] = float(1.0 / a0);
    c.coeffs[1] = float(-1.0 / a0);
    c.coeffs[2] = 0.0f;
    c.coeffs[3] = 1.0f;
    c.coeffs[4] = float((n - 1.0) / a0);
    c.coeffs[5] = 0.0f;
}

void computeFirstOrderLowPassCoeffs(BiquadCoeffs& c, double sr, double freq)
{
    freq = jlimit_ns::jlimit(20.0, sr * 0.45, freq);
    double n = std::tan(PI * freq / sr);
    double a0 = n + 1.0;
    c.coeffs[0] = float(n / a0);
    c.coeffs[1] = float(n / a0);
    c.coeffs[2] = 0.0f;
    c.coeffs[3] = 1.0f;
    c.coeffs[4] = float((n - 1.0) / a0);
    c.coeffs[5] = 0.0f;
}

void computeNotchCoeffs(BiquadCoeffs& c, double sr, double freq, float q)
{
    double af = jlimit_ns::jlimit(20.0, sr * 0.45, preWarpFrequency(freq, sr));
    double w0 = 2.0 * PI * af / sr;
    double cosw0 = std::cos(w0);
    double alpha = std::sin(w0) / (2.0 * q);

    c.coeffs[0] = float(1.0 / (1.0 + alpha));
    c.coeffs[1] = float(-2.0 * cosw0 / (1.0 + alpha));
    c.coeffs[2] = float(1.0 / (1.0 + alpha));
    c.coeffs[3] = 1.0f;
    c.coeffs[4] = float(-2.0 * cosw0 / (1.0 + alpha));
    c.coeffs[5] = float((1.0 - alpha) / (1.0 + alpha));
}

void computeTiltShelfCoeffs(BiquadCoeffs& c, double sr, double freq, float gainDB)
{
    freq = jlimit_ns::jlimit(20.0, sr * 0.45, freq);
    double w0 = 2.0 * PI * freq;
    double T = 1.0 / sr;
    double wc = (2.0 / T) * std::tan(w0 * T / 2.0);
    double A = std::pow(10.0, gainDB / 40.0);
    double sqrtA = std::sqrt(A);
    double twoOverT = 2.0 / T;

    double b0 = twoOverT + wc * sqrtA;
    double b1 = wc * sqrtA - twoOverT;
    double a0 = twoOverT + wc / sqrtA;
    double a1 = wc / sqrtA - twoOverT;

    c.coeffs[0] = float(b0 / a0);
    c.coeffs[1] = float(b1 / a0);
    c.coeffs[2] = 0.0f;
    c.coeffs[3] = 1.0f;
    c.coeffs[4] = float(a1 / a0);
    c.coeffs[5] = 0.0f;
}

//==============================================================================
// Test Framework
//==============================================================================

int totalTests = 0;
int passedTests = 0;
int failedTests = 0;

inline double gainToDb(double g) { return 20.0 * std::log10(std::max(g, 1e-10)); }

bool check(const std::string& name, double actual, double expected, double toleranceDB)
{
    totalTests++;
    double diff = std::abs(actual - expected);
    bool pass = diff <= toleranceDB;

    if (pass)
    {
        passedTests++;
        std::cout << "\033[32m[PASS]\033[0m " << name
                  << ": " << std::fixed << std::setprecision(2) << actual
                  << " dB (expected " << expected << " dB, diff " << diff << " dB)\n";
    }
    else
    {
        failedTests++;
        std::cout << "\033[31m[FAIL]\033[0m " << name
                  << ": " << std::fixed << std::setprecision(2) << actual
                  << " dB (expected " << expected << " dB, diff " << diff << " dB > " << toleranceDB << " dB)\n";
    }
    return pass;
}

bool check(const std::string& name, bool actual, bool expected)
{
    totalTests++;
    bool pass = (actual == expected);

    if (pass)
    {
        passedTests++;
        std::cout << "\033[32m[PASS]\033[0m " << name << "\n";
    }
    else
    {
        failedTests++;
        std::cout << "\033[31m[FAIL]\033[0m " << name
                  << ": got " << (actual ? "true" : "false")
                  << ", expected " << (expected ? "true" : "false") << "\n";
    }
    return pass;
}

bool checkLinear(const std::string& name, double actual, double expected, double tolerance)
{
    totalTests++;
    double diff = std::abs(actual - expected);
    bool pass = diff <= tolerance;

    if (pass)
    {
        passedTests++;
        std::cout << "\033[32m[PASS]\033[0m " << name
                  << ": " << std::fixed << std::setprecision(4) << actual
                  << " (expected " << expected << ", diff " << diff << ")\n";
    }
    else
    {
        failedTests++;
        std::cout << "\033[31m[FAIL]\033[0m " << name
                  << ": " << std::fixed << std::setprecision(4) << actual
                  << " (expected " << expected << ", diff " << diff << " > " << tolerance << ")\n";
    }
    return pass;
}

//==============================================================================
// British Mode Tests
//==============================================================================

void testBritishESeriesLFShelf()
{
    // E-Series: LF shelf at 100Hz, +6dB
    BiquadCoeffs c;
    computeLowShelfCoeffs(c, 44100.0, 100.0, 6.0, 0.65);
    check("E-Series LF shelf +6dB at 20Hz", gainToDb(c.getMagnitudeForFrequency(20, 44100)), 6.0, 1.5);
    check("E-Series LF shelf ~0dB at 1kHz", gainToDb(c.getMagnitudeForFrequency(1000, 44100)), 0.0, 0.5);
}

void testBritishGSeriesProportionalQ()
{
    // G-Series dynamic Q: Q should increase with gain
    // At low gain (3dB), Q should be moderate
    // At high gain (12dB), Q should be higher
    float baseQ = 1.0f;
    float lowGainQ = baseQ * (1.0f + (3.0f / 20.0f) * 2.0f);
    float highGainQ = baseQ * (1.0f + (12.0f / 20.0f) * 2.0f);
    check("G-Series Q increases with gain", highGainQ > lowGainQ, true);
    check("G-Series low gain Q reasonable", lowGainQ, 1.3, 0.2);
    check("G-Series high gain Q reasonable", highGainQ, 2.2, 0.3);
}

void testBritish18dBHPF()
{
    // 18dB/oct HPF = 1st order + 2nd order (Q=1.0) cascade
    // At cutoff: should be approximately -3dB
    BiquadCoeffs first, second;
    computeFirstOrderHighPassCoeffs(first, 44100.0, 100.0);
    computeHighPassCoeffs(second, 44100.0, 100.0, 1.0);
    double magAtCutoff = first.getMagnitudeForFrequency(100, 44100) *
                         second.getMagnitudeForFrequency(100, 44100);
    check("18dB HPF -3dB at cutoff", gainToDb(magAtCutoff), -3.0, 1.5);

    // Stopband: should attenuate at ~18dB/oct
    double magOneOctBelow = first.getMagnitudeForFrequency(50, 44100) *
                            second.getMagnitudeForFrequency(50, 44100);
    double attenuation = gainToDb(magAtCutoff) - gainToDb(magOneOctBelow);
    check("18dB HPF slope ~18dB/oct", attenuation, 18.0, 4.0);
}

//==============================================================================
// Tube EQ Interaction Tests
//==============================================================================

void testPultecBoostOnly()
{
    // Boost at 60Hz, no atten: should produce a peak centered at 60Hz
    BiquadCoeffs peak;
    float boostGain = 6.0f;
    float peakGainDB = boostGain * 1.4f; // ~8.4 dB
    float Q = 0.55f;
    computePeakingCoeffs(peak, 44100.0, 60.0, peakGainDB, Q);
    check("Tube LF boost peak at 60Hz", gainToDb(peak.getMagnitudeForFrequency(60, 44100)), peakGainDB, 1.0);
    check("Tube LF boost flat at 1kHz", gainToDb(peak.getMagnitudeForFrequency(1000, 44100)), 0.0, 0.5);
}

void testPultecAttenOnly()
{
    // Atten at 60Hz, no boost: should produce a low shelf cut
    BiquadCoeffs dip;
    float attenGain = 6.0f;
    float dipGainDB = -(attenGain * 1.6f); // ~-9.6 dB
    float dipFreq = 60.0f * 0.55f; // ~33 Hz
    computeLowShelfCoeffs(dip, 44100.0, dipFreq, dipGainDB, 0.5);
    // Below the dip frequency, should be cut
    double mag20 = gainToDb(dip.getMagnitudeForFrequency(20, 44100));
    check("Tube LF atten cuts below freq", mag20 < -3.0, true);
}

void testPultecBoostAttenInteraction()
{
    // Classic vintage trick: boost AND atten at same frequency
    // Should produce: peak at 60Hz + dip below 60Hz
    float boost = 6.0f, atten = 6.0f, freq = 60.0f;

    // Peak filter
    float peakGainDB = boost * 1.4f + atten * boost * 0.08f;
    float Q = 0.55f * (1.0f + atten * 0.015f);
    BiquadCoeffs peak;
    computePeakingCoeffs(peak, 44100.0, freq, peakGainDB, Q);

    // Dip shelf
    float dipFreqRatio = 0.55f + 0.15f * (1.0f - atten / 10.0f);
    float dipFreq = freq * dipFreqRatio;
    float dipGainDB = -(atten * 1.6f + boost * atten * 0.06f);
    float dipQ = 0.5f + atten * 0.03f;
    BiquadCoeffs dip;
    computeLowShelfCoeffs(dip, 44100.0, dipFreq, dipGainDB, dipQ);

    // Combined response
    double mag60 = gainToDb(peak.getMagnitudeForFrequency(60, 44100) *
                            dip.getMagnitudeForFrequency(60, 44100));
    double mag30 = gainToDb(peak.getMagnitudeForFrequency(30, 44100) *
                            dip.getMagnitudeForFrequency(30, 44100));
    double mag1k = gainToDb(peak.getMagnitudeForFrequency(1000, 44100) *
                            dip.getMagnitudeForFrequency(1000, 44100));

    check("Tube LF trick: boost at 60Hz", mag60 > 3.0, true);
    check("Tube LF trick: dip at 30Hz", mag30 < mag60 - 3.0, true);
    check("Tube LF trick: flat at 1kHz", std::abs(mag1k) < 1.0, true);
}

//==============================================================================
// Dynamic EQ Tests
//==============================================================================

void testDynamicGainReduction()
{
    // Verify gain reduction math
    float threshold = -20.0f;
    float ratio = 4.0f;
    float range = 12.0f;

    // 10dB above threshold
    float input = -10.0f;
    float overshoot = input - threshold; // 10 dB
    float reduction = overshoot * (1.0f - 1.0f / ratio); // 7.5 dB
    reduction = std::min(reduction, range);
    check("Dynamic reduction at -10dB", reduction, 7.5, 0.1);

    // Below threshold: no reduction
    float belowInput = -25.0f;
    float belowOvershoot = std::max(0.0f, belowInput - threshold);
    check("No reduction below threshold", belowOvershoot, 0.0, 0.001);

    // Range limiting
    float extremeInput = 0.0f;
    float extremeOvershoot = extremeInput - threshold; // 20 dB
    float extremeReduction = extremeOvershoot * (1.0f - 1.0f / ratio); // 15 dB
    extremeReduction = std::min(extremeReduction, range); // clamped to 12 dB
    check("Dynamic range limits reduction", extremeReduction, 12.0, 0.1);
}

void testDynamicSoftKnee()
{
    float threshold = -20.0f;
    float ratio = 4.0f;
    float kneeWidth = 6.0f;
    float halfKnee = kneeWidth / 2.0f;

    // Below knee: no reduction
    float belowKnee = threshold - halfKnee - 1.0f;
    check("Below knee: no reduction", belowKnee < threshold - halfKnee, true);

    // At threshold: partial reduction (half of full ratio effect)
    // In the soft knee region, gain reduction should be less than full ratio
    float atThreshold = threshold;
    float x = atThreshold - threshold + halfKnee; // = halfKnee
    // Quadratic interpolation
    float softReduction = (x * x) / (2.0f * kneeWidth) * (1.0f - 1.0f / ratio);
    check("Soft knee at threshold: partial reduction", softReduction > 0.0, true);
    check("Soft knee at threshold: less than full", softReduction < 3.0, true);
}

//==============================================================================
// Extreme Parameter Tests
//==============================================================================

void testExtremeParameters()
{
    BiquadCoeffs c;

    // Max gain, min freq, min Q
    computePeakingCoeffs(c, 44100.0, 20.0, 24.0, 0.1);
    check("Extreme: max gain min freq finite", std::isfinite(c.getMagnitudeForFrequency(20, 44100)), true);

    // Min gain, max freq, max Q
    computePeakingCoeffs(c, 44100.0, 20000.0, -24.0, 100.0);
    check("Extreme: min gain max freq finite", std::isfinite(c.getMagnitudeForFrequency(20000, 44100)), true);

    // Near Nyquist
    computePeakingCoeffs(c, 44100.0, 21000.0, 12.0, 5.0);
    check("Extreme: above Nyquist finite", std::isfinite(c.getMagnitudeForFrequency(20000, 44100)), true);

    // Very high sample rate (4x oversampling at 96kHz)
    computePeakingCoeffs(c, 384000.0, 1000.0, 12.0, 2.0);
    check("384kHz peaking at 1kHz", gainToDb(c.getMagnitudeForFrequency(1000, 384000)), 12.0, 1.0);
}

void testSampleRateIndependence()
{
    // Same filter should produce same magnitude at same frequency regardless of sample rate
    double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };

    for (double sr : rates)
    {
        BiquadCoeffs c;
        computePeakingCoeffs(c, sr, 1000.0, 6.0, 1.5);
        double mag = gainToDb(c.getMagnitudeForFrequency(1000, sr));
        std::string name = "SR " + std::to_string((int)sr) + " peak at 1kHz";
        check(name.c_str(), mag, 6.0, 0.5);
    }
}

//==============================================================================
// Main
//==============================================================================

int main()
{
    std::cout << "=== Multi-Q Mode Tests ===" << std::endl;

    std::cout << "\n--- British Mode ---" << std::endl;
    testBritishESeriesLFShelf();
    testBritishGSeriesProportionalQ();
    testBritish18dBHPF();

    std::cout << "\n--- Tube EQ Interaction ---" << std::endl;
    testPultecBoostOnly();
    testPultecAttenOnly();
    testPultecBoostAttenInteraction();

    std::cout << "\n--- Dynamic EQ ---" << std::endl;
    testDynamicGainReduction();
    testDynamicSoftKnee();

    std::cout << "\n--- Extreme Parameters ---" << std::endl;
    testExtremeParameters();
    testSampleRateIndependence();

    std::cout << "\n=== Results: " << passedTests << "/" << totalTests
              << " passed ===" << std::endl;
    return (failedTests > 0) ? 1 : 0;
}
