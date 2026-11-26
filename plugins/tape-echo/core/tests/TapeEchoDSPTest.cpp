#include "TapeEchoDSP.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace duskaudio
{
// The record path's oversampling stages and the detector's Hilbert pair as
// prepare() set them up, so the tests below check the production
// coefficients rather than a copy of them.
struct TapeEchoDSPTestAccess
{
    using Hilbert = TapeEchoDSP::HilbertPair;
    static auto outerUp(const TapeEchoDSP& dsp)   { return dsp.channels[0].feedUp1; }
    static auto innerUp(const TapeEchoDSP& dsp)   { return dsp.channels[0].feedUp2; }
    static auto innerDown(const TapeEchoDSP& dsp) { return dsp.channels[0].tapeDown2; }
    static auto outerDown(const TapeEchoDSP& dsp) { return dsp.channels[0].tapeDown1; }
};
} // namespace duskaudio

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr int kSamples = 4096;
constexpr double kTwoPi = 6.283185307179586476925;
using Access = duskaudio::TapeEchoDSPTestAccess;

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

// Idle noise floor at Tape Age Old: one channel of output for silent input.
std::vector<double> renderNoiseFloor(float noise01, bool hum50, int mode = 1)
{
    constexpr int kSettle = 48000, kLength = 96000, kBlock = 256;
    duskaudio::TapeEchoDSP dsp;
    dsp.setMode(mode);
    dsp.setTapeAge(1.0f);
    dsp.setNoise(noise01);
    dsp.setHumFrequency50(hum50);
    dsp.prepare(kSampleRate, kBlock);

    const std::vector<float> silence(kBlock, 0.0f);
    std::vector<float> left(kBlock), right(kBlock);
    std::vector<double> captured;
    captured.reserve(kLength);
    for (int offset = 0; offset < kSettle + kLength; offset += kBlock)
    {
        const float* inputs[] = { silence.data(), silence.data() };
        float* outputs[] = { left.data(), right.data() };
        dsp.processBlock(inputs, outputs, 2, kBlock);
        if (offset >= kSettle)
            captured.insert(captured.end(), left.begin(), left.end());
    }
    captured.resize(kLength);
    return captured;
}

double rmsOf(const std::vector<double>& x)
{
    double sum = 0.0;
    for (const double v : x)
        sum += v * v;
    return std::sqrt(sum / static_cast<double>(x.size()));
}

// Single-bin magnitude; the capture length makes 250 and 300 Hz exact bins.
double toneMagnitude(const std::vector<double>& x, double hz)
{
    const double w = 2.0 * 3.14159265358979323846 * hz / kSampleRate;
    double re = 0.0, im = 0.0;
    for (size_t n = 0; n < x.size(); ++n)
    {
        re += x[n] * std::cos(w * static_cast<double>(n));
        im += x[n] * std::sin(w * static_cast<double>(n));
    }
    return std::sqrt(re * re + im * im);
}

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

// First arrival (first sample within 40 dB of the impulse's peak), in seconds,
// of the spring alone at a given host rate. Negative when the render is invalid:
// any non-finite sample, or no arrival at all (a silent render).
double springOnsetSeconds(double rate)
{
    duskaudio::SpringReverb spring;
    spring.prepare(rate, 1.0f);
    const int samples = (int)std::lround(0.2 * rate);
    std::vector<float> output((size_t)samples, 0.0f);
    float peak = 0.0f;
    for (int i = 0; i < samples; ++i)
    {
        const float sample = spring.process(i == 0 ? 1.0f : 0.0f);
        if (!std::isfinite(sample))
            return -1.0;
        output[(size_t)i] = sample;
        peak = std::max(peak, std::abs(sample));
    }
    if (peak <= 0.0f)
        return -1.0;
    for (int i = 0; i < samples; ++i)
        if (std::abs(output[(size_t)i]) > 0.01f * peak)
            return (double)i / rate;
    return -1.0;
}

// One channel pair of output from TapeEchoDSP for a fixed stereo programme in
// mode 11 (three heads, regeneration, spring), rendered in calls of
// blockSize samples. inPlace passes the output buffers as the input too, the
// way hosts that process in place do.
std::array<std::vector<float>, 2> renderMode11(int blockSize, bool inPlace)
{
    constexpr int renderSamples = 24000;
    duskaudio::TapeEchoDSP dsp;
    dsp.setMode(11);
    dsp.setIntensity(0.6f);
    dsp.setReverbLevel(0.6f);
    dsp.setTapeAge(1.0f);
    dsp.setRepeatRate(0.8f);
    dsp.prepare(kSampleRate, blockSize);

    std::array<std::vector<float>, 2> input{
        std::vector<float>(renderSamples, 0.0f),
        std::vector<float>(renderSamples, 0.0f) };
    uint32_t seed = 987654321u;
    for (int i = 0; i < 6000; ++i)
        for (int c = 0; c < 2; ++c)
        {
            seed = seed * 1664525u + 1013904223u;
            input[(size_t)c][(size_t)i] =
                0.4f * (float)(int32_t)seed * (1.0f / 2147483648.0f);
        }
    std::array<std::vector<float>, 2> output = input;
    std::array<std::vector<float>, 2> scratch{
        std::vector<float>(renderSamples, 0.0f),
        std::vector<float>(renderSamples, 0.0f) };
    for (int offset = 0; offset < renderSamples; offset += blockSize)
    {
        const int count = std::min(blockSize, renderSamples - offset);
        float* outs[] = { (inPlace ? output : scratch)[0].data() + offset,
                          (inPlace ? output : scratch)[1].data() + offset };
        const float* ins[] = { (inPlace ? output : input)[0].data() + offset,
                               (inPlace ? output : input)[1].data() + offset };
        dsp.processBlock(ins, outs, 2, count);
    }
    return inPlace ? output : scratch;
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

float sineAt(double hz, double rate, size_t n)
{
    return (float)std::sin(kTwoPi * hz * (double)n / rate);
}

// Level in dB of DFT bin `bin` over x[offset, offset + length), scaled so a
// unit sine on that bin reads 0 dB.
double binLevelDb(const std::vector<float>& x, size_t offset, size_t length, size_t bin)
{
    double re = 0.0, im = 0.0;
    for (size_t i = 0; i < length; ++i)
    {
        const double phase = kTwoPi * (double)bin * (double)i / (double)length;
        re += (double)x[offset + i] * std::cos(phase);
        im -= (double)x[offset + i] * std::sin(phase);
    }
    return 20.0 * std::log10(std::max(2.0 * std::hypot(re, im) / (double)length, 1.0e-30));
}

// The record path's x4 oversampling, stage by stage, with the coefficients
// prepare() loads at 48 kHz. Passbands are unity, and images (upsamplers) and
// folds (decimators) sit at the designs' depth: measured -115.2 dB worst for
// the outer stages (from 27 kHz) and -107.7 dB for the inner ones (from
// 58.5 kHz at 192 kHz), on these grids. A swapped or mistyped coefficient
// costs tens of dB. Every tone sits on a 10 Hz bin centre at every rate (N
// samples at 48 kHz, 2N at 96, 4N at 192), so the plain DFT leaks nothing;
// each stage runs one window to settle before the measured one.
bool recordOversamplingMeetsDesign()
{
    duskaudio::TapeEchoDSP dsp;
    dsp.prepare(48000.0, 512);
    constexpr size_t N = 4800;
    constexpr double kPassDb = 1.0e-4;
    constexpr double kOuterStopDb = -114.5;
    constexpr double kInnerStopDb = -107.0;
    const auto bin10 = [](double hz) { return (size_t)std::lround(hz / 10.0); };
    double worstPass = 0.0, worstOuter = -999.0, worstInner = -999.0;
    const auto pass = [&](double db) { worstPass = std::max(worstPass, std::abs(db)); };

    // Outer upsampler, 48 -> 96 kHz: image at 48 kHz - f.
    for (double f = 10.0; f <= 21000.0; f += 170.0)
    {
        auto up = Access::outerUp(dsp);
        up.reset();
        std::vector<float> y;
        for (size_t n = 0; n < 2 * N; ++n)
        {
            float a0 = 0.0f, a1 = 0.0f, b0 = 0.0f, b1 = 0.0f;
            up.process(sineAt(f, 48000.0, n), 0.0f, a0, a1, b0, b1);
            y.push_back(a0);
            y.push_back(a1);
        }
        pass(binLevelDb(y, 2 * N, 2 * N, bin10(f)));
        worstOuter = std::max(worstOuter, binLevelDb(y, 2 * N, 2 * N, bin10(48000.0 - f)));
    }
    // Inner upsampler, 96 -> 192 kHz, on what the outer stage passes and
    // its transition band: image at 96 kHz - f.
    for (double f = 10.0; f <= 37490.0; f += 370.0)
    {
        auto up = Access::innerUp(dsp);
        up.reset();
        std::vector<float> y;
        for (size_t n = 0; n < 4 * N; ++n)
        {
            float a0 = 0.0f, a1 = 0.0f, b0 = 0.0f, b1 = 0.0f;
            up.process(sineAt(f, 96000.0, n), 0.0f, a0, a1, b0, b1);
            y.push_back(a0);
            y.push_back(a1);
        }
        pass(binLevelDb(y, 4 * N, 4 * N, bin10(f)));
        worstInner = std::max(worstInner, binLevelDb(y, 4 * N, 4 * N, bin10(96000.0 - f)));
    }
    // Inner decimator, 192 -> 96 kHz: fold at 96 kHz - f.
    const auto innerDecimate = [&](double f) {
        auto down = Access::innerDown(dsp);
        down.reset();
        std::vector<float> y;
        for (size_t n = 0; n < 4 * N; ++n)
            y.push_back(down.process(sineAt(f, 192000.0, 2 * n), sineAt(f, 192000.0, 2 * n + 1)));
        return y;
    };
    for (const double f : { 1000.0, 21000.0, 27000.0 })
        pass(binLevelDb(innerDecimate(f), 2 * N, 2 * N, bin10(f)));
    for (double f = 58500.0; f <= 95990.0; f += 370.0)
        worstInner = std::max(worstInner,
            binLevelDb(innerDecimate(f), 2 * N, 2 * N, bin10(96000.0 - f)));
    // Outer decimator, 96 -> 48 kHz: fold at 48 kHz - f.
    const auto outerDecimate = [&](double f) {
        auto down = Access::outerDown(dsp);
        down.reset();
        std::vector<float> y;
        for (size_t n = 0; n < 2 * N; ++n)
            y.push_back(down.process(sineAt(f, 96000.0, 2 * n), sineAt(f, 96000.0, 2 * n + 1)));
        return y;
    };
    for (const double f : { 20.0, 1000.0, 20000.0, 21000.0 })
        pass(binLevelDb(outerDecimate(f), N, N, bin10(f)));
    for (double f = 27000.0; f <= 47990.0; f += 170.0)
        worstOuter = std::max(worstOuter,
            binLevelDb(outerDecimate(f), N, N, bin10(48000.0 - f)));
    // The whole round trip as the record path runs it: the pair upsamplers
    // carry one signal here, and the inner one runs once per 2x sample.
    for (const double f : { 20.0, 100.0, 1000.0, 5000.0, 10000.0, 15000.0, 20000.0, 21000.0 })
    {
        auto up1 = Access::outerUp(dsp);
        auto up2 = Access::innerUp(dsp);
        auto down2 = Access::innerDown(dsp);
        auto down1 = Access::outerDown(dsp);
        up1.reset();
        up2.reset();
        down2.reset();
        down1.reset();
        std::vector<float> y;
        for (size_t n = 0; n < 2 * N; ++n)
        {
            float h0 = 0.0f, h1 = 0.0f, unused0 = 0.0f, unused1 = 0.0f;
            float q[4] = {};
            up1.process(sineAt(f, 48000.0, n), 0.0f, h0, h1, unused0, unused1);
            up2.process(h0, 0.0f, q[0], q[1], unused0, unused1);
            up2.process(h1, 0.0f, q[2], q[3], unused0, unused1);
            // In sample order: as two arguments of one call the decimator's
            // calls would run in an unspecified order.
            const float early = down2.process(q[0], q[1]);
            const float late = down2.process(q[2], q[3]);
            y.push_back(down1.process(early, late));
        }
        pass(binLevelDb(y, N, N, bin10(f)));
    }

    if (worstPass > kPassDb || worstOuter > kOuterStopDb || worstInner > kInnerStopDb)
    {
        std::cerr << "record oversampling off its design: passband " << worstPass
                  << " dB (limit " << kPassDb << "), outer stopband " << worstOuter
                  << " dB (limit " << kOuterStopDb << "), inner stopband " << worstInner
                  << " dB (limit " << kInnerStopDb << ")\n";
        return false;
    }
    return true;
}

// BiquadLanes runs every lane through Biquad::process's operations in its
// order, so each lane must match a chain of Biquads bit for bit, and a lane
// left at zero coefficients must output zero.
bool biquadLanesMatchScalarChains()
{
    constexpr int kSections = 3;
    constexpr int kLanes = 5;
    constexpr int kIdleLane = kLanes - 1;
    duskaudio::BiquadLanes<kSections, kLanes> bank;
    duskaudio::Biquad chains[kSections][kIdleLane];
    for (int s = 0; s < kSections; ++s)
        for (int l = 0; l < kIdleLane; ++l)
        {
            const duskaudio::BiquadCoeffs k = l == 2
                ? duskaudio::Biquad::lowPass(48000.0, 3000.0f + 2000.0f * (float)s, 0.707f)
                : duskaudio::Biquad::peak(48000.0, 200.0f * (float)(1 + s + 3 * l),
                                          6.0f - 4.0f * (float)s, 0.5f + 0.3f * (float)l);
            bank.setCoeffs(s, l, k);
            chains[s][l].setCoeffs(k);
        }
    uint32_t seed = 7u;
    for (int n = 0; n < 20000; ++n)
    {
        float lanes[kLanes];
        float scalar[kLanes];
        for (int l = 0; l < kLanes; ++l)
        {
            seed = seed * 1664525u + 1013904223u;
            lanes[l] = scalar[l] = (float)((double)seed / 4294967296.0 * 2.0 - 1.0);
        }
        bank.process(lanes);
        for (int l = 0; l < kIdleLane; ++l)
        {
            for (int s = 0; s < kSections; ++s)
                scalar[l] = chains[s][l].process(scalar[l]);
            if (std::memcmp(&scalar[l], &lanes[l], sizeof(float)) != 0)
            {
                std::cerr << "BiquadLanes lane " << l << " differs from its Biquad chain at sample "
                          << n << ": " << lanes[l] << " vs " << scalar[l] << '\n';
                return false;
            }
        }
        if (lanes[kIdleLane] != 0.0f)
        {
            std::cerr << "BiquadLanes idle lane output " << lanes[kIdleLane] << '\n';
            return false;
        }
    }
    return true;
}

// The detector's Hilbert pair reads a sinusoid's amplitude, sqrt(i^2 + q^2),
// within 0.65 % at any phase (worst measured 0.615 %): from 20 Hz to 22 kHz at
// 44.1 kHz, the band the design covers, and from 700 Hz (the detector's
// high-pass, ahead of the pair) to 20 kHz at every rate up to 192 kHz.
bool detectorHilbertReadsAmplitude()
{
    struct Band { double rate, lowHz, highHz; };
    constexpr Band kBands[] = { { 44100.0, 20.0, 22000.0 }, { 48000.0, 700.0, 20000.0 },
                                { 96000.0, 700.0, 20000.0 }, { 192000.0, 700.0, 20000.0 } };
    constexpr double kLimit = 0.0065;
    const double step = std::pow(2.0, 1.0 / 16.0);
    for (const Band& band : kBands)
        for (double f = band.lowHz;; f = std::min(f * step, band.highHz))
        {
            Access::Hilbert hilbert;
            const size_t total = (size_t)(0.5 * band.rate);
            double lowest = 1.0e9, highest = 0.0;
            for (size_t n = 0; n < total; ++n)
            {
                float i = 0.0f, q = 0.0f;
                hilbert.process(sineAt(f, band.rate, n), i, q);
                if (n >= total / 2)
                {
                    const double amplitude = std::hypot((double)i, (double)q);
                    lowest = std::min(lowest, amplitude);
                    highest = std::max(highest, amplitude);
                }
            }
            if (highest - 1.0 > kLimit || 1.0 - lowest > kLimit)
            {
                std::cerr << "detector Hilbert pair at " << band.rate << " Hz reads a " << f
                          << " Hz sine as " << lowest << ".." << highest << " (limit +-"
                          << kLimit << ")\n";
                return false;
            }
            if (f >= band.highHz)
                break;
        }
    return true;
}
}

int main()
{
    if (!recordOversamplingMeetsDesign() || !biquadLanesMatchScalarChains()
        || !detectorHilbertReadsAmplitude())
        return 1;

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

    // The tank runs at a reduced internal rate behind host-rate resampling
    // whose latency is taken out of the transit. If that latency were wrong,
    // the arrival would move with the host rate (the factor is 4 at 44.1/48,
    // 8 at 88.2/96 and 16 at 192 kHz), so pin it to within 0.1 ms of 48 kHz.
    // The internal-rate sample grid itself is ~0.09 ms, hence the bound.
    {
        const double reference = springOnsetSeconds(48000.0);
        if (!(reference >= 0.0))
        {
            std::cerr << "spring onset render invalid at 48 kHz "
                         "(non-finite or silent)\n";
            return 1;
        }
        // 22.05 kHz (one stage) and 32 kHz (16 kHz internal, runtime cliff)
        // are pinned too. Hosts below 20 kHz are not resampled and their
        // internal sample grid (0.09-0.125 ms) is coarser than this bound.
        for (double rate : { 22050.0, 32000.0, 44100.0, 88200.0, 96000.0, 192000.0 })
        {
            const double onset = springOnsetSeconds(rate);
            if (!(onset >= 0.0))
            {
                std::cerr << "spring onset render invalid at " << rate
                          << " Hz (non-finite or silent)\n";
                return 1;
            }
            if (!(std::abs(onset - reference) <= 0.0001))
            {
                std::cerr << "spring first arrival moves with the host rate: "
                          << onset * 1000.0 << " ms at " << rate << " Hz vs "
                          << reference * 1000.0 << " ms at 48 kHz\n";
                return 1;
            }
        }
    }

    // The per-sample work runs in passes over 128-sample sub-blocks, and every
    // filter keeps its own history across them: the output must not depend
    // on where host blocks split those sub-blocks, nor on the host passing the
    // output buffers as the input (in-place processing). Mode 11 keeps all
    // three heads, the regenerating loop and the spring busy.
    {
        const auto at128 = renderMode11(128, false);
        const auto at129 = renderMode11(129, false);
        const auto inPlace129 = renderMode11(129, true);
        bool heard = false;
        for (int c = 0; c < 2; ++c)
            for (size_t i = 0; i < at128[(size_t)c].size(); ++i)
            {
                heard = heard || std::abs(at128[(size_t)c][i]) > 1.0e-4f;
                if (std::memcmp(&at128[(size_t)c][i], &at129[(size_t)c][i], sizeof(float)) != 0
                    || std::memcmp(&at128[(size_t)c][i], &inPlace129[(size_t)c][i], sizeof(float)) != 0)
                {
                    std::cerr << "mode 11 output depends on block split or in-place "
                                 "buffers at channel " << c << " sample " << i << ": "
                              << at128[(size_t)c][i] << " / " << at129[(size_t)c][i]
                              << " / " << inPlace129[(size_t)c][i] << '\n';
                    return 1;
                }
            }
        if (!heard)
        {
            std::cerr << "mode 11 block-split check rendered silence\n";
            return 1;
        }
    }

    // Noise spans the captured floor (0) to 60 dB above it (1), and the hum
    // selector moves the fifth harmonic, the strongest line, from 300 to 250 Hz.
    const std::vector<double> floor60 = renderNoiseFloor(0.0f, false);
    const std::vector<double> loud60 = renderNoiseFloor(1.0f, false);
    const std::vector<double> loud50 = renderNoiseFloor(1.0f, true);
    const double noiseLiftDb = 20.0 * std::log10(rmsOf(loud60) / rmsOf(floor60));
    const double fifth60 = toneMagnitude(loud60, 300.0) / toneMagnitude(loud60, 250.0);
    const double fifth50 = toneMagnitude(loud50, 250.0) / toneMagnitude(loud50, 300.0);
    // Reverb Only selects no playback head, so the repro bed must be absent.
    const double reverbOnlyNoise = rmsOf(renderNoiseFloor(1.0f, false, 12));
    if (rmsOf(floor60) <= 0.0 || std::fabs(noiseLiftDb - 60.0) > 0.5
        || fifth60 < 4.0 || fifth50 < 4.0
        || reverbOnlyNoise > 1.0e-3 * rmsOf(floor60))
    {
        std::cerr << "noise bed control failed: lift=" << noiseLiftDb
                  << " dB fifth60=" << fifth60 << " fifth50=" << fifth50
                  << " reverbOnly=" << reverbOnlyNoise << '\n';
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
            1.0f, 1.905322f, 2.759947f, 1.905322f,
            1.0f, 1.905322f, 2.759947f, 1.0f,
            1.905322f, 1.0f, 1.0f, 1.0f };
    constexpr std::array<float, duskaudio::TapeEchoDSP::kNumModes>
        leadingHeadOffsets{
            0.0f, -0.6929f, -0.9351f, -0.6929f,
            0.0f, -0.6929f, -0.9351f, 0.0f,
            -0.6929f, 0.0f, 0.0f, 0.0f };
    for (int mode = 1; mode <= duskaudio::TapeEchoDSP::kNumModes; ++mode)
        if (duskaudio::TapeEchoDSP::leadingHeadRatioForMode(mode)
                != leadingHeadRatios[static_cast<size_t>(mode - 1)]
            || duskaudio::TapeEchoDSP::leadingHeadOffsetMsForMode(mode)
                != leadingHeadOffsets[static_cast<size_t>(mode - 1)])
        {
            std::cerr << "wrong leading-head timing for mode " << mode << '\n';
            return 1;
        }

    // POWER off holds the loop silent. The repro noise bed feeds the loop
    // tap, so a bed that POWER does not gate self-starts a high-Intensity
    // loop while the user hears dry, and the tape is full when POWER returns
    // (measured -24 dBFS in the first 100 ms at Intensity 0.9).
    {
        constexpr int block = 256;
        duskaudio::TapeEchoDSP dsp;
        dsp.setIntensity(0.9f);
        dsp.setRepeatRate(1.0f);
        dsp.prepare(kSampleRate, block);
        dsp.setBypass(true);
        std::array<std::vector<float>, 2> silence{
            std::vector<float>(block, 0.0f), std::vector<float>(block, 0.0f) };
        std::array<std::vector<float>, 2> output{
            std::vector<float>(block, 0.0f), std::vector<float>(block, 0.0f) };
        const float* inputs[] = { silence[0].data(), silence[1].data() };
        float* outputs[] = { output[0].data(), output[1].data() };
        for (int n = 0; n < static_cast<int>(10.0 * kSampleRate) / block; ++n)
            dsp.processBlock(inputs, outputs, 2, block);
        dsp.setBypass(false);
        float peak = 0.0f;
        for (int n = 0; n < static_cast<int>(0.1 * kSampleRate) / block; ++n)
        {
            dsp.processBlock(inputs, outputs, 2, block);
            for (const auto& channel : output)
                for (const float v : channel)
                    peak = std::max(peak, std::abs(v));
        }
        if (!(peak < 1.0e-4f))
        {
            std::cerr << "loop ran while POWER was off: peak " << peak
                      << " in the first 100 ms after POWER on\n";
            return 1;
        }
    }

    std::cout << "Tape Echo DSP tests passed\n";
    return 0;
}
