// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// MultiCompDSP — framework-free C++17 DSP core (zero JUCE/DPF includes).
#pragma once

#include "MultiCompModes.hpp"
#include "MultiCompParams.hpp"
#include "MultiCompAutoGain.hpp"
#include "MultiCompHelpers.hpp"
#include "../../shared-dpf/dsp/DuskCrossover.hpp"
#include "../../shared-dpf/dsp/DuskDenormals.hpp"
#include "../../shared-dpf/dsp/DuskFilters.hpp"
#include "../../shared-dpf/dsp/DuskOversampler.hpp"
#include "../../shared-dpf/dsp/DuskSmoothed.hpp"

#include <array>
#include <atomic>
#include <vector>

namespace duskaudio
{

class MultiCompDSP
{
public:
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
        return band >= 0 && band < kMultiCompBands ? bandGR[static_cast<size_t>(band)].load(std::memory_order_relaxed) : 0.0f;
    }
    float getGainReduction() const noexcept { return masterGR.load(std::memory_order_relaxed); }
    float getInputLevel() const noexcept { return inputLevel.load(std::memory_order_relaxed); }
    float getOutputLevel() const noexcept { return outputLevel.load(std::memory_order_relaxed); }

    // The JUCE product reports the maximum oversampling latency so PDC remains
    // constant while the quality selector changes. Multiband is native-rate.
    int getLatencySamples() const noexcept;

private:
    static constexpr int kMaxChannels = 2;
    static constexpr int kMixRampMs = 20;
    static constexpr int kBypassRampMs = 30;

    void processRange(const float* const* in, const float* const* sidechain,
                      float* const* out, int nCh, int first, int nSamples);
    void prepareLookahead(const float* const* in, const float* (&processingIn)[kMaxChannels],
                          int nCh, int nSamples);
    void processMultiband(const float* const* input, const float* const* sidechain,
                          float* const* output, int nCh, int nSamples, float mix);
    void updateCrossovers();
    float processOne(MultiCompMode mode, float input, float sc, int channel, int sampleIndex, int nSamples);
    void updateMeters(const float* const* in, float* const* out, int nCh, int nSamples);
    void processLatencyHistory(const float* const* in, float* const* out, int nCh, int nSamples, bool emit) noexcept;

    MultiCompParameterState params;
    MultiCompModes modes;
    double sampleRate = 48000.0;
    int maxBlock = 512;
    int preparedOversampling = 2;
    std::array<MultiCompAntiAliasing, kMaxChannels> oversamplers;
    MultiCompTruePeakDetector truePeakDetector;
    std::array<MultiCompSidechainFilter, kMaxChannels> sidechainFilters;
    std::array<MultiCompSidechainFilter, kMaxChannels> sidechainFiltersExternal;
    std::array<MultiCompSidechainEQ, kMaxChannels> sidechainEQ;

    std::array<DuskCrossover, kMaxChannels> crossover1, crossover2, crossover3;
    std::array<DuskCrossover, kMaxChannels> scCrossover1, scCrossover2, scCrossover3;
    std::array<std::array<std::vector<float>, kMaxChannels>, kMultiCompBands> bands, sidechainBands;
    std::array<std::vector<float>, kMaxChannels> processedSidechain;
    std::array<std::vector<float>, kMaxChannels> modeInput;
    std::vector<float> dry, mixCurve, bypassCurve, autoGainCurve;
    std::array<std::vector<float>, kMaxChannels> dryPathDelay;
    std::array<int, kMaxChannels> dryPathWrite{{0, 0}};
    std::array<std::vector<float>, kMaxChannels> bypassDelay;
    int bypassWrite = 0;
    std::array<std::vector<float>, kMaxChannels> delayedInput;
    std::array<std::vector<float>, kMaxChannels> globalLookahead;
    std::array<int, kMaxChannels> globalLookaheadWrite{{0, 0}};
    std::array<float, kMultiCompBands * kMaxChannels> multibandEnvelopes{};

    LinearRamp busMixRamp;
    LinearRamp digitalMixRamp;
    SmoothedValue globalMixSmoother;
    LinearRamp bypassRamp;
    MultiCompAutoGainMatcher autoGainMatcher;
    SmoothedValue autoGainSmoother;
    bool bypassSettled = false;
    bool lastBypass = false;
    bool firstBlock = true;
    int antiAliasLatency = 0;

    std::array<std::atomic<float>, kMultiCompBands> bandGR{{0.0f, 0.0f, 0.0f, 0.0f}};
    std::atomic<float> masterGR{0.0f}, inputLevel{-60.0f}, outputLevel{-60.0f};
    uint32_t noiseState = 0x6d2b79f5u;
};

} // namespace duskaudio
