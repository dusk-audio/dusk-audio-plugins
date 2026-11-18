// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// #148: enabling Digital saturation must not remove the audible top octave.
#include "../core/MultiQDSP.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>

namespace
{
int checks = 0, failures = 0;
void check(bool ok, const char* message)
{
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}

void response(double sr, int setting, int channels, int bands)
{
    constexpr int block = 512, length = 2048;
    // The processor owns ~897 KB of fixed DSP storage. Allocate the fixture
    // during setup so MSVC's default 1 MB test stack still has room to run it.
    auto instance = std::make_unique<duskaudio::MultiQDSP>();
    auto& dsp = *instance;
    duskaudio::MultiQDSP::Params p;
    p.bandEnabled[0] = p.bandEnabled[7] = false;
    p.oversampling = setting;
    // At zero drive the waveshaper is identity. The audio still traverses the
    // actual per-band up/down filters: this isolates their passband and delay.
    for (int b = 1; b <= bands; ++b)
    {
        p.bandSatType[b] = 1;
        p.bandSatDrive[b] = 0.0f;
    }
    dsp.prepare(sr, block);
    std::array<float, block> left{}, right{}, outL{}, outR{};
    const float* in[] = {left.data(), right.data()};
    float* out[] = {outL.data(), outR.data()};
    // Settle enable and coefficient ramps before the measurement impulse.
    for (int i = 0; i < 32; ++i) dsp.process(in, out, channels, block, p);
    std::array<float, length> irL{}, irR{};
    for (int offset = 0; offset < length; offset += block)
    {
        left[0] = offset == 0 ? 0.125f : 0.0f;
        right[0] = offset == 0 ? -0.0625f : 0.0f;
        dsp.process(in, out, channels, block, p);
        for (int i = 0; i < block; ++i)
        {
            irL[offset + i] = outL[i];
            irR[offset + i] = outR[i];
        }
    }
    const int perBandLatency = setting == 0 ? 0 : setting == 1 ? 63 : 67;
    check(dsp.getLatencySamples() == bands * perBandLatency,
          "Digital reports the summed active saturation-filter latency");
    for (int channel = 0; channel < channels; ++channel)
    {
        const auto& ir = channel == 0 ? irL : irR;
        const double amplitude = channel == 0 ? 0.125 : 0.0625;
        double energy = 0.0, centroid = 0.0;
        bool finite = true;
        for (int i = 0; i < length; ++i)
        {
            finite &= std::isfinite(ir[i]);
            const double power = double(ir[i]) * ir[i];
            energy += power;
            centroid += i * power;
        }
        check(finite && energy > amplitude * amplitude * 0.1,
              "the measured channel contains finite, excited output");
        centroid /= energy;
        // Account for the decimation phase when checking the rendered delay,
        // separately from the oversampler's rounded host-latency contract.
        const double renderedPerBand = setting == 0 ? 0.0 : setting == 1 ? 62.5 : 65.75;
        check(std::abs(centroid - bands * renderedPerBand) < 0.15 * bands,
              "rendered delay follows the selected filters, including serial bands");
        double worst = 0.0;
        for (double hz : {1000.0, 10000.0, 15000.0, 19000.0, 20000.0})
        {
            double re = 0.0, im = 0.0;
            for (int i = 0; i < length; ++i)
            {
                const double phase = 2.0 * duskaudio::kMultiQPi * hz * i / sr;
                re += ir[i] * std::cos(phase);
                im -= ir[i] * std::sin(phase);
            }
            const double gain = 20.0 * std::log10(std::hypot(re, im) / amplitude);
            worst = std::max(worst, std::abs(gain));
            check(std::isfinite(gain) && std::abs(gain) < 0.1,
                  "actual Digital saturation chain stays within 0.1 dB through 20 kHz");
        }
        std::printf("Digital OS: sr=%.0f setting=%d channels=%d bands=%d ch=%d latency=%d centroid=%.5f max|gain|=%.6f dB\n",
                    sr, setting, channels, bands, channel, dsp.getLatencySamples(), centroid, worst);
    }
    // The same running processor must return to transparent, zero-latency
    // Digital when saturation is disabled, regardless of the selected factor.
    const auto active = p;
    p.bandSatType.fill(0);
    left.fill(0.125f); right.fill(-0.0625f);
    dsp.process(in, out, channels, block, p);
    bool exact = dsp.getLatencySamples() == 0;
    for (int i = 0; i < block; ++i)
        exact &= outL[i] == left[i] && (channels == 1 || outR[i] == right[i]);
    check(exact, "disabling saturation restores exact passthrough and zero latency");

    // Repeated prepare and reset must discard every oversampler history. Replay
    // the measurement on this used object and compare with its fresh response.
    dsp.reset();
    dsp.prepare(sr, block);
    dsp.prepare(sr, block);
    p = active;
    left.fill(0.0f); right.fill(0.0f);
    for (int i = 0; i < 32; ++i) dsp.process(in, out, channels, block, p);
    bool reproduced = true;
    for (int offset = 0; offset < length; offset += block)
    {
        left[0] = offset == 0 ? 0.125f : 0.0f;
        right[0] = offset == 0 ? -0.0625f : 0.0f;
        dsp.process(in, out, channels, block, p);
        for (int i = 0; i < block; ++i)
            reproduced &= outL[i] == irL[offset + i]
                && (channels == 1 || outR[i] == irR[offset + i]);
    }
    check(reproduced, "reset and repeated prepare reproduce a fresh processor exactly");
}
}

int main()
{
    for (double sr : {44100.0, 48000.0, 96000.0})
        for (int setting : {0, 1, 2})
            for (int channels : {1, 2})
                for (int bands : {1, 6}) response(sr, setting, channels, bands);
    std::printf("Multi-Q Digital oversampling: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
