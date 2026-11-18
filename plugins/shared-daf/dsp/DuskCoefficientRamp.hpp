// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
#pragma once

#include <array>
#include <cstddef>
#include <cmath>

namespace duskaudio
{
// One-pole interpolation of float coefficient sets with retained fractional
// progress. Rounding the accumulator to float every sample can stop the ramp
// many ULPs short of target, changing the filter's actual transfer function.
// Targets, published coefficients and audio recurrences remain float. Only the
// interpolation history uses double; no allocation, locks or unbounded loops.
template <std::size_t N>
class CoefficientRamp
{
public:
    void reset(const float* current) noexcept
    {
        for (std::size_t i = 0; i < N; ++i) history[i] = current[i];
    }

    // reset() at initialisation, snap and lifecycle reset. Changing a target
    // while ramping must retain history, including its sub-float progress.
    bool step(float* current, const float* target, float alpha) noexcept
    {
        bool settled = true;
        for (std::size_t i = 0; i < N; ++i)
        {
            history[i] += double(alpha) * (double(target[i]) - history[i]);
            current[i] = static_cast<float>(history[i]);
            // Keep the existing absolute convergence tolerance, evaluated on
            // retained progress. It also gives zero targets a finite end point.
            settled &= std::abs(history[i] - double(target[i])) < 1.0e-7;
        }
        if (settled)
            for (std::size_t i = 0; i < N; ++i) current[i] = static_cast<float>(history[i] = target[i]);
        return settled;
    }

private:
    std::array<double, N> history{};
};
} // namespace duskaudio
