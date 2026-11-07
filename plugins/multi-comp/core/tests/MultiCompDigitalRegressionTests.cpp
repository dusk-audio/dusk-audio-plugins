// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
#include "../MultiCompDSP.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
using duskaudio::MultiCompDSP;
using duskaudio::MultiCompMode;
using duskaudio::MultiCompModes;
using duskaudio::MultiCompParameterState;
using Parameter = MultiCompDSP::Parameter;

int failures = 0;

void check(bool condition, const char* message)
{
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", message);
    if (!condition) ++failures;
}

void configure(MultiCompParameterState& p)
{
    p.digitalThreshold.store(-20.0f);
    p.digitalRatio.store(4.0f);
    p.digitalKnee.store(0.0f);
    p.digitalAttack.store(0.01f);
    p.digitalRelease.store(1.0f);
    p.digitalOutput.store(0.0f);
    p.digitalLookahead.store(0.0f);
    p.digitalAdaptive.store(false);
}

void testAttackRange()
{
    double maxError = 0.0;
    float fastestFirst = 0.0f, slowerFirst = 0.0f;
    constexpr float carrier = 0.02f, key = 0.8f;
    const double reduction = (20.0 * std::log10(static_cast<double>(key)) + 20.0) * 0.9;
    const double target = std::pow(10.0, -reduction / 20.0);
    for (double rate : {48000.0, 96000.0, 192000.0})
        for (int factor : {1, 2, 4})
            for (float attackMs : {0.01f, 0.02f, 0.05f, 0.09f, 0.1f, 1.0f, 100.0f})
            {
                MultiCompModes modes;
                modes.prepare(rate, 256, factor);
                MultiCompParameterState p;
                configure(p);
                p.digitalRatio.store(10.0f);
                p.digitalAttack.store(attackMs);
                const double coefficient = std::exp(-1.0 / (static_cast<double>(attackMs) * 0.001 * rate * factor));
                double reference = 1.0;
                for (int n = 0; n < 16; ++n)
                {
                    reference = coefficient * reference + (1.0 - coefficient) * target;
                    const float output = modes.process(MultiCompMode::Digital, carrier, 0, key, p);
                    maxError = std::max(maxError, std::abs(output - carrier * reference));
                    if (rate == 48000.0 && factor == 1 && n == 0)
                    {
                        if (attackMs == 0.01f) fastestFirst = output;
                        if (attackMs == 0.09f) slowerFirst = output;
                    }
                }
            }
    std::printf("attack range: rates=48/96/192k factors=1/2/4 settings=0.01..100ms max_sample_error=%.9g first_0.01ms=%.9g first_0.09ms=%.9g\n",
                maxError, fastestFirst, slowerFirst);
    check(maxError < 1.0e-7 && slowerFirst - fastestFirst > 0.005f,
          "every advertised Digital attack follows the one-pole time constant, including 0.01..0.09 ms");
}

float staticReduction(float levelDb, float threshold, float knee, float ratio)
{
    MultiCompModes modes;
    modes.prepare(48000.0, 256, 1);
    MultiCompParameterState p;
    configure(p);
    p.digitalThreshold.store(threshold);
    p.digitalKnee.store(knee);
    p.digitalRatio.store(ratio);
    const float key = std::pow(10.0f, levelDb / 20.0f);
    float output = 0.0f;
    for (int n = 0; n < 256; ++n)
        output = modes.process(MultiCompMode::Digital, 0.02f, 0, key, p);
    return -20.0f * std::log10(output / 0.02f);
}

void testSoftKnee()
{
    double maxError = 0.0;
    int points = 0;
    for (float threshold : {-40.0f, -20.0f, -6.0f})
        for (float ratio : {1.0f, 2.0f, 4.0f, 20.0f})
            for (float knee : {0.0f, 6.0f, 12.0f})
                for (float offset : {-12.0f, -6.1f, -6.0f, -5.9f, -3.1f, -3.0f, -2.9f,
                                      -1.0f, 0.0f, 1.0f, 2.9f, 3.0f, 3.1f, 5.9f, 6.0f, 6.1f, 12.0f})
                {
                    const double slope = 1.0 - 1.0 / ratio;
                    const double distance = offset + knee * 0.5;
                    const double expected = knee > 0.0f && offset > -knee * 0.5f && offset < knee * 0.5f
                        ? slope * distance * distance / (2.0 * knee)
                        : slope * std::max(0.0f, offset);
                    const float measured = staticReduction(threshold + offset, threshold, knee, ratio);
                    maxError = std::max(maxError, std::abs(measured - expected));
                    ++points;
                }
    constexpr float step = 0.05f;
    const float lower = staticReduction(-26.0f, -20.0f, 12.0f, 4.0f);
    const float upper = staticReduction(-14.0f, -20.0f, 12.0f, 4.0f);
    const float lowerLeft = (lower - staticReduction(-26.0f - step, -20.0f, 12.0f, 4.0f)) / step;
    const float lowerRight = (staticReduction(-26.0f + step, -20.0f, 12.0f, 4.0f) - lower) / step;
    const float upperLeft = (upper - staticReduction(-14.0f - step, -20.0f, 12.0f, 4.0f)) / step;
    const float upperRight = (staticReduction(-14.0f + step, -20.0f, 12.0f, 4.0f) - upper) / step;
    std::printf("soft knee: %d settled points max_GR_error_db=%.9g lower_slopes=%.9g/%.9g upper_slopes=%.9g/%.9g midpoint_GR_db=%.9g\n",
                points, maxError, lowerLeft, lowerRight, upperLeft, upperRight,
                staticReduction(-20.0f, -20.0f, 12.0f, 4.0f));
    check(maxError < 3.0e-5 && std::abs(lowerRight - lowerLeft) < 0.005f
              && std::abs(upperRight - upperLeft) < 0.005f,
          "Digital quadratic soft knee joins both outer laws smoothly and preserves hard-knee and 1:1 controls");
}

struct Render
{
    std::array<std::vector<float>, 2> output;
    int releaseAt = 0;
};

// The unequal main histories expose the original bug without changing the
// common linked key. Independent key histories are the opposite control.
Render renderAdaptive(bool adaptive, bool external, int factorSetting,
                      int linkMode, float linkPercent, int channels,
                      bool unequalMain = true, bool unequalKey = false,
                      int lifecycle = 0)
{
    constexpr double rate = 48000.0;
    constexpr int count = 76800, releaseAt = 28800, block = 256;
    MultiCompDSP dsp;
    dsp.setMode(static_cast<int>(MultiCompMode::Digital));
    dsp.setOversampling(factorSetting);
    dsp.setStereoLink(linkPercent);
    dsp.setParameter(Parameter::StereoLinkMode, static_cast<float>(linkMode));
    dsp.setParameter(Parameter::DigitalThreshold, -30.0f);
    dsp.setParameter(Parameter::DigitalRatio, 10.0f);
    dsp.setParameter(Parameter::DigitalKnee, 0.0f);
    dsp.setParameter(Parameter::DigitalAttack, 0.1f);
    dsp.setParameter(Parameter::DigitalRelease, 200.0f);
    dsp.setParameter(Parameter::DigitalAdaptive, adaptive ? 1.0f : 0.0f);
    dsp.setParameter(Parameter::ExternalSidechain, external ? 1.0f : 0.0f);
    dsp.setParameter(Parameter::NoiseEnable, 0.0f);
    dsp.setParameter(Parameter::AutoMakeup, 0.0f);
    dsp.prepare(rate, block);
    std::array<std::vector<float>, 2> input, key;
    Render result;
    result.releaseAt = releaseAt;
    for (int ch = 0; ch < 2; ++ch)
    {
        input[ch].resize(count);
        key[ch].resize(count);
        result.output[ch].resize(count);
        for (int n = 0; n < count; ++n)
        {
            const float main = external ? 0.05f : (n < releaseAt ? 0.2f : 0.005f);
            input[ch][n] = unequalMain && ch == 1 && n < releaseAt && n % 128 != 0 ? 0.0f : main;
            key[ch][n] = n < releaseAt && (!unequalKey || ch == 0 || n % 128 == 0) ? 0.5f : 0.0f;
        }
    }
    // Exercise stale adaptive/envelope/delay state before a lifecycle boundary.
    // reset and mode re-entry must give the same subsequent render as fresh.
    if (lifecycle != 0)
    {
        for (int start = 0; start < count; start += block)
        {
            const float* ip[] = {input[0].data() + start, input[1].data() + start};
            const float* sp[] = {key[0].data() + start, key[1].data() + start};
            float* op[] = {result.output[0].data() + start, result.output[1].data() + start};
            dsp.processBlockExternal(ip, external ? sp : nullptr, op, channels, block);
        }
        if (lifecycle == 1) dsp.reset();
        else
        {
            dsp.setMode(static_cast<int>(MultiCompMode::Multiband));
            const float* ip[] = {input[0].data(), input[1].data()};
            float* op[] = {result.output[0].data(), result.output[1].data()};
            dsp.processBlock(ip, op, channels, block);
            dsp.setMode(static_cast<int>(MultiCompMode::Digital));
        }
    }
    for (int start = 0; start < count; start += block)
    {
        const float* ip[] = {input[0].data() + start, input[1].data() + start};
        const float* sp[] = {key[0].data() + start, key[1].data() + start};
        float* op[] = {result.output[0].data() + start, result.output[1].data() + start};
        dsp.processBlockExternal(ip, external ? sp : nullptr, op, channels, block);
    }
    return result;
}

double channelDifference(const Render& render)
{
    double delta = 0.0;
    for (int n = render.releaseAt + 256; n < render.releaseAt + 12000; ++n)
        delta = std::max(delta, static_cast<double>(std::abs(render.output[0][n] - render.output[1][n])));
    return delta;
}

double renderDifference(const Render& a, const Render& b, int channels = 2)
{
    double delta = 0.0;
    for (int ch = 0; ch < channels; ++ch)
        for (size_t n = 0; n < a.output[ch].size(); ++n)
            delta = std::max(delta, static_cast<double>(std::abs(a.output[ch][n] - b.output[ch][n])));
    return delta;
}

void testAdaptiveLink()
{
    double linkedError = 0.0, offError = 0.0, smallestAdaptiveEffect = 1.0;
    for (int setting : {0, 1, 2})
        for (bool external : {false, true})
        {
            const auto linked = renderAdaptive(true, external, setting, 0, 100.0f, 2);
            const auto fixed = renderAdaptive(false, external, setting, 0, 100.0f, 2);
            linkedError = std::max(linkedError, channelDifference(linked));
            offError = std::max(offError, channelDifference(fixed));
            smallestAdaptiveEffect = std::min(smallestAdaptiveEffect, renderDifference(linked, fixed));
            std::printf("adaptive link: external=%d OS_setting=%d linked_delta=%.9g fixed_delta=%.9g adaptive_effect=%.9g\n",
                        external, setting, channelDifference(linked), channelDifference(fixed), renderDifference(linked, fixed));
        }
    check(linkedError < 1.0e-8 && offError < 1.0e-8 && smallestAdaptiveEffect > 1.0e-4,
          "100% Digital stereo link has equal nontrivial gain recovery with Adaptive on/off, internal/external, OS off/2x/4x");

    const auto independent = renderAdaptive(true, true, 0, 2, 100.0f, 2, false, true);
    const auto zeroLink = renderAdaptive(true, true, 0, 0, 0.0f, 2, false, true);
    const auto partialLink = renderAdaptive(true, true, 0, 0, 50.0f, 2, false, true);
    const auto fullyLinked = renderAdaptive(true, true, 0, 0, 100.0f, 2, false, true);
    const auto mono = renderAdaptive(true, true, 0, 0, 100.0f, 1, false, true);
    const auto ms = renderAdaptive(true, true, 0, 1, 100.0f, 2, false, true);
    const auto msZeroLink = renderAdaptive(true, true, 0, 1, 0.0f, 2, false, true);
    std::printf("adaptive neighbours: dualmono_vs_0link=%.9g mono_vs_dualmono_left=%.9g dualmono_delta=%.9g partial_delta=%.9g full_delta=%.9g MS_link_control_delta=%.9g\n",
                renderDifference(independent, zeroLink), renderDifference(mono, independent, 1),
                channelDifference(independent), channelDifference(partialLink), channelDifference(fullyLinked),
                renderDifference(ms, msZeroLink));
    // The unequal keys keep Dual Mono's channels apart, so its equalities below are not vacuous; the link amount
    // closes that gap in order, to nothing at 100 %.
    check(renderDifference(independent, zeroLink) < 1.0e-8 && renderDifference(mono, independent, 1) < 1.0e-8
              && renderDifference(ms, msZeroLink) < 1.0e-8 && channelDifference(independent) > 1.0e-3
              && channelDifference(fullyLinked) < 1.0e-8
              && channelDifference(partialLink) > channelDifference(fullyLinked)
              && channelDifference(partialLink) < channelDifference(independent),
          "Adaptive Digital: Dual Mono is 0% link, mono is Dual Mono's left, Mid-Side ignores the link amount");

    const auto fresh = renderAdaptive(true, true, 2, 0, 100.0f, 2);
    const auto reset = renderAdaptive(true, true, 2, 0, 100.0f, 2, true, false, 1);
    const auto reentry = renderAdaptive(true, true, 2, 0, 100.0f, 2, true, false, 2);
    // Mode transitions crossfade at the output, so compare the settled
    // recovery rather than asserting the transition itself is byte-identical.
    double resetError = renderDifference(fresh, reset), reentryError = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (size_t n = static_cast<size_t>(fresh.releaseAt); n < fresh.output[ch].size(); ++n)
            reentryError = std::max(reentryError, static_cast<double>(std::abs(fresh.output[ch][n] - reentry.output[ch][n])));
    std::printf("adaptive lifecycle: reset_delta=%.9g reentry_release_delta=%.9g fresh_release_peak=%.9g\n",
                resetError, reentryError, fresh.output[0][fresh.releaseAt + 4800]);
    check(resetError < 1.0e-8 && reentryError < 1.0e-8 && std::abs(fresh.output[0][fresh.releaseAt + 4800]) > 1.0e-3f,
          "Adaptive Digital starts from rest after reset() and after re-entry from a Multiband block");
}
} // namespace

int main()
{
    testAttackRange();
    testSoftKnee();
    testAdaptiveLink();
    std::printf("Digital regression checks: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
