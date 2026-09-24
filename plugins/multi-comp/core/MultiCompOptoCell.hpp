// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
// OPTO (opto leveler) detector, level estimator, static law and gain cell.
//
// Moved verbatim out of MultiCompModes::processOpto (T4-P1-CELL, 2026-09-22),
// so a later detector/estimator/cell is a swap of this ONE header, in the lab
// core first and then in production. processOpto keeps the gain application
// and the whole output stage: make-up, colour, colour DC, HF shelf, output
// ceiling.
//
// Per sample and channel, processOpto calls OptoCell::process() once, at the
// processing rate (host rate x oversampling factor).
//   It receives:
//     input            this sample's audio, before the cell gain;
//     compressed       input * gain(ch): this sample's post-cell audio, before
//                      make-up and colour. This feedforward cell ignores it; a
//                      feedback cell taps it;
//     sidechain        the external key sample;
//     external         the EFFECTIVE external-sidechain state;
//     optoDetector     MultiCompDSP's explicit detector feed (own or linked);
//     useOptoDetector  true selects optoDetector over input / sidechain;
//     limit            the Comp/Limit switch, true for Limit;
//     peakReduction    the Peak Reduction control, 0..100 (clamped here).
//   It returns the cell gain for the NEXT sample, which gain(ch) also reads
//   until the next call. It leaves two values of THIS sample for the colour
//   stage: inputLevelDb(ch), the corrected detector level in dB, and
//   dynamicGrDb(ch), the summed population reduction in dB before the
//   low-frequency gain floor.
//
// Lifecycle: setRate() refreshes every coefficient and keeps history (runtime
// oversampling changes); reset() clears all history, both channels, and keeps
// the coefficients; scaleCounters() runs after setRate() on a rate change and
// rescales the sample counters so they keep their physical time.
//
// The static law (optoThresholdDb, optoCurveDb) sits at namespace scope
// because the colour stage in processOpto also reads it, at PR 70. A
// replacement header must keep providing it until the colour blend owns its
// own reference table.
#pragma once
#include "../../shared-daf/dsp/DuskFilters.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace duskaudio
{

inline constexpr std::array<float, 23> kOptoCompressCurve{{
    0.9207f, 1.9474f, 3.1430f, 4.6974f, 6.1696f, 7.6165f,
    9.2638f, 10.8676f, 12.4485f, 13.9366f, 15.7917f, 17.2499f,
    19.0803f, 20.4897f, 22.3318f, 23.8425f, 25.2656f, 26.5058f,
    28.2780f, 29.5828f, 30.6369f, 32.1649f, 32.9052f}};
inline constexpr std::array<float, 23> kOptoLimitCurve{{
    0.9379f, 1.9978f, 3.2454f, 4.8601f, 6.4467f, 8.0793f,
    9.6964f, 11.7394f, 13.5249f, 15.2602f, 17.4341f, 19.2438f,
    21.5010f, 23.3864f, 25.7863f, 27.9270f, 30.0609f, 32.0834f,
    34.7103f, 36.8851f, 38.8184f, 40.5691f, 40.9082f}};

inline float optoThresholdDb(float peakReduction, bool limit) noexcept
{
    // The reference knob is normalised 0..1; Multi-Comp exposes the same
    // control as a displayed 0..100 percentage. Compress and Limit use
    // their independently measured onset tables.
    constexpr std::array<float, 9> compressThresholds{{
        -3.8483f, -10.8579f, -17.0206f, -21.4516f, -25.7740f,
        -33.8121f, -40.1926f, -44.3555f, -45.4059f}};
    constexpr std::array<float, 9> limitThresholds{{
        -4.1256f, -11.1198f, -17.2740f, -21.6889f, -26.0047f,
        -34.0625f, -40.5015f, -44.6568f, -45.6471f}};
    const auto& thresholds = limit ? limitThresholds : compressThresholds;
    const auto& curve = limit ? kOptoLimitCurve : kOptoCompressCurve;
    const float onsetOffset = (1.0f - curve[0])
        / ((curve[1] - curve[0]) * 0.5f);
    const float thresholdCorrection = -onsetOffset;
    const float normalised = std::clamp(peakReduction * 0.01f, 0.0f, 1.0f);
    if (normalised <= 0.1f) return 1000.0f;

    // The first measured threshold is at 0.2.  Continue its measured
    // 0.2->0.3 slope toward 0.1 so automation stays continuous while still
    // leaving 0.0 and 0.1 inactive over the measured input range.
    if (normalised < 0.2f)
        return thresholds[0] + thresholdCorrection
            + (normalised - 0.2f) * (thresholds[1] - thresholds[0]) * 10.0f;
    if (normalised >= 1.0f) return thresholds.back() + thresholdCorrection;
    // The original tenth-step sweep did not sample PR 0.55.  A dedicated
    // steady-state capture there places the Compress threshold 0.53 dB
    // below the linear 0.50 -> 0.60 interpolation (measured GR residuals
    // were -0.239/-0.377/-0.500 dB at -24/-18/-12 dBFS).  Preserve the
    // measured endpoints and interpolate through the new midpoint.
    if (!limit && normalised >= 0.5f && normalised < 0.6f)
    {
        constexpr float midpointThreshold = -24.1428f;
        if (normalised < 0.55f)
            return thresholds[3] + thresholdCorrection
                + (normalised - 0.5f)
                    * (midpointThreshold - thresholds[3]) * 20.0f;
        return midpointThreshold + thresholdCorrection
            + (normalised - 0.55f)
                * (thresholds[4] - midpointThreshold) * 20.0f;
    }
    const float position = (normalised - 0.2f) * 10.0f;
    const size_t index = static_cast<size_t>(position);
    const float fraction = position - static_cast<float>(index);
    return thresholds[index] + thresholdCorrection
        + fraction * (thresholds[index + 1] - thresholds[index]);
}

inline float optoCurveDb(float overshootDb, bool limit) noexcept
{
    const auto& curve = limit ? kOptoLimitCurve : kOptoCompressCurve;
    const float position = overshootDb * 0.5f;
    if (position <= 0.0f) {
        if (limit) return std::max(0.0f, curve[0] + position * (curve[1] - curve[0]));
        return curve[0] * std::exp(position * (curve[1] - curve[0]) / curve[0]);
    }
    if (position >= static_cast<float>(curve.size() - 1))
    {
        const float extraPosition
            = position - static_cast<float>(curve.size() - 1);
        if (limit)
            return curve.back() + extraPosition
                * (curve.back() - curve[curve.size() - 2]);

        // Transition from the table's final measured slope to just below
        // unity (1.90 dB GR per 2 dB input), then approach the Limit
        // continuation smoothly. Both joins are C1: a hard slope change at
        // the table edge and the old min() crossing produced unmeasured
        // output-slope steps.
        constexpr float compressInitialSlope = 1.90f;
        constexpr float slopeTransitionPositions = 0.10f;
        const float tableEndSlope = curve.back()
            - curve[curve.size() - 2];
        if (extraPosition < slopeTransitionPositions)
            return curve.back() + tableEndSlope * extraPosition
                + (compressInitialSlope - tableEndSlope)
                    * extraPosition * extraPosition
                    / (2.0f * slopeTransitionPositions);
        const float limitSlope = kOptoLimitCurve.back()
            - kOptoLimitCurve[kOptoLimitCurve.size() - 2];
        const float limitContinuation = kOptoLimitCurve.back()
            + extraPosition * limitSlope;
        const float transitionValue = curve.back()
            + slopeTransitionPositions
                * (tableEndSlope + compressInitialSlope) * 0.5f;
        const float limitAtTransition = kOptoLimitCurve.back()
            + slopeTransitionPositions * limitSlope;
        const float initialGap = limitAtTransition - transitionValue;
        const float convergenceRate = (compressInitialSlope - limitSlope)
            / initialGap;
        return limitContinuation - initialGap
            * std::exp(-convergenceRate
                * (extraPosition - slopeTransitionPositions));
    }
    const size_t index = static_cast<size_t>(position);
    const float fraction = position - static_cast<float>(index);
    return curve[index] + fraction * (curve[index + 1] - curve[index]);
}

class OptoCell
{
public:
    static constexpr int kChannels = 2;

    // Coefficients at the processing rate. History is kept, so a runtime
    // oversampling change continues every envelope and filter.
    void setRate(float sr) noexcept
    {
        optoInvSampleRate = 1.0f / sr;
        optoFloorHighPassStep = 1.0f - std::exp(-6.283185307f * 30.0f / sr);
        optoFloorLowPassStep = 1.0f - std::exp(-6.283185307f * 2.016362169f / sr);
        optoFloorBandStep = 1.0f - std::exp(-6.283185307f * 1000.0f / sr);
        for (auto& filter : optoLimitFloorFilter)
            filter.setCoeffs(Biquad::lowPass(sr, 300.0f, 0.70710678f));
        optoFloorPowerStep = 1.0f - std::exp(-1.0f / (0.050f * sr));
        // The 0.4 ms / 8 ms rectifier supplies the programme integration that
        // separates sustained energy from unsupported peaks. The 50 us /
        // 40 ms follower supplies both the calibrated ceiling and the first
        // 1.5 ms of the isolated-event fast-cell target.
        constexpr float detectorAttackSeconds = 0.000400f;
        optoDetectorAttack = std::exp(-optoInvSampleRate / detectorAttackSeconds);
        optoDetectorRelease = std::exp(-optoInvSampleRate / 0.008f);
        optoDetectorFloorPeakAttack = std::exp(-optoInvSampleRate / 0.010f);
        optoDetectorFloorPeakRelease = std::exp(-optoInvSampleRate / 0.100f);
        optoDetectorPeakAttack = std::exp(-optoInvSampleRate / 0.000050f);
        optoDetectorPeakRelease = std::exp(-optoInvSampleRate / 0.040f);
        optoChargePeakRelease = std::exp(-optoInvSampleRate / 0.00025f);
        optoChargeTopOffSamples = std::max(1, static_cast<int>(
            std::lround(0.020f * sr)));
        optoFastPathSamples = std::max(1, static_cast<int>(
            std::lround(0.0015f * sr)));
        optoDetectorSilenceHoldSamples = std::max(1, static_cast<int>(
            std::lround(0.0005f * sr)));
        optoDetectorFloorHoldSamples = std::max(1, static_cast<int>(
            std::lround(0.030f * sr)));
        constexpr float optoCalibrationRate = 96000.0f;
        optoFastAttackAtCalibrationRate = std::exp(
            -1.0f / (0.021f * optoCalibrationRate));
        optoCalibrationRateRatio = optoCalibrationRate / sr;
        optoSlowAttack = std::exp(-optoInvSampleRate / 0.190f);
        optoSustainedTargetSmoothing = std::exp(-optoInvSampleRate / 0.001f);
        optoSustainedTopOffAttack = std::exp(-optoInvSampleRate / 0.014f);
        optoLimitFastTopOffAttack = std::exp(-optoInvSampleRate / 0.0013f);
        optoLimitSlowTopOffAttack = std::exp(-optoInvSampleRate / 0.0037f);
        optoFlashRelease = std::exp(-optoInvSampleRate / 0.010f);
        optoFastRelease = std::exp(-optoInvSampleRate / 0.064f);
        optoMidRelease = std::exp(-optoInvSampleRate / 0.185f);
        optoSlowRelease = std::exp(-optoInvSampleRate / 1.174f);
        optoProgrammeMemoryRelease = std::exp(-optoInvSampleRate / 0.250f);
        optoRecentEventChargeRelease = std::exp(-optoInvSampleRate / 1.000f);
        optoRecentEventChargeReset = std::exp(-optoInvSampleRate / 0.012f);
        optoProgrammeMotionRelease = std::exp(-optoInvSampleRate / 0.500f);
        optoProgrammeActivityAttack = std::exp(-optoInvSampleRate / 0.100f);
        optoIsolatedEventAttack = std::exp(-optoInvSampleRate / 0.00020f);
        optoIsolatedEventRelease = std::exp(-optoInvSampleRate / 0.006120327539f);
        // Measured reference opto leveler detector weighting. The shelf's equivalent Q is
        // the JSON fit's S=0.6998415302 converted to the RBJ shelf-Q form.
        // Design at the processing rate: processOpto is called at fs*osFactor.
        const std::array<BiquadCoeffs, kOptoDetectorSections> weightingCoeffs{{
            Biquad::shelf(sr, 319.1844220f, 4.73782359f, 0.5894442553f, false),
            Biquad::peak(sr, 134.4305880f, 0.59151688f, 0.5136042617f),
            Biquad::peak(sr, 880.5758706f, -0.4288385533f, 0.6986416568f),
            Biquad::peak(sr, 5840.123777f, 1.410524878f, 0.4820592696f),
            Biquad::peak(sr, 9991.669467f, -1.407323237f, 0.7988600998f)
        }};
        for (auto& channel : optoDetectorWeighting)
            for (size_t section = 0; section < kOptoDetectorSections; ++section)
                channel[section].setCoeffs(weightingCoeffs[section]);
        for (auto& channel : optoDepthWeighting)
        {
            channel[0].setCoeffs(Biquad::shelf(sr, 2000.0f, 1.25f, 0.70710678f, true));
            channel[1].setCoeffs(Biquad::peak(sr, 20000.0f, 2.0f, 2.5f));
            channel[2].setCoeffs(Biquad::shelf(sr, 2200.0f, 2.4f, 0.70710678f, true));
            channel[3].setCoeffs(Biquad::peak(sr, 20000.0f, 1.0f, 2.5f));
        }
    }

    // Clears every history, both channels. Coefficients are kept.
    void reset() noexcept
    {
        for (auto& d : channels) d = ChannelState{};
        // Detector filters hold state between blocks. Clear every section for
        // both channels so reset/reprepare is deterministic on every platform.
        for (auto& channel : optoDetectorWeighting)
            for (auto& filter : channel)
                filter.reset();
        for (auto& channel : optoDepthWeighting)
            for (auto& filter : channel)
                filter.reset();
        for (auto& filter : optoLimitFloorFilter) filter.reset();
    }

    // After setRate() on a runtime rate change: `scaleCounter(value, maximum)`
    // returns an elapsed or remaining sample count at the new rate.
    template <typename ScaleCounter>
    void scaleCounters(const ScaleCounter& scaleCounter) noexcept
    {
        for (auto& d : channels)
        {
            d.detectorExposureSamples = scaleCounter(
                d.detectorExposureSamples, optoChargeTopOffSamples);
            d.detectorFloorOnlySamples = scaleCounter(
                d.detectorFloorOnlySamples, optoDetectorFloorHoldSamples);
            d.detectorUnsupportedSamples = scaleCounter(
                d.detectorUnsupportedSamples, optoDetectorSilenceHoldSamples);
            d.detectorReleaseExposureSamples = scaleCounter(
                d.detectorReleaseExposureSamples, optoChargeTopOffSamples);
            d.detectorSilentSamples = scaleCounter(
                d.detectorSilentSamples, optoDetectorSilenceHoldSamples);
        }
    }

    // One sample of one channel; see the contract at the top of this file.
    float process(int ch, float input, float /*compressed*/, float sidechain,
                  bool external, float optoDetector, bool useOptoDetector,
                  bool limit, float peakReduction) noexcept
    {
        auto& d = channels[static_cast<size_t>(ch)];
        // The measured detector tap is pre-gain. Weight only the selected
        // detector source; the audio path above remains untouched.
        float sc = useOptoDetector ? optoDetector
                                   : (external ? sidechain : input);
        const float detectorInputAbs = std::abs(sc);
        // Deep bass compression approaches a frequency-dependent gain
        // floor. Reference phase measurements exclude an audio leakage
        // path: this energy ratio acts on the control gain only. Limit
        // has a steeper rolloff, preserving its measured 1 kHz top law.
        d.floorHighPassLow += optoFloorHighPassStep * (sc - d.floorHighPassLow);
        d.floorLow += optoFloorLowPassStep * (sc - d.floorHighPassLow - d.floorLow);
        d.floorBandLow += optoFloorBandStep * (d.floorLow - d.floorBandLow);
        const float limitFloorSignal = optoLimitFloorFilter[static_cast<size_t>(ch)].process(d.floorLow);
        const float floorSignal = limit ? limitFloorSignal : d.floorBandLow;
        d.floorPower += optoFloorPowerStep * (floorSignal * floorSignal - d.floorPower);
        d.inputPower += optoFloorPowerStep * (sc * sc - d.inputPower);
        // Silence must be decided against the recent signal scale, never an
        // absolute epsilon: the oversampler's FIR tail spends its last few
        // samples in cancellation territory where whether it sits above or
        // below any fixed threshold depends on per-op rounding (FMA
        // contraction flipped hold-vs-discharge every burst and moved
        // high-crest gain reduction by 1.5 dB between platforms). Relative to
        // the 40 ms peak follower the crossing lands in the steep part of the
        // tail at every signal level, and the short hold bridges the samples
        // near a waveform zero crossing. The floor only keeps a long-silent
        // peak from dragging the threshold into denormal territory.
        const float silenceFloor = std::max(d.detectorPeak * 1.0e-4f, 1.0e-9f);
        if (detectorInputAbs > silenceFloor)
            d.detectorSilentSamples = 0;
        else if (d.detectorSilentSamples < optoDetectorSilenceHoldSamples)
            ++d.detectorSilentSamples;
        const bool hasDetectorInput
            = d.detectorSilentSamples < optoDetectorSilenceHoldSamples;
        for (auto& filter : optoDetectorWeighting[static_cast<size_t>(ch)])
            sc = filter.process(sc);
        // High-frequency detector emphasis grows with cell reduction.
        // Advance both mode paths so Comp/Limit changes retain history.
        const float depthInput = sc;
        const float priorCellReduction = d.fastGrDb + d.midGrDb + d.slowGrDb;
        const float compressShelfWeight = std::clamp((priorCellReduction - 8.0f) / 20.0f, 0.0f, 1.0f);
        const float limitShelfWeight = std::clamp((priorCellReduction - 12.0f) / 28.0f, 0.0f, 1.0f);
        const float depthPeakWeight = std::clamp((priorCellReduction - 8.0f) / 8.0f, 0.0f, 1.0f);
        auto& depthFilters = optoDepthWeighting[static_cast<size_t>(ch)];
        float compressDetector = sc + compressShelfWeight * (depthFilters[0].process(sc) - sc);
        float limitDetector = sc + limitShelfWeight * (depthFilters[2].process(sc) - sc);
        compressDetector += depthPeakWeight * (depthFilters[1].process(compressDetector) - compressDetector);
        limitDetector += depthPeakWeight * (depthFilters[3].process(limitDetector) - limitDetector);
        sc = limit ? limitDetector : compressDetector;
        d.depthInputPower += optoFloorPowerStep * (depthInput * depthInput - d.depthInputPower);
        d.depthOutputPower += optoFloorPowerStep * (sc * sc - d.depthOutputPower);
        const float spectralBoostDb = std::clamp(10.0f * std::log10(
            std::max(d.depthOutputPower, 1.0e-12f) / std::max(d.depthInputPower, 1.0e-12f)), 0.0f, 4.0f);
        const float pr = std::clamp(peakReduction, 0.0f, 100.0f);
        const float detectorAbs = std::abs(sc);
        const bool detectorRising = detectorAbs > d.detectorLevel;
        const float detectorCoeff = detectorRising
            ? optoDetectorAttack : optoDetectorRelease;
        d.detectorLevel = detectorAbs
            + (d.detectorLevel - detectorAbs) * detectorCoeff;
        const bool detectorPeakRising = detectorAbs > d.detectorPeak;
        const float detectorPeakCoeff = detectorPeakRising
            ? optoDetectorPeakAttack : optoDetectorPeakRelease;
        d.detectorPeak = detectorAbs
            + (d.detectorPeak - detectorAbs) * detectorPeakCoeff;
        const float chargePeakCoeff = detectorAbs > d.chargePeak
            ? optoDetectorPeakAttack : optoChargePeakRelease;
        d.chargePeak = detectorAbs
            + (d.chargePeak - detectorAbs) * chargePeakCoeff;
        constexpr float detectorSupportFloor = 0.006309573f; // -44 dBFS
        const bool detectorAboveSupportFloor
            = detectorInputAbs > detectorSupportFloor;
        // A persistent sub-audible floor can keep the relative silence gate
        // open. Require 30 ms: even a 20 Hz sine whose peak only just clears
        // the support floor returns above it within 25 ms, so audible
        // low-frequency zero crossings cannot masquerade as floor noise.
        const bool hadPersistentFloor
            = d.detectorFloorOnlySamples >= optoDetectorFloorHoldSamples;
        if (detectorAboveSupportFloor
            && hadPersistentFloor)
        {
            // A hard reset at detectorSupportFloor made otherwise identical
            // events over -43 and -45 dBFS beds differ by 6.84 dB. Blend the
            // retained exposure across the sub-audible floor range: a -80 dBFS
            // or lower floor behaves as silence, while the blend reaches the
            // uninterrupted-exposure path continuously at -44 dBFS.
            constexpr float lowestExposureFloorDb = -80.0f;
            constexpr float detectorSupportFloorDb = -44.0f;
            const float floorExposurePosition = std::clamp(
                (gainToDecibels(std::max(d.detectorFloorPeak, 1.0e-12f))
                    - lowestExposureFloorDb)
                    / (detectorSupportFloorDb - lowestExposureFloorDb),
                0.0f, 1.0f);
            const float floorExposureBlend = floorExposurePosition
                * floorExposurePosition * (3.0f - 2.0f * floorExposurePosition);
            d.detectorExposureSamples = static_cast<int>(std::lround(
                static_cast<float>(d.detectorExposureSamples)
                    * floorExposureBlend));
            d.detectorEventActive = false;
            d.nextEventWeight = 1.0f - floorExposureBlend;
        }
        // Once a real floor starts, bridge its exact waveform-zero samples;
        // do not turn an untouched run of digital zero into floor history.
        const bool floorSignalPresent = detectorInputAbs > 1.0e-12f
            || d.detectorFloorOnlySamples > 0;
        if (!detectorAboveSupportFloor && floorSignalPresent)
        {
            // Track the recent floor rather than freezing the first few
            // samples after the signal crosses -44 dBFS. The 10 ms attack /
            // 100 ms release spans low-frequency cycles but forgets a decayed
            // tail before a later event. The slower attack also prevents the few
            // below-threshold samples at an event's rising edge from
            // materially contaminating the estimate before it is consumed.
            const float floorPeakCoeff
                = detectorInputAbs > d.detectorFloorPeak
                    ? optoDetectorFloorPeakAttack
                    : optoDetectorFloorPeakRelease;
            d.detectorFloorPeak = detectorInputAbs
                + (d.detectorFloorPeak - detectorInputAbs) * floorPeakCoeff;
            d.detectorFloorOnlySamples = std::min(
                d.detectorFloorOnlySamples + 1, optoDetectorFloorHoldSamples);
        }
        else
        {
            d.detectorFloorOnlySamples = 0;
            d.detectorFloorPeak = 0.0f;
        }
        const int previousDetectorExposureSamples = d.detectorExposureSamples;
        d.detectorExposureSamples = hasDetectorInput
            ? std::min(d.detectorExposureSamples + 1, optoChargeTopOffSamples)
            : 0;
        // The static law was measured with the original 50 us / 40 ms peak
        // follower on a 997 Hz sine.  At 48 kHz, fixed-point iteration of one
        // full-wave period gives peaks of 0.925093862 for the 0.4 ms / 8 ms
        // integrator and 0.994476788 for that peak follower.  Therefore the
        // exact calibration-condition correction is the peak ratio
        // 0.994476788 / 0.925093862 = 1.07500095 (= 0.628177 dB), stored as
        // the linear factor so the per-sample path carries no pow().
        constexpr float detectorIntegrationCalibrationGain = 1.07500095f;
        const float integratedDetectorLevel = d.detectorLevel
            * detectorIntegrationCalibrationGain;
        const float effectiveDetectorLevel = std::min(
            d.detectorPeak, integratedDetectorLevel);
        const float uncorrectedInputLevelDb = gainToDecibels(effectiveDetectorLevel);
        // A calibrated 997 Hz sine leaves at most 0.138029 dB between the peak
        // reference and integrated detector, whereas the fitted gaussian
        // waveform averages 3.629921 dB and peaks at 6.186975 dB.  The excess
        // is therefore a measured fluctuating-signal term, not a knob offset.
        constexpr float sineSeparationGuardDb = 0.15f;
        constexpr float maximumFittedSeparationDb = 6.19f;
        const float detectorSeparationDb = gainToDecibels(
            d.detectorPeak / std::max(integratedDetectorLevel, 1.0e-12f));
        const float fluctuationDb = std::clamp(
            detectorSeparationDb - sineSeparationGuardDb,
            0.0f, maximumFittedSeparationDb - sineSeparationGuardDb);
        const float exposureSaturation = std::clamp(
            static_cast<float>(d.detectorExposureSamples)
                / static_cast<float>(optoChargeTopOffSamples),
            0.0f, 1.0f);
        const float sustainedExposurePosition = std::clamp(
            (exposureSaturation - 0.75f) / 0.25f, 0.0f, 1.0f);
        const float sustainedExposureBlend = sustainedExposurePosition
            * sustainedExposurePosition * (3.0f - 2.0f * sustainedExposurePosition);
        // The constrained fit uses the -36/-30/-18/-12 dBFS broadband points
        // while retaining the crest triplet; -24 dBFS is held out. Sustained
        // exposure uses the separately measured dense-programme correction.
        constexpr float broadbandFitPivotDb = -18.0f;
        const float broadbandFitAtPivot = 0.010f
            + (0.055f - 0.010f) * sustainedExposureBlend;
        constexpr float broadbandFitSlope = -0.0167f;
        const float broadbandBusyPosition = std::clamp((d.programmeActivity / std::max(pr * 0.01f, 0.10f) - 0.11f) / 0.04f, 0.0f, 1.0f);
        const float broadbandBusyWeight = broadbandBusyPosition * broadbandBusyPosition * (3.0f - 2.0f * broadbandBusyPosition);
        const float broadbandCorrectionDb = (1.0f - sustainedExposureBlend * (1.0f - broadbandBusyWeight)) * fluctuationDb
            * (broadbandFitAtPivot - 0.3118395415f * spectralBoostDb + broadbandFitSlope
                * (uncorrectedInputLevelDb - broadbandFitPivotDb));
        const float inputLevelDb = uncorrectedInputLevelDb
            + broadbandCorrectionDb;
        const float thresholdDb = optoThresholdDb(pr, limit);
        const float overdriveDb = inputLevelDb - thresholdDb;
        const float targetGrDb = pr <= 10.0f ? 0.0f : optoCurveDb(overdriveDb, limit);
        // The fast cell has a second, peak-fed charge path. The isolated event
        // grid shows full peak contribution at -12 dBFS but progressively
        // companded contribution toward 0 dBFS; sustained signals are
        // unchanged because their calibrated integrated and peak levels meet.
        const float peakInputLevelDb = uncorrectedInputLevelDb
            + std::max(0.0f, detectorSeparationDb);
        const float peakSeparationDb = std::max(
            0.0f, peakInputLevelDb - inputLevelDb);
        const float fastLevelBlend = std::clamp(
            -peakInputLevelDb / 12.0f, 0.0f, 1.0f);
        const float fastExposureBlend = std::clamp(
            1.0f - static_cast<float>(
                d.detectorExposureSamples - optoDetectorSilenceHoldSamples)
                / static_cast<float>(std::max(
                    1, optoFastPathSamples - optoDetectorSilenceHoldSamples)),
            0.0f, 1.0f);
        const float fastPeakBlend = fastLevelBlend * fastExposureBlend;
        const float fastInputLevelDb = inputLevelDb
            + peakSeparationDb * fastPeakBlend;
        const float fastTargetGrDb = pr <= 10.0f ? 0.0f
            : fastPeakBlend > 0.0f
                ? optoCurveDb(fastInputLevelDb - thresholdDb, limit)
                : targetGrDb;
        // The release populations partition, rather than augment, the static
        // law. Their measured 11.02 / 8.01 / 3.20 dB amplitudes sum to the
        // 22.23 dB target at the memory-curve operating point.
        constexpr float highDriveTotal = 11.02f + 8.01f + 3.20f;
        constexpr float highDriveFastShare = 11.02f / highDriveTotal;
        constexpr float highDriveMidShare = 8.01f / highDriveTotal;
        constexpr float highDriveSlowShare = 3.20f / highDriveTotal;
        // Projecting the corrected 5 s low-drive exposure through the same
        // fixed taus gives this fully charged operating-point partition.
        constexpr float lowDriveTotal = 4.469f + 3.749f + 2.674f;
        constexpr float lowDriveFastShare = 4.469f / lowDriveTotal;
        constexpr float lowDriveMidShare = 3.749f / lowDriveTotal;
        constexpr float lowDriveSlowShare = 2.674f / lowDriveTotal;
        const float driveBlend = 1.0f / (1.0f + std::exp(
            -(overdriveDb - 5.0f) / 0.8f));
        const float baseFastShare = lowDriveFastShare
            + (highDriveFastShare - lowDriveFastShare) * driveBlend;
        const float baseMidShare = lowDriveMidShare
            + (highDriveMidShare - lowDriveMidShare) * driveBlend;
        const float limitFastShareBoost = limit ? 0.175f : 0.0f;
        const float fastShare = baseFastShare + limitFastShareBoost;
        const float midShare = baseMidShare - limitFastShareBoost;
        const float slowShare = lowDriveSlowShare
            + (highDriveSlowShare - lowDriveSlowShare) * driveBlend;
        // The base 21 ms fast charge applies at the amplitude-fit point. At
        // high drive, the rate and remaining-capacity exponents are calibrated
        // jointly against isolated events and the 2/5/20/40 Hz repeated-burst
        // points (10 Hz held out). An empty population charges quickly, then
        // slows as it approaches capacity.
        constexpr float lowDriveAttackRate = 2.1f;
        constexpr float highDriveFastAttackRate = 1600.0f;
        constexpr float highDriveSlowAttackRate = 200.0f;
        constexpr float highDriveFastChargeExponent = 5.1f;
        constexpr float highDriveSlowChargeExponent = 1.5f;
        constexpr float highDriveFastMinimumChargeRate = 0.150f;
        // The isolated-event grid shows that empty-cell charge is much less
        // level-dependent than final GR capacity. Scale the high-drive rate
        // inversely around the fitted 18 dB pivot; later fill remains limited
        // by the capacity exponent below.
        constexpr float fastRatePivotDb = 18.0f;
        const float fastRateRatio
            = fastRatePivotDb / std::max(targetGrDb, 1.0f);
        const float fastRateRatioSquared = fastRateRatio * fastRateRatio;
        const float fastRateCompanding = std::clamp(
            fastRateRatioSquared * fastRateRatioSquared, 0.25f, 2.0f);
        const float compandedFastAttackRate
            = highDriveFastAttackRate * fastRateCompanding;
        const float fastExposureRateScale = 1.0f - sustainedExposureBlend;
        const float fastAttackRate = lowDriveAttackRate
            + (compandedFastAttackRate * fastExposureRateScale
                - lowDriveAttackRate) * driveBlend;
        const float selectedHighDriveSlowAttackRate = limit
            ? 50.0f : highDriveSlowAttackRate;
        const float slowAttackRate = lowDriveAttackRate
            + (selectedHighDriveSlowAttackRate - lowDriveAttackRate) * driveBlend;
        const float limitSlowRateBlend = std::clamp(
            (pr - 60.0f) / 40.0f, 0.0f, 1.0f);
        const float selectedLimitSlowPopulationAttackRate = 5.0f
            + (35.0f - 5.0f) * limitSlowRateBlend;
        const float limitSlowPopulationAttackRate = lowDriveAttackRate
            + (selectedLimitSlowPopulationAttackRate - lowDriveAttackRate)
                * driveBlend;
        const float fastChargeExponent = 1.0f
            + (highDriveFastChargeExponent - 1.0f) * driveBlend;
        const float slowChargeExponent = 1.0f
            + (highDriveSlowChargeExponent - 1.0f) * driveBlend;
        const float fastCellTargetGrDb = fastShare * fastTargetGrDb;
        d.fastSustainedTargetDb = fastCellTargetGrDb
            + (d.fastSustainedTargetDb - fastCellTargetGrDb)
                * optoSustainedTargetSmoothing;
        // Positive motion of the already-smoothed cell target distinguishes a
        // settled pedestal from continuing programme without inspecting the
        // waveform or switching regimes.  Normalising the leaky result by PR
        // separates the measured pedestal ceiling (0.102) from sustained
        // broadband onset (0.155); the slower follower makes that boundary a
        // continuous 100 ms attack / 500 ms release transition.
        const float positiveSustainedTargetChargeDb = std::max(
            d.fastSustainedTargetDb - d.previousSustainedTargetDb, 0.0f);
        d.previousSustainedTargetDb = d.fastSustainedTargetDb;
        d.programmeMotion = std::min(
            d.programmeMotion * optoProgrammeMotionRelease
                + 0.010f * positiveSustainedTargetChargeDb,
            1.0f);
        const float programmeActivityCoeff
            = d.programmeMotion > d.programmeActivity
                ? optoProgrammeActivityAttack : optoProgrammeMotionRelease;
        d.programmeActivity = d.programmeMotion
            + (d.programmeActivity - d.programmeMotion)
                * programmeActivityCoeff;
        // The nonlinear attack was fitted at the shipping 2x processing rate
        // (96 kHz). Advance that discrete charge law on a fixed 96 kHz clock;
        // scaling 1-coeff at the processing rate saturates at different attack
        // rates for 1x, 2x and 4x.
        const float fastAttackCoeffAtCalibrationRate = std::max(
            0.0f, 1.0f
                - (1.0f - optoFastAttackAtCalibrationRate) * fastAttackRate);
        d.fastAttackReferencePhase += optoCalibrationRateRatio;
        const int fastAttackReferenceSteps = static_cast<int>(
            d.fastAttackReferencePhase);
        d.fastAttackReferencePhase -= static_cast<float>(
            fastAttackReferenceSteps);
        const float slowAttackCoeff = std::max(
            0.0f, 1.0f - (1.0f - optoSlowAttack) * slowAttackRate);
        const float slowPopulationAttackCoeff = limit ? std::max(
            0.0f, 1.0f - (1.0f - optoSlowAttack)
                * limitSlowPopulationAttackRate) : slowAttackCoeff;
        const float midCellTargetGrDb = midShare * targetGrDb;
        const float slowCellTargetGrDb = slowShare * targetGrDb;
        const float standingGrDb = d.fastGrDb + d.midGrDb + d.slowGrDb;
        const float positiveCellChargeDb = std::max(
            standingGrDb - d.previousCellGrDb, 0.0f);
        d.previousCellGrDb = standingGrDb;
        const float detectorSupport = std::clamp(
            d.detectorPeak / detectorSupportFloor, 0.0f, 1.0f);
        const float recentEventChargeTarget = 1.0f
            - exposureSaturation * detectorSupport;
        const float recentEventChargeCoeff
            = recentEventChargeTarget > d.recentEventCharge
                ? optoRecentEventChargeReset : optoRecentEventChargeRelease;
        d.recentEventCharge = recentEventChargeTarget
            + (d.recentEventCharge - recentEventChargeTarget)
                * recentEventChargeCoeff;
        constexpr float eventHistoryPerChargedDb = 0.000020f;
        d.recentEventCharge = std::min(
            d.recentEventCharge
                + eventHistoryPerChargedDb * positiveCellChargeDb,
            1.0f);
        const float settledEventHistory = 1.0f - d.recentEventCharge;
        const float settledHistorySquared
            = settledEventHistory * settledEventHistory;
        const float settledHistoryFourth
            = settledHistorySquared * settledHistorySquared;
        const float settledEventWeight = settledHistoryFourth
            * settledHistoryFourth * settledHistoryFourth;
        const float settledEventDrainWeight
            = settledHistoryFourth * settledEventHistory;
        const float pedestalAttackStrength = 1.75f
            + 24.0f * std::exp(-standingGrDb / 6.5f);
        const float continuousAttackScale = 1.0f
            + settledEventWeight * (pedestalAttackStrength - 1.0f);
        // The live dense trace overcharged only while a high-drive, already
        // loaded cell was leaving its reset state.  Fade that startup charge
        // over the same event-history reservoir; an empty short-event cell and
        // the independently calibrated Limit path remain unchanged.
        const float highDriveChargePosition = std::clamp(
            (pr * 0.01f - 0.70f) / 0.30f, 0.0f, 1.0f);
        const float highDriveChargeWeight = highDriveChargePosition
            * highDriveChargePosition
            * (3.0f - 2.0f * highDriveChargePosition);
        const float programmeLoadPosition = std::clamp(
            (standingGrDb - 1.5f) / 1.5f, 0.0f, 1.0f);
        const float programmeLoadWeight = programmeLoadPosition
            * programmeLoadPosition
            * (3.0f - 2.0f * programmeLoadPosition);
        const float loadedHighDriveWeight
            = highDriveChargeWeight * programmeLoadWeight;
        const float startupAttackWeight = std::sqrt(d.recentEventCharge);
        const float startupAttackScale = 1.0f
            - 0.70f * startupAttackWeight * loadedHighDriveWeight;
        const float programmeAttackScale = continuousAttackScale
            * (limit ? 1.0f : startupAttackScale);
        // 1e-9 is a linear-amplitude denominator floor for a peak/peak
        // ratio (both operands at audio scale); the 1e-12 below is a log-domain
        // floor before dB conversion.  Different domains, deliberately
        // different constants; neither is a gating branch (the decisions flow
        // through the continuous clamps above).
        const float chargePeakRatio = d.chargePeak
            / std::max(d.detectorPeak, 1.0e-9f);
        const float fastChargeSupport = std::clamp(
            (chargePeakRatio - 0.10f) / 0.20f, 0.0f, 1.0f);
        const float continuousChargeSupport = 1.0f
            - settledEventDrainWeight * (1.0f - fastChargeSupport);
        const float coherentFastAttack = std::max(
            0.0f, 1.0f - (1.0f - fastAttackCoeffAtCalibrationRate)
                * programmeAttackScale);
        const float coherentSlowAttack = std::max(
            0.0f, 1.0f - (1.0f - slowAttackCoeff)
                * programmeAttackScale);
        const float slowQuietPosition = std::clamp(
            (d.programmeActivity / std::max(pr * 0.01f, 0.10f) - 0.11f)
                / 0.04f, 0.0f, 1.0f);
        const float slowQuietWeight = limit ? 0.0f : sustainedExposureBlend
            * (1.0f - slowQuietPosition * slowQuietPosition
                * (3.0f - 2.0f * slowQuietPosition));
        const float slowCellAttackScale = programmeAttackScale + slowQuietWeight
            * (79.99716945f * std::exp(-standingGrDb / 6.47622725f)
                * std::clamp(std::pow(24.0f / std::max(targetGrDb, 1.0f), 4.0f),
                             0.125f, 1.0f) - programmeAttackScale);
        const float coherentSlowPopulationAttack = std::max(
            0.0f, 1.0f - (1.0f - slowPopulationAttackCoeff)
                * slowCellAttackScale);
        const bool detectorDriven = detectorAbs > effectiveDetectorLevel * 0.4f;
        // Support is intentionally judged against the frequency-weighted peak:
        // replacing it with an unweighted peak preserves the 1 kHz grid but
        // adds 0.55 dB of over-compression on the dense reference programme.
        const bool detectorInputPeakSupported = detectorInputAbs
                > detectorSupportFloor || detectorInputAbs
            > std::max(d.detectorPeak * 0.04f, 1.0e-9f);
        const bool detectorInputStartsNewEvent = detectorInputAbs
            > std::max(d.detectorPeak * 0.50f, 1.0e-9f);
        const int previousUnsupportedSamples = d.detectorUnsupportedSamples;
        d.detectorUnsupportedSamples = detectorInputPeakSupported
            ? 0 : std::min(d.detectorUnsupportedSamples + 1,
                           optoDetectorSilenceHoldSamples);
        const bool detectorSupported = detectorInputPeakSupported
            || d.detectorUnsupportedSamples < optoDetectorSilenceHoldSamples;
        d.programmeMemory *= optoProgrammeMemoryRelease;
        if (hasDetectorInput && detectorAboveSupportFloor
            && !d.detectorEventActive)
        {
            d.programmeMemory = std::min(
                d.programmeMemory + 0.25f * d.nextEventWeight, 1.0f);
            d.detectorEventActive = true;
            d.nextEventWeight = 1.0f;
        }
        else if (!hasDetectorInput)
        {
            d.detectorEventActive = false;
            d.nextEventWeight = 1.0f;
        }
        if (!detectorSupported
            && previousUnsupportedSamples < optoDetectorSilenceHoldSamples)
            d.detectorReleaseExposureSamples = previousDetectorExposureSamples;
        else if (detectorInputStartsNewEvent)
            d.detectorReleaseExposureSamples = 0;
        const bool retainPreviousExposure
            = d.detectorReleaseExposureSamples > 0;
        const int releaseExposureSamples = retainPreviousExposure
            ? d.detectorReleaseExposureSamples : d.detectorExposureSamples;
        const float releaseExposureLinear = std::clamp(
            static_cast<float>(releaseExposureSamples)
                / static_cast<float>(optoChargeTopOffSamples),
            0.0f, 1.0f);
        const float releaseExposureBlend
            = releaseExposureLinear * releaseExposureLinear;
        const float repetitionBlend = std::clamp(
            (d.programmeMemory - 0.25f) / 0.75f, 0.0f, 1.0f);
        const float repeatedExposureTopOff = 0.50f * repetitionBlend * std::clamp(
            (static_cast<float>(d.detectorExposureSamples)
                - 0.40f * static_cast<float>(optoChargeTopOffSamples))
                / (0.10f * static_cast<float>(optoChargeTopOffSamples)),
            0.0f, 1.0f);
        const float fastMinimumChargeRate = highDriveFastMinimumChargeRate
            * driveBlend * repeatedExposureTopOff;
        const float fastRecoveryBlend = std::max(
            releaseExposureBlend, repetitionBlend);
        const float exposureDependentFastRelease = optoFlashRelease
            + (optoFastRelease - optoFlashRelease) * fastRecoveryBlend;
        const float fastRelease = exposureDependentFastRelease
            + (optoSlowRelease - exposureDependentFastRelease) * repetitionBlend;
        const float midReleaseExposureBlend = std::clamp(
            3.2f * static_cast<float>(releaseExposureSamples)
                / static_cast<float>(optoChargeTopOffSamples),
            0.0f, 1.0f);
        const float exposureDependentMidRelease = detectorSupported ? optoMidRelease
            : optoFlashRelease
                + (optoMidRelease - optoFlashRelease)
                    * std::max(midReleaseExposureBlend, repetitionBlend);
        const auto followTarget = [detectorDriven, detectorSupported,
                                   hasDetectorInput, continuousChargeSupport](
                                      float& state, float target, float attack,
                                      float release, float chargeExponent,
                                      float minimumChargeRate,
                                      int attackSteps = 1) noexcept {
            // Silence discharges the cells directly; cascading the detector's
            // 40 ms waveform integration into them is what produced D3e's
            // false 8-120 ms hold. A stale detector envelope may continue a
            // release toward a lower target, but cannot recharge a cell until
            // the selected detector input supports it again.
            if (!hasDetectorInput)
                state *= release;
            else if (!detectorSupported)
                state *= release;
            else if (!detectorDriven && target > state)
                return;
            else if (target > state)
            {
                const auto advanceAttack = [&] {
                    const float remainingFraction = target > 1.0e-9f
                        ? std::clamp((target - state) / target, 0.0f, 1.0f)
                        : 0.0f;
                    const float curvedAttackStep = (1.0f - attack) * std::max(
                        std::pow(remainingFraction, chargeExponent - 1.0f),
                        minimumChargeRate) * continuousChargeSupport;
                    state += curvedAttackStep * (target - state);
                };
                for (int step = 0; step < attackSteps; ++step)
                    advanceAttack();
            }
            else
                state = target + (state - target) * release;
        };
        // Three gain-reduction populations share the static capacity. The
        // fast and mid releases interpolate with event exposure and programme
        // memory; the slow optical afterglow retains its measured 1.174 s tau.
        followTarget(d.fastGrDb, fastCellTargetGrDb,
                     coherentFastAttack, fastRelease,
                     fastChargeExponent, fastMinimumChargeRate,
                     fastAttackReferenceSteps);
        followTarget(d.midGrDb, midCellTargetGrDb,
                     coherentSlowAttack, exposureDependentMidRelease,
                     slowChargeExponent, 0.0f);
        const float previousSlowGrDb = d.slowGrDb;
        followTarget(d.slowGrDb, slowCellTargetGrDb,
                     coherentSlowPopulationAttack, optoSlowRelease,
                     slowChargeExponent, 0.0f);
        const float topOffLoadedWeight = std::clamp((standingGrDb - 5.0f) / 10.0f, 0.0f, 1.0f);
        const float topOffStartupScale = 1.0f - 0.90f * startupAttackWeight * topOffLoadedWeight;
        const float sustainedTopOffBase = std::min(
            d.fastSustainedTargetDb, fastCellTargetGrDb);
        // The 0.12 dB full-scale bias closes the measured long-exposure
        // residual. Scale it into the onset so a target crossing zero cannot
        // toggle a 0.12 dB step; the exposure blend starts at 15 ms and reaches
        // full strength at 20 ms.
        const float sustainedTopOffTarget = sustainedTopOffBase > 0.0f
            ? sustainedTopOffBase
                + 0.12f * std::min(sustainedTopOffBase, 1.0f)
            : 0.0f;
        if (sustainedExposureBlend > 0.0f
            && sustainedTopOffTarget > d.fastGrDb)
        {
            const float limitTopOffAttack = optoLimitFastTopOffAttack
                + (optoLimitSlowTopOffAttack - optoLimitFastTopOffAttack)
                    * limitSlowRateBlend;
            const float sustainedTopOffAttack = limit
                ? limitTopOffAttack : optoSustainedTopOffAttack;
            const float blendedTopOffAttack = sustainedExposureBlend >= 1.0f
                ? sustainedTopOffAttack
                : std::pow(sustainedTopOffAttack, sustainedExposureBlend);
            d.fastGrDb = sustainedTopOffTarget
                + (d.fastGrDb - sustainedTopOffTarget)
                    * (1.0f - (1.0f - blendedTopOffAttack) * continuousChargeSupport * (limit ? 1.0f : topOffStartupScale));
        }
        const float chargeInputLevelDb = gainToDecibels(
            std::max(d.chargePeak, 1.0e-12f));
        const float chargeTargetGrDb = pr <= 10.0f ? 0.0f
            : optoCurveDb(chargeInputLevelDb - thresholdDb, limit);
        const float chargedTotalGrDb
            = d.fastGrDb + d.midGrDb + d.slowGrDb;
        // A settled optical cell accepts a much larger two-millisecond charge
        // than a cell in ongoing programme.  The continuous activity weight
        // above owns that distinction.  Capacity is the joint fit to the 16
        // clean pedestal/event cells: state suppression is approximately
        // -0.75 dB of lift per dB of standing GR, event-level growth is
        // sub-linear, and the low-PR exponent preserves the PR 0.40 cells.
        // Limit is excluded because its transient grid is separately fitted.
        const float isolatedNormalisedDrive = pr * 0.01f;
        const float isolatedBusyPosition = std::clamp(
            (d.programmeActivity
                    / std::max(isolatedNormalisedDrive, 0.10f)
                - 0.11f) / 0.04f,
            0.0f, 1.0f);
        const float isolatedBusyWeight = isolatedBusyPosition
            * isolatedBusyPosition * (3.0f - 2.0f * isolatedBusyPosition);
        const float isolatedFluctuationSupport = std::clamp(
            fluctuationDb / 1.0f, 0.0f, 1.0f);
        const float isolatedEventDriveBase = std::clamp(
            (isolatedNormalisedDrive - 0.10f) / 0.60f, 0.0f, 1.0f);
        const float isolatedEventDrive = std::pow(
            isolatedEventDriveBase, 2.3f);
        const float isolatedEventLevel = 1.0f - fastLevelBlend;
        const float isolatedEventLevelSquared
            = isolatedEventLevel * isolatedEventLevel;
        const float isolatedEventCapacityDb = isolatedEventDrive
            * ((13.17391332f + 12.06835352f * isolatedEventLevel
                    + -9.619465215f * isolatedEventLevelSquared)
                    * std::exp(-standingGrDb / 4.6f)
);
        const float isolatedTargetSupport = std::clamp(
            chargeTargetGrDb - standingGrDb, 0.0f, 1.0f);
        const float isolatedEventSupport = (limit ? 0.0f : 1.0f)
            * sustainedExposureBlend
            * (1.0f - isolatedBusyWeight) * isolatedFluctuationSupport
            * fastChargeSupport * isolatedTargetSupport;
        const float isolatedEventTargetDb
            = isolatedEventCapacityDb * isolatedEventSupport;
        if (isolatedEventTargetDb > d.isolatedEventGrDb)
            d.isolatedEventGrDb = isolatedEventTargetDb
                + (d.isolatedEventGrDb - isolatedEventTargetDb)
                    * optoIsolatedEventAttack;
        else
            // Continuing excitation retains its supported charge. Draining
            // toward zero here truncated the measured 5 ms event response.
            d.isolatedEventGrDb = isolatedEventTargetDb
                + (d.isolatedEventGrDb - isolatedEventTargetDb)
                    * optoIsolatedEventRelease;
        const float eventExcessGrDb = std::max(
            chargedTotalGrDb - chargeTargetGrDb, 0.0f);
        if (eventExcessGrDb > 0.0f && chargedTotalGrDb > 1.0e-9f)
        {
            const float quietReleaseSeconds = 0.007398957047f
                + (0.03018757556f - 0.007398957047f)
                    * std::clamp(eventExcessGrDb / 15.0f, 0.0f, 1.0f);
            const float busyReleaseSeconds = 0.007f + (0.024f - 0.007f)
                * std::clamp(eventExcessGrDb / 15.0f, 0.0f, 1.0f);
            const float eventReleaseSeconds = quietReleaseSeconds
                + isolatedBusyWeight * (busyReleaseSeconds - quietReleaseSeconds);
            const float eventRelease = std::exp(
                -optoInvSampleRate / eventReleaseSeconds);
            // Settled events use the full measured excess drain.  Ongoing
            // programme continuously approaches the PR-squared drain that
            // closes the five-point live A/B; a small low-state term prevents
            // PR 0.40 from being under-compressed.  The target/charge gap adds
            // drain while a settled event is ending, without narrowing the
            // sustained-tone or busy-programme paths.
            const float normalisedDrive = pr * 0.01f;
            const float lowStateDrainPosition = std::clamp(
                (6.0f - chargedTotalGrDb) / 4.0f, 0.0f, 1.0f);
            const float lowStateDrainWeight = lowStateDrainPosition
                * lowStateDrainPosition
                * (3.0f - 2.0f * lowStateDrainPosition);
            const float driveDrainScale = std::min(
                normalisedDrive * normalisedDrive
                    + 0.20f * lowStateDrainWeight,
                1.0f);
            const float normalisedProgrammeActivity = d.programmeActivity
                / std::max(normalisedDrive, 0.10f);
            const float busyDrainPosition = std::clamp(
                (normalisedProgrammeActivity - 0.11f) / 0.04f,
                0.0f, 1.0f);
            const float busyDrainWeight = busyDrainPosition
                * busyDrainPosition * (3.0f - 2.0f * busyDrainPosition);
            const float programmeDrainScale = 1.0f
                + busyDrainWeight * (driveDrainScale - 1.0f);
            const float eventDrainPosition = std::clamp(
                (targetGrDb - chargeTargetGrDb - 1.0f) / 1.25f,
                0.0f, 1.0f);
            const float eventDrainDemand = eventDrainPosition
                * eventDrainPosition * (3.0f - 2.0f * eventDrainPosition);
            const float eventFluctuationSupport = std::clamp(
                fluctuationDb / 0.50f, 0.0f, 1.0f);
            const float supplementalDrainSupport = fastChargeSupport
                * (1.0f - busyDrainWeight) * eventDrainDemand
                * eventFluctuationSupport;
            const float eventDrainSupport = 1.0f - fastChargeSupport
                + supplementalDrainSupport;
            const float programmeDrainReadiness = settledEventDrainWeight
                + 0.50f * (1.0f - settledEventDrainWeight) * busyDrainWeight * sustainedExposureBlend;
            const float drainEngagement = programmeDrainReadiness
                * programmeDrainScale * eventDrainSupport;
            const float excessFraction = eventExcessGrDb / chargedTotalGrDb;
            const float drainStep = drainEngagement * (1.0f - eventRelease);
            d.fastGrDb -= drainStep * ((1.0f - busyDrainWeight)
                * std::max(d.fastGrDb - fastShare * chargeTargetGrDb, 0.0f)
                + busyDrainWeight * d.fastGrDb * excessFraction);
            d.midGrDb -= drainStep * ((1.0f - busyDrainWeight)
                * std::max(d.midGrDb - midShare * chargeTargetGrDb, 0.0f)
                + busyDrainWeight * d.midGrDb * excessFraction);
            d.slowGrDb -= drainStep * busyDrainWeight
                * std::max(d.slowGrDb - d.slowEventGrDb, 0.0f) * excessFraction;
        }
        // Tag newly acquired quiet-event charge inside the slow pool.
        // Keep the settled baseline while the tagged afterglow recovers.
        if (d.slowGrDb > previousSlowGrDb)
            d.slowEventGrDb += (d.slowGrDb - previousSlowGrDb)
                * slowQuietWeight * isolatedFluctuationSupport;
        else if (previousSlowGrDb > 1.0e-9f)
            d.slowEventGrDb *= d.slowGrDb / previousSlowGrDb;
        const float slowBaselineDb = std::max(d.slowGrDb - d.slowEventGrDb, 0.0f) / slowShare;
        const float afterglowSeconds = 0.075f + 0.110f * std::exp(-slowBaselineDb / 5.0f);
        const float eventAfterglowLoss = d.slowEventGrDb
            * (1.0f - std::exp(-optoInvSampleRate / afterglowSeconds));
        d.slowGrDb -= eventAfterglowLoss;
        d.slowEventGrDb -= eventAfterglowLoss;
        const float dynamicGrDb = std::max(
            0.0f, d.fastGrDb + d.midGrDb + d.slowGrDb
                + d.isolatedEventGrDb);
        const float dynamicMeasuredGain = decibelsToGain(-dynamicGrDb);
        const float floorPowerRatio = std::clamp(
            d.floorPower / std::max(d.inputPower, 1.0e-12f)
                * (limit ? 1.155625f : 1.0f), 0.0f, 1.0f);
        const float floorExponent = limit ? 1.662649637f : 2.146063511f;
        const float gainPower = std::pow(dynamicMeasuredGain, floorExponent);
        d.gain = std::pow(gainPower + (1.0f - gainPower)
            * std::pow(floorPowerRatio, floorExponent * 0.5f), 1.0f / floorExponent);
        if (!std::isfinite(d.gain)) d.gain = 1.0f;
        d.inputLevelDb = inputLevelDb;
        d.dynamicGrDb = dynamicGrDb;
        return d.gain;
    }

    float gain(int ch) const noexcept
    {
        return channels[static_cast<size_t>(ch)].gain;
    }

    float inputLevelDb(int ch) const noexcept
    {
        return channels[static_cast<size_t>(ch)].inputLevelDb;
    }

    float dynamicGrDb(int ch) const noexcept
    {
        return channels[static_cast<size_t>(ch)].dynamicGrDb;
    }

private:
    struct ChannelState
    {
        float gain = 1, detectorLevel = 0, detectorPeak = 0, chargePeak = 0;
        // Detector energy for the measured low-frequency control-gain floor.
        float floorHighPassLow = 0, floorLow = 0, floorBandLow = 0;
        float floorPower = 0, inputPower = 0;
        float depthInputPower = 0, depthOutputPower = 0;
        float fastGrDb = 0, midGrDb = 0, slowGrDb = 0;
        float isolatedEventGrDb = 0;
        // A tagged subset of slowGrDb, with the measured short-event decay.
        float slowEventGrDb = 0;
        float fastSustainedTargetDb = 0;
        float fastAttackReferencePhase = 0;
        float programmeMemory = 0;
        float previousCellGrDb = 0, previousSustainedTargetDb = 0;
        float recentEventCharge = 1;
        float programmeMotion = 0, programmeActivity = 0;
        float detectorFloorPeak = 0, nextEventWeight = 1;
        int detectorExposureSamples = 0, detectorFloorOnlySamples = 0;
        int detectorUnsupportedSamples = 0;
        int detectorReleaseExposureSamples = 0;
        bool detectorEventActive = false;
        // Starts saturated so a freshly reset state counts as silent.
        int detectorSilentSamples = 1 << 20;
        // This sample's results for the colour stage; not history.
        float inputLevelDb = 0, dynamicGrDb = 0;
    };

    std::array<ChannelState, kChannels> channels{};
    static constexpr size_t kOptoDetectorSections = 5;
    std::array<std::array<Biquad, kOptoDetectorSections>, kChannels> optoDetectorWeighting;
    std::array<std::array<Biquad, 4>, kChannels> optoDepthWeighting;
    std::array<Biquad, kChannels> optoLimitFloorFilter;
    float optoInvSampleRate = 1.0f / 48000.0f;
    float optoDetectorAttack = 0, optoDetectorRelease = 0;
    float optoDetectorFloorPeakAttack = 0, optoDetectorFloorPeakRelease = 0;
    float optoDetectorPeakAttack = 0, optoDetectorPeakRelease = 0;
    float optoChargePeakRelease = 0;
    int optoChargeTopOffSamples = 1, optoFastPathSamples = 1;
    int optoDetectorSilenceHoldSamples = 1, optoDetectorFloorHoldSamples = 1;
    float optoSlowAttack = 0;
    float optoSustainedTargetSmoothing = 0, optoSustainedTopOffAttack = 0;
    float optoLimitFastTopOffAttack = 0, optoLimitSlowTopOffAttack = 0;
    float optoFastAttackAtCalibrationRate = 0;
    float optoCalibrationRateRatio = 1;
    float optoFlashRelease = 0, optoFastRelease = 0;
    float optoMidRelease = 0, optoSlowRelease = 0;
    float optoProgrammeMemoryRelease = 0;
    float optoRecentEventChargeRelease = 0, optoRecentEventChargeReset = 0;
    float optoProgrammeMotionRelease = 0, optoProgrammeActivityAttack = 0;
    float optoIsolatedEventAttack = 0, optoIsolatedEventRelease = 0;
    float optoFloorHighPassStep = 0, optoFloorLowPassStep = 0;
    float optoFloorBandStep = 0, optoFloorPowerStep = 0;
};

} // namespace duskaudio
