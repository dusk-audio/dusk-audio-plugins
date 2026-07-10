// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// FETCompressor.hpp — no-JUCE port of UniversalCompressor::FETCompressor
// (multicomp.cpp L1637–L2165).

#pragma once

#include "UniversalCompressorServices.hpp"   // LookupTables, TransientShaper, Constants

#include "../HardwareEmulation/TransformerEmulation.h"   // + HardwareProfiles, WaveshaperCurves
#include "../HardwareEmulation/ConvolutionEngine.h"      // StereoConvolution / ShortConvolution

#include <array>
#include <cmath>
#include <vector>

namespace duskaudio
{

class FETCompressor
{
public:
    void prepare (double sampleRate, int numChannels);
    void updateSampleRate (double newSampleRate);

    // process(): RT / per-sample. Multi-line signature preserved verbatim from
    // the source; defaults match the JUCE original.
    //   inputGainDb  : fet_input   (dB)
    //   outputGainDb : fet_output  (dB, 0 == unity; caller forces 0 under auto-makeup)
    //   attackMs     : fet_attack  (ms)
    //   releaseMs    : fet_release (ms)
    //   ratioIndex   : fet_ratio   (0=4:1,1=8:1,2=12:1,3=20:1,4=All)
    //   lookupTables/transientShaper : owned by the top level, passed in
    //   useMeasuredCurve : fet_curve_mode (0=Modern,1=Measured)
    //   transientSensitivity : fet_transient (0..100; 0 on the strip path)
    //   thresholdDb  : fet_threshold (dBFS), defaults to the fixed -10 dB
    float process (float input, int channel, float inputGainDb, float outputGainDb,
                   float attackMs, float releaseMs, int ratioIndex, bool oversample = false,
                   const LookupTables* lookupTables = nullptr, TransientShaper* transientShaper = nullptr,
                   bool useMeasuredCurve = false, float transientSensitivity = 0.0f,
                   float sidechainSignal = 0.0f, bool useExternalSidechain = false,
                   float thresholdDb = Constants::FET_THRESHOLD_DB);

    float getGainReduction (int channel) const;

    void reset();

private:
    struct Detector
    {
        float envelope = 1.0f;
        float prevOutput = 0.0f;
        float previousLevel = 0.0f;
        float modulationPhase = 0.0f;
        float modulationRate = 4.5f;
        float dcState = 0.0f;
        float prevSq = 0.0f;
        float peakHold = 0.0f;
        int transientCounter = 0;
        float releaseMemory = 0.0f;
        float voltageSag = 0.0f;
        int sagCounter = 0;
        float hfChokeState = 0.0f;
        float tiltState = 0.0f;
        float subBassHpState = 0.0f;
    };

    // Calibrate the analog chain (input xfmr + output xfmr + convolution) so it
    // is level-neutral at unity. Main-thread only (sweeps a warm-up sine).
    void calibrateHardwareGain()
    {
        constexpr int calibrationSamples = 4800;
        constexpr float refAmplitude = 0.126f;  // -18dB peak
        constexpr float refFreq = 1000.0f;

        float sr = static_cast<float>(sampleRate);
        float angularStep = 2.0f * kPiF * refFreq / sr;

        inputTransformer.reset();
        outputTransformer.reset();

        int warmup = static_cast<int>(sr * 0.05f);
        for (int i = 0; i < warmup; ++i)
        {
            float x = refAmplitude * std::sin(angularStep * static_cast<float>(i));
            x = inputTransformer.processSample(x, 0);
            x = outputTransformer.processSample(x, 0);
            convolution.processSample(x, 0);
        }

        double inputRmsSquared = 0.0;
        double outputRmsSquared = 0.0;
        for (int i = 0; i < calibrationSamples; ++i)
        {
            float phase = angularStep * static_cast<float>(warmup + i);
            float input = refAmplitude * std::sin(phase);

            float x = inputTransformer.processSample(input, 0);
            x = outputTransformer.processSample(x, 0);
            x = convolution.processSample(x, 0);

            inputRmsSquared += static_cast<double>(input * input);
            outputRmsSquared += static_cast<double>(x * x);
        }

        inputRmsSquared /= calibrationSamples;
        outputRmsSquared /= calibrationSamples;

        if (outputRmsSquared > 1e-12 && inputRmsSquared > 1e-12)
        {
            float chainGain = static_cast<float>(std::sqrt(outputRmsSquared / inputRmsSquared));
            hardwareGainCompensation = 1.0f / chainGain;
        }
        else
        {
            hardwareGainCompensation = 1.0f;
        }

        inputTransformer.reset();
        outputTransformer.reset();
        convolution.reset();
    }

    std::vector<Detector> detectors;
    double sampleRate = 0.0;
    float hardwareGainCompensation = 1.0f;
    float tiltCoeff = 0.0f;

    HardwareEmulation::TransformerEmulation inputTransformer;
    HardwareEmulation::TransformerEmulation outputTransformer;
    HardwareEmulation::StereoConvolution convolution;
};

inline void FETCompressor::prepare (double sampleRate, int numChannels)
{
    this->sampleRate = sampleRate;
    detectors.resize(numChannels);
    for (auto& detector : detectors)
    {
        detector.envelope = 1.0f;
        detector.prevOutput = 0.0f;
        detector.previousLevel = 0.0f;
        detector.modulationPhase = 0.0f;
        detector.dcState = 0.0f;
        detector.prevSq = 0.0f;
        detector.peakHold = 0.0f;
        detector.transientCounter = 0;
        detector.releaseMemory = 0.0f;
        detector.voltageSag = 0.0f;
        detector.sagCounter = 0;
        detector.hfChokeState = 0.0f;
        detector.tiltState = 0.0f;
        detector.subBassHpState = 0.0f;
    }

    // 1176 sidechain tilt: ~3dB/octave via 1st-order HPF at 800Hz blended with original
    {
        float sr = static_cast<float>(sampleRate);
        tiltCoeff = 1.0f - std::exp(-2.0f * 3.14159f * 800.0f / sr);
    }

    inputTransformer.prepare(sampleRate, numChannels);
    inputTransformer.setProfile(HardwareEmulation::HardwareProfiles::getFETCompressor().inputTransformer);
    inputTransformer.setEnabled(true);

    outputTransformer.prepare(sampleRate, numChannels);
    outputTransformer.setProfile(HardwareEmulation::HardwareProfiles::getFETCompressor().outputTransformer);
    outputTransformer.setEnabled(true);

    convolution.prepare(sampleRate);
    convolution.loadTransformerIR(HardwareEmulation::ShortConvolution::TransformerType::FET);

    calibrateHardwareGain();
}

inline void FETCompressor::updateSampleRate (double newSampleRate)
{
    if (newSampleRate <= 0.0 || newSampleRate == sampleRate)
        return;
    sampleRate = newSampleRate;
    float sr = static_cast<float>(sampleRate);
    tiltCoeff = 1.0f - std::exp(-2.0f * 3.14159f * 800.0f / sr);
    int numCh = static_cast<int>(detectors.size());
    inputTransformer.prepare(sampleRate, numCh);
    inputTransformer.setProfile(HardwareEmulation::HardwareProfiles::getFETCompressor().inputTransformer);
    outputTransformer.prepare(sampleRate, numCh);
    outputTransformer.setProfile(HardwareEmulation::HardwareProfiles::getFETCompressor().outputTransformer);
    convolution.prepare(sampleRate);
    convolution.loadTransformerIR(HardwareEmulation::ShortConvolution::TransformerType::FET);
    calibrateHardwareGain();
}

inline float FETCompressor::process (float input, int channel, float inputGainDb, float outputGainDb,
                                     float attackMs, float releaseMs, int ratioIndex, bool /*oversample*/,
                                     const LookupTables* lookupTables, TransientShaper* transientShaper,
                                     bool useMeasuredCurve, float transientSensitivity,
                                     float sidechainSignal, bool useExternalSidechain,
                                     float thresholdDb)
{
    if (channel >= static_cast<int>(detectors.size()))
        return input;

    if (sampleRate <= 0.0)
        return input;

    auto& detector = detectors[channel];

    float transformedInput = inputTransformer.processSample(input, channel);

    float threshold = dbToGain(thresholdDb);

    float inputGainLin = dbToGain(inputGainDb);
    float amplifiedInput = transformedInput * inputGainLin;

    // Measured Rev D 1176 ratios (not the nominal panel markings). All-buttons
    // follows its own curve below; the table entry there only clamps.
    std::array<float, 5> ratios = {3.85f, 7.40f, 12.50f, 21.50f, 21.50f};
    float ratio = ratios[jlimit(0, 4, ratioIndex)];

    // Feedback topology: apply the PREVIOUS envelope to get the compressed signal.
    float compressed = amplifiedInput * detector.envelope;

    // FET saturation (asymmetric) before feedback detection so its nonlinearity
    // colors what the sidechain sees.
    float saturated = compressed;
    float sr = static_cast<float>(sampleRate);
    {
        float grDb = -gainToDb(detector.envelope + 0.001f);
        float grNorm = jlimit(0.0f, 1.0f, grDb / 20.0f);

        float k2, k3;
        if (ratioIndex == 4)
        {
            k2 = 0.04f + grNorm * 0.04f;
            k3 = 0.005f + grNorm * 0.010f;
        }
        else
        {
            k2 = 0.024f + grNorm * 0.026f;
            k3 = 0.004f + grNorm * 0.008f;
        }

        float x = saturated;

        float sq = x * x;

        float alpha = 1.0f / (1.0f + 6.2831853f * 10.0f / sr);
        float h2 = alpha * (detector.dcState + sq - detector.prevSq);
        detector.dcState = h2;
        detector.prevSq = sq;

        float h3 = x * x * x;
        saturated = x + k2 * h2 + k3 * h3;
    }

    // HF choke for sidechain detection (FET junction capacitance): detection-only LPF.
    float chokedSignal = saturated;
    {
        float grDb = -gainToDb(detector.envelope + 0.001f);
        float grNorm = jlimit(0.0f, 1.0f, grDb / 20.0f);
        float cornerHz = 20000.0f - grNorm * 2000.0f;
        float w = 6.2831853f * cornerHz / sr;
        float lpCoeff = 1.0f - std::exp(-w);
        detector.hfChokeState += lpCoeff * (saturated - detector.hfChokeState);
        chokedSignal = detector.hfChokeState;
    }

    float detectionLevel;
    if (useExternalSidechain)
    {
        detectionLevel = std::abs(sidechainSignal * inputGainLin);
    }
    else if (ratioIndex == 4)
    {
        float instantLevel = std::abs(chokedSignal);

        float detCeiling = threshold * 1.5f;
        if (instantLevel > detCeiling)
            instantLevel = detCeiling + (instantLevel - detCeiling) / (1.0f + (instantLevel - detCeiling));

        float peakAttackCoeff = std::exp(-1.0f / (0.00005f * sr));
        float peakReleaseCoeff = std::exp(-1.0f / (0.005f * sr));
        if (instantLevel > detector.peakHold)
            detector.peakHold += (1.0f - peakAttackCoeff) * (instantLevel - detector.peakHold);
        else
            detector.peakHold += (1.0f - peakReleaseCoeff) * (instantLevel - detector.peakHold);
        detectionLevel = detector.peakHold;
    }
    else
    {
        detectionLevel = std::abs(chokedSignal);
    }

    // 1176 sidechain tilt: 3dB/octave HF emphasis.
    detector.tiltState += tiltCoeff * (detectionLevel - detector.tiltState);
    float hfContent = detectionLevel - detector.tiltState;
    detectionLevel = std::max(detectionLevel + hfContent * 0.35f, 0.0f);

    float reduction = 0.0f;

    if (ratioIndex == 4)
    {
        // ABI threshold shift: combined resistor networks lower effective threshold by ~6dB.
        float abiThreshold = threshold * 0.5f;

        if (detectionLevel > abiThreshold)
        {
            float overThreshDb = gainToDb(detectionLevel / abiThreshold);

            if (lookupTables != nullptr)
            {
                reduction = lookupTables->getAllButtonsReduction(overThreshDb, useMeasuredCurve);
            }
            else
            {
                float knee = 4.0f;
                if (overThreshDb < knee)
                {
                    float t = overThreshDb / knee;
                    reduction = overThreshDb * t * 0.95f;
                }
                else
                {
                    float baseReduction = knee * 0.95f + (overThreshDb - knee) * 0.98f;

                    float wrapOnset = 14.0f;
                    float wrapFull = 16.0f;
                    if (overThreshDb > wrapOnset)
                    {
                        float wrapT = jlimit(0.0f, 1.0f, (overThreshDb - wrapOnset) / (wrapFull - wrapOnset));
                        float smooth = wrapT * wrapT * (3.0f - 2.0f * wrapT);
                        float excess = overThreshDb - wrapOnset;
                        baseReduction += excess * 0.05f * smooth;
                    }
                    reduction = baseReduction;
                }
            }

            if (transientShaper != nullptr && transientSensitivity > 0.01f)
            {
                float transientMod = transientShaper->process(input, channel, transientSensitivity);
                reduction /= transientMod;
            }

            reduction = jmin(reduction, 30.0f);
        }
    }
    else if (detectionLevel > threshold)
    {
        float overThreshDb = gainToDb(detectionLevel / threshold);
        reduction = overThreshDb * (1.0f - 1.0f / ratio);
        reduction = jmin(reduction, Constants::FET_MAX_REDUCTION_DB);
    }

    const float minRelease = 0.05f;
    const float maxRelease = 1.1f;
    float attackTime = jmax(0.0001f, attackMs / 1000.0f);
    float releaseNorm = jlimit(0.0f, 1.0f, releaseMs / 1100.0f);
    float releaseTime = minRelease * std::pow(maxRelease / minRelease, releaseNorm);

    if (ratioIndex == 4)
    {
        attackTime = jmax(0.0002f, attackTime * 2.0f);

        float reductionFactor = jlimit(0.0f, 1.0f, reduction / 20.0f);
        releaseTime *= (1.0f + reductionFactor * 0.5f);

        // Release memory: transient onsets lengthen the slow tail (sidechain cap
        // not fully discharging between hits).
        float memoryDecay = std::exp(-1.0f / (0.5f * sr));
        detector.releaseMemory *= memoryDecay;
        if (detector.transientCounter == 0 && reduction > 3.0f)
            detector.releaseMemory = jmin(1.0f, detector.releaseMemory + 0.15f);
        releaseTime *= (1.0f + detector.releaseMemory * 0.3f);
    }

    float programFactor = jlimit(0.5f, 2.0f, 1.0f + reduction * 0.05f);
    float signalDelta = std::abs(detectionLevel - detector.previousLevel);
    detector.previousLevel = detectionLevel;

    if (signalDelta > 0.1f)
    {
        attackTime *= 0.8f;
        releaseTime *= 1.2f;
    }
    else
    {
        attackTime *= programFactor;
        releaseTime *= programFactor;
    }

    float targetGain = dbToGain(-reduction);
    float attackCoeff = std::exp(-1.0f / jmax(Constants::EPSILON, attackTime * sr));
    float releaseCoeff = std::exp(-1.0f / jmax(Constants::EPSILON, releaseTime * sr));

    if (ratioIndex == 4)
    {
        if (targetGain < detector.envelope)
        {
            // ABI program-dependent attack delay: first ~30 samples use a slower
            // attack to let the transient poke through.
            if (detector.transientCounter < 30)
            {
                float delayedAttack = attackCoeff * 0.5f + 0.5f;
                detector.envelope = delayedAttack * detector.envelope + (1.0f - delayedAttack) * targetGain;
                detector.transientCounter++;
            }
            else
            {
                detector.envelope = attackCoeff * detector.envelope + (1.0f - attackCoeff) * targetGain;
            }
        }
        else
        {
            detector.transientCounter = 0;

            float grDb = -gainToDb(detector.envelope + 0.001f);

            float fastTimeBase = 0.05f;
            float fastTimeMin = 0.025f;
            float grScale = jlimit(0.0f, 1.0f, grDb / 20.0f);
            float fastTime = fastTimeBase - grScale * (fastTimeBase - fastTimeMin);
            float fastCoeff = std::exp(-1.0f / jmax(Constants::EPSILON, fastTime * sr));

            float blend = grScale;
            float effectiveCoeff = fastCoeff * blend + releaseCoeff * (1.0f - blend);
            detector.envelope = effectiveCoeff * detector.envelope + (1.0f - effectiveCoeff) * targetGain;
        }
    }
    else
    {
        if (targetGain < detector.envelope)
            detector.envelope = attackCoeff * detector.envelope + (1.0f - attackCoeff) * targetGain;
        else
            detector.envelope = releaseCoeff * detector.envelope + (1.0f - releaseCoeff) * targetGain;
    }

    detector.envelope = jlimit(0.001f, 1.0f, detector.envelope);

    if (std::isnan(detector.envelope) || std::isinf(detector.envelope))
        detector.envelope = 1.0f;

    // Voltage sag: PSU sag under sustained heavy load (GR > 15dB for >100ms drops
    // the ceiling ~0.75dB, recovering slowly).
    float sagGain = 1.0f;
    if (ratioIndex == 4)
    {
        float grDb = -gainToDb(detector.envelope + 0.001f);
        int sagThresholdSamples = static_cast<int>(sr * 0.1f);

        if (grDb > 15.0f)
        {
            detector.sagCounter = jmin(detector.sagCounter + 1, sagThresholdSamples + 1);
        }
        else
        {
            detector.sagCounter = jmax(0, detector.sagCounter - 1);
        }

        float sagTarget = (detector.sagCounter > sagThresholdSamples) ? 0.75f : 0.0f;

        float sagAttack = std::exp(-1.0f / (0.05f * sr));
        float sagRelease = std::exp(-1.0f / (0.3f * sr));
        if (sagTarget > detector.voltageSag)
            detector.voltageSag += (1.0f - sagAttack) * (sagTarget - detector.voltageSag);
        else
            detector.voltageSag += (1.0f - sagRelease) * (sagTarget - detector.voltageSag);

        sagGain = dbToGain(-detector.voltageSag);
    }

    float output = saturated;
    output = outputTransformer.processSample(output, channel);
    output = convolution.processSample(output, channel);
    output *= hardwareGainCompensation;

    output *= sagGain;

    // Sub-bass tightening: GR-driven 1-pole HPF (cutoff 20Hz off → 80Hz at 20dB GR);
    // LPF state tracks the low end, subtract to get the HPF output.
    {
        float grDbHpf = -gainToDb(detector.envelope + 0.001f);
        float grScaleHpf = jlimit(0.0f, 1.0f, grDbHpf / 20.0f);
        float hpfCutoffHz = 20.0f + grScaleHpf * 60.0f;
        float alpha = 1.0f - std::exp(-2.0f * kPiF * hpfCutoffHz / sr);
        detector.subBassHpState += alpha * (output - detector.subBassHpState);
        output -= detector.subBassHpState;
    }

    float outputGainLin = dbToGain(outputGainDb);
    float finalOutput = output * outputGainLin;

    return jlimit(-Constants::OUTPUT_HARD_LIMIT, Constants::OUTPUT_HARD_LIMIT, finalOutput);
}

inline float FETCompressor::getGainReduction (int channel) const
{
    if (channel >= static_cast<int>(detectors.size()))
        return 0.0f;
    return gainToDb(detectors[channel].envelope);
}

inline void FETCompressor::reset()
{
    for (auto& detector : detectors)
        detector = Detector{};
    inputTransformer.reset();
    outputTransformer.reset();
    convolution.reset();
}

} // namespace duskaudio
