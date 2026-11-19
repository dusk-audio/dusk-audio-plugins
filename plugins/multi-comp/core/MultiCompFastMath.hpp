// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
// Base-2 logarithm and exponential for gain laws evaluated over a block.
//
// Each function exists as a scalar form and a four-lane form that perform the
// same IEEE single-precision operations in the same order (separate multiply
// and add, no fusion; the plugin builds with -ffp-contract=off), so a value is
// the same whichever form computed it and on every platform. The four-lane form
// uses NEON on arm64 and the scalar form lane by lane elsewhere.
//
// Accuracy, checked against double-precision libm at every normal float input
// (2026-10-19):
//   log2: absolute error under 4.0e-6 (0.000024 dB as 20 log10); the worst
//         case is the float rounding of results near -126
//   exp2: relative error under 2.6e-7 (0.0000022 dB) for y in [-126, 127]
#pragma once

#include <cstdint>
#include <cstring>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace duskaudio::fastmath
{
// log2(m) = (2 / ln 2) atanh(t), t = (m - 1) / (m + 1), for m in [sqrt(1/2), sqrt(2)).
inline constexpr float kLogC1 = 2.88539008f, kLogC3 = 0.961796694f, kLogC5 = 0.577078016f, kLogC7 = 0.412198583f;
// 2^f = e^(f ln 2), Taylor to the sixth power, for |f| <= 0.5.
inline constexpr float kLn2 = 0.693147181f;
inline constexpr float kExpC2 = 0.5f, kExpC3 = 0.166666667f, kExpC4 = 0.0416666667f,
                       kExpC5 = 0.00833333333f, kExpC6 = 0.00138888889f;

// log2(x) for finite normal x > 0 (callers floor the argument first).
inline float log2(float x) noexcept
{
    std::uint32_t bits;
    std::memcpy(&bits, &x, sizeof bits);
    int exponent = static_cast<int>((bits >> 23) & 0xffu) - 127;
    bits = (bits & 0x007fffffu) | 0x3f800000u;
    float m;
    std::memcpy(&m, &bits, sizeof m);
    if (m > 1.41421356f)
    {
        m = m * 0.5f;
        exponent = exponent + 1;
    }
    const float t = (m - 1.0f) / (m + 1.0f);
    const float t2 = t * t;
    const float series = t * (kLogC1 + t2 * (kLogC3 + t2 * (kLogC5 + t2 * kLogC7)));
    return static_cast<float>(exponent) + series;
}

// 2^y, y clamped to [-126, 127].
inline float exp2(float y) noexcept
{
    const float c = y < -126.0f ? -126.0f : (y > 127.0f ? 127.0f : y);
    const int n = static_cast<int>(c + (c >= 0.0f ? 0.5f : -0.5f));   // nearest, ties away from zero
    const float f = c - static_cast<float>(n);
    const float z = f * kLn2;
    const float poly = 1.0f + z * (1.0f + z * (kExpC2 + z * (kExpC3 + z * (kExpC4 + z * (kExpC5 + z * kExpC6)))));
    const std::uint32_t scaleBits = static_cast<std::uint32_t>(n + 127) << 23;
    float scale;
    std::memcpy(&scale, &scaleBits, sizeof scale);
    return poly * scale;
}

// Four lanes of log2 / exp2, identical to the scalar forms lane by lane.
inline void log2x4(const float* x, float* out) noexcept
{
#if defined(__aarch64__)
    const uint32x4_t bits = vreinterpretq_u32_f32(vld1q_f32(x));
    int32x4_t exponent = vsubq_s32(vreinterpretq_s32_u32(vandq_u32(vshrq_n_u32(bits, 23), vdupq_n_u32(0xffu))),
                                   vdupq_n_s32(127));
    float32x4_t m = vreinterpretq_f32_u32(vorrq_u32(vandq_u32(bits, vdupq_n_u32(0x007fffffu)), vdupq_n_u32(0x3f800000u)));
    const uint32x4_t high = vcgtq_f32(m, vdupq_n_f32(1.41421356f));
    m = vbslq_f32(high, vmulq_f32(m, vdupq_n_f32(0.5f)), m);
    exponent = vsubq_s32(exponent, vreinterpretq_s32_u32(high));   // a true lane is -1: adds one
    const float32x4_t one = vdupq_n_f32(1.0f);
    const float32x4_t t = vdivq_f32(vsubq_f32(m, one), vaddq_f32(m, one));
    const float32x4_t t2 = vmulq_f32(t, t);
    float32x4_t series = vaddq_f32(vdupq_n_f32(kLogC5), vmulq_f32(t2, vdupq_n_f32(kLogC7)));
    series = vaddq_f32(vdupq_n_f32(kLogC3), vmulq_f32(t2, series));
    series = vaddq_f32(vdupq_n_f32(kLogC1), vmulq_f32(t2, series));
    series = vmulq_f32(t, series);
    vst1q_f32(out, vaddq_f32(vcvtq_f32_s32(exponent), series));
#else
    for (int lane = 0; lane < 4; ++lane) out[lane] = log2(x[lane]);
#endif
}

inline void exp2x4(const float* y, float* out) noexcept
{
#if defined(__aarch64__)
    const float32x4_t c = vminq_f32(vmaxq_f32(vld1q_f32(y), vdupq_n_f32(-126.0f)), vdupq_n_f32(127.0f));
    const float32x4_t half = vbslq_f32(vcgeq_f32(c, vdupq_n_f32(0.0f)), vdupq_n_f32(0.5f), vdupq_n_f32(-0.5f));
    const int32x4_t n = vcvtq_s32_f32(vaddq_f32(c, half));   // truncates toward zero, as the scalar cast
    const float32x4_t f = vsubq_f32(c, vcvtq_f32_s32(n));
    const float32x4_t z = vmulq_f32(f, vdupq_n_f32(kLn2));
    float32x4_t poly = vaddq_f32(vdupq_n_f32(kExpC5), vmulq_f32(z, vdupq_n_f32(kExpC6)));
    poly = vaddq_f32(vdupq_n_f32(kExpC4), vmulq_f32(z, poly));
    poly = vaddq_f32(vdupq_n_f32(kExpC3), vmulq_f32(z, poly));
    poly = vaddq_f32(vdupq_n_f32(kExpC2), vmulq_f32(z, poly));
    poly = vaddq_f32(vdupq_n_f32(1.0f), vmulq_f32(z, poly));
    poly = vaddq_f32(vdupq_n_f32(1.0f), vmulq_f32(z, poly));
    const float32x4_t scale = vreinterpretq_f32_s32(vshlq_n_s32(vaddq_s32(n, vdupq_n_s32(127)), 23));
    vst1q_f32(out, vmulq_f32(poly, scale));
#else
    for (int lane = 0; lane < 4; ++lane) out[lane] = exp2(y[lane]);
#endif
}
} // namespace duskaudio::fastmath
