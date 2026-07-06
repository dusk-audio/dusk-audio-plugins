// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// DuskADAA.hpp — first-order antiderivative antialiasing (ADAA) helpers.
//
// Framework-free copy of Multi-Q's ADAASaturation.h. Standard waveshaper
// evaluation y = f(x) aliases when f is nonlinear and the signal has energy
// near Nyquist. ADAA replaces the point evaluation with the mean of f over
// [x_prev, x] using f's analytical antiderivative F1:
//     y = (F1(x) - F1(x_prev)) / (x - x_prev)
// alias-free to ~2x-oversampling degree for smooth nonlinearities, at a few
// multiplies per sample. Reference: Parker et al., DAFx 2016.

#pragma once

#include <cmath>

namespace duskaudio { namespace adaa
{

static constexpr float kEps = 1e-7f;

template <typename F, typename F1>
inline float process(float x, float xPrev, F f, F1 antideriv)
{
    const float dx = x - xPrev;
    if (std::abs(dx) < kEps)
        return f(0.5f * (x + xPrev)); // midpoint fallback (L'Hopital)
    return (antideriv(x) - antideriv(xPrev)) / dx;
}

// Antiderivative of f(v) = b·v² + c·v³ + d·v⁴ + e·v⁵.
inline float polyAntideriv(float v, float b, float c, float d, float e)
{
    const float v2 = v * v, v3 = v2 * v, v4 = v2 * v2, v5 = v4 * v, v6 = v5 * v;
    return b * v3 * (1.0f / 3.0f) + c * v4 * 0.25f + d * v5 * 0.2f + e * v6 * (1.0f / 6.0f);
}

// f(v) = b·v² + c·v³ + d·v⁴ + e·v⁵ (midpoint fallback).
inline float polyWaveshaper(float v, float b, float c, float d, float e)
{
    const float v2 = v * v, v3 = v2 * v, v4 = v2 * v2, v5 = v4 * v;
    return b * v2 + c * v3 + d * v4 + e * v5;
}

} } // namespace duskaudio::adaa
