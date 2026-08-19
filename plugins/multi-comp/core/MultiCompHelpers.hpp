// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Framework-free transcriptions of the JUCE processor's detector helpers.
#pragma once

#include "MultiCompParams.hpp"
#include "../../shared-dpf/dsp/DuskOversampler.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace duskaudio
{

class MultiCompSidechainFilter
{
public:
    void prepare(double rate) noexcept
    {
        sampleRate = rate > 0.0 ? rate : 44100.0;
        reset();
        updateCoefficients(80.0f);
    }

    void setFrequency(float frequency) noexcept
    {
        const float f = std::clamp(frequency, 20.0f, 500.0f);
        if (std::abs(f - currentFrequency) > 0.1f) updateCoefficients(f);
    }

    float process(float input) noexcept
    {
        const float output = b0 * input + z1;
        z1 = b1 * input - a1 * output + z2;
        z2 = b2 * input - a2 * output;
        return output;
    }

    void reset() noexcept { z1 = z2 = 0.0f; }

private:
    void updateCoefficients(float frequency) noexcept
    {
        currentFrequency = frequency;
        const float omega = 2.0f * 3.14159265358979323846f * frequency / static_cast<float>(sampleRate);
        const float c = std::cos(omega), s = std::sin(omega);
        const float alpha = s / (2.0f * 0.707f);
        const float invA0 = 1.0f / (1.0f + alpha);
        b0 = ((1.0f + c) * 0.5f) * invA0;
        b1 = -(1.0f + c) * invA0;
        b2 = b0;
        a1 = -2.0f * c * invA0;
        a2 = (1.0f - alpha) * invA0;
    }

    double sampleRate = 44100.0;
    float currentFrequency = 80.0f;
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};

class MultiCompTransientShaper
{
public:
    void prepare(double rate) noexcept
    {
        sampleRate = rate > 0.0 ? rate : 44100.0;
        fastAttack = std::exp(-1.0f / (0.0005f * static_cast<float>(sampleRate)));
        fastRelease = std::exp(-1.0f / (0.020f * static_cast<float>(sampleRate)));
        slowAttack = std::exp(-1.0f / (0.010f * static_cast<float>(sampleRate)));
        slowRelease = std::exp(-1.0f / (0.100f * static_cast<float>(sampleRate)));
        holdSamples = static_cast<int>(0.005f * static_cast<float>(sampleRate));
        reset();
    }

    float process(float input, int channel, float sensitivity) noexcept
    {
        if (channel < 0 || channel >= 2) return 1.0f;
        auto& state = channels[static_cast<size_t>(channel)];
        const float absolute = std::abs(input);
        state.fast = absolute > state.fast ? fastAttack * state.fast + (1.0f - fastAttack) * absolute
                                            : fastRelease * state.fast + (1.0f - fastRelease) * absolute;
        state.slow = absolute > state.slow ? slowAttack * state.slow + (1.0f - slowAttack) * absolute
                                           : slowRelease * state.slow + (1.0f - slowRelease) * absolute;
        if (absolute > state.peak) { state.peak = absolute; state.hold = holdSamples; }
        else if (state.hold > 0) --state.hold;
        else state.peak *= 0.9995f;
        const float ratio = state.slow > 0.0001f ? state.fast / state.slow : 1.0f;
        const float amount = std::clamp(sensitivity / 100.0f, 0.0f, 1.0f);
        return ratio > 1.0f ? 1.0f + std::min((ratio - 1.0f) * 2.0f, 2.0f) * amount : 1.0f;
    }

    void reset() noexcept { for (auto& state : channels) state = Channel{}; }

private:
    struct Channel { float fast = 0.0f, slow = 0.0f, peak = 0.0f; int hold = 0; };
    double sampleRate = 44100.0;
    std::array<Channel, 2> channels{};
    float fastAttack = 0.0f, fastRelease = 0.0f, slowAttack = 0.0f, slowRelease = 0.0f;
    int holdSamples = 0;
};

class MultiCompTruePeakDetector
{
public:
    static constexpr int TAPS_PER_PHASE = 12;
    void prepare() noexcept { for (auto& c : channels) c = Channel{}; initialize(); }
    void setOversamplingFactor(int factor) noexcept { oversamplingFactor = factor == 1 ? 8 : 4; }
    float processSample(float sample, int channel) noexcept
    {
        if (channel < 0 || channel >= 2) return std::abs(sample);
        auto& state = channels[static_cast<size_t>(channel)];
        state.history[state.index] = sample;
        state.index = (state.index + 1) & 15;
        float peak = std::abs(sample);
        const int phases = oversamplingFactor == 4 ? 4 : 8;
        for (int phase = 1; phase < phases; ++phase)
        {
            const auto& coeff = phases == 4 ? coefficients4[static_cast<size_t>(phase)] : coefficients8[static_cast<size_t>(phase)];
            float value = 0.0f;
            for (int i = 0; i < TAPS_PER_PHASE; ++i)
                value += state.history[static_cast<size_t>((state.index - TAPS_PER_PHASE + i + 16) & 15)] * coeff[static_cast<size_t>(i)];
            peak = std::max(peak, std::abs(value));
        }
        state.peak = peak;
        return peak;
    }
    float getTruePeak(int channel) const noexcept { return channel >= 0 && channel < 2 ? channels[static_cast<size_t>(channel)].peak : 0.0f; }
    int getLatency() const noexcept { return TAPS_PER_PHASE / 2; }

private:
    struct Channel { std::array<float, 16> history{}; float peak = 0.0f; int index = 0; };
    std::array<Channel, 2> channels{};
    int oversamplingFactor = 4;
    std::array<std::array<float, TAPS_PER_PHASE>, 4> coefficients4{};
    std::array<std::array<float, TAPS_PER_PHASE>, 8> coefficients8{};
    void initialize() noexcept
    {
        coefficients4[0] = {{0.0000f,-0.0015f,0.0076f,-0.0251f,0.0700f,-0.3045f,0.9722f,0.3045f,-0.0700f,0.0251f,-0.0076f,0.0015f}};
        coefficients4[1] = {{-0.0005f,0.0027f,-0.0105f,0.0330f,-0.1125f,0.7265f,0.7265f,-0.1125f,0.0330f,-0.0105f,0.0027f,-0.0005f}};
        coefficients4[2] = {{0.0015f,-0.0076f,0.0251f,-0.0700f,0.3045f,0.9722f,-0.3045f,0.0700f,-0.0251f,0.0076f,-0.0015f,0.0000f}};
        coefficients4[3] = {{-0.0010f,0.0055f,-0.0178f,0.0514f,-0.1755f,0.8940f,0.5260f,-0.0900f,0.0280f,-0.0092f,0.0023f,-0.0003f}};
        coefficients8[0] = {{0.0000f,-0.0008f,0.0038f,-0.0126f,0.0350f,-0.1523f,0.9861f,0.1523f,-0.0350f,0.0126f,-0.0038f,0.0008f}};
        coefficients8[1] = {{-0.0002f,0.0011f,-0.0045f,0.0147f,-0.0503f,0.3245f,0.9352f,0.0650f,-0.0175f,0.0063f,-0.0019f,0.0003f}};
        coefficients8[2] = {{-0.0004f,0.0020f,-0.0078f,0.0245f,-0.0837f,0.5405f,0.8415f,-0.0180f,0.0030f,0.0000f,-0.0005f,0.0000f}};
        coefficients8[3] = coefficients4[1];
        coefficients8[4] = {{0.0000f,-0.0005f,0.0000f,0.0030f,-0.0180f,0.8415f,0.5405f,-0.0837f,0.0245f,-0.0078f,0.0020f,-0.0004f}};
        coefficients8[5] = {{0.0003f,-0.0019f,0.0063f,-0.0175f,0.0650f,0.9352f,0.3245f,-0.0503f,0.0147f,-0.0045f,0.0011f,-0.0002f}};
        coefficients8[6] = {{0.0008f,-0.0038f,0.0126f,-0.0350f,0.1523f,0.9861f,0.1523f,-0.0350f,0.0126f,-0.0038f,0.0008f,0.0000f}};
        coefficients8[7] = {{0.0005f,-0.0028f,0.0095f,-0.0270f,0.1050f,0.9650f,0.2380f,-0.0420f,0.0137f,-0.0042f,0.0010f,-0.0001f}};
    }
};

class MultiCompAntiAliasing
{
public:
    void prepare(int maxBlock) noexcept
    {
        (void)maxBlock;
        Oversampler maxOversampler;
        maxOversampler.setFactor(4);
        maxLatency = static_cast<int>(std::lround(maxOversampler.latency()));
        compensation.assign(static_cast<size_t>(std::max(1, maxLatency + 1)), 0.0f);
        writePosition = 0;
        setFactor(factor);
    }
    void setFactor(int factorValue) noexcept { factor = factorValue; oversamplingOff = factorValue <= 1; use4x = factorValue >= 4; oversampler.setFactor(oversamplingOff ? 1 : (use4x ? 4 : 2)); }
    void reset() noexcept { oversampler.reset(); std::fill(compensation.begin(), compensation.end(), 0.0f); writePosition = 0; }
    template <class Fn> float process(float input, Fn&& fn) noexcept
    {
        const float wet = oversampler.processSample(input, static_cast<Fn&&>(fn));
        if (compensation.empty() || maxLatency <= 0) return wet;
        const int current = oversamplingOff ? 0 : (use4x ? 27 : 23);
        const int delay = std::clamp(maxLatency - current, 0, maxLatency);
        compensation[static_cast<size_t>(writePosition)] = wet;
        const int read = (writePosition - delay + static_cast<int>(compensation.size())) % static_cast<int>(compensation.size());
        const float result = compensation[static_cast<size_t>(read)];
        writePosition = (writePosition + 1) % static_cast<int>(compensation.size());
        return delay > 0 ? result : wet;
    }
    template <class Fn> float processSample(float input, Fn&& fn) noexcept { return process(input, static_cast<Fn&&>(fn)); }
    float latency() const noexcept { return static_cast<float>(maxLatency); }
    bool isOversamplingOff() const noexcept { return oversamplingOff; }
private:
    Oversampler oversampler;
    bool oversamplingOff = false, use4x = false;
    std::vector<float> compensation;
    int maxLatency = 27, writePosition = 0, factor = 2;
};

class MultiCompLookupTables
{
public:
    static constexpr int kAllButtonsSize = 512;
    void prepare() noexcept
    {
        constexpr float points[][2] = {{0.0f,0.0f},{1.0f,0.9f},{2.0f,1.9f},{4.0f,3.85f},{6.0f,5.8f},
                                       {8.0f,7.8f},{10.0f,9.8f},{15.0f,14.8f},{20.0f,19.7f},{30.0f,29.5f}};
        for (int i = 0; i < kAllButtonsSize; ++i)
        {
            const float over = 30.0f * static_cast<float>(i) / static_cast<float>(kAllButtonsSize - 1);
            modern[static_cast<size_t>(i)] = over < 1.0f ? over * (0.7f + over * 0.28f) : 0.98f + (over - 1.0f) * 0.99f;
            modern[static_cast<size_t>(i)] = std::min(modern[static_cast<size_t>(i)], 30.0f);
            float measuredReduction = 0.0f;
            for (int p = 0; p < 9; ++p)
                if (over >= points[p][0] && over <= points[p + 1][0])
                {
                    const float t = (over - points[p][0]) / (points[p + 1][0] - points[p][0]);
                    measuredReduction = points[p][1] + t * (points[p + 1][1] - points[p][1]);
                    break;
                }
            measured[static_cast<size_t>(i)] = over > 30.0f ? 29.5f : measuredReduction;
        }
    }

    float getAllButtonsReduction(float overThresholdDb, bool measuredCurve) const noexcept
    {
        const float index = std::clamp(overThresholdDb, 0.0f, 30.0f) * static_cast<float>(kAllButtonsSize - 1) / 30.0f;
        const int i0 = static_cast<int>(index), i1 = std::min(i0 + 1, kAllButtonsSize - 1);
        const float fraction = index - static_cast<float>(i0);
        const auto& curve = measuredCurve ? measured : modern;
        return curve[static_cast<size_t>(i0)] + fraction * (curve[static_cast<size_t>(i1)] - curve[static_cast<size_t>(i0)]);
    }

private:
    std::array<float, kAllButtonsSize> modern{}, measured{};
};

inline float applyCoreDistortion(float input, DistortionType type, float amount) noexcept
{
    if (type == DistortionType::Off || amount <= 0.0f) return input;
    float wet = input;
    if (type == DistortionType::Soft) wet = std::tanh(input * (1.0f + amount));
    else if (type == DistortionType::Hard)
    {
        float threshold = std::min(0.7f / (0.5f + amount * 0.5f), 0.95f);
        const float negThreshold = threshold * 0.9f;
        if (wet > threshold) { const float diff = wet - threshold, n = diff / (1.0f - threshold); wet = threshold + diff / (1.0f + n * n); }
        else if (wet < -negThreshold) { const float diff = std::abs(wet) - negThreshold, n = diff / (1.0f - negThreshold); wet = -negThreshold - diff / (1.0f + n * n); }
    }
    else if (type == DistortionType::Clip)
    {
        const float limit = 1.0f / (0.5f + amount * 0.5f);
        wet = std::clamp(input, -limit, limit);
    }
    return wet;
}

} // namespace duskaudio
