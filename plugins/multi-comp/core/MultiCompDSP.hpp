// Copyright (C) 2026 Dusk Audio , GNU GPL v3.0 or later (see repository LICENSE).
// MultiCompDSP , framework-free C++17 DSP core (zero JUCE/DAF includes).
#pragma once

#include "MultiCompModes.hpp"
#include "MultiCompParams.hpp"
#include "MultiCompAutoGain.hpp"
#include "MultiCompHelpers.hpp"
#include "../../shared-daf/dsp/DuskCrossover.hpp"
#include "../../shared-daf/dsp/DuskDenormals.hpp"
#include "../../shared-daf/dsp/DuskFilters.hpp"
#include "../../shared-daf/dsp/DuskOversampler.hpp"
#include "../../shared-daf/dsp/DuskSmoothed.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace duskaudio
{

struct MultiCompDSPTestAccess;

class MultiCompDSP
{
public:
    static constexpr float kMinPublishedGainReductionDb = -60.0f;
    static constexpr float kMaxPublishedGainReductionDb = 0.0f;

    enum class Parameter
    {
        Mode, Bypass, StereoLink, Mix, SidechainHP, TruePeakEnable, TruePeakQuality, ExternalSidechain, AutoMakeup, Distortion, DistortionAmount,
        Oversampling, GlobalLookahead, OptoPeakReduction, OptoGain, OptoLimit,
        FetInput, FetOutput, FetAttack, FetRelease, FetRatio, FetCurve, FetTransient, FetThreshold,
        VcaThreshold, VcaRatio, VcaAttack, VcaRelease, VcaOutput, VcaOverEasy, VcaClassicDetector,
        BusThreshold, BusRatio, BusAttack, BusRelease, BusMakeup, BusMix,
        StudioVcaThreshold, StudioVcaRatio, StudioVcaAttack, StudioVcaRelease, StudioVcaOutput,
        DigitalThreshold, DigitalRatio, DigitalKnee, DigitalAttack, DigitalRelease, DigitalLookahead, DigitalMix, DigitalOutput, DigitalAdaptive,
        Crossover1, Crossover2, Crossover3, EnvelopeCurve, GlobalSidechainListen,
        MbMix, MbOutput, NoiseEnable, SaturationMode, ScLowFreq, ScLowGain,
        ScHighFreq, ScHighGain, StereoLinkMode
    };
    enum class MultibandParameter { Threshold, Ratio, Attack, Release, Makeup, Bypass, Solo, Enabled };

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    void processBlock(const float* const* in, float* const* out, int numChannels, int numSamples);
    void processBlockExternal(const float* const* in, const float* const* sidechain,
                              float* const* out, int numChannels, int numSamples);

    void setParameter(Parameter parameter, float value) noexcept;
    void setMultibandParameter(int band, MultibandParameter parameter, float value) noexcept;
    void setMode(int value) noexcept { setParameter(Parameter::Mode, static_cast<float>(value)); }
    void setBypass(bool value) noexcept { params.bypass.store(value, std::memory_order_relaxed); }
    void setMix(float value) noexcept { params.mix.store(value, std::memory_order_relaxed); }
    void setStereoLink(float value) noexcept { params.stereoLink.store(value, std::memory_order_relaxed); }
    void setExternalSidechain(bool value) noexcept { params.externalSidechain.store(value, std::memory_order_relaxed); }
    void setOversampling(int value) noexcept { params.oversampling.store(value, std::memory_order_relaxed); }

    MultiCompParameterState& parameterState() noexcept { return params; }
    const MultiCompParameterState& parameterState() const noexcept { return params; }

    float getBandGainReduction(int band) const noexcept
    {
        return band >= 0 && band < kMultiCompBands
            ? std::clamp(bandGR[static_cast<size_t>(band)].load(std::memory_order_relaxed),
                         kMinPublishedGainReductionDb, kMaxPublishedGainReductionDb)
            : 0.0f;
    }
    float getGainReduction() const noexcept
    {
        return std::clamp(masterGR.load(std::memory_order_relaxed),
                          kMinPublishedGainReductionDb, kMaxPublishedGainReductionDb);
    }
    float getInputLevel() const noexcept { return inputLevel.load(std::memory_order_relaxed); }
    float getOutputLevel() const noexcept { return outputLevel.load(std::memory_order_relaxed); }

    // The JUCE product reports the maximum oversampling latency so PDC remains
    // constant while the quality selector changes. Multiband is native-rate.
    int getLatencySamples() const noexcept;

private:
    friend struct MultiCompDSPTestAccess;

    static constexpr int kMaxChannels = 2;
    static constexpr int kBypassRampMs = 30;
    static constexpr int kSidechainListenRampMs = 30;
    static constexpr int kAutoGainTransitionMs = 50;
    static constexpr int kAutoGainMeasurementWindow = 64;
    static constexpr int kCrossoverRampMs = 20;
    static constexpr int kCrossoverCoefficientInterval = 8;

    void processRange(const float* const* in, const float* const* sidechain,
                      float* const* out, int nCh, int nSamples,
                      bool externalSidechain, bool autoMakeup,
                      MultiCompMode mode, int linkMode, int actualOversampling,
                      float digitalLookaheadMs);
    void syncModeParameters(MultiCompMode mode, float digitalLookaheadMs) noexcept;
    void prepareLookahead(const float* const* in, const float* (&processingIn)[kMaxChannels],
                          int nCh, int nSamples, int delay);
    void processMultiband(const float* const* input, const float* const* sidechain,
                          float* const* output, int nCh, int nSamples);
    std::array<float, 3> crossoverTargets() const noexcept;
    void prepareCrossovers() noexcept;
    void resetCrossovers() noexcept;
    void updateCrossoverTargets() noexcept;
    void rebuildMultibandTopology(std::uint8_t mask) noexcept;
    DuskCrossover& crossoverForBoundary(int boundary, int channel, bool sidechain) noexcept;
    void updateMeters(float inPeak, float* const* out, int nCh, int nSamples);
    void processLatencyHistory(const float* const* in, float* const* out, int nCh,
                               int nSamples, int delay, bool emit) noexcept;
    void processSidechainListenHistory(const float* const* sidechain, int nCh,
                                       int nSamples, int delay) noexcept;
    void resetAutoGainMeasurement() noexcept;
    int latencySamplesForMode(MultiCompMode mode, float globalLookaheadMs,
                              float digitalLookaheadMs) const noexcept;

    MultiCompParameterState params;
    MultiCompParameterState modeParams;
    MultiCompModes modes;
    double sampleRate = 48000.0;
    int maxBlock = 512;
    std::array<MultiCompAntiAliasing, kMaxChannels> oversamplers;
    MultiCompAntiAliasing optoLinkedDetectorOversampler;
    MultiCompTruePeakDetector truePeakDetector;
    std::array<MultiCompSidechainFilter, kMaxChannels> sidechainFilters;
    std::array<MultiCompSidechainEQ, kMaxChannels> sidechainEQ;

    std::array<DuskCrossover, kMaxChannels> crossover1, crossover2, crossover3;
    std::array<DuskCrossover, kMaxChannels> scCrossover1, scCrossover2, scCrossover3;
    std::array<LinearRamp, 3> crossoverRamps;
    std::array<std::vector<float>, 3> crossoverCurves;
    std::array<std::array<std::vector<float>, kMaxChannels>, kMultiCompBands> bands, sidechainBands;
    std::array<std::vector<float>, kMaxChannels> processedSidechain;
    std::array<std::vector<float>, kMaxChannels> modeInput;
    std::vector<float> dry, bypassDry, mixCurve, bypassCurve, autoGainCurve;
    std::array<std::vector<float>, kMaxChannels> dryPathDelay;
    std::array<int, kMaxChannels> dryPathWrite{{0, 0}};
    std::array<std::vector<float>, kMaxChannels> bypassDelay;
    int bypassWrite = 0;
    std::array<std::vector<float>, kMaxChannels> sidechainListenDelay;
    std::array<int, kMaxChannels> sidechainListenWrite{{0, 0}};
    std::array<std::vector<float>, kMaxChannels> delayedInput;
    std::array<std::vector<float>, kMaxChannels> globalLookahead;
    std::array<int, kMaxChannels> globalLookaheadWrite{{0, 0}};
    std::array<float, kMaxChannels> previousOversampledSidechain{{0.0f, 0.0f}};
    std::array<bool, kMaxChannels> previousOversampledSidechainValid{{false, false}};
    std::array<float, kMaxChannels> previousOptoOwnSidechain{{0.0f, 0.0f}};
    std::array<bool, kMaxChannels> previousOptoOwnSidechainValid{{false, false}};
    std::array<float, kMaxChannels> previousBusSidechain{{0.0f, 0.0f}};
    std::array<bool, kMaxChannels> previousBusSidechainValid{{false, false}};
    std::array<float, kMultiCompBands * kMaxChannels> multibandEnvelopes{};
    std::uint8_t activeBandMask = 0x0f;
    std::array<int, kMultiCompBands> enabledBandIndices{{0, 1, 2, 3}};
    std::array<int, kMultiCompBands - 1> stageBoundaryIndices{{0, 1, 2}};
    int numEnabledBands = kMultiCompBands;
    int numActiveStages = kMultiCompBands - 1;
    int topBandIndex = kMultiCompBands - 1;

    LinearRamp busMixRamp;
    LinearRamp digitalMixRamp;
    SmoothedValue globalMixSmoother;
    LinearRamp bypassRamp;
    LinearRamp sidechainListenRamp;
    LinearRamp manualMakeupScaleRamp;
    MultiCompAutoGainMatcher autoGainMatcher;
    SmoothedValue autoGainSmoother;
    bool bypassSettled = false;
    bool lastBypass = false;
    bool lastExternalSidechain = false;
    bool lastAutoMakeup = false;
    bool firstBlock = true;
    int lastMode = -1;
    int autoGainHoldSamples = 0;
    double autoGainInputPower = 0.0;
    double autoGainOutputPower = 0.0;
    int autoGainMeasurementCount = 0;
    int antiAliasLatency = 0;

    std::array<std::atomic<float>, kMultiCompBands> bandGR{{0.0f, 0.0f, 0.0f, 0.0f}};
    std::atomic<float> masterGR{0.0f}, inputLevel{-60.0f}, outputLevel{-60.0f};
    std::uint32_t noiseState = 0x6d2b79f5u;
};

} // namespace duskaudio
