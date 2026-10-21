// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace multicompp
{
// Average-responding VU detector, calibrated to the RMS of a sine. This is
// separate from the existing peak meters and never modifies the audio buffer.
class OutputVu
{
public:
    void prepare(double sampleRate) noexcept
    {
        const double rate = std::isfinite(sampleRate) && sampleRate > 0 ? sampleRate : 48000;
        coefficient = -std::expm1(-1.0 / (0.045 * rate));
        reset();
    }
    void reset() noexcept
    {
        stages = {};
        published.store(-120.0f, std::memory_order_relaxed);
    }
    void process(const float* const* output, int channels, int samples) noexcept
    {
        if (output == nullptr || channels <= 0 || samples <= 0) return;
        channels = std::min(channels, 2);
        const auto rectified = [](float x) noexcept { return std::isfinite(x) ? std::abs(static_cast<double>(x)) : 0.0; };
        const auto advance = [this](std::array<double, 2>& state, double value) noexcept {
            state[0] += coefficient * (value - state[0]);
            state[1] += coefficient * (state[0] - state[1]);
        };
        double level = 0;
        if (channels == 2 && output[0] != nullptr && output[1] != nullptr)
        {
            // Both channels in one pass: two independent recurrences side by side.
            auto& left = stages[0];
            auto& right = stages[1];
#if defined(__aarch64__)
            // The two channels as the lanes of one register: the same double
            // arithmetic as advance(), lane by lane.
            const float64x2_t c = vdupq_n_f64(coefficient);
            float64x2_t first = {left[0], right[0]}, second = {left[1], right[1]};
            for (int i = 0; i < samples; ++i)
            {
                const float l = output[0][i], r = output[1][i];
                const float64x2_t value = {rectified(l), rectified(r)};
                first = vaddq_f64(first, vmulq_f64(c, vsubq_f64(value, first)));
                second = vaddq_f64(second, vmulq_f64(c, vsubq_f64(first, second)));
            }
            left = {vgetq_lane_f64(first, 0), vgetq_lane_f64(second, 0)};
            right = {vgetq_lane_f64(first, 1), vgetq_lane_f64(second, 1)};
#else
            for (int i = 0; i < samples; ++i)
            {
                advance(left, rectified(output[0][i]));
                advance(right, rectified(output[1][i]));
            }
#endif
            level = std::max(left[1], right[1]);
        }
        else
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& state = stages[static_cast<size_t>(ch)];
                if (ch >= channels || output[ch] == nullptr) { state = {}; continue; }
                for (int i = 0; i < samples; ++i) advance(state, rectified(output[ch][i]));
                level = std::max(level, state[1]);
            }
        // Per-channel detection preserves anti-phase stereo and one-sided
        // signals; the single needle reports the louder channel.
        constexpr double rectifiedToRms = 1.1107207345395915;
        published.store(static_cast<float>(20 * std::log10(std::max(1.0e-6, level * rectifiedToRms))),
                        std::memory_order_relaxed);
    }
    float levelDb() const noexcept { return published.load(std::memory_order_relaxed); }

private:
    std::array<std::array<double, 2>, 2> stages{};
    double coefficient = 0;
    std::atomic<float> published{-120.0f};
};
}
