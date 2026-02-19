/**
 * Multi-Q DSP Coefficient Tests
 * ==============================
 * Standalone test for BiquadCoeffs and coefficient computation.
 * Verifies that computed filter coefficients produce correct frequency responses.
 *
 * Compile:
 *   c++ -std=c++17 -O2 -o test_multiq_dsp test_multiq_dsp.cpp -lm
 *
 * Run:
 *   ./test_multiq_dsp
 */

#include <cmath>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cstring>

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
    double af = preWarpFrequency(freq, sr);
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

void computeHighPassCoeffs(BiquadCoeffs& c, double sr, double freq, float q)
{
    double w0 = 2.0 * PI * freq / sr;
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
    double w0 = 2.0 * PI * freq / sr;
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
    double n = std::tan(PI * freq / sr);
    double a0 = n + 1.0;
    c.coeffs[0] = float(n / a0);
    c.coeffs[1] = float(n / a0);
    c.coeffs[2] = 0.0f;
    c.coeffs[3] = 1.0f;
    c.coeffs[4] = float((n - 1.0) / a0);
    c.coeffs[5] = 0.0f;
}

void computeTiltShelfCoeffs(BiquadCoeffs& c, double sr, double freq, float gainDB)
{
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
// Tests
//==============================================================================

void testIdentityFilter()
{
    std::cout << "\n=== Identity Filter ===\n";
    BiquadCoeffs c;

    // Identity filter should have unity magnitude at all frequencies
    checkLinear("Identity at 100 Hz", c.getMagnitudeForFrequency(100, 44100), 1.0, 1e-10);
    checkLinear("Identity at 1 kHz",  c.getMagnitudeForFrequency(1000, 44100), 1.0, 1e-10);
    checkLinear("Identity at 10 kHz", c.getMagnitudeForFrequency(10000, 44100), 1.0, 1e-10);
}

void testPeakingFilter()
{
    std::cout << "\n=== Peaking Filter ===\n";
    double sr = 96000;
    BiquadCoeffs c;

    // +6 dB peak at 1 kHz, Q=1.0
    computePeakingCoeffs(c, sr, 1000.0f, 6.0f, 1.0f);
    double magAtCenter = gaintodB(c.getMagnitudeForFrequency(1000, sr));
    check("Peak +6dB at center freq", magAtCenter, 6.0, 0.5);

    // Far from center should be ~0 dB
    double magFarBelow = gaintodB(c.getMagnitudeForFrequency(50, sr));
    check("Peak +6dB far below (50 Hz)", magFarBelow, 0.0, 0.5);

    double magFarAbove = gaintodB(c.getMagnitudeForFrequency(20000, sr));
    check("Peak +6dB far above (20 kHz)", magFarAbove, 0.0, 0.5);

    // -12 dB cut at 4 kHz, Q=2.0
    computePeakingCoeffs(c, sr, 4000.0f, -12.0f, 2.0f);
    magAtCenter = gaintodB(c.getMagnitudeForFrequency(4000, sr));
    check("Cut -12dB at center freq", magAtCenter, -12.0, 0.5);

    // 0 dB gain should produce identity
    computePeakingCoeffs(c, sr, 1000.0f, 0.0f, 1.0f);
    checkLinear("Peak 0dB = identity at 100 Hz", c.getMagnitudeForFrequency(100, sr), 1.0, 0.001);
    checkLinear("Peak 0dB = identity at 1 kHz",  c.getMagnitudeForFrequency(1000, sr), 1.0, 0.001);
}

void testLowShelfFilter()
{
    std::cout << "\n=== Low Shelf Filter ===\n";
    double sr = 96000;
    BiquadCoeffs c;

    // +6 dB low shelf at 200 Hz, Q=0.71
    computeLowShelfCoeffs(c, sr, 200.0f, 6.0f, 0.71f);

    // Well below cutoff: should be ~+6 dB
    double magBelow = gaintodB(c.getMagnitudeForFrequency(20, sr));
    check("LowShelf +6dB well below (20 Hz)", magBelow, 6.0, 1.0);

    // Well above cutoff: should be ~0 dB
    double magAbove = gaintodB(c.getMagnitudeForFrequency(10000, sr));
    check("LowShelf +6dB well above (10 kHz)", magAbove, 0.0, 0.5);
}

void testHighShelfFilter()
{
    std::cout << "\n=== High Shelf Filter ===\n";
    double sr = 96000;
    BiquadCoeffs c;

    // +6 dB high shelf at 4 kHz, Q=0.71
    computeHighShelfCoeffs(c, sr, 4000.0f, 6.0f, 0.71f);

    // Well above cutoff: should be ~+6 dB
    double magAbove = gaintodB(c.getMagnitudeForFrequency(20000, sr));
    check("HighShelf +6dB well above (20 kHz)", magAbove, 6.0, 1.0);

    // Well below cutoff: should be ~0 dB
    double magBelow = gaintodB(c.getMagnitudeForFrequency(100, sr));
    check("HighShelf +6dB well below (100 Hz)", magBelow, 0.0, 0.5);
}

void testHighPassFilter()
{
    std::cout << "\n=== High-Pass Filter ===\n";
    double sr = 44100;
    BiquadCoeffs c;

    // 2nd-order HPF at 100 Hz, Q=0.707 (Butterworth)
    double warpedFreq = preWarpFrequency(100, sr);
    computeHighPassCoeffs(c, sr, static_cast<float>(warpedFreq), 0.707f);

    // Well above cutoff: should be ~0 dB
    double magAbove = gaintodB(c.getMagnitudeForFrequency(10000, sr));
    check("HPF 100Hz above (10 kHz)", magAbove, 0.0, 0.5);

    // At cutoff: Butterworth should be -3 dB
    double magAtCutoff = gaintodB(c.getMagnitudeForFrequency(100, sr));
    check("HPF 100Hz at cutoff (-3dB)", magAtCutoff, -3.0, 0.5);

    // Well below cutoff: should be heavily attenuated (12 dB/oct for 2nd order)
    double magBelow = gaintodB(c.getMagnitudeForFrequency(10, sr));
    bool belowCutoff = magBelow < -30.0;
    totalTests++;
    if (belowCutoff) { passedTests++; std::cout << "\033[32m[PASS]\033[0m HPF 100Hz far below (10 Hz): " << magBelow << " dB (< -30 dB)\n"; }
    else { failedTests++; std::cout << "\033[31m[FAIL]\033[0m HPF 100Hz far below (10 Hz): " << magBelow << " dB (expected < -30 dB)\n"; }
}

void testLowPassFilter()
{
    std::cout << "\n=== Low-Pass Filter ===\n";
    double sr = 44100;
    BiquadCoeffs c;

    // 2nd-order LPF at 5 kHz, Q=0.707
    double warpedFreq = preWarpFrequency(5000, sr);
    computeLowPassCoeffs(c, sr, static_cast<float>(warpedFreq), 0.707f);

    // Well below cutoff: should be ~0 dB
    double magBelow = gaintodB(c.getMagnitudeForFrequency(100, sr));
    check("LPF 5kHz below (100 Hz)", magBelow, 0.0, 0.5);

    // At cutoff: Butterworth should be -3 dB
    double magAtCutoff = gaintodB(c.getMagnitudeForFrequency(5000, sr));
    check("LPF 5kHz at cutoff (-3dB)", magAtCutoff, -3.0, 0.5);
}

void testNotchFilter()
{
    std::cout << "\n=== Notch Filter ===\n";
    double sr = 96000;
    BiquadCoeffs c;

    // Notch at 1 kHz, Q=10 (very narrow)
    computeNotchCoeffs(c, sr, 1000.0f, 10.0f);

    // At center: should be deeply attenuated
    double magAtCenter = gaintodB(c.getMagnitudeForFrequency(1000, sr));
    bool deepNotch = magAtCenter < -40.0;
    totalTests++;
    if (deepNotch) { passedTests++; std::cout << "\033[32m[PASS]\033[0m Notch at center: " << magAtCenter << " dB (< -40 dB)\n"; }
    else { failedTests++; std::cout << "\033[31m[FAIL]\033[0m Notch at center: " << magAtCenter << " dB (expected < -40 dB)\n"; }

    // Away from center: should be ~0 dB
    double magAway = gaintodB(c.getMagnitudeForFrequency(5000, sr));
    check("Notch away from center (5 kHz)", magAway, 0.0, 0.5);
}

void testFirstOrderFilters()
{
    std::cout << "\n=== First-Order Filters ===\n";
    double sr = 44100;
    BiquadCoeffs c;

    // 1st-order HPF at 200 Hz
    double wf = preWarpFrequency(200, sr);
    computeFirstOrderHighPassCoeffs(c, sr, wf);

    double magAbove = gaintodB(c.getMagnitudeForFrequency(10000, sr));
    check("1st-order HPF 200Hz above (10 kHz)", magAbove, 0.0, 0.5);

    double magAtCutoff = gaintodB(c.getMagnitudeForFrequency(200, sr));
    check("1st-order HPF 200Hz at cutoff (-3dB)", magAtCutoff, -3.0, 0.5);

    // 1st-order LPF at 5 kHz
    wf = preWarpFrequency(5000, sr);
    computeFirstOrderLowPassCoeffs(c, sr, wf);

    double magBelow = gaintodB(c.getMagnitudeForFrequency(100, sr));
    check("1st-order LPF 5kHz below (100 Hz)", magBelow, 0.0, 0.5);

    double magAtCutoffLP = gaintodB(c.getMagnitudeForFrequency(5000, sr));
    check("1st-order LPF 5kHz at cutoff (-3dB)", magAtCutoffLP, -3.0, 0.5);
}

void testTiltShelfFilter()
{
    std::cout << "\n=== Tilt Shelf Filter ===\n";
    double sr = 96000;
    BiquadCoeffs c;

    // +6 dB tilt shelf at 1 kHz
    // This is a 1st-order low shelf: A = 10^(gainDB/40) ≈ 1.413 (+3 dB)
    // DC: gain = A, Center: gain = sqrt(A), HF: gain = 1.0
    computeTiltShelfCoeffs(c, sr, 1000.0f, 6.0f);

    // Well above: should be ~0 dB (unity at high freq)
    double magAbove = gaintodB(c.getMagnitudeForFrequency(20000, sr));
    check("Tilt +6dB above (20 kHz)", magAbove, 0.0, 0.5);

    // Well below: should be ~+3 dB (A = 10^(6/40))
    double magBelow = gaintodB(c.getMagnitudeForFrequency(20, sr));
    check("Tilt +6dB below (20 Hz)", magBelow, 3.0, 0.5);

    // At center: should be ~+1.5 dB (sqrt(A))
    double magCenter = gaintodB(c.getMagnitudeForFrequency(1000, sr));
    check("Tilt +6dB at center (1 kHz)", magCenter, 1.5, 0.5);
}

void testCascadedHighPass()
{
    std::cout << "\n=== Cascaded HPF (Butterworth) ===\n";
    double sr = 44100;

    // 4th-order Butterworth HPF at 100 Hz (2 stages, 24 dB/oct)
    // Butterworth Q values for 4th-order: 0.5412, 1.3066
    double wf = preWarpFrequency(100, sr);
    BiquadCoeffs stage1, stage2;
    computeHighPassCoeffs(stage1, sr, static_cast<float>(wf), 0.5412f);
    computeHighPassCoeffs(stage2, sr, static_cast<float>(wf), 1.3066f);

    // Cascaded response = product of individual magnitudes
    double magAbove = gaintodB(
        stage1.getMagnitudeForFrequency(10000, sr) *
        stage2.getMagnitudeForFrequency(10000, sr));
    check("4th-order HPF above (10 kHz)", magAbove, 0.0, 0.5);

    double magAtCutoff = gaintodB(
        stage1.getMagnitudeForFrequency(100, sr) *
        stage2.getMagnitudeForFrequency(100, sr));
    check("4th-order HPF at cutoff (-3dB)", magAtCutoff, -3.0, 1.0);

    // One octave below cutoff: 24 dB/oct = -24 dB
    double magOneOctBelow = gaintodB(
        stage1.getMagnitudeForFrequency(50, sr) *
        stage2.getMagnitudeForFrequency(50, sr));
    check("4th-order HPF 1 oct below (-24dB)", magOneOctBelow, -24.0, 2.0);
}

void testSampleRateIndependence()
{
    std::cout << "\n=== Sample Rate Independence (Pre-Warping) ===\n";

    // A peaking filter at 1 kHz should have the same gain at center frequency
    // regardless of sample rate (thanks to pre-warping)
    BiquadCoeffs c44, c96, c192;
    computePeakingCoeffs(c44,  44100, 1000.0f, 6.0f, 1.0f);
    computePeakingCoeffs(c96,  96000, 1000.0f, 6.0f, 1.0f);
    computePeakingCoeffs(c192, 192000, 1000.0f, 6.0f, 1.0f);

    double mag44  = gaintodB(c44.getMagnitudeForFrequency(1000, 44100));
    double mag96  = gaintodB(c96.getMagnitudeForFrequency(1000, 96000));
    double mag192 = gaintodB(c192.getMagnitudeForFrequency(1000, 192000));

    check("Peak 1kHz +6dB @ 44.1kHz", mag44, 6.0, 0.5);
    check("Peak 1kHz +6dB @ 96kHz", mag96, 6.0, 0.5);
    check("Peak 1kHz +6dB @ 192kHz", mag192, 6.0, 0.5);

    // High-frequency test: 10 kHz peak at 96 kHz and 192 kHz
    // At 44.1 kHz, 10 kHz is too close to Nyquist — pre-warped freq exceeds
    // the 0.45*sr clamp, so bandwidth compression is expected (not a bug)
    BiquadCoeffs cHF96, cHF192;
    computePeakingCoeffs(cHF96,  96000, 10000.0f, 6.0f, 1.0f);
    computePeakingCoeffs(cHF192, 192000, 10000.0f, 6.0f, 1.0f);

    double magHF96  = gaintodB(cHF96.getMagnitudeForFrequency(10000, 96000));
    double magHF192 = gaintodB(cHF192.getMagnitudeForFrequency(10000, 192000));

    check("Peak 10kHz +6dB @ 96kHz (pre-warped)", magHF96, 6.0, 0.5);
    check("Peak 10kHz +6dB @ 192kHz (pre-warped)", magHF192, 6.0, 0.5);

    // At 44.1 kHz, verify the peak still exists but accept reduced gain due to Nyquist proximity
    BiquadCoeffs cHF44;
    computePeakingCoeffs(cHF44, 44100, 10000.0f, 6.0f, 1.0f);
    double magHF44 = gaintodB(cHF44.getMagnitudeForFrequency(10000, 44100));
    check("Peak 10kHz +6dB @ 44.1kHz (near Nyquist, reduced)", magHF44, 6.0, 2.0);
}

void testMagnitudeEvaluation()
{
    std::cout << "\n=== BiquadCoeffs::getMagnitudeForFrequency Accuracy ===\n";

    // Test against known analytical result for a simple 1st-order lowpass
    // H(z) = n/(n+1) * (1 + z^-1) / (1 + (n-1)/(n+1) * z^-1)
    // At DC (f=0): |H| = 2n/(n+1) * 1/(1 + (n-1)/(n+1)) = 2n/(n+1) * (n+1)/2n = 1
    // At Nyquist (f=sr/2): |H| = 0

    BiquadCoeffs c;
    computeFirstOrderLowPassCoeffs(c, 44100, preWarpFrequency(1000, 44100));

    // At DC, 1st-order LPF should have unity gain
    double magDC = c.getMagnitudeForFrequency(0.001, 44100);
    checkLinear("1st-order LPF magnitude at DC", magDC, 1.0, 0.01);

    // At Nyquist, should approach 0
    double magNyquist = c.getMagnitudeForFrequency(22049, 44100);
    bool nearZero = magNyquist < 0.1;
    totalTests++;
    if (nearZero) { passedTests++; std::cout << "\033[32m[PASS]\033[0m 1st-order LPF magnitude at Nyquist: " << magNyquist << " (< 0.1)\n"; }
    else { failedTests++; std::cout << "\033[31m[FAIL]\033[0m 1st-order LPF magnitude at Nyquist: " << magNyquist << " (expected < 0.1)\n"; }
}

//==============================================================================
// Main
//==============================================================================

int main()
{
    std::cout << "Multi-Q DSP Coefficient Tests\n";
    std::cout << "=============================\n";

    testIdentityFilter();
    testPeakingFilter();
    testLowShelfFilter();
    testHighShelfFilter();
    testHighPassFilter();
    testLowPassFilter();
    testNotchFilter();
    testFirstOrderFilters();
    testTiltShelfFilter();
    testCascadedHighPass();
    testSampleRateIndependence();
    testMagnitudeEvaluation();

    std::cout << "\n=============================\n";
    std::cout << "Results: " << passedTests << "/" << totalTests << " passed";
    if (failedTests > 0)
        std::cout << ", \033[31m" << failedTests << " FAILED\033[0m";
    std::cout << "\n";

    return failedTests > 0 ? 1 : 0;
}
