// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace duskaudio::sslbus
{
// Charging drive in dB and its slope with respect to detector level.
struct Drive { float value, slope; };

// Piecewise-linear node table. Beyond the last node the last segment continues:
// the native drive keeps steepening, and a held constant would drop the law's
// slope back to its linear term (the 2:1 / 30 ms / 0.1 s gap was exactly that).
template <size_t N>
inline Drive nodeCorrection(const float (&x)[N], const float (&y)[N], float over) noexcept
{
    if (over <= x[0]) return {y[0], 0.0f};
    for (size_t i = 1; i < N; ++i)
        if (over < x[i])
            return {y[i - 1] + (y[i] - y[i - 1]) * (over - x[i - 1]) / (x[i] - x[i - 1]),
                    (y[i] - y[i - 1]) / (x[i] - x[i - 1])};
    const float slope = (y[N - 1] - y[N - 2]) / (x[N - 1] - x[N - 2]);
    return {y[N - 1] + slope * (over - x[N - 1]), slope};
}

// The 2:1 knee has curvature that a single softplus/quadratic cannot follow.
// Nodes to over 22 are the knee fit (docs/multi-comp-2-bus-knee-2026-09-09.md);
// 24..40 are refitted on near-zero-leak (0.1 ms / 0.1 s) native sweeps to
// +90 dBFS at thresholds -15 and 0 (bus recalibration pass 2, 2026-09-10).
inline Drive twoToOneKneeCorrection(float over) noexcept
{
    static constexpr float knots[] = {-8, -4, -2, 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 28, 32, 36, 40};
    static constexpr float correction[] = {
        -0.0625746791f, 0.00482597678f, 0.163230086f, 0.2441113f,
        -0.0205606853f, -0.29017392f, -0.314523931f, 0.145520334f,
        1.22632276f, 0.849255571f, 0.132092162f, -0.312541902f,
        -0.415231217f, 0.00725999968f, 0.921038369f,
        2.98161387f, 9.40705204f, 21.5422306f, 41.1714401f, 70.0121231f
    };
    return nodeCorrection(knots, correction, over);
}

// 4:1 drive correction, fitted the same way. At high drive the native law
// approaches an amplitude-linear detector (d ln R / d over near ln10/20),
// which the softplus + quadratic form cannot follow.
inline Drive fourToOneCorrection(float over) noexcept
{
    static constexpr float knots[] = {-4, 0, 4, 8, 12, 16, 20, 24};
    static constexpr float correction[] = {0.0f, -0.0047555659f, 0.526297331f, 0.853223681f, 2.06527352f, 6.44655609f, 15.6572886f, 38.0347404f};
    return nodeCorrection(knots, correction, over);
}

// Feedback control-voltage law measured from the native reference console bus compressor, HR16.
// The ratio switch changes both detector bias and curvature. These coefficients
// operate on the compressed detector level in dBFS RMS; applying a feedforward
// (1 - 1/ratio) slope here would divide the compression a second time.
// 10:1 carries a measured slope scale and knee-weighted offset (same sweeps).
inline Drive drive(float detectorDb, float threshold, int ratio) noexcept
{
    struct Law { float taper, bias, slope, knee, curvature; };
    static constexpr Law laws[] = {
        {1.31881286f, -36.9566441f, 1.05645830f, 1.53259474f, 0.00234331807f},
        {1.32474543f, -31.6290932f, 1.22353614f, 0.18546719f, 0.16623485f},
        {1.32403956f, -27.5279309f, 6.77537908f, 0.04877920f, 0.37314398f}
    };
    constexpr float tenToOneScale = 1.03098309f, tenToOneOffset = 0.770660579f;
    const int ratioIndex = std::clamp(ratio, 0, 2);
    const auto& law = laws[ratioIndex];
    const float over = detectorDb - (law.taper * threshold + law.bias);
    const float tail = std::exp(-std::abs(over) / law.knee);
    const float knee = std::max(over, 0.0f) + law.knee * std::log1p(tail);
    const float kneeSlope = (over >= 0.0f ? 1.0f : tail) / (1.0f + tail);
    const float base = law.slope * knee + law.curvature * knee * knee;
    const float baseSlope = (law.slope + 2.0f * law.curvature * knee) * kneeSlope;
    Drive d{};
    if (ratioIndex == 0)
    {
        const Drive c = twoToOneKneeCorrection(over);
        d = {base + c.value, baseSlope + c.slope};
    }
    else if (ratioIndex == 1)
    {
        const Drive c = fourToOneCorrection(over);
        d = {base + c.value, baseSlope + c.slope};
    }
    else
    {
        const float weight = knee / (knee + 1.0f);
        d = {base * tenToOneScale + tenToOneOffset * weight,
             baseSlope * tenToOneScale + tenToOneOffset * kneeSlope / ((knee + 1.0f) * (knee + 1.0f))};
    }
    return d.value > 0.0f ? d : Drive{0.0f, 0.0f};
}

inline float reductionDb(float detectorDb, float threshold, int ratio) noexcept
{
    return drive(detectorDb, threshold, ratio).value;
}
} // namespace duskaudio::sslbus
