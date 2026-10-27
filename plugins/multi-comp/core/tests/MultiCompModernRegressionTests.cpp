// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
#include "MultiCompDSP.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
using duskaudio::MultiCompDSP;
using duskaudio::MultiCompMode;
using P = MultiCompDSP::Parameter;
using B = MultiCompDSP::MultibandParameter;
constexpr int kRate = 48000;
constexpr int kBlock = 256;
constexpr double kPi = 3.14159265358979323846;
int failures = 0;
bool finiteAudio = true;

void check(const char* name, bool passed)
{
    passed = passed && finiteAudio;
    std::printf("%s: %s\n", passed ? "PASS" : "FAIL", name);
    if (!passed) ++failures;
    finiteAudio = true;
}

float tone(int sample, float frequency, float amplitude)
{
    return amplitude * static_cast<float>(std::sin(2.0 * kPi * frequency * sample / kRate));
}

void configure(MultiCompDSP& dsp, MultiCompMode mode = MultiCompMode::Multiband,
               bool compression = false)
{
    dsp.setMode(static_cast<int>(mode));
    dsp.setOversampling(0);
    dsp.setParameter(P::AutoMakeup, 0.0f);
    dsp.setParameter(P::NoiseEnable, 0.0f);
    dsp.setParameter(P::Distortion, 0.0f);
    dsp.setParameter(P::DigitalMix, 100.0f);
    dsp.setParameter(P::DigitalOutput, 0.0f);
    dsp.setParameter(P::DigitalThreshold, compression ? -30.0f : 0.0f);
    dsp.setParameter(P::DigitalRatio, compression ? 8.0f : 1.0f);
    dsp.setParameter(P::DigitalAttack, 10.0f);
    dsp.setParameter(P::DigitalRelease, 100.0f);
    dsp.setParameter(P::MbMix, 100.0f);
    dsp.setParameter(P::MbOutput, 0.0f);
    for (int band = 0; band < 4; ++band)
    {
        dsp.setMultibandParameter(band, B::Threshold, compression ? -30.0f : 0.0f);
        dsp.setMultibandParameter(band, B::Ratio, compression ? 8.0f : 1.0f);
        dsp.setMultibandParameter(band, B::Attack, 10.0f);
        dsp.setMultibandParameter(band, B::Release, 100.0f);
        dsp.setMultibandParameter(band, B::Makeup, 0.0f);
    }
}

struct Render
{
    std::array<std::vector<float>, 2> audio;
    float reduction = 0.0f;
    int latency = 0;
};

template <typename Signal>
Render renderPrepared(MultiCompDSP& dsp, int count, int channels, bool external, Signal signal)
{
    Render result;
    result.latency = dsp.getLatencySamples();
    for (auto& channel : result.audio) channel.resize(static_cast<size_t>(count));
    std::array<std::array<float, kBlock>, 2> input{}, key{}, output{};
    const float* in[] = {input[0].data(), input[1].data()};
    const float* sc[] = {key[0].data(), key[1].data()};
    float* out[] = {output[0].data(), output[1].data()};
    for (int position = 0; position < count; position += kBlock)
    {
        const int length = std::min(kBlock, count - position);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < length; ++i)
                signal(position + i, ch, input[static_cast<size_t>(ch)][static_cast<size_t>(i)],
                       key[static_cast<size_t>(ch)][static_cast<size_t>(i)]);
        if (external) dsp.processBlockExternal(in, sc, out, channels, length);
        else dsp.processBlock(in, out, channels, length);
        result.reduction = std::min(result.reduction, dsp.getGainReduction());
        for (int ch = 0; ch < channels; ++ch)
        {
            for (int i = 0; i < length; ++i)
                finiteAudio = finiteAudio && std::isfinite(output[static_cast<size_t>(ch)][static_cast<size_t>(i)]);
            std::copy_n(output[static_cast<size_t>(ch)].data(), length,
                        result.audio[static_cast<size_t>(ch)].data() + position);
        }
    }
    return result;
}

double rms(const std::vector<float>& samples, int start)
{
    double energy = 0.0;
    for (size_t i = static_cast<size_t>(start); i < samples.size(); ++i)
        energy += static_cast<double>(samples[i]) * samples[i];
    return std::sqrt(energy / (samples.size() - static_cast<size_t>(start)));
}

double delta(const std::vector<float>& a, const std::vector<float>& b, int start = 0)
{
    double largest = 0.0;
    for (size_t i = static_cast<size_t>(start); i < a.size(); ++i)
        largest = std::max(largest, std::abs(static_cast<double>(a[i]) - b[i]));
    return largest;
}

void testCrossover()
{
    // Every host-valid topology with at least two bands, including non-adjacent
    // enabled bands. A coherent multitone measures each frequency separately.
    constexpr std::array<float, 12> frequencies{{50, 100, 200, 300, 500, 600, 750, 1000, 2000, 3000, 8000, 12000}};
    constexpr int count = 65536, start = count - kRate;
    double worstDb = 0.0, minimumMagnitude = 1.0;
    int worstMask = 0, worstConfig = 0, worstFrequency = 0, cases = 0;
    for (int config = 0; config < 2; ++config)
        for (int mask = 0; mask < 16; ++mask)
        {
            int enabled = 0;
            for (int band = 0; band < 4; ++band) enabled += (mask >> band) & 1;
            if (enabled < 2) continue;
            for (float mix : {0.0f, 50.0f, 100.0f})
            {
                MultiCompDSP dsp;
                configure(dsp);
                dsp.setParameter(P::MbMix, mix);
                if (config == 1)
                {
                    dsp.setParameter(P::Crossover1, 500.0f);
                    dsp.setParameter(P::Crossover2, 750.0f);
                    dsp.setParameter(P::Crossover3, 2000.0f);
                }
                // Enable first so the public setter's two-band guard does not
                // silently repair a temporary invalid mask during this sweep.
                for (int band = 0; band < 4; ++band) dsp.setMultibandParameter(band, B::Enabled, 1.0f);
                for (int band = 0; band < 4; ++band)
                    if ((mask & (1 << band)) == 0) dsp.setMultibandParameter(band, B::Enabled, 0.0f);
                dsp.prepare(kRate, kBlock);
                const auto result = renderPrepared(dsp, count, 1, false,
                    [&](int sample, int, float& input, float& key)
                    {
                        input = 0.0f;
                        for (float hz : frequencies) input += tone(sample, hz, 0.005f);
                        key = input;
                    });
                for (float hz : frequencies)
                {
                    double real = 0.0, imag = 0.0;
                    for (int i = start; i < count; ++i)
                    {
                        const double phase = 2.0 * kPi * hz * i / kRate;
                        real += result.audio[0][static_cast<size_t>(i)] * std::sin(phase);
                        imag += result.audio[0][static_cast<size_t>(i)] * std::cos(phase);
                    }
                    const double magnitude = 2.0 * std::hypot(real, imag) / (kRate * 0.005);
                    const double db = std::abs(20.0 * std::log10(magnitude));
                    minimumMagnitude = std::min(minimumMagnitude, magnitude);
                    if (db > worstDb)
                    {
                        worstDb = db;
                        worstMask = mask;
                        worstConfig = config;
                        worstFrequency = static_cast<int>(hz);
                    }
                }
                ++cases;
            }
        }
    std::printf("crossover: renders=%d tones=%zu worst=%.9g dB mask=%d config=%d Hz=%d minimumMagnitude=%.9g\n",
                cases, frequencies.size(), worstDb, worstMask, worstConfig, worstFrequency, minimumMagnitude);
    check("neutral Multiband remains flat for every enabled topology and dry/wet endpoint", cases == 66 && minimumMagnitude > 0.9 && worstDb < 0.025);
}

void testDigitalHighPass()
{
    double minimumRelief = 1000.0, largestDryDifference = 0.0, minimumWetRms = 1.0;
    float strongestCompression = 0.0f;
    for (int channels : {1, 2})
        for (bool external : {false, true})
            for (float mix : {0.0f, 100.0f})
            {
                std::array<Render, 2> results;
                for (int filter = 0; filter < 2; ++filter)
                {
                    MultiCompDSP dsp;
                    configure(dsp, MultiCompMode::Digital, true);
                    dsp.setExternalSidechain(external);
                    dsp.setParameter(P::DigitalAttack, 1.0f);
                    dsp.setParameter(P::DigitalMix, mix);
                    dsp.setParameter(P::SidechainHP, filter == 0 ? 0.0f : 500.0f);
                    dsp.prepare(kRate, kBlock);
                    results[static_cast<size_t>(filter)] = renderPrepared(dsp, kRate, channels, external,
                        [](int sample, int ch, float& input, float& key)
                        { input = tone(sample, 50.0f, ch == 0 ? 0.25f : 0.2f); key = input; });
                }
                strongestCompression = std::min(strongestCompression, results[0].reduction);
                for (int ch = 0; ch < channels; ++ch)
                    if (mix == 0.0f)
                        largestDryDifference = std::max(largestDryDifference, delta(results[0].audio[static_cast<size_t>(ch)], results[1].audio[static_cast<size_t>(ch)]));
                    else
                    {
                        const double reference = rms(results[0].audio[static_cast<size_t>(ch)], kRate / 2);
                        minimumWetRms = std::min(minimumWetRms, reference);
                        minimumRelief = std::min(minimumRelief, 20.0 * std::log10(rms(results[1].audio[static_cast<size_t>(ch)], kRate / 2) / reference));
                    }
            }
    std::printf("Digital SC HP: minimum LF compression relief=%.9g dB dryDelta=%.9g activeGR=%.9g dB\n", minimumRelief, largestDryDifference, strongestCompression);
    check("Digital SC HP filters internal and external detectors in mono/stereo while preserving dry audio", minimumWetRms > 0.001 && minimumRelief > 3.0 && largestDryDifference < 1.0e-7 && strongestCompression < -6.0f);
}

void testLookahead()
{
    double largestPeakRatio = 0.0, largestDryDelta = 0.0;
    bool correctLatency = true, excited = true;
    float weakestCompression = -100.0f;
    for (MultiCompMode mode : {MultiCompMode::Multiband, MultiCompMode::Digital})
        for (int channels : {1, 2})
            for (bool external : {false, true})
                for (float mix : {0.0f, 100.0f})
                {
                    std::array<Render, 2> results;
                    for (int setting = 0; setting < 2; ++setting)
                    {
                        MultiCompDSP dsp;
                        configure(dsp, mode, true);
                        dsp.setExternalSidechain(external);
                        dsp.setParameter(P::GlobalLookahead, setting == 0 ? 0.0f : 10.0f);
                        dsp.setParameter(mode == MultiCompMode::Multiband ? P::MbMix : P::DigitalMix, mix);
                        dsp.prepare(kRate, kBlock);
                        results[static_cast<size_t>(setting)] = renderPrepared(dsp, 32768, channels, external,
                            [](int sample, int, float& input, float& key)
                            { input = sample >= 2048 && sample < 18000 ? tone(sample - 2048, 1000.0f, 0.6f) : 0.0f; key = input; });
                    }
                    correctLatency = correctLatency && results[1].latency - results[0].latency == 480;
                    if (mix == 0.0f)
                    {
                        for (size_t i = 0; i + 480 < results[0].audio[0].size(); ++i)
                            largestDryDelta = std::max(largestDryDelta, std::abs(static_cast<double>(results[0].audio[0][i]) - results[1].audio[0][i + 480]));
                    }
                    else
                    {
                        double originalPeak = 0.0, anticipatedPeak = 0.0;
                        for (int i = 2048; i < 2288; ++i)
                        {
                            originalPeak = std::max(originalPeak, std::abs(static_cast<double>(results[0].audio[0][static_cast<size_t>(i)])));
                            anticipatedPeak = std::max(anticipatedPeak, std::abs(static_cast<double>(results[1].audio[0][static_cast<size_t>(i + 480)])));
                        }
                        largestPeakRatio = std::max(largestPeakRatio, anticipatedPeak / originalPeak);
                        excited = excited && originalPeak > 0.1 && anticipatedPeak > 0.01;
                        weakestCompression = std::max(weakestCompression, results[1].reduction);
                        std::printf("lookahead: mode=%d channels=%d external=%d peak0=%.9g peak10=%.9g ratio=%.9g\n", static_cast<int>(mode), channels, external, originalPeak, anticipatedPeak, anticipatedPeak / originalPeak);
                    }
                }
    std::printf("lookahead: worst onset ratio=%.9g dryDelta=%.9g weakestGR=%.9g dB latencyCorrect=%d\n", largestPeakRatio, largestDryDelta, weakestCompression, correctLatency);
    check("global lookahead anticipates internal and external compression without altering aligned dry audio", excited && correctLatency && largestPeakRatio < 0.8 && largestDryDelta < 1.0e-6 && weakestCompression < -6.0f);
}

void testMidSideKey()
{
    double largestOrthogonalDifference = 0.0, minimumMatchingAttenuation = 1000.0;
    double minimumReferenceRms = 1.0;
    for (MultiCompMode mode : {MultiCompMode::Multiband, MultiCompMode::Digital})
        for (int audioSide = 0; audioSide < 2; ++audioSide)
        {
            std::array<double, 3> levels{};
            // Silent, orthogonal, matching key. Both audio components and
            // both external-key components must exercise real compression.
            for (int keyKind = 0; keyKind < 3; ++keyKind)
            {
                MultiCompDSP dsp;
                configure(dsp, mode, true);
                dsp.setParameter(P::StereoLinkMode, 1.0f);
                dsp.setExternalSidechain(true);
                dsp.prepare(kRate, kBlock);
                auto result = renderPrepared(dsp, 65536, 2, true,
                    [&](int sample, int ch, float& input, float& key)
                    {
                        input = tone(sample, 1000.0f, 0.15f) * (ch == 1 && audioSide ? -1.0f : 1.0f);
                        const bool keySide = keyKind == 2 ? audioSide != 0 : audioSide == 0;
                        key = keyKind == 0 ? 0.0f : tone(sample, 1000.0f, 0.6f) * (ch == 1 && keySide ? -1.0f : 1.0f);
                    });
                levels[static_cast<size_t>(keyKind)] = rms(result.audio[0], 32768);
            }
            minimumReferenceRms = std::min(minimumReferenceRms, levels[0]);
            const double orthogonal = std::abs(20.0 * std::log10(levels[1] / levels[0]));
            const double matching = -20.0 * std::log10(levels[2] / levels[0]);
            largestOrthogonalDifference = std::max(largestOrthogonalDifference, orthogonal);
            minimumMatchingAttenuation = std::min(minimumMatchingAttenuation, matching);
            std::printf("M/S key: mode=%d audioSide=%d orthogonalDelta=%.9g dB matchingAttenuation=%.9g dB\n", static_cast<int>(mode), audioSide, orthogonal, matching);
        }
    check("external M/S keys compress their matching component and preserve the orthogonal component", minimumReferenceRms > 0.05 && largestOrthogonalDifference < 0.05 && minimumMatchingAttenuation > 6.0);
}

void testAutoMakeup()
{
    double largestAutomaticDelta = 0.0, largestManualError = 0.0, minimumRms = 1.0;
    for (MultiCompMode mode : {MultiCompMode::Multiband, MultiCompMode::Digital})
        for (bool external : {false, true})
            for (bool automatic : {false, true})
            {
                std::array<double, 2> levels{};
                for (int output = 0; output < 2; ++output)
                {
                    MultiCompDSP dsp;
                    configure(dsp, mode);
                    dsp.setExternalSidechain(external);
                    dsp.setParameter(P::AutoMakeup, automatic ? 1.0f : 0.0f);
                    dsp.setParameter(mode == MultiCompMode::Multiband ? P::MbOutput : P::DigitalOutput, output == 0 ? 0.0f : 24.0f);
                    dsp.prepare(kRate, kBlock);
                    const auto result = renderPrepared(dsp, 6 * kRate, 1, external,
                        [](int sample, int, float& input, float& key) { input = tone(sample, 1000.0f, 0.01f); key = 0.0f; });
                    levels[static_cast<size_t>(output)] = rms(result.audio[0], 5 * kRate);
                }
                minimumRms = std::min(minimumRms, levels[0]);
                const double change = 20.0 * std::log10(levels[1] / levels[0]);
                std::printf("output gain: mode=%d automatic=%d external=%d output0to24=%.9g dB\n", static_cast<int>(mode), automatic, external, change);
                if (automatic && !external) largestAutomaticDelta = std::max(largestAutomaticDelta, std::abs(change));
                else largestManualError = std::max(largestManualError, std::abs(change - 24.0));
            }
    check("Auto Makeup owns internal-key output while manual and external-key output remain effective", minimumRms > 0.001 && largestAutomaticDelta < 0.025 && largestManualError < 0.025);
}

void testDisabledSolo()
{
    double largestIgnoredSoloDelta = 0.0, largestBypassDelta = 0.0;
    double minimumAudibleRms = 1.0, minimumActiveSoloAttenuation = 1000.0;
    for (float mix : {0.0f, 50.0f, 100.0f})
        for (bool bypass : {false, true})
        {
            std::array<Render, 3> results;
            for (int soloKind = 0; soloKind < 3; ++soloKind)
            {
                MultiCompDSP dsp;
                configure(dsp);
                dsp.setParameter(P::MbMix, mix);
                dsp.setBypass(bypass);
                if (soloKind == 1) dsp.setMultibandParameter(1, B::Solo, 1.0f);
                if (soloKind == 2) dsp.setMultibandParameter(0, B::Solo, 1.0f);
                dsp.prepare(kRate, kBlock);
                // Reproduce the UI sequence: run an enabled solo, disable it,
                // then let the output settle with the solo value retained.
                renderPrepared(dsp, 2048, 2, false,
                    [](int sample, int, float& input, float& key) { input = tone(sample, 8000.0f, 0.1f); key = input; });
                dsp.setMultibandParameter(1, B::Enabled, 0.0f);
                results[static_cast<size_t>(soloKind)] = renderPrepared(dsp, 16384, 2, false,
                    [](int sample, int, float& input, float& key) { input = tone(sample + 2048, 8000.0f, 0.1f); key = input; });
            }
            const double audibleRms = rms(results[0].audio[0], 8192);
            minimumAudibleRms = std::min(minimumAudibleRms, audibleRms);
            const double ignoredDelta = delta(results[0].audio[0], results[1].audio[0], 8192);
            largestIgnoredSoloDelta = std::max(largestIgnoredSoloDelta, ignoredDelta);
            if (bypass) largestBypassDelta = std::max(largestBypassDelta, delta(results[0].audio[0], results[2].audio[0], 8192));
            else minimumActiveSoloAttenuation = std::min(minimumActiveSoloAttenuation, -20.0 * std::log10(rms(results[2].audio[0], 8192) / audibleRms));
        }
    std::printf("disabled Solo: ignoredDelta=%.9g bypassDelta=%.9g minimumReferenceRms=%.9g activeSoloAttenuation=%.9g dB\n", largestIgnoredSoloDelta, largestBypassDelta, minimumAudibleRms, minimumActiveSoloAttenuation);
    check("disabled-band Solo cannot hide or mute active bands while enabled Solo and bypass retain their behavior", minimumAudibleRms > 0.05 && largestIgnoredSoloDelta < 1.0e-6 && largestBypassDelta < 1.0e-6 && minimumActiveSoloAttenuation > 20.0);
}

void testMixAlignment()
{
    double largestError = 0.0, largestPhaseError = 0.0, minimumEndpointDifference = 1.0;
    for (bool padding : {false, true})
        for (bool external : {false, true})
            for (bool midSide : {false, true})
                for (float lookahead : {0.0f, 10.0f})
                    for (bool compression : {false, true})
                    {
                        std::array<Render, 3> results;
                        for (int mix = 0; mix < 3; ++mix)
                        {
                            MultiCompDSP dsp;
                            configure(dsp, MultiCompMode::Multiband, compression);
                            if (!compression) dsp.setParameter(P::MbOutput, -6.020599913f);
                            dsp.setParameter(P::Crossover1, 500.0f);
                            dsp.setParameter(P::Crossover2, 750.0f);
                            dsp.setParameter(P::Crossover3, 2000.0f);
                            dsp.setParameter(P::MbMix, mix * 50.0f);
                            dsp.setParameter(P::GlobalLookahead, lookahead);
                            dsp.setParameter(P::StereoLinkMode, midSide ? 1.0f : 0.0f);
                            dsp.setExternalSidechain(external);
                            dsp.setHostLatencyPadding(padding);
                            dsp.prepare(kRate, kBlock);
                            results[static_cast<size_t>(mix)] = renderPrepared(dsp, 16384, 2, external,
                                [](int sample, int ch, float& input, float& key)
                                {
                                    input = tone(sample, 80.0f, 0.1f) + tone(sample, ch == 0 ? 600.0f : 1000.0f, 0.05f)
                                          + tone(sample, 3000.0f, 0.05f) + tone(sample, 12000.0f, 0.02f);
                                    key = input;
                                });
                        }
                        minimumEndpointDifference = std::min(minimumEndpointDifference, delta(results[0].audio[0], results[2].audio[0], 8192));
                        for (int ch = 0; ch < 2; ++ch)
                            for (size_t sample = 8192; sample < results[0].audio[0].size(); ++sample)
                            {
                                const double expected = 0.5 * (results[0].audio[static_cast<size_t>(ch)][sample] + results[2].audio[static_cast<size_t>(ch)][sample]);
                                largestError = std::max(largestError, std::abs(results[1].audio[static_cast<size_t>(ch)][sample] - expected));
                                if (!compression)
                                    largestPhaseError = std::max(largestPhaseError, std::abs(results[2].audio[static_cast<size_t>(ch)][sample] - 0.5 * results[0].audio[static_cast<size_t>(ch)][sample]));
                            }
                    }
    std::printf("Multiband mix: midpointError=%.9g phaseError=%.9g minimumDryWetDifference=%.9g\n", largestError, largestPhaseError, minimumEndpointDifference);
    check("Multiband dry/wet interpolation preserves aligned L/R and M/S audio with latency padding and lookahead", largestError < 2.0e-6 && largestPhaseError < 2.0e-6 && minimumEndpointDifference > 0.01);
}

void testCrossoverLifecycle()
{
    const auto programme = [](int sample, int ch, float& input, float& key)
    {
        input = tone(sample, ch == 0 ? 600.0f : 3000.0f, 0.1f);
        key = input;
    };
    const auto silence = [](int, int, float& input, float& key) { input = key = 0.0f; };
    const auto initialize = [](MultiCompDSP& dsp)
    {
        configure(dsp, MultiCompMode::Multiband, true);
        dsp.setParameter(P::Crossover1, 500.0f);
        dsp.setParameter(P::Crossover2, 750.0f);
        dsp.setParameter(P::Crossover3, 2000.0f);
        dsp.setParameter(P::GlobalLookahead, 10.0f);
        dsp.setExternalSidechain(true);
    };
    double largestResetDelta = 0.0, largestPrepareDelta = 0.0;
    for (int channels : {1, 2})
    {
        MultiCompDSP reused, fresh;
        initialize(reused);
        initialize(fresh);
        reused.prepare(kRate, kBlock);
        renderPrepared(reused, 4096, channels, true, programme);
        reused.reset();
        fresh.prepare(kRate, kBlock);
        const auto resetRender = renderPrepared(reused, 4096, channels, true, programme);
        const auto freshRender = renderPrepared(fresh, 4096, channels, true, programme);
        for (int ch = 0; ch < channels; ++ch)
            largestResetDelta = std::max(largestResetDelta, delta(resetRender.audio[static_cast<size_t>(ch)], freshRender.audio[static_cast<size_t>(ch)]));
        // Re-prepare twice after nonzero history and a rate/block-size change.
        reused.prepare(96000.0, 128);
        reused.prepare(96000.0, 128);
        fresh.prepare(96000.0, 128);
        const auto repeatRender = renderPrepared(reused, 4096, channels, true, programme);
        const auto rateRender = renderPrepared(fresh, 4096, channels, true, programme);
        for (int ch = 0; ch < channels; ++ch)
            largestPrepareDelta = std::max(largestPrepareDelta, delta(repeatRender.audio[static_cast<size_t>(ch)], rateRender.audio[static_cast<size_t>(ch)]));
    }

    MultiCompDSP topology;
    initialize(topology);
    topology.setParameter(P::GlobalLookahead, 0.0f);
    topology.prepare(kRate, kBlock);
    const auto warm = renderPrepared(topology, 4096, 2, true, programme);
    topology.setMultibandParameter(1, B::Enabled, 0.0f);
    topology.setMultibandParameter(2, B::Enabled, 0.0f);
    renderPrepared(topology, 8192, 2, true, silence);
    topology.setMultibandParameter(1, B::Enabled, 1.0f);
    topology.setMultibandParameter(2, B::Enabled, 1.0f);
    const auto restored = renderPrepared(topology, 512, 2, true, silence);
    double topologyTail = 0.0;
    for (const auto& channel : restored.audio)
        for (float sample : channel) topologyTail = std::max(topologyTail, std::abs(static_cast<double>(sample)));

    // A reduced-channel callback must retire an absent channel's crossover,
    // compensation and detector histories rather than exposing a stale tail.
    MultiCompDSP mono;
    configure(mono);
    mono.prepare(kRate, kBlock);
    const auto stereoWarm = renderPrepared(mono, 4096, 2, false, programme);
    renderPrepared(mono, 8192, 1, false, silence);
    const auto resumed = renderPrepared(mono, 512, 2, false, silence);
    double resumedRight = 0.0;
    for (float sample : resumed.audio[1]) resumedRight = std::max(resumedRight, std::abs(static_cast<double>(sample)));
    const double warmLevel = std::min(rms(warm.audio[0], 2048), rms(stereoWarm.audio[1], 2048));
    std::printf("crossover lifecycle: resetDelta=%.9g reprepareDelta=%.9g restoredTopologyTail=%.9g resumedRightPeak=%.9g warmRms=%.9g\n", largestResetDelta, largestPrepareDelta, topologyTail, resumedRight, warmLevel);
    check("Multiband crossover and detector histories clear on reset, prepare, topology reactivation and reduced channels", warmLevel > 0.001 && largestResetDelta < 1.0e-7 && largestPrepareDelta < 1.0e-7 && topologyTail < 1.0e-7 && resumedRight < 1.0e-7);
}
}

int main(int argc, char** argv)
{
    const auto selected = [&](const char* name) { return argc == 1 || (argc == 2 && std::strcmp(argv[1], name) == 0); };
    if (selected("--crossover")) testCrossover();
    if (selected("--digital-hp")) testDigitalHighPass();
    if (selected("--lookahead")) testLookahead();
    if (selected("--mid-side")) testMidSideKey();
    if (selected("--auto-makeup")) testAutoMakeup();
    if (selected("--disabled-solo")) testDisabledSolo();
    if (selected("--mix-alignment")) testMixAlignment();
    if (selected("--crossover-lifecycle")) testCrossoverLifecycle();
    std::printf("Modern compressor regression failures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
