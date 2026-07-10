// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// OptoCompressor.hpp — no-JUCE port of UniversalCompressor::OptoCompressor
// (multicomp.cpp L1180–L1636).
//
// The PUBLIC interface below is FINAL — the top-level UniversalCompressorDSP
// calls exactly these. Every constant, coefficient and per-sample op order is
// preserved from the source. See core/PORT_NOTES.md §Mode contracts.

#pragma once

#include "UniversalCompressorServices.hpp"
#include "../HardwareEmulation/TransformerEmulation.h"
#include "../HardwareEmulation/TubeEmulation.h"

#include <cmath>
#include <vector>

namespace duskaudio
{

class OptoCompressor
{
public:
    void prepare (double sr, int numChannels)
    {
        this->sampleRate = sr;
        detectors.resize ((size_t) numChannels);
        for (auto& det : detectors)
        {
            det.elPanelLevel = 0.0f;
            det.cellCharge = 0.0f;
            det.phosphorGlow = 0.0f;
            det.phosphorAfterglow = 0.0f;
            det.accumulatedCharge = 0.0f;
            det.smoothedConductance = 0.0f;
            det.t4bGain = 1.0f;
            det.shelfX1 = 0.0f;
            det.shelfY1 = 0.0f;
            det.t4bDcState = 0.0f;
        }

        float srf = static_cast<float> (sampleRate);
        invSampleRate = 1.0f / srf;
        attackCoeff = std::exp (-1.0f / (Constants::T4B_ATTACK_TIME * srf));
        fastReleaseCoeff = std::exp (-1.0f / (Constants::T4B_FAST_RELEASE_TIME * srf));
        phosphorDecayCoeff = std::exp (-1.0f / (Constants::T4B_PHOSPHOR_BASE_DECAY * srf));
        {
            float wc = 2.0f * 3.14159f * 1000.0f / srf;
            float A = std::pow (10.0f, 3.0f / 20.0f);
            float alpha = std::tan (wc / 2.0f);
            float sqrtA = std::sqrt (A);
            float norm = 1.0f / (1.0f + alpha * sqrtA);
            shelfB0 = (A + sqrtA * alpha) * norm;
            shelfB1 = (sqrtA * alpha - A) * norm;
            shelfA1 = (alpha * sqrtA - 1.0f) * norm;
        }
        phosphorAttackCoeff = std::pow (phosphorDecayCoeff, Constants::T4B_PHOSPHOR_ATTACK_RATIO);
        condAttackCoeff = 1.0f - std::exp (-2.0f * kPiF * Constants::T4B_CONDUCTANCE_ATTACK_FREQ / srf);
        condReleaseCoeff = 1.0f - std::exp (-2.0f * kPiF * Constants::T4B_CONDUCTANCE_RELEASE_FREQ / srf);
        elPanelAttackCoeff = 1.0f - std::exp (-2.0f * kPiF * Constants::T4B_EL_PANEL_ATTACK_FREQ / srf);
        elPanelReleaseCoeff = 1.0f - std::exp (-2.0f * kPiF * Constants::T4B_EL_PANEL_RELEASE_FREQ / srf);
        scLevelSmoothCoeff = 1.0f - std::exp (-2.0f * kPiF * Constants::SC_LEVEL_SMOOTH_FREQ / srf);

        inputTransformer.prepare (sampleRate, numChannels);
        inputTransformer.setProfile (HardwareEmulation::HardwareProfiles::getOptoCompressor().inputTransformer);
        inputTransformer.setEnabled (true);

        outputTransformer.prepare (sampleRate, numChannels);
        outputTransformer.setProfile (HardwareEmulation::HardwareProfiles::getOptoCompressor().outputTransformer);
        outputTransformer.setEnabled (true);

        tubeStage.prepare (sampleRate, numChannels);
        tubeStage.setTubeType (HardwareEmulation::TubeEmulation::TubeType::Triode_12BH7);
        tubeStage.setDrive (0.15f);

        calibrateHardwareGain();
    }

    void updateSampleRate (double newSampleRate)
    {
        if (newSampleRate <= 0.0 || newSampleRate == sampleRate)
            return;
        sampleRate = newSampleRate;
        int numCh = static_cast<int> (detectors.size());

        float srf = static_cast<float> (sampleRate);
        invSampleRate = 1.0f / srf;
        attackCoeff = std::exp (-1.0f / (Constants::T4B_ATTACK_TIME * srf));
        fastReleaseCoeff = std::exp (-1.0f / (Constants::T4B_FAST_RELEASE_TIME * srf));
        phosphorDecayCoeff = std::exp (-1.0f / (Constants::T4B_PHOSPHOR_BASE_DECAY * srf));
        {
            float wc = 2.0f * 3.14159f * 1000.0f / srf;
            float A = std::pow (10.0f, 3.0f / 20.0f);
            float alpha = std::tan (wc / 2.0f);
            float sqrtA = std::sqrt (A);
            float norm = 1.0f / (1.0f + alpha * sqrtA);
            shelfB0 = (A + sqrtA * alpha) * norm;
            shelfB1 = (sqrtA * alpha - A) * norm;
            shelfA1 = (alpha * sqrtA - 1.0f) * norm;
        }
        phosphorAttackCoeff = std::pow (phosphorDecayCoeff, Constants::T4B_PHOSPHOR_ATTACK_RATIO);
        condAttackCoeff = 1.0f - std::exp (-2.0f * kPiF * Constants::T4B_CONDUCTANCE_ATTACK_FREQ / srf);
        condReleaseCoeff = 1.0f - std::exp (-2.0f * kPiF * Constants::T4B_CONDUCTANCE_RELEASE_FREQ / srf);
        elPanelAttackCoeff = 1.0f - std::exp (-2.0f * kPiF * Constants::T4B_EL_PANEL_ATTACK_FREQ / srf);
        elPanelReleaseCoeff = 1.0f - std::exp (-2.0f * kPiF * Constants::T4B_EL_PANEL_RELEASE_FREQ / srf);
        scLevelSmoothCoeff = 1.0f - std::exp (-2.0f * kPiF * Constants::SC_LEVEL_SMOOTH_FREQ / srf);

        inputTransformer.prepare (sampleRate, numCh);
        inputTransformer.setProfile (HardwareEmulation::HardwareProfiles::getOptoCompressor().inputTransformer);
        outputTransformer.prepare (sampleRate, numCh);
        outputTransformer.setProfile (HardwareEmulation::HardwareProfiles::getOptoCompressor().outputTransformer);
        tubeStage.prepare (sampleRate, numCh);
        tubeStage.setTubeType (HardwareEmulation::TubeEmulation::TubeType::Triode_12BH7);
        tubeStage.setDrive (0.15f);
        calibrateHardwareGain();
    }

    float process (float input, int channel, float peakReduction, float gain, bool limitMode,
                   bool oversample = false, float sidechainSignal = 0.0f, bool useExternalSidechain = false)
    {
        (void) oversample;
        if (channel >= static_cast<int> (detectors.size()))
            return input;
        if (sampleRate <= 0.0)
            return input;

        peakReduction = jlimit (0.0f, 100.0f, peakReduction);
        gain = jlimit (-40.0f, 40.0f, gain);

        auto& det = detectors[(size_t) channel];

        // Stage 1: Input transformer (UTC A-10)
        float x = inputTransformer.processSample (input, channel);

        // Stage 2: T4B gain cell — apply gain from previous sample's T4B state
        float compressed = x * det.t4bGain;

        // T4B even-harmonic distortion: proportional to gain reduction depth
        float grAmount = 1.0f - det.t4bGain;
        if (grAmount > 0.01f)
        {
            float sq = compressed * compressed;
            det.t4bDcState = det.t4bDcState * 0.9999f + sq * 0.0001f;
            float h2 = sq - det.t4bDcState;

            float k2 = grAmount * 0.12f;
            compressed = compressed + k2 * h2;
        }

        // Stage 3+4: Sidechain signal selection + frequency shaping
        float scSignal;
        if (useExternalSidechain)
        {
            scSignal = sidechainSignal;
            det.shelfX1 = 0.0f;
            det.shelfY1 = 0.0f;
        }
        else if (!limitMode)
        {
            scSignal = compressed;
            float shelfOut = shelfB0 * scSignal + shelfB1 * det.shelfX1 - shelfA1 * det.shelfY1;
            det.shelfX1 = scSignal;
            det.shelfY1 = shelfOut;
            scSignal = shelfOut;
        }
        else
        {
            scSignal = input * 0.5f + compressed * 0.5f;
            det.shelfX1 = 0.0f;
            det.shelfY1 = 0.0f;
        }

        // Stage 5: Peak Reduction = sidechain amplifier gain
        float prNorm = peakReduction * 0.01f;
        float peakReductionGain = prNorm * prNorm * prNorm * Constants::PEAK_REDUCTION_MAX_SC_GAIN;

        // Stage 5b: 6AQ5 sidechain driver tube — soft-clips before the EL panel
        float scDrive = std::abs (scSignal * peakReductionGain);

        float effectiveDrive = std::max (0.0f, scDrive - Constants::SC_DRIVER_THRESHOLD);

        float scLevel = std::tanh (effectiveDrive * Constants::SC_DRIVER_SATURATION)
            * Constants::SC_DRIVER_OUTPUT_SCALE;

        // Stage 6: Sidechain envelope smoothing + T4B cell update
        det.scLevelSmoothed += scLevelSmoothCoeff * (scLevel - det.scLevelSmoothed);
        updateT4BCell (det, det.scLevelSmoothed);

        // Stage 7: Output stage
        float makeupGain = dbToGain (gain);

        float grCompensation = 1.0f / jmax (0.1f, det.t4bGain);
        float tubeBoost = 1.0f + (grCompensation - 1.0f) * 0.7f;

        float output = compressed * makeupGain * tubeBoost;

        float dynamicDrive = 0.15f + grAmount * 0.3f;
        tubeStage.setDrive (dynamicDrive);

        output = tubeStage.processSample (output, channel);

        output /= tubeBoost;

        output = outputTransformer.processSample (output, channel);

        output *= hardwareGainCompensation;

        return jlimit (-Constants::OUTPUT_HARD_LIMIT, Constants::OUTPUT_HARD_LIMIT, output);
    }

    float getGainReduction (int channel) const
    {
        if (channel >= static_cast<int> (detectors.size()))
            return 0.0f;
        return gainToDb (detectors[(size_t) channel].t4bGain);
    }

    void reset()
    {
        for (auto& det : detectors)
            det = Detector{};
        inputTransformer.reset();
        outputTransformer.reset();
        tubeStage.reset();
    }

private:
    struct Detector
    {
        float elPanelLevel = 0.0f;
        float cellCharge = 0.0f;
        float phosphorGlow = 0.0f;
        float phosphorAfterglow = 0.0f;
        float accumulatedCharge = 0.0f;
        float smoothedConductance = 0.0f;
        float t4bGain = 1.0f;
        float shelfX1 = 0.0f;
        float shelfY1 = 0.0f;
        float scLevelSmoothed = 0.0f;
        float t4bDcState = 0.0f;
    };

    void updateT4BCell (Detector& det, float scLevel)
    {
        // EL panel thermal response: asymmetric — heats fast, cools slowly
        float elCoeff = (scLevel > det.elPanelLevel) ? elPanelAttackCoeff : elPanelReleaseCoeff;
        det.elPanelLevel += elCoeff * (scLevel - det.elPanelLevel);

        // CdS fast component: charge toward EL panel light level
        float lightLevel = det.elPanelLevel;
        if (lightLevel > det.cellCharge)
        {
            det.cellCharge = lightLevel + (det.cellCharge - lightLevel) * attackCoeff;
        }
        else
        {
            float progDepFactor = 1.0f + det.accumulatedCharge * Constants::T4B_PROG_DEP_RELEASE_SCALE;
            float adjReleaseCoeff = std::pow (fastReleaseCoeff, 1.0f / progDepFactor);
            det.cellCharge = lightLevel + (det.cellCharge - lightLevel) * adjReleaseCoeff;
        }

        // EL phosphor persistence: slow component creating two-stage release
        if (lightLevel > det.phosphorGlow)
        {
            det.phosphorGlow = lightLevel + (det.phosphorGlow - lightLevel) * phosphorAttackCoeff;
        }
        else
        {
            float slowDecayTime = Constants::T4B_PHOSPHOR_BASE_DECAY
                + det.accumulatedCharge * Constants::T4B_PROG_DEP_PHOSPHOR_SCALE;
            float phosphorReleaseCoeff = std::exp (-invSampleRate / slowDecayTime);
            det.phosphorGlow = lightLevel + (det.phosphorGlow - lightLevel) * phosphorReleaseCoeff;
        }

        // Secondary phosphor afterglow (dual-decay T4B)
        if (lightLevel > det.phosphorAfterglow)
        {
            float afterglowAttackCoeff = std::pow (
                std::exp (-1.0f / (Constants::T4B_PHOSPHOR_AFTERGLOW_DECAY * static_cast<float> (sampleRate))),
                Constants::T4B_PHOSPHOR_AFTERGLOW_ATTACK_RATIO);
            det.phosphorAfterglow = lightLevel
                + (det.phosphorAfterglow - lightLevel) * afterglowAttackCoeff;
        }
        else
        {
            float afterglowDecayTime = Constants::T4B_PHOSPHOR_AFTERGLOW_DECAY
                + det.accumulatedCharge * Constants::T4B_PROG_DEP_PHOSPHOR_SCALE;
            float afterglowReleaseCoeff = std::exp (-invSampleRate / afterglowDecayTime);
            det.phosphorAfterglow = lightLevel
                + (det.phosphorAfterglow - lightLevel) * afterglowReleaseCoeff;
        }

        // Program-dependent charge accumulation
        det.accumulatedCharge += det.cellCharge * Constants::T4B_PROG_DEP_CHARGE_RATE * invSampleRate
            - det.accumulatedCharge * Constants::T4B_PROG_DEP_DISCHARGE_RATE * invSampleRate;
        det.accumulatedCharge = jlimit (0.0f, 1.0f, det.accumulatedCharge);

        // CdS resistance-to-gain mapping
        float cellResponse = det.cellCharge
                            + det.phosphorGlow * Constants::T4B_PHOSPHOR_COUPLING
                            + det.phosphorAfterglow * Constants::T4B_PHOSPHOR_AFTERGLOW_COUPLING;
        cellResponse = jlimit (0.0f, 1.0f, cellResponse);
        float conductance = (cellResponse > 0.0f)
            ? std::min (Constants::T4B_CONDUCTANCE_K * std::pow (cellResponse, Constants::T4B_GAMMA),
                        Constants::T4B_MAX_CONDUCTANCE)
            : 0.0f;

        // Asymmetric conductance smoothing: fast attack, slow release
        float condCoeff = (conductance > det.smoothedConductance) ? condAttackCoeff : condReleaseCoeff;
        det.smoothedConductance += condCoeff * (conductance - det.smoothedConductance);
        det.smoothedConductance = jlimit (0.0f, Constants::T4B_MAX_CONDUCTANCE, det.smoothedConductance);

        // Voltage divider: gain = 1 / (1 + conductance)
        float newGain = 1.0f / (1.0f + det.smoothedConductance);
        newGain = jlimit (0.01f, 1.0f, newGain);

        // Release slew limiter — CdS cell cannot recover faster than ~91ms; attack unrestricted.
        float gainDelta = newGain - det.t4bGain;
        if (gainDelta > 0.0f)
        {
            float maxReleaseDelta = Constants::T4B_MAX_GAIN_RELEASE_RATE * invSampleRate;
            gainDelta = std::min (gainDelta, maxReleaseDelta);
        }
        det.t4bGain += gainDelta;

        if (std::isnan (det.t4bGain) || std::isinf (det.t4bGain))
            det.t4bGain = 1.0f;
    }

    void calibrateHardwareGain()
    {
        // Reference 1kHz sine at -18dB through the hardware chain; runs on the
        // main thread from prepare(), never on the audio thread.
        constexpr int calibrationSamples = 4800;
        constexpr float refAmplitude = 0.126f;
        constexpr float refFreq = 1000.0f;

        float srf = static_cast<float> (sampleRate);
        float angularStep = 2.0f * kPiF * refFreq / srf;

        inputTransformer.reset();
        tubeStage.reset();
        outputTransformer.reset();

        int warmup = static_cast<int> (srf * 0.05f);
        for (int i = 0; i < warmup; ++i)
        {
            float xw = refAmplitude * std::sin (angularStep * static_cast<float> (i));
            xw = inputTransformer.processSample (xw, 0);
            xw = tubeStage.processSample (xw, 0);
            outputTransformer.processSample (xw, 0);
        }

        double inputRmsSquared = 0.0;
        double outputRmsSquared = 0.0;
        for (int i = 0; i < calibrationSamples; ++i)
        {
            float phase = angularStep * static_cast<float> (warmup + i);
            float in = refAmplitude * std::sin (phase);

            float xr = inputTransformer.processSample (in, 0);
            xr = tubeStage.processSample (xr, 0);
            xr = outputTransformer.processSample (xr, 0);

            inputRmsSquared += static_cast<double> (in * in);
            outputRmsSquared += static_cast<double> (xr * xr);
        }

        inputRmsSquared /= calibrationSamples;
        outputRmsSquared /= calibrationSamples;

        if (outputRmsSquared > 1e-12 && inputRmsSquared > 1e-12)
        {
            float chainGain = static_cast<float> (std::sqrt (outputRmsSquared / inputRmsSquared));
            hardwareGainCompensation = 1.0f / chainGain;
        }
        else
        {
            hardwareGainCompensation = 1.0f;
        }

        inputTransformer.reset();
        tubeStage.reset();
        outputTransformer.reset();
    }

    std::vector<Detector> detectors;
    double sampleRate = 0.0;

    float invSampleRate = 0.0f;
    float attackCoeff = 0.0f;
    float fastReleaseCoeff = 0.0f;
    float phosphorDecayCoeff = 0.0f;
    float shelfB0 = 1.0f, shelfB1 = 0.0f, shelfA1 = 0.0f;
    float phosphorAttackCoeff = 0.0f;
    float condAttackCoeff = 0.0f;
    float condReleaseCoeff = 0.0f;
    float elPanelAttackCoeff = 0.0f;
    float elPanelReleaseCoeff = 0.0f;
    float scLevelSmoothCoeff = 0.0f;
    float hardwareGainCompensation = 1.0f;

    HardwareEmulation::TransformerEmulation inputTransformer;
    HardwareEmulation::TransformerEmulation outputTransformer;
    HardwareEmulation::TubeEmulation tubeStage;
};

} // namespace duskaudio
