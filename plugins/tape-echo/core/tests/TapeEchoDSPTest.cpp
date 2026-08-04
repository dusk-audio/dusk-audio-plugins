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

MeterResult renderWithBlockSize(int blockSize)
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
    if (dsp.getRecordPeakLevel() != peakBeforeZeroBlock)
        return {};

    return { dsp.getRecordVuLevel(), dsp.getRecordPeakLevel() };
}

bool approximatelyEqual(float a, float b) noexcept
{
    return std::abs(a - b) <= 1.0e-6f * std::max(1.0f, std::max(std::abs(a), std::abs(b)));
}
}

int main()
{
    const MeterResult oneSample = renderWithBlockSize(1);
    const MeterResult smallBlock = renderWithBlockSize(64);
    const MeterResult wholeRender = renderWithBlockSize(kSamples);

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

    std::cout << "Tape Echo meter tests passed\n";
    return 0;
}
