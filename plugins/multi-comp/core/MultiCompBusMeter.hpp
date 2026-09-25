// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later.
#pragma once
#include <algorithm>
#include <cmath>

namespace duskaudio::busLaw
{
// Display-only moving-coil response measured against the native BUS reference.
// Run on the audio clock: editor frame rate must not alter the meter motion.
// This does not change or replace the compressor's raw gain-reduction telemetry.
class CompressionMeter
{
public:
    void prepare(double sampleRate) noexcept
    {
        const double dt = 1.0 / (std::isfinite(sampleRate) && sampleRate > 0.0
                                   ? sampleRate : 48000.0);
        constexpr double omega = 24.0, damping = 0.7;
        const double decay = damping * omega;
        const double frequency = omega * std::sqrt(1.0 - damping * damping);
        const double e = std::exp(-decay * dt);
        const double s = std::sin(frequency * dt) / frequency;
        const double c = std::cos(frequency * dt);
        a11 = e * (c + decay * s); a12 = e * s;
        a21 = -e * omega * omega * s; a22 = e * (c - decay * s);
        reset();
    }
    void reset() noexcept { position = velocity = 0.0; }
    void process(float reductionDb) noexcept
    {
        // Modest overtravel beyond the printed 20 dB mark is intentional.
        const double target = std::isfinite(reductionDb)
            ? 0.917 * std::max(static_cast<double>(reductionDb), 0.0) : 0.0;
        const double error = position - target;
        position = target + a11 * error + a12 * velocity;
        velocity = a21 * error + a22 * velocity;
    }
    float reading() const noexcept { return static_cast<float>(std::clamp(position, 0.0, 22.0)); }
private:
    double position = 0.0, velocity = 0.0;
    double a11 = 1.0, a12 = 0.0, a21 = 0.0, a22 = 1.0;
};
} // namespace duskaudio::busLaw
