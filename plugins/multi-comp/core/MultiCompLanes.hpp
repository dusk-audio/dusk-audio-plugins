// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
// Lane policies for per-sample steps written once as a template over a policy (the OPTO cell in
// MultiCompOptoCell.hpp, the FET's C6 loop in MultiCompModes.hpp): a step runs either on one channel (Lane1: plain
// double / float / int / bool) or on two channels at once (Lane2: two-lane NEON registers on arm64; elsewhere the
// callers step the channels one at a time on Lane1).
//
// Every Lane2 operation is the IEEE operation Lane1 performs, lane by lane: separate multiply and add (no
// fusion; the plugin builds with -ffp-contract=off), float lanes for float arithmetic, and the same library
// calls per lane for the transcendental functions. A pair step therefore leaves each channel's state exactly as
// two single-channel steps would.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace duskaudio::lanes
{
// ---------------------------------------------------------------------------------------------- one lane
struct Lane1
{
    using D = double;
    using F = float;
    using I = int;
    using M = bool;   // a condition, for doubles, floats and ints alike
    static constexpr int kLanes = 1;

    static D d(double x) noexcept { return x; }
    static F f(float x) noexcept { return x; }
    static I i(int x) noexcept { return x; }
    static M m(bool x) noexcept { return x; }
    static D toD(F x) noexcept { return static_cast<double>(x); }
    static F toF(D x) noexcept { return static_cast<float>(x); }
    static D fromI(I x) noexcept { return static_cast<double>(x); }
    static D sel(M m, D a, D b) noexcept { return m ? a : b; }
    static F sel(M m, F a, F b) noexcept { return m ? a : b; }
    static I sel(M m, I a, I b) noexcept { return m ? a : b; }
    static M sel(M m, M a, M b) noexcept { return m ? a : b; }
    template <class Fn> static D map(Fn&& fn, D x) noexcept { return fn(x); }
    template <class Fn> static D map(Fn&& fn, D x, D y) noexcept { return fn(x, y); }
    template <class Fn> static F mapF(Fn&& fn, F x) noexcept { return fn(x); }
    template <class Fn> static F mapF(Fn&& fn, F x, F y) noexcept { return fn(x, y); }
    static bool any(M m) noexcept { return m; }
    static bool all(M m) noexcept { return m; }
    static double lane(D x, int) noexcept { return x; }
    static float laneF(F x, int) noexcept { return x; }
    static bool laneM(M x, int) noexcept { return x; }
};

// ---------------------------------------------------------------------------------------------- two lanes
#if defined(__aarch64__)
struct D2 { float64x2_t v; };
struct F2 { float32x2_t v; };
struct I2 { int32x2_t v; };
struct M2 { uint32x2_t v; };   // a condition per lane, as all-ones / zero 32-bit words

inline D2 operator+(D2 a, D2 b) noexcept { return {vaddq_f64(a.v, b.v)}; }
inline D2 operator-(D2 a, D2 b) noexcept { return {vsubq_f64(a.v, b.v)}; }
inline D2 operator*(D2 a, D2 b) noexcept { return {vmulq_f64(a.v, b.v)}; }
inline D2 operator/(D2 a, D2 b) noexcept { return {vdivq_f64(a.v, b.v)}; }
inline D2 operator-(D2 a) noexcept { return {vnegq_f64(a.v)}; }
inline F2 operator+(F2 a, F2 b) noexcept { return {vadd_f32(a.v, b.v)}; }
inline F2 operator-(F2 a, F2 b) noexcept { return {vsub_f32(a.v, b.v)}; }
inline F2 operator*(F2 a, F2 b) noexcept { return {vmul_f32(a.v, b.v)}; }
inline F2 operator/(F2 a, F2 b) noexcept { return {vdiv_f32(a.v, b.v)}; }
inline F2 operator-(F2 a) noexcept { return {vneg_f32(a.v)}; }
inline I2 operator+(I2 a, I2 b) noexcept { return {vadd_s32(a.v, b.v)}; }
inline I2 operator-(I2 a, I2 b) noexcept { return {vsub_s32(a.v, b.v)}; }
inline I2 operator-(I2 a) noexcept { return {vneg_s32(a.v)}; }
inline M2 narrow(uint64x2_t m) noexcept { return {vmovn_u64(m)}; }
inline uint64x2_t widen(M2 m) noexcept { return vreinterpretq_u64_s64(vmovl_s32(vreinterpret_s32_u32(m.v))); }
inline M2 operator<(D2 a, D2 b) noexcept { return narrow(vcltq_f64(a.v, b.v)); }
inline M2 operator<=(D2 a, D2 b) noexcept { return narrow(vcleq_f64(a.v, b.v)); }
inline M2 operator>(D2 a, D2 b) noexcept { return narrow(vcgtq_f64(a.v, b.v)); }
inline M2 operator>=(D2 a, D2 b) noexcept { return narrow(vcgeq_f64(a.v, b.v)); }
inline M2 operator==(D2 a, D2 b) noexcept { return narrow(vceqq_f64(a.v, b.v)); }
inline M2 operator<(F2 a, F2 b) noexcept { return {vclt_f32(a.v, b.v)}; }
inline M2 operator<=(F2 a, F2 b) noexcept { return {vcle_f32(a.v, b.v)}; }
inline M2 operator>(F2 a, F2 b) noexcept { return {vcgt_f32(a.v, b.v)}; }
inline M2 operator>=(F2 a, F2 b) noexcept { return {vcge_f32(a.v, b.v)}; }
inline M2 operator==(F2 a, F2 b) noexcept { return {vceq_f32(a.v, b.v)}; }
inline M2 operator<(I2 a, I2 b) noexcept { return {vclt_s32(a.v, b.v)}; }
inline M2 operator>(I2 a, I2 b) noexcept { return {vcgt_s32(a.v, b.v)}; }
inline M2 operator==(I2 a, I2 b) noexcept { return {vceq_s32(a.v, b.v)}; }
inline M2 operator&&(M2 a, M2 b) noexcept { return {vand_u32(a.v, b.v)}; }
inline M2 operator||(M2 a, M2 b) noexcept { return {vorr_u32(a.v, b.v)}; }
inline M2 operator!(M2 a) noexcept { return {vmvn_u32(a.v)}; }

struct Lane2
{
    using D = D2;
    using F = F2;
    using I = I2;
    using M = M2;
    static constexpr int kLanes = 2;

    static D d(double x) noexcept { return {vdupq_n_f64(x)}; }
    static D d(double a, double b) noexcept { return {float64x2_t{a, b}}; }
    static F f(float x) noexcept { return {vdup_n_f32(x)}; }
    static F f(float a, float b) noexcept { return {float32x2_t{a, b}}; }
    static I i(int x) noexcept { return {vdup_n_s32(x)}; }
    static I i(int a, int b) noexcept { return {int32x2_t{a, b}}; }
    static M m(bool x) noexcept { return {vdup_n_u32(x ? 0xffffffffu : 0u)}; }
    static M m(bool a, bool b) noexcept { return {uint32x2_t{a ? 0xffffffffu : 0u, b ? 0xffffffffu : 0u}}; }
    static D toD(F x) noexcept { return {vcvt_f64_f32(x.v)}; }
    static F toF(D x) noexcept { return {vcvt_f32_f64(x.v)}; }
    static D fromI(I x) noexcept { return {vcvtq_f64_s64(vmovl_s32(x.v))}; }
    static D sel(M m, D a, D b) noexcept { return {vbslq_f64(widen(m), a.v, b.v)}; }
    static F sel(M m, F a, F b) noexcept { return {vbsl_f32(m.v, a.v, b.v)}; }
    static I sel(M m, I a, I b) noexcept { return {vbsl_s32(m.v, a.v, b.v)}; }
    static M sel(M m, M a, M b) noexcept { return {vbsl_u32(m.v, a.v, b.v)}; }
    template <class Fn> static D map(Fn&& fn, D x) noexcept
    {
        return d(fn(vgetq_lane_f64(x.v, 0)), fn(vgetq_lane_f64(x.v, 1)));
    }
    template <class Fn> static D map(Fn&& fn, D x, D y) noexcept
    {
        return d(fn(vgetq_lane_f64(x.v, 0), vgetq_lane_f64(y.v, 0)), fn(vgetq_lane_f64(x.v, 1), vgetq_lane_f64(y.v, 1)));
    }
    template <class Fn> static F mapF(Fn&& fn, F x) noexcept
    {
        return f(fn(vget_lane_f32(x.v, 0)), fn(vget_lane_f32(x.v, 1)));
    }
    template <class Fn> static F mapF(Fn&& fn, F x, F y) noexcept
    {
        return f(fn(vget_lane_f32(x.v, 0), vget_lane_f32(y.v, 0)), fn(vget_lane_f32(x.v, 1), vget_lane_f32(y.v, 1)));
    }
    static D swap(D x) noexcept { return {vextq_f64(x.v, x.v, 1)}; }   // each lane gets the other's value
    static F swap(F x) noexcept { return {vrev64_f32(x.v)}; }
    static bool any(M m) noexcept { return vmaxv_u32(m.v) != 0; }
    static bool all(M m) noexcept { return vminv_u32(m.v) != 0; }
    static double lane(D x, int k) noexcept { return k == 0 ? vgetq_lane_f64(x.v, 0) : vgetq_lane_f64(x.v, 1); }
    static float laneF(F x, int k) noexcept { return k == 0 ? vget_lane_f32(x.v, 0) : vget_lane_f32(x.v, 1); }
    static int laneI(I x, int k) noexcept { return k == 0 ? vget_lane_s32(x.v, 0) : vget_lane_s32(x.v, 1); }
    static bool laneM(M x, int k) noexcept { return (k == 0 ? vget_lane_u32(x.v, 0) : vget_lane_u32(x.v, 1)) != 0; }
};

// Lane-wise library functions with the scalar ones' results.
inline D2 vmin(D2 a, D2 b) noexcept { return Lane2::sel(b < a, b, a); }   // std::min(a, b): (b < a) ? b : a
inline D2 vmax(D2 a, D2 b) noexcept { return Lane2::sel(a < b, b, a); }   // std::max(a, b): (a < b) ? b : a
inline F2 vmin(F2 a, F2 b) noexcept { return Lane2::sel(b < a, b, a); }
inline F2 vmax(F2 a, F2 b) noexcept { return Lane2::sel(a < b, b, a); }
inline I2 vmin(I2 a, I2 b) noexcept { return Lane2::sel(b < a, b, a); }
inline I2 vmax(I2 a, I2 b) noexcept { return Lane2::sel(a < b, b, a); }
inline D2 vabs(D2 a) noexcept { return {vabsq_f64(a.v)}; }
inline F2 vabs(F2 a) noexcept { return {vabs_f32(a.v)}; }
inline D2 vsqrt(D2 a) noexcept { return {vsqrtq_f64(a.v)}; }
inline F2 vsqrt(F2 a) noexcept { return {vsqrt_f32(a.v)}; }
inline M2 visfinite(D2 a) noexcept { return vabs(a) <= Lane2::d(1.79769313486231570815e308); }
inline M2 visfinite(F2 a) noexcept { return vabs(a) <= Lane2::f(3.40282347e+38f); }
// An upper bound on log2(x) for a positive normal x, read from its bits: the exponent plus the mantissa's fraction m,
// plus 0.0861, the most log2(1 + m) exceeds m on [0, 1). Converting the bit pattern to float rounds it to 24 bits,
// within 1e-5 of the exact value.
// Four consecutive floats from each of two addresses as four two-lane values: out[j] = {a[j], b[j]}.
inline void vtranspose4(const float* a, const float* b, F2* out) noexcept
{
    const float32x4_t x = vld1q_f32(a), y = vld1q_f32(b);
    const float32x4_t low = vzip1q_f32(x, y), high = vzip2q_f32(x, y);
    out[0] = {vget_low_f32(low)};
    out[1] = {vget_high_f32(low)};
    out[2] = {vget_low_f32(high)};
    out[3] = {vget_high_f32(high)};
}
inline F2 vlog2Above(F2 x) noexcept
{
    const float32x2_t bits = vcvt_f32_s32(vreinterpret_s32_f32(x.v));
    return {vadd_f32(vmul_f32(vsub_f32(bits, vdup_n_f32(1065353216.0f)), vdup_n_f32(1.0f / 8388608.0f)),
                     vdup_n_f32(0.0861f))};
}
#endif

// Lane1 counterparts, so the template body reads the same for both policies.
inline double vmin(double a, double b) noexcept { return std::min(a, b); }
inline double vmax(double a, double b) noexcept { return std::max(a, b); }
inline float vmin(float a, float b) noexcept { return std::min(a, b); }
inline float vmax(float a, float b) noexcept { return std::max(a, b); }
inline int vmin(int a, int b) noexcept { return std::min(a, b); }
inline int vmax(int a, int b) noexcept { return std::max(a, b); }
inline double vabs(double a) noexcept { return std::abs(a); }
inline float vabs(float a) noexcept { return std::abs(a); }
inline double vsqrt(double a) noexcept { return std::sqrt(a); }
inline float vsqrt(float a) noexcept { return std::sqrt(a); }
inline bool visfinite(double a) noexcept { return std::isfinite(a); }
inline bool visfinite(float a) noexcept { return std::isfinite(a); }
inline void vtranspose4(const float* a, const float*, float* out) noexcept
{
    for (int j = 0; j < 4; ++j) out[j] = a[j];
}
inline float vlog2Above(float x) noexcept
{
    std::int32_t bits;
    std::memcpy(&bits, &x, sizeof bits);
    return (static_cast<float>(bits) - 1065353216.0f) * (1.0f / 8388608.0f) + 0.0861f;
}
} // namespace duskaudio::lanes
