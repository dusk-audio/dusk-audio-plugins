// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
// The Opto output stage's transfer (the generated curve of MultiCompOptoOutputStage.hpp) evaluated one sample at a
// time or four at once. Both forms perform the same IEEE single-precision operations in the same order (separate
// multiply and add, no fusion; the plugin builds with -ffp-contract=off), so a sample's value does not depend on
// which form computed it. Against optoOutputStageCurve() the only difference is asinh, taken here from its series
// below |y| = 0.25 (relative error under 2e-8) and from ln(|y| + sqrt(y^2 + 1)) above it, with ln from the
// exponent and the atanh series of the mantissa (absolute error under 7e-10): within a few float ulps of the
// library's (CPU-parity campaign, 2026-10-21).
#pragma once

#include "MultiCompOptoOutputStage.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace duskaudio::optocurve
{
// The generated samples with the curve's end values repeated on either side: the four samples around position p
// are kPadded[i .. i + 3] for i = floor(p), the reads optoOutputStageCurve()'s clamped at() makes.
inline constexpr std::array<float, kOptoOutputStageCurve.size() + 3> kPadded = [] {
    std::array<float, kOptoOutputStageCurve.size() + 3> padded{};
    const int last = kOptoOutputStageLast;
    for (int k = 0; k < static_cast<int>(padded.size()); ++k)
        padded[static_cast<size_t>(k)] = kOptoOutputStageCurve[static_cast<size_t>(std::clamp(k - 1, 0, last))];
    return padded;
}();

inline constexpr float kInverseX0 = 1.0f / kOptoOutputStageX0;
inline constexpr float kLn2 = 0.693147181f;
// asinh(y) = y - y^3 / 6 + 3 y^5 / 40 - 15 y^7 / 336 + 105 y^9 / 3456 - ...
inline constexpr float kA3 = -0.166666667f, kA5 = 0.075f, kA7 = -0.0446428571f, kA9 = 0.0303819444f;
// ln m = 2 atanh t, t = (m - 1) / (m + 1): 2 (t + t^3 / 3 + t^5 / 5 + t^7 / 7 + t^9 / 9)
inline constexpr float kT3 = 0.333333333f, kT5 = 0.2f, kT7 = 0.142857143f, kT9 = 0.111111111f;

inline float asinhPart(float y) noexcept
{
    const float a = y < 0.0f ? -y : y;
    float r;
    if (a < 0.25f)
    {
        const float a2 = a * a;
        r = a + a * a2 * (kA3 + a2 * (kA5 + a2 * (kA7 + a2 * kA9)));
    }
    else
    {
        const float v = a + std::sqrt(a * a + 1.0f);
        std::uint32_t bits;
        std::memcpy(&bits, &v, sizeof bits);
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
        r = static_cast<float>(exponent) * kLn2 + 2.0f * (t + t * t2 * (kT3 + t2 * (kT5 + t2 * (kT7 + t2 * kT9))));
    }
    return y < 0.0f ? -r : r;
}

// The output stage's transfer for one sample.
inline float curve(float x) noexcept
{
    constexpr int last = kOptoOutputStageLast;
    const float s = asinhPart(x * kInverseX0);
    // NaN fails the comparison and lands on a finite node, as in optoOutputStageCurve().
    const float raw = (s + kOptoOutputStageSMax) / kOptoOutputStageStep;
    const float position = raw > 0.0f ? (raw < static_cast<float>(last) ? raw : static_cast<float>(last)) : 0.0f;
    const int i = std::min(static_cast<int>(position), last - 1);
    const float t = position - static_cast<float>(i);
    const float* c = kPadded.data() + i;
    const float t2 = t * t, t3 = t2 * t;
    return c[0] * (-0.5f * t3 + t2 - 0.5f * t)
         + c[1] * (1.5f * t3 - 2.5f * t2 + 1.0f)
         + c[2] * (-1.5f * t3 + 2.0f * t2 + 0.5f * t)
         + c[3] * (0.5f * t3 - 0.5f * t2);
}

// curve() for four samples (out[k] = curve(x[k])).
inline void curve4(const float* x, float* out) noexcept
{
#if defined(__aarch64__)
    const float32x4_t y = vmulq_f32(vld1q_f32(x), vdupq_n_f32(kInverseX0));
    const uint32x4_t negative = vcltq_f32(y, vdupq_n_f32(0.0f));
    const float32x4_t a = vbslq_f32(negative, vnegq_f32(y), y);
    // Series, |a| < 0.25
    const float32x4_t a2 = vmulq_f32(a, a);
    float32x4_t series = vaddq_f32(vdupq_n_f32(kA7), vmulq_f32(a2, vdupq_n_f32(kA9)));
    series = vaddq_f32(vdupq_n_f32(kA5), vmulq_f32(a2, series));
    series = vaddq_f32(vdupq_n_f32(kA3), vmulq_f32(a2, series));
    series = vaddq_f32(a, vmulq_f32(vmulq_f32(a, a2), series));
    // ln(a + sqrt(a^2 + 1)), |a| >= 0.25
    const float32x4_t v = vaddq_f32(a, vsqrtq_f32(vaddq_f32(a2, vdupq_n_f32(1.0f))));
    const uint32x4_t bits = vreinterpretq_u32_f32(v);
    int32x4_t exponent = vsubq_s32(vreinterpretq_s32_u32(vandq_u32(vshrq_n_u32(bits, 23), vdupq_n_u32(0xffu))),
                                   vdupq_n_s32(127));
    float32x4_t m = vreinterpretq_f32_u32(vorrq_u32(vandq_u32(bits, vdupq_n_u32(0x007fffffu)), vdupq_n_u32(0x3f800000u)));
    const uint32x4_t high = vcgtq_f32(m, vdupq_n_f32(1.41421356f));
    m = vbslq_f32(high, vmulq_f32(m, vdupq_n_f32(0.5f)), m);
    exponent = vbslq_s32(high, vaddq_s32(exponent, vdupq_n_s32(1)), exponent);
    const float32x4_t t = vdivq_f32(vsubq_f32(m, vdupq_n_f32(1.0f)), vaddq_f32(m, vdupq_n_f32(1.0f)));
    const float32x4_t t2 = vmulq_f32(t, t);
    float32x4_t odd = vaddq_f32(vdupq_n_f32(kT7), vmulq_f32(t2, vdupq_n_f32(kT9)));
    odd = vaddq_f32(vdupq_n_f32(kT5), vmulq_f32(t2, odd));
    odd = vaddq_f32(vdupq_n_f32(kT3), vmulq_f32(t2, odd));
    odd = vaddq_f32(t, vmulq_f32(vmulq_f32(t, t2), odd));
    const float32x4_t logarithm = vaddq_f32(vmulq_f32(vcvtq_f32_s32(exponent), vdupq_n_f32(kLn2)),
                                            vmulq_f32(vdupq_n_f32(2.0f), odd));
    const float32x4_t r = vbslq_f32(vcltq_f32(a, vdupq_n_f32(0.25f)), series, logarithm);
    const float32x4_t s = vbslq_f32(negative, vnegq_f32(r), r);
    // The spline
    constexpr int last = kOptoOutputStageLast;
    const float32x4_t raw = vdivq_f32(vaddq_f32(s, vdupq_n_f32(kOptoOutputStageSMax)), vdupq_n_f32(kOptoOutputStageStep));
    const float32x4_t lastF = vdupq_n_f32(static_cast<float>(last));
    const float32x4_t position = vbslq_f32(vcgtq_f32(raw, vdupq_n_f32(0.0f)),
                                           vbslq_f32(vcltq_f32(raw, lastF), raw, lastF), vdupq_n_f32(0.0f));
    const int32x4_t index = vminq_s32(vcvtq_s32_f32(position), vdupq_n_s32(last - 1));
    const float32x4_t tt = vsubq_f32(position, vcvtq_f32_s32(index));
    const float32x4_t r0 = vld1q_f32(kPadded.data() + vgetq_lane_s32(index, 0));
    const float32x4_t r1 = vld1q_f32(kPadded.data() + vgetq_lane_s32(index, 1));
    const float32x4_t r2 = vld1q_f32(kPadded.data() + vgetq_lane_s32(index, 2));
    const float32x4_t r3 = vld1q_f32(kPadded.data() + vgetq_lane_s32(index, 3));
    // Transpose: c[j] = {r0[j], r1[j], r2[j], r3[j]}
    const float32x4_t lo01 = vzip1q_f32(r0, r1), hi01 = vzip2q_f32(r0, r1);
    const float32x4_t lo23 = vzip1q_f32(r2, r3), hi23 = vzip2q_f32(r2, r3);
    const float32x4_t c0 = vreinterpretq_f32_f64(vzip1q_f64(vreinterpretq_f64_f32(lo01), vreinterpretq_f64_f32(lo23)));
    const float32x4_t c1 = vreinterpretq_f32_f64(vzip2q_f64(vreinterpretq_f64_f32(lo01), vreinterpretq_f64_f32(lo23)));
    const float32x4_t c2 = vreinterpretq_f32_f64(vzip1q_f64(vreinterpretq_f64_f32(hi01), vreinterpretq_f64_f32(hi23)));
    const float32x4_t c3 = vreinterpretq_f32_f64(vzip2q_f64(vreinterpretq_f64_f32(hi01), vreinterpretq_f64_f32(hi23)));
    const float32x4_t t2s = vmulq_f32(tt, tt), t3s = vmulq_f32(t2s, tt);
    const float32x4_t half = vdupq_n_f32(0.5f);
    const float32x4_t w0 = vsubq_f32(vaddq_f32(vmulq_f32(vdupq_n_f32(-0.5f), t3s), t2s), vmulq_f32(half, tt));
    const float32x4_t w1 = vaddq_f32(vsubq_f32(vmulq_f32(vdupq_n_f32(1.5f), t3s), vmulq_f32(vdupq_n_f32(2.5f), t2s)),
                                     vdupq_n_f32(1.0f));
    const float32x4_t w2 = vaddq_f32(vaddq_f32(vmulq_f32(vdupq_n_f32(-1.5f), t3s), vmulq_f32(vdupq_n_f32(2.0f), t2s)),
                                     vmulq_f32(half, tt));
    const float32x4_t w3 = vsubq_f32(vmulq_f32(half, t3s), vmulq_f32(half, t2s));
    float32x4_t y4 = vaddq_f32(vmulq_f32(c0, w0), vmulq_f32(c1, w1));
    y4 = vaddq_f32(y4, vmulq_f32(c2, w2));
    y4 = vaddq_f32(y4, vmulq_f32(c3, w3));
    vst1q_f32(out, y4);
#else
    for (int k = 0; k < 4; ++k) out[k] = curve(x[k]);
#endif
}

// The curve as the stage oversampler takes it: one sample, or four.
struct OutputCurve
{
    float operator()(float x) const noexcept { return curve(x); }
    static void four(const float* x, float* out) noexcept { curve4(x, out); }
};
} // namespace duskaudio::optocurve
