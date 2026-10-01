// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
// Local 2x oversampling around the Opto output stage (MultiCompOptoOutputStage.hpp).
//
// The stage's curve carries the reference's small-signal harmonics, and at the
// mode rate (96 kHz with the default 2x setting) a 20 kHz tone's upper
// harmonics fold back into the audio band (4 kHz at -84 dBFS, 16 kHz at
// -90 dBFS).  The reference does not alias.  The curve therefore runs at twice
// the mode rate between two 23-tap linear-phase half-band filters (Kaiser
// beta 8: 0.001 dB passband ripple to 24 kHz for the pair, -81 dB stopband).
//
// Latency is constant across the oversampling settings, as for the rest of
// the plugin: kOptoStageLatencyHost host samples.  With oversampling off the
// half-band pair cannot be flat to the host Nyquist, so the curve then runs at
// the host rate and only the delay is applied.
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>

namespace duskaudio
{
inline constexpr int kOptoStageLatencyHost = 6;

class OptoStageOversampler
{
public:
    // Half-band even-index taps (the centre tap is 0.5, odd offsets are 0);
    // normalised so the even taps sum to exactly 0.5 (unity DC both ways).
    static constexpr std::array<float, 12> kEven{{
        -6.7672990620e-05f, 1.5787272547e-03f, -8.3598648108e-03f,
        2.8201934508e-02f, -7.9925065056e-02f, 3.0857194109e-01f,
        3.0857194109e-01f, -7.9925065056e-02f, 2.8201934508e-02f,
        -8.3598648108e-03f, 1.5787272547e-03f, -6.7672990620e-05f}};
    // The up/down pair delays by 22 samples at the local rate: 11 mode-rate samples.
    static constexpr int kFilterDelay = 11;
    static constexpr int kMaxFactor = 4;
    static constexpr int kMaxDelay = kOptoStageLatencyHost * kMaxFactor;

    void setFactor(int oversamplingFactor) noexcept
    {
        factor = std::clamp(oversamplingFactor, 1, kMaxFactor);
        const int target = kOptoStageLatencyHost * factor;
        active = factor >= 2;
        pad = target - (active ? kFilterDelay : 0);
    }

    void reset() noexcept
    {
        input.fill(0.0f);
        phase0.fill(0.0f);
        phase1.fill(0.0f);
        delay.fill(0.0f);
        inputWrite = phaseWrite = delayWrite = 0;
    }

    // One mode-rate sample in, one out, delayed by delaySamples().
    template <typename Curve>
    float process(float x, const Curve& curve) noexcept
    {
        float y;
        if (active)
        {
            input[static_cast<size_t>(inputWrite)] = x;
            // Local-rate samples 2m and 2m+1 (zero-stuffed input, gain 2).
            float even = 0.0f;
            for (int i = 0; i < 12; ++i)
                even += kEven[static_cast<size_t>(i)] * at(input, inputWrite - i);
            const float upEven = 2.0f * even;
            const float upOdd = at(input, inputWrite - 5);
            inputWrite = (inputWrite + 1) % kHistory;
            phase0[static_cast<size_t>(phaseWrite)] = curve(upEven);
            phase1[static_cast<size_t>(phaseWrite)] = curve(upOdd);
            // Decimate: even taps on the 2m samples, the centre on 2m - 11.
            float down = 0.0f;
            for (int i = 0; i < 12; ++i)
                down += kEven[static_cast<size_t>(i)] * at(phase0, phaseWrite - i);
            down += 0.5f * at(phase1, phaseWrite - 6);
            phaseWrite = (phaseWrite + 1) % kHistory;
            y = down;
        }
        else
            y = curve(x);
        return delayLine(y);
    }

    // Delays a value computed at the mode rate by the stage's latency, so
    // gains and corrections applied after the stage stay aligned with it.
    int delaySamples() const noexcept { return kOptoStageLatencyHost * factor; }

private:
    static constexpr int kHistory = 16;

    static float at(const std::array<float, kHistory>& line, int index) noexcept
    {
        return line[static_cast<size_t>((index % kHistory + kHistory) % kHistory)];
    }

    float delayLine(float y) noexcept
    {
        if (pad <= 0) return y;
        delay[static_cast<size_t>(delayWrite)] = y;
        const int read = (delayWrite - pad + kDelayLength) % kDelayLength;
        delayWrite = (delayWrite + 1) % kDelayLength;
        return delay[static_cast<size_t>(read)];
    }

    static constexpr int kDelayLength = kMaxDelay + 1;
    std::array<float, kHistory> input{}, phase0{}, phase1{};
    std::array<float, kDelayLength> delay{};
    int inputWrite = 0, phaseWrite = 0, delayWrite = 0;
    int factor = 2, pad = 1;
    bool active = true;
};

// A fixed delay of the stage's latency for values computed at the mode rate.
class OptoStageDelay
{
public:
    void reset() noexcept { line.fill(0.0f); write = 0; }
    float process(float value, int delaySamples) noexcept
    {
        line[static_cast<size_t>(write)] = value;
        const int read = (write - std::clamp(delaySamples, 0, kLength - 1) + kLength) % kLength;
        write = (write + 1) % kLength;
        return line[static_cast<size_t>(read)];
    }

private:
    static constexpr int kLength = OptoStageOversampler::kMaxDelay + 1;
    std::array<float, kLength> line{};
    int write = 0;
};
} // namespace duskaudio
