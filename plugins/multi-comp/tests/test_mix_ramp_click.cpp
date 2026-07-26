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
 *
 * The Bus compressor also has an automation-only local mix. Its stereo-linked
 * path once ignored that parameter entirely, so this gate additionally checks
 * the local dry/full-wet endpoints and its 20 ms automation ramp.
 */

#include <JuceHeader.h>
#include "../UniversalCompressor.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
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

struct BusRender
{
    std::vector<float> inputL;
    std::vector<float> inputR;
    std::vector<float> outputL;
    std::vector<float> outputR;
};

void configureBus(UniversalCompressor& plugin, bool oversampling, float stereoLink,
                  float busMix, double sr, int block)
{
    plugin.setPlayConfigDetails(2, 2, sr, block);
    plugin.setInternalOversamplingEnabled(oversampling);
    plugin.prepareToPlay(sr, block);

    setParam(plugin, "mode", static_cast<float>(static_cast<int>(CompressorMode::Bus)));
    setParam(plugin, "auto_makeup", 0.0f);
    setParam(plugin, "mix", 100.0f);
    setParam(plugin, "noise_enable", 0.0f);
    setParam(plugin, "stereo_link_mode", 0.0f);
    setParam(plugin, "stereo_link", stereoLink);
    setParam(plugin, "bus_threshold", -24.0f);
    setParam(plugin, "bus_ratio", 2.0f);
    setParam(plugin, "bus_attack", 0.0f);
    setParam(plugin, "bus_release", 0.0f);
    setParam(plugin, "bus_makeup", 6.0f);
    setParam(plugin, "bus_mix", busMix);
}

BusRender renderBus(float stereoLink, float busMix, bool oversampling,
                    double sr, int block, int numBlocks,
                    float targetBusMix = -1.0f, int changeBlock = -1)
{
    UniversalCompressor plugin;
    configureBus(plugin, oversampling, stereoLink, busMix, sr, block);

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buf(2, block);
    juce::Random rng(0x129B05);

    BusRender result;
    const auto total = static_cast<size_t>(block) * static_cast<size_t>(numBlocks);
    result.inputL.reserve(total);
    result.inputR.reserve(total);
    result.outputL.reserve(total);
    result.outputR.reserve(total);

    for (int b = 0; b < numBlocks; ++b)
    {
        if (b == changeBlock && targetBusMix >= 0.0f)
            setParam(plugin, "bus_mix", targetBusMix);

        for (int i = 0; i < block; ++i)
        {
            // Keep both channels identical so the link itself cannot introduce
            // an unrelated image shift into the endpoint/control comparisons.
            const float s = 0.55f * (rng.nextFloat() * 2.0f - 1.0f);
            buf.setSample(0, i, s);
            buf.setSample(1, i, s);
            result.inputL.push_back(s);
            result.inputR.push_back(s);
        }

        plugin.processBlock(buf, midi);
        const float* outL = buf.getReadPointer(0);
        const float* outR = buf.getReadPointer(1);
        for (int i = 0; i < block; ++i)
        {
            result.outputL.push_back(outL[i]);
            result.outputR.push_back(outR[i]);
        }
    }

    return result;
}

float maxAbsDiffFrom(const std::vector<float>& a, const std::vector<float>& b, size_t start)
{
    if (a.size() != b.size() || start > a.size())
        return std::numeric_limits<float>::infinity();

    float peak = 0.0f;
    for (size_t i = start; i < a.size(); ++i)
        peak = juce::jmax(peak, std::abs(a[i] - b[i]));
    return peak;
}

bool byteEqualFrom(const std::vector<float>& a, const std::vector<float>& b, size_t start)
{
    return a.size() == b.size() && start <= a.size()
        && std::memcmp(a.data() + start, b.data() + start,
                       (a.size() - start) * sizeof(float)) == 0;
}

/** Renders the automation-only Bus mix while stereo link is engaged. */
SweepResult renderLinkedBusMixSweep(bool oversampling, bool moveMix,
                                    double sr, int block, int numBlocks)
{
    UniversalCompressor plugin;
    configureBus(plugin, oversampling, 100.0f, 100.0f, sr, block);

    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buf(2, block);
    const double phaseInc = 2.0 * juce::MathConstants<double>::pi * 220.0 / sr;
    double phase = 0.0;

    SweepResult result;
    float previous = 0.0f;
    bool havePrevious = false;
    float currentMix = 100.0f;
    constexpr int warmupBlocks = 48;

    for (int b = 0; b < numBlocks; ++b)
    {
        if (moveMix && b >= warmupBlocks)
        {
            const float t = static_cast<float>(b - warmupBlocks)
                          / static_cast<float>(std::max(1, numBlocks - warmupBlocks - 1));
            const float triangle = t < 0.5f ? (1.0f - 2.0f * t) : (2.0f * t - 1.0f);
            currentMix = juce::jlimit(0.0f, 100.0f, triangle * 100.0f);
            setParam(plugin, "bus_mix", currentMix);
        }

        for (int i = 0; i < block; ++i)
        {
            const float s = static_cast<float>(0.35 * std::sin(phase));
            phase += phaseInc;
            buf.setSample(0, i, s);
            buf.setSample(1, i, s);
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

    std::cout << "\n  Bus local mix under stereo link:\n";

    // At native rate the Bus-local dry endpoint must return the exact samples
    // presented to the Bus core. This is deliberately a byte comparison.
    const auto busDry = renderBus(100.0f, 0.0f, false, sr, block, 64);
    constexpr size_t dryCompareStart = static_cast<size_t>(8 * block);
    const bool dryL = byteEqualFrom(busDry.inputL, busDry.outputL, dryCompareStart);
    const bool dryR = byteEqualFrom(busDry.inputR, busDry.outputR, dryCompareStart);
    const float dryMaxError = juce::jmax(
        maxAbsDiffFrom(busDry.inputL, busDry.outputL, dryCompareStart),
        maxAbsDiffFrom(busDry.inputR, busDry.outputR, dryCompareStart));
    constexpr float dryTolerance = 1.0e-7f;
    const bool dryOk = (dryL && dryR) || dryMaxError <= dryTolerance;
    if (! dryOk)
        ++failures;
    std::cout << (dryOk ? "  ok   " : "  FAIL ")
              << std::setw(28) << std::left << "0% local mix = dry (native)" << std::right
              << "  byte-equal L/R: " << (dryL ? "yes" : "no") << "/" << (dryR ? "yes" : "no")
              << "  max error " << std::scientific << std::setprecision(3) << dryMaxError
              << " (tol " << dryTolerance << ")" << std::fixed << std::setprecision(6) << "\n";

    // A local-dry render must keep running the complete wet chain: the detector
    // feedback and output-stage state stay pre-blend. Once its 20 ms ramp reaches
    // 100%, it must therefore byte-null against a control held full-wet throughout.
    for (const bool oversampling : { false, true })
    {
        constexpr int changeBlock = 16;
        constexpr int compareBlock = 24;  // 8 blocks later: safely past the 20 ms ramp
        const auto control = renderBus(100.0f, 100.0f, oversampling, sr, block, 96);
        const auto linked  = renderBus(100.0f,   0.0f, oversampling, sr, block, 96,
                                      100.0f, changeBlock);
        const size_t compareSample = static_cast<size_t>(compareBlock) * static_cast<size_t>(block);
        const float diff = juce::jmax(maxAbsDiffFrom(control.outputL, linked.outputL, compareSample),
                                      maxAbsDiffFrom(control.outputR, linked.outputR, compareSample));
        const bool ok = diff == 0.0f;
        if (! ok)
            ++failures;
        std::cout << (ok ? "  ok   " : "  FAIL ")
                  << std::setw(28) << std::left
                  << (oversampling ? "100% local mix (oversampled)" : "100% local mix (native)")
                  << std::right << "  post-ramp control delta " << diff
                  << " (byte-null)\n";
    }

    constexpr float busMixMaxRatio = 1.4f;
    for (const bool oversampling : { false, true })
    {
        const SweepResult staticRun = renderLinkedBusMixSweep(oversampling, false, sr, block, numBlocks);
        const SweepResult sweepRun  = renderLinkedBusMixSweep(oversampling, true,  sr, block, numBlocks);
        const float ratio = staticRun.peakDelta > 1.0e-9f
                          ? sweepRun.peakDelta / staticRun.peakDelta : 0.0f;
        const bool ok = ratio <= busMixMaxRatio;
        if (! ok)
            ++failures;
        std::cout << (ok ? "  ok   " : "  FAIL ")
                  << std::setw(28) << std::left
                  << (oversampling ? "local mix sweep (oversampled)" : "local mix sweep (native)")
                  << std::right << "  static " << staticRun.peakDelta
                  << "  sweep " << sweepRun.peakDelta
                  << "  ratio " << std::setprecision(2) << ratio
                  << std::setprecision(6);
        if (! ok)
            std::cout << " (worst block " << sweepRun.peakBlock
                      << ", bus_mix " << std::setprecision(1) << sweepRun.peakMix << "%)"
                      << std::setprecision(6);
        std::cout << "\n";
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
