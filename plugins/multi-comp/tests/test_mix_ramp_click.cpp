/*
 * Mix-knob click gate.
 *
 * The Mix value used to be read once per block and applied as a constant gain,
 * so every buffer boundary during a knob move was a gain step. Two more
 * discontinuities sat on top: the dry delay ring only advanced while mixing was
 * active (so it went stale at 100 % wet and jumped on re-entry), and the 0 % /
 * 100 % endpoints switched whole code paths, including a different dry source.
 *
 * Test: feed a steady sine and sweep Mix across its full travel (100 -> 0 -> 100)
 * one step per block, the way a fast knob drag or an automation lane does. With
 * a per-sample ramp the output stays a continuous waveform, so the largest
 * sample-to-sample jump stays close to what the sine itself produces. A stepped
 * blend leaves a visible discontinuity at block boundaries.
 *
 * The measure is the peak |x[n] - x[n-1]| over the sweep, compared against the
 * same figure from a static render (no knob movement) of the same signal. Any
 * excess is the click.
 */

#include <JuceHeader.h>
#include "../UniversalCompressor.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>

namespace
{

bool g_paramError = false;

void setParam(UniversalCompressor& plugin, const juce::String& paramID, float value)
{
    auto& params = plugin.getParameters();
    if (auto* param = params.getParameter(paramID))
        param->setValueNotifyingHost(params.getParameterRange(paramID).convertTo0to1(value));
    else
    {
        std::cerr << "ERROR: param '" << paramID << "' not found — test config invalid\n";
        g_paramError = true;
    }
}

struct Scenario
{
    const char* name;
    CompressorMode mode;
    bool oversampling;
};

struct SweepResult
{
    float peakDelta = 0.0f;
    int   peakBlock = -1;
    float peakMix = -1.0f;    // Mix target in effect when the worst step happened
};

/** Renders a sine while stepping Mix once per block; returns peak sample delta. */
SweepResult renderSweep(const Scenario& scenario, bool moveKnob, double sr, int block, int numBlocks)
{
    UniversalCompressor plugin;
    plugin.setPlayConfigDetails(2, 2, sr, block);
    plugin.setInternalOversamplingEnabled(scenario.oversampling);
    plugin.prepareToPlay(sr, block);

    setParam(plugin, "mode", static_cast<float>(static_cast<int>(scenario.mode)));
    setParam(plugin, "auto_makeup", 0.0f);      // isolate the blend from makeup movement
    setParam(plugin, "mix", 100.0f);

    if (scenario.mode == CompressorMode::Multiband)
        for (const juce::String name : { "low", "lowmid", "highmid", "high" })
        {
            setParam(plugin, "mb_" + name + "_enabled", 1.0f);
            setParam(plugin, "mb_" + name + "_ratio", 4.0f);
        }

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buf(2, block);

    const double phaseInc = 2.0 * juce::MathConstants<double>::pi * 220.0 / sr;
    double phase = 0.0;

    SweepResult result;
    float previous = 0.0f;
    bool havePrevious = false;
    float currentMix = 100.0f;

    // Warm up at a static 100 % so envelopes settle before we judge deltas.
    constexpr int warmupBlocks = 48;

    for (int b = 0; b < numBlocks; ++b)
    {
        if (moveKnob && b >= warmupBlocks)
        {
            // 100 -> 0 -> 100 across the post-warmup blocks.
            const float t = static_cast<float>(b - warmupBlocks)
                          / static_cast<float>(std::max(1, numBlocks - warmupBlocks - 1));
            const float triangle = t < 0.5f ? (1.0f - 2.0f * t) : (2.0f * t - 1.0f);
            currentMix = juce::jlimit(0.0f, 100.0f, triangle * 100.0f);
            setParam(plugin, "mix", currentMix);
        }

        for (int i = 0; i < block; ++i)
        {
            const auto s = static_cast<float>(0.35 * std::sin(phase));
            phase += phaseInc;
            for (int ch = 0; ch < 2; ++ch)
                buf.setSample(ch, i, s);
        }

        plugin.processBlock(buf, midi);

        if (b >= warmupBlocks)
        {
            const float* out = buf.getReadPointer(0);
            for (int i = 0; i < block; ++i)
            {
                if (havePrevious)
                {
                    const float delta = std::abs(out[i] - previous);
                    if (delta > result.peakDelta)
                    {
                        result.peakDelta = delta;
                        result.peakBlock = b;
                        result.peakMix = currentMix;
                    }
                }
                previous = out[i];
                havePrevious = true;
            }
        }
    }

    return result;
}

/** Static render at a fixed Mix; returns the settled output of channel 0. */
std::vector<float> renderStatic(const Scenario& scenario, float mixPercent,
                                double sr, int block, int numBlocks)
{
    UniversalCompressor plugin;
    plugin.setPlayConfigDetails(2, 2, sr, block);
    plugin.setInternalOversamplingEnabled(scenario.oversampling);
    plugin.prepareToPlay(sr, block);

    setParam(plugin, "mode", static_cast<float>(static_cast<int>(scenario.mode)));
    setParam(plugin, "auto_makeup", 0.0f);
    setParam(plugin, "mix", mixPercent);

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buf(2, block);

    // Noise, not a tone: cross-correlating a 220 Hz sine at 48 kHz is ambiguous
    // at multiples of its 218-sample period, which reads as a phantom 218-sample
    // misalignment. Noise has a single unambiguous correlation peak.
    juce::Random rng(0xA11614);

    std::vector<float> out;
    out.reserve(static_cast<size_t>(block) * static_cast<size_t>(numBlocks));

    for (int b = 0; b < numBlocks; ++b)
    {
        for (int i = 0; i < block; ++i)
        {
            const auto s = 0.30f * (rng.nextFloat() * 2.0f - 1.0f);
            for (int ch = 0; ch < 2; ++ch)
                buf.setSample(ch, i, s);
        }
        plugin.processBlock(buf, midi);
        const float* p = buf.getReadPointer(0);
        for (int i = 0; i < block; ++i)
            out.push_back(p[i]);
    }
    return out;
}

/** Best-matching integer lag (in samples) of b relative to a, searched +/-maxLag. */
int bestLag(const std::vector<float>& a, const std::vector<float>& b, int maxLag, int start, int count)
{
    double best = -1.0e30;
    int bestL = 0;
    for (int lag = -maxLag; lag <= maxLag; ++lag)
    {
        double dot = 0.0;
        for (int i = start; i < start + count; ++i)
        {
            const int j = i + lag;
            if (j < 0 || j >= static_cast<int>(b.size()))
                continue;
            dot += static_cast<double>(a[static_cast<size_t>(i)]) * b[static_cast<size_t>(j)];
        }
        if (dot > best)
        {
            best = dot;
            bestL = lag;
        }
    }
    return bestL;
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    constexpr double sr = 48000.0;
    constexpr int block = 256;
    constexpr int numBlocks = 560;      // ~3 s: warmup plus a full down-up sweep

    const Scenario scenarios[] = {
        { "Opto, oversampled",     CompressorMode::Opto,      true  },
        { "Opto, native rate",     CompressorMode::Opto,      false },
        { "Bus, oversampled",      CompressorMode::Bus,       true  },
        { "Multiband",             CompressorMode::Multiband, true  },
    };

    // A moving blend of two coherent signals cannot create a step larger than
    // the static signal's own slew by more than the per-sample ramp increment.
    // 1.6x leaves room for envelope movement without admitting a click, which
    // measured 10x-30x over static before the fix.
    constexpr float kMaxRatio = 1.6f;

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "\n--- Mix knob ramp / click gate ---\n";
    std::cout << "  peak |x[n]-x[n-1]| while sweeping Mix, vs the same render held static\n\n";

    int failures = 0;
    for (const auto& scenario : scenarios)
    {
        const SweepResult staticRun = renderSweep(scenario, false, sr, block, numBlocks);
        const SweepResult sweepRun  = renderSweep(scenario, true,  sr, block, numBlocks);
        const float ratio = staticRun.peakDelta > 1.0e-9f ? sweepRun.peakDelta / staticRun.peakDelta : 0.0f;
        const bool ok = ratio <= kMaxRatio;
        if (! ok)
            ++failures;

        std::cout << (ok ? "  ok   " : "  FAIL ")
                  << std::setw(20) << std::left << scenario.name << std::right
                  << "  static " << std::setw(10) << staticRun.peakDelta
                  << "  sweep " << std::setw(10) << sweepRun.peakDelta
                  << "  ratio " << std::setprecision(2) << std::setw(7) << ratio
                  << std::setprecision(6);
        if (! ok)
            std::cout << "   (worst at block " << sweepRun.peakBlock
                      << ", mix " << std::setprecision(1) << sweepRun.peakMix << "%)"
                      << std::setprecision(6);
        std::cout << "\n";
    }

    // The 0 % endpoint has its own code path (all processing skipped, dry emitted
    // time-aligned to the reported latency). It must land on exactly the same
    // samples as the mixer's dry, or crossing into it steps the waveform.
    std::cout << "\n  0% endpoint alignment (0% path vs 1% mix through the normal path):\n";
    for (const auto& scenario : scenarios)
    {
        constexpr int alignBlocks = 200;
        const auto zero = renderStatic(scenario, 0.0f, sr, block, alignBlocks);
        const auto near = renderStatic(scenario, 1.0f, sr, block, alignBlocks);
        const int lag = bestLag(zero, near, 256, block * 100, block * 50);
        const bool ok = (lag == 0);
        if (! ok)
            ++failures;
        std::cout << (ok ? "  ok   " : "  FAIL ")
                  << std::setw(20) << std::left << scenario.name << std::right
                  << "  lag " << lag << " samples\n";
    }

    if (g_paramError)
    {
        std::cout << "\n  RESULT: FAIL (missing parameter — test config invalid)\n";
        return 2;
    }

    std::cout << "\n  RESULT: " << (failures == 0 ? "PASS" : "FAIL")
              << " (limit: sweep delta <= " << std::setprecision(2) << kMaxRatio
              << "x static delta)\n";
    return failures == 0 ? 0 : 1;
}
