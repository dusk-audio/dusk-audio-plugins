// Copyright (C) 2026 Dusk Audio , GNU GPL v3.0 or later (see repository LICENSE).
// Framework-free compressor mode math.  The detector/gain-cell equations in
// this file are direct C++17 transcriptions of multicomp.cpp; JUCE containers,
// Decibels, jlimit and IIR filters are replaced by small local helpers/shared DSP.
#pragma once

#include "MultiCompParams.hpp"
#include "MultiCompHelpers.hpp"
#include "../../shared-daf/dsp/DuskCrossover.hpp"
#include "../../shared-daf/dsp/DuskFilters.hpp"
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
        const double newFs = sampleRate > 0.0 ? sampleRate : 48000.0;
        const int newFactor = oversamplingFactor == 4 ? 4 : (oversamplingFactor == 2 ? 2 : 1);
        // The skip is only safe once the one-time work below has actually run.
        // Without the flag, a first prepare whose arguments happen to equal the
        // member defaults (48 kHz, factor 1: a 48 kHz host with oversampling
        // Off) returns here with digitalDelay still empty and the hardware
        // stages unprepared, and Digital mode then indexes an empty ring buffer.
        if (prepared && newFs == fs && newFactor == osFactor) return;
        fs = newFs;
        osFactor = newFactor;
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
        for (auto& filter : busSidechainHighPass) filter.reset();
        for (auto& filter : busSidechainLowShelf) filter.reset();
        for (auto& filter : busSidechainHighShelf) filter.reset();
        prepareHardware(modeRate);
        for (int ch = 0; ch < kChannels; ++ch)
        {
            inputTransformerFet[ch].reset(); outputTransformerFet[ch].reset();
            inputTransformerBus[ch].reset(); outputTransformerBus[ch].reset();
        }
        for (auto& delay : digitalDelay)
            delay.assign(static_cast<size_t>(std::ceil(fs * 0.01 * 4.0)) + 2, 0.0f);
        digitalWrite.fill(0);
        prepared = true;
    }

    // Oversampling can be automated without reallocating or resetting the
    // mode state.  The host-rate buffers are provisioned for the maximum
    // factor in prepare(); only rate-dependent coefficients are refreshed at
    // this block boundary.
    void setRate(double sampleRate, int oversamplingFactor) noexcept
    {
        const double newFs = sampleRate > 0.0 ? sampleRate : 48000.0;
        const int newFactor = oversamplingFactor == 4 ? 4 : (oversamplingFactor == 2 ? 2 : 1);
        if (newFs == fs && newFactor == osFactor) return;
        const double oldModeRate = fs * osFactor;
        fs = newFs;
        osFactor = newFactor;
        const float sr = static_cast<float>(fs * osFactor);
        transientShaper.setRate(sr);
        updateRateCoefficients(sr);
        // The Opto fields below store elapsed or remaining samples. Preserve
        // their physical time when runtime oversampling changes the rate.
        const double counterScale = static_cast<double>(sr) / oldModeRate;
        const auto scaleCounter = [counterScale](int value, int maximum) {
            if (value <= 0) return 0;
            return std::min(maximum, std::max(1, static_cast<int>(
                std::lround(static_cast<double>(value) * counterScale))));
        };
        for (auto& d : opto)
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
            d.colourPeakHold = scaleCounter(
                d.colourPeakHold, optoColourPeakHoldSamples);
        }
        updateHardwareRate(sr);
        selectHardwareGains();
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
        for (auto& filter : busSidechainHighPass) filter.reset();
        for (auto& filter : busSidechainLowShelf) filter.reset();
        for (auto& filter : busSidechainHighShelf) filter.reset();
        for (auto& delay : digitalDelay)
            for (auto& x : delay) x = 0.0f;
        digitalWrite.fill(0);
        resetHardware();
        transientShaper.reset();
    }

    // `external` is the EFFECTIVE sidechain state (the parameter AND a host
    // bus actually feeding us), not the raw parameter. Reading the parameter
    // in here made every detector switch to its external topology the moment
    // the user armed the switch, even with nothing connected, which changed
    // the compression character instead of doing nothing.
    float process(MultiCompMode mode, float input, int ch, const float sc,
                  const MultiCompParameterState& p, float localMix = 1.0f,
                  bool external = false, float optoDetector = 0.0f,
                  bool useOptoDetector = false) noexcept
    {
        ch = std::clamp(ch, 0, 1);
        switch (mode)
        {
            case MultiCompMode::Opto: return processOpto(
                input, ch, sc, p, external, optoDetector, useOptoDetector);
            case MultiCompMode::FET: return processFET(
                input, ch, sc, p, false, external, useOptoDetector);
            case MultiCompMode::VCA: return processVCA(input, ch, sc, p, external);
            case MultiCompMode::Bus: return processBus(input, ch, sc, p, localMix, external);
            case MultiCompMode::StudioFET: return processFET(
                input, ch, sc, p, true, external, false);
            case MultiCompMode::StudioVCA: return processStudioVCA(input, ch, sc, p, external);
            case MultiCompMode::Digital: return processDigital(input, ch, sc, p, localMix, external);
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
            case MultiCompMode::FET: return gainToDecibels(
                fet[ch].envelope * fet[ch].kneeGain
                    * fet[ch].recoveryGain);
            case MultiCompMode::VCA: return gainToDecibels(vca[ch].envelope);
            case MultiCompMode::Bus: return gainToDecibels(bus[ch].envelope);
            case MultiCompMode::StudioFET: return gainToDecibels(studioFet[ch].envelope);
            case MultiCompMode::StudioVCA: return gainToDecibels(studioVca[ch].envelope);
            case MultiCompMode::Digital: return gainToDecibels(digital[ch].envelope);
            case MultiCompMode::Multiband: break;
        }
        return 0.0f;
    }

    float fetColourGainReduction(int ch) const noexcept
    {
        ch = std::clamp(ch, 0, 1);
        return gainToDecibels(fet[ch].envelope);
    }

    float fetPostBurstRecoveryGain(int ch) const noexcept
    {
        return fet[static_cast<size_t>(std::clamp(ch, 0, 1))].recoveryGain;
    }

    void clearFetPostBurstRecovery(int channel = -1) noexcept
    {
        const auto clear = [](auto& d) noexcept {
            d.programmeExposureSeconds = 0.0f;
            d.programmeMaximumGrDb = 0.0f;
            d.programmeSilentSamples = 0;
            d.kneePeak = 0.0f;
            d.kneeBaseReductionDb = 0.0f;
            d.kneeCorrectionDb = 0.0f;
            d.kneeGain = 1.0f;
            d.recoveryPreviousInstantLevel = 0.0f;
            d.recoveryMaximumLevelDrop = 0.0f;
            d.recoveryMaximumGrDb = 0.0f;
            d.recoveryElapsedSeconds = 0.0f;
            d.recoveryTerminalWeight = 0.0f;
            d.recoveryReleasePosition = 0.5f;
            d.recoveryAttackPosition = 0.5f;
            d.recoveryGain = 1.0f;
            d.recoveryRatioIndex = 0;
            d.recoveryWasSupported = false;
        };
        if (channel >= 0 && channel < 2)
            clear(fet[static_cast<size_t>(channel)]);
        else
            for (auto& d : fet) clear(d);
    }

    void setBusSidechainControls(float highPassFrequency,
                                 float lowFrequency, float lowGain,
                                 float highFrequency, float highGain) noexcept
    {
        const float rate = static_cast<float>(fs * osFactor);
        const float hp = std::clamp(highPassFrequency, 0.0f, 500.0f);
        const float lowF = std::clamp(lowFrequency, 60.0f, 500.0f);
        const float lowG = std::clamp(lowGain, -12.0f, 12.0f);
        const float highF = std::clamp(highFrequency, 2000.0f, 16000.0f);
        const float highG = std::clamp(highGain, -12.0f, 12.0f);
        if (rate != busSidechainRate || hp != busSidechainHighPassFrequency)
            for (auto& filter : busSidechainHighPass)
                filter.setCoeffs(Biquad::highPass(rate, std::max(hp, 20.0f), 0.707f));
        if (rate != busSidechainRate || lowF != busSidechainLowFrequency
            || lowG != busSidechainLowGain)
            for (auto& filter : busSidechainLowShelf)
                filter.setCoeffs(Biquad::shelfSlope1(rate, lowF, lowG, false));
        if (rate != busSidechainRate || highF != busSidechainHighFrequency
            || highG != busSidechainHighGain)
            for (auto& filter : busSidechainHighShelf)
                filter.setCoeffs(Biquad::shelfSlope1(rate, highF, highG, true));
        busSidechainRate = rate;
        busSidechainHighPassFrequency = hp;
        busSidechainLowFrequency = lowF;
        busSidechainLowGain = lowG;
        busSidechainHighFrequency = highF;
        busSidechainHighGain = highG;
    }

    void processBusPair(float inputLeft, float inputRight,
                        float sidechainLeft, float sidechainRight,
                        const MultiCompParameterState& p, float mix,
                        bool external, float linkAmount,
                        float& outputLeft, float& outputRight) noexcept
    {
        auto& left = bus[0];
        auto& right = bus[1];
        const float sr = static_cast<float>(fs * osFactor);
        const float leftLevel = busRmsLevel(left,
            external ? std::abs(sidechainLeft) : busFeedbackRect(left, 0, sr), sr);
        const float rightLevel = busRmsLevel(right,
            external ? std::abs(sidechainRight) : busFeedbackRect(right, 1, sr), sr);
        const float linkedLevel = std::max(leftLevel, rightLevel);
        const float amount = std::clamp(linkAmount, 0.0f, 1.0f);
        advanceBusEnvelope(left, leftLevel + (linkedLevel - leftLevel) * amount, p, sr);
        advanceBusEnvelope(right, rightLevel + (linkedLevel - rightLevel) * amount, p, sr);
        outputLeft = renderBusOutput(inputLeft, 0, p, mix);
        outputRight = renderBusOutput(inputRight, 1, p, mix);
    }

private:
    struct OptoState
    {
        float gain = 1, detectorLevel = 0, detectorPeak = 0, chargePeak = 0;
        float colourPeak = 0, colourDc = 0;
        float fastGrDb = 0, midGrDb = 0, slowGrDb = 0;
        float isolatedEventGrDb = 0;
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
        int colourPeakHold = 0;
        bool detectorEventActive = false;
        // Starts saturated so a freshly reset state counts as silent.
        int detectorSilentSamples = 1 << 20;
    };
    struct FETState
    {
        float envelope = 1, previous = 0, dc = 0, prevSq = 0, peak = 0;
        float kneePeak = 0;
        float kneeBaseReductionDb = 0;
        float kneeCorrectionDb = 0, kneeGain = 1;
        float recoveryPreviousInstantLevel = 0;
        float recoveryMaximumLevelDrop = 0;
        float recoveryMaximumGrDb = 0, recoveryElapsedSeconds = 0;
        float recoveryTerminalWeight = 0, recoveryReleasePosition = 0.5f;
        float recoveryAttackPosition = 0.5f;
        float recoveryGain = 1;
        int recoveryRatioIndex = 0;
        float kneePeakHoldSeconds = 0;
        float kneeBaseHoldSeconds = 0;
        float fastGrDb = 0, intermediateGrDb = 0, slowGrDb = 0;
        float programmeExposureSeconds = 0, programmeMaximumGrDb = 0;
        float releaseMemory = 0, sag = 0, hf = 0, tilt = 0, subBass = 0;
        float colourPeak = 0, colourHighPassLow = 0, colourHighPassLower = 0;
        float colourLow = 0, colourLower = 0, colourLowPeak = 0;
        float shallowT3Lag = 0, shallowT3Lower = 0, shallowT3Lowest = 0;
        float shallowT5Lag = 0, shallowT5Lower = 0, shallowT5Lowest = 0;
        float colourLowDc = 0, colourLowPrevSq = 0;
        float colourLowHarmonicDc = 0;
        float previousLevel = 0.0f, voltageSag = 0.0f, subBassHpState = 0.0f;
        int transient = 0, sagCounter = 0, programmeSilentSamples = 0;
        bool recoveryWasSupported = false;
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
    bool prepared = false;
    std::array<OptoState, 2> opto{};
    std::array<FETState, 2> fet{};
    std::array<VCAState, 2> vca{};
    std::array<BusState, 2> bus{};
    std::array<StudioFETState, 2> studioFet{};
    std::array<StudioVCAState, 2> studioVca{};
    std::array<DigitalState, 2> digital{};
    std::array<Biquad, 2> busSidechainHighPass, busSidechainLowShelf,
                          busSidechainHighShelf;
    float busSidechainRate = 0.0f;
    float busSidechainHighPassFrequency = -1.0f;
    float busSidechainLowFrequency = -1.0f, busSidechainLowGain = 0.0f;
    float busSidechainHighFrequency = -1.0f, busSidechainHighGain = 0.0f;
    std::array<std::vector<float>, kChannels> digitalDelay;
    std::array<int, 2> digitalWrite{{0, 0}};

    std::array<HardwareEmulation::TransformerEmulation, 2> inputTransformerFet, outputTransformerFet;
    std::array<HardwareEmulation::TransformerEmulation, 2> inputTransformerBus, outputTransformerBus;
    std::array<HardwareEmulation::TransformerEmulation, 2> inputTransformerStudioFet, outputTransformerStudioFet;
    std::array<HardwareEmulation::TransformerEmulation, 2> inputTransformerStudioVca, outputTransformerStudioVca;
    HardwareEmulation::StereoConvolution fetConvolution, busConvolution;
    MultiCompTransientShaper transientShaper;
    MultiCompLookupTables lookupTables;

    static constexpr size_t kOptoDetectorSections = 5;
    std::array<std::array<Biquad, kOptoDetectorSections>, kChannels> optoDetectorWeighting;
    float optoInvSampleRate = 1.0f / 48000.0f;
    float optoDetectorAttack = 0, optoDetectorRelease = 0;
    float optoDetectorFloorPeakAttack = 0, optoDetectorFloorPeakRelease = 0;
    float optoDetectorPeakAttack = 0, optoDetectorPeakRelease = 0;
    float optoChargePeakRelease = 0;
    float optoColourPeakRelease = 0, optoColourDcSmoothing = 0;
    int optoChargeTopOffSamples = 1, optoFastPathSamples = 1;
    int optoColourPeakHoldSamples = 1;
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
    float fetTilt = 0, fetHardwareGain = 1.0f;
    float busHardwareGain = 1.0f;
    std::array<float, 3> fetHardwareGains{{1.0f, 1.0f, 1.0f}};
    std::array<float, 3> busHardwareGains{{1.0f, 1.0f, 1.0f}};

    void prepareHardware(double rate)
    {
        auto preparePair = [rate](auto& in, auto& out, const auto& profile, bool enable) {
            in.prepare(rate, 2); in.setProfile(profile.inputTransformer); in.setEnabled(enable);
            out.prepare(rate, 2); out.setProfile(profile.outputTransformer); out.setEnabled(enable);
        };
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
        cacheHardwareGains();
        updateHardwareRate(rate);
        selectHardwareGains();
        resetHardware();
        const float sr = static_cast<float>(rate);
        updateRateCoefficients(sr);
    }

    void updateRateCoefficients(float sr) noexcept
    {
        optoInvSampleRate = 1.0f / sr;
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
        optoColourPeakRelease = std::exp(-optoInvSampleRate / 0.040f);
        optoChargeTopOffSamples = std::max(1, static_cast<int>(
            std::lround(0.020f * sr)));
        optoFastPathSamples = std::max(1, static_cast<int>(
            std::lround(0.0015f * sr)));
        optoColourPeakHoldSamples = std::max(1, static_cast<int>(
            std::lround(0.002f * sr)));
        optoDetectorSilenceHoldSamples = std::max(1, static_cast<int>(
            std::lround(0.0005f * sr)));
        optoDetectorFloorHoldSamples = std::max(1, static_cast<int>(
            std::lround(0.030f * sr)));
        optoColourDcSmoothing = 1.0f - std::exp(
            -kDuskTwoPi * 10.0f * optoInvSampleRate);
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
        optoIsolatedEventRelease = std::exp(-optoInvSampleRate / 0.015f);
        fetTilt = 1.0f - std::exp(-2.0f * kDuskPi * 800.0f / sr);
        // Measured UAD LA-2A detector weighting. The shelf's equivalent Q is
        // the JSON fit's S=0.6998415302 converted to the RBJ shelf-Q form.
        // Design at the processing rate: processOpto is called at fs*osFactor.
        const std::array<BiquadCoeffs, kOptoDetectorSections> weightingCoeffs{{
            Biquad::shelf(sr, 319.1844220f, 3.778170748f, 0.5894442553f, false),
            Biquad::peak(sr, 134.4305880f, 1.245285462f, 0.5136042617f),
            Biquad::peak(sr, 880.5758706f, -0.4288385533f, 0.6986416568f),
            Biquad::peak(sr, 5840.123777f, 1.410524878f, 0.4820592696f),
            Biquad::peak(sr, 9991.669467f, -1.407323237f, 0.7988600998f)
        }};
        for (auto& channel : optoDetectorWeighting)
            for (size_t section = 0; section < kOptoDetectorSections; ++section)
                channel[section].setCoeffs(weightingCoeffs[section]);
    }

    static int hardwareGainIndex(int factor) noexcept
    {
        return factor == 4 ? 2 : (factor == 2 ? 1 : 0);
    }

    void updateHardwareRate(double rate) noexcept
    {
        auto updatePairs = [rate](auto& input, auto& output) {
            for (int ch = 0; ch < 2; ++ch)
            {
                input[ch].setSampleRate(rate);
                output[ch].setSampleRate(rate);
            }
        };
        updatePairs(inputTransformerFet, outputTransformerFet);
        updatePairs(inputTransformerBus, outputTransformerBus);
        updatePairs(inputTransformerStudioFet, outputTransformerStudioFet);
        updatePairs(inputTransformerStudioVca, outputTransformerStudioVca);
    }

    void cacheHardwareGains()
    {
        constexpr int factors[] = {1, 2, 4};
        for (int i = 0; i < 3; ++i)
        {
            const double rate = fs * factors[i];
            updateHardwareRate(rate);
            calibrateFetHardwareGain(rate);
            calibrateBusHardwareGain(rate);
            fetHardwareGains[static_cast<size_t>(i)] = fetHardwareGain;
            busHardwareGains[static_cast<size_t>(i)] = busHardwareGain;
        }
    }

    void selectHardwareGains() noexcept
    {
        const size_t index = static_cast<size_t>(hardwareGainIndex(osFactor));
        fetHardwareGain = fetHardwareGains[index];
        busHardwareGain = busHardwareGains[index];
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
        // Detector filters hold state between blocks. Clear every section for
        // both channels so reset/reprepare is deterministic on every platform.
        for (auto& channel : optoDetectorWeighting)
            for (auto& filter : channel)
                filter.reset();
        fetConvolution.reset(); busConvolution.reset();
        for (int ch = 0; ch < 2; ++ch)
        {
            inputTransformerFet[ch].reset(); outputTransformerFet[ch].reset();
            inputTransformerBus[ch].reset(); outputTransformerBus[ch].reset();
            inputTransformerStudioFet[ch].reset(); outputTransformerStudioFet[ch].reset();
            inputTransformerStudioVca[ch].reset(); outputTransformerStudioVca[ch].reset();
        }
    }

    inline static constexpr std::array<float, 23> kOptoCompressCurve{{
        0.9207f, 1.9474f, 3.1430f, 4.6974f, 6.1696f, 7.6165f,
        9.2638f, 10.8676f, 12.4485f, 13.9366f, 15.7917f, 17.2499f,
        19.0803f, 20.4897f, 22.3318f, 23.8425f, 25.2656f, 26.5058f,
        28.2780f, 29.5828f, 30.6369f, 32.1649f, 32.9052f}};
    inline static constexpr std::array<float, 23> kOptoLimitCurve{{
        0.9379f, 1.9978f, 3.2454f, 4.8601f, 6.4467f, 8.0793f,
        9.6964f, 11.7394f, 13.5249f, 15.2602f, 17.4341f, 19.2438f,
        21.5010f, 23.3864f, 25.7863f, 27.9270f, 30.0609f, 32.0834f,
        34.7103f, 36.8851f, 38.8184f, 40.5691f, 40.9082f}};

    // Installed UADx 1176 v1.0.3 control laws, measured from three source
    // levels at each position so neither compression nor the low-level floor
    // can masquerade as pot taper. Values are relative to the clockwise stop.
    // Multi-Comp's existing host ranges remain unchanged: saved/automated
    // values retain their knob position and are mapped only inside vintage FET.
    inline static constexpr std::array<float, 12> kFetControlPositions{{
        0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.625915527f,
        0.7f, 0.8f, 0.9f, 1.0f}};
    inline static constexpr std::array<float, 12> kFetInputRelativeDb{{
        -120.0f, -38.892933f, -33.678216f, -25.932386f, -20.790574f,
        -13.389983f, -8.620572f, -7.996695f, -6.054538f, -1.246802f,
        -0.066005f, 0.0f}};
    inline static constexpr std::array<float, 12> kFetOutputRelativeDb{{
        -120.0f, -76.428106f, -55.024575f, -41.535576f, -30.782583f,
        -21.810000f, -14.485545f, -12.846387f, -8.604931f, -4.012500f,
        -0.978134f, 0.0f}};

    // Vintage-FET attack drive law. Six measured points, no extrapolation:
    // see the note at the use site in processFET. The abscissa is the SETTLED
    // reduction in dB, which is what `positiveReduction` carries; the ordinate
    // multiplies the base attack time. Both ends are held, so the domain is
    // exactly the span the reference was rendered at.
    inline static constexpr std::array<float, 6> kFetDriveReductionDb{{
        4.2865f, 6.6572f, 9.1406f, 14.1087f, 24.0669f, 34.0261f}};
    inline static constexpr std::array<float, 6> kFetDriveAttackScale{{
        14.8651f, 10.6649f, 7.8264f,
        5.0588f, 2.9199f, 2.3322f}};

    static float fetDriveAttackScale(float positiveReductionDb) noexcept
    {
        // Geometric interpolation: a multiplier on a time constant is a ratio,
        // so the natural interpolant between two anchors is the constant-ratio
        // one, and the required curve is strongly convex across a factor of six
        // -- linear interpolation of the same anchors reads 3.8 % high at the
        // middle of the widest gap. The independent check on the interpolant is
        // the held-out -24 dBFS row (19.0 dB settled, in that widest gap, never
        // an anchor): curve RMS 0.0291, between its neighbours' 0.0309 and
        // 0.0262.
        //
        // A NaN argument falls through every comparison to the last entry,
        // which is finite and positive. The clamp it replaced passed NaN
        // straight through to the attack coefficient.
        if (positiveReductionDb <= kFetDriveReductionDb.front())
            return kFetDriveAttackScale.front();
        for (size_t i = 1; i < kFetDriveReductionDb.size(); ++i)
            if (positiveReductionDb <= kFetDriveReductionDb[i])
            {
                const float fraction
                    = (positiveReductionDb - kFetDriveReductionDb[i - 1])
                    / (kFetDriveReductionDb[i] - kFetDriveReductionDb[i - 1]);
                return kFetDriveAttackScale[i - 1] * std::pow(
                    kFetDriveAttackScale[i] / kFetDriveAttackScale[i - 1],
                    fraction);
            }
        return kFetDriveAttackScale.back();
    }

    static float fetControlLaw(float position,
                               const std::array<float, 12>& values) noexcept
    {
        position = std::clamp(position, 0.0f, 1.0f);
        for (size_t i = 1; i < kFetControlPositions.size(); ++i)
            if (position <= kFetControlPositions[i])
            {
                const float low = kFetControlPositions[i - 1];
                const float high = kFetControlPositions[i];
                const float fraction = (position - low) / (high - low);
                return values[i - 1] + fraction * (values[i] - values[i - 1]);
            }
        return values.back();
    }

    static float fetInputGainDb(float legacyValueDb) noexcept
    {
        const float position = (legacyValueDb + 20.0f) / 60.0f;
        // 39.850671 = 40.0 - 0.149329. The vintage chain carried a
        // frequency-flat +0.149329 dB (x1.017341) gain excess against the
        // reference: ten zero-gain-reduction operating points spanning 89 dB of
        // output level reproduce it to 2.6e-05 dB, and the zero-GR frequency
        // sweep reproduces it to 0.008 dB from 100 Hz to 16 kHz.
        //
        // It is removed here, ahead of the detector, rather than from the
        // output trim, because that is where it was measured to live. Removing
        // it post-compressor translates all 80 static cells by exactly
        // -0.149334 dB (measured spread 1.7e-07 dB) and leaves a residual
        // proportional to each cell's reduction slope: regressing that residual
        // on the measured per-cell slope gives -0.146893 dB per unit slope
        // against the -0.149334 a pure pre-detector error predicts, and the
        // correlation between residual and slope falls from -0.76 to +0.02 once
        // the excess is taken out here instead. The solved split is therefore
        // +0.1469 dB pre / +0.0025 dB post, i.e. all of it pre within the
        // measurement -- not the +0.093/+0.056 that four static rows suggested.
        // The 64 non-All-buttons static cells land within 0.082 dB this way
        // against 0.195 dB the other way.
        //
        // `inputGain` scales the audio and the detector together, so this also
        // lowers every settled reduction by slope x 0.149329 dB (0.124 dB at
        // 4:1). The attack drive table and the `fetBroadbandK2` lookup are both
        // reduction-indexed and therefore both moved with it. The attack drive
        // table was re-measured against that shift when it was written;
        // `fetBroadbandK2` was NOT, and this sentence used to claim both were.
        // It has since been re-fitted in its own right against a dense measured
        // surface -- see the note there.
        return 39.850671f + fetControlLaw(position, kFetInputRelativeDb);
    }

    static float fetOutputGainDb(float legacyValueDb) noexcept
    {
        const float position = (legacyValueDb + 20.0f) / 40.0f;
        // Absolute trim includes the measured linear convolution-chain loss;
        // the relative pot law above remains the independently fitted result.
        return 4.360566f + fetControlLaw(position, kFetOutputRelativeDb);
    }

    static float fetBroadbandK2(float gainReductionDb) noexcept
    {
        // The black-face unit's even-order path is programme dependent rather
        // than a monotonic polynomial drive: its equivalent coefficient falls
        // from 0.0042 at the compression onset to a minimum of 0.0020 near 7 dB
        // of reduction, then rises by a factor of nine to 0.0163 by 28 dB.
        //
        // COORDINATE. The argument is what the call site passes:
        // `-gainToDecibels(envelope + 0.001f)`, per sample. That is NOT the
        // settled output-referred reduction the render probes report -- the
        // +0.001 offset alone costs 0.087 dB at 20 dB -- and every anchor below
        // is stated in the call site's coordinate, measured there by
        // `MultiCompCoreTest --fet-h2-surface`. Do not re-fit these against a
        // reduction read anywhere else without moving the abscissa first.
        //
        // MEASUREMENT. 39 operating points at 1 kHz spanning Input 0.2-1.0 and
        // source -48 to -6 dBFS (`probe_h2_surface.py`, campaign
        // reference_comparison_1176), each giving the coefficient that lands
        // the reference unit's own H2: rows reaching the same reduction from
        // different knob/level combinations agree to about 0.1 dB, which is
        // what makes a reduction-indexed law the right shape here. Every
        // segment below contains at least two of those rows. The sixteen-row
        // campaign harmonic grid, which is all the previous anchor set ever had,
        // reaches eight distinct depths -- four of them zero -- so it left two of
        // that table's seven segments with no point at all and gave four more
        // exactly one, never in the interior. That is how a non-monotonic wiggle
        // (0.0027 at 2.8 dB, 0.0020 at 7.2, then 3x to 0.0059 at 13.0) survived:
        // its widest segment was sampled once, near the bottom edge, and read
        // 5.14 dB high in the middle. Worst residual of this fit over the 39
        // rows: 0.191 dB.
        //
        // Above 33.5 dB nothing is measured: the reference's own reduction law
        // collapses past roughly +37 dB internal (a separate, unfixed defect),
        // so the last segment is a straight-line extension of the fitted
        // 28-33.5 dB trend, not data.
        //
        // 100 Hz is deliberately not in the fit. There `lowFrequencyK2 *
        // colourLowH2` supplies most of the even order, so a 100 Hz row's
        // implied coefficient belongs to that mechanism, not to this one.
        constexpr std::array<float, 14> reductionDb{{
            0.0f, 0.6f, 1.5f, 3.2f, 5.0f, 7.0f, 9.0f,
            13.0f, 17.5f, 20.0f, 23.0f, 25.5f, 28.0f, 40.0f}};
        constexpr std::array<float, 14> coefficients{{
            0.003912f, 0.004212f, 0.003316f, 0.002510f, 0.002094f, 0.001966f,
            0.002194f, 0.003210f, 0.005215f, 0.006591f, 0.009206f, 0.012971f,
            0.016280f, 0.018549f}};
        gainReductionDb = std::clamp(gainReductionDb, 0.0f, 40.0f);
        for (size_t i = 1; i < reductionDb.size(); ++i)
            if (gainReductionDb <= reductionDb[i])
            {
                const float fraction = (gainReductionDb - reductionDb[i - 1])
                    / (reductionDb[i] - reductionDb[i - 1]);
                return coefficients[i - 1]
                    + fraction * (coefficients[i] - coefficients[i - 1]);
            }
        return coefficients.back();
    }

    static float fetBroadbandK3(float gainReductionDb) noexcept
    {
        // The black-face unit's broadband third harmonic is not reproduced by
        // one constant cubic. Its equivalent coefficient grows modestly from
        // compression onset to 8 dB of reduction, then falls to roughly one
        // third of the old -0.006 coefficient by 33 dB.
        //
        // COORDINATE. As for `fetBroadbandK2`, every anchor is expressed in the
        // call site's `-gainToDecibels(envelope + 0.001f)` coordinate, measured
        // in process by `MultiCompCoreTest --fet-h2-surface`. It is not the
        // settled output-referred reduction printed by the VST render probe.
        //
        // MEASUREMENT. The zero and full cubic builds identify the complex 3f
        // contribution over the dense 1 kHz surface. Separate half-cubic builds
        // reproduce their complex midpoint to 0.003712 dB before, and 0.003494
        // dB after, the independently fitted low-frequency T3 correction. That
        // establishes this coefficient as a scalable source rather than a
        // magnitude coincidence. The first fit landed at 0.089538 dB measured
        // worst over the 36 reference rows above the -92 dBc floor. Changing
        // low T3 then exposed this path's finite 1 kHz leakage, so the same
        // zero/half/full inversion was repeated on top of it. Retaining anchors
        // until piecewise interpolation is within 0.10 dB now predicts 0.097975
        // dB worst at 1 kHz while preserving 100 Hz at 0.631009 dB worst.
        //
        // This table fits H3 only. The same control predicts up to 4.93 dB H5
        // error, which remains assigned to the separately measured fifth-order
        // path rather than being hidden in this coefficient.
        constexpr std::array<float, 13> reductionDb{{
            0.0f, 0.3543f, 1.0175f, 3.2496f, 6.9921f, 10.0089f, 12.6634f,
            19.8974f, 22.6659f, 27.5467f, 31.0579f, 33.4397f, 40.0f}};
        constexpr std::array<float, 13> coefficients{{
            -0.004527116f, -0.004527116f, -0.005052484f, -0.005072347f,
            -0.005519443f, -0.005649644f, -0.005065947f, -0.003015710f,
            -0.002580895f, -0.002362978f, -0.001939411f, -0.001867239f,
            -0.001867239f}};
        gainReductionDb = std::clamp(gainReductionDb, 0.0f, 40.0f);
        for (size_t i = 1; i < reductionDb.size(); ++i)
            if (gainReductionDb <= reductionDb[i])
            {
                const float fraction = (gainReductionDb - reductionDb[i - 1])
                    / (reductionDb[i] - reductionDb[i - 1]);
                return coefficients[i - 1]
                    + fraction * (coefficients[i] - coefficients[i - 1]);
            }
        return coefficients.back();
    }

    static float fetLowFrequencyK3(float gainReductionDb) noexcept
    {
        // The low-frequency third-order transformer path is separately
        // reduction-dependent. A constant -0.0058 coefficient leaves the
        // installed unit's 100 Hz H3 up to 5.51 dB away, but this path is not
        // perfectly band-limited: fitting 100 Hz alone moves 1 kHz H3 by more
        // than 1 dB and undoes the broadband cubic fit above.
        //
        // COORDINATE. These are anchors in the same call-site internal-GR
        // coordinate as the broadband K2/K3 tables, measured by
        // `MultiCompCoreTest --fet-h2-surface`.
        //
        // MEASUREMENT. Zero, half, and full low-T3 builds prove complex
        // superposition to 0.000226 dB worst over scoreable H3. A constrained
        // fit uses all 96 scoreable 100 Hz H3 rows while bounding all 36
        // scoreable 1 kHz rows, then removes anchors until either frequency's
        // predeclared guard would fail. This eight-anchor result predicts
        // 0.606240 dB worst at 100 Hz and 0.897453 dB at 1 kHz, compared with
        // 5.510934 and 0.089538 dB for the constant coefficient. The broadband
        // residual is intentionally closed again by that mechanism's own fit;
        // it is kept visible here rather than hidden in an unconstrained
        // low-frequency table.
        //
        // H5 is not part of this fit and remains assigned to the independent
        // fifth-order term below.
        constexpr std::array<float, 8> reductionDb{{
            0.0f, 0.2378f, 0.5373f, 0.7234f, 1.3622f, 2.5753f, 13.8515f,
            40.0f}};
        constexpr std::array<float, 8> coefficients{{
            -0.002084180f, -0.004712892f, -0.003397340f, -0.003469579f,
            -0.005932644f, -0.006994165f, -0.004806491f, -0.006058491f}};
        gainReductionDb = std::clamp(gainReductionDb, 0.0f, 40.0f);
        for (size_t i = 1; i < reductionDb.size(); ++i)
            if (gainReductionDb <= reductionDb[i])
            {
                const float fraction = (gainReductionDb - reductionDb[i - 1])
                    / (reductionDb[i] - reductionDb[i - 1]);
                return coefficients[i - 1]
                    + fraction * (coefficients[i] - coefficients[i - 1]);
            }
        return coefficients.back();
    }

    static std::array<float, 4> fetShallowKneeOddCorrection(
        float drivenInputDb) noexcept
    {
        // The 100 Hz shallow-knee H3 is complex: the installed unit's phase
        // moves from 164.6 to 90.7 degrees while the reduction cell engages.
        // A scalar correction can match magnitude only by jumping between two
        // coefficient roots. These two coefficients instead scale the first
        // and second 300 Hz low-pass poles of a pure T3 basis made from the
        // held raw input. The matching pair of filtered T5 bases jointly holds
        // the prior H5 complex vector, preventing the T3 correction from
        // inheriting fifth harmonic from reconstruction ripple. Using the
        // detector input rather than the already-compressed colour waveform
        // and retaining both poles prevents H5 leakage; omitting the poles
        // moved the established 1 kHz H3 guard by 0.74 dB. Even
        // half-dB rows from -14 through -6 dB are the fit anchors; the
        // intervening quarter-dB reference rows are held out. The endpoints
        // fade the cell to zero outside the measured 4:1 knee and preserve the
        // established deeper-reduction harmonic surface.
        constexpr std::array<float, 19> driveDb{{
            -15.0f, -14.0f, -13.5f, -13.0f, -12.5f, -12.0f, -11.5f,
            -11.0f, -10.5f, -10.0f, -9.5f, -9.0f, -8.5f, -8.0f,
            -7.5f, -7.0f, -6.5f, -6.0f, -4.0f}};
        constexpr std::array<float, 19> direct{{
            0.0f, 0.000000147296f, -0.000012437259f, -0.000035329776f,
            -0.000073386195f, -0.000116595019f, -0.000146058870f,
            -0.000167959659f, -0.000201904590f, -0.000242491601f,
            -0.000318190851f, -0.000423721595f, -0.000564032270f,
            -0.000698676667f, -0.000822144740f, -0.000966098630f,
            -0.001131396372f, -0.001393907435f, 0.0f}};
        constexpr std::array<float, 19> lag{{
            0.0f, -0.000004630468f, 0.000020562542f, 0.000067365078f,
            0.000146981010f, 0.000236175923f, 0.000293332999f,
            0.000334985008f, 0.000396516160f, 0.000387766529f,
            0.000409416966f, 0.000448438044f, 0.000549813571f,
            0.000622585136f, 0.000659935770f, 0.000737630182f,
            0.000851202512f, 0.001211519530f, 0.0f}};
        constexpr std::array<float, 19> fifthLag{{
            0.0f, -0.000000015059f, -0.000000011610f, -0.000000001779f,
            0.000000020677f, 0.000000045071f, 0.000000046848f,
            0.000000042798f, 0.000000028838f, -0.000000269259f,
            -0.000000699823f, -0.000001267242f, -0.000001847419f,
            -0.000002489201f, -0.000003196090f, -0.000003875615f,
            -0.000004587354f, -0.000005053848f, 0.0f}};
        constexpr std::array<float, 19> fifthLower{{
            0.0f, -0.000000019836f, 0.000000139720f, 0.000000432226f,
            0.000000928802f, 0.000001480890f, 0.000001844100f,
            0.000002119219f, 0.000002523170f, 0.000002663537f,
            0.000003078891f, 0.000003709499f, 0.000004741675f,
            0.000005619440f, 0.000006332498f, 0.000007265373f,
            0.000008476385f, 0.000011060490f, 0.0f}};
        if (drivenInputDb <= driveDb.front()
            || drivenInputDb >= driveDb.back())
            return {{0.0f, 0.0f, 0.0f, 0.0f}};
        for (size_t i = 1; i < driveDb.size(); ++i)
            if (drivenInputDb <= driveDb[i])
            {
                const float fraction = (drivenInputDb - driveDb[i - 1])
                    / (driveDb[i] - driveDb[i - 1]);
                // The fitted -14..-6 dB anchors are uniformly spaced. Cubic
                // interpolation keeps their measured curvature without using
                // any of the intervening quarter-dB validation rows. The two
                // unequal endpoint fades remain linear.
                if (i - 1 >= 2 && i <= 16)
                {
                    const auto cubic = [fraction](float p0, float p1,
                                                  float p2, float p3) noexcept {
                        const float t2 = fraction * fraction;
                        const float t3 = t2 * fraction;
                        return 0.5f * (2.0f * p1
                            + (-p0 + p2) * fraction
                            + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3)
                                * t2
                            + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
                    };
                    return {{
                        cubic(direct[i - 2], direct[i - 1], direct[i],
                              direct[i + 1]),
                        cubic(lag[i - 2], lag[i - 1], lag[i], lag[i + 1]),
                        cubic(fifthLag[i - 2], fifthLag[i - 1], fifthLag[i],
                              fifthLag[i + 1]),
                        cubic(fifthLower[i - 2], fifthLower[i - 1],
                              fifthLower[i], fifthLower[i + 1])}};
                }
                return {{
                    direct[i - 1] + fraction
                        * (direct[i] - direct[i - 1]),
                    lag[i - 1] + fraction * (lag[i] - lag[i - 1]),
                    fifthLag[i - 1] + fraction
                        * (fifthLag[i] - fifthLag[i - 1]),
                    fifthLower[i - 1] + fraction
                        * (fifthLower[i] - fifthLower[i - 1])}};
            }
        return {{0.0f, 0.0f, 0.0f, 0.0f}};
    }

    static std::array<float, 4> fetBroadbandComplexH3Correction(
        float inputPosition, float releasePosition,
        float drivenInputDb, double hostSampleRate) noexcept
    {
        // The sparse reduction-only K3 fit gets the original campaign anchors'
        // magnitudes right, but misses the UAD vector between them: at Input
        // 0.8 its H3 phase rotates by more than 120 degrees while the cubic
        // stays near 135 degrees. A pure raw-input T3 and its first three
        // 300 Hz poles provide four real degrees of freedom: two solve the
        // reference's complex 1 kHz H3 and two force the same cell's complex
        // 100 Hz H3 contribution to zero. T3 contributes no H1/H2/H5.
        //
        // The Wave 27 scalar contradiction was a missing coordinate, not a
        // rate law: its matched-reduction probe used Release 0.5, while the
        // original harmonic grid used 0.66595459. The required lag coefficient
        // changes sign between those controls. Both tables below are fitted at
        // 48 kHz. Release-0.5 rows set a 0.9826 source normalisation at 96 kHz;
        // four untouched campaign-Release rows then validate to 0.075 dB / 0.42
        // degrees. Release interpolation holds its measured endpoints.
        constexpr std::array<float, 9> halfReleaseDriveDb{{
            -10.0f, -6.0f, 0.0f, 6.0f, 12.0f, 18.0f, 24.0f, 30.0f, 34.0f}};
        constexpr std::array<std::array<float, 9>, 4> halfRelease{{
            {{-0.000001129286f, 0.000005640098f, 0.000020928763f,
               0.000015788718f, 0.000013878506f, 0.000017151017f,
               0.000011323861f, 0.000009239721f, 0.000010164660f}},
            {{0.000126741287f, 0.000260478093f, 0.000207309249f,
              0.000174141674f, 0.000104720559f, 0.000050368387f,
              0.000024496578f, 0.000013932452f, 0.000010243772f}},
            {{-0.000253599807f, -0.000537741764f, -0.000462143430f,
              -0.000384481333f, -0.000240319580f, -0.000137359480f,
              -0.000073002796f, -0.000047337873f, -0.000041811016f}},
            {{0.000248913519f, 0.000543718419f, 0.000499131315f,
              0.000412038918f, 0.000265486269f, 0.000170002288f,
              0.000094725879f, 0.000065180976f, 0.000061539456f}},
        }};
        constexpr std::array<float, 8> campaignReleaseDriveDb{{
            -9.396131f, 2.603869f, 8.603869f, 14.603869f,
            20.603869f, 26.603869f, 29.603869f, 32.603869f}};
        constexpr std::array<std::array<float, 8>, 4> campaignRelease{{
            {{-0.000001761571f, 0.000028899354f, 0.000019785929f,
               0.000020777596f, 0.000019925249f, 0.000010698004f,
               0.000010906451f, 0.000010859942f}},
            {{0.000048332712f, -0.000289051296f, -0.000162517054f,
              -0.000130925980f, -0.000093221427f, -0.000059479269f,
              -0.000046187095f, -0.000036586152f}},
            {{-0.000093944202f, 0.000523663427f, 0.000287074460f,
              0.000221217220f, 0.000146846412f, 0.000097882236f,
              0.000070606261f, 0.000051314367f}},
            {{0.000089547765f, -0.000461366619f, -0.000245113153f,
              -0.000177927938f, -0.000105963674f, -0.000075748103f,
              -0.000048322742f, -0.000029309312f}},
        }};
        const auto tableValue = [drivenInputDb](const auto& positions,
                                                const auto& values) noexcept {
            if (drivenInputDb <= positions.front()) return values.front();
            for (size_t i = 1; i < positions.size(); ++i)
                if (drivenInputDb <= positions[i])
                {
                    const float fraction
                        = (drivenInputDb - positions[i - 1])
                        / (positions[i] - positions[i - 1]);
                    return values[i - 1]
                        + fraction * (values[i] - values[i - 1]);
                }
            return values.back();
        };
        const float releaseBlend = std::clamp(
            (releasePosition - 0.5f) / (0.66595459f - 0.5f),
            0.0f, 1.0f);
        std::array<float, 4> coefficients{};
        for (size_t basis = 0; basis < coefficients.size(); ++basis)
        {
            const float half = tableValue(
                halfReleaseDriveDb, halfRelease[basis]);
            coefficients[basis] = half + releaseBlend * (tableValue(
                campaignReleaseDriveDb, campaignRelease[basis]) - half);
        }
        // Only Input 0.8 has the dense complex oracle. The nearest measured
        // Input 0.7/0.9 rows retain the established reduction-only surface;
        // interpolate continuously between those three measured positions.
        const float positionWeight = std::clamp(
            1.0f - std::abs(inputPosition - 0.8f) / 0.1f, 0.0f, 1.0f);
        const float rateBlend = std::clamp(
            static_cast<float>((hostSampleRate - 48000.0) / 48000.0),
            0.0f, 1.0f);
        const float rateScale = 1.0f - 0.0174f * rateBlend;
        for (float& coefficient : coefficients)
            coefficient *= positionWeight * rateScale;
        return coefficients;
    }

    static std::array<float, 4> fetBroadbandComplexH5Correction(
        float inputPosition, float releasePosition,
        float drivenInputDb, double hostSampleRate) noexcept
    {
        // The reduction-only broadband-T5 table matches the sparse campaign
        // magnitudes, but not their complex vectors, and it misses the dense
        // Release-0.5 surface by 5.13 dB / 137.48 degrees. Scaling that source
        // cannot close the gap: its least-squares optimum still leaves up to
        // 2.51 dB / 33.62 degrees. A raw T5 plus its first three 300 Hz poles
        // supplies four real degrees of freedom. Two solve the complex 1 kHz
        // H5 residual and two force the same cell's complex 100 Hz H5 to zero.
        // T5 contributes no H1-H4 for the settled calibration sine.
        //
        // The tables are same-stimulus UAD fits at Release 0.5 and the original
        // campaign Release 0.66595459, separately at 48/96 kHz. Zero guards
        // below the -92 dBc scoring onset keep the correction dormant where
        // the reference fifth harmonic is not measurable. Input 0.2 and 0.8
        // are the only campaign positions with a scoreable complex oracle;
        // triangular weights leave their measured neighbours unchanged.
        constexpr std::array<float, 9> halfDriveDb{{
            -10.0f, -6.0f, 0.0f, 6.0f, 12.0f, 18.0f, 24.0f, 30.0f, 34.0f}};
        constexpr std::array<std::array<float, 4>, 9> half48{{
            {{0.0f, 0.0f, 0.0f, 0.0f}},
            {{-2.496762303e-05f, -6.385083138e-04f, 1.322520898e-03f, -2.605647916e-03f}},
            {{-3.514863744e-05f, -9.802532544e-04f, 2.028960828e-03f, -3.975562361e-03f}},
            {{-2.844470984e-05f, -7.681089883e-04f, 1.590256165e-03f, -3.122185053e-03f}},
            {{-1.812680163e-05f, -4.895134041e-04f, 1.013464766e-03f, -1.989751412e-03f}},
            {{-1.124180239e-05f, -3.045824937e-04f, 6.305764770e-04f, -1.237766028e-03f}},
            {{-6.849515997e-06f, -1.858045370e-04f, 3.846670558e-04f, -7.550101469e-04f}},
            {{-4.103466552e-06f, -1.111874142e-04f, 2.301909137e-04f, -4.518421406e-04f}},
            {{-2.953060083e-06f, -7.837672966e-05f, 1.622897785e-04f, -3.189758675e-04f}},
        }};
        constexpr std::array<std::array<float, 4>, 9> half96{{
            {{0.0f, 0.0f, 0.0f, 0.0f}},
            {{-2.199947933e-05f, -6.955178209e-04f, 1.417204358e-03f, -2.795922969e-03f}},
            {{-3.178495081e-05f, -1.039834703e-03f, 2.118426437e-03f, -4.171582779e-03f}},
            {{-2.495754847e-05f, -8.237529328e-04f, 1.678135300e-03f, -3.303008516e-03f}},
            {{-1.597939544e-05f, -5.263453760e-04f, 1.072272524e-03f, -2.110740121e-03f}},
            {{-9.907290171e-06f, -3.287461995e-04f, 6.696985743e-04f, -1.317771421e-03f}},
            {{-6.047465184e-06f, -2.011216196e-04f, 4.097062525e-04f, -8.060867201e-04f}},
            {{-3.672773926e-06f, -1.206963874e-04f, 2.458859059e-04f, -4.840796946e-04f}},
            {{-2.616770294e-06f, -8.514188609e-05f, 1.734619187e-04f, -3.416789155e-04f}},
        }};
        constexpr std::array<float, 8> campaignEightDriveDb{{
            -10.0f, 2.603869f, 8.603869f, 14.603869f, 20.603869f,
            26.603869f, 29.603869f, 32.603869f}};
        constexpr std::array<std::array<float, 4>, 8> campaignEight48{{
            {{0.0f, 0.0f, 0.0f, 0.0f}},
            {{-3.392391092e-05f, -1.252005652e-03f, 2.586600369e-03f, -4.992588415e-03f}},
            {{-2.295591452e-05f, -8.489944705e-04f, 1.753971953e-03f, -3.385138287e-03f}},
            {{-1.444476717e-05f, -5.389668373e-04f, 1.113416694e-03f, -2.147990378e-03f}},
            {{-8.945497246e-06f, -3.353536706e-04f, 6.927667765e-04f, -1.336185579e-03f}},
            {{-5.372873574e-06f, -2.021097288e-04f, 4.175060493e-04f, -8.051449580e-04f}},
            {{-4.179771556e-06f, -1.565668807e-04f, 3.234341623e-04f, -6.238524233e-04f}},
            {{-3.254192256e-06f, -1.217262133e-04f, 2.514626946e-04f, -4.850624161e-04f}},
        }};
        constexpr std::array<std::array<float, 4>, 8> campaignEight96{{
            {{0.0f, 0.0f, 0.0f, 0.0f}},
            {{-2.853863582e-05f, -1.331864879e-03f, 2.709335030e-03f, -5.249923320e-03f}},
            {{-2.058420386e-05f, -9.509113032e-04f, 1.934452523e-03f, -3.749883227e-03f}},
            {{-1.262977189e-05f, -5.699577274e-04f, 1.159570016e-03f, -2.249843134e-03f}},
            {{-8.677201044e-06f, -3.902275582e-04f, 7.939217905e-04f, -1.540609249e-03f}},
            {{-4.724630197e-06f, -2.104973889e-04f, 4.282735793e-04f, -8.313753642e-04f}},
            {{-3.800751490e-06f, -1.683110645e-04f, 3.424497924e-04f, -6.649333896e-04f}},
            {{-2.876872782e-06f, -1.261247400e-04f, 2.566260055e-04f, -4.984914149e-04f}},
        }};
        constexpr std::array<float, 3> campaignTwoDriveDb{{
            -10.0f, -5.827545f, 0.172455f}};
        constexpr std::array<std::array<float, 4>, 3> campaignTwo48{{
            {{0.0f, 0.0f, 0.0f, 0.0f}},
            {{-2.400488442e-05f, -9.349371226e-04f, 1.930961527e-03f, -3.717920381e-03f}},
            {{-3.593272669e-05f, -1.310352387e-03f, 2.707331605e-03f, -5.228575893e-03f}},
        }};
        constexpr std::array<std::array<float, 4>, 3> campaignTwo96{{
            {{0.0f, 0.0f, 0.0f, 0.0f}},
            {{-2.104118307e-05f, -9.690486077e-04f, 1.971370845e-03f, -3.821899905e-03f}},
            {{-3.033543485e-05f, -1.386319642e-03f, 2.820318067e-03f, -5.469396430e-03f}},
        }};
        const auto tableValue = [drivenInputDb](const auto& positions,
                                                const auto& values) noexcept {
            if (drivenInputDb <= positions.front()) return values.front();
            for (size_t i = 1; i < positions.size(); ++i)
                if (drivenInputDb <= positions[i])
                {
                    const float fraction = (drivenInputDb - positions[i - 1])
                        / (positions[i] - positions[i - 1]);
                    std::array<float, 4> result{};
                    for (size_t basis = 0; basis < result.size(); ++basis)
                        result[basis] = values[i - 1][basis]
                            + fraction * (values[i][basis]
                                - values[i - 1][basis]);
                    return result;
                }
            return values.back();
        };
        const float rateBlend = std::clamp(
            static_cast<float>((hostSampleRate - 48000.0) / 48000.0),
            0.0f, 1.0f);
        const auto rateValue = [rateBlend](const auto& low,
                                           const auto& high) noexcept {
            std::array<float, 4> result{};
            for (size_t basis = 0; basis < result.size(); ++basis)
                result[basis] = low[basis]
                    + rateBlend * (high[basis] - low[basis]);
            return result;
        };
        const auto halfEight = rateValue(
            tableValue(halfDriveDb, half48),
            tableValue(halfDriveDb, half96));
        const auto campaignEight = rateValue(
            tableValue(campaignEightDriveDb, campaignEight48),
            tableValue(campaignEightDriveDb, campaignEight96));
        const auto campaignTwo = rateValue(
            tableValue(campaignTwoDriveDb, campaignTwo48),
            tableValue(campaignTwoDriveDb, campaignTwo96));
        const float releaseBlend = std::clamp(
            (releasePosition - 0.5f) / (0.66595459f - 0.5f),
            0.0f, 1.0f);
        const float eightWeight = std::clamp(
            1.0f - std::abs(inputPosition - 0.8f) / 0.1f, 0.0f, 1.0f);
        const float twoWeight = std::clamp(
            1.0f - std::abs(inputPosition - 0.2f) / 0.1f, 0.0f, 1.0f);
        std::array<float, 4> result{};
        for (size_t basis = 0; basis < result.size(); ++basis)
        {
            const float eight = halfEight[basis]
                + releaseBlend * (campaignEight[basis] - halfEight[basis]);
            result[basis] = eightWeight * eight
                + twoWeight * releaseBlend * campaignTwo[basis];
        }
        return result;
    }

    static float fetLowFrequencyK5(float gainReductionDb) noexcept
    {
        // The low-frequency fifth-order transformer contribution also changes
        // with reduction. The former constant 0.00255 coefficient leaves the
        // installed unit's 100 Hz H5 up to 6.35 dB away.
        //
        // COORDINATE. These anchors use the same per-sample internal-GR
        // coordinate as the other FET harmonic tables. Zero, half, and full
        // low-T5 builds reproduce their complex H5 midpoint to 0.000280 dB,
        // establishing a scalable source. The constant-coefficient fit uses
        // every scoreable 100 Hz H5 row and retains anchors until piecewise
        // interpolation is within 0.10 dB. Production evaluates the table per
        // sample, however, and detector ripple traverses the steep onset
        // segment: recalibrating only the 0.8882 dB anchor from 0.001205130 to
        // 0.001150000 changes its measured error from +0.259219 to -0.079061
        // dB. The guard at 0.7234 dB keeps the already fitted 100 Hz H3
        // residual below its declared bound near compression onset.
        //
        // The finite 1 kHz leakage is deliberately not hidden in this table;
        // it belongs to the separately measured broadband fifth-order path.
        constexpr std::array<float, 18> reductionDb{{
            0.0f, 0.7234f, 0.8882f, 0.9345f, 1.1328f, 1.3622f, 1.7057f,
            2.1781f, 2.5753f, 3.1896f, 3.9931f, 6.9247f, 9.8790f,
            12.0660f, 13.8515f, 24.6525f, 34.3368f, 40.0f}};
        constexpr std::array<float, 18> coefficients{{
            0.002167500f, 0.002167500f, 0.001150000f, 0.001378785f,
            0.001957890f, 0.002406180f, 0.002787915f, 0.003019200f,
            0.002900880f, 0.002935050f, 0.002883795f, 0.002550765f,
            0.002316930f, 0.002024700f, 0.001853085f, 0.002283525f,
            0.002515575f, 0.002515575f}};
        gainReductionDb = std::clamp(gainReductionDb, 0.0f, 40.0f);
        for (size_t i = 1; i < reductionDb.size(); ++i)
            if (gainReductionDb <= reductionDb[i])
            {
                const float fraction = (gainReductionDb - reductionDb[i - 1])
                    / (reductionDb[i] - reductionDb[i - 1]);
                return coefficients[i - 1]
                    + fraction * (coefficients[i] - coefficients[i - 1]);
            }
        return coefficients.back();
    }

    static float fetBroadbandK5(float gainReductionDb) noexcept
    {
        // The remaining 1 kHz fifth harmonic is a distinct, high-passed
        // contribution. An unfiltered T5 source is measurably the wrong
        // mechanism: at half strength it improves 1 kHz but worsens the
        // already fitted 100 Hz H5 residual from 0.079061 to 0.717420 dB.
        // Two 250 Hz high-pass poles reduce that leakage while preserving the
        // mid-band contribution.
        //
        // COORDINATE. These anchors use the same per-sample internal-GR
        // coordinate as the other FET harmonic tables. Zero, half, and full
        // high-passed T5 controls reproduce their complex midpoint to
        // 0.000357 dB for H3 and 0.000165 dB for H5. The fit uses all 32
        // scoreable 1 kHz H5 rows and removes anchors until the predicted
        // piecewise-interpolation error reaches 0.10 dB. Its independent
        // guards predict 0.107523 dB worst at 100 Hz H5, 0.099039 dB at
        // 1 kHz H3, and 0.728298 dB at 100 Hz H3.
        constexpr std::array<float, 8> reductionDb{{
            0.0f, 2.6305f, 3.2496f, 4.5656f, 6.9921f, 13.9253f,
            33.4397f, 40.0f}};
        constexpr std::array<float, 8> coefficients{{
            -0.000098400f, -0.000098400f, -0.000104240f,
            -0.000101680f, -0.000086860f, -0.000062020f,
            -0.000093440f, -0.000093440f}};
        gainReductionDb = std::clamp(gainReductionDb, 0.0f, 40.0f);
        for (size_t i = 1; i < reductionDb.size(); ++i)
            if (gainReductionDb <= reductionDb[i])
            {
                const float fraction = (gainReductionDb - reductionDb[i - 1])
                    / (reductionDb[i] - reductionDb[i - 1]);
                return coefficients[i - 1]
                    + fraction * (coefficients[i] - coefficients[i - 1]);
            }
        return coefficients.back();
    }

    static float fetLowFrequencyK2(float gainReductionDb) noexcept
    {
        // The vintage unit's second even-order path: the low-frequency
        // transformer asymmetry, generated at the call site from a 250 Hz
        // two-pole copy of the saturated signal. This is its coefficient.
        //
        // COORDINATE. Same as `fetBroadbandK2` above and for the same reason:
        // the argument is the call site's own `-gainToDecibels(envelope +
        // 0.001f)`, per sample, and every anchor here was measured in exactly
        // that coordinate by `MultiCompCoreTest --fet-h2-surface`.
        //
        // MEASUREMENT. 91 compressing operating points at 100 Hz, Input
        // 0.2-1.0 x source -48 to -6 dBFS (`probe_h2_surface.py`, campaign
        // reference_comparison_1176). At 100 Hz mine's 2f output is the sum of
        // TWO contributions, this one and `fetBroadbandK2`, and they arrive
        // nearly in QUADRATURE -- the two-pole low pass turns -21.8 deg per
        // pole at 100 Hz and squaring doubles it, measured -78 to -86 deg
        // across the axis. So the coefficient each row asks for is the positive
        // root of |B + v C| = target, not a ratio of magnitudes; B and C are
        // the broadband and low-frequency complex second harmonics, read
        // separately by rebuilding with the other coefficient zeroed.
        // Superposition was verified rather than assumed: a build at half this
        // coefficient reproduces B + 0.5 C over 198 operating points to
        // 2.7e-05 dB.
        //
        // The result is a clean single-valued function of the reduction.
        // Triplets that reach the same depth from three different (Input,
        // source) combinations agree to 0.07-0.35 %: 9.88/9.89/9.94 dB ask
        // 0.030548/0.030563/0.030574, and 14.83/14.84/14.88 dB ask
        // 0.032141/0.032165/0.032153. That last triplet is the point. The law
        // this replaced was `0.0400 * clamp(grDb / 12) * saturation`, whose
        // charge term SATURATES at 12 dB, so every row above it got the same
        // coefficient; the reference keeps asking for 3.7 % more between 12.6
        // and 18.8 dB, and the campaign read that as a mysterious second
        // GR-dependent term the charge did not carry. It is not a second term.
        // It is the reduction axis the clamp threw away.
        //
        // Every segment holds at least one measured row INSIDE it, not only at
        // its edges -- the hole that let `fetBroadbandK2`'s predecessor read
        // 5.14 dB high. Above 34.35 dB nothing is measured and the last anchor
        // holds rather than extrapolating.
        //
        // Worst residual over the 91 rows: 0.189 dB, and the six worst all sit
        // below 1.4 dB of reduction. That band is NOT this table's to fix: with
        // this coefficient at zero the broadband term alone still reads
        // +0.06 to +0.14 dB high there, because `fetBroadbandK2` is fitted at
        // 1 kHz. Driving this table negative is not physical and is not the
        // answer; the floor moves when that table gains 100 Hz support.
        constexpr std::array<float, 14> reductionDb{{
            0.0f, 0.9f, 1.4f, 2.0f, 2.6f, 4.0f, 7.0f, 8.9f, 12.6f, 18.0f,
            22.0f, 28.0f, 31.0f, 40.0f}};
        constexpr std::array<float, 14> coefficients{{
            0.000000f, 0.000497f, 0.005382f, 0.009019f, 0.011509f, 0.016751f,
            0.024972f, 0.029460f, 0.032097f, 0.032621f, 0.032929f, 0.032396f,
            0.031305f, 0.031302f}};
        gainReductionDb = std::clamp(gainReductionDb, 0.0f, 40.0f);
        for (size_t i = 1; i < reductionDb.size(); ++i)
            if (gainReductionDb <= reductionDb[i])
            {
                const float fraction = (gainReductionDb - reductionDb[i - 1])
                    / (reductionDb[i] - reductionDb[i - 1]);
                return coefficients[i - 1]
                    + fraction * (coefficients[i] - coefficients[i - 1]);
            }
        return coefficients.back();
    }

    static float fetReferenceReductionDb(float inputLevelDb, int ratioIndex,
                                         float thresholdControlDb) noexcept
    {
        // The installed 1176's settled sine transfer collapses onto one input
        // axis regardless of whether the level is reached with the source or
        // Input control. A conventional quadratic knee fits that surface to
        // 0.032 dB or better for every button. Because this is a feed-forward
        // equivalent of the hardware feedback law, these are reduction slopes
        // (1 - 1/R), not the nominal faceplate ratios.
        constexpr std::array<float, 5> reductionSlopes{{
            0.82857823f, 0.90319133f, 0.93707197f, 0.96619657f, 0.95953144f}};
        constexpr std::array<float, 5> measuredThresholdsDb{{
            -8.34012099f, -5.90065303f, -4.47645544f, -2.66788769f,
            -4.74344379f}};
        constexpr std::array<float, 5> kneeWidthsDb{{
            14.12963324f, 7.19106925f, 1.87103644f, 4.83803856f,
            4.87980041f}};
        ratioIndex = std::clamp(ratioIndex, 0, 4);
        const float slope = reductionSlopes[static_cast<size_t>(ratioIndex)];
        // -10 dB is the reference-compatible extension setting. Moving the
        // extension shifts the measured transfer without changing its shape.
        const float threshold = measuredThresholdsDb[static_cast<size_t>(ratioIndex)]
            + thresholdControlDb + 10.0f;
        const float width = kneeWidthsDb[static_cast<size_t>(ratioIndex)];
        const float kneeStart = threshold - width * 0.5f;
        if (inputLevelDb <= kneeStart) return 0.0f;
        // At the clockwise Input stop the installed unit stops adding reduction
        // near the top of every ratio's detector domain. Dense 4:1 measurements
        // resolve output slopes of 0.98--1.00 after the bend; ten-row sibling
        // controls find the same plateau at progressively higher levels for
        // 8:1, 12:1, and 20:1. All-buttons reaches it at the 4:1 coordinate.
        // Holding the detector coordinate, rather than the computed reduction,
        // leaves the knee and every previously fitted row below +38.7 dB intact.
        constexpr std::array<float, 5> maximumDetectorLevelsDb{{
            38.72f, 38.85f, 39.00f, 39.09f, 38.23f}};
        inputLevelDb = std::min(
            inputLevelDb,
            maximumDetectorLevelsDb[static_cast<size_t>(ratioIndex)]);
        if (inputLevelDb >= threshold + width * 0.5f)
            return slope * (inputLevelDb - threshold);
        const float position = inputLevelDb - kneeStart;
        return slope * position * position / (2.0f * width);
    }

    static float fetReferenceKneeOnsetDb(float inputLevelDb,
                                         float thresholdControlDb) noexcept
    {
        // The earlier five-point static grid did not resolve the bottom half
        // of the 4:1 knee. Independent 100 Hz and 1 kHz quarter-dB sweeps do:
        // the installed unit remains below 0.003 dB reduction through -10.75
        // dB driven level, then joins the established static law. The two
        // reference sweeps collapse within 0.00564 dB, so this is a common
        // low-level cell rather than the deeper LF detector-response surface.
        // Values are the 1 kHz reference reduction; the 100 Hz sweep is the
        // held-out frequency and is gated separately in the core test.
        constexpr std::array<float, 33> reductionDb{{
            0.001054679f, 0.001119700f, 0.001188390f, 0.001261232f,
            0.001338008f, 0.001419694f, 0.001506451f, 0.001597734f,
            0.001694829f, 0.001797734f, 0.001906877f, 0.002021984f,
            0.002144161f, 0.002273709f, 0.010022404f, 0.055453550f,
            0.129645571f, 0.224473074f, 0.335152209f, 0.456996590f,
            0.589745641f, 0.724483669f, 0.866982937f, 1.015019298f,
            1.167031527f, 1.323735118f, 1.482176304f, 1.643956780f,
            1.808799028f, 1.968619347f, 2.138448954f, 2.309558868f,
            2.483170986f,
        }};
        // Move the table with the existing Threshold extension control. At
        // its reference-compatible -10 dB setting this offset is exactly zero.
        const float thresholdShift = thresholdControlDb + 10.0f;
        const float level = inputLevelDb - thresholdShift;
        constexpr float firstLevelDb = -14.0f;
        constexpr float lastLevelDb = -6.0f;
        constexpr float referenceKneeFloorDb = -15.404937f;
        if (level <= referenceKneeFloorDb) return 0.0f;
        if (level < firstLevelDb)
            return reductionDb.front()
                * (level - referenceKneeFloorDb)
                / (firstLevelDb - referenceKneeFloorDb);
        if (level <= lastLevelDb)
        {
            const float tablePosition = (level - firstLevelDb) * 4.0f;
            const size_t low = static_cast<size_t>(std::min(
                static_cast<int>(reductionDb.size()) - 2,
                std::max(0, static_cast<int>(std::floor(tablePosition)))));
            const float fraction = tablePosition - static_cast<float>(low);
            return reductionDb[low]
                + fraction * (reductionDb[low + 1] - reductionDb[low]);
        }
        // The measured onset is almost tangent to the established curve by
        // -4 dB. Join there and leave every deeper static/maximum-GR anchor on
        // the accepted law rather than extrapolating the new table.
        constexpr float joinLevelDb = -4.0f;
        const float shiftedJoinLevelDb = joinLevelDb + thresholdShift;
        const float joinedReduction = fetReferenceReductionDb(
            shiftedJoinLevelDb, 0, thresholdControlDb);
        if (level < joinLevelDb)
        {
            const float fraction = (level - lastLevelDb)
                / (joinLevelDb - lastLevelDb);
            return reductionDb.back()
                + fraction * (joinedReduction - reductionDb.back());
        }
        return fetReferenceReductionDb(inputLevelDb, 0, thresholdControlDb);
    }

    static float fetAllProcessorCorrectionDb(float inputLevelDb) noexcept
    {
        // All-buttons has a separate 1.20 dB small-signal gain and a curved
        // transition into limiting. This correction was measured after the
        // common analog stages, on a dense 997 Hz grid, so the target below
        // accounts for their level-dependent residual instead of applying a
        // second output trim. Positions are in `detectorLevelDb` and are
        // consumed as passed; the recurring .145 fraction in them is inherited
        // from the grid the original measurement was laid out on and is NOT an
        // offset to be applied to or removed from the argument (see the note on
        // the coordinate below). Intermediate points are linearly interpolated.
        //
        // The first FIVE entries are the small-signal plateau: below the
        // All-buttons knee no reduction is generated, so -(entry) IS the
        // small-signal gain and it must be the reference's measured 1.20 dB.
        // The flat +0.15 dB that used to be added to this function's result at
        // the call site is a fit constant belonging to the compressing region
        // only, and is now carried by the entries from -6.145 dB upward --
        // where it was fitted -- instead of by every entry. Applied to the
        // plateau it made the small-signal gain 1.048 dB. That was invisible
        // while the chain carried the +0.149329 dB excess removed in
        // fetInputGainDb, which very nearly cancelled it; the scalar-immune
        // measurement is the All-buttons-minus-4:1 output difference at Input
        // 0.4 / -36 dBFS, where both laws are below their knees: reference
        // +1.19992 dB, this plugin +1.04601 dB before the change.
        //
        // -7.224 dB ends the plateau. The 2 dB sweep the table was originally
        // fitted on samples this transition at exactly the knot spacing, so it
        // could not see the segment interiors; a 0.5 dB sweep of the same
        // operating point (Input 0.4, All buttons, Modern curve) shows the
        // reference producing NO reduction at all up to -7.5 dB internal --
        // consecutive 0.5 dB steps come back +0.499629 / +0.499584 / +0.499533
        // / +0.499476 / +0.499412 dB -- and then knee-ing hard. A single
        // straight segment from -8.145 to -6.145 starts compressing 0.9 dB too
        // early and read -0.206 / -0.334 / -0.272 dB against the reference at
        // -7.5 / -7.0 / -6.5. Extending the plateau and re-fitting the -6.145
        // entry against that sweep leaves at worst 0.022 dB across the whole
        // transition.
        //
        // The knot's value came out of least squares, but the best-determined
        // thing at that abscissa is this plugin's OWN slope break: with the
        // campaign's pinned -10 dB threshold `fetReferenceReductionDb` starts
        // generating All-buttons reduction at -4.74344 - 10 + 10 - 4.87980/2 =
        // -7.18334 dB, which the fit landed 0.041 dB from. The reference's own
        // knee onset is only bounded to (-7.65, -7.15) by the 0.5 dB grid, so
        // it does not pin the knot nearly as well; do not re-derive this
        // position from "where the reference knees" alone.
        //
        // The argument is `detectorLevelDb` as passed, with no offset. A
        // reference-free inversion pins that: the only difference between the
        // two builds either side of this re-fit is this table, so the measured
        // output difference over the 0.5 dB grid IS the difference of the two
        // tables sampled at the consumption argument, and solving the one
        // unknown gives detectorLevelDb - 0.008 (+/-0.03). An earlier note here
        // claimed a +0.130 dB effective offset from detector ripple; that was
        // the Wave 5 -0.149329 dB input-gain correction subtracted twice in the
        // analysis coordinate, not a property of the chain. It cost the fit
        // 0.009 dB, which is far inside what the grid resolves, but do not
        // carry that offset into a fit of the segments above.
        constexpr std::array<float, 23> positions{{
            -30.145f, -20.145f, -12.145f, -8.145f, -7.224f, -6.145f,
            -4.145f, -2.145f, -0.145f, 1.855f, 2.608f, 3.855f,
            7.149f, 13.064f, 14.608f, 15.855f, 19.234f, 25.234f,
            26.608f, 27.855f, 32.608f, 33.855f, 40.0f}};
        constexpr std::array<float, 23> corrections{{
            -1.20005f, -1.19849f, -1.19245f, -1.18313f, -1.18313f,
            -0.55515f, 0.48398f, 0.67817f, 0.57889f, 0.51182f,
            0.49441f, 0.46731f, 0.41443f, 0.42950f, 0.46340f,
            0.44900f, 0.48120f, 0.51680f, 0.51190f, 0.50250f,
            0.54200f, 0.54070f, 0.54070f}};
        if (inputLevelDb <= positions.front()) return corrections.front();
        for (size_t i = 1; i < positions.size(); ++i)
            if (inputLevelDb <= positions[i])
            {
                const float fraction = (inputLevelDb - positions[i - 1])
                    / (positions[i] - positions[i - 1]);
                return corrections[i - 1]
                    + fraction * (corrections[i] - corrections[i - 1]);
            }
        return corrections.back();
    }

    static float fetAllOverloadMemoryDb(float baseReductionDb) noexcept
    {
        // The Modern All-buttons arm carries an additional slow high-level
        // population below its maximum-reduction plateau. A 9--11.5 second
        // sweep measures +0.312 / +0.345 / +0.228 dB missing reduction at
        // nominal internal levels +34 / +37 / +38 dB, falling to zero when the
        // overload cap engages. The coordinate here is the reduction produced
        // by the existing static law at the call site, before this correction.
        constexpr std::array<float, 5> reductionDb{{
            36.00f, 37.65f, 40.53f, 41.49f, 41.78f}};
        constexpr std::array<float, 5> memoryDb{{
            0.000f, 0.312f, 0.345f, 0.228f, 0.000f}};
        if (baseReductionDb <= reductionDb.front()) return memoryDb.front();
        for (size_t i = 1; i < reductionDb.size(); ++i)
            if (baseReductionDb <= reductionDb[i])
            {
                const float fraction = (baseReductionDb - reductionDb[i - 1])
                    / (reductionDb[i] - reductionDb[i - 1]);
                return memoryDb[i - 1]
                    + fraction * (memoryDb[i] - memoryDb[i - 1]);
            }
        return memoryDb.back();
    }

    static float fetAttackPosition(float legacyMilliseconds) noexcept
    {
        // Invert the existing descriptor's 0.3 skew to recover the host knob
        // position without changing its saved-state or automation domain.
        const float proportion = std::clamp(
            (legacyMilliseconds - 0.02f) / (80.0f - 0.02f), 0.0f, 1.0f);
        return std::pow(proportion, 0.3f);
    }

    static float fetAttackKnobScale(float attackPosition,
                                    float positiveReductionDb) noexcept
    {
        // A 4 kHz carrier resolves the installed unit's slow half of the Attack
        // knob without the 1 kHz estimator's 0.5 ms floor. The correction is
        // programme dependent: a single multiplier fixed the shallow row but
        // made the deep row slower even though it needed to move in the other
        // direction. Interpolate the independently measured shallow and deep
        // corrections, holding both ends outside the measured 14--34 dB span.
        // The midpoint and upper half retain the established drive-axis law.
        const float depth = std::clamp(
            (positiveReductionDb - 14.0f) / 20.0f, 0.0f, 1.0f);
        const float atSlowStop = 1.45f - 0.20f * depth;
        const float atQuarter = 1.25f - 0.25f * depth;
        if (attackPosition <= 0.25f)
            return atSlowStop
                + 4.0f * attackPosition * (atQuarter - atSlowStop);
        if (attackPosition < 0.50f)
            return atQuarter
                + 4.0f * (attackPosition - 0.25f) * (1.0f - atQuarter);
        return 1.0f;
    }

    static float fetReleasePosition(float legacyMilliseconds) noexcept
    {
        return std::clamp(
            (legacyMilliseconds - 50.0f) / (1100.0f - 50.0f), 0.0f, 1.0f);
    }

    static float fetReleaseExposureScale(float exposureSeconds,
                                         float maximumGrDb) noexcept
    {
        constexpr std::array<float, 4> exposures{{0.01f, 0.10f, 1.0f, 5.0f}};
        constexpr std::array<float, 4> lowDrive{{0.42f, 0.46f, 0.65f, 0.80f}};
        constexpr std::array<float, 4> midDrive{{0.51f, 0.56f, 0.82f, 1.00f}};
        constexpr std::array<float, 4> highDrive{{0.10f, 0.70f, 1.00f, 1.24f}};
        const auto interpolateExposure = [&exposures, exposureSeconds](
            const std::array<float, 4>& values) noexcept {
            if (exposureSeconds <= exposures.front()) return values.front();
            for (size_t i = 1; i < exposures.size(); ++i)
                if (exposureSeconds <= exposures[i])
                {
                    const float lowLog = std::log(exposures[i - 1]);
                    const float highLog = std::log(exposures[i]);
                    const float fraction = (std::log(exposureSeconds) - lowLog)
                        / (highLog - lowLog);
                    return values[i - 1]
                        + fraction * (values[i] - values[i - 1]);
                }
            return values.back();
        };
        const float low = interpolateExposure(lowDrive);
        const float mid = interpolateExposure(midDrive);
        const float high = interpolateExposure(highDrive);
        const float drive = std::clamp((maximumGrDb - 14.0f) / 20.0f,
                                       0.0f, 1.0f);
        return drive <= 0.5f
            ? low + drive * 2.0f * (mid - low)
            : mid + (drive - 0.5f) * 2.0f * (high - mid);
    }

    static float fetPostBurstRecoveryAssistDb(
        float elapsedSeconds, float maximumGrDb, int ratioIndex,
        float terminalWeight, float attackPosition, float releasePosition,
        float hostSampleRate) noexcept
    {
        // The installed cell sheds a finite terminal-charge population after
        // programme support disappears. It is separate from the ordinary
        // asymptotic release: the candidate/UAD difference is 1.1--3.4 dB in
        // the first 250 ms recovery window and has crossed zero by 3.25 s.
        // Maximum reduction owns the population size/shape; the final active
        // detector sample supplies the measured phase coordinate that a
        // phase-blind release scalar could not reproduce.
        const float drive = std::clamp(
            (maximumGrDb - 14.0f) / 20.0f, 0.0f, 1.0f);
        if (maximumGrDb < 8.0f || elapsedSeconds < 0.0f) return 0.0f;
        const float lowerDrive = std::min(drive * 2.0f, 1.0f);
        const float upperDrive = std::max(drive * 2.0f - 1.0f, 0.0f);
        float amplitudeDb = 1.93f
            + lowerDrive * (2.85f - 1.93f)
            + upperDrive * (3.38f - 2.85f);
        float exponent = 4.00f
            + lowerDrive * (2.87f - 4.00f)
            + upperDrive * (1.50f - 2.87f);
        const float rate = std::clamp(
            (hostSampleRate - 48000.0f) / 48000.0f, 0.0f, 1.0f);
        const float phase = std::clamp(terminalWeight, 0.0f, 1.0f);
        amplitudeDb += phase * drive * (0.69f + 0.13f * rate);
        exponent += phase * drive * (0.25f - 0.07f * rate);
        amplitudeDb += 0.20f * drive
            * (1.0f - std::clamp(attackPosition, 0.0f, 1.0f));
        if (ratioIndex == 1)
        {
            amplitudeDb *= 1.15f;
            exponent += 0.70f;
        }
        else if (ratioIndex != 0)
            return 0.0f;
        const float nominalReleaseSeconds
            = 2.00f - 1.82f * releasePosition
                + 0.12f * releasePosition * releasePosition;
        const float durationSeconds = 3.0f
            * nominalReleaseSeconds / 1.12f;
        const float position = 1.0f - elapsedSeconds
            / std::max(durationSeconds, 0.05f);
        const float terminalAssist = position > 0.0f
            ? amplitudeDb * std::pow(position, exponent) : 0.0f;
        // The slow tail crosses the reference after the terminal population
        // has discharged. This small bipolar arm is independently constrained
        // by the 1.875/3.375/4.875/5.875 s windows, where a positive-only
        // assist would leave up to +0.164 dB of over-recovery.
        float tailAmplitudeDb = drive <= 0.5f
            ? 0.17f
            : 0.17f + (drive - 0.5f) * 2.0f * (0.03f - 0.17f);
        if (ratioIndex == 1) tailAmplitudeDb += 0.08f;
        const float tailSeconds = elapsedSeconds * 1.12f
            / std::max(nominalReleaseSeconds, 0.05f);
        float tailShape = 0.0f;
        if (tailSeconds > 0.375f && tailSeconds <= 1.875f)
            tailShape = (tailSeconds - 0.375f) / 1.5f * 0.40f;
        else if (tailSeconds <= 3.375f && tailSeconds > 1.875f)
            tailShape = 0.40f
                + (tailSeconds - 1.875f) / 1.5f * 0.60f;
        else if (tailSeconds <= 4.875f && tailSeconds > 3.375f)
            tailShape = 1.0f
                - (tailSeconds - 3.375f) / 1.5f * 0.70f;
        else if (tailSeconds <= 5.875f && tailSeconds > 4.875f)
            tailShape = 0.30f
                - (tailSeconds - 4.875f) * 0.20f;
        else if (tailSeconds <= 6.5f && tailSeconds > 5.875f)
            tailShape = 0.10f
                * (1.0f - (tailSeconds - 5.875f) / 0.625f);
        return terminalAssist - tailAmplitudeDb
            * std::max(tailShape, 0.0f);
    }

    static float optoThresholdDb(float peakReduction, bool limit) noexcept
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

    static float optoCurveDb(float overshootDb, bool limit) noexcept
    {
        const auto& curve = limit ? kOptoLimitCurve : kOptoCompressCurve;
        const float position = overshootDb * 0.5f;
        if (position <= 0.0f)
            return std::max(0.0f, curve[0] + position * (curve[1] - curve[0]));
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

    static std::array<float, 4> optoHarmonicRatios(
        float inputLevelDb, float compressionBlend) noexcept
    {
        // Six measured 1 kHz endpoints, stored as linear amplitude ratios
        // (10^(dBc/20)). Interpolating amplitudes makes each table endpoint
        // reproduce its measured dBc value exactly. Measurements outside the
        // -24/-12/-6 dBFS span are clamped because no extrapolation was measured.
        constexpr std::array<float, 3> levels{{-24.0f, -12.0f, -6.0f}};
        constexpr std::array<std::array<float, 4>, 3> uncompressed{{
            {{0.0005565446f, 0.0000726942f, 0.0000030832f, 0.0000058884f}},
            {{0.0021062019f, 0.0001267652f, 0.0000204644f, 0.0000541377f}},
            {{0.0033381051f, 0.0016730156f, 0.0004310226f, 0.0005787620f}}
        }};
        constexpr std::array<std::array<float, 4>, 3> compressed{{
            {{0.0023388372f, 0.0019386526f, 0.0011547820f, 0.0006974290f}},
            {{0.0032322132f, 0.0017278260f, 0.0004645153f, 0.0005217951f}},
            {{0.0021305906f, 0.0016311729f, 0.0003483373f, 0.0003384543f}}
        }};
        size_t lower = 0;
        float levelFraction = 0.0f;
        if (inputLevelDb >= levels.back())
            lower = levels.size() - 1;
        else if (inputLevelDb > levels.front())
        {
            lower = inputLevelDb < levels[1] ? 0u : 1u;
            levelFraction = (inputLevelDb - levels[lower])
                / (levels[lower + 1] - levels[lower]);
        }
        std::array<float, 4> ratios{};
        const size_t upper = std::min(lower + 1, levels.size() - 1);
        const float grFraction = std::clamp(compressionBlend, 0.0f, 1.0f);
        for (size_t harmonic = 0; harmonic < ratios.size(); ++harmonic)
        {
            const float clean = uncompressed[lower][harmonic]
                + (uncompressed[upper][harmonic] - uncompressed[lower][harmonic])
                    * levelFraction;
            const float reduced = compressed[lower][harmonic]
                + (compressed[upper][harmonic] - compressed[lower][harmonic])
                    * levelFraction;
            ratios[harmonic] = clean + (reduced - clean) * grFraction;
        }
        return ratios;
    }

    static float optoOutputStage(float input) noexcept
    {
        // The observed peak plateau is +4.72 dBFS, or 1.721868575 linear.
        // Above the fitted knee, this reciprocal approach is value/slope
        // continuous with the linear path and converges to that measured
        // plateau. The one fitted value (1.1575) uses the -24 dBFS sweep only:
        // processBlock fit RMS 0.023 dB; the held-out -12 dBFS sweep is 0.137 dB
        // RMS with a 0.230 dB worst point. The old +6.344 dB stored value is deliberately
        // absent: it described a different fit's mathematical asymptote.
        constexpr float peakCeiling = 1.721868575f;
        constexpr float linearThreshold = 1.1575f;
        const float magnitude = std::abs(input);
        if (magnitude <= linearThreshold) return input;
        constexpr float headroom = peakCeiling - linearThreshold;
        const float excess = magnitude - linearThreshold;
        const float limited = peakCeiling
            - headroom * headroom / (headroom + excess);
        return std::copysign(limited, input);
    }

    float processOpto(float input, int ch, float sidechain,
                      const MultiCompParameterState& p, bool external,
                      float optoDetector, bool useOptoDetector) noexcept
    {
        auto& d = opto[ch];
        // `gain` is the physical cell gain applied to this sample. Colour is
        // added later as a residual with no fundamental term, so the static
        // law no longer has to anticipate or invert a colour-stage level shift.
        const float appliedGain = d.gain;
        const float compressed = input * appliedGain;
        const bool limit = p.optoLimit.load(std::memory_order_relaxed);
        // The measured detector tap is pre-gain. Weight only the selected
        // detector source; the audio path above remains untouched.
        float sc = useOptoDetector ? optoDetector
                                   : (external ? sidechain : input);
        const float detectorInputAbs = std::abs(sc);
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
        const float pr = std::clamp(p.optoPeakReduction.load(std::memory_order_relaxed), 0.0f, 100.0f);
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
        const float broadbandCorrectionDb = fluctuationDb
            * (broadbandFitAtPivot + broadbandFitSlope
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
        const float cellLoaded = std::clamp(standingGrDb / 0.50f, 0.0f, 1.0f);
        const float recentEventChargeTarget = 1.0f
            - exposureSaturation * detectorSupport * cellLoaded;
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
            - 0.90f * startupAttackWeight * loadedHighDriveWeight;
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
        const float coherentSlowPopulationAttack = std::max(
            0.0f, 1.0f - (1.0f - slowPopulationAttackCoeff)
                * programmeAttackScale);
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
        const float repeatedExposureTopOff = 0.90f * repetitionBlend * std::clamp(
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
        followTarget(d.slowGrDb, slowCellTargetGrDb,
                     coherentSlowPopulationAttack, optoSlowRelease,
                     slowChargeExponent, 0.0f);
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
                + (d.fastGrDb - sustainedTopOffTarget) * blendedTopOffAttack;
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
        const float isolatedHighEventPosition = std::clamp(
            (isolatedEventLevel - 0.50f) / 0.50f, 0.0f, 1.0f);
        const float isolatedHighEventWeight = isolatedHighEventPosition
            * isolatedHighEventPosition
            * (3.0f - 2.0f * isolatedHighEventPosition);
        const float isolatedEventCapacityDb = isolatedEventDrive
            * ((10.0f + 12.4f * isolatedEventLevel
                    + 10.7f * isolatedEventLevelSquared)
                    * std::exp(-standingGrDb / 4.6f)
                + 64.0f * isolatedHighEventWeight
                    * std::exp(-standingGrDb / 2.5f));
        const float isolatedTargetSupport = std::clamp(
            targetGrDb, 0.0f, 1.0f);
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
            d.isolatedEventGrDb *= optoIsolatedEventRelease;
        const float eventExcessGrDb = std::max(
            chargedTotalGrDb - chargeTargetGrDb, 0.0f);
        if (eventExcessGrDb > 0.0f && chargedTotalGrDb > 1.0e-9f)
        {
            const float eventReleaseSeconds = 0.007f
                + (0.024f - 0.007f)
                    * std::clamp(eventExcessGrDb / 15.0f, 0.0f, 1.0f);
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
            const float drainEngagement = settledEventDrainWeight
                * programmeDrainScale * eventDrainSupport;
            const float excessFraction = eventExcessGrDb / chargedTotalGrDb;
            const float drainGain = 1.0f
                - drainEngagement * (1.0f - eventRelease) * excessFraction;
            d.fastGrDb *= drainGain;
            d.midGrDb *= drainGain;
            d.slowGrDb *= drainGain;
        }
        const float dynamicGrDb = std::max(
            0.0f, d.fastGrDb + d.midGrDb + d.slowGrDb
                + d.isolatedEventGrDb);
        const float dynamicMeasuredGain = decibelsToGain(-dynamicGrDb);
        d.gain = dynamicMeasuredGain;
        if (!std::isfinite(d.gain)) d.gain = 1.0f;
        const float makeup = optoKnobToLinearGain(
            p.optoGain.load(std::memory_order_relaxed));

        // The PR=0.7 spectrum is the measured compressed endpoint. Scale
        // toward it with physical cell reduction relative to the reduction
        // this same detector level would produce at PR=0.7. This ties colour
        // to compression, not to the knob position: no GR means no blend.
        const float referenceGrDb = optoCurveDb(
            inputLevelDb - optoThresholdDb(70.0f, limit), limit);
        const float compressionBlend = referenceGrDb > 1.0e-6f
            ? dynamicGrDb / referenceGrDb : 0.0f;

        const float inputAbs = std::abs(input);
        // Hold longer than the 1 kHz calibration period so its normalisation
        // peak is constant, while the measured 40 ms release still follows
        // genuine level drops on programme material.
        if (inputAbs >= d.colourPeak)
        {
            d.colourPeak = inputAbs;
            d.colourPeakHold = optoColourPeakHoldSamples;
        }
        else if (d.colourPeakHold > 0)
            --d.colourPeakHold;
        else
            d.colourPeak *= optoColourPeakRelease;
        const float colourPeak = std::max(d.colourPeak, 1.0e-12f);
        const float u = std::clamp(input / colourPeak, -1.0f, 1.0f);
        const float u2 = u * u;
        const float u3 = u2 * u;
        const float u4 = u2 * u2;
        const float u5 = u4 * u;
        // Chebyshev bases synthesize H2-H5 without an H1 component for a
        // settled sinusoid. Even bases omit their constant term so silence
        // produces silence; the resulting DC is removed below.
        const float bases[4] = {
            2.0f * u2,
            4.0f * u3 - 3.0f * u,
            8.0f * u4 - 8.0f * u2,
            16.0f * u5 - 20.0f * u3 + 5.0f * u
        };
        const auto ratios = optoHarmonicRatios(
            gainToDecibels(colourPeak), compressionBlend);
        float colour = 0.0f;
        for (size_t harmonic = 0; harmonic < ratios.size(); ++harmonic)
            colour += ratios[harmonic] * bases[harmonic];
        colour *= colourPeak * appliedGain * makeup;
        d.colourDc += optoColourDcSmoothing * (colour - d.colourDc);
        if (makeup == 0.0f) return 0.0f;
        const float out = compressed * makeup + colour - d.colourDc;
        return optoOutputStage(out);
    }

    float processFET(float input, int ch, float sidechain,
                     const MultiCompParameterState& p, bool studio,
                     bool external, bool linked) noexcept
    {
        auto& d = studio ? static_cast<FETState&>(studioFet[ch]) : fet[ch];
        const float sr = static_cast<float>(fs * osFactor);
        auto& inT = studio ? inputTransformerStudioFet[ch] : inputTransformerFet[ch];
        auto& outT = studio ? outputTransformerStudioFet[ch] : outputTransformerFet[ch];
        const float inputValue = p.fetInput.load(std::memory_order_relaxed);
        const float inputGain = decibelsToGain(
            studio ? inputValue : fetInputGainDb(inputValue));
        // Equivalent source-level/Input combinations have the same measured
        // vintage output spectrum. The generic transformer saturates far
        // earlier than this unit when driven by its 40 dB Input range, so the
        // calibrated vintage coloration below owns the audio stage. Its subtly
        // shaped signal remains useful to the unlinked detector. Studio FET
        // retains its established transformer-before-gain audio topology.
        const float transformedInput = inT.processSample(input, ch);
        const float gained = studio ? transformedInput * inputGain
                                    : input * inputGain;
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
            const float k2 = fetBroadbandK2(grDb);
            const float k3 = fetBroadbandK3(grDb);
            // `fetBroadbandK3` is a settled small-signal fit. On the first
            // uncompressed cycle, Input can drive this internal node tens of
            // times beyond every fitted row; extrapolating x^3 there creates
            // a false rectified crest and even reverses the 1 kHz fundamental.
            // The installed unit bounds that cell before its output stage.
            // Keep the fitted polynomial bit-identical throughout its measured
            // domain and bound only the out-of-domain source that it consumes.
            // An 8.0 source limit is the measured compromise across the 1 kHz
            // startup surface, the 0.5-20 ms attack curve, and the independent
            // 4 kHz Attack-knob surface; see the Wave 21 evidence note.
            constexpr float kFetBroadbandK3SourceLimit = 8.0f;
            const float k3Source = std::clamp(
                saturated, -kFetBroadbandK3SourceLimit,
                kFetBroadbandK3SourceLimit);
            const float sq = saturated * saturated;
            const float alpha = 1.0f / (1.0f + kDuskTwoPi * 10.0f / sr);
            const float h2 = alpha * (d.dc + sq - d.prevSq);
            d.dc = h2;
            d.prevSq = sq;
            // High-pass and normalise the broadband colour source so its
            // Chebyshev T5 term contributes fifth harmonic without folding a
            // large cubic back into the independently fitted K3 surface or
            // duplicating the low-frequency transformer term.
            const float colourHighPassCoeff = 1.0f
                - std::exp(-2.0f * kDuskPi * 250.0f / sr);
            d.colourHighPassLow += colourHighPassCoeff
                * (saturated - d.colourHighPassLow);
            float colourHighPassed = saturated - d.colourHighPassLow;
            d.colourHighPassLower += colourHighPassCoeff
                * (colourHighPassed - d.colourHighPassLower);
            colourHighPassed -= d.colourHighPassLower;
            const float colourPeakRelease = std::exp(-1.0f / (0.1f * sr));
            const float colourHighPassedAbs = std::abs(colourHighPassed);
            d.colourPeak = colourHighPassedAbs >= d.colourPeak
                ? colourHighPassedAbs
                : colourHighPassedAbs + (d.colourPeak - colourHighPassedAbs)
                    * colourPeakRelease;
            const float colourNormalised = std::clamp(
                colourHighPassed / std::max(d.colourPeak, 1.0e-9f),
                -1.0f, 1.0f);
            const float colour2 = colourNormalised * colourNormalised;
            const float colour3 = colour2 * colourNormalised;
            const float colour5 = colour2 * colour3;
            const float broadbandHarmonicDrive = std::clamp(
                grDb / 14.0f, 0.0f, 1.0f);
            const float broadbandH5 = d.colourPeak * broadbandHarmonicDrive
                * fetBroadbandK5(grDb)
                * (16.0f * colour5 - 20.0f * colour3
                    + 5.0f * colourNormalised);
            // The hardware's low-frequency transformer asymmetry grows with
            // gain reduction. Generate that even harmonic from a 250 Hz
            // two-pole low-passed copy; `fetLowFrequencyK2` above is its
            // measured coefficient.
            //
            // The two poles put this path 49 dB down at 1 kHz once squared,
            // which is NOT negligible against `fetBroadbandK2` and must not be
            // treated as if it were: measured by rebuilding with the
            // coefficient zeroed, the term moves the 1 kHz second harmonic by
            // up to 0.175 dB (Input 0.6, -24 dBFS). Any change here has to be
            // scored on the 1 kHz surface as well as the 100 Hz one -- the
            // Wave 9 fit carries the 39-row 1 kHz set as a constraint for
            // exactly this reason, and lands it at 0.2335 dB worst against the
            // 0.2625 dB it inherited.
            const float colourLowCoeff = 1.0f
                - std::exp(-2.0f * kDuskPi * 250.0f / sr);
            d.colourLow += colourLowCoeff * (saturated - d.colourLow);
            d.colourLower += colourLowCoeff * (d.colourLow - d.colourLower);
            const float colourLowSq = d.colourLower * d.colourLower;
            const float colourLowH2 = alpha
                * (d.colourLowDc + colourLowSq - d.colourLowPrevSq);
            d.colourLowDc = colourLowH2;
            d.colourLowPrevSq = colourLowSq;
            const float lowFrequencyK2 = fetLowFrequencyK2(grDb);
            const float colourLowPeakRelease = std::exp(-1.0f / (0.1f * sr));
            const float colourLowerAbs = std::abs(d.colourLower);
            d.colourLowPeak = colourLowerAbs >= d.colourLowPeak
                ? colourLowerAbs
                : colourLowerAbs
                    + (d.colourLowPeak - colourLowerAbs)
                        * colourLowPeakRelease;
            const float lowNormalised = std::clamp(
                d.colourLower / std::max(d.colourLowPeak, 1.0e-9f),
                -1.0f, 1.0f);
            const float low2 = lowNormalised * lowNormalised;
            const float low3 = low2 * lowNormalised;
            const float low4 = low2 * low2;
            const float low5 = low4 * lowNormalised;
            const float lowHarmonicDrive = std::clamp(grDb / 14.0f, 0.0f, 1.0f);
            const float lowFrequencyK3 = fetLowFrequencyK3(grDb);
            const float lowFrequencyK5 = fetLowFrequencyK5(grDb);
            const float lowT3 = 4.0f * low3 - 3.0f * lowNormalised;
            const float shallowNormalised = std::clamp(
                gained / std::max(d.kneePeak, 1.0e-9f), -1.0f, 1.0f);
            const float shallowNormalised2
                = shallowNormalised * shallowNormalised;
            const float shallowNormalised3
                = shallowNormalised2 * shallowNormalised;
            const float shallowT3 = 4.0f * shallowNormalised2
                * shallowNormalised - 3.0f * shallowNormalised;
            const float shallowT5 = 16.0f * shallowNormalised3
                    * shallowNormalised2
                - 20.0f * shallowNormalised3
                + 5.0f * shallowNormalised;
            const float lowT3LagCoeff = 1.0f
                - std::exp(-2.0f * kDuskPi * 300.0f / sr);
            d.shallowT3Lag += lowT3LagCoeff
                * (shallowT3 - d.shallowT3Lag);
            d.shallowT3Lower += lowT3LagCoeff
                * (d.shallowT3Lag - d.shallowT3Lower);
            d.shallowT3Lowest += lowT3LagCoeff
                * (d.shallowT3Lower - d.shallowT3Lowest);
            d.shallowT5Lag += lowT3LagCoeff
                * (shallowT5 - d.shallowT5Lag);
            d.shallowT5Lower += lowT3LagCoeff
                * (d.shallowT5Lag - d.shallowT5Lower);
            d.shallowT5Lowest += lowT3LagCoeff
                * (d.shallowT5Lower - d.shallowT5Lowest);
            const float shallowDriveDb = gainToDecibels(
                std::max(d.kneePeak, 1.0e-12f));
            const std::array<float, 4> shallowOddCorrection
                = ratioIndex == 0
                    ? fetShallowKneeOddCorrection(shallowDriveDb)
                    : std::array<float, 4>{{0.0f, 0.0f, 0.0f, 0.0f}};
            const float inputPosition = std::clamp(
                (inputValue + 20.0f) / 60.0f, 0.0f, 1.0f);
            const float releasePosition = fetReleasePosition(
                p.fetRelease.load(std::memory_order_relaxed));
            const std::array<float, 4> broadbandComplexH3Correction
                = ratioIndex == 0
                    ? fetBroadbandComplexH3Correction(
                        inputPosition, releasePosition, shallowDriveDb, fs)
                    : std::array<float, 4>{{0.0f, 0.0f, 0.0f, 0.0f}};
            const std::array<float, 4> broadbandComplexH5Correction
                = ratioIndex == 0
                    ? fetBroadbandComplexH5Correction(
                        inputPosition, releasePosition, shallowDriveDb, fs)
                    : std::array<float, 4>{{0.0f, 0.0f, 0.0f, 0.0f}};
            float lowHarmonics = d.colourLowPeak * lowHarmonicDrive
                * (lowFrequencyK3 * lowT3
                    + 0.0037f * (8.0f * low4 - 8.0f * low2)
                    + lowFrequencyK5 * (16.0f * low5 - 20.0f * low3
                        + 5.0f * lowNormalised));
            lowHarmonics += d.colourLowPeak
                * (shallowOddCorrection[0] * d.shallowT3Lag
                    + shallowOddCorrection[1] * d.shallowT3Lower
                    + shallowOddCorrection[2] * d.shallowT5Lag
                    + shallowOddCorrection[3] * d.shallowT5Lower);
            lowHarmonics += d.kneePeak
                * (broadbandComplexH3Correction[0] * shallowT3
                    + broadbandComplexH3Correction[1] * d.shallowT3Lag
                    + broadbandComplexH3Correction[2] * d.shallowT3Lower
                    + broadbandComplexH3Correction[3] * d.shallowT3Lowest);
            lowHarmonics += d.kneePeak
                * (broadbandComplexH5Correction[0] * shallowT5
                    + broadbandComplexH5Correction[1] * d.shallowT5Lag
                    + broadbandComplexH5Correction[2] * d.shallowT5Lower
                    + broadbandComplexH5Correction[3] * d.shallowT5Lowest);
            d.colourLowHarmonicDc += (1.0f - alpha)
                * (lowHarmonics - d.colourLowHarmonicDc);
            lowHarmonics -= d.colourLowHarmonicDc;
            saturated += k2 * h2 + lowFrequencyK2 * colourLowH2
                + k3 * k3Source * k3Source * k3Source
                + broadbandH5 + lowHarmonics;
        }

        // FET junction capacitance choke in the feedback path.  Studio FET
        // uses the unsaturated compressed signal here, matching its JUCE class.
        const float feedbackInput = studio ? compressed : saturated;
        const float grDb = -gainToDecibels(d.envelope + 0.001f);
        const float grNorm = std::clamp(grDb / 20.0f, 0.0f, 1.0f);
        const float corner = 20000.0f - grNorm * 2000.0f;
        const float chokeCoeff = 1.0f - std::exp(-2.0f * kDuskPi * corner / sr);
        d.hf += chokeCoeff * (feedbackInput - d.hf);
        float detect = 0.0f;
        float kneeDetect = 0.0f;
        float vintageInstantLevel = 0.0f;
        bool vintageProgrammeActive = false;
        bool vintageProgrammeSupported = false;
        if (!studio)
        {
            // The static reference law above is expressed on the signal driven
            // by Input, before gain reduction. Its feedback topology has the
            // same settled transfer, but using the already-reduced signal with
            // a feed-forward slope produces a second, incorrect ratio. The
            // fast peak detector supplies a stable level to the timing stage.
            // The ordinary internal path follows the oversampled audio and its
            // measured reconstruction latency. Only an external detector or
            // an active vintage stereo link consumes the sidechain prepared by
            // MultiCompDSP; using it unconditionally erased the first-cycle
            // overshoot by giving the detector unintended lookahead.
            const float instantLevel = std::abs(
                (external || linked) ? sidechain * inputGain
                                     : transformedInput * inputGain);
            vintageInstantLevel = instantLevel;
            vintageProgrammeActive = instantLevel > 0.03f;
            const float peakReleaseCoeff = std::exp(-1.0f / (0.040f * sr));
            if (instantLevel > d.peak)
                d.peak = instantLevel;
            else
                d.peak += (1.0f - peakReleaseCoeff) * (instantLevel - d.peak);
            detect = d.peak;
            const float kneeInstantLevel = std::abs(
                (external || linked) ? sidechain * inputGain
                                     : input * inputGain);
            const float kneePeakRelease = std::exp(
                -1.0f / (0.250f * sr));
            constexpr float kneeHoldSeconds = 0.030f;
            const float sampleSeconds = 1.0f / sr;
            const auto advanceHeldPeak = [kneePeakRelease, kneeHoldSeconds,
                                          sampleSeconds](
                float inputLevel, float& peak,
                float& holdSeconds) noexcept {
                if (inputLevel >= peak)
                {
                    peak = inputLevel;
                    holdSeconds = kneeHoldSeconds;
                }
                else if (holdSeconds > 0.0f)
                    holdSeconds = std::max(0.0f, holdSeconds - sampleSeconds);
                else
                    peak = inputLevel
                        + (peak - inputLevel) * kneePeakRelease;
            };
            advanceHeldPeak(kneeInstantLevel, d.kneePeak,
                            d.kneePeakHoldSeconds);
            kneeDetect = d.kneePeak;
        }
        else if (external)
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
        const float thresholdControlDb = studio
            ? -10.0f : p.fetThreshold.load(std::memory_order_relaxed);
        const float threshold = decibelsToGain(thresholdControlDb);
        float reduction = 0.0f;
        float kneeTargetReduction = 0.0f;
        float kneeHandoff = 1.0f;
        if (!studio)
        {
            const float detectorLevelDb = gainToDecibels(
                std::max(detect, 1.0e-12f));
            const bool measuredExtension
                = p.fetCurve.load(std::memory_order_relaxed) != 0;
            if (ratioIndex == 4 && measuredExtension)
            {
                const float abiThreshold = threshold * 0.5f;
                if (detect > abiThreshold)
                    reduction = lookupTables.getAllButtonsReduction(
                        gainToDecibels(detect / abiThreshold), true);
            }
            else
            {
                if (ratioIndex == 0)
                {
                    // The installed unit's shallow knee is frequency-flat even
                    // though its deeper feedback detector is not. A second,
                    // raw-input peak cell supplies only this onset interval;
                    // it hands back to the transformed detector before the
                    // accepted LF response and harmonic anchors. Its release
                    // spans many 100 Hz half-cycles, preventing rectifier ripple
                    // from turning the common static curve into a frequency law.
                    const float kneeLevelDb = gainToDecibels(
                        std::max(kneeDetect, 1.0e-12f));
                    const float onsetReduction = fetReferenceKneeOnsetDb(
                        kneeLevelDb, thresholdControlDb);
                    const float normalisedKneeLevel = kneeLevelDb
                        - (thresholdControlDb + 10.0f);
                    // The onset table itself rejoins the accepted detector law
                    // at -4 dB. Fade this auxiliary cell out over the preceding
                    // decibel; carrying it above that join adds a second,
                    // 100 ms trajectory to the already-matched -42 dBFS attack.
                    const float handoff = std::clamp(
                        (normalisedKneeLevel + 5.0f) / 1.0f,
                        0.0f, 1.0f);
                    const float smoothHandoff = handoff * handoff
                        * (3.0f - 2.0f * handoff);
                    kneeTargetReduction = onsetReduction;
                    kneeHandoff = smoothHandoff;
                    // Keep the established detector/envelope trajectory as the
                    // coloration coordinate. The shallow auxiliary gain cell
                    // below supplies only the difference to the measured knee,
                    // so every harmonic ratio remains on its fitted axis.
                    reduction = fetReferenceReductionDb(
                        detectorLevelDb, ratioIndex, thresholdControlDb);
                }
                else
                    reduction = fetReferenceReductionDb(
                        detectorLevelDb, ratioIndex, thresholdControlDb);
                if (ratioIndex == 4)
                    reduction += fetAllProcessorCorrectionDb(detectorLevelDb);
            }
            if (p.fetTransient.load(std::memory_order_relaxed) > 0.01f)
                reduction /= std::max(1.0f, transientShaper.process(
                    input, ch, p.fetTransient.load(std::memory_order_relaxed)));
        }
        else if (ratioIndex == 4)
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
        const float minimumReduction = !studio && ratioIndex == 4 ? -1.3f : -1.0f;
        reduction = std::clamp(reduction, minimumReduction, 60.0f);
        if (!studio)
        {
            // The installed unit charges most of its reduction in the FET cell,
            // then adds a measured 2.2--3.4 dB programme-memory population with
            // a roughly 0.9 s time constant. Keeping both populations in dB
            // makes their settled sum equal the calibrated static law.
            const float basePositiveReduction = std::max(0.0f, reduction);
            const float allOverloadMemory = ratioIndex == 4
                ? fetAllOverloadMemoryDb(basePositiveReduction) : 0.0f;
            reduction = std::min(60.0f, reduction + allOverloadMemory);
            const float positiveReduction = std::max(0.0f, reduction);
            const float attackPosition = fetAttackPosition(
                p.fetAttack.load(std::memory_order_relaxed));
            const float attackPosition2 = attackPosition * attackPosition;
            const float ratioSlowOffset = ratioIndex == 0 ? 0.80f
                : ratioIndex == 1 ? 0.0f
                : ratioIndex == 2 ? -0.65f
                : ratioIndex == 3 ? -1.20f : 1.90f;
            const float slowTarget = basePositiveReduction > 0.0f
                ? std::min(basePositiveReduction,
                    1.4f + 0.058f * basePositiveReduction
                        + ratioSlowOffset
                        + 0.8f * attackPosition2 * attackPosition2)
                : 0.0f;
            const float fastTarget
                = reduction - slowTarget - allOverloadMemory;
            // At the three deep 4:1 drive points, the installed unit's 40--60
            // ms plateau exceeds ours by 0.567 / 0.545 / 0.684 dB while the
            // settled reduction already matches. That nearly constant 0.599 dB
            // is a third population, not more total reduction: split it out of
            // the existing slow target and give it the measured intermediate
            // charge time. All-buttons stays on its independently fitted arm.
            constexpr float intermediateTargetCeilingDb = 0.5985f;
            // The -42 dBFS row (4.2865 dB settled reduction) already has the
            // reference shape without this population; the -36 dBFS row
            // (9.1406 dB) benefits from the full split. Interpolate between the
            // two measured anchors so the cell engages with reduction depth.
            const float intermediateDepth = std::clamp(
                (positiveReduction - 4.2865f) / (9.1406f - 4.2865f),
                0.0f, 1.0f);
            const float intermediateTarget = ratioIndex == 4
                ? allOverloadMemory
                : std::min(slowTarget,
                    intermediateTargetCeilingDb * intermediateDepth);
            const float longSlowTarget = ratioIndex == 4
                ? slowTarget : slowTarget - intermediateTarget;
            const float ratioAttackScale = ratioIndex == 0 ? 1.0f
                : ratioIndex == 1 ? 0.65f
                : ratioIndex == 2 ? 0.45f
                : ratioIndex == 3 ? 0.25f : 0.90f;
            const float baseFastAttackSeconds
                = (0.000047f - 0.000026f * attackPosition
                    - 0.000008f * attackPosition * attackPosition)
                    * ratioAttackScale;
            // The measured cell charges FASTER the harder it is driven -- the
            // ordinary sense for a feedback detector, and the opposite of what
            // this line used to say before the campaign measured it.
            //
            // `kFetDriveAttackScale` is a table, not a fit, and every entry is
            // one rendered comparison against the installed unit: source levels
            // -42 / -39 / -36 / -30 / -18 / -6 dBFS at input knob 0.8, which
            // arrive here as settled reductions of 4.29 / 6.66 / 9.14 / 14.11 /
            // 24.07 / 34.03 dB. Each entry was tuned to minimise the normalised
            // curve RMS against the reference's own attack trajectory over
            // 0.5-20 ms, which is the campaign's primary dynamics gate; the
            // fitted time constant is a diagnostic only, because above ~30 dB of
            // reduction it inverts sign against every other measure of speed.
            //
            // The abscissa is the SETTLED reduction, not the 40-60 ms plateau:
            // the programme-memory population is only ~5 % charged at 50 ms, so
            // the plateau reads 3-4 dB low and would put every anchor off axis.
            //
            // Both ends hold rather than extrapolate. The previous law was a
            // parabola fitted only to the three deep anchors, and its 0-14 dB
            // segment -- 2-10 dB of reduction, where most programme material
            // sits -- was pure extrapolation that nothing gated. Below the
            // first anchor the question is moot in any case: `slowTarget`
            // saturates at `positiveReduction` under 2.39 dB, so the fast
            // population is empty and this multiplier has no effect at all.
            // Above the last anchor the deepest reduction any measured row
            // reaches is All-buttons at 36.1 dB, 2 dB past it.
            //
            // Holding the ends is also the safety property. `reduction` is
            // clamped to 60 dB, and `advance()` turns a non-positive time into
            // std::max(1.0e-6f, time) -- a silent 1 us attack rather than a
            // failure. The old parabola crossed zero at 47.7 dB, only 11.7 dB
            // outside its clamp, so widening that clamp would have produced
            // exactly that regime with no assertion firing. A table of positive
            // values with held ends cannot, whatever argument it is given.
            const float driveAttackScale
                = fetDriveAttackScale(positiveReduction);
            const float fastAttackSeconds
                = baseFastAttackSeconds * driveAttackScale
                    * (6.5f + 5.7f * attackPosition
                        - 9.4f * attackPosition * attackPosition)
                    * fetAttackKnobScale(attackPosition, positiveReduction);
            const float allOverloadDepth = std::clamp(
                (basePositiveReduction - 40.50f) / (41.75f - 40.50f),
                0.0f, 1.0f);
            const float slowAttackSeconds = ratioIndex == 4
                ? 1.50f - 1.05f * allOverloadDepth
                : 0.95f + 0.55f * attackPosition2 * attackPosition2;
            const float intermediateAttackSeconds = ratioIndex == 4
                ? 5.0f : 0.010f;
            // Splitting a fixed target makes the practical 3--5 s "settled"
            // windows charge slightly farther than the old single population.
            // Keeping the remainder 8 % slower makes the two populations meet
            // the old trajectory again in that measured window while leaving
            // their asymptotic sum unchanged.
            const float longSlowAttackSeconds
                = ratioIndex != 4 && intermediateTarget > 0.0f
                    ? slowAttackSeconds * 1.08f : slowAttackSeconds;
            const float releasePosition = fetReleasePosition(
                p.fetRelease.load(std::memory_order_relaxed));
            const int programmeSilenceHoldSamples = std::max(
                1, static_cast<int>(std::lround(0.002f * sr)));
            if (vintageProgrammeActive)
                d.programmeSilentSamples = 0;
            else
                d.programmeSilentSamples = std::min(
                    d.programmeSilentSamples + 1,
                    programmeSilenceHoldSamples + 1);
            vintageProgrammeSupported = vintageProgrammeActive
                || d.programmeSilentSamples <= programmeSilenceHoldSamples;
            const float recoveryLevelDrop = std::max(
                d.recoveryPreviousInstantLevel - vintageInstantLevel, 0.0f);
            d.recoveryPreviousInstantLevel = vintageInstantLevel;
            if (vintageProgrammeSupported)
            {
                if (!d.recoveryWasSupported)
                    d.recoveryMaximumLevelDrop = 0.0f;
                d.recoveryMaximumLevelDrop = std::max(
                    d.recoveryMaximumLevelDrop, recoveryLevelDrop);
                d.recoveryWasSupported = true;
                d.recoveryElapsedSeconds = 0.0f;
                d.recoveryGain = 1.0f;
            }
            else
            {
                if (d.recoveryWasSupported)
                {
                    d.recoveryWasSupported = false;
                    d.recoveryMaximumGrDb = d.programmeMaximumGrDb;
                    d.recoveryTerminalWeight = std::clamp(
                        (d.recoveryMaximumLevelDrop - 12.0f) / 8.0f,
                        0.0f, 1.0f);
                    d.recoveryAttackPosition = attackPosition;
                    d.recoveryReleasePosition = releasePosition;
                    d.recoveryRatioIndex = ratioIndex;
                    d.recoveryElapsedSeconds = 0.0f;
                }
                else
                    d.recoveryElapsedSeconds += 1.0f / sr;
                if (ratioIndex != d.recoveryRatioIndex
                    || std::abs(attackPosition - d.recoveryAttackPosition)
                        > 1.0e-4f
                    || std::abs(releasePosition - d.recoveryReleasePosition)
                        > 1.0e-4f)
                    d.recoveryMaximumGrDb = 0.0f;
                const float assistDb = fetPostBurstRecoveryAssistDb(
                    d.recoveryElapsedSeconds, d.recoveryMaximumGrDb,
                    d.recoveryRatioIndex, d.recoveryTerminalWeight,
                    d.recoveryAttackPosition, d.recoveryReleasePosition,
                    static_cast<float>(fs));
                d.recoveryGain = decibelsToGain(assistDb);
            }
            if (vintageProgrammeSupported)
            {
                d.programmeExposureSeconds = std::min(
                    5.0f, d.programmeExposureSeconds + 1.0f / sr);
                d.programmeMaximumGrDb = std::max(
                    d.programmeMaximumGrDb, positiveReduction);
            }
            const float exposureReleaseScale = fetReleaseExposureScale(
                std::max(d.programmeExposureSeconds, 0.01f),
                d.programmeMaximumGrDb);
            const float ratioReleaseScale = ratioIndex == 0 ? 1.0f
                : ratioIndex == 1 ? 0.875f
                : ratioIndex == 2 ? 0.842f
                : ratioIndex == 3 ? 0.762f : 1.0f;
            const float fastReleaseSeconds = ratioIndex == 4
                ? 0.020f
                : (2.00f - 1.82f * releasePosition
                    + 0.12f * releasePosition * releasePosition)
                    * ratioReleaseScale * exposureReleaseScale;
            const float slowReleaseSeconds = ratioIndex == 4
                ? 0.020f
                : (2.00f - 2.17f * releasePosition
                    + 0.82f * releasePosition * releasePosition)
                    * ratioReleaseScale * exposureReleaseScale;
            const auto advance = [sr](float state, float target,
                                      float attackSeconds,
                                      float releaseTimeSeconds) noexcept {
                float time = target > state ? attackSeconds : releaseTimeSeconds;
                // Let a quiescent All-buttons cell acquire its measured
                // small-signal expansion promptly; recovery from positive GR
                // still uses the selected release above.
                if (state <= 0.0f && target < 0.0f) time = 0.050f;
                const float coefficient = std::exp(
                    -1.0f / (std::max(1.0e-6f, time) * sr));
                return target + (state - target) * coefficient;
            };
            d.fastGrDb = advance(
                d.fastGrDb, fastTarget, fastAttackSeconds, fastReleaseSeconds);
            d.intermediateGrDb = advance(
                d.intermediateGrDb, intermediateTarget,
                intermediateAttackSeconds, slowReleaseSeconds);
            d.slowGrDb = advance(
                d.slowGrDb, longSlowTarget,
                longSlowAttackSeconds, slowReleaseSeconds);
            d.envelope = decibelsToGain(
                -(d.fastGrDb + d.intermediateGrDb + d.slowGrDb));
            if (!vintageProgrammeActive
                && d.programmeSilentSamples > programmeSilenceHoldSamples
                && d.fastGrDb + d.intermediateGrDb + d.slowGrDb < 0.05f)
            {
                d.programmeExposureSeconds = 0.0f;
                d.programmeMaximumGrDb = 0.0f;
            }
        }
        else
        {
            const float minRelease = 0.05f, maxRelease = 1.1f;
            float attack = std::max(0.0001f,
                p.fetAttack.load(std::memory_order_relaxed) * 0.001f);
            const float releaseNorm = std::clamp(
                p.fetRelease.load(std::memory_order_relaxed) / 1100.0f,
                0.0f, 1.0f);
            float release = minRelease * std::pow(
                maxRelease / minRelease, releaseNorm);
            if (ratioIndex == 4)
            {
                attack = std::max(0.0002f, attack * 2.0f);
                release *= 1.0f + std::clamp(
                    reduction / 20.0f, 0.0f, 1.0f) * 0.5f;
                const float memoryDecay = std::exp(-1.0f / (0.5f * sr));
                d.releaseMemory *= memoryDecay;
                if (d.transient == 0 && reduction > 3.0f)
                    d.releaseMemory = std::min(1.0f, d.releaseMemory + 0.15f);
                release *= 1.0f + d.releaseMemory * 0.3f;
            }
            const float programFactor = std::clamp(
                1.0f + reduction * 0.05f, 0.5f, 2.0f);
            const float signalDelta = std::abs(detect - d.previousLevel);
            d.previousLevel = detect;
            if (signalDelta > 0.1f)
            {
                attack *= 0.8f;
                release *= 1.2f;
            }
            else
            {
                attack *= programFactor;
                release *= programFactor;
            }
            const float target = decibelsToGain(-reduction);
            const float attackCoeff = std::exp(-1.0f / (
                std::max(1.0e-5f, attack * sr)));
            const float releaseCoeff = std::exp(-1.0f / (
                std::max(1.0e-5f, release * sr)));
            if (ratioIndex == 4)
            {
                if (target < d.envelope)
                {
                    if (d.transient < 30)
                    {
                        const float delayedAttack = attackCoeff * 0.5f + 0.5f;
                        d.envelope = delayedAttack * d.envelope
                            + (1.0f - delayedAttack) * target;
                        ++d.transient;
                    }
                    else
                        d.envelope = attackCoeff * d.envelope
                            + (1.0f - attackCoeff) * target;
                }
                else
                {
                    d.transient = 0;
                    const float gr = -gainToDecibels(d.envelope + 0.001f);
                    const float fastBase = 0.05f, fastMin = 0.025f;
                    const float scaleGr = std::clamp(gr / 20.0f, 0.0f, 1.0f);
                    const float fast = fastBase
                        - scaleGr * (fastBase - fastMin);
                    const float fastCoeff = std::exp(-1.0f / (
                        std::max(1.0e-5f, fast * sr)));
                    const float effective = fastCoeff * scaleGr
                        + releaseCoeff * (1.0f - scaleGr);
                    d.envelope = effective * d.envelope
                        + (1.0f - effective) * target;
                }
            }
            else if (target < d.envelope)
                d.envelope = attackCoeff * d.envelope
                    + (1.0f - attackCoeff) * target;
            else
                d.envelope = releaseCoeff * d.envelope
                    + (1.0f - releaseCoeff) * target;
        }
        const float maximumEnvelope = ratioIndex == 4
            ? (studio ? 1.12f : 1.17f) : 1.0f;
        d.envelope = std::clamp(d.envelope, 0.001f, maximumEnvelope);
        if (!std::isfinite(d.envelope)) d.envelope = 1.0f;
        if (!studio && ratioIndex == 0)
        {
            const float currentBaseReduction = std::max(
                0.0f, -gainToDecibels(d.envelope));
            constexpr float kneeHoldSeconds = 0.030f;
            if (currentBaseReduction >= d.kneeBaseReductionDb)
            {
                d.kneeBaseReductionDb = currentBaseReduction;
                d.kneeBaseHoldSeconds = kneeHoldSeconds;
            }
            else if (d.kneeBaseHoldSeconds > 0.0f)
                d.kneeBaseHoldSeconds = std::max(
                    0.0f, d.kneeBaseHoldSeconds - 1.0f / sr);
            else
            {
                const float release = std::exp(-1.0f / (0.250f * sr));
                d.kneeBaseReductionDb = currentBaseReduction
                    + (d.kneeBaseReductionDb - currentBaseReduction)
                        * release;
            }
            if (kneeHandoff >= 1.0f)
            {
                // Once the raw detector reaches the accepted-law join this
                // auxiliary cell is outside its measured domain. Clear it
                // immediately: releasing its earlier onset correction into a
                // deep attack would alter the established dynamics curve.
                d.kneeCorrectionDb = 0.0f;
                d.kneeGain = 1.0f;
            }
            else
            {
                // A retained deep envelope can pass back through the shallow
                // range after the input has already fallen far below the
                // knee. It is release memory, not a new onset error. Require
                // the same live-programme support used by the envelope's
                // exposure cell; its 2 ms hold spans carrier zero crossings
                // but expires long before the 250 ms raw-peak release can
                // manufacture a false +0.85 dB post-burst gain hump.
                const float rawTargetCorrectionDb = vintageProgrammeSupported
                    ? (d.kneeBaseReductionDb - kneeTargetReduction)
                        * (1.0f - kneeHandoff)
                    : 0.0f;
                // The pre-change onset miss tops out at 0.724 dB. Keep this
                // cell inside that shallow domain even while the main
                // programme envelope is releasing from tens of dB of GR;
                // otherwise it mistakes retained deep reduction for a knee
                // error and cancels it on the auxiliary 100 ms time constant.
                constexpr float maximumCorrectionDb = 0.85f;
                const float targetCorrectionDb = std::clamp(
                    rawTargetCorrectionDb,
                    -maximumCorrectionDb, maximumCorrectionDb);
                constexpr float timeSeconds = 0.100f;
                const float coefficient = std::exp(
                    -1.0f / (timeSeconds * sr));
                d.kneeCorrectionDb = targetCorrectionDb
                    + (d.kneeCorrectionDb - targetCorrectionDb) * coefficient;
                d.kneeGain = decibelsToGain(d.kneeCorrectionDb);
            }
        }
        else
        {
            d.kneeBaseReductionDb = 0.0f;
            d.kneeCorrectionDb = 0.0f;
            d.kneeGain = 1.0f;
        }
        float sagGain = 1.0f;
        if (ratioIndex == 4 && !studio
            && p.fetCurve.load(std::memory_order_relaxed) != 0)
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
        if (!studio) out *= d.kneeGain * d.recoveryGain;
        // Studio FET keeps the generic output-transformer model. The measured
        // vintage spectrum is generated explicitly above; passing it through
        // that transformer introduced a non-physical H2 cancellation notch.
        if (studio) out = outT.processSample(out, ch);
        if (!studio) out = fetConvolution.processSample(out, ch) * fetHardwareGain;
        const float grHpf = -gainToDecibels(d.envelope + 0.001f);
        const float hpfCutoff = studio
            ? 20.0f + std::clamp(grHpf / 20.0f, 0.0f, 1.0f) * 60.0f
            : 5.0f;
        const float hpfAlpha = 1.0f - std::exp(-2.0f * kDuskPi * hpfCutoff / sr);
        if (!studio) { d.subBassHpState += hpfAlpha * (out - d.subBassHpState); out -= d.subBassHpState; }
        const float outputValue = p.fetOutput.load(std::memory_order_relaxed);
        const float outputGain = decibelsToGain(
            studio ? outputValue : fetOutputGainDb(outputValue));
        // The installed model is linear throughout the measured steady range,
        // then bends startup peaks toward a ~7.1 linear ceiling. The previous
        // hard +/-2 guard flattened both the level sweep and its harmonics.
        // The atan branch deliberately starts at slope span*(2/pi)/scale
        // (~0.537), not 1.0: knee/span/scale are one joint fit to the measured
        // reference startup-peak surface, and forcing C1 continuity at the
        // knee reshapes that fitted curve. Do not smooth the derivative kink
        // without re-fitting all three constants against fresh captures.
        const float drivenOutput = out * outputGain;
        constexpr float ceilingKnee = 1.97879118f;
        constexpr float ceilingSpan = 5.16417142f;
        constexpr float ceilingScale = 6.11998526f;
        const float magnitude = std::abs(drivenOutput);
        if (magnitude <= ceilingKnee) return drivenOutput;
        const float limited = ceilingKnee + ceilingSpan * (2.0f / kDuskPi)
            * std::atan((magnitude - ceilingKnee) / ceilingScale);
        return std::copysign(limited, drivenOutput);
    }

    float processVCA(float input, int ch, float sidechain,
                     const MultiCompParameterState& p, bool /*external*/) noexcept
    {
        auto& d = vca[ch];
        const float sr = static_cast<float>(fs * osFactor);
        // The DSP has already selected, filtered and stereo-linked the
        // detector signal for both internal and external operation. VCA is
        // feed-forward, so consuming that signal preserves its topology while
        // making the global sidechain controls effective.
        const float detect = std::abs(sidechain);
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

    float busFeedbackRect(BusState& d, int ch, float sr) noexcept
    {
        const float alpha = 1.0f / (1.0f + kDuskTwoPi * 60.0f / sr);
        d.hp = alpha * (d.hp + d.compressed - d.prev);
        d.prev = d.compressed;
        d.hp2 = alpha * (d.hp2 + d.hp - d.prev2);
        d.prev2 = d.hp;
        float detector = d.hp2;
        const size_t channel = static_cast<size_t>(std::clamp(ch, 0, 1));
        if (busSidechainHighPassFrequency >= 1.0f)
            detector = busSidechainHighPass[channel].process(detector);
        if (std::abs(busSidechainLowGain) > 0.001f)
            detector = busSidechainLowShelf[channel].process(detector);
        if (std::abs(busSidechainHighGain) > 0.001f)
            detector = busSidechainHighShelf[channel].process(detector);
        return std::abs(detector);
    }

    static float busRmsLevel(BusState& d, float rectified, float sr) noexcept
    {
        const float coefficient = std::exp(-1.0f / (0.005f * sr));
        d.rms = coefficient * d.rms
              + (1.0f - coefficient) * rectified * rectified;
        return std::sqrt(std::max(0.0f, d.rms));
    }

    static float busReduction(float level, const MultiCompParameterState& p) noexcept
    {
        const float ratios[3] = {2.0f, 4.0f, 10.0f};
        const float ratio = ratios[std::clamp(
            p.busRatio.load(std::memory_order_relaxed), 0, 2)];
        const float over = gainToDecibels(std::max(level, 1.0e-9f)
            / decibelsToGain(p.busThreshold.load(std::memory_order_relaxed)));
        const float slope = 1.0f - 1.0f / ratio;
        const float reduction = over <= -5.0f ? 0.0f
            : (over >= 5.0f ? over * slope
                            : slope * (over + 5.0f) * (over + 5.0f) / 20.0f);
        return std::min(reduction, 20.0f);
    }

    static void advanceBusEnvelope(BusState& d, float level,
                                   const MultiCompParameterState& p,
                                   float sr) noexcept
    {
        const float reduction = busReduction(level, p);
        const float attacks[6] = {0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f};
        const float releases[5] = {100.0f, 300.0f, 600.0f, 1200.0f, -1.0f};
        const float attack = attacks[std::clamp(
            p.busAttack.load(std::memory_order_relaxed), 0, 5)] * 0.001f;
        float release = releases[std::clamp(
            p.busRelease.load(std::memory_order_relaxed), 0, 4)] * 0.001f;
        if (release < 0.0f)
        {
            const float delta = std::abs(level - d.previous);
            d.previous = d.previous * 0.95f + level * 0.05f;
            const float transientDensity = std::clamp(delta * 20.0f, 0.0f, 1.0f);
            const float compressionFactor = std::clamp(reduction / 12.0f, 0.0f, 1.0f);
            release = 0.15f + (1.0f - transientDensity) * compressionFactor * 0.30f;
        }
        const float target = decibelsToGain(-reduction);
        const float time = target < d.envelope ? attack : release;
        const float coefficient = std::exp(-1.0f
            / std::max(1.0f, time * sr));
        d.envelope = target + (d.envelope - target) * coefficient;
        if (!std::isfinite(d.envelope)) d.envelope = 1.0f;
    }

    float renderBusOutput(float input, int ch,
                          const MultiCompParameterState& p, float mix) noexcept
    {
        auto& d = bus[ch];
        const float transformed = inputTransformerBus[ch].processSample(input, ch);
        float out = transformed * d.envelope;
        d.compressed = out;
        out += 0.004f * out * out + 0.003f * out * out * out;
        out = outputTransformerBus[ch].processSample(out, ch);
        out = busConvolution.processSample(out, ch) * busHardwareGain
            * decibelsToGain(p.busMakeup.load(std::memory_order_relaxed));
        out = std::clamp(out, -2.0f, 2.0f);
        return out * mix + input * (1.0f - mix);
    }

    float processBus(float input, int ch, float sidechain,
                     const MultiCompParameterState& p, float mix,
                     bool external) noexcept
    {
        auto& d = bus[ch];
        const float sr = static_cast<float>(fs * osFactor);
        const float level = busRmsLevel(d,
            external ? std::abs(sidechain) : busFeedbackRect(d, ch, sr), sr);
        advanceBusEnvelope(d, level, p, sr);
        return renderBusOutput(input, ch, p, mix);
    }

    float processStudioVCA(float input, int ch, float sidechain, const MultiCompParameterState& p, bool external) noexcept
    {
        auto& d = studioVca[ch];
        const float sr = static_cast<float>(fs * osFactor);
        const float transformed = inputTransformerStudioVca[ch].processSample(input, ch);
        const float x = external ? sidechain : input;
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

    float processDigital(float input, int ch, float sidechain, const MultiCompParameterState& p, float mix, bool /*external*/) noexcept
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
        // The dry half of the local mix takes the same delayed sample as the wet
        // half; blending against the undelayed input combs by the lookahead time.
        return wet * mix + delayed * (1.0f - mix);
    }
};

} // namespace duskaudio
