#pragma once
#include <cstdint>
#include <cstring>

// Bitwise finite check — immune to -ffast-math / finite-math-only optimizations
// that make std::isfinite unreliable
inline bool safeIsFinite(float v)
{
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}
