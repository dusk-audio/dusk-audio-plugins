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
        fs = newFs;
        osFactor = newFactor;
        const float sr = static_cast<float>(fs * osFactor);
        transientShaper.setRate(sr);
        updateRateCoefficients(sr);
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
            case MultiCompMode::FET: return processFET(input, ch, sc, p, false, external);
            case MultiCompMode::VCA: return processVCA(input, ch, sc, p, external);
            case MultiCompMode::Bus: return processBus(input, ch, sc, p, localMix, external);
            case MultiCompMode::StudioFET: return processFET(input, ch, sc, p, true, external);
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
        float gain = 1, detectorLevel = 0, detectorPeak = 0;
        float colourPeak = 0, colourDc = 0;
        float fastGrDb = 0, midGrDb = 0, slowGrDb = 0;
        int detectorExposureSamples = 0, colourPeakHold = 0;
        // Starts saturated so a freshly reset state counts as silent.
        int detectorSilentSamples = 1 << 20;
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
    bool prepared = false;
    std::array<OptoState, 2> opto{};
    std::array<FETState, 2> fet{};
    std::array<VCAState, 2> vca{};
    std::array<BusState, 2> bus{};
    std::array<StudioFETState, 2> studioFet{};
    std::array<StudioVCAState, 2> studioVca{};
    std::array<DigitalState, 2> digital{};
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
    float optoDetectorPeakAttack = 0, optoDetectorPeakRelease = 0;
    float optoColourPeakRelease = 0, optoColourDcSmoothing = 0;
    int optoChargeTopOffSamples = 1, optoColourPeakHoldSamples = 1;
    int optoDetectorSilenceHoldSamples = 1;
    float optoFastAttack = 0, optoSlowAttack = 0;
    float optoFastRelease = 0, optoMidRelease = 0, optoSlowRelease = 0;
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
        // separates sustained energy from unsupported peaks.  The original
        // 50 us / 40 ms peak follower remains as a ceiling after calibration;
        // it does not drive a second gain-cell path.
        constexpr float detectorAttackSeconds = 0.000400f;
        optoDetectorAttack = std::exp(-optoInvSampleRate / detectorAttackSeconds);
        optoDetectorRelease = std::exp(-optoInvSampleRate / 0.008f);
        optoDetectorPeakAttack = std::exp(-optoInvSampleRate / 0.000050f);
        optoDetectorPeakRelease = std::exp(-optoInvSampleRate / 0.040f);
        optoColourPeakRelease = std::exp(-optoInvSampleRate / 0.040f);
        optoChargeTopOffSamples = std::max(1, static_cast<int>(
            std::lround(0.020f * sr)));
        optoColourPeakHoldSamples = std::max(1, static_cast<int>(
            std::lround(0.002f * sr)));
        optoDetectorSilenceHoldSamples = std::max(1, static_cast<int>(
            std::lround(0.0005f * sr)));
        optoColourDcSmoothing = 1.0f - std::exp(
            -kDuskTwoPi * 10.0f * optoInvSampleRate);
        optoFastAttack = std::exp(-optoInvSampleRate / 0.021f);
        optoSlowAttack = std::exp(-optoInvSampleRate / 0.190f);
        optoFastRelease = std::exp(-optoInvSampleRate / 0.064f);
        optoMidRelease = std::exp(-optoInvSampleRate / 0.185f);
        optoSlowRelease = std::exp(-optoInvSampleRate / 1.174f);
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
            return curve.back() + (position - static_cast<float>(curve.size() - 1))
                * (curve.back() - curve[curve.size() - 2]);
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
        d.detectorExposureSamples = hasDetectorInput
            ? std::min(d.detectorExposureSamples + 1, optoChargeTopOffSamples)
            : 0;
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
        // The static law was measured with the original 50 us / 40 ms peak
        // follower on a 997 Hz sine.  At 48 kHz, fixed-point iteration of one
        // full-wave period gives peaks of 0.925093862 for the 0.4 ms / 8 ms
        // integrator and 0.994476788 for that peak follower.  Therefore the
        // exact calibration-condition correction is
        // 20*log10(0.994476788 / 0.925093862) = 0.628177 dB.
        constexpr float detectorIntegrationCalibrationDb = 0.628177f;
        const float integratedDetectorLevel = d.detectorLevel
            * decibelsToGain(detectorIntegrationCalibrationDb);
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
        // The constrained fit uses the -36/-30/-18/-12 dBFS broadband points
        // while retaining the crest triplet; -24 dBFS is held out.
        constexpr float broadbandFitPivotDb = -18.0f;
        constexpr float broadbandFitAtPivot = 0.010f;
        constexpr float broadbandFitSlope = -0.0167f;
        const float broadbandCorrectionDb = fluctuationDb
            * (broadbandFitAtPivot + broadbandFitSlope
                * (uncorrectedInputLevelDb - broadbandFitPivotDb));
        const float inputLevelDb = uncorrectedInputLevelDb
            + broadbandCorrectionDb;
        const float thresholdDb = optoThresholdDb(pr, limit);
        const float overdriveDb = inputLevelDb - thresholdDb;
        const float targetGrDb = pr <= 10.0f ? 0.0f : optoCurveDb(overdriveDb, limit);
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
        const float fastShare = lowDriveFastShare
            + (highDriveFastShare - lowDriveFastShare) * driveBlend;
        const float midShare = lowDriveMidShare
            + (highDriveMidShare - lowDriveMidShare) * driveBlend;
        const float slowShare = lowDriveSlowShare
            + (highDriveSlowShare - lowDriveSlowShare) * driveBlend;
        // The measured 21 ms fast charge applies at the amplitude-fit point.
        // At high drive, the independent rate and remaining-capacity exponents
        // are calibrated against the 2/5/20/40 Hz repeated-burst points (10 Hz
        // held out). An empty population charges quickly, then its rate falls
        // as it approaches capacity. This raises the per-event charge without
        // turning dense repetition into an equivalent sustained tone.
        constexpr float lowDriveAttackRate = 2.1f;
        constexpr float highDriveFastAttackRate = 200.0f;
        constexpr float highDriveSlowAttackRate = 200.0f;
        constexpr float highDriveFastChargeExponent = 3.0f;
        constexpr float highDriveSlowChargeExponent = 1.5f;
        constexpr float highDriveFastMinimumChargeRate = 0.150f;
        const float fastAttackRate = lowDriveAttackRate
            + (highDriveFastAttackRate - lowDriveAttackRate) * driveBlend;
        const float slowAttackRate = lowDriveAttackRate
            + (highDriveSlowAttackRate - lowDriveAttackRate) * driveBlend;
        const float fastChargeExponent = 1.0f
            + (highDriveFastChargeExponent - 1.0f) * driveBlend;
        const float slowChargeExponent = 1.0f
            + (highDriveSlowChargeExponent - 1.0f) * driveBlend;
        // A finite top-off rate applies only after 20 ms of uninterrupted
        // exposure. It lets a sustained source reach cell capacity without
        // changing the charge of the measured 5 ms and 10 ms burst events.
        const float fastMinimumChargeRate
            = d.detectorExposureSamples >= optoChargeTopOffSamples
                ? highDriveFastMinimumChargeRate * driveBlend : 0.0f;
        const float fastAttackCoeff = std::max(
            0.0f, 1.0f - (1.0f - optoFastAttack) * fastAttackRate);
        const float slowAttackCoeff = std::max(
            0.0f, 1.0f - (1.0f - optoSlowAttack) * slowAttackRate);
        const bool detectorDriven = detectorAbs > effectiveDetectorLevel * 0.4f;
        const auto followTarget = [detectorDriven, hasDetectorInput](
                                      float& state, float target, float attack,
                                      float release, float chargeExponent,
                                      float minimumChargeRate) noexcept {
            // Silence discharges the cells directly; cascading the detector's
            // 40 ms waveform integration into them is what produced D3e's
            // false 8-120 ms hold. A stale detector envelope may continue a
            // release toward a lower target, but cannot recharge a cell until
            // the selected detector input supports it again.
            if (!hasDetectorInput)
                state *= release;
            else if (!detectorDriven && target > state)
                return;
            else if (target > state)
            {
                const float remainingFraction = target > 1.0e-9f
                    ? std::clamp((target - state) / target, 0.0f, 1.0f)
                    : 0.0f;
                const float curvedAttackStep = (1.0f - attack) * std::max(
                    std::pow(remainingFraction, chargeExponent - 1.0f),
                    minimumChargeRate);
                state += curvedAttackStep * (target - state);
            }
            else
                state = target + (state - target) * release;
        };
        // Three independent gain-reduction populations. Exposure changes only
        // their fill; their discharge constants never change with drive,
        // exposure, or Peak Reduction.
        followTarget(d.fastGrDb, fastShare * targetGrDb,
                     fastAttackCoeff, optoFastRelease, fastChargeExponent,
                     fastMinimumChargeRate);
        followTarget(d.midGrDb, midShare * targetGrDb,
                     slowAttackCoeff, optoMidRelease, slowChargeExponent, 0.0f);
        followTarget(d.slowGrDb, slowShare * targetGrDb,
                     slowAttackCoeff, optoSlowRelease, slowChargeExponent, 0.0f);
        const float dynamicGrDb = std::max(
            0.0f, d.fastGrDb + d.midGrDb + d.slowGrDb);
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

    float processFET(float input, int ch, float sidechain, const MultiCompParameterState& p, bool studio, bool external) noexcept
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

    float processVCA(float input, int ch, float sidechain, const MultiCompParameterState& p, bool external) noexcept
    {
        auto& d = vca[ch];
        const float sr = static_cast<float>(fs * osFactor);
        // JUCE's VCA is feed-forward: internal detection is the audio input
        // even when the host-side stereo-link buffer is populated.  Only an
        // actual external sidechain replaces the detector source.
        const float detect = std::abs(external ? sidechain : input);
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

    float processBus(float input, int ch, float sidechain, const MultiCompParameterState& p, float mix, bool external) noexcept
    {
        auto& d = bus[ch];
        const float sr = static_cast<float>(fs * osFactor);
        const float fb = d.compressed;
        const float alpha = 1.0f / (1.0f + kDuskTwoPi * 60.0f / sr);
        d.hp = alpha * (d.hp + fb - d.prev); d.prev = fb;
        d.hp2 = alpha * (d.hp2 + d.hp - d.prev2); d.prev2 = d.hp;
        const float rect = external ? std::abs(sidechain) : std::abs(d.hp2);
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
