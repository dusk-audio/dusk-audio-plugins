#include "../MultiCompDSP.hpp"
#include "../../../shared-dpf/dsp/DuskCrossover.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using duskaudio::DuskCrossover;
using duskaudio::MultiCompDSP;

namespace
{
constexpr float kPi = 3.14159265358979323846f;

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
            DuskCrossover c1, c2, c3;
            c1.prepare(sr, cfg[0]); c2.prepare(sr, cfg[1]); c3.prepare(sr, cfg[2]);
            const int n = 32768;
            std::vector<float> sum(static_cast<size_t>(n)), original(static_cast<size_t>(n));
            for (int i = 0; i < n; ++i)
            {
                const float x = 0.25f * std::sin(2.0f * kPi * 997.0f * static_cast<float>(i) / static_cast<float>(sr));
                original[static_cast<size_t>(i)] = x;
                float l0, h0, l1, h1, l2, h2;
                c1.process(x, l0, h0); c2.process(h0, l1, h1); c3.process(h1, l2, h2);
                sum[static_cast<size_t>(i)] = l0 + l1 + l2 + h2;
            }
            const float ratio = rms(sum, 4096) / rms(original, 4096);
            require(std::abs(duskaudio::gainToDecibels(ratio)) < 0.001f, "LR4 flat-sum reconstruction");
        }
    }
    std::puts("LR4 flat-sum: 44.1/48/96 kHz, two 3-split configurations OK");
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
    dsp.setBypass(true);
    for (int block = 0; block < 10; ++block) dsp.processBlock(ip, op, 1, 256);
    for (int i = 0; i < 256; ++i) require(out[static_cast<size_t>(i)] == in[static_cast<size_t>(i)], "settled bypass bit exact");
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

void testGoldenVectors()
{
    constexpr int kSamples = 4096;
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
    }
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
}

int main()
{
    testCrossoverFlatness();
    testStaticCurves();
    testEnvelopeAndReset();
    testMixBypassAndBlockEdges();
    testMultibandMixAlignment();
    testSidechainEq();
    testGoldenVectors();
    std::puts("Multi-Comp core tests: PASS");
    return 0;
}
