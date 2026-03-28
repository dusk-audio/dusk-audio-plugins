#pragma once
#include <cmath>
#include <algorithm>  // std::max, std::min

// detail::kPi is a POSIX extension not guaranteed by the C++ standard.
// Use a local constant for portability (MSVC, strict std modes).
namespace AnalogMatchedBiquad { namespace detail {
    static constexpr double kPi = 3.14159265358979323846;
} }

//==============================================================================
/**
    Analog-matched biquad coefficient design — cramping-free at all sample rates.

    The standard bilinear transform (BLT/RBJ cookbook) pre-warps only the center
    frequency, leaving the Q/bandwidth shape cramped near Nyquist: filters get
    narrower on the high side because frequencies compress toward π.

    This module uses two complementary techniques:

    1. **Peaking/Notch/BandPass**: Pre-warp the BANDWIDTH (not just f0) via
       `kbw = tan(π·bw/sr)`. The center frequency is placed with `cos(2π·fc/sr)`
       which gives exact gain at fc in the digital domain. Result: correct Hz
       bandwidth at all frequencies, no oversampling needed.
       - H(DC) = 0 dB ✓
       - H(fc) = gainDB ✓
       - H(Nyquist) = 0 dB ✓
       - bandwidth = fc/Q Hz (pre-warped, does not compress near Nyquist)

    2. **Shelves/HP/LP**: Use `k = tan(π·fc/sr)` (bilinear transform at fc), which
       places the -3dB turnover at exactly fc. The transition shape is better than
       the current preWarpFrequency() + RBJ approach which warps the entire shelf.

    CPU impact: negligible — a few extra trig ops at parameter-change time only.

    This header intentionally does NOT include MultiQ.h to avoid circular includes.
    Include it after BiquadCoeffs is defined (i.e., after MultiQ.h).

    References:
    - S. J. Orfanidis, JAES 53(11), 2005 (bandwidth-matched EQ design)
    - R. Bristow-Johnson, "Audio EQ Cookbook" (shelf/HP/LP formulas)
*/

namespace AnalogMatchedBiquad
{

static inline double clampFreq(double fc, double sr)
{
    return std::max(1.0, std::min(fc, sr * 0.4998));
}

//------------------------------------------------------------------------------
/**
    Peaking (parametric) filter with pre-warped bandwidth.

    Unlike RBJ peaking (which warps the Q in the digital domain, cramping the
    high-frequency skirt), this pre-warps the BANDWIDTH independently:
        kbw = tan(π · fc/Q / sr)
    giving the same -3dB bandwidth in Hz at all sample rates.

    @param c      Output BiquadCoeffs — must already exist (e.g. BiquadCoeffs c)
    @param fc     Centre frequency Hz
    @param sr     Sample rate Hz
    @param gainDB Gain in dB (+/-)
    @param Q      Quality factor (bandwidth = fc/Q Hz)
*/
static void computePeaking(BiquadCoeffs& c, double fc, double sr,
                            double gainDB, double Q)
{
    fc = clampFreq(fc, sr);
    Q  = std::max(0.01, Q);

    if (std::abs(gainDB) < 0.01)
    {
        c.setIdentity();
        return;
    }

    const double W0d  = 2.0 * detail::kPi * fc / sr;
    const double bw   = fc / Q;
    const double kbw  = std::tan(detail::kPi * std::min(bw, sr * 0.4998) / sr);
    const double A    = std::pow(10.0, gainDB / 40.0);  // amplitude (sqrt of power gain)
    const double cosW = std::cos(W0d);

    // A = 10^(gainDB/40) < 1 for cuts, > 1 for boosts.
    // H(fc) = A² = 10^(gainDB/20) for both signs — no branch needed.
    const double b0 = 1.0 + kbw * A,  b2 = 1.0 - kbw * A;
    const double a0 = 1.0 + kbw / A,  a2 = 1.0 - kbw / A;
    const double b1 = -2.0 * cosW;
    const double a1 = -2.0 * cosW;

    c.coeffs[0] = static_cast<float>(b0 / a0);
    c.coeffs[1] = static_cast<float>(b1 / a0);
    c.coeffs[2] = static_cast<float>(b2 / a0);
    c.coeffs[3] = 1.0f;
    c.coeffs[4] = static_cast<float>(a1 / a0);
    c.coeffs[5] = static_cast<float>(a2 / a0);
}

//------------------------------------------------------------------------------
/**
    Low shelf with pre-warped turnover frequency.
    Uses k = tan(π·fc/sr) so the -3dB turnover appears at exactly fc.
*/
static void computeLowShelf(BiquadCoeffs& c, double fc, double sr,
                              double gainDB, double Q)
{
    fc = clampFreq(fc, sr);
    Q  = std::max(0.01, Q);

    if (std::abs(gainDB) < 0.01) { c.setIdentity(); return; }

    const double A    = std::pow(10.0, gainDB / 40.0);
    const double k    = std::tan(detail::kPi * fc / sr);   // pre-warped at fc
    const double k2   = k * k;
    const double sqA  = std::sqrt(A);
    // Derive cosW/sinW from pre-warped k rather than digital fc to avoid double-warping
    const double cosW = (1.0 - k2) / (1.0 + k2);
    const double sinW = 2.0 * k / (1.0 + k2);
    const double alpha = sinW / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / Q - 1.0) + 2.0);

    const double b0 =  A * ((A + 1.0) - (A - 1.0) * cosW + 2.0 * sqA * alpha);
    const double b1 =  2.0 * A * ((A - 1.0) - (A + 1.0) * cosW);
    const double b2 =  A * ((A + 1.0) - (A - 1.0) * cosW - 2.0 * sqA * alpha);
    const double a0 = (A + 1.0) + (A - 1.0) * cosW + 2.0 * sqA * alpha;
    const double a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosW);
    const double a2 = (A + 1.0) + (A - 1.0) * cosW - 2.0 * sqA * alpha;

    c.coeffs[0] = static_cast<float>(b0 / a0);
    c.coeffs[1] = static_cast<float>(b1 / a0);
    c.coeffs[2] = static_cast<float>(b2 / a0);
    c.coeffs[3] = 1.0f;
    c.coeffs[4] = static_cast<float>(a1 / a0);
    c.coeffs[5] = static_cast<float>(a2 / a0);
}

//------------------------------------------------------------------------------
/**
    High shelf with pre-warped turnover frequency.
*/
static void computeHighShelf(BiquadCoeffs& c, double fc, double sr,
                               double gainDB, double Q)
{
    fc = clampFreq(fc, sr);
    Q  = std::max(0.01, Q);

    if (std::abs(gainDB) < 0.01) { c.setIdentity(); return; }

    const double A    = std::pow(10.0, gainDB / 40.0);
    const double sqA  = std::sqrt(A);
    const double k    = std::tan(detail::kPi * fc / sr);
    const double k2   = k * k;
    const double cosW = (1.0 - k2) / (1.0 + k2);
    const double sinW = 2.0 * k / (1.0 + k2);
    const double alpha = sinW / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / Q - 1.0) + 2.0);

    const double b0 =  A * ((A + 1.0) + (A - 1.0) * cosW + 2.0 * sqA * alpha);
    const double b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosW);
    const double b2 =  A * ((A + 1.0) + (A - 1.0) * cosW - 2.0 * sqA * alpha);
    const double a0 = (A + 1.0) - (A - 1.0) * cosW + 2.0 * sqA * alpha;
    const double a1 =  2.0 * ((A - 1.0) - (A + 1.0) * cosW);
    const double a2 = (A + 1.0) - (A - 1.0) * cosW - 2.0 * sqA * alpha;

    c.coeffs[0] = static_cast<float>(b0 / a0);
    c.coeffs[1] = static_cast<float>(b1 / a0);
    c.coeffs[2] = static_cast<float>(b2 / a0);
    c.coeffs[3] = 1.0f;
    c.coeffs[4] = static_cast<float>(a1 / a0);
    c.coeffs[5] = static_cast<float>(a2 / a0);
}

//------------------------------------------------------------------------------
/** 2nd-order high-pass, pre-warped at fc. k = tan(π·fc/sr). */
static void computeHighPass(BiquadCoeffs& c, double fc, double sr, double Q)
{
    fc = clampFreq(fc, sr);
    Q  = std::max(0.01, Q);
    const double k    = std::tan(detail::kPi * fc / sr);
    const double norm = 1.0 / (k * k + k / Q + 1.0);

    c.coeffs[0] = static_cast<float>(norm);
    c.coeffs[1] = static_cast<float>(-2.0 * norm);
    c.coeffs[2] = static_cast<float>(norm);
    c.coeffs[3] = 1.0f;
    c.coeffs[4] = static_cast<float>(2.0 * (k * k - 1.0) * norm);
    c.coeffs[5] = static_cast<float>((k * k - k / Q + 1.0) * norm);
}

//------------------------------------------------------------------------------
/** 2nd-order low-pass, pre-warped at fc. */
static void computeLowPass(BiquadCoeffs& c, double fc, double sr, double Q)
{
    fc = clampFreq(fc, sr);
    Q  = std::max(0.01, Q);
    const double k    = std::tan(detail::kPi * fc / sr);
    const double k2   = k * k;
    const double norm = 1.0 / (k2 + k / Q + 1.0);

    c.coeffs[0] = static_cast<float>(k2 * norm);
    c.coeffs[1] = static_cast<float>(2.0 * k2 * norm);
    c.coeffs[2] = static_cast<float>(k2 * norm);
    c.coeffs[3] = 1.0f;
    c.coeffs[4] = static_cast<float>(2.0 * (k2 - 1.0) * norm);
    c.coeffs[5] = static_cast<float>((k2 - k / Q + 1.0) * norm);
}

//------------------------------------------------------------------------------
/** 1st-order high-pass, pre-warped at fc. */
static void computeFirstOrderHighPass(BiquadCoeffs& c, double fc, double sr)
{
    fc = clampFreq(fc, sr);
    const double k    = std::tan(detail::kPi * fc / sr);
    const double norm = 1.0 / (1.0 + k);

    c.coeffs[0] = static_cast<float>(norm);
    c.coeffs[1] = static_cast<float>(-norm);
    c.coeffs[2] = 0.0f;
    c.coeffs[3] = 1.0f;
    c.coeffs[4] = static_cast<float>((k - 1.0) * norm);
    c.coeffs[5] = 0.0f;
}

//------------------------------------------------------------------------------
/** 1st-order low-pass, pre-warped at fc. */
static void computeFirstOrderLowPass(BiquadCoeffs& c, double fc, double sr)
{
    fc = clampFreq(fc, sr);
    const double k    = std::tan(detail::kPi * fc / sr);
    const double norm = 1.0 / (1.0 + k);

    c.coeffs[0] = static_cast<float>(k * norm);
    c.coeffs[1] = static_cast<float>(k * norm);
    c.coeffs[2] = 0.0f;
    c.coeffs[3] = 1.0f;
    c.coeffs[4] = static_cast<float>((k - 1.0) * norm);
    c.coeffs[5] = 0.0f;
}

//------------------------------------------------------------------------------
/**
    Notch filter with pre-warped center and bandwidth.
    The notch null appears at exactly fc; width is set by the pre-warped bandwidth.
*/
static void computeNotch(BiquadCoeffs& c, double fc, double sr, double Q)
{
    fc = clampFreq(fc, sr);
    Q  = std::max(0.01, Q);
    const double W0d  = 2.0 * detail::kPi * fc / sr;
    const double cosW = std::cos(W0d);
    const double kbw  = std::tan(detail::kPi * std::min(fc / Q, sr * 0.4998) / sr);
    const double norm = 1.0 / (1.0 + kbw);

    c.coeffs[0] = static_cast<float>(norm);
    c.coeffs[1] = static_cast<float>(-2.0 * cosW * norm);
    c.coeffs[2] = static_cast<float>(norm);
    c.coeffs[3] = 1.0f;
    c.coeffs[4] = static_cast<float>(-2.0 * cosW * norm);
    c.coeffs[5] = static_cast<float>((1.0 - kbw) * norm);
}

//------------------------------------------------------------------------------
/** Band-pass with pre-warped center and bandwidth. */
static void computeBandPass(BiquadCoeffs& c, double fc, double sr, double Q)
{
    fc = clampFreq(fc, sr);
    Q  = std::max(0.01, Q);
    const double W0d  = 2.0 * detail::kPi * fc / sr;
    const double cosW = std::cos(W0d);
    const double kbw  = std::tan(detail::kPi * std::min(fc / Q, sr * 0.4998) / sr);
    const double norm = 1.0 / (1.0 + kbw);

    c.coeffs[0] = static_cast<float>(kbw * norm);
    c.coeffs[1] = 0.0f;
    c.coeffs[2] = static_cast<float>(-kbw * norm);
    c.coeffs[3] = 1.0f;
    c.coeffs[4] = static_cast<float>(-2.0 * cosW * norm);
    c.coeffs[5] = static_cast<float>((1.0 - kbw) * norm);
}

} // namespace AnalogMatchedBiquad
