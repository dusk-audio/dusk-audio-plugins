#include "TapeEchoDSP.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kSamples = 4096;

struct MeterResult
{
    float vu = 0.0f;
    float peak = 0.0f;
};

struct RenderResult
{
    MeterResult meter;
    bool zeroBlockStable = false;
};

RenderResult renderWithBlockSize(int blockSize)
{
    duskaudio::TapeEchoDSP dsp;
    dsp.prepare(kSampleRate, blockSize);

    std::array<std::vector<float>, 2> input{
        std::vector<float>(kSamples, 0.0f),
        std::vector<float>(kSamples, 0.0f) };
    std::array<std::vector<float>, 2> output{
        std::vector<float>(kSamples, 0.0f),
        std::vector<float>(kSamples, 0.0f) };
    input[0][0] = 0.75f;
    input[1][0] = 0.75f;

    for (int offset = 0; offset < kSamples; offset += blockSize)
    {
        const int count = std::min(blockSize, kSamples - offset);
        const float* inputs[] = {
            input[0].data() + offset,
            input[1].data() + offset };
        float* outputs[] = {
            output[0].data() + offset,
            output[1].data() + offset };
        dsp.processBlock(inputs, outputs, 2, count);
    }

    const float peakBeforeZeroBlock = dsp.getRecordPeakLevel();
    const float* inputs[] = { input[0].data(), input[1].data() };
    float* outputs[] = { output[0].data(), output[1].data() };
    dsp.processBlock(inputs, outputs, 2, 0);
    return {
        { dsp.getRecordVuLevel(), dsp.getRecordPeakLevel() },
        dsp.getRecordPeakLevel() == peakBeforeZeroBlock };
}

bool approximatelyEqual(float a, float b) noexcept
{
    return std::abs(a - b) <= 1.0e-6f * std::max(1.0f, std::max(std::abs(a), std::abs(b)));
}

std::vector<float> renderAtMix(float mix)
{
    constexpr int renderSamples = 8192;
    constexpr int blockSize = 64;
    duskaudio::TapeEchoDSP dsp;
    dsp.setMix(mix);
    dsp.setRepeatRate(1.0f);
    dsp.prepare(kSampleRate, blockSize);

    std::array<std::vector<float>, 2> input{
        std::vector<float>(renderSamples, 0.0f),
        std::vector<float>(renderSamples, 0.0f) };
    std::array<std::vector<float>, 2> output{
        std::vector<float>(renderSamples, 0.0f),
        std::vector<float>(renderSamples, 0.0f) };
    input[0][0] = 0.5f;
    input[1][0] = 0.5f;

    for (int offset = 0; offset < renderSamples; offset += blockSize)
    {
        const float* inputs[]{ input[0].data() + offset,
                              input[1].data() + offset };
        float* outputs[]{ output[0].data() + offset,
                          output[1].data() + offset };
        dsp.processBlock(inputs, outputs, 2, blockSize);
    }
    return output[0];
}

std::vector<float> renderSpringImpulse()
{
    constexpr int renderSamples = 96000;
    duskaudio::SpringReverb spring;
    spring.prepare(kSampleRate, 1.0f);
    std::vector<float> output(renderSamples, 0.0f);
    for (int i = 0; i < renderSamples; ++i)
        output[(size_t)i] = spring.process(i == 0 ? 1.0f : 0.0f);
    return output;
}

double windowEnergy(const std::vector<float>& signal,
                    double startSeconds, double endSeconds)
{
    const size_t begin = (size_t)std::lround(startSeconds * kSampleRate);
    const size_t end = std::min(signal.size(),
        (size_t)std::lround(endSeconds * kSampleRate));
    double energy = 0.0;
    for (size_t i = begin; i < end; ++i)
        energy += (double)signal[i] * (double)signal[i];
    return energy;
}
}

int main()
{
    const RenderResult oneSampleResult = renderWithBlockSize(1);
    const RenderResult smallBlockResult = renderWithBlockSize(64);
    const RenderResult wholeRenderResult = renderWithBlockSize(kSamples);
    if (!oneSampleResult.zeroBlockStable
        || !smallBlockResult.zeroBlockStable
        || !wholeRenderResult.zeroBlockStable)
    {
        std::cerr << "zero-length block changed meter state\n";
        return 1;
    }

    const MeterResult oneSample = oneSampleResult.meter;
    const MeterResult smallBlock = smallBlockResult.meter;
    const MeterResult wholeRender = wholeRenderResult.meter;

    const bool peakInvariant = oneSample.peak > 0.0f
        && approximatelyEqual(oneSample.peak, smallBlock.peak)
        && approximatelyEqual(oneSample.peak, wholeRender.peak);
    const bool vuInvariant = oneSample.vu > 0.0f
        && approximatelyEqual(oneSample.vu, smallBlock.vu)
        && approximatelyEqual(oneSample.vu, wholeRender.vu);

    if (!peakInvariant || !vuInvariant)
    {
        std::cerr << "meter result depends on block size\n"
                  << "  1: peak=" << oneSample.peak << " vu=" << oneSample.vu << '\n'
                  << " 64: peak=" << smallBlock.peak << " vu=" << smallBlock.vu << '\n'
                  << "all: peak=" << wholeRender.peak << " vu=" << wholeRender.vu << '\n';
        return 1;
    }

    // Drive the meters up first: asserting they are zero straight after
    // prepare() would pass even if reset() cleared nothing at all.
    duskaudio::TapeEchoDSP resetCheck;
    resetCheck.prepare(kSampleRate, 64);
    {
        std::array<std::vector<float>, 2> input{
            std::vector<float>(64, 0.75f),
            std::vector<float>(64, 0.75f) };
        std::array<std::vector<float>, 2> output{
            std::vector<float>(64, 0.0f),
            std::vector<float>(64, 0.0f) };
        const float* inputs[] = { input[0].data(), input[1].data() };
        float* outputs[] = { output[0].data(), output[1].data() };
        for (int block = 0; block < 8; ++block)
            resetCheck.processBlock(inputs, outputs, 2, 64);
    }
    if (resetCheck.getRecordPeakLevel() <= 0.0f
        || resetCheck.getRecordVuLevel() <= 0.0f)
    {
        std::cerr << "meters did not register the pre-reset signal\n";
        return 1;
    }

    resetCheck.reset();
    if (resetCheck.getRecordPeakLevel() != 0.0f
        || resetCheck.getRecordVuLevel() != 0.0f)
    {
        std::cerr << "reset did not clear meter state\n";
        return 1;
    }

    const auto dryOnly = renderAtMix(0.0f);
    const auto parallel = renderAtMix(0.5f);
    const auto wetOnly = renderAtMix(1.0f);
    bool heardWetPath = false;
    for (size_t i = 0; i < parallel.size(); ++i)
    {
        heardWetPath = heardWetPath || std::abs(wetOnly[i]) > 1.0e-7f;
        if (!approximatelyEqual(parallel[i], dryOnly[i] + wetOnly[i]))
        {
            std::cerr << "setMix(0.5) no longer preserves parallel dry-plus-wet output"
                      << " at sample " << i << '\n';
            return 1;
        }
    }
    if (!heardWetPath)
    {
        std::cerr << "mix regression did not exercise the wet path\n";
        return 1;
    }

    // The spring is a propagation model, not an instantaneous resonator: the
    // first packet must follow the one-way transit, and genuine round trips
    // must remain present after it. This also guards against unstable allpass
    // coefficients and accidentally disconnecting either wave direction.
    const auto springImpulse = renderSpringImpulse();
    const bool springFinite = std::all_of(
        springImpulse.begin(), springImpulse.end(),
        [](float sample) { return std::isfinite(sample) && std::abs(sample) < 100.0f; });
    const double preArrival = windowEnergy(springImpulse, 0.0, 0.018);
    const double firstPacket = windowEnergy(springImpulse, 0.020, 0.090);
    const double firstReturns = windowEnergy(springImpulse, 0.090, 0.300);
    const double lateTail = windowEnergy(springImpulse, 0.500, 1.500);
    // Equal-length decay windows: the tail must still be DECAYING, not merely
    // present. A loop that crept to unity would keep lateTail above its floor
    // while holding or growing, which the thresholds alone cannot catch.
    const double earlyDecay = windowEnergy(springImpulse, 0.500, 1.000);
    const double lateDecay  = windowEnergy(springImpulse, 1.000, 1.500);
    if (!springFinite || preArrival > 1.0e-20
        || firstPacket < 1.0e-6 || firstReturns < 1.0e-8
        || lateTail < 1.0e-10 || !(lateDecay < earlyDecay))
    {
        std::cerr << "dispersive spring propagation regression\n"
                  << "  finite=" << springFinite
                  << " pre=" << preArrival
                  << " first=" << firstPacket
                  << " returns=" << firstReturns
                  << " late=" << lateTail
                  << " earlyDecay=" << earlyDecay
                  << " lateDecay=" << lateDecay << '\n';
        return 1;
    }

    duskaudio::TapeEchoDSP ageCheck;
    constexpr std::array<std::array<float, 2>, 9> ageCases{{
        {{ -1.0f, 0.0f }},
        {{ 0.10f, 0.0f }},
        {{ 0.249f, 0.0f }},
        {{ 0.25f, 0.5f }},
        {{ 0.50f, 0.5f }},
        {{ 0.749f, 0.5f }},
        {{ 0.75f, 1.0f }},
        {{ 0.90f, 1.0f }},
        {{ 2.0f, 1.0f }} }};
    for (const auto& testCase : ageCases)
    {
        ageCheck.setTapeAge(testCase[0]);
        if (ageCheck.getTapeAge() != testCase[1])
        {
            std::cerr << "setTapeAge(" << testCase[0] << ") produced "
                      << ageCheck.getTapeAge() << " instead of " << testCase[1]
                      << '\n';
            return 1;
        }
    }

    constexpr std::array<float, duskaudio::TapeEchoDSP::kNumModes>
        leadingHeadRatios{
            1.0f, 1.91172f, 2.76118f, 1.91172f,
            1.0f, 1.91172f, 2.76118f, 1.0f,
            1.91172f, 1.0f, 1.0f, 1.0f };
    constexpr std::array<float, duskaudio::TapeEchoDSP::kNumModes>
        leadingHeadOffsets{
            0.0f, -1.428f, -2.141f, -1.428f,
            0.0f, -1.428f, -2.141f, 0.0f,
            -1.428f, 0.0f, 0.0f, 0.0f };
    for (int mode = 1; mode <= duskaudio::TapeEchoDSP::kNumModes; ++mode)
        if (duskaudio::TapeEchoDSP::leadingHeadRatioForMode(mode)
                != leadingHeadRatios[static_cast<size_t>(mode - 1)]
            || duskaudio::TapeEchoDSP::leadingHeadOffsetMsForMode(mode)
                != leadingHeadOffsets[static_cast<size_t>(mode - 1)])
        {
            std::cerr << "wrong leading-head timing for mode " << mode << '\n';
            return 1;
        }

    std::cout << "Tape Echo DSP tests passed\n";
    return 0;
}
