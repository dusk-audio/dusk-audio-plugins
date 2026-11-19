// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>

namespace multicompp
{
// The Studio VCA face's level meter, as the reference studio VCA compressor's own I/O meter reads (measured
// 2026-10-26 from frames of its needle: sine, square, half-wave and pink at equal rms, staircases and bursts):
// the louder channel's peak level, instant attack, a release exponential in amplitude with a 0.274 s time
// constant, shown on a -60..0 dB scale. INPUT reads the input after the Input control, OUTPUT the plugin's output.
// Display only; fed one host block at a time with the block's peak.
class StudioVcaLevelMeter
{
public:
    static constexpr double kReleaseSeconds = 0.274;
    static constexpr float kFloorDb = -120.0f;

    void prepare(double sampleRate) noexcept
    {
        rate = std::isfinite(sampleRate) && sampleRate > 0 ? sampleRate : 48000.0;
        decaySamples = -1;
        reset();
    }
    void reset() noexcept
    {
        level = 0.0;
        published.store(kFloorDb, std::memory_order_relaxed);
    }
    void process(float blockPeak, int samples) noexcept
    {
        if (samples <= 0) return;
        if (samples != decaySamples)
        {
            decaySamples = samples;
            decay = std::exp(-static_cast<double>(samples) / (kReleaseSeconds * rate));
        }
        const double peak = std::isfinite(blockPeak) ? static_cast<double>(blockPeak) : 0.0;
        level = std::max(peak, level * decay);
        published.store(level > 1.0e-6 ? static_cast<float>(20.0 * std::log10(level)) : kFloorDb,
                         std::memory_order_relaxed);
    }
    float levelDb() const noexcept { return published.load(std::memory_order_relaxed); }

private:
    double rate = 48000.0, level = 0.0, decay = 1.0;
    int decaySamples = -1;
    std::atomic<float> published{kFloorDb};
};
}
