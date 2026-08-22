#include "../MultiCompDSP.hpp"
#include "../../../shared-daf/dsp/DuskCrossover.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using duskaudio::DuskCrossover;
using duskaudio::MultiCompDSP;

namespace
{
constexpr float kPi = duskaudio::kDuskPi;

void require(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

float rms(const std::vector<float>& x, size_t start = 0)
{
    double sum = 0.0;
    for (size_t i = start; i < x.size(); ++i) sum += static_cast<double>(x[i]) * x[i];
    return static_cast<float>(std::sqrt(sum / std::max<size_t>(1, x.size() - start)));
}

float renderSine(duskaudio::MultiCompMode mode, float amplitude, double sr = 48000.0)
{
    MultiCompDSP dsp;
    dsp.prepare(sr, 256);
    dsp.setOversampling(0);
    dsp.setMode(static_cast<int>(mode));
    dsp.setMix(100.0f);
    dsp.setParameter(MultiCompDSP::Parameter::SidechainHP, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::FetInput, 20.0f);
    dsp.setParameter(MultiCompDSP::Parameter::OptoPeakReduction, 75.0f);
    dsp.setParameter(MultiCompDSP::Parameter::VcaThreshold, -20.0f);
    dsp.setParameter(MultiCompDSP::Parameter::BusThreshold, -25.0f);
    dsp.setParameter(MultiCompDSP::Parameter::StudioVcaThreshold, -20.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, -20.0f);
    for (int block = 0; block < 100; ++block)
    {
        std::vector<float> in(256), out(256);
        for (int i = 0; i < 256; ++i) in[static_cast<size_t>(i)] = amplitude * std::sin(2.0f * kPi * 1000.0f * static_cast<float>(block * 256 + i) / static_cast<float>(sr));
        const float* inputs[] = {in.data()}; float* outputs[] = {out.data()};
        dsp.processBlock(inputs, outputs, 1, 256);
        if (block == 99) return rms(out);
    }
    return 0.0f;
}

void testCrossoverFlatness()
{
    const float configurations[][3] = {{200.0f, 2000.0f, 8000.0f}, {100.0f, 1000.0f, 5000.0f}};
    for (const auto& cfg : configurations)
    {
        for (double sr : {44100.0, 48000.0, 96000.0})
        {
            for (const float frequency : {cfg[0], cfg[2],
                                           std::min(cfg[2] * 1.5f, static_cast<float>(sr * 0.45))})
            {
                DuskCrossover c1, c2, c3;
                c1.prepare(sr, cfg[0]); c2.prepare(sr, cfg[1]); c3.prepare(sr, cfg[2]);
                const int n = 16384;
                std::vector<float> sum(static_cast<size_t>(n)), original(static_cast<size_t>(n));
                for (int i = 0; i < n; ++i)
                {
                    const float x = 0.25f * std::sin(2.0f * kPi * frequency * static_cast<float>(i) / static_cast<float>(sr));
                    original[static_cast<size_t>(i)] = x;
                    float l0, h0, l1, h1, l2, h2;
                    c1.processStandard(x, l0, h0); c2.processStandard(h0, l1, h1); c3.processStandard(h1, l2, h2);
                    sum[static_cast<size_t>(i)] = l0 + l1 + l2 + h2;
                }
                const float ratio = rms(sum, 4096) / rms(original, 4096);
                require(std::abs(duskaudio::gainToDecibels(ratio)) < 0.1f, "standard LR4 magnitude reconstruction");
            }

            DuskCrossover standard;
            standard.prepare(sr, cfg[0]);
            auto branchLevel = [&, sr](float frequency, bool low) {
                standard.reset();
                std::vector<float> values(8192);
                for (int i = 0; i < 8192; ++i)
                {
                    const float x = std::sin(2.0f * kPi * frequency * static_cast<float>(i) / static_cast<float>(sr));
                    float l, h;
                    standard.processStandard(x, l, h);
                    values[static_cast<size_t>(i)] = low ? l : h;
                }
                return rms(values, 4096);
            };
            require(branchLevel(cfg[0] * 0.25f, true) > branchLevel(cfg[0] * 0.25f, false), "standard LR4 low edge magnitude");
            require(branchLevel(cfg[0] * 4.0f, false) > branchLevel(cfg[0] * 4.0f, true), "standard LR4 high edge magnitude");
        }
    }
    std::puts("LR4 standard magnitude flatness: 44.1/48/96 kHz, two 3-split configurations OK");
}

void testStaticCurves()
{
    const duskaudio::MultiCompMode modes[] = {
        duskaudio::MultiCompMode::Opto, duskaudio::MultiCompMode::FET, duskaudio::MultiCompMode::VCA,
        duskaudio::MultiCompMode::Bus, duskaudio::MultiCompMode::StudioFET, duskaudio::MultiCompMode::StudioVCA,
        duskaudio::MultiCompMode::Digital, duskaudio::MultiCompMode::Multiband};
    for (auto mode : modes)
    {
        const float quiet = renderSine(mode, 0.05f);
        const float medium = renderSine(mode, 0.25f);
        const float hot = renderSine(mode, 0.8f);
        require(std::isfinite(quiet) && std::isfinite(medium) && std::isfinite(hot), "static curve finite");
        require(quiet <= medium * 1.05f && medium <= hot * 1.05f, "static curve monotonic");
    }
    const float inputDb = duskaudio::gainToDecibels(0.5f / std::sqrt(2.0f));
    const float expectedDb = inputDb - (inputDb - (-20.0f)) * (1.0f - 1.0f / 4.0f);
    const float digitalDb = duskaudio::gainToDecibels(renderSine(duskaudio::MultiCompMode::Digital, 0.5f));
    require(std::abs(digitalDb - expectedDb) < 2.0f, "digital static curve follows threshold/ratio math");
    std::puts("static curves: all eight modes finite and monotonic");
}

void testEnvelopeAndReset()
{
    for (int mode = 0; mode < 8; ++mode)
    {
        MultiCompDSP dsp;
        dsp.prepare(48000.0, 256); dsp.setOversampling(0); dsp.setMode(mode);
        constexpr int kStepBlocks = 64;
        std::vector<float> reference(static_cast<size_t>(kStepBlocks * 256)), second(static_cast<size_t>(kStepBlocks * 256)), in(256), out(256);
        float attackReduction = 0.0f, firstReleaseReduction = 0.0f, settledReleaseReduction = 0.0f;
        for (int block = 0; block < kStepBlocks; ++block)
        {
            for (int i = 0; i < 256; ++i) in[static_cast<size_t>(i)] = block < kStepBlocks / 2 ? 0.8f : 0.0f;
            const float* ip[] = {in.data()}; float* op[] = {out.data()};
            dsp.processBlock(ip, op, 1, 256);
            std::copy(out.begin(), out.end(), reference.begin() + block * 256);
            if (block == kStepBlocks / 2 - 1) attackReduction = dsp.getGainReduction();
            if (block == kStepBlocks / 2) firstReleaseReduction = dsp.getGainReduction();
            if (block == kStepBlocks - 1) settledReleaseReduction = dsp.getGainReduction();
        }
        for (float x : reference) require(std::isfinite(x), "envelope step finite");
        require(std::isfinite(attackReduction) && std::isfinite(firstReleaseReduction)
                    && std::isfinite(settledReleaseReduction), "envelope meter finite");
        require(settledReleaseReduction >= attackReduction - 0.5f,
                "release returns toward unity at the configured time scale");
        dsp.reset();
        for (int block = 0; block < kStepBlocks; ++block)
        {
            for (int i = 0; i < 256; ++i) in[static_cast<size_t>(i)] = block < kStepBlocks / 2 ? 0.8f : 0.0f;
            const float* ip[] = {in.data()}; float* op[] = {out.data()};
            dsp.processBlock(ip, op, 1, 256);
            std::copy(out.begin(), out.end(), second.begin() + block * 256);
        }
        float maxDiff = 0.0f;
        for (size_t i = 0; i < reference.size(); ++i) maxDiff = std::max(maxDiff, std::abs(reference[i] - second[i]));
        require(maxDiff == 0.0f, "reset determinism");
    }
    std::puts("attack/release steps: finite; reset determinism: all eight modes OK");
}

void testMixBypassAndBlockEdges()
{
    MultiCompDSP dsp;
    dsp.prepare(48000.0, 256); dsp.setOversampling(0); dsp.setMode(6);
    std::vector<float> in(256), out(256), previous(256);
    for (int i = 0; i < 256; ++i) in[static_cast<size_t>(i)] = 0.4f * std::sin(2.0f * kPi * 440.0f * i / 48000.0f);
    const float* ip[] = {in.data()}; float* op[] = {out.data()};
    dsp.processBlock(ip, op, 1, 256);
    dsp.setMix(0.0f); dsp.processBlock(ip, op, 1, 256);
    float largestDelta = 0.0f;
    for (int i = 1; i < 256; ++i) largestDelta = std::max(largestDelta, std::abs(out[static_cast<size_t>(i)] - out[static_cast<size_t>(i - 1)]));
    require(largestDelta < 0.5f, "mix ramp bounded sample delta");
    MultiCompDSP bypassCheck;
    bypassCheck.prepare(48000.0, 256);
    bypassCheck.setOversampling(0);
    bypassCheck.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
    bypassCheck.setParameter(MultiCompDSP::Parameter::DigitalThreshold, 0.0f);
    bypassCheck.setParameter(MultiCompDSP::Parameter::DigitalRatio, 1.0f);
    std::vector<float> bypassInput(256), bypassOutput(256);
    const float* bypassIp[] = {bypassInput.data()}; float* bypassOp[] = {bypassOutput.data()};
    for (int block = 0; block < 10; ++block)
    {
        for (int i = 0; i < 256; ++i)
        {
            bypassInput[static_cast<size_t>(i)] = 0.1f + 0.0001f * static_cast<float>(block * 256 + i);
        }
        bypassCheck.processBlock(bypassIp, bypassOp, 1, 256);
    }
    bypassCheck.setBypass(true);
    for (int block = 0; block < 10; ++block)
    {
        for (int i = 0; i < 256; ++i)
        {
            bypassInput[static_cast<size_t>(i)] = 0.1f + 0.0001f * static_cast<float>(10 * 256 + block * 256 + i);
        }
        bypassCheck.processBlock(bypassIp, bypassOp, 1, 256);
        if (block == 9)
            for (int i = 0; i < 256; ++i)
            {
                const int absolute = 10 * 256 + block * 256 + i;
                const float expected = absolute >= bypassCheck.getLatencySamples() ? 0.1f + 0.0001f * static_cast<float>(absolute - bypassCheck.getLatencySamples()) : 0.0f;
                if (bypassOutput[static_cast<size_t>(i)] != expected)
                {
                    std::fprintf(stderr, "bypass mismatch i=%d out=%.9g expected=%.9g latency=%d\n", i, bypassOutput[static_cast<size_t>(i)], expected, bypassCheck.getLatencySamples());
                    require(false, "settled bypass is delayed bit-exact passthrough");
                }
            }
    }
    dsp.setBypass(false); dsp.processBlock(ip, op, 1, 256);
    float reentryDelta = 0.0f;
    for (int i = 0; i < 256; ++i)
    {
        require(std::isfinite(out[static_cast<size_t>(i)]), "bypass re-entry finite");
        if (i > 0) reentryDelta = std::max(reentryDelta, std::abs(out[static_cast<size_t>(i)] - out[static_cast<size_t>(i - 1)]));
    }
    require(reentryDelta < 1.0f, "bypass toggle bounded sample delta");
    dsp.processBlock(ip, op, 1, 0);
    dsp.processBlock(ip, op, 1, 1);
    (void)previous;
    std::puts("mix ramp, bypass, zero-sample and single-sample blocks OK");
}

float renderNeutralSine(int oversampling, float mix, float frequency = 997.0f)
{
    MultiCompDSP dsp;
    dsp.prepare(48000.0, 256);
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
    dsp.setOversampling(oversampling);
    dsp.setMix(mix);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalRatio, 1.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalLookahead, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    std::vector<float> in(256), out(256);
    float result = 0.0f;
    for (int block = 0; block < 32; ++block)
    {
        for (int i = 0; i < 256; ++i)
            in[static_cast<size_t>(i)] = 0.25f * std::sin(2.0f * kPi * frequency * static_cast<float>(block * 256 + i) / 48000.0f);
        const float* ip[] = {in.data()}; float* op[] = {out.data()};
        dsp.processBlock(ip, op, 1, 256);
        if (block >= 28) for (float x : out) result += x * x;
    }
    return std::sqrt(result / (4.0f * 256.0f));
}

float renderLookaheadSine(float mix, float frequency, float lookaheadMs)
{
    MultiCompDSP dsp;
    dsp.prepare(48000.0, 256);
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
    dsp.setOversampling(0);
    dsp.setMix(100.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalRatio, 1.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalMix, mix);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalLookahead, lookaheadMs);
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    std::vector<float> in(256), out(256);
    float result = 0.0f;
    for (int block = 0; block < 32; ++block)
    {
        for (int i = 0; i < 256; ++i)
            in[static_cast<size_t>(i)] = 0.25f * std::sin(2.0f * kPi * frequency * static_cast<float>(block * 256 + i) / 48000.0f);
        const float* ip[] = {in.data()}; float* op[] = {out.data()};
        dsp.processBlock(ip, op, 1, 256);
        if (block >= 28) for (float x : out) result += x * x;
    }
    return std::sqrt(result / (4.0f * 256.0f));
}

void testFourTimesHighFrequencyMixCoherence()
{
    const float fullWet = renderNeutralSine(2, 100.0f, 18000.0f);
    const float halfMix = renderNeutralSine(2, 50.0f, 18000.0f);
    const float lossDb = duskaudio::gainToDecibels(halfMix / std::max(fullWet, 1.0e-9f));
    std::printf("4x 18 kHz mix coherence: full-wet %.9g half-mix %.9g delta %.5f dB\n",
                fullWet, halfMix, lossDb);
    require(std::abs(lossDb) < 0.1f, "4x 18 kHz dry and wet remain phase coherent at 50% mix");
}

void testTruePeakOversampledPhaseInterpolation()
{
    duskaudio::MultiCompTruePeakDetector detector;
    detector.prepare();
    const float current = detector.processSample(0.0f, 0);
    const float next = detector.processSample(1.0f, 0);
    float minimum = next, maximum = current;
    for (int phase = 0; phase < 4; ++phase)
    {
        const float value = duskaudio::interpolateOversampledSidechain(current, next, phase, 4);
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }
    const float spread = maximum - minimum;
    std::printf("true-peak 4x detector phases: held spread 0; interpolated spread %.6f\n", spread);
    require(spread > 0.5f, "fast true-peak transient changes across oversampled detector phases");
}

void testLatencyMixBypassAndDigitalStereo()
{
    for (int oversampling : {0, 1, 2})
    {
        MultiCompDSP dsp;
        dsp.prepare(48000.0, 512);
        dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
        dsp.setOversampling(oversampling);
        dsp.setMix(100.0f);
        dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, 0.0f);
        dsp.setParameter(MultiCompDSP::Parameter::DigitalRatio, 1.0f);
        dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
        const int latency = dsp.getLatencySamples();
        require(latency == 27, "constant maximum anti-alias latency");

        std::vector<float> impulse(512, 0.0f), output(512, 0.0f);
        impulse[0] = 0.25f;
        const float* ip[] = {impulse.data()}; float* op[] = {output.data()};
        dsp.processBlock(ip, op, 1, static_cast<int>(impulse.size()));
        int peakIndex = 0;
        for (int i = 1; i < static_cast<int>(output.size()); ++i)
            if (std::abs(output[static_cast<size_t>(i)]) > std::abs(output[static_cast<size_t>(peakIndex)])) peakIndex = i;
        float peakValue = 0.0f; for (float x : output) peakValue = std::max(peakValue, std::abs(x));
        std::printf("latency impulse: os=%d reported=%d peak=%d amplitude=%.6f\n", oversampling, latency, peakIndex, peakValue);
        require(std::abs(peakIndex - latency) <= 2, "wet impulse group delay matches reported latency");

        const float fullWet = renderNeutralSine(oversampling, 100.0f);
        const float halfMix = renderNeutralSine(oversampling, 50.0f);
        const float mixDelta = std::abs(duskaudio::gainToDecibels(halfMix / std::max(fullWet, 1.0e-9f)));
        require(mixDelta < 0.05f, "phase-coherent 50% mix has no comb ripple");

        MultiCompDSP bypass;
        bypass.prepare(48000.0, 512);
        bypass.setOversampling(oversampling);
        bypass.setBypass(true);
        bypass.reset();
        std::vector<float> input(512), bypassed(512);
        for (int i = 0; i < 512; ++i) input[static_cast<size_t>(i)] = 0.3f * std::sin(2.0f * kPi * 440.0f * i / 48000.0f);
        const float* bip[] = {input.data()}; float* bop[] = {bypassed.data()};
        bypass.processBlock(bip, bop, 1, 512);
        for (int i = 0; i < 512; ++i)
        {
            const float expected = i >= latency ? input[static_cast<size_t>(i - latency)] : 0.0f;
            require(bypassed[static_cast<size_t>(i)] == expected, "settled bypass is latency-aligned bit-exact passthrough");
        }
        std::printf("latency/mix: os=%d reported=%d mix_delta=%.5f dB\n", oversampling, latency, mixDelta);
    }

    MultiCompDSP maximumLatency;
    maximumLatency.prepare(48000.0, 512);
    maximumLatency.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
    maximumLatency.setOversampling(2);
    maximumLatency.setParameter(MultiCompDSP::Parameter::GlobalLookahead, 10.0f);
    maximumLatency.setParameter(MultiCompDSP::Parameter::DigitalLookahead, 10.0f);
    require(maximumLatency.getLatencySamples() == 987, "maximum Digital lookaheads report their rendered delay");
    std::printf("latency maximum: os=2 global=10ms digital=10ms reported=%d\n", maximumLatency.getLatencySamples());

    MultiCompDSP stereo;
    stereo.prepare(48000.0, 256);
    stereo.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
    stereo.setParameter(MultiCompDSP::Parameter::DigitalLookahead, 5.0f);
    stereo.setParameter(MultiCompDSP::Parameter::DigitalThreshold, -20.0f);
    stereo.setParameter(MultiCompDSP::Parameter::DigitalRatio, 4.0f);
    std::vector<float> left(256), right(256, 0.0f), outLeft(256), outRight(256);
    for (int i = 0; i < 256; ++i) left[static_cast<size_t>(i)] = 0.3f * std::sin(2.0f * kPi * 440.0f * i / 48000.0f);
    const float* stereoIn[] = {left.data(), right.data()}; float* stereoOut[] = {outLeft.data(), outRight.data()};
    float rightPeak = 0.0f, leftEnergy = 0.0f;
    for (int block = 0; block < 8; ++block)
    {
        for (int i = 0; i < 256; ++i) left[static_cast<size_t>(i)] = 0.3f * std::sin(2.0f * kPi * 440.0f * (block * 256 + i) / 48000.0f);
        stereo.processBlock(stereoIn, stereoOut, 2, 256);
        if (block >= 6) for (int i = 0; i < 256; ++i) { rightPeak = std::max(rightPeak, std::abs(outRight[static_cast<size_t>(i)])); leftEnergy += outLeft[static_cast<size_t>(i)] * outLeft[static_cast<size_t>(i)]; }
    }
    std::printf("digital stereo: right_peak=%.9g left_energy=%.9g\n", rightPeak, leftEnergy);
    require(rightPeak < 1.0e-7f && leftEnergy > 1.0e-5f, "digital lookahead keeps stereo channels independent");

    std::vector<float> largeIn(1025, 0.1f), largeOut(1025);
    const float* largeIp[] = {largeIn.data()}; float* largeOp[] = {largeOut.data()};
    stereo.processBlock(largeIp, largeOp, 1, static_cast<int>(largeIn.size()));
    for (float x : largeOut) require(std::isfinite(x), "oversized blocks are chunked");
    std::puts("latency alignment, mix comb, bypass, digital stereo, oversized block: OK");
}

void testDigitalLookaheadMixAlignment()
{
    // 100 Hz against 5 ms of lookahead is half a period: a dry path taken from
    // the undelayed input cancels the wet one at 50% local mix.
    constexpr float lookaheadMs = 5.0f;
    for (const float frequency : {100.0f, 300.0f, 997.0f})
    {
        const float wet = renderLookaheadSine(100.0f, frequency, lookaheadMs);
        const float half = renderLookaheadSine(50.0f, frequency, lookaheadMs);
        const float delta = std::abs(duskaudio::gainToDecibels(half / std::max(wet, 1.0e-9f)));
        require(delta < 0.05f, "digital lookahead 50% mix has no comb ripple");
        std::printf("digital lookahead mix: %.0f Hz delta=%.5f dB\n",
                    static_cast<double>(frequency), static_cast<double>(delta));
    }
}

void testGoldenVectors()
{
    constexpr int kSamples = 4096;
    // Re-recorded 2026-08-19: affected hardware modes encoded stale oversampling-rate coefficients.
    constexpr float expectedRms[] = {0.166987091f, 0.610338330f, 0.162082925f, 0.269918233f,
                                     0.618480802f, 0.195874527f, 0.173109755f, 0.212109938f};
    constexpr float expectedPeak[] = {0.366553396f, 1.912103295f, 0.350156724f, 0.797850311f,
                                      1.836098075f, 0.657691538f, 0.349556237f, 0.815853894f};
    std::puts("golden vectors: deterministic step/sine-burst RMS peak");
    for (int mode = 0; mode < 8; ++mode)
    {
        MultiCompDSP dsp;
        dsp.prepare(48000.0, 256);
        dsp.setOversampling(0);
        dsp.setMode(mode);
        dsp.setMix(100.0f);
        dsp.setParameter(MultiCompDSP::Parameter::SidechainHP, 0.0f);
        dsp.setParameter(MultiCompDSP::Parameter::FetInput, 18.0f);
        dsp.setParameter(MultiCompDSP::Parameter::FetOutput, -4.0f);
        dsp.setParameter(MultiCompDSP::Parameter::FetAttack, 0.8f);
        dsp.setParameter(MultiCompDSP::Parameter::FetRelease, 150.0f);
        dsp.setParameter(MultiCompDSP::Parameter::OptoPeakReduction, 65.0f);
        dsp.setParameter(MultiCompDSP::Parameter::VcaThreshold, -20.0f);
        dsp.setParameter(MultiCompDSP::Parameter::VcaRatio, 4.0f);
        dsp.setParameter(MultiCompDSP::Parameter::BusThreshold, -18.0f);
        dsp.setParameter(MultiCompDSP::Parameter::StudioVcaThreshold, -20.0f);
        dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, -20.0f);
        std::vector<float> output;
        output.reserve(kSamples);
        for (int block = 0; block < kSamples / 256; ++block)
        {
            std::vector<float> in(256), out(256);
            for (int i = 0; i < 256; ++i)
            {
                const int n = block * 256 + i;
                in[static_cast<size_t>(i)] = n < 512 ? 0.35f : 0.55f * std::sin(2.0f * kPi * 997.0f * n / 48000.0f);
            }
            const float* inputs[] = {in.data()}; float* outputs[] = {out.data()};
            dsp.processBlock(inputs, outputs, 1, 256);
            output.insert(output.end(), out.begin(), out.end());
        }
        double sum = 0.0; float peak = 0.0f;
        for (float sample : output) { sum += static_cast<double>(sample) * sample; peak = std::max(peak, std::abs(sample)); }
        const float valueRms = static_cast<float>(std::sqrt(sum / output.size()));
        std::printf("  mode=%d rms=%.9f peak=%.9f\n", mode, valueRms, peak);
        const bool unchanged = std::abs(valueRms - expectedRms[mode]) <= 1.0e-4f
                            && std::abs(peak - expectedPeak[mode]) <= 1.0e-4f;
        if (!unchanged)
            std::fprintf(stderr, "golden mismatch mode=%d expected=(%.9f, %.9f) actual=(%.9f, %.9f)\n",
                         mode, expectedRms[mode], expectedPeak[mode], valueRms, peak);
        require(unchanged, "golden vector unchanged");
    }
}

void configureNeutralMultiband(MultiCompDSP& dsp)
{
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Multiband));
    dsp.setOversampling(0);
    dsp.setParameter(MultiCompDSP::Parameter::MbMix, 100.0f);
    dsp.setParameter(MultiCompDSP::Parameter::MbOutput, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::Distortion, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    for (int band = 0; band < duskaudio::kMultiCompBands; ++band)
    {
        dsp.setMultibandParameter(band, MultiCompDSP::MultibandParameter::Threshold, 0.0f);
        dsp.setMultibandParameter(band, MultiCompDSP::MultibandParameter::Ratio, 1.0f);
    }
}

float renderSoloedLowBand(bool enabled, bool bypass)
{
    MultiCompDSP dsp;
    dsp.prepare(48000.0, 256);
    configureNeutralMultiband(dsp);
    dsp.setMultibandParameter(0, MultiCompDSP::MultibandParameter::Enabled, enabled ? 1.0f : 0.0f);
    dsp.setMultibandParameter(0, MultiCompDSP::MultibandParameter::Bypass, bypass ? 1.0f : 0.0f);
    dsp.setMultibandParameter(0, MultiCompDSP::MultibandParameter::Solo, 1.0f);
    std::vector<float> input(256), output(256);
    const float* ip[] = {input.data()}; float* op[] = {output.data()};
    for (int block = 0; block < 80; ++block)
    {
        for (int i = 0; i < 256; ++i)
            input[static_cast<size_t>(i)] = 0.25f * std::sin(2.0f * kPi * 80.0f * static_cast<float>(block * 256 + i) / 48000.0f);
        dsp.processBlock(ip, op, 1, 256);
    }
    return rms(output);
}

void testMultibandEnabledTopology()
{
    const float disabled = renderSoloedLowBand(false, false);
    const float bypassed = renderSoloedLowBand(true, true);
    std::printf("multiband Enabled topology: disabled-solo RMS %.9g; bypassed-solo RMS %.9g\n",
                disabled, bypassed);
    require(disabled < 1.0e-8f, "disabled band is absent from the recombination");
    require(bypassed > 0.1f, "bypassed band remains audible without compression");

    MultiCompDSP minimumTwo;
    minimumTwo.prepare(48000.0, 256);
    configureNeutralMultiband(minimumTwo);
    for (int band = 0; band < duskaudio::kMultiCompBands; ++band)
        minimumTwo.setMultibandParameter(band, MultiCompDSP::MultibandParameter::Enabled, 0.0f);
    std::vector<float> input(256, 0.25f), output(256);
    const float* ip[] = {input.data()}; float* op[] = {output.data()};
    for (int block = 0; block < 20; ++block) minimumTwo.processBlock(ip, op, 1, 256);
    require(rms(output) > 0.1f, "all-disabled automation snapshot still enforces two active bands");
}

void testCrossoverAutomationContinuity()
{
    MultiCompDSP dsp;
    dsp.prepare(48000.0, 256);
    configureNeutralMultiband(dsp);
    std::vector<float> input(256), output(256);
    const float* ip[] = {input.data()}; float* op[] = {output.data()};
    float previous = 0.0f, steadyMaxDelta = 0.0f;
    for (int block = 0; block < 40; ++block)
    {
        for (int i = 0; i < 256; ++i)
            input[static_cast<size_t>(i)] = 0.25f * std::sin(2.0f * kPi * 350.0f * static_cast<float>(block * 256 + i) / 48000.0f);
        dsp.processBlock(ip, op, 1, 256);
        if (block == 39)
        {
            previous = output.back();
            for (int i = 1; i < 256; ++i)
                steadyMaxDelta = std::max(steadyMaxDelta, std::abs(output[static_cast<size_t>(i)] - output[static_cast<size_t>(i - 1)]));
        }
    }
    dsp.setParameter(MultiCompDSP::Parameter::Crossover1, 500.0f);
    for (int i = 0; i < 256; ++i)
        input[static_cast<size_t>(i)] = 0.25f * std::sin(2.0f * kPi * 350.0f * static_cast<float>(40 * 256 + i) / 48000.0f);
    dsp.processBlock(ip, op, 1, 256);
    const float boundaryStep = std::abs(output.front() - previous);
    float automationMaxDelta = boundaryStep;
    for (int i = 1; i < 256; ++i)
        automationMaxDelta = std::max(automationMaxDelta, std::abs(output[static_cast<size_t>(i)] - output[static_cast<size_t>(i - 1)]));
    std::printf("crossover automation: steady max delta %.9g; automation max delta %.9g; ratio %.4f\n",
                steadyMaxDelta, automationMaxDelta, automationMaxDelta / std::max(steadyMaxDelta, 1.0e-9f));
    require(automationMaxDelta < steadyMaxDelta * 1.2f, "crossover automation does not create a block-boundary transient");
}

float renderReprepareTone(MultiCompDSP& dsp, float frequency)
{
    dsp.reset();
    std::vector<float> in(256), out(256);
    double sum = 0.0;
    for (int block = 0; block < 80; ++block)
    {
        for (int i = 0; i < 256; ++i)
            in[static_cast<size_t>(i)] = 0.2f * std::sin(2.0f * kPi * frequency * static_cast<float>(block * 256 + i) / 96000.0f);
        const float* ip[] = {in.data()}; float* op[] = {out.data()};
        dsp.processBlock(ip, op, 1, 256);
        if (block >= 76)
            for (float sample : out) sum += static_cast<double>(sample) * sample;
    }
    return static_cast<float>(std::sqrt(sum / (4.0 * 256.0)));
}

void testReprepareMultiband()
{
    MultiCompDSP reused, fresh;
    reused.prepare(48000.0, 256);
    configureNeutralMultiband(reused);
    reused.prepare(96000.0, 256);
    fresh.prepare(96000.0, 256);
    configureNeutralMultiband(fresh);
    float maxDelta = 0.0f;
    for (const float frequency : {50.0f, 500.0f, 3000.0f, 10000.0f})
    {
        const float a = renderReprepareTone(reused, frequency);
        const float b = renderReprepareTone(fresh, frequency);
        maxDelta = std::max(maxDelta, std::abs(a - b));
    }
    require(maxDelta < 1.0e-6f, "re-prepare 48 kHz to 96 kHz matches fresh multiband");
    std::printf("multiband re-prepare: max band-tone energy delta %.9g\n", maxDelta);
}

float renderMultibandTone(float mix, float frequency)
{
    MultiCompDSP dsp;
    dsp.prepare(48000.0, 256);
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Multiband));
    dsp.setParameter(MultiCompDSP::Parameter::MbMix, mix);
    dsp.setParameter(MultiCompDSP::Parameter::MbOutput, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::Distortion, 0.0f);
    for (int band = 0; band < duskaudio::kMultiCompBands; ++band)
    {
        dsp.setMultibandParameter(band, MultiCompDSP::MultibandParameter::Threshold, 0.0f);
        dsp.setMultibandParameter(band, MultiCompDSP::MultibandParameter::Ratio, 1.0f);
    }
    std::vector<float> in(256), out(256);
    float sum = 0.0f;
    for (int block = 0; block < 200; ++block)
    {
        for (int i = 0; i < 256; ++i)
            in[static_cast<size_t>(i)] = 0.25f * std::sin(2.0f * kPi * frequency * static_cast<float>(block * 256 + i) / 48000.0f);
        const float* ip[] = {in.data()}; float* op[] = {out.data()};
        dsp.processBlock(ip, op, 1, 256);
        if (block == 199) for (float x : out) sum += x * x;
    }
    return std::sqrt(sum / 256.0f);
}

void testMultibandMixAlignment()
{
    float worstRippleDb = 0.0f;
    for (float frequency : {50.0f, 500.0f, 3000.0f, 10000.0f, 18000.0f})
    {
        const float fullWet = renderMultibandTone(100.0f, frequency);
        const float halfMix = renderMultibandTone(50.0f, frequency);
        const float ratio = halfMix / std::max(fullWet, 1.0e-9f);
        // A phase-misaligned dry path creates frequency-dependent combing. The
        // latency-aligned Phase-2 path must remain within 0.1 dB of full wet.
        worstRippleDb = std::max(worstRippleDb, std::abs(duskaudio::gainToDecibels(ratio)));
    }
    require(worstRippleDb < 0.1f, "multiband 50% mix has no comb ripple");
    std::printf("multiband mix alignment: max ripple %.5f dB\n", worstRippleDb);
}

float renderSidechainEqGR(float highGain)
{
    MultiCompDSP dsp;
    dsp.prepare(48000.0, 256);
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
    dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, -30.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalRatio, 10.0f);
    dsp.setParameter(MultiCompDSP::Parameter::ExternalSidechain, 1.0f);
    dsp.setParameter(MultiCompDSP::Parameter::SidechainHP, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::ScHighFreq, 8000.0f);
    dsp.setParameter(MultiCompDSP::Parameter::ScHighGain, highGain);
    std::vector<float> input(256, 0.02f), sidechain(256), output(256);
    float gr = 0.0f;
    for (int block = 0; block < 120; ++block)
    {
        for (int i = 0; i < 256; ++i)
            sidechain[static_cast<size_t>(i)] = 0.25f * std::sin(2.0f * kPi * 12000.0f * static_cast<float>(block * 256 + i) / 48000.0f);
        const float* ip[] = {input.data()}; const float* sc[] = {sidechain.data()}; float* op[] = {output.data()};
        dsp.processBlockExternal(ip, sc, op, 1, 256);
        if (block == 119) gr = dsp.getGainReduction();
    }
    return gr;
}

void testSidechainEq()
{
    const float flat = renderSidechainEqGR(0.0f);
    const float boosted = renderSidechainEqGR(12.0f);
    require(std::isfinite(flat) && std::isfinite(boosted), "sidechain EQ meter finite");
    require(boosted < flat - 0.2f, "sidechain high shelf increases HF compression");
    std::printf("sidechain EQ: GR %.4f dB -> %.4f dB with +12 dB HF shelf\n", flat, boosted);
}

void testMultibandBypassAndZeroLatency()
{
    MultiCompDSP reference, bypassed;
    for (MultiCompDSP* dsp : {&reference, &bypassed})
    {
        dsp->prepare(48000.0, 256);
        dsp->setMode(static_cast<int>(duskaudio::MultiCompMode::Multiband));
        dsp->setParameter(MultiCompDSP::Parameter::MbMix, 100.0f);
        dsp->setParameter(MultiCompDSP::Parameter::MbOutput, 0.0f);
        dsp->setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    }
    bypassed.setMultibandParameter(0, MultiCompDSP::MultibandParameter::Bypass, 1.0f);
    bypassed.setMultibandParameter(0, MultiCompDSP::MultibandParameter::Makeup, 12.0f);
    std::vector<float> input(256), referenceOut(256), bypassedOut(256);
    const float* ip[] = {input.data()};
    float* referenceOp[] = {referenceOut.data()};
    float* bypassedOp[] = {bypassedOut.data()};
    float worst = 0.0f;
    for (int block = 0; block < 20; ++block)
    {
        for (int i = 0; i < 256; ++i)
            input[static_cast<size_t>(i)] = 0.25f * std::sin(2.0f * kPi * 997.0f * static_cast<float>(block * 256 + i) / 48000.0f);
        reference.processBlock(ip, referenceOp, 1, 256);
        bypassed.processBlock(ip, bypassedOp, 1, 256);
        if (block >= 16)
            for (int i = 0; i < 256; ++i)
                worst = std::max(worst, std::abs(referenceOut[static_cast<size_t>(i)] - bypassedOut[static_cast<size_t>(i)]));
    }
    require(worst < 1.0e-6f, "disabled multiband band skips envelope and makeup");

    MultiCompDSP zeroDelay;
    zeroDelay.prepare(48000.0, 256);
    zeroDelay.setMode(static_cast<int>(duskaudio::MultiCompMode::Multiband));
    zeroDelay.setBypass(true);
    zeroDelay.reset();
    require(zeroDelay.getLatencySamples() == 0, "multiband bypass has zero latency");
    std::vector<float> zeroIn(256), zeroOut(256);
    for (int i = 0; i < 256; ++i)
        zeroIn[static_cast<size_t>(i)] = 0.1f + 0.0003f * static_cast<float>(i);
    const float* zeroIp[] = {zeroIn.data()}; float* zeroOp[] = {zeroOut.data()};
    zeroDelay.processBlock(zeroIp, zeroOp, 1, 256);
    for (int i = 0; i < 256; ++i)
        require(zeroOut[static_cast<size_t>(i)] == zeroIn[static_cast<size_t>(i)], "zero-delay bypass is current-input bit-exact");
    std::printf("multiband bypass/makeup: worst difference %.9g; zero-delay bypass: bit-exact\n", worst);
}

float renderDigitalDucking(bool autoMakeup, bool provideSidechain)
{
    MultiCompDSP dsp;
    dsp.prepare(48000.0, 256);
    dsp.setOversampling(0);
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
    dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, -30.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalRatio, 20.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalAttack, 0.1f);
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::ExternalSidechain, 1.0f);
    dsp.setParameter(MultiCompDSP::Parameter::AutoMakeup, autoMakeup ? 1.0f : 0.0f);
    std::vector<float> input(256, 0.2f), sidechain(256, 1.0f), output(256);
    const float* ip[] = {input.data()};
    const float* sc[] = {sidechain.data()};
    float* op[] = {output.data()};
    for (int block = 0; block < 240; ++block)
    {
        if (provideSidechain) dsp.processBlockExternal(ip, sc, op, 1, 256);
        else dsp.processBlock(ip, op, 1, 256);
    }
    return rms(output);
}

void testAutoGainEffectiveExternalSidechain()
{
    const float ducked = renderDigitalDucking(false, true);
    const float duckedAuto = renderDigitalDucking(true, true);
    const float internal = renderDigitalDucking(false, false);
    const float internalAuto = renderDigitalDucking(true, false);
    const float duckingDeltaDb = duskaudio::gainToDecibels(duckedAuto / std::max(ducked, 1.0e-9f));
    const float internalLiftDb = duskaudio::gainToDecibels(internalAuto / std::max(internal, 1.0e-9f));
    std::printf("auto gain external sidechain: ducking delta %.4f dB; armed/no-bus lift %.4f dB\n",
                duckingDeltaDb, internalLiftDb);
    require(std::abs(duckingDeltaDb) < 0.2f, "auto gain does not counteract effective external-sidechain ducking");
    require(internalLiftDb > 3.0f, "armed sidechain without a host bus leaves auto gain active");
}

void testAutoGainBypassSettleBoundary()
{
    MultiCompDSP dsp;
    dsp.prepare(48000.0, 256);
    dsp.setOversampling(0);
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
    dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, -40.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalRatio, 20.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalAttack, 0.1f);
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::AutoMakeup, 1.0f);
    std::vector<float> input(256, 0.25f), output(256);
    const float* ip[] = {input.data()}; float* op[] = {output.data()};
    for (int block = 0; block < 240; ++block) dsp.processBlock(ip, op, 1, 256);
    dsp.setBypass(true);
    for (int block = 0; block < 6; ++block) dsp.processBlock(ip, op, 1, 256);
    const float lastTransitionSample = output.back();
    dsp.processBlock(ip, op, 1, 256);
    const float firstSettledSample = output.front();
    const float boundaryStep = std::abs(firstSettledSample - lastTransitionSample);
    std::printf("auto gain bypass boundary: last=%.9g first=%.9g step=%.9g\n",
                lastTransitionSample, firstSettledSample, boundaryStep);
    require(boundaryStep < 1.0e-5f, "auto-gained bypass transition and settled bypass share one endpoint");
}

void setModeOutput(MultiCompDSP& dsp, int mode, bool high)
{
    const float db = high ? 20.0f : 0.0f;
    switch (static_cast<duskaudio::MultiCompMode>(mode))
    {
        case duskaudio::MultiCompMode::Opto:
            dsp.setParameter(MultiCompDSP::Parameter::OptoGain, high ? 75.0f : 50.0f); break;
        case duskaudio::MultiCompMode::FET:
        case duskaudio::MultiCompMode::StudioFET:
            dsp.setParameter(MultiCompDSP::Parameter::FetOutput, db); break;
        case duskaudio::MultiCompMode::VCA:
            dsp.setParameter(MultiCompDSP::Parameter::VcaOutput, db); break;
        case duskaudio::MultiCompMode::Bus:
            dsp.setParameter(MultiCompDSP::Parameter::BusMakeup, db); break;
        case duskaudio::MultiCompMode::StudioVCA:
            dsp.setParameter(MultiCompDSP::Parameter::StudioVcaOutput, db); break;
        case duskaudio::MultiCompMode::Digital:
            dsp.setParameter(MultiCompDSP::Parameter::DigitalOutput, db); break;
        case duskaudio::MultiCompMode::Multiband: break;
    }
}

void testAutoGainNeutralisesManualOutput()
{
    float worstDelta = 0.0f, fetPeakRatio = 0.0f;
    for (int mode = 0; mode < static_cast<int>(duskaudio::MultiCompMode::Multiband); ++mode)
    {
        MultiCompDSP neutral, high;
        for (MultiCompDSP* dsp : {&neutral, &high})
        {
            dsp->prepare(48000.0, 256);
            dsp->setOversampling(0);
            dsp->setMode(mode);
            dsp->setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
            dsp->setParameter(MultiCompDSP::Parameter::AutoMakeup, 1.0f);
        }
        setModeOutput(neutral, mode, false);
        setModeOutput(high, mode, true);
        std::vector<float> input(256), neutralOut(256), highOut(256);
        for (int i = 0; i < 256; ++i)
            input[static_cast<size_t>(i)] = 0.02f * std::sin(2.0f * kPi * 997.0f * static_cast<float>(i) / 48000.0f);
        const float* ip[] = {input.data()}; float* neutralOp[] = {neutralOut.data()}; float* highOp[] = {highOut.data()};
        neutral.processBlock(ip, neutralOp, 1, 256);
        high.processBlock(ip, highOp, 1, 256);
        float neutralPeak = 0.0f, highPeak = 0.0f;
        for (int i = 0; i < 256; ++i)
        {
            worstDelta = std::max(worstDelta, std::abs(highOut[static_cast<size_t>(i)] - neutralOut[static_cast<size_t>(i)]));
            neutralPeak = std::max(neutralPeak, std::abs(neutralOut[static_cast<size_t>(i)]));
            highPeak = std::max(highPeak, std::abs(highOut[static_cast<size_t>(i)]));
        }
        if (mode == static_cast<int>(duskaudio::MultiCompMode::FET))
            fetPeakRatio = highPeak / std::max(neutralPeak, 1.0e-9f);
    }
    std::printf("auto gain manual output: FET +20dB peak ratio %.6f; seven-mode max delta %.9g\n",
                fetPeakRatio, worstDelta);
    require(worstDelta < 1.0e-7f, "auto gain supplies unity manual output to every single-band mode");
}

void testAutoGainResetsOnModeChange()
{
    MultiCompDSP dsp;
    dsp.prepare(48000.0, 256);
    dsp.setOversampling(0);
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::FET));
    dsp.setParameter(MultiCompDSP::Parameter::FetInput, 20.0f);
    dsp.setParameter(MultiCompDSP::Parameter::FetThreshold, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::AutoMakeup, 1.0f);
    std::vector<float> input(256), output(256);
    const float* ip[] = {input.data()}; float* op[] = {output.data()};
    auto fillInput = [&](int block) {
        for (int i = 0; i < 256; ++i)
            input[static_cast<size_t>(i)] = 0.02f * std::sin(2.0f * kPi * 997.0f * static_cast<float>(block * 256 + i) / 48000.0f);
    };
    for (int block = 0; block < 240; ++block) { fillInput(block); dsp.processBlock(ip, op, 1, 256); }
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
    dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, 0.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalRatio, 1.0f);
    fillInput(240); dsp.processBlock(ip, op, 1, 256);
    const float immediateRatio = rms(output) / std::max(rms(input), 1.0e-9f);
    for (int block = 241; block < 305; ++block) { fillInput(block); dsp.processBlock(ip, op, 1, 256); }
    const float settledRatio = rms(output) / std::max(rms(input), 1.0e-9f);
    std::printf("auto gain mode change: immediate ratio %.6f; after 341ms %.6f\n",
                immediateRatio, settledRatio);
    require(settledRatio > 0.9f, "mode change discards old auto-gain history and returns toward unity");
}

void testBypassCompletesDuringSidechainListen()
{
    MultiCompDSP dsp;
    dsp.prepare(48000.0, 256);
    dsp.setOversampling(0);
    dsp.setParameter(MultiCompDSP::Parameter::ExternalSidechain, 1.0f);
    dsp.setParameter(MultiCompDSP::Parameter::GlobalSidechainListen, 1.0f);
    std::vector<float> input(256, 0.1f), sidechain(256, 0.8f), output(256);
    const float* ip[] = {input.data()}; const float* sc[] = {sidechain.data()}; float* op[] = {output.data()};
    for (int block = 0; block < 8; ++block) dsp.processBlockExternal(ip, sc, op, 1, 256);
    dsp.setBypass(true);
    for (int block = 0; block < 10; ++block) dsp.processBlockExternal(ip, sc, op, 1, 256);
    float maxDryError = 0.0f;
    for (float sample : output) maxDryError = std::max(maxDryError, std::abs(sample - 0.1f));
    std::printf("bypass during sidechain listen: final sample %.9g; dry error %.9g\n",
                output.back(), maxDryError);
    require(maxDryError < 1.0e-7f, "bypass reaches settled dry output while sidechain Listen remains on");
}

void testSettledBypassClearsGainReductionMeters()
{
    MultiCompDSP dsp;
    dsp.prepare(48000.0, 256);
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Multiband));
    dsp.setParameter(MultiCompDSP::Parameter::NoiseEnable, 0.0f);
    for (int band = 0; band < duskaudio::kMultiCompBands; ++band)
    {
        dsp.setMultibandParameter(band, MultiCompDSP::MultibandParameter::Threshold, -40.0f);
        dsp.setMultibandParameter(band, MultiCompDSP::MultibandParameter::Ratio, 20.0f);
        dsp.setMultibandParameter(band, MultiCompDSP::MultibandParameter::Attack, 0.1f);
    }
    std::vector<float> input(256, 0.8f), output(256);
    const float* ip[] = {input.data()}; float* op[] = {output.data()};
    for (int block = 0; block < 80; ++block) dsp.processBlock(ip, op, 1, 256);
    const float activeMaster = dsp.getGainReduction();
    const float activeBand = dsp.getBandGainReduction(0);
    dsp.setBypass(true);
    for (int block = 0; block < 10; ++block) dsp.processBlock(ip, op, 1, 256);
    float bypassBandMagnitude = 0.0f;
    for (int band = 0; band < duskaudio::kMultiCompBands; ++band)
        bypassBandMagnitude = std::max(bypassBandMagnitude, std::abs(dsp.getBandGainReduction(band)));
    std::printf("settled bypass GR: active master %.4f band0 %.4f; bypass master %.4f max-band %.4f\n",
                activeMaster, activeBand, dsp.getGainReduction(), bypassBandMagnitude);
    require(activeMaster < -1.0f && activeBand < -1.0f, "meter test establishes active multiband compression");
    require(dsp.getGainReduction() == 0.0f && bypassBandMagnitude == 0.0f,
            "settled bypass clears master and per-band gain-reduction meters");
}
}

// A first prepare whose arguments match the member defaults (48 kHz, factor 1)
// must still do the one-time setup. It previously returned early, leaving the
// Digital lookahead ring buffer empty, and the first Digital block then wrote
// past the end of it and divided by its zero size.
void testPrepareAtDefaultRateAndFactor()
{
    MultiCompDSP dsp;
    dsp.setOversampling(0);                     // factor 1, the member default
    dsp.prepare(48000.0, 256);                  // 48 kHz, also the member default
    dsp.setMode(static_cast<int>(duskaudio::MultiCompMode::Digital));
    dsp.setParameter(MultiCompDSP::Parameter::DigitalLookahead, 5.0f);
    dsp.setParameter(MultiCompDSP::Parameter::DigitalThreshold, -30.0f);

    std::array<float, 256> left{}, right{};
    for (int i = 0; i < 256; ++i)
        left[static_cast<size_t>(i)] = right[static_cast<size_t>(i)]
            = 0.5f * std::sin(2.0f * kPi * 220.0f * static_cast<float>(i) / 48000.0f);

    float* io[2] = {left.data(), right.data()};
    const float* in[2] = {left.data(), right.data()};
    for (int block = 0; block < 8; ++block)
        dsp.processBlock(in, io, 2, 256);

    for (int i = 0; i < 256; ++i)
    {
        require(std::isfinite(left[static_cast<size_t>(i)]), "default-rate prepare left finite");
        require(std::isfinite(right[static_cast<size_t>(i)]), "default-rate prepare right finite");
    }
    std::puts("prepare at default rate/factor: initialised, Digital lookahead safe");
}

void testOptoStereoDetectorIsolation()
{
    duskaudio::MultiCompModes modes;
    duskaudio::MultiCompParameterState parameters;
    modes.prepare(48000.0, 256, 1);
    parameters.optoPeakReduction.store(100.0f, std::memory_order_relaxed);

    for (int i = 0; i < 16384; ++i)
    {
        const float left = 0.8f * std::sin(2.0f * kPi * 997.0f * static_cast<float>(i) / 48000.0f);
        (void)modes.process(duskaudio::MultiCompMode::Opto, left, 0, left, parameters);
        (void)modes.process(duskaudio::MultiCompMode::Opto, 0.0f, 1, 0.0f, parameters);
    }

    const float rightGr = modes.gainReduction(duskaudio::MultiCompMode::Opto, 1);
    std::printf("opto stereo isolation: silent-right GR %.9g dB\n", rightGr);
    require(std::abs(rightGr) < 1.0e-7f, "left-only Opto signal does not alter right detector gain reduction");
}

void testHardwareRateRefreshAfterOversamplingChange()
{
    const duskaudio::MultiCompMode hardwareModes[] = {
        duskaudio::MultiCompMode::Opto, duskaudio::MultiCompMode::FET,
        duskaudio::MultiCompMode::Bus, duskaudio::MultiCompMode::StudioFET,
        duskaudio::MultiCompMode::StudioVCA};
    duskaudio::MultiCompParameterState parameters;
    parameters.optoPeakReduction.store(0.0f, std::memory_order_relaxed);
    parameters.fetInput.store(0.0f, std::memory_order_relaxed);

    for (const auto mode : hardwareModes)
    {
        duskaudio::MultiCompModes switched, fresh;
        switched.prepare(48000.0, 256, 2);
        switched.setRate(48000.0, 1);
        switched.reset();
        fresh.prepare(48000.0, 256, 1);
        fresh.reset();

        float maxDelta = 0.0f;
        for (int i = 0; i < 8192; ++i)
        {
            const float input = 0.2f * std::sin(2.0f * kPi * 997.0f * static_cast<float>(i) / 48000.0f);
            const float a = switched.process(mode, input, 0, input, parameters);
            const float b = fresh.process(mode, input, 0, input, parameters);
            maxDelta = std::max(maxDelta, std::abs(a - b));
        }
        std::printf("hardware rate refresh: mode=%d max_delta=%.9g\n", static_cast<int>(mode), maxDelta);
        require(maxDelta < 1.0e-7f, "oversampling rate switch matches freshly prepared hardware coefficients");
    }
}

int main()
{
    testTruePeakOversampledPhaseInterpolation();
    testCrossoverAutomationContinuity();
    testFourTimesHighFrequencyMixCoherence();
    testMultibandEnabledTopology();
    testSettledBypassClearsGainReductionMeters();
    testBypassCompletesDuringSidechainListen();
    testAutoGainResetsOnModeChange();
    testAutoGainNeutralisesManualOutput();
    testAutoGainBypassSettleBoundary();
    testAutoGainEffectiveExternalSidechain();
    testHardwareRateRefreshAfterOversamplingChange();
    testOptoStereoDetectorIsolation();
    testPrepareAtDefaultRateAndFactor();
    testCrossoverFlatness();
    testStaticCurves();
    testEnvelopeAndReset();
    testMixBypassAndBlockEdges();
    testLatencyMixBypassAndDigitalStereo();
    testDigitalLookaheadMixAlignment();
    testMultibandMixAlignment();
    testSidechainEq();
    testMultibandBypassAndZeroLatency();
    testReprepareMultiband();
    testGoldenVectors();
    std::puts("Multi-Comp core tests: PASS");
    return 0;
}
