#pragma once
#include <cmath>

//==============================================================================
/**
    First-order Antiderivative Antialiasing (ADAA) helpers.

    Standard waveshaper evaluation `y = f(x)` generates aliased harmonics when
    f is nonlinear and the signal has energy near Nyquist.  ADAA replaces the
    naive point-evaluation with the mean value of f over the interval [x_prev, x]:

        y = (F1(x) - F1(x_prev)) / (x - x_prev)

    where F1 is the analytical antiderivative of f.  This is mathematically
    equivalent to integrating f over the interval and is alias-free to the same
    degree as 2× oversampling for smooth, band-limited nonlinearities, with
    negligible CPU cost (a few extra multiplications per sample).

    When |x - x_prev| is very small (< eps), the ratio degenerates numerically;
    a midpoint fallback f((x + x_prev)/2) is used instead.

    Usage:
        float& xp = isLeft ? xPrevL : xPrevR;
        float y = ADAASaturation::process(xd, xp, f, F1);
        xp = xd;

    Reference: J. D. Parker et al., "Reducing the aliasing of nonlinear
    waveshaping using continuous-time convolution", DAFx 2016.
*/

namespace ADAASaturation
{

static constexpr float kEps = 1e-7f;

/** Applies first-order ADAA to a sample.

    @param x        Current input sample
    @param xPrev    Previous input sample (caller must update after call)
    @param f        Waveshaper function: float f(float x)
    @param antideriv Analytical antiderivative of f: float F1(float x)
    @returns        Alias-reduced output sample
*/
template<typename F, typename F1>
inline float process(float x, float xPrev, F f, F1 antideriv)
{
    float dx = x - xPrev;
    if (std::abs(dx) < kEps)
        return f(0.5f * (x + xPrev));   // Midpoint fallback (L'Hôpital)
    return (antideriv(x) - antideriv(xPrev)) / dx;
}

/** Antiderivative of a polynomial waveshaper:
        f(v) = b·v² + c·v³ + d·v⁴ + e·v⁵
        F1(v) = b·v³/3 + c·v⁴/4 + d·v⁵/5 + e·v⁶/6

    @returns F1(v)
*/
inline float polyAntideriv(float v, float b, float c, float d, float e)
{
    float v2 = v * v;
    float v3 = v2 * v;
    float v4 = v2 * v2;
    float v5 = v4 * v;
    float v6 = v5 * v;
    return b * v3 * (1.0f/3.0f)
         + c * v4 * 0.25f
         + d * v5 * 0.2f
         + e * v6 * (1.0f/6.0f);
}

/** Evaluates the polynomial waveshaper (for the midpoint fallback):
        f(v) = b·v² + c·v³ + d·v⁴ + e·v⁵
*/
inline float polyWaveshaper(float v, float b, float c, float d, float e)
{
    float v2 = v * v;
    float v3 = v2 * v;
    float v4 = v2 * v2;
    float v5 = v4 * v;
    return b*v2 + c*v3 + d*v4 + e*v5;
}

} // namespace ADAASaturation
