// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// WITNESS GATE -- NOT A PARITY GATE.  READ THIS BEFORE CHANGING A NUMBER HERE.
//
// These points pin slow-attack BUS settings where the model has diverged from the
// native UAD SSL G bus compressor. The complete law/detector calibration on
// 2026-09-11 puts all five witnesses below 0.3 dB; see
// docs/multi-comp-2-bus-finish-2026-09-11.md. Each bound remains measured + 0.01 dB.
//
// This file does NOT assert parity.  It asserts only that each divergence, as last
// pinned (dates in the fixture comments), does not GROW, so the known gap cannot
// widen unnoticed while other BUS work lands.  Each bound is its pinned error plus
// 0.01 dB.  They are
// two-sided on the signed error, so a sign flip (over-compression) fails too.
//
// Whenever a change reduces a divergence these bounds MUST be retightened.  The
// test prints a RETIGHTEN line whenever the measured divergence has dropped well
// under its pinned value, so an improvement is visible rather than silently
// absorbed. Target: every point below 0.3 dB (met on 2026-09-11).
//
// Do not widen a bound to make an unrelated change pass.  A bound that has to grow
// means BUS got further from the reference, which is the one thing this file exists
// to catch.
#include "../MultiCompDSP.hpp"
#include "MultiCompBusWitnessFixtures.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace
{
using DSP = duskaudio::MultiCompDSP;
using P = DSP::Parameter;
constexpr double pi = 3.14159265358979323846;
constexpr int kRate = 48000;
constexpr int kSegment = 2 * kRate;     // one input level per 2 s segment
constexpr int kLevels = 16;             // -30 dB to +15 dB in 3 dB steps
constexpr double kMakeupDb = 15.0;

void require(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

// The same stimulus the native captures were rendered with: one continuous
// 1000 Hz tone whose amplitude steps every 2 s.  The phase runs unbroken across
// the segment boundaries, exactly as saturation_capture.py generated it.
std::vector<float> stimulus()
{
    std::vector<float> signal(size_t(kSegment) * kLevels);
    for (int level = 0; level < kLevels; ++level)
    {
        const double amplitude = std::pow(10.0, (-30.0 + 3.0 * level) / 20.0);
        for (int i = 0; i < kSegment; ++i)
        {
            const long index = long(level) * kSegment + i;
            signal[size_t(index)] = float(amplitude
                * std::sin(2 * pi * 1000.0 * double(index) / kRate + 0.37));
        }
    }
    return signal;
}

double rootMeanSquare(const std::vector<float>& signal, int start, int count)
{
    double energy = 0;
    for (int i = 0; i < count; ++i)
    {
        const double sample = signal[size_t(start + i)];
        energy += sample * sample;
    }
    return std::sqrt(energy / count);
}

void witness(const buswitness::Point& point)
{
    DSP dsp;
    dsp.setMode(3);
    dsp.setParameter(P::TruePeakEnable, 0);
    dsp.setParameter(P::NoiseEnable, 0);
    dsp.setParameter(P::AutoMakeup, 0);
    dsp.setParameter(P::BusThreshold, -15);
    dsp.setParameter(P::BusRatio, float(point.ratio));
    dsp.setParameter(P::BusAttack, float(point.attack));
    dsp.setParameter(P::BusRelease, float(point.release));
    dsp.setParameter(P::BusHeadroom, 3);
    dsp.setParameter(P::BusMakeup, float(kMakeupDb));
    dsp.setParameter(P::SidechainHP, 0);
    dsp.setOversampling(1);
    dsp.setStereoLink(100);
    dsp.setMix(100);
    dsp.setExternalSidechain(false);
    dsp.prepare(kRate, 256);

    std::vector<float> left(256), right(256), outLeft(256), outRight(256);
    const float* in[] = {left.data(), right.data()};
    float* out[] = {outLeft.data(), outRight.data()};
    // The capture host ran five silent seconds before the stimulus.  Reproduce
    // it so the fixed-release control bias is settled at the first segment.
    std::fill(left.begin(), left.end(), 0.0f);
    std::fill(right.begin(), right.end(), 0.0f);
    for (int done = 0; done < 5 * kRate; done += 256)
        dsp.processBlock(in, out, 2, std::min(256, 5 * kRate - done));

    const auto input = stimulus();
    std::vector<float> rendered(input.size());
    for (size_t pos = 0; pos < input.size(); pos += 256)
    {
        const int count = int(std::min<size_t>(256, input.size() - pos));
        for (int i = 0; i < count; ++i)
            left[size_t(i)] = right[size_t(i)] = input[pos + size_t(i)];
        dsp.processBlock(in, out, 2, count);
        for (int i = 0; i < count; ++i) rendered[pos + size_t(i)] = outLeft[size_t(i)];
    }

    // Skip the first second of each segment so only the settled value is scored;
    // this gate is about the law's shape, not about attack or release timing.
    const int latency = dsp.getLatencySamples();
    const int window = int(0.9 * kRate);
    double worst = 0, worstSigned = 0;
    int worstLevel = 0;
    for (int level = 0; level < kLevels; ++level)
    {
        const int start = level * kSegment + kRate;
        const double reduction = kMakeupDb - 20 * std::log10(
            rootMeanSquare(rendered, start + latency, window)
            / rootMeanSquare(input, start, window));
        const double error = point.nativeGrDb[level] - reduction;
        if (std::abs(error) > worst)
        { worst = std::abs(error); worstSigned = error; worstLevel = -30 + 3 * level; }
    }

    const bool ok = std::isfinite(worst) && worst <= point.boundDb;
    std::printf("BUS witness %-22s worst %+.6f dB at %+d dBFS (pinned %.6f, bound %.6f) %s\n",
                point.label, worstSigned, worstLevel, point.pinnedWorstDb,
                point.boundDb, ok ? "ok" : "GREW");
    if (ok && worst < point.pinnedWorstDb - 0.05)
        std::printf("  RETIGHTEN: divergence fell from %.6f to %.6f dB; re-pin this bound.\n",
                    point.pinnedWorstDb, worst);
    require(ok, "the known BUS drive-law divergence has not grown at its measured operating points");
}
}

int main()
{
    for (const auto& point : buswitness::points) witness(point);
    std::puts("Multi-Comp BUS witness: PASS (divergence pinned, NOT parity)");
}
