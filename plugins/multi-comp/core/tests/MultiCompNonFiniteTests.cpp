// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later.
// Non-finite robustness gate.
//
// A single inf or NaN sample used to latch for good. It reached the sidechain
// shelf EQ (every mode's detector runs through it), the oversampler FIR
// histories, the multiband crossovers and the mode envelopes, none of which
// can flush a NaN with more audio, so every mode emitted NaN until a host
// reset() -- whether the sample arrived on the main input or on the external
// sidechain. A finite but huge burst did the same by overflowing a stage.
//
// In all eight modes, at every oversampling setting:
//   fault  one +inf, -inf or NaN sample on the main input or the external
//          sidechain, stereo and mono: every output sample finite, the faulted
//          block itself still audible, meters finite, and the tail within
//          kSingleFaultTailDb of a clean control render.
//   burst  10 ms at 3e38 peak -- finite, but it overflows every mode: every
//          output sample finite, the first block after the burst audible,
//          meters finite, and the tail within kBurstTailDb of the control.
//   param  every parameter (and every multiband band parameter, Mix and
//          Stereo Link) set to +inf, -inf or NaN mid-render (a corrupt
//          host automation value): every output sample finite, meters
//          finite, and the tail within kSingleFaultTailDb of the control.
//          std::clamp passes NaN through, so these reached int conversions
//          and table indices unguarded.
#include "../MultiCompDSP.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <tuple>
#include <vector>

namespace
{
using DSP = duskaudio::MultiCompDSP;
using P = DSP::Parameter;

constexpr double kPi = 3.141592653589793;
constexpr int kRate = 48000;
constexpr int kBlock = 256;
constexpr int kLength = kRate;                   // 1 s
constexpr int kFaultBlock = 47;
constexpr int kFaultSample = kFaultBlock * kBlock + 10;
constexpr int kBurstLength = kRate / 100;        // 10 ms
constexpr int kTailStart = kLength - kRate / 4;  // last 250 ms
constexpr float kBurstPeak = 3.0e38f;
// Measured worst on the guarded build: 0.000029 dB. One sample of a 0.25 sine
// replaced by silence half a second before the tail; 0.01 dB leaves a wide
// margin and still catches anything the fault leaves behind in state.
constexpr double kSingleFaultTailDb = 0.01;
// Measured worst: 0.271 dB (FET, whose programme-dependent release has 0.6 s
// rather than 0.87 s of history once recovery has reset it); every other mode
// 0.0000 dB. Unguarded, every mode misses by more than 570 dB.
constexpr double kBurstTailDb = 1.0;
// -60 dBFS RMS: audio is flowing again. Not a level claim.
constexpr double kAudibleRms = 1.0e-3;
constexpr const char* kModeNames[8] = {
    "Opto", "FET", "VCA", "Bus", "StudioFET", "StudioVCA", "Digital", "Multiband"};

enum class Fault { None, Sample, Burst, Parameter };

struct Render
{
    long nonFinite = 0;
    double recoveryBlockRms = 0.0;
    double tailRms = 0.0;
    bool metersFinite = true;
};

bool metersFinite(const DSP& dsp)
{
    bool finite = std::isfinite(dsp.getInputLevel()) && std::isfinite(dsp.getOutputLevel())
               && std::isfinite(dsp.getGainReduction()) && std::isfinite(dsp.getBusMeterReading());
    for (int band = 0; band < 4; ++band)
        finite = finite && std::isfinite(dsp.getBandGainReduction(band));
    return finite;
}

Render render(int mode, int oversampling, int channels, bool sidechain, Fault fault, float value)
{
    DSP dsp;
    dsp.setMode(mode);
    dsp.setParameter(P::TruePeakEnable, 0);
    dsp.setParameter(P::NoiseEnable, 0);
    dsp.setParameter(P::AutoMakeup, 0);
    dsp.setOversampling(oversampling);
    dsp.setStereoLink(100);
    dsp.setMix(100);
    dsp.setExternalSidechain(sidechain);
    dsp.prepare(kRate, kBlock);

    // The recovery proof. A single-sample fault must not cost even the block it
    // arrives in; a burst may cost its own blocks, but not the next one.
    const int recoveryBlock = fault == Fault::Burst
        ? (kFaultSample + kBurstLength - 1) / kBlock + 1 : kFaultBlock;
    std::vector<float> left(kBlock), right(kBlock), scLeft(kBlock), scRight(kBlock);
    std::vector<float> outLeft(kBlock), outRight(kBlock);
    const float* in[] = {left.data(), right.data()};
    const float* sc[] = {scLeft.data(), scRight.data()};
    float* out[] = {outLeft.data(), outRight.data()};

    Render result;
    double tailSquares = 0.0, recoverySquares = 0.0;
    long tailCount = 0, recoveryCount = 0;
    for (int block = 0; block * kBlock < kLength; ++block)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            const int n = block * kBlock + i;
            const double wave = std::sin(2.0 * kPi * 1000.0 * n / kRate + 0.37);
            const float clean = static_cast<float>(0.25 * wave);
            float faulted = clean;
            if (fault == Fault::Sample && n == kFaultSample)
                faulted = value;
            if (fault == Fault::Burst && n >= kFaultSample && n < kFaultSample + kBurstLength)
                faulted = static_cast<float>(value * wave);
            left[static_cast<size_t>(i)] = right[static_cast<size_t>(i)] = sidechain ? clean : faulted;
            scLeft[static_cast<size_t>(i)] = scRight[static_cast<size_t>(i)] = sidechain ? faulted : clean;
        }
        if (fault == Fault::Parameter && block == kFaultBlock)
        {
            for (int p = 0; p < static_cast<int>(P::None); ++p)
                dsp.setParameter(static_cast<P>(p), value);
            for (int band = 0; band < 4; ++band)
                for (int p = 0; p <= static_cast<int>(DSP::MultibandParameter::Enabled); ++p)
                    dsp.setMultibandParameter(band, static_cast<DSP::MultibandParameter>(p), value);
            dsp.setMix(value);
            dsp.setStereoLink(value);
        }
        if (sidechain) dsp.processBlockExternal(in, sc, out, channels, kBlock);
        else dsp.processBlock(in, out, channels, kBlock);
        result.metersFinite = metersFinite(dsp) && result.metersFinite;
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < kBlock; ++i)
            {
                const double y = out[ch][i];
                if (!std::isfinite(y)) { ++result.nonFinite; continue; }
                if (block == recoveryBlock) { recoverySquares += y * y; ++recoveryCount; }
                if (block * kBlock + i >= kTailStart) { tailSquares += y * y; ++tailCount; }
            }
    }
    result.recoveryBlockRms = recoveryCount > 0 ? std::sqrt(recoverySquares / static_cast<double>(recoveryCount)) : 0.0;
    result.tailRms = tailCount > 0 ? std::sqrt(tailSquares / static_cast<double>(tailCount)) : 0.0;
    return result;
}

double tailErrorDb(const Render& faulted, const Render& control)
{
    return std::abs(20.0 * std::log10(std::max(faulted.tailRms, 1.0e-30) / control.tailRms));
}

struct Summary
{
    long nonFinite = 0;
    double worstTailDb = 0.0;
    double quietestRecoveryRms = std::numeric_limits<double>::max();
    int meterFailures = 0;
    int failures = 0;
};

void score(Summary& summary, const Render& faulted, const Render& control, double tailBoundDb)
{
    const double tailDb = tailErrorDb(faulted, control);
    summary.nonFinite += faulted.nonFinite;
    summary.worstTailDb = std::max(summary.worstTailDb, tailDb);
    summary.quietestRecoveryRms = std::min(summary.quietestRecoveryRms, faulted.recoveryBlockRms);
    summary.meterFailures += faulted.metersFinite ? 0 : 1;
    if (faulted.nonFinite != 0 || !faulted.metersFinite || faulted.recoveryBlockRms <= kAudibleRms
        || !(tailDb <= tailBoundDb))
        ++summary.failures;
}

void print(const char* gate, const char* mode, const char* path, const Summary& s)
{
    std::printf("%-5s %-9s %-9s non-finite %7ld  recovery-block RMS %.6f  worst tail %9.6f dB  meter faults %d  %s\n",
                gate, mode, path, s.nonFinite, s.quietestRecoveryRms, s.worstTailDb, s.meterFailures,
                s.failures == 0 ? "PASS" : "FAIL");
}
} // namespace

int main()
{
    const float faults[] = {std::numeric_limits<float>::infinity(),
                            -std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::quiet_NaN()};
    int failures = 0;
    for (int mode = 0; mode < 8; ++mode)
        for (const bool sidechain : {false, true})
        {
            const char* path = sidechain ? "sidechain" : "main-in";
            Summary single, burst, parameter;
            for (int oversampling = 0; oversampling < 3; ++oversampling)
                for (int channels = 1; channels <= 2; ++channels)
                {
                    const Render control = render(mode, oversampling, channels, sidechain, Fault::None, 0.0f);
                    for (const float value : faults)
                        score(single, render(mode, oversampling, channels, sidechain, Fault::Sample, value),
                              control, kSingleFaultTailDb);
                    if (!sidechain)
                        for (const float value : faults)
                            score(parameter, render(mode, oversampling, channels, sidechain, Fault::Parameter, value),
                                  control, kSingleFaultTailDb);
                    if (channels == 2)
                        score(burst, render(mode, oversampling, channels, sidechain, Fault::Burst, kBurstPeak),
                              control, kBurstTailDb);
                }
            print("fault", kModeNames[mode], path, single);
            print("burst", kModeNames[mode], path, burst);
            if (!sidechain)
                print("param", kModeNames[mode], path, parameter);
            failures += single.failures + burst.failures + parameter.failures;
        }
    std::printf("%s: %d failing case(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
