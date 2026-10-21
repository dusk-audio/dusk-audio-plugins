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
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

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

// 2026-10-11: independent Q FIT 1 kHz positive input levels, two ratios
// and thresholds. Correction increments coincide to 0.002252 dB; the old
// hold misses 2.361935 dB at +15 dB. Preserve the old domain through 0 dB.
inline constexpr float kHotDetectorCalibrationStrength = 1.0f;
inline constexpr std::array<DetectorCalibrationPoint, 6> kHotDetectorCalibration{{
    {0.0f, 0.2401f},
    {3.0f, 0.3871333165921654f},
    {6.0f, 0.6474051045335686f},
    {9.0f, 1.0665004629175474f},
    {12.0f, 1.6975348429495067f},
    {15.0f, 2.602034570012348f}
}};

// 2026-10-11: independent 48/96 kHz SC-off controls above +15 dB.
// Holding the old endpoint misses 5.307492 dB of detector correction at
// +24 dB. The continued increments agree across two rates, thresholds,
// ratios and output gains within 0.004105 dB; output-gain control 2.21e-7 dB.
// Fresh +23/+24/+25/+27 dB controls extend the outer derivative; the
// rate/threshold/ratio/gain increment spread stays below 0.005709 dB.
// Keep every existing calibration point through +15 dB unchanged.
inline constexpr float kHotDetectorExtensionStrength = 1.0f;
inline constexpr std::array<DetectorCalibrationPoint, 7> kHotDetectorExtension{{
    {15.0f, 2.602034570012348f},
    {18.0f, 3.862056652019314f},
    {21.0f, 5.5854280533920875f},
    {23.0f, 7.055962570744926f},
    {24.0f, 7.909526279858181f},
    {25.0f, 8.84947339770055f},
    {27.0f, 11.03193047061749f},
}};

// Every calibration knot above sits on a whole dB, so the segment holding a
// level x is the one ending at the first knot at or above ceil(x). The three
// tables join into one row per whole dB from -55 to +27 (base below 0, hot up
// to +15, the extension above), each row holding its segment's two knots; the
// interpolation is the arithmetic the per-table linear searches ran, so the
// result is the same float, and a four-lane form (MultiCompModes) can read it.
struct DetectorSegment { float aLevel, aCorrection, bLevel, bCorrection; };

template <size_t N>
constexpr bool knotsOnWholeDb(const std::array<DetectorCalibrationPoint, N>& knots)
{
    for (size_t i = 0; i < N; ++i)
        if (knots[i].levelDb != static_cast<float>(static_cast<int>(knots[i].levelDb))
            || (i > 0 && !(knots[i].levelDb > knots[i - 1].levelDb)))
            return false;
    return N >= 2;
}

static_assert(knotsOnWholeDb(kDetectorCalibration) && knotsOnWholeDb(kHotDetectorCalibration)
                  && knotsOnWholeDb(kHotDetectorExtension),
              "detector calibration knots must be increasing whole dB values");
static_assert(kHotDetectorCalibrationStrength != 0.0f && kHotDetectorExtensionStrength != 0.0f,
              "the joined segment table assumes both hot tables are on");
static_assert(kDetectorCalibration.back().levelDb == 0.0f && kHotDetectorCalibration.front().levelDb == 0.0f
                  && kHotDetectorCalibration.back().levelDb == kHotDetectorExtension.front().levelDb,
              "the three tables must meet at 0 dB and at the extension's start");

inline constexpr int kDetectorSegmentLowest =
    static_cast<int>(kDetectorCalibration.front().levelDb) + 1;                         // -55
inline constexpr int kDetectorSegmentHighest = static_cast<int>(kHotDetectorExtension.back().levelDb);   // +27

template <size_t N>
constexpr void addSegments(std::array<DetectorSegment, kDetectorSegmentHighest - kDetectorSegmentLowest + 1>& rows,
                           const std::array<DetectorCalibrationPoint, N>& knots)
{
    for (int level = static_cast<int>(knots[0].levelDb) + 1;
         level <= static_cast<int>(knots[N - 1].levelDb); ++level)
    {
        size_t i = 1;
        while (static_cast<float>(level) > knots[i].levelDb) ++i;
        rows[static_cast<size_t>(level - kDetectorSegmentLowest)]
            = {knots[i - 1].levelDb, knots[i - 1].correctionDb, knots[i].levelDb, knots[i].correctionDb};
    }
}

constexpr std::array<DetectorSegment, kDetectorSegmentHighest - kDetectorSegmentLowest + 1> detectorSegments()
{
    std::array<DetectorSegment, kDetectorSegmentHighest - kDetectorSegmentLowest + 1> rows{};
    addSegments(rows, kDetectorCalibration);
    addSegments(rows, kHotDetectorCalibration);
    addSegments(rows, kHotDetectorExtension);
    return rows;
}

inline constexpr auto kDetectorSegments = detectorSegments();

// Interpolation inside a segment.
inline float detectorSegmentCorrectionDb(const DetectorSegment& s, float levelDb) noexcept
{
    const float t = (levelDb - s.aLevel) / (s.bLevel - s.aLevel);
    return s.aCorrection + (s.bCorrection - s.aCorrection) * t;
}

// Above the extension's last knot: its outer secant continued.
inline float detectorExtrapolatedCorrectionDb(float inputPeakDb) noexcept
{
    // Continue the measured outer secant; a held endpoint would
    // remove half the local derivative from an RMS ripple.
    // The continuation beyond +27 dB has not been characterized.
    const auto& a = kHotDetectorExtension[kHotDetectorExtension.size() - 2];
    const auto& b = kHotDetectorExtension.back();
    return b.correctionDb + (inputPeakDb - b.levelDb)
        * (b.correctionDb - a.correctionDb) / (b.levelDb - a.levelDb);
}

inline float detectorCorrectionDb(float inputPeakDb) noexcept
{
    if (!std::isfinite(inputPeakDb)
        || inputPeakDb <= kDetectorCalibration.front().levelDb)
        return 0.0f;
    if (inputPeakDb > kHotDetectorExtension.back().levelDb)
        return detectorExtrapolatedCorrectionDb(inputPeakDb);
    const int row = static_cast<int>(std::ceil(inputPeakDb)) - kDetectorSegmentLowest;
    return detectorSegmentCorrectionDb(kDetectorSegments[static_cast<size_t>(row)], inputPeakDb);
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

// VCA input coupling, measured 2026-10-11 (FIT family D and independent
// polarity placement probes). One pole at 4.600266 Hz: maximum complex-fit
// residual 0.000510 dB / 0.00571 degrees over 1--50 Hz at three levels.
// Mix's dry path is untouched. Pre-gain placement residual 0.00398 versus
// 0.06710 after the gain cell, normalized to the low-tone input amplitude.
// Enabled after the complete disabled-element byte-identity control.
// Strength zero retains the original sample path for repeatable controls.
inline constexpr float kAudioCouplingStrength = 1.0f;
inline constexpr double kAudioCouplingHz = 4.600266242421623;
// 2026-10-11 baseline lifecycle controls: retained RMS caused 2.890826e-6
// prepare mismatch; dormant audio/routing state caused 0.000405745 mono
// return mismatch. Isolated clears give exact equality, including 24
// wet/dry, bypass, listen and lookahead cases. No active-signal classifier.
inline constexpr bool kLifecycleResetEnabled = true;

// 2026-10-11 FIT H: one independent detector pole explains the complete
// half-cycle shapes at 15--45 Hz, both levels and ratios, to 0.0100 dB.
// Complex response residual: 0.002591 dB / 0.01660 degrees. The detector
// branches before the audio pole; adding that pole gives 0.643 dB / 5.62
// degree residual. The existing 35 ms RMS integrator and gain law stay put.
inline constexpr float kDetectorCouplingStrength = 1.0f;
inline constexpr double kDetectorCouplingHz = 20.02072895668114;

// Native-rate VCA coupling section. Bilinear first-order response; its very
// low corner is specified in Hz independently of the oversampling factor.
class LowFrequencyHighPass
{
public:
    void prepare(double rate, double cornerHz) noexcept
    {
        const double fs = std::isfinite(rate) && rate > 0.0 ? rate : 48000.0;
        const double k = std::tan(3.14159265358979323846 * cornerHz / fs);
        feed = 1.0 / (1.0 + k);
        feedback = (1.0 - k) / (1.0 + k);
        reset();
    }

    void reset() noexcept { previousInput = previousOutput = 0.0; }

    float process(float input, float strength) noexcept
    {
        if (strength == 0.0f) return input;
        const double y = feed * (static_cast<double>(input) - previousInput)
                       + feedback * previousOutput;
        previousInput = input;
        previousOutput = y;
        const float filtered = static_cast<float>(y);
        return strength == 1.0f ? filtered : input + strength * (filtered - input);
    }

    // process() on a left and a right filter over a block, in place. Each lane
    // performs process()'s double arithmetic; the pair shares the instructions.
    static void processPair(LowFrequencyHighPass& left, LowFrequencyHighPass& right,
                            float* x0, float* x1, int n, float strength) noexcept
    {
        if (strength == 0.0f) return;
#if defined(__aarch64__)
        const float64x2_t feed = {left.feed, right.feed};
        const float64x2_t feedback = {left.feedback, right.feedback};
        float64x2_t previousInput = {left.previousInput, right.previousInput};
        float64x2_t previousOutput = {left.previousOutput, right.previousOutput};
        for (int i = 0; i < n; ++i)
        {
            const float32x2_t input = {x0[i], x1[i]};
            const float64x2_t in = vcvt_f64_f32(input);
            const float64x2_t y = vaddq_f64(vmulq_f64(feed, vsubq_f64(in, previousInput)),
                                            vmulq_f64(feedback, previousOutput));
            previousInput = in;
            previousOutput = y;
            float32x2_t filtered = vcvt_f32_f64(y);
            if (strength != 1.0f)
                filtered = vadd_f32(input, vmul_n_f32(vsub_f32(filtered, input), strength));
            x0[i] = vget_lane_f32(filtered, 0);
            x1[i] = vget_lane_f32(filtered, 1);
        }
        left.previousInput = vgetq_lane_f64(previousInput, 0);
        right.previousInput = vgetq_lane_f64(previousInput, 1);
        left.previousOutput = vgetq_lane_f64(previousOutput, 0);
        right.previousOutput = vgetq_lane_f64(previousOutput, 1);
#else
        for (int i = 0; i < n; ++i)
        {
            x0[i] = left.process(x0[i], strength);
            x1[i] = right.process(x1[i], strength);
        }
#endif
    }

private:
    double feed = 1.0, feedback = 0.0;
    double previousInput = 0.0, previousOutput = 0.0;
};

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



// 2026-10-11 joint 44.1/48/96 kHz FIT response. The earlier 48 kHz-only
// realization missed the new 96 kHz C table by 2.374 dB. The three-rate
// response and independent short-pulse FIT residuals are documented in
// rate-followup/impulse-fit/projected-blend-fit.json.
// The lowest three measured sections and independent 20.020729 Hz pole
// preserve Q's LF magnitude/phase. Higher poles use the common bilinear
// mapping; no corner is clamped or selected from a host's Nyquist.
// P's measured 49.348321 ms linear transition is unchanged.
inline constexpr float kScResponseStrength = 1.0f;
inline constexpr double kScTransitionMs = 49.34832034753578;
inline constexpr std::array<double, 8> kScResponseZeroHz{{
    5.756520018445299, 7.58503667171528, 45.27084620441695, 1266.126284916478,
    311.6317223037134, 2828.6456577583094, 21804.85412256131, 27890.82245889833}};
inline constexpr std::array<double, 8> kScResponsePoleHz{{
    7.809181475826136, 16.760703575029417, 119.57010598264047, 751.986944483175,
    1630.4376516346435, 7727.690864281121, 33403.22353937673, 40420.16378298945}};
inline constexpr double kScResponseGainDb = 0.041299182276151536;

// 2026-10-12: normalized SC ladder identified jointly from steady response
// and independent short-pulse FIT data; no held DC/music/blind data fitted.
// Corners are fractions of the host rate, with one bilinear realization.
// The existing 20.020729 Hz detector pole is the fixed-Hz low-end part.
// FIT magnitude residual 0.032699 dB; LF phase 0.025538 degrees.
// Frozen-fit rate judgments: 88.2 kHz 0.044970 dB, 192 kHz
// 0.045211 dB. Y pulse residual 0.313651 dB. The transition is unchanged.
// Zero retains the previous response exactly for the complete off control.
inline constexpr float kRateScaledScStrength = 1.0f;
inline constexpr std::array<double, 8> kRateScaledScZeroFraction{{
    0.00011748757161015158, 0.00038202801916710044, 0.00094843173487066, 0.007133710397707068,
    0.007121114034996163, 0.04499253549690136, 0.1273179770248589, 0.32087446046258616}};
inline constexpr std::array<double, 8> kRateScaledScPoleFraction{{
    0.0003468442101471412, 0.0003893337437380088, 0.0025004424908011743, 0.016959087747612938,
    0.007824830148745156, 0.11422228160671877, 0.13373338386774883, 0.4968844303753111}};
inline constexpr double kRateScaledScGainDb = -19.501585521342495;

class SidechainResponse
{
public:
    void prepare(double sampleRate) noexcept
    {
        const double fs = std::isfinite(sampleRate) && sampleRate > 0.0
                            ? sampleRate : 48000.0;
        constexpr double pi = 3.14159265358979323846;
        if constexpr (kRateScaledScStrength != 0.0f)
        {
            for (size_t i = 0; i < sections.size(); ++i)
            {
                const double kp = pi * kRateScaledScPoleFraction[i];
                const double kz = pi * kRateScaledScZeroFraction[i];
                const double pole = (1.0 - kp) / (1.0 + kp);
                const double zero = (1.0 - kz) / (1.0 + kz);
                auto& section = sections[i];
                section.b0 = (1.0 - pole) / (1.0 - zero);
                section.b1 = -section.b0 * zero;
                section.a1 = -pole;
            }
            scale = std::pow(10.0, kRateScaledScGainDb / 20.0)
                  * std::sqrt(fs / 48000.0);
            step = 1.0 / (fs * kScTransitionMs * 0.001);
            reset();
            return;
        }
        const std::complex<double> atUnity =
            std::polar(1.0, -2.0 * pi * kSidechainTiltUnityHz / fs);
        std::complex<double> responseAtUnity{1.0, 0.0};
        for (size_t i = 0; i < sections.size(); ++i)
        {
            const double zeroHz = kScResponseZeroHz[i];
            const double poleHz = kScResponsePoleHz[i];
            const double k = pi * poleHz / fs;
            const double pole = i < 3 ? std::exp(-2.0 * k)
                                      : (1.0 - k) / (1.0 + k);
            const double nyquistGain = std::sqrt(
                (1.0 + std::pow(0.5 * fs / zeroHz, 2.0))
                / (1.0 + std::pow(0.5 * fs / poleHz, 2.0)));
            const double a = nyquistGain * (1.0 + pole) / (1.0 - pole);
            const double zero = (a - 1.0) / (a + 1.0);
            auto& section = sections[i];
            section.b0 = (1.0 - pole) / (1.0 - zero);
            section.b1 = -section.b0 * zero;
            section.a1 = -pole;
            responseAtUnity *= (section.b0 + section.b1 * atUnity)
                             / (1.0 + section.a1 * atUnity);
        }
        scale = std::pow(10.0, kScResponseGainDb / 20.0)
              / std::abs(responseAtUnity);
        step = 1.0 / (fs * kScTransitionMs * 0.001);
        reset();
    }

    void reset() noexcept
    {
        for (auto& section : sections)
            section.state = 0.0;
        weight = 0.0;
        initialized = false;
    }

    float process(float input, bool engaged) noexcept
    {
        const double target = engaged ? 1.0 : 0.0;
        if (!initialized)
        {
            weight = target;
            initialized = true;
        }
        if (weight < target)
            weight = std::min(target, weight + step);
        else if (weight > target)
            weight = std::max(target, weight - step);

        double filtered = input;
        for (auto& section : sections)
        {
            const double y = section.b0 * filtered + section.state;
            section.state = section.b1 * filtered - section.a1 * y;
            filtered = y;
        }
        return static_cast<float>(static_cast<double>(input)
                                  + weight * (filtered * scale - input));
    }

    // process() on a left and a right response over a block, in place. Each
    // lane performs process()'s double arithmetic; the pair shares the instructions.
    static void processPair(SidechainResponse& left, SidechainResponse& right,
                            float* x0, float* x1, int n, bool engaged) noexcept
    {
        if (n <= 0) return;
#if defined(__aarch64__)
        const double targetValue = engaged ? 1.0 : 0.0;
        for (SidechainResponse* r : {&left, &right})
            if (!r->initialized)
            {
                r->weight = targetValue;
                r->initialized = true;
            }
        constexpr size_t kSections = kSectionCount;
        float64x2_t b0[kSections], b1[kSections], a1[kSections], state[kSections];
        for (size_t k = 0; k < kSections; ++k)
        {
            b0[k] = float64x2_t{left.sections[k].b0, right.sections[k].b0};
            b1[k] = float64x2_t{left.sections[k].b1, right.sections[k].b1};
            a1[k] = float64x2_t{left.sections[k].a1, right.sections[k].a1};
            state[k] = float64x2_t{left.sections[k].state, right.sections[k].state};
        }
        const float64x2_t target = vdupq_n_f64(targetValue);
        const float64x2_t step = {left.step, right.step};
        const float64x2_t scale = {left.scale, right.scale};
        float64x2_t weight = {left.weight, right.weight};
        for (int i = 0; i < n; ++i)
        {
            // weight < target: min(target, weight + step); weight > target: max(target, weight - step)
            const float64x2_t rising = vminq_f64(target, vaddq_f64(weight, step));
            const float64x2_t falling = vmaxq_f64(target, vsubq_f64(weight, step));
            weight = vbslq_f64(vcltq_f64(weight, target), rising,
                               vbslq_f64(vcgtq_f64(weight, target), falling, weight));
            const float64x2_t input = vcvt_f64_f32(float32x2_t{x0[i], x1[i]});
            float64x2_t filtered = input;
            for (size_t k = 0; k < kSections; ++k)
            {
                const float64x2_t y = vaddq_f64(vmulq_f64(b0[k], filtered), state[k]);
                state[k] = vsubq_f64(vmulq_f64(b1[k], filtered), vmulq_f64(a1[k], y));
                filtered = y;
            }
            const float32x2_t out = vcvt_f32_f64(
                vaddq_f64(input, vmulq_f64(weight, vsubq_f64(vmulq_f64(filtered, scale), input))));
            x0[i] = vget_lane_f32(out, 0);
            x1[i] = vget_lane_f32(out, 1);
        }
        for (size_t k = 0; k < kSections; ++k)
        {
            left.sections[k].state = vgetq_lane_f64(state[k], 0);
            right.sections[k].state = vgetq_lane_f64(state[k], 1);
        }
        left.weight = vgetq_lane_f64(weight, 0);
        right.weight = vgetq_lane_f64(weight, 1);
#else
        for (int i = 0; i < n; ++i)
        {
            x0[i] = left.process(x0[i], engaged);
            x1[i] = right.process(x1[i], engaged);
        }
#endif
    }

private:
    struct Section
    {
        double b0 = 1.0, b1 = 0.0, a1 = 0.0, state = 0.0;
    };
    static constexpr size_t kSectionCount = 8;
    std::array<Section, kSectionCount> sections{};
    double scale = 1.0, step = 1.0, weight = 0.0;
    bool initialized = false;
};

// Retained only for kScResponseStrength == 0: this is the exact pre-patch-44
// signal path required by the disabled-element byte-identity control.
// The default VCA path uses SidechainResponse, never this legacy filter.
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

    // process() on a left and a right voicing over a block, in place: each
    // lane performs process()'s double arithmetic.
    static void processPair(OutputVoicing& left, OutputVoicing& right, float* x0, float* x1, int n) noexcept
    {
#if defined(__aarch64__)
        const auto pair = [](double l, double r) noexcept { return float64x2_t{l, r}; };
        const float64x2_t b0 = pair(left.b[0], right.b[0]), b1 = pair(left.b[1], right.b[1]),
                          b2 = pair(left.b[2], right.b[2]), a1 = pair(left.a[1], right.a[1]),
                          a2 = pair(left.a[2], right.a[2]), l0 = pair(left.lpB[0], right.lpB[0]),
                          l1 = pair(left.lpB[1], right.lpB[1]), la = pair(left.lpA1, right.lpA1);
        float64x2_t px1 = pair(left.px1, right.px1), px2 = pair(left.px2, right.px2),
                    py1 = pair(left.py1, right.py1), py2 = pair(left.py2, right.py2),
                    lx1 = pair(left.lx1, right.lx1), ly1 = pair(left.ly1, right.ly1);
        for (int i = 0; i < n; ++i)
        {
            const float64x2_t x = vcvt_f64_f32(float32x2_t{x0[i], x1[i]});
            const float64x2_t y = vsubq_f64(vsubq_f64(vaddq_f64(vaddq_f64(vmulq_f64(b0, x), vmulq_f64(b1, px1)),
                                                                vmulq_f64(b2, px2)),
                                                      vmulq_f64(a1, py1)),
                                            vmulq_f64(a2, py2));
            px2 = px1; px1 = x; py2 = py1; py1 = y;
            const float64x2_t z = vsubq_f64(vaddq_f64(vmulq_f64(l0, y), vmulq_f64(l1, lx1)), vmulq_f64(la, ly1));
            lx1 = y; ly1 = z;
            const float32x2_t out = vcvt_f32_f64(z);
            x0[i] = vget_lane_f32(out, 0);
            x1[i] = vget_lane_f32(out, 1);
        }
        left.px1 = vgetq_lane_f64(px1, 0); right.px1 = vgetq_lane_f64(px1, 1);
        left.px2 = vgetq_lane_f64(px2, 0); right.px2 = vgetq_lane_f64(px2, 1);
        left.py1 = vgetq_lane_f64(py1, 0); right.py1 = vgetq_lane_f64(py1, 1);
        left.py2 = vgetq_lane_f64(py2, 0); right.py2 = vgetq_lane_f64(py2, 1);
        left.lx1 = vgetq_lane_f64(lx1, 0); right.lx1 = vgetq_lane_f64(lx1, 1);
        left.ly1 = vgetq_lane_f64(ly1, 0); right.ly1 = vgetq_lane_f64(ly1, 1);
#else
        for (int i = 0; i < n; ++i)
        {
            x0[i] = left.process(x0[i]);
            x1[i] = right.process(x1[i]);
        }
#endif
    }

private:
    std::array<double, 3> b{{1.0, 0.0, 0.0}}, a{{1.0, 0.0, 0.0}};
    std::array<double, 2> lpB{{1.0, 0.0}};
    double lpA1 = 0.0;
    double px1 = 0.0, px2 = 0.0, py1 = 0.0, py2 = 0.0, lx1 = 0.0, ly1 = 0.0;
};

} // namespace duskaudio::vcaLaw
