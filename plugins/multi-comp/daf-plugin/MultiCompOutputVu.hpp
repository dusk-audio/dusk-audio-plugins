// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

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
        double level = 0;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& state = stages[static_cast<size_t>(ch)];
            if (ch >= channels || output[ch] == nullptr) { state = {}; continue; }
            for (int i = 0; i < samples; ++i)
            {
                const double value = std::isfinite(output[ch][i]) ? std::abs(output[ch][i]) : 0.0;
                state[0] += coefficient * (value - state[0]);
                state[1] += coefficient * (state[0] - state[1]);
            }
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
