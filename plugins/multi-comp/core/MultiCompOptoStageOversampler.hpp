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
// the plugin: optoStageLatencyHost() host samples.  With oversampling off the
// half-band pair cannot be flat to the host Nyquist, so the curve then runs at
// the host rate and only the delay is applied.
//
// Opto's rates follow the 44.1 / 48 kHz host. The cell's side chain is designed
// and fitted at the mode rate of the default 2x setting there (88.2 / 96 kHz),
// and it depends on that rate, so a faster host runs Opto at the same mode rate
// rather than at twice its own: an 88.2 / 96 kHz host runs the mode at 1x for
// the 2x setting and 2x for 4x (fixedRateModeFactor; vintage FET's loop does the
// same). The stage's latency keeps its duration: 12 host samples there for the 6
// at 44.1 / 48 kHz.
#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <type_traits>

namespace duskaudio
{
inline constexpr int kOptoStageLatencyHost = 6;   // at a 44.1 / 48 kHz host

// How many times the 44.1 / 48 kHz family the host rate is: 1, 2 (88.2 / 96 kHz) or 4 (176.4 / 192 kHz and up).
inline int hostRateMultiple(double hostRate) noexcept
{
    return hostRate < 70000.0 ? 1 : hostRate < 140000.0 ? 2 : 4;
}

// The mode-rate factor of a mode that keeps a 44.1 / 48 kHz host's mode rate (Opto, vintage FET) for an
// Oversampling setting's factor (1, 2 or 4) at this host rate.
inline int fixedRateModeFactor(int settingFactor, double hostRate) noexcept
{
    return std::max(1, settingFactor / hostRateMultiple(hostRate));
}

// The output stage's latency in host samples: kOptoStageLatencyHost at 44.1 / 48 kHz, the same duration above.
inline int optoStageLatencyHost(double hostRate) noexcept
{
    return kOptoStageLatencyHost * hostRateMultiple(hostRate);
}

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
    // The longest delay at the mode rate: the stage's latency times the mode factor is at most 6 x 4 in every
    // configuration (kOptoStageLatencyHost x the host-rate multiple x fixedRateModeFactor() <= 6 x 4).
    static constexpr int kMaxDelay = kOptoStageLatencyHost * kMaxFactor;

    // modeFactor: the mode rate over the host rate; latencyHost: the stage's latency in host samples
    // (optoStageLatencyHost()); oversampled: the Oversampling setting asks for oversampling, so the curve runs
    // at twice the mode rate.
    void configure(int modeFactor, int latencyHost, bool oversampled) noexcept
    {
        target = std::clamp(latencyHost * std::clamp(modeFactor, 1, kMaxFactor), 0, kMaxDelay);
        active = oversampled && target >= kFilterDelay;
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

    // process() for n consecutive samples. Each output sums its taps in
    // process()'s order; four outputs share the instructions, and a curve with a
    // four-sample form (Curve::four, the same arithmetic as its one-sample form)
    // takes them four at a time. `scratch` holds blockScratch(n) floats.
    static constexpr int blockScratch(int n) noexcept { return 3 * (kHistory + n); }
    template <typename Curve, typename = void> struct HasFour : std::false_type {};
    template <typename Curve> struct HasFour<Curve, std::void_t<decltype(&Curve::four)>> : std::true_type {};
    template <typename Curve>
    void processBlock(const float* x, float* y, int n, const Curve& curve, float* scratch) noexcept
    {
        if (n <= 0) return;
        if (active)
        {
            float* in = scratch;                        // the ring's history, then the block
            float* even = scratch + (kHistory + n);     // the local-rate phases, likewise
            float* odd = scratch + 2 * (kHistory + n);
            for (int j = 0; j < kHistory; ++j)
            {
                in[kHistory - 1 - j] = at(input, inputWrite - 1 - j);
                even[kHistory - 1 - j] = at(phase0, phaseWrite - 1 - j);
                odd[kHistory - 1 - j] = at(phase1, phaseWrite - 1 - j);
            }
            for (int k = 0; k < n; ++k) in[kHistory + k] = x[k];
            // Local-rate samples 2m and 2m+1 (zero-stuffed input, gain 2), through the curve.
            int k = 0;
            for (; k + 4 <= n; k += 4)
            {
                float sum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                for (int i = 0; i < 12; ++i)
                    for (int lane = 0; lane < 4; ++lane)
                        sum[lane] += kEven[static_cast<size_t>(i)] * in[kHistory + k + lane - i];
                if constexpr (HasFour<Curve>::value)
                {
                    const float twice[4] = {2.0f * sum[0], 2.0f * sum[1], 2.0f * sum[2], 2.0f * sum[3]};
                    Curve::four(twice, even + kHistory + k);
                    Curve::four(in + kHistory + k - 5, odd + kHistory + k);
                }
                else
                    for (int lane = 0; lane < 4; ++lane)
                    {
                        even[kHistory + k + lane] = curve(2.0f * sum[lane]);
                        odd[kHistory + k + lane] = curve(in[kHistory + k + lane - 5]);
                    }
            }
            for (; k < n; ++k)
            {
                float sum = 0.0f;
                for (int i = 0; i < 12; ++i) sum += kEven[static_cast<size_t>(i)] * in[kHistory + k - i];
                even[kHistory + k] = curve(2.0f * sum);
                odd[kHistory + k] = curve(in[kHistory + k - 5]);
            }
            // Decimate: even taps on the 2m samples, the centre on 2m - 11.
            k = 0;
            for (; k + 4 <= n; k += 4)
            {
                float sum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                for (int i = 0; i < 12; ++i)
                    for (int lane = 0; lane < 4; ++lane)
                        sum[lane] += kEven[static_cast<size_t>(i)] * even[kHistory + k + lane - i];
                for (int lane = 0; lane < 4; ++lane)
                    y[k + lane] = sum[lane] + 0.5f * odd[kHistory + k + lane - 6];
            }
            for (; k < n; ++k)
            {
                float sum = 0.0f;
                for (int i = 0; i < 12; ++i) sum += kEven[static_cast<size_t>(i)] * even[kHistory + k - i];
                y[k] = sum + 0.5f * odd[kHistory + k - 6];
            }
            // The rings keep the block's tail.
            for (int j = 0; j < std::min(n, kHistory); ++j)
            {
                const auto inputPosition = static_cast<size_t>((inputWrite + n - 1 - j) % kHistory);
                const auto phasePosition = static_cast<size_t>((phaseWrite + n - 1 - j) % kHistory);
                input[inputPosition] = in[kHistory + n - 1 - j];
                phase0[phasePosition] = even[kHistory + n - 1 - j];
                phase1[phasePosition] = odd[kHistory + n - 1 - j];
            }
            inputWrite = (inputWrite + n) % kHistory;
            phaseWrite = (phaseWrite + n) % kHistory;
        }
        else
            for (int k = 0; k < n; ++k) y[k] = curve(x[k]);
        delayBlock(y, n);
    }

    // Delays a value computed at the mode rate by the stage's latency, so
    // gains and corrections applied after the stage stay aligned with it.
    int delaySamples() const noexcept { return target; }

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

    // delayLine() for n consecutive values, in place: the first pad come out of the ring, the rest are the
    // block's own values pad places back; the ring keeps the block's newest values.
    void delayBlock(float* y, int n) noexcept
    {
        if (pad <= 0 || n <= 0) return;
        std::array<float, kMaxDelay> ahead{};
        const int lead = std::min(pad, n);
        for (int j = 0; j < lead; ++j)
            ahead[static_cast<size_t>(j)] = delay[static_cast<size_t>((delayWrite - pad + j + kDelayLength) % kDelayLength)];
        for (int j = std::max(0, n - kDelayLength); j < n; ++j)
            delay[static_cast<size_t>((delayWrite + j) % kDelayLength)] = y[j];
        delayWrite = (delayWrite + n) % kDelayLength;
        std::memmove(y + lead, y, static_cast<size_t>(n - lead) * sizeof(float));
        std::copy_n(ahead.data(), lead, y);
    }

    static constexpr int kDelayLength = kMaxDelay + 1;
    std::array<float, kHistory> input{}, phase0{}, phase1{};
    std::array<float, kDelayLength> delay{};
    int inputWrite = 0, phaseWrite = 0, delayWrite = 0;
    int target = kOptoStageLatencyHost * 2, pad = 1;
    bool active = true;
};

// A fixed delay of the stage's latency for values computed at the mode rate.
class OptoStageDelay
{
public:
    void reset() noexcept { line.fill(0.0f); write = 0; onesRun = 0; }
    float process(float value, int delaySamples) noexcept
    {
        onesRun = value == 1.0f ? std::min(onesRun + 1, kLength) : 0;
        line[static_cast<size_t>(write)] = value;
        const int read = (write - std::clamp(delaySamples, 0, kLength - 1) + kLength) % kLength;
        write = (write + 1) % kLength;
        return line[static_cast<size_t>(read)];
    }
    // process() for n values, in place. Returns true when every value out is 1: the whole line held ones and
    // only ones came in (the shipped state, where the delayed corrections are unity), which leaves `values`
    // as it was and just advances the line.
    bool processBlock(float* values, int n, int delaySamples) noexcept
    {
        if (n <= 0) return true;
        if (onesRun == kLength && std::all_of(values, values + n, [](float v) noexcept { return v == 1.0f; }))
        {
            write = (write + n) % kLength;
            return true;
        }
        const int delay = std::clamp(delaySamples, 0, kLength - 1);
        for (int k = 0; k < n; ++k)
        {
            onesRun = values[k] == 1.0f ? std::min(onesRun + 1, kLength) : 0;
            line[static_cast<size_t>(write)] = values[k];
            int read = write - delay;
            if (read < 0) read += kLength;
            write = write + 1 == kLength ? 0 : write + 1;
            values[k] = line[static_cast<size_t>(read)];
        }
        return false;
    }

private:
    static constexpr int kLength = OptoStageOversampler::kMaxDelay + 1;
    std::array<float, kLength> line{};
    int write = 0;
    int onesRun = 0;   // trailing values written that were exactly 1 (at most kLength: the whole line)
};
} // namespace duskaudio
