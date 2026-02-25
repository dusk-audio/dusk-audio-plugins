#pragma once

#include <cmath>

namespace DspUtils
{

// Tiny DC bias added to feedback paths to prevent denormal accumulation.
// Small enough to be inaudible but keeps FPU out of slow denormal mode.
static constexpr float kDenormalPrevention = 1.0e-15f;

// Returns the smallest power of 2 >= v. For v <= 1 returns 1.
inline int nextPowerOf2 (int v)
{
    if (v <= 1)
        return 1;

    unsigned int u = static_cast<unsigned int> (v - 1);
    u |= u >> 1;
    u |= u >> 2;
    u |= u >> 4;
    u |= u >> 8;
    u |= u >> 16;
    return static_cast<int> (u + 1);
}

// Cubic Hermite (Catmull-Rom) interpolation for fractional delay reads.
// idx is the integer part of the read position; frac is 0..1.
// Returns the interpolated value between buffer[idx] and buffer[idx+1].
// Buffer uses power-of-2 wrapping via bitmask.
inline float cubicHermite (const float* buffer, int mask, int idx, float frac)
{
    float y0 = buffer[(idx - 1) & mask];
    float y1 = buffer[idx & mask];
    float y2 = buffer[(idx + 1) & mask];
    float y3 = buffer[(idx + 2) & mask];

    float c0 = y1;
    float c1 = 0.5f * (y2 - y0);
    float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

// Fast rational tanh approximation: x*(27+x²)/(27+9x²).
// Accurate to within 0.001 for |x| < 3. Avoids the expensive log/exp
// path of std::tanh. Used in the FDN output soft-clipper.
inline float fastTanh (float x)
{
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

} // namespace DspUtils
