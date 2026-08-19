// Copyright (C) 2026 Dusk Audio , GNU GPL v3.0 or later (see repository LICENSE).
// Framework-free compressor mode math.  The detector/gain-cell equations in
// this file are direct C++17 transcriptions of multicomp.cpp; JUCE containers,
// Decibels, jlimit and IIR filters are replaced by small local helpers/shared DSP.
#pragma once

#include "MultiCompParams.hpp"
#include "MultiCompHelpers.hpp"
#include "../../shared-dpf/dsp/DuskCrossover.hpp"
#include "../../shared-dpf/dsp/DuskFilters.hpp"
#include "MultiCompHardware.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace duskaudio
{

class MultiCompModes
{
public:
    static constexpr int kChannels = 2;

    void prepare(double sampleRate, int /*maxBlock*/, int oversamplingFactor)
    {
        fs = sampleRate > 0.0 ? sampleRate : 48000.0;
        osFactor = oversamplingFactor == 4 ? 4 : (oversamplingFactor == 2 ? 2 : 1);
        const double modeRate = fs * osFactor;
        transientShaper.prepare(modeRate);
        lookupTables.prepare();
        for (auto& d : opto) d = OptoState{};
        for (auto& d : fet) d = FETState{};
        for (auto& d : vca) d = VCAState{};
        for (auto& d : bus) d = BusState{};
        for (auto& d : studioFet) d = StudioFETState{};
        for (auto& d : studioVca) d = StudioVCAState{};
        for (auto& d : digital) d = DigitalState{};
        prepareHardware(modeRate);
        for (int ch = 0; ch < kChannels; ++ch)
        {
            inputTransformerFet[ch].reset(); outputTransformerFet[ch].reset();
            inputTransformerBus[ch].reset(); outputTransformerBus[ch].reset();
        }
        for (auto& delay : digitalDelay)
            delay.assign(static_cast<size_t>(std::ceil(fs * 0.01 * 4.0)) + 2, 0.0f);
        digitalWrite.fill(0);
    }

    // Oversampling can be automated without reallocating or resetting the
    // mode state.  The host-rate buffers are provisioned for the maximum
    // factor in prepare(); only rate-dependent coefficients are refreshed at
    // this block boundary.
    void setRate(double sampleRate, int oversamplingFactor) noexcept
    {
        fs = sampleRate > 0.0 ? sampleRate : 48000.0;
        osFactor = oversamplingFactor == 4 ? 4 : (oversamplingFactor == 2 ? 2 : 1);
        const float sr = static_cast<float>(fs * osFactor);
        transientShaper.setRate(sr);
        updateRateCoefficients(sr);
    }

    void reset() noexcept
    {
        for (auto& d : opto) d = OptoState{};
        for (auto& d : fet) d = FETState{};
        for (auto& d : vca) d = VCAState{};
        for (auto& d : bus) d = BusState{};
        for (auto& d : studioFet) d = StudioFETState{};
        for (auto& d : studioVca) d = StudioVCAState{};
        for (auto& d : digital) d = DigitalState{};
        for (auto& delay : digitalDelay)
            for (auto& x : delay) x = 0.0f;
        digitalWrite.fill(0);
        resetHardware();
        transientShaper.reset();
    }

    float process(MultiCompMode mode, float input, int ch, const float sc,
                  const MultiCompParameterState& p, float localMix = 1.0f) noexcept
    {
        ch = std::clamp(ch, 0, 1);
        switch (mode)
        {
            case MultiCompMode::Opto: return processOpto(input, ch, sc, p);
            case MultiCompMode::FET: return processFET(input, ch, sc, p, false);
            case MultiCompMode::VCA: return processVCA(input, ch, sc, p);
            case MultiCompMode::Bus: return processBus(input, ch, sc, p, localMix);
            case MultiCompMode::StudioFET: return processFET(input, ch, sc, p, true);
            case MultiCompMode::StudioVCA: return processStudioVCA(input, ch, sc, p);
            case MultiCompMode::Digital: return processDigital(input, ch, sc, p, localMix);
            case MultiCompMode::Multiband: return input;
        }
        return input;
    }

    float gainReduction(MultiCompMode mode, int ch) const noexcept
    {
        ch = std::clamp(ch, 0, 1);
        switch (mode)
        {
            case MultiCompMode::Opto: return gainToDecibels(opto[ch].gain);
            case MultiCompMode::FET: return gainToDecibels(fet[ch].envelope);
            case MultiCompMode::VCA: return gainToDecibels(vca[ch].envelope);
            case MultiCompMode::Bus: return gainToDecibels(bus[ch].envelope);
            case MultiCompMode::StudioFET: return gainToDecibels(studioFet[ch].envelope);
            case MultiCompMode::StudioVCA: return gainToDecibels(studioVca[ch].envelope);
            case MultiCompMode::Digital: return gainToDecibels(digital[ch].envelope);
            case MultiCompMode::Multiband: break;
        }
        return 0.0f;
    }

private:
    struct OptoState
    {
        float el = 0, cell = 0, glow = 0, after = 0, charge = 0, conductance = 0;
        float gain = 1, sc = 0, dc = 0;
    };
    struct FETState
    {
        float envelope = 1, previous = 0, dc = 0, prevSq = 0, peak = 0;
        float releaseMemory = 0, sag = 0, hf = 0, tilt = 0, subBass = 0;
        float previousLevel = 0.0f, voltageSag = 0.0f, subBassHpState = 0.0f;
        int transient = 0, sagCounter = 0;
    };
    struct VCAState
    {
        float envelope = 1, rms = 0, signal = 0, rate = 0, previous = 0;
        float overshoot = 0, dc = 0, prevSq = 0;
    };
    struct BusState
    {
        float envelope = 1, rms = 0, previous = 0, hp = 0, prev = 0, hp2 = 0, prev2 = 0, compressed = 0;
    };
    struct StudioFETState : FETState {};
    struct StudioVCAState { float envelope = 1, rms = 0, previous = 0, smooth = 1; };
    struct DigitalState
    {
        float envelope = 1, peak = 0, rms = 0, crest = 1;
    };

    double fs = 48000.0;
    int osFactor = 1;
    std::array<OptoState, 2> opto{};
    std::array<FETState, 2> fet{};
    std::array<VCAState, 2> vca{};
    std::array<BusState, 2> bus{};
    std::array<StudioFETState, 2> studioFet{};
    std::array<StudioVCAState, 2> studioVca{};
    std::array<DigitalState, 2> digital{};
    std::array<std::vector<float>, kChannels> digitalDelay;
    std::array<int, 2> digitalWrite{{0, 0}};

    HardwareEmulation::TransformerEmulation inputTransformerOpto, outputTransformerOpto;
    HardwareEmulation::TubeEmulation optoTube;
    std::array<HardwareEmulation::TransformerEmulation, 2> inputTransformerFet, outputTransformerFet;
    std::array<HardwareEmulation::TransformerEmulation, 2> inputTransformerBus, outputTransformerBus;
    std::array<HardwareEmulation::TransformerEmulation, 2> inputTransformerStudioFet, outputTransformerStudioFet;
    std::array<HardwareEmulation::TransformerEmulation, 2> inputTransformerStudioVca, outputTransformerStudioVca;
    HardwareEmulation::StereoConvolution fetConvolution, busConvolution;
    MultiCompTransientShaper transientShaper;
    MultiCompLookupTables lookupTables;

    Biquad optoTiltShelf;
    float optoAttack = 0, optoRelease = 0, optoGlowDecay = 0, optoGlowAttack = 0;
    float optoCondAttack = 0, optoCondRelease = 0, optoElAttack = 0, optoElRelease = 0, optoScSmooth = 0;
    float fetTilt = 0, optoHardwareGain = 1.0f, fetHardwareGain = 1.0f;
    float busHardwareGain = 1.0f;

    void prepareHardware(double rate)
    {
        auto preparePair = [rate](auto& in, auto& out, const auto& profile, bool enable) {
            in.prepare(rate, 2); in.setProfile(profile.inputTransformer); in.setEnabled(enable);
            out.prepare(rate, 2); out.setProfile(profile.outputTransformer); out.setEnabled(enable);
        };
        const auto& op = HardwareEmulation::HardwareProfiles::getOptoCompressor();
        preparePair(inputTransformerOpto, outputTransformerOpto, op, true);
        optoTube.prepare(rate, 2);
        optoTube.setTubeType(HardwareEmulation::TubeEmulation::TubeType::Triode_12BH7);
        optoTube.setDrive(0.15f);
        calibrateOptoHardwareGain(rate);
        const auto& fp = HardwareEmulation::HardwareProfiles::getFETCompressor();
        const auto& bp = HardwareEmulation::HardwareProfiles::getConsoleBus();
        const auto& sfp = HardwareEmulation::HardwareProfiles::getStudioFET();
        const auto& svp = HardwareEmulation::HardwareProfiles::getStudioVCA();
        for (int ch = 0; ch < 2; ++ch)
        {
            preparePair(inputTransformerFet[ch], outputTransformerFet[ch], fp, true);
            preparePair(inputTransformerBus[ch], outputTransformerBus[ch], bp, true);
            preparePair(inputTransformerStudioFet[ch], outputTransformerStudioFet[ch], sfp, true);
            preparePair(inputTransformerStudioVca[ch], outputTransformerStudioVca[ch], svp, true);
        }
        fetConvolution.prepare(rate); fetConvolution.loadTransformerIR(HardwareEmulation::ShortConvolution::TransformerType::FET);
        busConvolution.prepare(rate); busConvolution.loadTransformerIR(HardwareEmulation::ShortConvolution::TransformerType::Console_Bus);
        calibrateFetHardwareGain(rate);
        calibrateBusHardwareGain(rate);
        const float sr = static_cast<float>(rate);
        updateRateCoefficients(sr);
    }

    void updateRateCoefficients(float sr) noexcept
    {
        optoAttack = std::exp(-1.0f / (0.002f * sr));
        optoRelease = std::exp(-1.0f / (0.060f * sr));
        optoGlowDecay = std::exp(-1.0f / (1.5f * sr));
        optoGlowAttack = std::pow(optoGlowDecay, 0.3f);
        optoCondAttack = 1.0f - std::exp(-2.0f * kDuskPi * 150.0f / sr);
        optoCondRelease = 1.0f - std::exp(-2.0f * kDuskPi * 4.0f / sr);
        optoElAttack = 1.0f - std::exp(-2.0f * kDuskPi * 150.0f / sr);
        optoElRelease = 1.0f - std::exp(-2.0f * kDuskPi * 5.0f / sr);
        optoScSmooth = 1.0f - std::exp(-2.0f * kDuskPi * 800.0f / sr);
        fetTilt = 1.0f - std::exp(-2.0f * kDuskPi * 800.0f / sr);
        optoTiltShelf.setCoeffs(Biquad::shelfSlope1(sr, 1000.0f, 3.0f, false));
    }

    template <typename ResetFn, typename SampleFn>
    static float calibrateChain(double rate, ResetFn&& resetFn, SampleFn&& sampleFn)
    {
        constexpr int calibrationSamples = 4800;
        constexpr float refAmplitude = 0.126f;
        constexpr float refFreq = 1000.0f;
        const float sr = static_cast<float>(rate);
        const float angularStep = 2.0f * kDuskPi * refFreq / sr;
        resetFn();
        const int warmup = static_cast<int>(sr * 0.05f);
        for (int i = 0; i < warmup; ++i)
            (void)sampleFn(refAmplitude * std::sin(angularStep * static_cast<float>(i)));
        double inputRmsSquared = 0.0, outputRmsSquared = 0.0;
        for (int i = 0; i < calibrationSamples; ++i)
        {
            const float input = refAmplitude * std::sin(angularStep * static_cast<float>(warmup + i));
            const float output = sampleFn(input);
            inputRmsSquared += static_cast<double>(input * input);
            outputRmsSquared += static_cast<double>(output * output);
        }
        resetFn();
        return outputRmsSquared > 1.0e-12 && inputRmsSquared > 1.0e-12
            ? 1.0f / static_cast<float>(std::sqrt(outputRmsSquared / inputRmsSquared)) : 1.0f;
    }

    void calibrateOptoHardwareGain(double rate)
    {
        optoHardwareGain = calibrateChain(rate,
            [this] { inputTransformerOpto.reset(); optoTube.reset(); outputTransformerOpto.reset(); },
            [this](float input) { float x = inputTransformerOpto.processSample(input, 0); x = optoTube.processSample(x, 0); return outputTransformerOpto.processSample(x, 0); });
    }

    void calibrateFetHardwareGain(double rate)
    {
        fetHardwareGain = calibrateChain(rate,
            [this] { inputTransformerFet[0].reset(); outputTransformerFet[0].reset(); fetConvolution.reset(); },
            [this](float input) { float x = inputTransformerFet[0].processSample(input, 0); x = outputTransformerFet[0].processSample(x, 0); return fetConvolution.processSample(x, 0); });
    }

    void calibrateBusHardwareGain(double rate)
    {
        busHardwareGain = calibrateChain(rate,
            [this] { inputTransformerBus[0].reset(); outputTransformerBus[0].reset(); busConvolution.reset(); },
            [this](float input) { float x = inputTransformerBus[0].processSample(input, 0); x = outputTransformerBus[0].processSample(x, 0); return busConvolution.processSample(x, 0); });
    }

    void resetHardware() noexcept
    {
        inputTransformerOpto.reset(); outputTransformerOpto.reset(); optoTube.reset();
        fetConvolution.reset(); busConvolution.reset();
        for (int ch = 0; ch < 2; ++ch)
        {
            inputTransformerFet[ch].reset(); outputTransformerFet[ch].reset();
            inputTransformerBus[ch].reset(); outputTransformerBus[ch].reset();
            inputTransformerStudioFet[ch].reset(); outputTransformerStudioFet[ch].reset();
            inputTransformerStudioVca[ch].reset(); outputTransformerStudioVca[ch].reset();
        }
    }

    float processOpto(float input, int ch, float sidechain, const MultiCompParameterState& p) noexcept
    {
        auto& d = opto[ch];
        // JUCE prepares OptoCompressor at the oversampled rate.  This method
        // is called once per oversampled sample, so all state updates whose
        // coefficients are expressed as invSampleRate must use that same
        // rate rather than the host-rate fs.
        const float sr = static_cast<float>(fs * osFactor);
        float x = inputTransformerOpto.processSample(input, ch);
        float compressed = x * d.gain;
        const float initialGr = 1.0f - d.gain;
        if (initialGr > 0.01f)
        {
            const float sq = compressed * compressed;
            d.dc = d.dc * 0.9999f + sq * 0.0001f;
            compressed += initialGr * 0.12f * (sq - d.dc);
        }
        const bool limit = p.optoLimit.load(std::memory_order_relaxed);
        const bool external = p.externalSidechain.load(std::memory_order_relaxed);
        float sc = external ? sidechain : compressed;
        if (external)
        {
            // JUCE bypasses the internal R37 shelf for an external source and
            // clears its state at the source transition.
            optoTiltShelf.reset();
        }
        else if (!limit)
        {
            sc = optoTiltShelf.process(sc);
        }
        else sc = input * 0.5f + compressed * 0.5f;
        const float pr = std::clamp(p.optoPeakReduction.load(std::memory_order_relaxed), 0.0f, 100.0f);
        const float peakReductionGain = std::pow(pr * 0.01f, 3.0f) * 14.0f;
        const float effectiveDrive = std::max(0.0f, std::abs(sc * peakReductionGain) - 0.03f);
        const float scLevel = std::tanh(effectiveDrive * 0.8f);
        d.sc += optoScSmooth * (scLevel - d.sc);
        const float elCoeff = d.sc > d.el ? optoElAttack : optoElRelease;
        d.el += elCoeff * (d.sc - d.el);
        const float lightLevel = d.el;
        if (lightLevel > d.cell)
            d.cell = lightLevel + (d.cell - lightLevel) * optoAttack;
        else
        {
            const float progDepFactor = 1.0f + d.charge * 5.0f;
            const float adjReleaseCoeff = std::pow(optoRelease, 1.0f / progDepFactor);
            d.cell = lightLevel + (d.cell - lightLevel) * adjReleaseCoeff;
        }

        if (lightLevel > d.glow)
            d.glow = lightLevel + (d.glow - lightLevel) * optoGlowAttack;
        else
        {
            const float slowDecayTime = 1.5f + d.charge * 3.0f;
            const float phosphorReleaseCoeff = std::exp(-1.0f / (sr * slowDecayTime));
            d.glow = lightLevel + (d.glow - lightLevel) * phosphorReleaseCoeff;
        }

        const float afterglowAttackCoeff = std::pow(
            std::exp(-1.0f / (5.0f * sr)), 0.25f);
        if (lightLevel > d.after)
            d.after = lightLevel + (d.after - lightLevel) * afterglowAttackCoeff;
        else
        {
            const float afterglowDecayTime = 5.0f + d.charge * 3.0f;
            const float afterglowReleaseCoeff = std::exp(-1.0f / (sr * afterglowDecayTime));
            d.after = lightLevel + (d.after - lightLevel) * afterglowReleaseCoeff;
        }
        d.charge = std::clamp(d.charge + d.cell * 0.15f / sr - d.charge * 0.12f / sr, 0.0f, 1.0f);
        const float response = std::clamp(d.cell + d.glow * 0.40f + d.after * 0.12f, 0.0f, 1.0f);
        const float conductance = response > 0.0f ? std::min(3.0f * std::pow(response, 0.7f), 6.0f) : 0.0f;
        const float cc = conductance > d.conductance ? optoCondAttack : optoCondRelease;
        d.conductance = std::clamp(d.conductance + cc * (conductance - d.conductance), 0.0f, 6.0f);
        float newGain = std::clamp(1.0f / (1.0f + d.conductance), 0.01f, 1.0f);
        float delta = newGain - d.gain;
        if (delta > 0.0f) delta = std::min(delta, 10.0f / sr);
        d.gain += delta;
        if (!std::isfinite(d.gain)) d.gain = 1.0f;
        const float grAmount = 1.0f - d.gain;
        const float makeup = decibelsToGain(optoKnobToGainDb(p.optoGain.load(std::memory_order_relaxed)));
        const float grCompensation = 1.0f / std::max(0.1f, d.gain);
        const float tubeBoost = 1.0f + (grCompensation - 1.0f) * 0.7f;
        optoTube.setDrive(0.15f + grAmount * 0.3f);
        float out = optoTube.processSample(compressed * makeup * tubeBoost, ch) / tubeBoost;
        out = outputTransformerOpto.processSample(out, ch);
        out *= optoHardwareGain;
        return std::clamp(out, -2.0f, 2.0f);
    }

    float processFET(float input, int ch, float sidechain, const MultiCompParameterState& p, bool studio) noexcept
    {
        auto& d = studio ? static_cast<FETState&>(studioFet[ch]) : fet[ch];
        const float sr = static_cast<float>(fs * osFactor);
        auto& inT = studio ? inputTransformerStudioFet[ch] : inputTransformerFet[ch];
        auto& outT = studio ? outputTransformerStudioFet[ch] : outputTransformerFet[ch];
        const float inputGain = decibelsToGain(p.fetInput.load(std::memory_order_relaxed));
        const float gained = inT.processSample(input, ch) * inputGain;
        const int ratioIndex = std::clamp(p.fetRatio.load(std::memory_order_relaxed), 0, 4);
        const float ratios[5] = {studio ? 4.0f : 3.85f, studio ? 8.0f : 7.40f, studio ? 12.0f : 12.50f, studio ? 20.0f : 21.50f, studio ? 20.0f : 21.50f};
        const float ratio = ratios[ratioIndex];
        const float compressed = gained * d.envelope;
        float saturated = compressed;
        if (!studio)
        {
            // Vintage FET saturation is inside the feedback loop.  The
            // sidechain must see this signal, not the post-envelope shortcut.
            const float grDb = -gainToDecibels(d.envelope + 0.001f);
            const float grNorm = std::clamp(grDb / 20.0f, 0.0f, 1.0f);
            const float k2 = ratioIndex == 4 ? 0.04f + grNorm * 0.04f
                                             : 0.024f + grNorm * 0.026f;
            const float k3 = ratioIndex == 4 ? 0.005f + grNorm * 0.010f
                                             : 0.004f + grNorm * 0.008f;
            const float sq = saturated * saturated;
            const float alpha = 1.0f / (1.0f + kDuskTwoPi * 10.0f / sr);
            const float h2 = alpha * (d.dc + sq - d.prevSq);
            d.dc = h2;
            d.prevSq = sq;
            saturated += k2 * h2 + k3 * saturated * saturated * saturated;
        }

        // FET junction capacitance choke in the feedback path.  Studio FET
        // uses the unsaturated compressed signal here, matching its JUCE class.
        const float feedbackInput = studio ? compressed : saturated;
        const float grDb = -gainToDecibels(d.envelope + 0.001f);
        const float grNorm = std::clamp(grDb / 20.0f, 0.0f, 1.0f);
        const float corner = 20000.0f - grNorm * 2000.0f;
        const float chokeCoeff = 1.0f - std::exp(-2.0f * kDuskPi * corner / sr);
        d.hf += chokeCoeff * (feedbackInput - d.hf);
        const bool external = p.externalSidechain.load(std::memory_order_relaxed);
        float detect = 0.0f;
        if (external)
            detect = std::abs(sidechain * inputGain);
        else if (ratioIndex == 4)
        {
            const float thresholdForPeak = decibelsToGain(studio ? -10.0f : p.fetThreshold.load(std::memory_order_relaxed));
            float instantLevel = std::abs(d.hf);
            const float detCeiling = thresholdForPeak * 1.5f;
            if (instantLevel > detCeiling)
                instantLevel = detCeiling + (instantLevel - detCeiling) / (1.0f + (instantLevel - detCeiling));
            const float peakAttackCoeff = std::exp(-1.0f / (0.00005f * sr));
            const float peakReleaseCoeff = std::exp(-1.0f / (0.005f * sr));
            if (instantLevel > d.peak)
                d.peak += (1.0f - peakAttackCoeff) * (instantLevel - d.peak);
            else
                d.peak += (1.0f - peakReleaseCoeff) * (instantLevel - d.peak);
            detect = d.peak;
        }
        else
            detect = std::abs(d.hf);
        d.tilt += fetTilt * (detect - d.tilt);
        detect = std::max(detect + (detect - d.tilt) * 0.35f, 0.0f);
        // JUCE applies the all-buttons threshold shift exactly once below via
        // abiThreshold = threshold * 0.5f.  Do not fold a second -6.0206 dB
        // offset into the base threshold here.
        const float threshold = decibelsToGain(studio ? -10.0f : p.fetThreshold.load(std::memory_order_relaxed));
        float reduction = 0.0f;
        if (ratioIndex == 4)
        {
            const float abiThreshold = threshold * 0.5f;
            if (detect > abiThreshold)
            {
                const float over = gainToDecibels(detect / abiThreshold);
                reduction = lookupTables.getAllButtonsReduction(over, p.fetCurve.load(std::memory_order_relaxed) != 0);
                if (p.fetTransient.load(std::memory_order_relaxed) > 0.01f)
                    reduction /= std::max(1.0f, transientShaper.process(input, ch, p.fetTransient.load(std::memory_order_relaxed)));
                reduction = std::min(reduction, 30.0f);
            }
            else if (studio)
            {
                const float below = -gainToDecibels(std::max(detect, 0.0001f) / abiThreshold);
                if (below > 0.0f && below < 3.0f) reduction = -std::sin((below / 3.0f) * kDuskPi);
            }
        }
        else if (detect > threshold)
        {
            const float over = gainToDecibels(detect / threshold);
            reduction = std::min(over * (1.0f - 1.0f / ratio), 60.0f);
        }
        // Studio FET allows the JUCE expansion bump to survive below threshold.
        reduction = std::clamp(reduction, -1.0f, 60.0f);
        const float minRelease = 0.05f, maxRelease = 1.1f;
        float attack = std::max(0.0001f, p.fetAttack.load(std::memory_order_relaxed) * 0.001f);
        const float releaseNorm = std::clamp(p.fetRelease.load(std::memory_order_relaxed) / 1100.0f, 0.0f, 1.0f);
        float release = minRelease * std::pow(maxRelease / minRelease, releaseNorm);
        if (ratioIndex == 4)
        {
            attack = std::max(0.0002f, attack * 2.0f);
            release *= 1.0f + std::clamp(reduction / 20.0f, 0.0f, 1.0f) * 0.5f;
            const float memoryDecay = std::exp(-1.0f / (0.5f * sr));
            d.releaseMemory *= memoryDecay;
            if (d.transient == 0 && reduction > 3.0f) d.releaseMemory = std::min(1.0f, d.releaseMemory + 0.15f);
            release *= 1.0f + d.releaseMemory * 0.3f;
        }
        const float programFactor = std::clamp(1.0f + reduction * 0.05f, 0.5f, 2.0f);
        const float signalDelta = std::abs(detect - d.previousLevel);
        d.previousLevel = detect;
        if (signalDelta > 0.1f) { attack *= 0.8f; release *= 1.2f; }
        else { attack *= programFactor; release *= programFactor; }
        const float target = decibelsToGain(-reduction);
        const float attackCoeff = std::exp(-1.0f / (std::max(1.0e-5f, attack * sr)));
        const float releaseCoeff = std::exp(-1.0f / (std::max(1.0e-5f, release * sr)));
        if (ratioIndex == 4)
        {
            if (target < d.envelope)
            {
                if (d.transient < 30) { const float delayedAttack = attackCoeff * 0.5f + 0.5f; d.envelope = delayedAttack * d.envelope + (1.0f - delayedAttack) * target; ++d.transient; }
                else d.envelope = attackCoeff * d.envelope + (1.0f - attackCoeff) * target;
            }
            else
            {
                d.transient = 0;
                const float gr = -gainToDecibels(d.envelope + 0.001f);
                const float fastBase = 0.05f, fastMin = 0.025f, scaleGr = std::clamp(gr / 20.0f, 0.0f, 1.0f);
                const float fast = fastBase - scaleGr * (fastBase - fastMin);
                const float fastCoeff = std::exp(-1.0f / (std::max(1.0e-5f, fast * sr)));
                const float effective = fastCoeff * scaleGr + releaseCoeff * (1.0f - scaleGr);
                d.envelope = effective * d.envelope + (1.0f - effective) * target;
            }
        }
        else if (target < d.envelope) d.envelope = attackCoeff * d.envelope + (1.0f - attackCoeff) * target;
        else d.envelope = releaseCoeff * d.envelope + (1.0f - releaseCoeff) * target;
        d.envelope = std::clamp(d.envelope, 0.001f, ratioIndex == 4 ? 1.12f : 1.0f);
        if (!std::isfinite(d.envelope)) d.envelope = 1.0f;
        float sagGain = 1.0f;
        if (ratioIndex == 4 && !studio)
        {
            const float gr = -gainToDecibels(d.envelope + 0.001f);
            const int limit = static_cast<int>(sr * 0.1f);
            d.sagCounter = gr > 15.0f ? std::min(d.sagCounter + 1, limit + 1) : std::max(0, d.sagCounter - 1);
            const float targetSag = d.sagCounter > limit ? 0.75f : 0.0f;
            const float attackSag = std::exp(-1.0f / (0.05f * sr)), releaseSag = std::exp(-1.0f / (0.3f * sr));
            const float coeffSag = targetSag > d.voltageSag ? attackSag : releaseSag;
            d.voltageSag += (1.0f - coeffSag) * (targetSag - d.voltageSag);
            sagGain = decibelsToGain(-d.voltageSag);
        }
        float out = saturated;
        if (studio)
        {
            out = gained * d.envelope;
            const float outputGrDb = -gainToDecibels(d.envelope + 0.001f);
            const float outputGrNorm = std::clamp(outputGrDb / 20.0f, 0.0f, 1.0f);
            const float scale = 0.3f;
            const float k2 = (ratioIndex == 4 ? 0.04f + outputGrNorm * 0.12f : 0.004f) * scale;
            const float k3 = (ratioIndex == 4 ? 0.005f + outputGrNorm * 0.015f : 0.001f) * scale;
            const float sq = out * out;
            const float hp = (d.dc + sq - d.prevSq) / (1.0f + kDuskTwoPi * 10.0f / sr);
            d.dc = hp;
            d.prevSq = sq;
            out += k2 * hp + k3 * out * out * out;
        }
        out *= sagGain;
        out = outT.processSample(out, ch);
        if (!studio) out = fetConvolution.processSample(out, ch) * fetHardwareGain;
        const float grHpf = -gainToDecibels(d.envelope + 0.001f);
        const float hpfCutoff = 20.0f + std::clamp(grHpf / 20.0f, 0.0f, 1.0f) * 60.0f;
        const float hpfAlpha = 1.0f - std::exp(-2.0f * kDuskPi * hpfCutoff / sr);
        if (!studio) { d.subBassHpState += hpfAlpha * (out - d.subBassHpState); out -= d.subBassHpState; }
        return std::clamp(out * decibelsToGain(p.fetOutput.load(std::memory_order_relaxed)), -2.0f, 2.0f);
    }

    float processVCA(float input, int ch, float sidechain, const MultiCompParameterState& p) noexcept
    {
        auto& d = vca[ch];
        const float sr = static_cast<float>(fs * osFactor);
        // JUCE's VCA is feed-forward: internal detection is the audio input
        // even when the host-side stereo-link buffer is populated.  Only an
        // actual external sidechain replaces the detector source.
        const float detect = std::abs(p.externalSidechain.load(std::memory_order_relaxed) ? sidechain : input);
        d.rate = d.rate * 0.95f + std::abs(detect - d.previous) * 0.05f;
        d.previous = detect;
        const float rmsMs = p.vcaClassicDetector.load(std::memory_order_relaxed) ? 0.010f : 0.005f + 0.030f * std::exp(-3.0f * std::clamp((gainToDecibels(std::max(detect, 0.0001f)) + 20.0f) / 30.0f, 0.0f, 1.0f));
        const float rmsCoeff = std::exp(-1.0f / (std::max(0.0001f, rmsMs) * sr));
        d.rms = d.rms * rmsCoeff + detect * detect * (1.0f - rmsCoeff);
        const float level = std::sqrt(std::max(d.rms, 0.0f));
        d.signal = d.signal * 0.99f + level * 0.01f;
        const float threshold = decibelsToGain(p.vcaThreshold.load(std::memory_order_relaxed));
        const float ratio = std::max(1.0f, p.vcaRatio.load(std::memory_order_relaxed));
        const float over = gainToDecibels(std::max(level, 1.0e-9f) / threshold);
        float reduction = 0.0f;
        if (p.vcaOverEasy.load(std::memory_order_relaxed))
        {
            const float kneeWidth = 10.0f, kneeStart = -5.0f, kneeEnd = 5.0f;
            if (over > kneeStart && over < kneeEnd) { const float x = over - kneeStart; reduction = (1.0f - 1.0f / ratio) * x * x / (2.0f * kneeWidth); }
            else if (over >= kneeEnd) reduction = over * (1.0f - 1.0f / ratio);
        }
        else if (level > threshold) reduction = over * (1.0f - 1.0f / ratio);
        reduction = std::clamp(reduction, 0.0f, 60.0f);
        const float userAttackScale = p.vcaAttack.load(std::memory_order_relaxed) / 15.0f;
        const float programAttack = reduction > 0.1f ? (reduction <= 10.0f ? 0.015f : (reduction <= 20.0f ? 0.005f : 0.003f)) : 0.015f;
        const float attack = std::clamp(programAttack * userAttackScale, 0.0001f, 0.050f);
        const float userRelease = p.vcaRelease.load(std::memory_order_relaxed) * 0.001f;
        const float programRelease = reduction > 0.1f ? std::max(0.008f, reduction / 120.0f) : 0.008f;
        const float blend = std::clamp((userRelease - 0.01f) / 0.5f, 0.0f, 1.0f);
        const float release = programRelease * (1.0f - blend) + userRelease * blend;
        const float target = decibelsToGain(-reduction);
        const float attackCoeff = std::exp(-1.0f / (std::max(1.0e-6f, attack) * sr));
        if (target < d.envelope)
        {
            d.envelope = target + (d.envelope - target) * attackCoeff;
            if (attack < 0.005f && reduction > 5.0f)
                d.overshoot = std::clamp((0.005f - attack) / 0.004f, 0.0f, 1.0f) * std::clamp(reduction / 20.0f, 0.0f, 1.0f) * 0.02f;
            else d.overshoot *= 0.95f;
        }
        else
        {
            const float currentDb = gainToDecibels(std::max(d.envelope, 0.0001f));
            const float rate = reduction > 0.1f ? reduction / std::max(0.001f, release) : 120.0f;
            d.envelope = decibelsToGain(std::min(currentDb + rate / sr, 0.0f));
            d.overshoot *= 0.98f;
        }
        d.envelope = std::clamp(d.envelope, 0.0001f, 1.0f);
        const float envelope = std::clamp(d.envelope * (1.0f + d.overshoot), 0.0001f, 1.0f);
        const float compressed = input * envelope;
        float out = compressed;
        if (std::abs(out) > 0.01f)
        {
            const float h2 = (d.dc + compressed * compressed - d.prevSq) / (1.0f + kDuskTwoPi * 10.0f / sr);
            d.dc = h2; d.prevSq = compressed * compressed;
            const float factor = reduction > 2.0f ? std::min(1.0f, reduction / 30.0f) : 0.0f;
            out += h2 * (0.0003f + 0.001f * factor) + compressed * compressed * compressed * (0.0006f + (reduction > 10.0f ? 0.0008f * factor : 0.0f));
            if (std::abs(out) > 1.5f) out = std::copysign(1.5f + std::tanh((std::abs(out) - 1.5f) * 0.3f) * 0.2f, out);
        }
        return std::clamp(out * decibelsToGain(p.vcaOutput.load(std::memory_order_relaxed)), -2.0f, 2.0f);
    }

    float processBus(float input, int ch, float sidechain, const MultiCompParameterState& p, float mix) noexcept
    {
        auto& d = bus[ch];
        const float sr = static_cast<float>(fs * osFactor);
        const float fb = d.compressed;
        const float alpha = 1.0f / (1.0f + kDuskTwoPi * 60.0f / sr);
        d.hp = alpha * (d.hp + fb - d.prev); d.prev = fb;
        d.hp2 = alpha * (d.hp2 + d.hp - d.prev2); d.prev2 = d.hp;
        const float rect = p.externalSidechain.load(std::memory_order_relaxed) ? std::abs(sidechain) : std::abs(d.hp2);
        const float rc = std::exp(-1.0f / (0.005f * sr));
        d.rms = rc * d.rms + (1.0f - rc) * rect * rect;
        const float level = std::sqrt(std::max(0.0f, d.rms));
        const float ratios[3] = {2.0f, 4.0f, 10.0f};
        const float ratio = ratios[std::clamp(p.busRatio.load(std::memory_order_relaxed), 0, 2)];
        const float over = gainToDecibels(std::max(level, 1.0e-9f) / decibelsToGain(p.busThreshold.load(std::memory_order_relaxed)));
        const float slope = 1.0f - 1.0f / ratio;
        float reduction = over <= -5.0f ? 0.0f : (over >= 5.0f ? over * slope : slope * (over + 5.0f) * (over + 5.0f) / (2.0f * 10.0f));
        reduction = std::min(reduction, 20.0f);
        const float attacks[6] = {0.1f, 0.3f, 1, 3, 10, 30};
        const float releases[5] = {100, 300, 600, 1200, -1};
        const float attack = attacks[std::clamp(p.busAttack.load(std::memory_order_relaxed), 0, 5)] * 0.001f;
        float release = releases[std::clamp(p.busRelease.load(std::memory_order_relaxed), 0, 4)] * 0.001f;
        if (release < 0.0f)
        {
            const float delta = std::abs(level - d.previous);
            d.previous = d.previous * 0.95f + level * 0.05f;
            const float transientDensity = std::clamp(delta * 20.0f, 0.0f, 1.0f);
            const float compressionFactor = std::clamp(reduction / 12.0f, 0.0f, 1.0f);
            release = 0.15f + (1.0f - transientDensity) * compressionFactor * (0.45f - 0.15f);
        }
        const float target = decibelsToGain(-reduction);
        const float coeff = std::exp(-1.0f / (std::max(1.0f, (target < d.envelope ? attack : release) * sr)));
        d.envelope = target + (d.envelope - target) * coeff;
        const float transformed = inputTransformerBus[ch].processSample(input, ch);
        float out = transformed * d.envelope;
        d.compressed = out;
        out += 0.004f * out * out + 0.003f * out * out * out;
        out = outputTransformerBus[ch].processSample(out, ch);
        out = busConvolution.processSample(out, ch) * busHardwareGain * decibelsToGain(p.busMakeup.load(std::memory_order_relaxed));
        out = std::clamp(out, -2.0f, 2.0f);
        return out * mix + input * (1.0f - mix);
    }

    float processStudioVCA(float input, int ch, float sidechain, const MultiCompParameterState& p) noexcept
    {
        auto& d = studioVca[ch];
        const float sr = static_cast<float>(fs * osFactor);
        const float transformed = inputTransformerStudioVca[ch].processSample(input, ch);
        const float x = p.externalSidechain.load(std::memory_order_relaxed) ? sidechain : input;
        const float rc = std::exp(-1.0f / (0.010f * sr));
        d.rms = rc * d.rms + (1.0f - rc) * x * x;
        const float level = std::sqrt(std::max(0.0f, d.rms));
        const float threshold = decibelsToGain(p.studioVcaThreshold.load(std::memory_order_relaxed));
        const float ratio = std::max(1.0f, p.studioVcaRatio.load(std::memory_order_relaxed));
        constexpr float kneeWidth = 6.0f;
        const float start = threshold * decibelsToGain(-kneeWidth * 0.5f), end = threshold * decibelsToGain(kneeWidth * 0.5f);
        float reduction = 0.0f;
        if (level > start)
        {
            if (level < end) { const float pos = (level - start) / (end - start); reduction = gainToDecibels(level / threshold) * (1.0f - 1.0f / (1.0f + (ratio - 1.0f) * pos * pos)); }
            else reduction = gainToDecibels(level / threshold) * (1.0f - 1.0f / ratio);
        }
        reduction = std::clamp(reduction, 0.0f, 40.0f);
        const float attack = std::clamp(p.studioVcaAttack.load(std::memory_order_relaxed) * 0.001f, 0.0003f, 0.075f);
        float release = std::clamp(p.studioVcaRelease.load(std::memory_order_relaxed) * 0.001f, 0.1f, 4.0f);
        const float signalDelta = std::abs(level - d.previous);
        d.previous = level;
        const float transientness = std::clamp(signalDelta * 15.0f, 0.0f, 1.0f);
        float releaseScale = 1.0f - transientness * 0.5f;
        const float compressionDepth = std::clamp((1.0f - d.envelope) * 5.0f, 0.0f, 1.0f);
        releaseScale *= 1.0f + compressionDepth * 0.3f * (1.0f - transientness);
        release *= releaseScale;
        const float target = decibelsToGain(-reduction);
        const float c = std::exp(-1.0f / ((target < d.envelope ? attack : release) * sr));
        d.envelope = c * d.envelope + (1.0f - c) * target;
        const float smooth = std::exp(-1.0f / (0.002f * sr));
        d.smooth = smooth * d.smooth + (1.0f - smooth) * d.envelope;
        float out = transformed * d.smooth;
        if (std::abs(out) > 1.2f)
            out = std::copysign(1.2f + 0.3f * std::tanh((std::abs(out) - 1.2f) * 3.0f), out);
        out = outputTransformerStudioVca[ch].processSample(out, ch);
        return std::clamp(out * decibelsToGain(p.studioVcaOutput.load(std::memory_order_relaxed)), -2.0f, 2.0f);
    }

    float processDigital(float input, int ch, float sidechain, const MultiCompParameterState& p, float mix) noexcept
    {
        auto& d = digital[ch];
        const float sr = static_cast<float>(fs * osFactor);
        const float lookahead = std::clamp(p.digitalLookahead.load(std::memory_order_relaxed), 0.0f, 10.0f);
        auto& delayLine = digitalDelay[static_cast<size_t>(ch)];
        const int delay = std::min(static_cast<int>(std::round(lookahead * 0.001f * sr)), static_cast<int>(delayLine.size()) - 1);
        int& wp = digitalWrite[ch];
        float delayed = input;
        if (delay > 0)
        {
            const int rp = (wp - delay + static_cast<int>(delayLine.size())) % static_cast<int>(delayLine.size());
            delayed = delayLine[static_cast<size_t>(rp)];
        }
        delayLine[static_cast<size_t>(wp)] = input;
        wp = (wp + 1) % static_cast<int>(delayLine.size());
        const float detect = std::abs(sidechain);
        const float db = gainToDecibels(std::max(detect, 0.00001f));
        const float threshold = p.digitalThreshold.load(std::memory_order_relaxed), ratio = std::max(1.0f, p.digitalRatio.load(std::memory_order_relaxed)), knee = std::max(0.0f, p.digitalKnee.load(std::memory_order_relaxed));
        float reduction = 0.0f;
        if (knee > 0.0f && db > threshold - knee * 0.5f)
        {
            if (db < threshold + knee * 0.5f) { const float pos = (db - threshold + knee * 0.5f) / knee; reduction = (db - threshold) * (1.0f - 1.0f / (1.0f + (ratio - 1.0f) * pos * pos)) * pos; }
            else reduction = (db - threshold) * (1.0f - 1.0f / ratio);
        }
        else if (db > threshold) reduction = (db - threshold) * (1.0f - 1.0f / ratio);
        reduction = std::max(0.0f, reduction);
        float release = std::max(0.001f, p.digitalRelease.load(std::memory_order_relaxed) * 0.001f);
        if (p.digitalAdaptive.load(std::memory_order_relaxed))
        {
            const float absolute = std::abs(input);
            const float peakCoeff = std::exp(-1.0f / (0.1f * sr));
            d.peak = absolute > d.peak ? absolute : peakCoeff * d.peak + (1.0f - peakCoeff) * absolute;
            const float rmsCoeff = std::exp(-1.0f / (0.3f * sr));
            d.rms = rmsCoeff * d.rms + (1.0f - rmsCoeff) * absolute * absolute;
            const float rms = std::sqrt(std::max(d.rms, 0.0f));
            d.crest = std::clamp(rms > 0.0001f ? d.peak / rms : 1.0f, 1.0f, 20.0f);
            const float multiplier = d.crest < 6.0f ? 1.0f + (6.0f - d.crest) / 5.0f
                                                     : 1.0f - std::min(1.0f, (d.crest - 6.0f) / 6.0f) * 0.67f;
            release *= multiplier;
        }
        const float target = decibelsToGain(-reduction), attack = std::max(0.0001f, p.digitalAttack.load(std::memory_order_relaxed) * 0.001f);
        const float c = std::exp(-1.0f / ((target < d.envelope ? attack : release) * sr));
        d.envelope = std::clamp(c * d.envelope + (1.0f - c) * target, 0.0001f, 1.0f);
        const float wet = std::clamp(delayed * d.envelope * decibelsToGain(p.digitalOutput.load(std::memory_order_relaxed)), -2.0f, 2.0f);
        return wet * mix + input * (1.0f - mix);
    }
};

} // namespace duskaudio
