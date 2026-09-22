// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// VCACompressor.hpp — no-JUCE port of UniversalCompressor::VCACompressor
// (multicomp.cpp L2166–L2528).
//
// The PUBLIC interface below is FINAL — the top-level UniversalCompressorDSP
// calls exactly these. Every constant, coefficient and per-sample op order is
// preserved from the source. See core/PORT_NOTES.md §Mode contracts.

#pragma once

#include "UniversalCompressorServices.hpp"

namespace duskaudio
{

class VCACompressor
{
public:
    // vca_detector_mode: choice 1 == "Classic" (fixed 10 ms RMS), 0 == "Adaptive".
    // Pushed from the top level before process() so the process() signature stays fixed.
    void setDetectorClassic (bool classic) noexcept;

    void prepare (double sampleRate, int numChannels);

    // process(): RT / per-sample.
    //   threshold     : vca_threshold (dB)
    //   ratio         : vca_ratio     (:1)
    //   attackParam   : vca_attack    (ms)
    //   releaseParam  : vca_release   (ms)
    //   outputGain    : vca_output    (dB, 0 == unity; caller forces 0 under auto-makeup)
    //   overEasy      : vca_overeasy toggle
    float process (float input, int channel, float threshold, float ratio,
                   float attackParam, float releaseParam, float outputGain,
                   bool overEasy = false, bool oversample = false,
                   float sidechainSignal = 0.0f, bool useExternalSidechain = false);

    float getGainReduction (int channel) const;

    void updateSampleRate (double newSampleRate);

    void reset();

private:
    struct Detector
    {
        float envelope = 1.0f;
        float rmsBuffer = 0.0f;
        float previousReduction = 0.0f;
        float signalEnvelope = 0.0f;
        float envelopeRate = 0.0f;
        float previousInput = 0.0f;
        float overshootAmount = 0.0f;
        float dcState = 0.0f;
        float prevSq = 0.0f;
    };

    static void resetDetector (Detector& detector) noexcept
    {
        detector.envelope = 1.0f;
        detector.rmsBuffer = 0.0f;
        detector.previousReduction = 0.0f;
        detector.signalEnvelope = 0.0f;
        detector.envelopeRate = 0.0f;
        detector.previousInput = 0.0f;
        detector.overshootAmount = 0.0f;
        detector.dcState = 0.0f;
        detector.prevSq = 0.0f;
    }

    std::vector<Detector> detectors;
    double sampleRate = 0.0;
    bool detectorClassic = false;
};

inline void VCACompressor::setDetectorClassic (bool classic) noexcept
{
    detectorClassic = classic;
}

inline void VCACompressor::prepare (double sampleRate, int numChannels)
{
    this->sampleRate = sampleRate;
    detectors.resize ((size_t) numChannels);
    for (auto& detector : detectors)
        resetDetector (detector);
}

inline void VCACompressor::reset()
{
    for (auto& detector : detectors)
        resetDetector (detector);
}

inline float VCACompressor::process (float input, int channel, float threshold, float ratio,
                                     float attackParam, float releaseParam, float outputGain,
                                     bool overEasy, bool /*oversample*/,
                                     float sidechainSignal, bool useExternalSidechain)
{
    if (channel >= static_cast<int>(detectors.size()))
        return input;

    if (sampleRate <= 0.0)
        return input;

    auto& detector = detectors[(size_t) channel];

    float detectionLevel;
    if (useExternalSidechain)
        detectionLevel = std::abs (sidechainSignal);
    else
        detectionLevel = std::abs (input);

    float signalDelta = std::abs (detectionLevel - detector.previousInput);
    detector.envelopeRate = detector.envelopeRate * 0.95f + signalDelta * 0.05f;
    detector.previousInput = detectionLevel;

    float rmsTimeMs;
    if (detectorClassic)
    {
        rmsTimeMs = 0.010f;
    }
    else
    {
        float levelDb = gainToDb (jmax (detectionLevel, 0.0001f));
        float levelAboveRef = jlimit (0.0f, 30.0f, levelDb + 20.0f);
        float levelFactor = levelAboveRef / 30.0f;
        rmsTimeMs = 0.005f + 0.030f * std::exp (-3.0f * levelFactor);
    }
    const float rmsAlpha = std::exp (-1.0f / (jmax (0.0001f, rmsTimeMs) * static_cast<float>(sampleRate)));
    detector.rmsBuffer = detector.rmsBuffer * rmsAlpha + detectionLevel * detectionLevel * (1.0f - rmsAlpha);
    float rmsLevel = std::sqrt (detector.rmsBuffer);

    const float envelopeAlpha = 0.99f;
    detector.signalEnvelope = detector.signalEnvelope * envelopeAlpha + rmsLevel * (1.0f - envelopeAlpha);

    float thresholdLin = dbToGain (threshold);

    float reduction = 0.0f;
    float overThreshDb = gainToDb (jmax (Constants::EPSILON, rmsLevel) / thresholdLin);

    if (overEasy)
    {
        float kneeWidth = 10.0f;
        float kneeStart = -kneeWidth * 0.5f;
        float kneeEnd = kneeWidth * 0.5f;

        if (overThreshDb <= kneeStart)
        {
            reduction = 0.0f;
        }
        else if (overThreshDb < kneeEnd)
        {
            float x = overThreshDb - kneeStart;
            reduction = (1.0f - 1.0f / ratio) * (x * x) / (2.0f * kneeWidth);
        }
        else
        {
            reduction = overThreshDb * (1.0f - 1.0f / ratio);
        }
    }
    else
    {
        if (rmsLevel > thresholdLin)
            reduction = overThreshDb * (1.0f - 1.0f / ratio);
    }

    reduction = jmin (jmax (0.0f, reduction), Constants::VCA_MAX_REDUCTION_DB);

    float attackTime, releaseTime;

    float userAttackScale = attackParam / 15.0f;

    float programAttackTime;
    if (reduction > 0.1f)
    {
        if (reduction <= 10.0f)
            programAttackTime = 0.015f;
        else if (reduction <= 20.0f)
            programAttackTime = 0.005f;
        else
            programAttackTime = 0.003f;
    }
    else
    {
        programAttackTime = 0.015f;
    }

    attackTime = programAttackTime * userAttackScale;
    attackTime = jlimit (0.0001f, 0.050f, attackTime);

    float userReleaseTime = releaseParam / 1000.0f;

    const float releaseRate = 120.0f;
    float programReleaseTime;
    if (reduction > 0.1f)
    {
        programReleaseTime = reduction / releaseRate;
        programReleaseTime = jmax (0.008f, programReleaseTime);
    }
    else
    {
        programReleaseTime = 0.008f;
    }

    float blendFactor = jlimit (0.0f, 1.0f, (userReleaseTime - 0.01f) / 0.5f);
    releaseTime = programReleaseTime * (1.0f - blendFactor) + userReleaseTime * blendFactor;

    float targetGain = dbToGain (-reduction);

    float attackCoeff = std::exp (-1.0f / (jmax (Constants::EPSILON, attackTime * static_cast<float>(sampleRate))));

    if (targetGain < detector.envelope)
    {
        detector.envelope = targetGain + (detector.envelope - targetGain) * attackCoeff;

        if (attackTime < 0.005f && reduction > 5.0f)
        {
            float overshootFactor = (0.005f - attackTime) / 0.004f;
            float reductionFactor = jlimit (0.0f, 1.0f, reduction / 20.0f);

            detector.overshootAmount = overshootFactor * reductionFactor * 0.02f;
        }
        else
        {
            detector.overshootAmount *= 0.95f;
        }
    }
    else
    {
        float currentDb = gainToDb (jmax (0.0001f, detector.envelope));
        float targetDb = gainToDb (jmax (0.0001f, targetGain));

        float effectiveRate;
        if (reduction > 0.1f)
            effectiveRate = reduction / jmax (0.001f, releaseTime);
        else
            effectiveRate = 120.0f;

        float releaseStepDb = effectiveRate / static_cast<float>(sampleRate);
        currentDb = jmin (currentDb + releaseStepDb, targetDb);
        detector.envelope = dbToGain (currentDb);

        detector.overshootAmount *= 0.98f;
    }

    detector.envelope = jlimit (0.0001f, 1.0f, detector.envelope);

    if (std::isnan (detector.envelope) || std::isinf (detector.envelope))
        detector.envelope = 1.0f;

    detector.previousReduction = reduction;

    float envelopeWithOvershoot = detector.envelope * (1.0f + detector.overshootAmount);
    envelopeWithOvershoot = jlimit (0.0001f, 1.0f, envelopeWithOvershoot);

    float compressed = input * envelopeWithOvershoot;

    float processed = compressed;
    float absLevel = std::abs (processed);

    float harmonicLevelDb = gainToDb (jmax (0.0001f, absLevel));

    if (absLevel > 0.01f)
    {
        float sign = (processed < 0.0f) ? -1.0f : 1.0f;

        float h2_level = 0.0f;
        float h3_level = 0.0f;

        float circuitH2 = 0.0003f;
        float circuitH3 = 0.0006f;

        h2_level = circuitH2;
        h3_level = circuitH3;

        if (harmonicLevelDb > -30.0f && reduction > 2.0f)
        {
            float compressionFactor = jmin (1.0f, reduction / 30.0f);

            float h2_comp = 0.001f * compressionFactor;
            h2_level += h2_comp;

            if (reduction > 10.0f)
            {
                float h3_comp = 0.0008f * compressionFactor;
                h3_level += h3_comp;
            }
        }

        processed = compressed;

        float sq = compressed * compressed;
        float hpAlpha = 1.0f / (1.0f + 6.2831853f * 10.0f / static_cast<float>(sampleRate));
        float h2_signal = hpAlpha * (detector.dcState + sq - detector.prevSq);
        detector.dcState = h2_signal;
        detector.prevSq = sq;
        processed += h2_signal * h2_level;

        float h3_signal = compressed * compressed * compressed;
        processed += h3_signal * h3_level;

        if (absLevel > 1.5f)
        {
            float excess = absLevel - 1.5f;
            float vcaSat = 1.5f + std::tanh (excess * 0.3f) * 0.2f;
            processed = sign * vcaSat * (processed / absLevel);
        }
    }

    float output = processed * dbToGain (outputGain);

    return jlimit (-Constants::OUTPUT_HARD_LIMIT, Constants::OUTPUT_HARD_LIMIT, output);
}

inline float VCACompressor::getGainReduction (int channel) const
{
    if (channel >= static_cast<int>(detectors.size()))
        return 0.0f;
    return gainToDb (detectors[(size_t) channel].envelope);
}

inline void VCACompressor::updateSampleRate (double newSampleRate)
{
    if (newSampleRate > 0.0 && newSampleRate != sampleRate)
        sampleRate = newSampleRate;
}

} // namespace duskaudio
