// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Measured control laws of the VCA mode's reference unit (UAD dbx 160,
// campaign: dusk-audio-tools plugins/MultiComp/tests/reference_comparison_dbx160).
// Parity is judged at matched knob positions, so the host parameters carry the
// knob positions and these functions carry the device's laws. Framework-free;
// shared by the DSP, the UI, and the tests.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace duskaudio::dbx160
{

// THRESHOLD: linear in dB across the knob, -55 dB full-left to 0 dB
// full-right (probe_laws.py text read-back, 41 points, exact to 0.05 dB).
inline constexpr float kThresholdMinDb = -55.0f;
inline constexpr float kThresholdMaxDb = 0.0f;
inline constexpr float kThresholdDefaultDb = -27.0f;   // reference default 0.509064

// COMPRESSION: knob position 0..100 (%) to the ratio the device APPLIES,
// measured as a 6 dB input step (-12 -> -6 dBFS, 15-21 dB over threshold)
// over the output step at 21 regular positions plus the held-out 97.5 %
// midpoint (probe_compress_law.py). The panel
// read-back rounds lower (text 4.0:1 where 4.14:1 is applied; 13.6 vs 15.9 at
// 90 %), so the measured table is the law. At the INF stop the reference's
// output actually falls slightly with input: from -12 to -6 dBFS it drops
// 0.070243 dB at each of three thresholds, i.e. a -0.011707 dB/dB slope or
// -85.418:1 (the read-out shows INF). Between points the ratio is interpolated
// linearly in 1/ratio (slope), the quantity a compressor applies.
struct CompressPoint { float position; float ratio; };
inline constexpr std::array<CompressPoint, 22> kCompressLaw{{
    {0.0f, 1.000f},
    {5.0f, 1.255f},
    {10.0f, 1.518f},
    {15.0f, 1.790f},
    {20.0f, 2.072f},
    {25.0f, 2.366f},
    {30.0f, 2.675f},
    {35.0f, 3.001f},
    {40.0f, 3.349f},
    {45.0f, 3.725f},
    {50.0f, 4.137f},
    {55.0f, 4.597f},
    {60.0f, 5.124f},
    {65.0f, 5.746f},
    {70.0f, 6.512f},
    {75.0f, 7.515f},
    {80.0f, 8.939f},
    {85.0f, 11.243f},
    {90.0f, 15.932f},
    {95.0f, 32.424f},
    {97.5f, 87.972f},
    {100.0f, -85.418f}}};
inline constexpr float kCompressDefaultPosition = 50.4944f;  // reference default 0.504944

inline constexpr float lawSlope(float ratio) noexcept
{
    return std::isinf(ratio) ? 0.0f : 1.0f / ratio;   // negative for the over-infinite stop
}

inline float compressSlope(float position) noexcept
{
    const float p = std::clamp(position, 0.0f, 100.0f);
    for (size_t i = 1; i < kCompressLaw.size(); ++i)
    {
        const auto& a = kCompressLaw[i - 1];
        const auto& b = kCompressLaw[i];
        if (p <= b.position)
        {
            const float t = (p - a.position) / (b.position - a.position);
            const float sa = lawSlope(a.ratio);
            const float sb = lawSlope(b.ratio);
            return sa + (sb - sa) * t;
        }
    }
    return lawSlope(kCompressLaw.back().ratio);
}

// Ratio for a knob position; reads as infinite from the point the slope
// reaches zero (the stop region, where the device slightly over-reduces).
inline float compressRatio(float position) noexcept
{
    const float slope = compressSlope(position);
    return slope > 0.0f ? 1.0f / slope : std::numeric_limits<float>::infinity();
}

// Knob position for a ratio (inverse of the law); used to place ring labels
// and to convert legacy ratio presets.
inline float compressPosition(float ratio) noexcept
{
    if (!(ratio > 1.0f)) return 0.0f;
    if (std::isinf(ratio)) return 100.0f;
    const float slope = 1.0f / ratio;
    for (size_t i = 1; i < kCompressLaw.size(); ++i)
    {
        const auto& a = kCompressLaw[i - 1];
        const auto& b = kCompressLaw[i];
        const float sa = lawSlope(a.ratio);
        const float sb = lawSlope(b.ratio);
        if (slope <= sa && slope >= sb)
        {
            const float t = (sa - sb) > 0.0f ? (sa - slope) / (sa - sb) : 0.0f;
            return a.position + (b.position - a.position) * t;
        }
    }
    return 100.0f;
}

// The reference's RMS detector is not exactly linear in dB. Dense 1 dB
// sweeps at threshold positions 0, 0.25 and 0.509064 showed that the
// ratio-normalised residual follows absolute input level, not threshold or
// ratio: the same correction fitted at 4:1, combined with the directly
// measured Inf-stop slope above, predicts the held-out Inf curves within
// 0.017 dB. These points are a reduced linear interpolation of that 4:1 fit.
// Below the measured onset the correction is neutral; above the measured
// range it is held rather than extrapolated.
struct DetectorCalibrationPoint { float levelDb; float correctionDb; };
inline constexpr std::array<DetectorCalibrationPoint, 18> kDetectorCalibration{{
    {-56.0f, 0.0000f}, {-55.0f, 0.4237f}, {-54.0f, 0.1705f},
    {-52.0f, -0.2395f}, {-50.0f, -0.5287f}, {-48.0f, -0.7115f},
    {-46.0f, -0.8128f}, {-44.0f, -0.8555f}, {-42.0f, -0.8425f},
    {-40.0f, -0.7701f}, {-36.0f, -0.6011f}, {-32.0f, -0.3873f},
    {-28.0f, -0.1744f}, {-24.0f, 0.0282f}, {-20.0f, 0.1538f},
    {-16.0f, 0.2235f}, {-14.0f, 0.2401f}, {0.0f, 0.2401f},
}};

inline float detectorCorrectionDb(float inputPeakDb) noexcept
{
    if (!std::isfinite(inputPeakDb)
        || inputPeakDb <= kDetectorCalibration.front().levelDb)
        return 0.0f;
    for (size_t i = 1; i < kDetectorCalibration.size(); ++i)
    {
        const auto& a = kDetectorCalibration[i - 1];
        const auto& b = kDetectorCalibration[i];
        if (inputPeakDb <= b.levelDb)
        {
            const float t = (inputPeakDb - a.levelDb) / (b.levelDb - a.levelDb);
            return a.correctionDb + (b.correctionDb - a.correctionDb) * t;
        }
    }
    return kDetectorCalibration.back().correctionDb;
}

// OUTPUT GAIN: linear -20..+20 dB (probe_gain_law.py: matches the read-back to
// 0.003 dB at every position) -- identical to the existing vca_output law.

// SC FILTER: the reference's sidechain filter is an on/off switch whose
// engaged response, measured as detector sensitivity from 40 Hz to 20 kHz
// (probe_sc_mix_laws.py + probe_sc_hf.py), is a half-order tilt close to
// |H(f)| = sqrt(f / 276 Hz) -- -8.6 dB at 40 Hz, +10.4 dB at 3 kHz, still
// rising to +19.4 dB at 20 kHz. A cascade of seven first-order sections
// (zeros at 7 * 5^k Hz, each pole 2.25x above its zero; corners above Nyquist
// clamp to flat) fitted directly in the bilinear digital domain matches the
// fifteen measured points to 0.12 dB RMSE (worst 0.29 dB at 12 kHz), and is
// normalised to unity at 276 Hz.
inline constexpr float kSidechainTiltUnityHz = 276.0f;
inline constexpr int kSidechainTiltSections = 7;
inline constexpr float kSidechainTiltFirstZeroHz = 7.0f;
inline constexpr float kSidechainTiltZeroSpacing = 5.0f;
inline constexpr float kSidechainTiltPoleRatio = 2.25f;

// The measured engaged response, in detector dB relative to 276 Hz, for tests.
struct SidechainTiltPoint { float hz; float db; };
inline constexpr std::array<SidechainTiltPoint, 15> kSidechainTiltMeasured{{
    {40.0f, -8.62f}, {60.0f, -6.74f}, {80.0f, -5.37f}, {100.0f, -4.32f}, {150.0f, -2.55f},
    {200.0f, -1.39f}, {300.0f, 0.28f}, {500.0f, 2.60f}, {1000.0f, 5.71f}, {3000.0f, 10.40f},
    {5000.0f, 12.85f}, {8000.0f, 14.91f}, {12000.0f, 16.66f}, {16000.0f, 18.15f}, {20000.0f, 19.35f}}};

class SidechainTilt
{
public:
    void prepare(double sampleRate) noexcept
    {
        const double fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        const double pi = 3.14159265358979323846;
        double gainAtUnity = 1.0;
        for (int i = 0; i < kSidechainTiltSections; ++i)
        {
            const double zeroHz = kSidechainTiltFirstZeroHz * std::pow(static_cast<double>(kSidechainTiltZeroSpacing), i);
            const double poleHz = kSidechainTiltPoleRatio * zeroHz;
            // Bilinear transform of (1 + s/wz) / (1 + s/wp) with the usual
            // frequency pre-warping of both corners.
            const double kz = std::tan(pi * std::min(zeroHz, 0.49 * fs) / fs);
            const double kp = std::tan(pi * std::min(poleHz, 0.49 * fs) / fs);
            const double b0 = (1.0 + 1.0 / kz) / (1.0 + 1.0 / kp);
            const double b1 = (1.0 - 1.0 / kz) / (1.0 + 1.0 / kp);
            const double a1 = (1.0 - 1.0 / kp) / (1.0 + 1.0 / kp);
            sections[static_cast<size_t>(i)] = {static_cast<float>(b0), static_cast<float>(b1),
                                                static_cast<float>(a1), 0.0f, 0.0f};
            // Analogue magnitude at the unity frequency for normalisation.
            const double wu = kSidechainTiltUnityHz;
            gainAtUnity *= std::sqrt((1.0 + (wu / zeroHz) * (wu / zeroHz))
                                     / (1.0 + (wu / poleHz) * (wu / poleHz)));
        }
        normalise = static_cast<float>(1.0 / gainAtUnity);
        reset();
    }

    void reset() noexcept
    {
        for (auto& s : sections) { s.x1 = 0.0f; s.y1 = 0.0f; }
    }

    float process(float x) noexcept
    {
        for (auto& s : sections)
        {
            const float y = s.b0 * x + s.b1 * s.x1 - s.a1 * s.y1;
            s.x1 = x;
            s.y1 = y;
            x = y;
        }
        return x * normalise;
    }

private:
    struct Section { float b0 = 1.0f, b1 = 0.0f, a1 = 0.0f, x1 = 0.0f, y1 = 0.0f; };
    std::array<Section, static_cast<size_t>(kSidechainTiltSections)> sections{};
    float normalise = 1.0f;
};

} // namespace duskaudio::dbx160
