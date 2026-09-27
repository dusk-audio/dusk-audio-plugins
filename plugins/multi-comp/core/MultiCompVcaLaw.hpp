// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Measured control laws of the VCA mode's reference unit (reference VCA compressor plugin,
// campaign: the VCA reference-comparison harness in dusk-audio-tools).
// Parity is judged at matched knob positions, so the host parameters carry the
// knob positions and these functions carry the device's laws. Framework-free;
// shared by the DSP, the UI, and the tests.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>

namespace duskaudio::vcaLaw
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

// DETECTOR ONSET: the reference's RMS state cannot climb faster than a fixed
// rate, and it climbs from a floor. Stepping a 1 kHz tone out of digital
// silence, its gain stays at unity for 0.54 ms at any step level from -24 to
// 0 dBFS (26-28 samples at 48 kHz, 52-55 at 96 kHz); a pedestal far below
// threshold shortens that (8 samples from -60 dBFS, 2-3 from -50), and for
// several milliseconds afterwards the unit reduces as if the energy of that
// first half-millisecond had never arrived (-2.3 / -1.1 / -0.7 dB less GR over
// the next three cycles). One rule reproduces all of it: the power
// integrator's state rises by at most kDetectorRiseDbPerMs, from
// kDetectorFloorDb, and energy beyond the limit is discarded. Fitted
// 2026-09-11 to 28 stepped probes (dusk-audio-tools
// plugins/MultiComp/tests/programme_parity/vca: fit_onset_state.py): first
// cycle within 0.9 dB, the next three within 0.2 dB (previously up to 13 dB
// and 3.8 dB), confirmed unchanged in time at 96 kHz. Steady state, release
// and steps from a -40 dBFS pedestal are unaffected: the limit binds only
// while the state is far below the arriving power.
inline constexpr float kDetectorRiseDbPerMs = 84.5467219f;
inline constexpr float kDetectorFloorDb = -91.1694352f;

// OUTPUT GAIN: linear -20..+20 dB (probe_gain_law.py: matches the read-back to
// 0.003 dB at every position) -- identical to the existing vca_output law.

// SC FILTER: the reference's sidechain filter is an on/off switch whose
// engaged response is a half-order tilt, roughly |H(f)| = sqrt(f / 290 Hz),
// that levels off above ~20 kHz. Remeasured 2026-09-11 at 44.1, 48 and 96 kHz
// (dusk-audio-tools plugins/MultiComp/tests/programme_parity/vca: tilt_all.py,
// tilt_rate.py): each probe tone's gain reduction on a -60 dBFS pilot is mapped
// back to a detector level through the same unit's own SC-Off level sweep, at
// two tone levels 12 dB apart (identical results, so the tilt is linear). The
// same procedure recovers our own known design to 0.00 dB above 700 Hz; its
// small low-frequency bias (+0.17 dB at 40 Hz, detector ripple) is removed
// from the reference values below. The earlier campaign table (probe_sc_hf.py)
// read 0.3-0.7 dB high at both ends, and the old ladder's top section, with its
// pole clamped just under Nyquist, rose +4.9 dB above the reference at 23.5 kHz,
// so ultrasonic content over-drove the detector (a -12 dB snare with 14 % of
// its energy above 20 kHz lost 3-4 dB more than the reference).
//
// Realisation: eight first-order sections fitted jointly to the three rates,
// each designed at the host rate with a matched-Z pole and its zero solved so
// the section's gain at Nyquist equals the analogue section's gain at fs / 2
// (a magnitude-matched one-pole shelf). No corner is clamped, so nothing folds
// up against Nyquist at any rate. Residual against the reference: <= 0.26 dB
// to 20 kHz and <= 0.27 dB to 23.5 kHz at 48 kHz, <= 0.27 / 0.31 dB at 44.1 kHz,
// <= 0.45 dB at 96 kHz (the reference's own in-band shape shifts ~0.2 dB with
// rate). Normalised digitally at the fitted unity frequency.
inline constexpr double kSidechainTiltUnityHz = 286.637944;
inline constexpr int kSidechainTiltSections = 8;
inline constexpr std::array<float, 8> kSidechainTiltZeroHz{{
    0.91690101f, 4.40563334f, 59.7318946f, 313.543966f,
    2045.29265f, 18816.0346f, 19927.1537f, 21334.9133f}};
inline constexpr std::array<float, 8> kSidechainTiltPoleHz{{
    2.42038204f, 36.9131747f, 127.332884f, 806.213569f,
    4974.03307f, 25503.6051f, 31301.2147f, 29317.4969f}};

// The reference's engaged response at 48 kHz, in detector dB relative to the
// switch-out (flat) detector, for tests (2026-09-11 remeasurement above).
struct SidechainTiltPoint { float hz; float db; };
inline constexpr std::array<SidechainTiltPoint, 25> kSidechainTiltMeasured{{
    {40.0f, -9.50f}, {60.0f, -7.21f}, {100.0f, -4.55f}, {150.0f, -2.71f}, {200.0f, -1.51f},
    {276.0f, -0.18f}, {400.0f, 1.47f}, {700.0f, 4.07f}, {1000.0f, 5.69f}, {1500.0f, 7.32f},
    {2000.0f, 8.50f}, {3000.0f, 10.32f}, {5000.0f, 12.69f}, {7000.0f, 14.12f}, {8000.0f, 14.66f},
    {10000.0f, 15.54f}, {12000.0f, 16.28f}, {14000.0f, 16.98f}, {16000.0f, 17.64f}, {18000.0f, 18.23f},
    {20000.0f, 18.72f}, {21000.0f, 18.91f}, {22000.0f, 19.05f}, {23000.0f, 19.13f}, {23500.0f, 19.15f}}};

class SidechainTilt
{
public:
    void prepare(double sampleRate) noexcept
    {
        const double fs = sampleRate > 0.0 ? sampleRate : 44100.0;
        const double pi = 3.14159265358979323846;
        const std::complex<double> zu = std::polar(1.0, -2.0 * pi * kSidechainTiltUnityHz / fs);
        std::complex<double> atUnity = 1.0;
        for (int i = 0; i < kSidechainTiltSections; ++i)
        {
            const double zh = kSidechainTiltZeroHz[static_cast<size_t>(i)];
            const double ph = kSidechainTiltPoleHz[static_cast<size_t>(i)];
            // H(z) = g (1 - q z^-1) / (1 - pz z^-1): pole matched to the
            // analogue pole, DC gain 1, Nyquist gain equal to the analogue
            // section's gain at fs / 2.
            const double pz = std::exp(-2.0 * pi * ph / fs);
            const double nyquistGain = std::sqrt((1.0 + (0.5 * fs / zh) * (0.5 * fs / zh))
                                               / (1.0 + (0.5 * fs / ph) * (0.5 * fs / ph)));
            const double a = nyquistGain * (1.0 + pz) / (1.0 - pz);
            const double q = (a - 1.0) / (a + 1.0);
            const double g = (1.0 - pz) / (1.0 - q);
            sections[static_cast<size_t>(i)] = {static_cast<float>(g), static_cast<float>(-g * q),
                                                static_cast<float>(-pz), 0.0f, 0.0f};
            atUnity *= (g - g * q * zu) / (1.0 - pz * zu);
        }
        normalise = static_cast<float>(1.0 / std::abs(atUnity));
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

// OUTPUT VOICING: native impulse at 48 kHz (setting-independent +0.13 dB
// around 10 kHz), plus pilot-normalised SC-On HF tone probes at 44.1/96 kHz.
// Fitted 2026-09-16: vca-programme-20260911/vca-voicing-families.json,
// fit_vca_linear_eq.py, exp_postfilter_score.py and make_vca_voicing_core.py.
// Maximum residual: 48 kHz family 0.008 dB to 20 kHz; 96 kHz family
// 0.026 dB to 44 kHz; 48 kHz family at a 44.1 kHz host 0.047 dB.
// The reference curve scales with host Nyquist at 44.1/48 kHz: this is a
// host-rate digital response. An oversampled analogue match would miss it
// at 44.1 kHz, so this stage runs AFTER downsampling, at the host rate.
struct OutputVoicingFamily { double peakHz; double gainDb; double q; double lowpassHz; };
inline constexpr OutputVoicingFamily kOutputVoicing48{
    12302.077765110202, 0.19377605455040672, 0.40418487576463935, 129578.02520201266};
inline constexpr OutputVoicingFamily kOutputVoicing96{
    10021.447091993265, 0.2151958011836086, 0.9311700023107351, 118902.54589407815};

class OutputVoicing
{
public:
    void prepare(double hostRate) noexcept
    {
        const double fs = std::isfinite(hostRate) && hostRate > 0.0 ? hostRate : 48000.0;
        const auto& family = fs >= 70000.0 ? kOutputVoicing96 : kOutputVoicing48;
        const double pi = 3.14159265358979323846;
        // RBJ peaking section. Preserve the lab's double-precision arithmetic
        // and evaluation order, including its 1 kHz magnitude normalisation.
        const double A = std::pow(10.0, family.gainDb / 40.0);
        const double w = 2.0 * pi * std::min(family.peakHz, 0.49 * fs) / fs;
        const double al = std::sin(w) / (2.0 * family.q);
        const double a0 = 1.0 + al / A;
        b[0] = (1.0 + al * A) / a0;
        b[1] = -2.0 * std::cos(w) / a0;
        b[2] = (1.0 - al * A) / a0;
        a[0] = 1.0;
        a[1] = -2.0 * std::cos(w) / a0;
        a[2] = (1.0 - al / A) / a0;

        // Magnitude-matched one-pole LP: matched pole, unity DC gain, and
        // Nyquist gain equal to the analogue low-pass magnitude at fs / 2.
        const double fc = family.lowpassHz;
        const double pz = std::exp(-2.0 * pi * fc / fs);
        const double ny = 1.0 / std::sqrt(1.0 + (0.5 * fs / fc) * (0.5 * fs / fc));
        const double Am = ny * (1.0 + pz) / (1.0 - pz);
        const double qz = (Am - 1.0) / (Am + 1.0);
        const double g = (1.0 - pz) / (1.0 - qz);
        lpB[0] = g;
        lpB[1] = -g * qz;
        lpA1 = -pz;

        // Static-law and headroom measurements are calibrated at 1 kHz.
        const double wr = 2.0 * pi * 1000.0 / fs;
        const double cr = std::cos(wr), sr = std::sin(wr);
        const double c2 = std::cos(2 * wr), s2 = std::sin(2 * wr);
        const double nr = b[0] + b[1] * cr + b[2] * c2;
        const double ni = -(b[1] * sr + b[2] * s2);
        const double dr = 1.0 + a[1] * cr + a[2] * c2;
        const double di = -(a[1] * sr + a[2] * s2);
        const double lnr = lpB[0] + lpB[1] * cr, lni = -lpB[1] * sr;
        const double ldr = 1.0 + lpA1 * cr, ldi = -lpA1 * sr;
        const double mag = std::sqrt((nr * nr + ni * ni) / (dr * dr + di * di)
                                    * (lnr * lnr + lni * lni) / (ldr * ldr + ldi * ldi));
        for (double& coefficient : b) coefficient /= mag;
        reset();
    }

    void reset() noexcept
    {
        px1 = px2 = py1 = py2 = lx1 = ly1 = 0.0;
    }

    float process(float input) noexcept
    {
        const double x = input;
        const double y = b[0] * x + b[1] * px1 + b[2] * px2 - a[1] * py1 - a[2] * py2;
        px2 = px1; px1 = x; py2 = py1; py1 = y;
        const double z = lpB[0] * y + lpB[1] * lx1 - lpA1 * ly1;
        lx1 = y; ly1 = z;
        return static_cast<float>(z);
    }

private:
    std::array<double, 3> b{{1.0, 0.0, 0.0}}, a{{1.0, 0.0, 0.0}};
    std::array<double, 2> lpB{{1.0, 0.0}};
    double lpA1 = 0.0;
    double px1 = 0.0, px2 = 0.0, py1 = 0.0, py2 = 0.0, lx1 = 0.0, ly1 = 0.0;
};

} // namespace duskaudio::vcaLaw
