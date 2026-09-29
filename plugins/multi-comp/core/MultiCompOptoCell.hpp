// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
// OPTO (LA-2A) cell: CANDIDATE from package T4-P4c (2026-09-23). Lab-core use only.
//
// FEEDBACK T4 cell with the production statics frozen in:
//   side chain  d = useOptoDetector ? optoDetector : (external ? sidechain : input)
//               Compress u = (g_cell[n-1] + b_c) * d      Limit u = (a * g_cell[n-1] + b) * d
//               loop = LF_GR(Depth_GR(W(u)))  W = the production 5-biquad detector weighting (exact
//               coefficients), unity at 1 kHz; Depth = production optoDepthWeighting fed by the cell's own
//               reduction at n-1; LF_GR = the LF detector-law term, a low shelf blended in by the same
//               reduction: loop += clamp(GR / span, 0, 1) * (Shelf(loop) - loop)
//               s = 10^(P(pr)/20) * loop
//   EL law      (R) l *= (P_slew / P_drive)^(kappa / 2): brightness ~ f^kappa, exactly 1 at 1 kHz
//               (S) or, with elRevHysteresis > 0, l *= (f_half / 1 kHz)^kappa from significant reversals
//               (T) elFreqCorner > 0: slew low-passed at the corner first (brightness flattens above it)
//   traps       (U) G_1 <-> T: capture k_t (1 - T/T_max) G_1, emission k_r T (statics unchanged)
//   element     (P) pStrength > 0: applied *= 1 - c w over(|x|) / |x|, c = pStrength (PR/100)^pPrExp applied
//   EL panel    e += beta * (|s| - e); l = Lambda(20 log10 e) (static-constrained light table, solved
//               exactly on the ripple of the settled 1 kHz sine); optional light ceiling l / (1 + l / Lmax).
//   CdS         h = 1 / (1 + (l / l_h)^m)   (equilibrium-preserving time-scale modulation)
//               G_i = (G_i + h dt A_i l) / (1 + h dt (R_i + B_i G_i)) for each population
//   conductance C = sum G_i, or with the split static C = Psi(20 log10 sum G_i) (conductance table)
//   divider     g_cell = 1 / (1 + C)   (the feedback loop taps g_cell)
//   LF floor    production energy ratio r(f) of the pre-gain detector and gain blend
//               g = (g_cell^k + (1 - g_cell^k) r^(k/2))^(1/k), returned for the NEXT sample.
// With full stereo link g_L == g_R, so the per-channel cell forms the linked feedback side chain as
// g_cell[n-1] * optoDetector. Interface as defined by T4-P1-CELL: process(...) returns the next-sample
// gain; gain(), inputLevelDb(), dynamicGrDb() (the cell reduction before the LF floor, as production).
// setParams()/params()/primeEquilibrium()/lightAt()/conductanceAt()/elLevel() are lab-only hooks used by the
// fitting kernel; OPTO_LAB_PROBE is empty unless a lab build defines it.
#pragma once
#include "../../shared-daf/dsp/DuskFilters.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#ifndef OPTO_LAB_PROBE
#define OPTO_LAB_PROBE(channel, appliedGain, cellGainValue)
#endif

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

// ---- BEGIN GENERATED DEFAULTS (Q) ----
// Production TFU1, landed from the frozen lab fit on 2026-09-28
// kOptoCellDefaults order: elTauSeconds, lightCeiling, fastRelease, fastBimolecular, slowCharge, slowRelease, slowBimolecular, memoryCharge, memoryRelease, limitOutputMix, limitInputMix, compressInputMix, timeScaleLight, timeScaleExponent, depthWeighting, lowFrequencyFloor, lfTermGainDb, lfTermFrequency, lfTermGrSpan, conductanceTableOn, flashCharge, flashRelease, flashBimolecular, lfShelfDb, hFloor, lfLitTauAttack, lfLitTauRelease, lfLitLevelDb, lfLitSpanDb, lfLitFloor, lfLitLog, hChargeOnly, attackRateDbPerS, attackRatePower, attackRateTau, elFreqExponent, elFreqTau, hfPeakDb, lfPeakDb, midPeakDb, hf10kPeakDb, elRevHysteresis, elRevRelease, elFreqCorner, trapCapture, trapEmit, trapCapacity, pStrength, pPrExp, pThresh, pKnee, pAsym, lightTableStartDb, lightTableStepDb, lightSlopeAbove, conductanceTableStartDb, conductanceTableStepDb, conductanceSlopeAbove, prStart, prStep, prSlopeBelow, prGate
inline constexpr std::array<float, 62> kOptoCellDefaults{{
    2.11744767e-06f, 0.0f, 9.61066185f, 1.19941853f, 5.8555379f, 29.7343009f,
    0.815240487f, 0.00118519559f, 0.999977779f, 0.990760822f, 0.0365957224f, 0.0f,
    5.7612485f, 0.271498753f, 1.0f, 1.0f, -1.21699251f, 379.874676f,
    40.0f, 0.0f, 0.783605349f, 287.614327f, 0.0992242707f, 6.85569138f,
    0.0f, 0.0193125313f, 0.020685854f, -28.593274f, 42.5204821f, 0.0f,
    1.0f, 0.0f, 0.0f, 2.0f, 0.01f, 0.197542329f,
    0.000508995409f, 1.55828697f, 0.301784265f, 0.00367525373f, -1.19515582f, 0.0f,
    0.02f, 2109.09771f, 62.0298299f, 2.85700604f, 14.1897856f, 0.0f,
    10.0f, 0.047f, 0.1f, 0.3f, -59.0f, 1.0f,
    0.082666399f, 0.0f, 0.1f, 0.0f, 0.3125f, 0.015625f,
    59.1923887f, 0.15f,
}};

inline constexpr std::array<float, 37> kOptoCellLightTable{{
    0.0f, 0.00912684295f, 0.0119820964f, 0.0055029667f, 0.00670960546f, 0.00696813967f,
    0.00804904569f, 0.0093537569f, 0.0112371184f, 0.0152263958f, 0.0775384083f, 0.359695494f,
    0.89149797f, 2.84241629f, 6.8655405f, 14.5869112f, 31.5150318f, 63.6852684f,
    133.770477f, 229.237335f, 339.253937f, 462.929565f, 680.991394f, 987.750671f,
    1365.13977f, 1858.52783f, 2430.92554f, 3437.19482f, 3202.54712f, 3421.12549f,
    2995.16431f, 2868.4541f, 2989.75293f, 2424.57397f, 2666.40479f, 2836.38477f,
    3145.78638f,
}};

inline constexpr std::array<float, 2> kOptoCellConductanceTable{{
    0.0f, 0.0f,
}};

inline constexpr std::array<float, 45> kOptoCellPrGainDb{{
    -33.9895821f, -33.0647011f, -31.7282295f, -30.5767021f, -29.6676674f, -28.8836613f,
    -28.1747055f, -27.5470505f, -26.8787422f, -26.1037903f, -25.3618774f, -24.7241745f,
    -24.0803757f, -23.129549f, -22.2184792f, -21.4799328f, -20.9825573f, -20.4134197f,
    -19.8114147f, -19.2458191f, -18.5005341f, -17.7552509f, -16.0216465f, -14.4301796f,
    -12.8165121f, -11.2860289f, -9.74315739f, -8.65912247f, -7.55919933f, -6.66117764f,
    -6.04502869f, -5.35649109f, -4.63854742f, -3.85050201f, -3.09465241f, -2.24689794f,
    -1.72590232f, -1.26240313f, -0.875841439f, -0.556340396f, -0.211995497f, 0.0f,
    0.0f, 0.0f, 0.0f,
}};

// ---- END GENERATED DEFAULTS (Q) ----

struct OptoCellParams
{
    float elTauSeconds, lightCeiling;
    float fastRelease, fastBimolecular;
    float slowCharge, slowRelease, slowBimolecular;
    float memoryCharge, memoryRelease;
    float limitOutputMix, limitInputMix, compressInputMix;
    float timeScaleLight, timeScaleExponent;
    float depthWeighting, lowFrequencyFloor;
    float lfTermGainDb, lfTermFrequency, lfTermGrSpan;
    float conductanceTableOn;
    float flashCharge, flashRelease, flashBimolecular;
    float lfShelfDb, hFloor;
    float lfLitTauAttack, lfLitTauRelease, lfLitLevelDb, lfLitSpanDb, lfLitFloor, lfLitLog, hChargeOnly;
    float attackRateDbPerS, attackRatePower, attackRateTau;
    float elFreqExponent, elFreqTau, hfPeakDb, lfPeakDb, midPeakDb, hf10kPeakDb;
    float elRevHysteresis, elRevRelease, elFreqCorner;
    float trapCapture, trapEmit, trapCapacity;
    float pStrength, pPrExp, pThresh, pKnee, pAsym;
    const float* lightTable;
    int lightTableSize;
    float lightTableStartDb, lightTableStepDb, lightSlopeAbove;
    const float* conductanceTable;
    int conductanceTableSize;
    float conductanceTableStartDb, conductanceTableStepDb, conductanceSlopeAbove;
    const float* prGainDb;
    int prGainRows;
    float prStart, prStep, prSlopeBelow, prGate;
};

inline OptoCellParams defaultOptoCellParams() noexcept
{
    OptoCellParams p{};
    float* lead = &p.elTauSeconds;
    for (int i = 0; i < 52; ++i) lead[i] = kOptoCellDefaults[static_cast<size_t>(i)];
    p.lightTable = kOptoCellLightTable.data();
    p.lightTableSize = static_cast<int>(kOptoCellLightTable.size());
    p.lightTableStartDb = kOptoCellDefaults[52];
    p.lightTableStepDb = kOptoCellDefaults[53];
    p.lightSlopeAbove = kOptoCellDefaults[54];
    p.conductanceTable = kOptoCellConductanceTable.data();
    p.conductanceTableSize = static_cast<int>(kOptoCellConductanceTable.size());
    p.conductanceTableStartDb = kOptoCellDefaults[55];
    p.conductanceTableStepDb = kOptoCellDefaults[56];
    p.conductanceSlopeAbove = kOptoCellDefaults[57];
    p.prGainDb = kOptoCellPrGainDb.data();
    p.prGainRows = static_cast<int>(kOptoCellPrGainDb.size());
    p.prStart = kOptoCellDefaults[58];
    p.prStep = kOptoCellDefaults[59];
    p.prSlopeBelow = kOptoCellDefaults[60];
    p.prGate = kOptoCellDefaults[61];
    return p;
}

class OptoCell
{
public:
    static constexpr int kChannels = 2;
    static constexpr int kPopulations = 4;   // fast, slow, exposure memory, flash

    OptoCell() noexcept { cellParams = defaultOptoCellParams(); }

    // Coefficients at the processing rate; history is kept.
    void setRate(float sr) noexcept
    {
        sampleRate = static_cast<double>(sr);
        dt = 1.0 / sampleRate;
        // Production detector weighting (exact coefficients).
        const std::array<BiquadCoeffs, kWeightingSections> weightingCoeffs{{
            Biquad::shelf(sr, 319.1844220f, cellParams.lfShelfDb, 0.5894442553f, false),
            Biquad::peak(sr, 134.4305880f, cellParams.lfPeakDb, 0.5136042617f),
            Biquad::peak(sr, 880.5758706f, cellParams.midPeakDb, 0.6986416568f),
            Biquad::peak(sr, 5840.123777f, cellParams.hfPeakDb, 0.4820592696f),
            Biquad::peak(sr, 9991.669467f, cellParams.hf10kPeakDb, 0.7988600998f)
        }};
        double magnitudeAt1k = 1.0;
        const double w1k = 6.283185307179586 * 1000.0 / sampleRate;
        for (size_t section = 0; section < kWeightingSections; ++section)
        {
            for (auto& channel : loopWeighting) channel[section].setCoeffs(weightingCoeffs[section]);
            for (auto& channel : levelWeighting) channel[section].setCoeffs(weightingCoeffs[section]);
            magnitudeAt1k *= loopWeighting[0][section].magnitude(w1k);
        }
        weightingNormalisation = static_cast<float>(1.0 / magnitudeAt1k);
        {   // Colour-stage level weighting keeps the production W, independent of the loop's lfShelfDb.
            const auto productionShelf = Biquad::shelf(sr, 319.1844220f, 4.73782359f, 0.5894442553f, false);
            for (auto& channel : levelWeighting) channel[0].setCoeffs(productionShelf);
            const std::array<BiquadCoeffs, 4> productionPeaks{{
                Biquad::peak(sr, 134.4305880f, 0.59151688f, 0.5136042617f),
                Biquad::peak(sr, 880.5758706f, -0.4288385533f, 0.6986416568f),
                Biquad::peak(sr, 5840.123777f, 1.410524878f, 0.4820592696f),
                Biquad::peak(sr, 9991.669467f, -1.407323237f, 0.7988600998f)}};
            for (auto& channel : levelWeighting)
                for (size_t section = 1; section < kWeightingSections; ++section)
                    channel[section].setCoeffs(productionPeaks[section - 1]);
            double levelAt1k = 1.0;
            for (size_t section = 0; section < kWeightingSections; ++section)
                levelAt1k *= levelWeighting[0][section].magnitude(w1k);
            levelNormalisation = static_cast<float>(1.0 / levelAt1k);
        }
        {   // Lit-gated LF shelf: section 0 and sections 1..4 each unity at 1 kHz; tracker coefficients.
            const double s0 = loopWeighting[0][0].magnitude(w1k);
            shelfNormalisation = static_cast<float>(1.0 / s0);
            weightingNormalisation14 = static_cast<float>(s0 / magnitudeAt1k);
            litAttack = std::exp(-1.0 / (std::max(static_cast<double>(cellParams.lfLitTauAttack), 1.0e-6) * sampleRate));
            litRelease = std::exp(-1.0 / (std::max(static_cast<double>(cellParams.lfLitTauRelease), 1.0e-6) * sampleRate));
            attackRateStep = 1.0 - std::exp(-1.0 / (std::max(static_cast<double>(cellParams.attackRateTau), 1.0e-6) * sampleRate));
            elFreqStep = 1.0 - std::exp(-1.0 / (std::max(static_cast<double>(cellParams.elFreqTau), 1.0e-6) * sampleRate));
            slewNormalisation = 1.0 / (2.0 * std::sin(3.141592653589793 * 1000.0 / sampleRate));
            reversalRelease = std::exp(-1.0 / (std::max(static_cast<double>(cellParams.elRevRelease), 1.0e-6) * sampleRate));
            reversalHalfCycle1k = sampleRate / 2000.0;
            if (cellParams.elFreqCorner > 0.0f)
            {   // one-pole low-pass on the slew; |H(1 kHz)| normalised away so a 1 kHz sine keeps ratio 1
                slewLowPassStep = 1.0 - std::exp(-6.283185307179586 * static_cast<double>(cellParams.elFreqCorner) / sampleRate);
                const double w = 6.283185307179586 * 1000.0 / sampleRate;
                const double pole = 1.0 - slewLowPassStep;
                const double mag = slewLowPassStep / std::sqrt(1.0 - 2.0 * pole * std::cos(w) + pole * pole);
                slewLowPassNormalisation = 1.0 / mag;
            }
        }
        // Production optoDepthWeighting (exact coefficients).
        for (auto& channel : depthWeighting)
        {
            channel[0].setCoeffs(Biquad::shelf(sr, 2000.0f, 1.25f, 0.70710678f, true));
            channel[1].setCoeffs(Biquad::peak(sr, 20000.0f, 2.0f, 2.5f));
            channel[2].setCoeffs(Biquad::shelf(sr, 2200.0f, 2.4f, 0.70710678f, true));
            channel[3].setCoeffs(Biquad::peak(sr, 20000.0f, 1.0f, 2.5f));
        }
        // LF detector-law term (low shelf; blended by the cell reduction).
        for (auto& filter : lfTermFilter)
            filter.setCoeffs(Biquad::shelf(sr, cellParams.lfTermFrequency > 0.0f ? cellParams.lfTermFrequency : 100.0f,
                                           cellParams.lfTermGainDb, 0.70710678f, false));
        // Production LF floor energy path (exact coefficients).
        floorHighPassStep = 1.0f - std::exp(-6.283185307f * 30.0f / sr);
        floorLowPassStep = 1.0f - std::exp(-6.283185307f * 2.016362169f / sr);
        floorBandStep = 1.0f - std::exp(-6.283185307f * 1000.0f / sr);
        for (auto& filter : limitFloorFilter)
            filter.setCoeffs(Biquad::lowPass(sr, 300.0f, 0.70710678f));
        floorPowerStep = 1.0f - std::exp(-1.0f / (0.050f * sr));
        levelPeakAttack = std::exp(-1.0 / (0.000050 * sampleRate));
        levelPeakRelease = std::exp(-1.0 / (0.040 * sampleRate));
        elStep = 1.0 - std::exp(-1.0 / (static_cast<double>(cellParams.elTauSeconds) * sampleRate));
    }

    void reset() noexcept
    {
        for (auto& c : channels) c = ChannelState{};
        for (auto& channel : loopWeighting) for (auto& f : channel) f.reset();
        for (auto& channel : levelWeighting) for (auto& f : channel) f.reset();
        for (auto& channel : depthWeighting) for (auto& f : channel) f.reset();
        for (auto& f : lfTermFilter) f.reset();
        for (auto& f : limitFloorFilter) f.reset();
    }

    // This cell keeps no sample counters.
    template <typename ScaleCounter>
    void scaleCounters(const ScaleCounter&) noexcept {}

    // withApplied = false (lab kernel only) skips the applied-gain floor blend, which feeds no state; every
    // state update is identical, and the returned value is then the cell gain before the floor.
    template <bool withApplied = true>
    float process(int ch, float input, float /*compressed*/, float sidechain,
                  bool external, float optoDetector, bool useOptoDetector,
                  bool limit, float peakReduction) noexcept
    {
        const size_t c = static_cast<size_t>(ch);
        auto& d = channels[c];
        const float base = useOptoDetector ? optoDetector : (external ? sidechain : input);

        // Production LF floor energies from the pre-gain detector; both mode paths advance.
        d.floorHighPassLow += floorHighPassStep * (base - d.floorHighPassLow);
        d.floorLow += floorLowPassStep * (base - d.floorHighPassLow - d.floorLow);
        d.floorBandLow += floorBandStep * (d.floorLow - d.floorBandLow);
        const float limitFloorSignal = limitFloorFilter[c].process(d.floorLow);
        const float floorSignal = limit ? limitFloorSignal : d.floorBandLow;
        d.floorPower += floorPowerStep * (floorSignal * floorSignal - d.floorPower);
        d.inputPower += floorPowerStep * (base * base - d.inputPower);

        // Colour-stage level: pre-gain weighted detector through a 50 us / 40 ms peak follower.
        float levelSignal = base;
        for (auto& f : levelWeighting[c]) levelSignal = f.process(levelSignal);
        const double levelAbs = std::abs(static_cast<double>(levelSignal) * levelNormalisation);
        const double levelCoeff = levelAbs > d.levelPeak ? levelPeakAttack : levelPeakRelease;
        d.levelPeak = levelAbs + (d.levelPeak - levelAbs) * levelCoeff;

        // Feedback side chain on the cell gain (before the floor).
        const double gPrev = d.cellGain;
        const double mix = limit
            ? static_cast<double>(cellParams.limitOutputMix) * gPrev + cellParams.limitInputMix
            : gPrev + cellParams.compressInputMix;
        float loop = static_cast<float>(mix * base);
        if (cellParams.lfLitSpanDb > 0.0f)
        {
            // Lit-gated LF shelf: the LF sensitivity needs an illuminated photocell (tracked EL level at n-1).
            for (size_t section = 1; section < kWeightingSections; ++section) loop = loopWeighting[c][section].process(loop);
            loop *= weightingNormalisation14;
            const float shelved = loopWeighting[c][0].process(loop) * shelfNormalisation;
            const double litDb = cellParams.lfLitLog != 0.0f ? d.litLevel : 20.0 * std::log10(std::max(d.litLevel, 1.0e-12));
            const float engage = std::clamp(static_cast<float>((litDb - (cellParams.lightTableStartDb + cellParams.lfLitLevelDb))
                                                               / cellParams.lfLitSpanDb), 0.0f, 1.0f);
            const float w = cellParams.lfLitFloor + (1.0f - cellParams.lfLitFloor) * engage;
            loop += w * (shelved - loop);
        }
        else
        {
            for (auto& f : loopWeighting[c]) loop = f.process(loop);
            loop *= weightingNormalisation;
        }
        // Production optoDepthWeighting, fed by the cell reduction at n-1; both mode paths advance.
        const float priorReduction = static_cast<float>(d.grDb);
        const float compressShelfWeight = std::clamp((priorReduction - 8.0f) / 20.0f, 0.0f, 1.0f);
        const float limitShelfWeight = std::clamp((priorReduction - 12.0f) / 28.0f, 0.0f, 1.0f);
        const float depthPeakWeight = std::clamp((priorReduction - 8.0f) / 8.0f, 0.0f, 1.0f);
        auto& depth = depthWeighting[c];
        float compressDetector = loop + compressShelfWeight * (depth[0].process(loop) - loop);
        float limitDetector = loop + limitShelfWeight * (depth[2].process(loop) - loop);
        compressDetector += depthPeakWeight * (depth[1].process(compressDetector) - compressDetector);
        limitDetector += depthPeakWeight * (depth[3].process(limitDetector) - limitDetector);
        if (cellParams.depthWeighting != 0.0f)
            loop = limit ? limitDetector : compressDetector;
        // LF detector-law term, fed by the same reduction at n-1.
        if (cellParams.lfTermGainDb != 0.0f)
        {
            const float lfWeight = std::clamp(priorReduction / cellParams.lfTermGrSpan, 0.0f, 1.0f);
            loop += lfWeight * (lfTermFilter[c].process(loop) - loop);
        }

        const double prNorm = std::clamp(static_cast<double>(peakReduction) * 0.01, 0.0, 1.0);
        const bool inert = prNorm <= static_cast<double>(cellParams.prGate);
        if (prNorm != cachedPrNorm)
        {
            cachedPrNorm = prNorm;
            cachedSideChainGain = sideChainGain(prNorm);
        }
        const double s = inert ? 0.0 : cachedSideChainGain * static_cast<double>(loop);
        d.el += elStep * (std::abs(s) - d.el);
        if (cellParams.lfLitLog != 0.0f)
        {
            const double elDb = std::max(20.0 * std::log10(std::max(d.el, 1.0e-12)),
                                         static_cast<double>(cellParams.lightTableStartDb) - 60.0);
            d.litLevel = elDb + (d.litLevel - elDb) * (elDb > d.litLevel ? litAttack : litRelease);
        }
        else
            d.litLevel = d.el + (d.litLevel - d.el) * (d.el > d.litLevel ? litAttack : litRelease);
        double light = inert ? 0.0 : lightFor(d.el);
        if (cellParams.lightCeiling > 0.0f)
            light = light / (1.0 + light / static_cast<double>(cellParams.lightCeiling));
        if (cellParams.elFreqExponent != 0.0f && cellParams.elRevHysteresis > 0.0f)
        {
            // EL light is emitted at polarity reversals: the last completed half-cycle between significant
            // reversals (Schmitt trigger at +-h x peak |s|) sets (f / 1 kHz)^kappa, held to the next reversal.
            const double magnitude = std::abs(s);
            d.reversalPeak = magnitude > d.reversalPeak ? magnitude : d.reversalPeak * reversalRelease;
            const double threshold = static_cast<double>(cellParams.elRevHysteresis) * d.reversalPeak;
            ++d.reversalCount;
            if ((d.reversalSign > 0 && s < -threshold) || (d.reversalSign < 0 && s > threshold))
            {
                d.reversalSign = -d.reversalSign;
                d.reversalPhi = std::pow(reversalHalfCycle1k / static_cast<double>(d.reversalCount),
                                         static_cast<double>(cellParams.elFreqExponent));
                d.reversalCount = 0;
            }
            light *= d.reversalPhi;
        }
        else if (cellParams.elFreqExponent != 0.0f)
        {
            // EL brightness frequency law (AC electroluminescence, light ~ f^kappa at a given drive amplitude):
            // the drive's slew-to-level mean-square ratio estimates (f / 1 kHz)^2, exactly 1 for a settled 1 kHz sine.
            double slew = (s - d.drivePrev) * slewNormalisation;
            if (cellParams.elFreqCorner > 0.0f)
            {   // EL brightness flattens above the corner: low-pass the slew before its mean square
                d.slewLow += slewLowPassStep * (slew - d.slewLow);
                slew = d.slewLow * slewLowPassNormalisation;
            }
            d.drivePrev = s;
            d.drivePower += elFreqStep * (s * s - d.drivePower);
            d.slewPower += elFreqStep * (slew * slew - d.slewPower);
            const double ratio = std::clamp(d.slewPower / std::max(d.drivePower, 1.0e-30), 1.0e-4, 1.0e4);
            light *= std::pow(ratio, 0.5 * static_cast<double>(cellParams.elFreqExponent));
        }
        double step = dt;
        if (cellParams.timeScaleLight > 0.0f && cellParams.hFloor > 0.0f)
        {
            const double r = std::pow(std::max(light, 0.0) / static_cast<double>(cellParams.timeScaleLight),
                                      static_cast<double>(cellParams.timeScaleExponent));
            step = dt * (static_cast<double>(cellParams.hFloor) + r) / (1.0 + r);
        }
        else if (cellParams.timeScaleLight > 0.0f && light > 0.0)
            step = dt / (1.0 + std::pow(light / static_cast<double>(cellParams.timeScaleLight),
                                        static_cast<double>(cellParams.timeScaleExponent)));
        // Attack-rate limit (measured native attack saturation, ratelaw map): charge slows when the
        // ripple-averaged GR rise rate (from previous samples) approaches Smax; ~0 at steady state.
        const double rateScale = (cellParams.attackRateDbPerS > 0.0f && d.grRateSmooth > 0.0)
            ? 1.0 / (1.0 + std::pow(d.grRateSmooth / static_cast<double>(cellParams.attackRateDbPerS),
                                    static_cast<double>(cellParams.attackRatePower)))
            : 1.0;
        const double chargeStep = step * rateScale;
        const double releaseStep = cellParams.hChargeOnly != 0.0f ? dt : step;
        d.population[0] = (d.population[0] + chargeStep * light)
            / (1.0 + releaseStep * (cellParams.fastRelease + cellParams.fastBimolecular * d.population[0]));
        d.population[1] = (d.population[1] + chargeStep * cellParams.slowCharge * light)
            / (1.0 + releaseStep * (cellParams.slowRelease + cellParams.slowBimolecular * d.population[1]));
        if (cellParams.memoryCharge > 0.0f)
            d.population[2] = (d.population[2] + chargeStep * cellParams.memoryCharge * light)
                / (1.0 + releaseStep * cellParams.memoryRelease);
        if (cellParams.flashCharge > 0.0f)
            d.population[3] = (d.population[3] + chargeStep * cellParams.flashCharge * light)
                / (1.0 + releaseStep * (cellParams.flashRelease + cellParams.flashBimolecular * d.population[3]));
        if (cellParams.trapCapture > 0.0f)
        {
            // CdS trap filling: population-1 carriers are captured into traps and re-emitted. Capture equals
            // emission at equilibrium (statics unchanged); filled traps sustain the conductance after exposure.
            const double fill = cellParams.trapCapacity > 0.0f
                ? std::max(0.0, 1.0 - d.trap / static_cast<double>(cellParams.trapCapacity)) : 1.0;
            const double capture = static_cast<double>(cellParams.trapCapture) * fill * d.population[1] * dt;
            const double emit = static_cast<double>(cellParams.trapEmit) * d.trap * dt;
            d.population[1] = std::max(0.0, d.population[1] - capture + emit);
            d.trap = std::max(0.0, d.trap + capture - emit);
        }
        double conductance = 0.0;
        for (auto& pop : d.population)
        {
            if (pop < 1.0e-30) pop = 0.0;          // no denormal tails after release
            conductance += pop;
        }
        if (cellParams.conductanceTableOn != 0.0f) conductance = conductanceFor(conductance);   // split static Psi
        d.cellGain = 1.0 / (1.0 + conductance);
        {
            const double newGr = 8.685889638065035 * std::log1p(conductance);
            if (cellParams.attackRateDbPerS > 0.0f)
                d.grRateSmooth += attackRateStep * ((newGr - d.grDb) / dt - d.grRateSmooth);
            d.grDb = newGr;
        }

        // Production LF floor gain blend on the applied gain.
        float applied = static_cast<float>(d.cellGain);
        if (!withApplied) return applied;
        if (cellParams.lowFrequencyFloor != 0.0f)
        {
            const float floorPowerRatio = std::clamp(
                d.floorPower / std::max(d.inputPower, 1.0e-12f) * (limit ? 1.155625f : 1.0f), 0.0f, 1.0f);
            const float floorExponent = limit ? 1.662649637f : 2.146063511f;
            const float gainPower = std::pow(applied, floorExponent);
            applied = std::pow(gainPower + (1.0f - gainPower)
                * std::pow(floorPowerRatio, floorExponent * 0.5f), 1.0f / floorExponent);
            if (!std::isfinite(applied)) applied = 1.0f;
        }
        if (cellParams.pStrength > 0.0f)
        {   // (P) per-channel input-threshold element: asymmetric soft knee on this input sample, ahead of the
            // cell, sized by the PR control and by the cell transmission; gains the next oversampled sample
            const float ax = std::abs(input);
            const float vt = cellParams.pThresh;
            const float kw = std::max(cellParams.pKnee * vt, 0.0f);
            float over = 0.0f;
            if (ax >= vt + 0.5f * kw) over = ax - vt;
            else if (kw > 0.0f && ax > vt - 0.5f * kw)
            {
                const float u = ax - vt + 0.5f * kw;
                over = u * u / (2.0f * kw);
            }
            if (over > 0.0f)
            {
                const float prn = std::clamp(peakReduction * 0.01f, 0.0f, 1.0f);
                const float w = input >= 0.0f ? 1.0f : cellParams.pAsym;
                const float c = cellParams.pStrength * std::pow(prn, cellParams.pPrExp) * applied;
                applied *= std::max(1.0f - c * w * over / std::max(ax, 1.0e-12f), 0.0f);
            }
        }
        d.gain = applied;
        OPTO_LAB_PROBE(ch, applied, d.cellGain);
        return applied;
    }

    float gain(int ch) const noexcept { return channels[static_cast<size_t>(ch)].gain; }
    // The colour-stage level of the last processed sample (converted on read; same value as a stored conversion).
    float inputLevelDb(int ch) const noexcept
    {
        return gainToDecibels(static_cast<float>(channels[static_cast<size_t>(ch)].levelPeak));
    }
    float dynamicGrDb(int ch) const noexcept { return static_cast<float>(channels[static_cast<size_t>(ch)].grDb); }

    // ---- lab-only hooks (fitting kernel) ----
    void setParams(const OptoCellParams& p) noexcept
    {
        cellParams = p;
        cachedPrNorm = -1.0;
        if (sampleRate > 0.0) setRate(static_cast<float>(sampleRate));
    }
    const OptoCellParams& params() const noexcept { return cellParams; }
    void primeEquilibrium(int ch, double el, const double* population) noexcept
    {
        auto& d = channels[static_cast<size_t>(ch)];
        d.el = el;
        d.litLevel = cellParams.lfLitLog != 0.0f ? std::max(20.0 * std::log10(std::max(el, 1.0e-12)), static_cast<double>(cellParams.lightTableStartDb) - 60.0) : el;
        d.grRateSmooth = 0.0;
        d.drivePower = el * el;
        d.slewPower = el * el;
        d.drivePrev = 0.0;
        d.reversalPeak = el;
        d.slewLow = 0.0;
        if (cellParams.trapCapture > 0.0f)
        {
            const double g1 = population[1];
            const double kt = cellParams.trapCapture, kr = cellParams.trapEmit, tm = cellParams.trapCapacity;
            d.trap = tm > 0.0 ? kt * g1 / (kr + kt * g1 / tm) : (kr > 0.0 ? kt * g1 / kr : 0.0);
        }
        else
            d.trap = 0.0;
        d.reversalPhi = 1.0;
        d.reversalSign = 1;
        d.reversalCount = 0;
        double conductance = 0.0;
        for (int i = 0; i < kPopulations; ++i)
        {
            d.population[static_cast<size_t>(i)] = population[i];
            conductance += population[i];
        }
        if (cellParams.conductanceTableOn != 0.0f) conductance = conductanceFor(conductance);
        d.cellGain = 1.0 / (1.0 + conductance);
        d.gain = static_cast<float>(d.cellGain);
        d.grDb = 8.685889638065035 * std::log1p(conductance);
    }
    double cellGain(int ch) const noexcept { return channels[static_cast<size_t>(ch)].cellGain; }
    double elLevel(int ch) const noexcept { return channels[static_cast<size_t>(ch)].el; }
    double lightAt(double el) const noexcept { return lightFor(el); }
    double conductanceAt(double sumG) const noexcept { return conductanceFor(sumG); }

private:
    static constexpr size_t kWeightingSections = 5;

    struct ChannelState
    {
        double cellGain = 1.0, grDb = 0.0, el = 0.0, levelPeak = 0.0, litLevel = 0.0, grRateSmooth = 0.0;
        double drivePower = 0.0, slewPower = 0.0, drivePrev = 0.0;
        double reversalPeak = 0.0, reversalPhi = 1.0, slewLow = 0.0, trap = 0.0;
        int reversalSign = 1, reversalCount = 0;
        std::array<double, kPopulations> population{};
        float gain = 1.0f;
        float floorHighPassLow = 0.0f, floorLow = 0.0f, floorBandLow = 0.0f;
        float floorPower = 0.0f, inputPower = 0.0f;
    };

    double sideChainGain(double prNorm) const noexcept
    {
        const double position = (prNorm - cellParams.prStart) / cellParams.prStep;
        double db;
        if (position <= 0.0)
            db = cellParams.prGainDb[0] + cellParams.prSlopeBelow * (prNorm - cellParams.prStart);
        else if (position >= cellParams.prGainRows - 1)
            db = cellParams.prGainDb[cellParams.prGainRows - 1];
        else
        {
            const int k = static_cast<int>(position);
            const double f = position - k;
            db = cellParams.prGainDb[k] + f * (cellParams.prGainDb[k + 1] - cellParams.prGainDb[k]);
        }
        return std::pow(10.0, db * 0.05);
    }

    // Piecewise-linear table over a dB axis: zero one step below the start, linear ramp to t[0] at the start,
    // exponential continuation above the top.
    static double tableLookup(const float* t, int n, double start, double step, double slopeAbove,
                              double levelDb) noexcept
    {
        const double top = start + step * (n - 1);
        if (levelDb >= top)
            return t[n - 1] * std::exp(slopeAbove * (levelDb - top));
        if (levelDb <= start - step) return 0.0;
        if (levelDb < start) return t[0] * (levelDb - (start - step)) / step;
        const double position = (levelDb - start) / step;
        const int k = std::min(static_cast<int>(position), n - 2);
        const double f = position - k;
        return t[k] + f * (t[k + 1] - t[k]);
    }

    double lightFor(double el) const noexcept
    {
        if (el <= 1.0e-30) return 0.0;
        return tableLookup(cellParams.lightTable, cellParams.lightTableSize, cellParams.lightTableStartDb,
                           cellParams.lightTableStepDb, cellParams.lightSlopeAbove,
                           8.685889638065035 * std::log(el));
    }

    double conductanceFor(double sumG) const noexcept
    {
        if (sumG <= 1.0e-30) return 0.0;
        return tableLookup(cellParams.conductanceTable, cellParams.conductanceTableSize,
                           cellParams.conductanceTableStartDb, cellParams.conductanceTableStepDb,
                           cellParams.conductanceSlopeAbove, 8.685889638065035 * std::log(sumG));
    }

    OptoCellParams cellParams{};
    std::array<ChannelState, kChannels> channels{};
    std::array<std::array<Biquad, kWeightingSections>, kChannels> loopWeighting;
    std::array<std::array<Biquad, kWeightingSections>, kChannels> levelWeighting;
    std::array<std::array<Biquad, 4>, kChannels> depthWeighting;
    std::array<Biquad, kChannels> lfTermFilter;
    std::array<Biquad, kChannels> limitFloorFilter;
    double sampleRate = 0.0, dt = 1.0 / 96000.0, elStep = 0.0;
    double levelPeakAttack = 0.0, levelPeakRelease = 0.0;
    double cachedPrNorm = -1.0, cachedSideChainGain = 0.0;
    float weightingNormalisation = 1.0f;
    float levelNormalisation = 1.0f;
    float shelfNormalisation = 1.0f, weightingNormalisation14 = 1.0f;
    double litAttack = 0.0, litRelease = 0.0;
    double attackRateStep = 1.0;
    double elFreqStep = 1.0, slewNormalisation = 1.0;
    double reversalRelease = 0.0, reversalHalfCycle1k = 48.0;
    double slewLowPassStep = 1.0, slewLowPassNormalisation = 1.0;
    float floorHighPassStep = 0.0f, floorLowPassStep = 0.0f, floorBandStep = 0.0f, floorPowerStep = 0.0f;
};

} // namespace duskaudio
