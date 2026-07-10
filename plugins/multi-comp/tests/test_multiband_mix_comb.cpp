/*
 * Issue #114 regression test — Multiband dry/wet mix must not comb-filter.
 *
 * The 4-band Linkwitz-Riley split recombines as an ALLPASS (magnitude-flat, but
 * phase-rotated at each crossover). Before the fix the mix knob blended a
 * flat-phase RAW input dry against that allpass-phase wet, combing at the
 * crossover frequencies (200 / 2k / 8k Hz). The fix makes the dry the SUM OF THE
 * (uncompressed) SPLIT BANDS, so dry and wet share the identical allpass phase.
 *
 * Discriminator: with every band at ratio 1:1 (no gain reduction) the wet
 * (sum of compressed bands) EQUALS the dry (sum of uncompressed bands), so a
 * phase-correct mix at ANY percentage must yield the SAME signal as 100 % wet.
 * We therefore null mix=50 % against mix=100 %: the fixed engine cancels to the
 * float floor; the old raw-dry blend leaves a large comb residual (0.5 * (raw
 * input - allpassed wet)).
 *
 * Also reported (informational): the 50%-vs-100% transfer difference at each
 * crossover — the comb lived exactly there.
 */

#include <JuceHeader.h>
#include "../UniversalCompressor.h"
#include <cmath>
#include <iostream>
#include <iomanip>
#include <vector>

static void processWithPlugin(UniversalCompressor& plugin, juce::AudioBuffer<float>& buffer, int blockSize = 512)
{
    juce::MidiBuffer midi;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    juce::AudioBuffer<float> block(numChannels, blockSize);
    for (int offset = 0; offset < numSamples; offset += blockSize)
    {
        const int thisBlock = std::min(blockSize, numSamples - offset);
        for (int ch = 0; ch < numChannels; ++ch)
            block.copyFrom(ch, 0, buffer, ch, offset, thisBlock);
        if (thisBlock < blockSize)
            for (int ch = 0; ch < numChannels; ++ch)
                block.clear(ch, thisBlock, blockSize - thisBlock);
        juce::AudioBuffer<float> view(block.getArrayOfWritePointers(), numChannels, thisBlock);
        plugin.processBlock(view, midi);
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.copyFrom(ch, offset, block, ch, 0, thisBlock);
    }
}

static bool g_paramError = false;   // set if any requested parameter is missing

static void setParam(UniversalCompressor& plugin, const juce::String& paramID, float value)
{
    auto& params = plugin.getParameters();
    if (auto* param = params.getParameter(paramID))
        param->setValueNotifyingHost(params.getParameterRange(paramID).convertTo0to1(value));
    else
    {
        std::cerr << "ERROR: param '" << paramID << "' not found — test config invalid\n";
        g_paramError = true;   // a missing param would silently produce a false PASS
    }
}

static void configUnityMultiband(UniversalCompressor& plugin, float mixPercent)
{
    setParam(plugin, "mode", static_cast<float>(static_cast<int>(CompressorMode::Multiband)));
    setParam(plugin, "mix", mixPercent);
    for (const juce::String name : { "low", "lowmid", "highmid", "high" })
    {
        setParam(plugin, "mb_" + name + "_ratio", 1.0f);   // unity: no GR regardless of threshold
        setParam(plugin, "mb_" + name + "_enabled", 1.0f);
        setParam(plugin, "mb_" + name + "_bypass", 0.0f);
        setParam(plugin, "mb_" + name + "_solo", 0.0f);
        setParam(plugin, "mb_" + name + "_makeup", 0.0f);
    }
}

static float rmsDb(const std::vector<float>& v)
{
    double s = 0.0;
    for (float x : v) s += static_cast<double>(x) * x;
    const double r = std::sqrt(s / std::max<size_t>(1, v.size()));
    return r > 1e-12 ? 20.0f * static_cast<float>(std::log10(r)) : -240.0f;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    constexpr double sr = 48000.0;
    constexpr int    block = 512;
    constexpr int    warmup = 16384;          // let crossover state + mix smoother settle
    constexpr int    measure = 32768;
    const int        total = warmup + measure;

    // Deterministic white noise, stereo.
    juce::AudioBuffer<float> input(2, total);
    juce::Random rng(0x114C0FFE);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = input.getWritePointer(ch);
        for (int i = 0; i < total; ++i)
            d[i] = 0.25f * (rng.nextFloat() * 2.0f - 1.0f);
    }

    auto render = [&](float mixPercent)
    {
        UniversalCompressor plugin;
        plugin.setPlayConfigDetails(2, 2, sr, block);
        plugin.prepareToPlay(sr, block);
        configUnityMultiband(plugin, mixPercent);
        juce::AudioBuffer<float> buf;
        buf.makeCopyOf(input);
        processWithPlugin(plugin, buf, block);
        return buf;
    };

    juce::AudioBuffer<float> wet100 = render(100.0f);
    juce::AudioBuffer<float> mix50  = render(50.0f);

    // Null mix=50 against mix=100 over the settled window (both channels).
    std::vector<float> residual, reference;
    residual.reserve(static_cast<size_t>(measure) * 2);
    reference.reserve(static_cast<size_t>(measure) * 2);
    float peakDelta = 0.0f; double peakHzChan = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = warmup; i < total; ++i)
        {
            const float a = wet100.getSample(ch, i);
            const float b = mix50.getSample(ch, i);
            residual.push_back(b - a);
            reference.push_back(a);
            peakDelta = std::max(peakDelta, std::abs(b - a));
            (void)peakHzChan;
        }

    const float refDb = rmsDb(reference);
    const float resDb = rmsDb(residual);
    const float nullDb = resDb - refDb;   // residual relative to signal level

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n--- Issue #114: Multiband mix comb-filter null test ---\n";
    std::cout << "  (unity ratios: mix=50% MUST equal mix=100% when dry is phase-correct)\n";
    std::cout << "  reference (100% wet) RMS : " << refDb  << " dB\n";
    std::cout << "  residual (50% - 100%) RMS: " << resDb  << " dB\n";
    std::cout << "  null depth (rel signal)  : " << nullDb << " dB\n";
    std::cout << "  peak sample delta        : " << peakDelta << "\n";

    // Phase-correct dry -> the two renders are identical to the float/smoother
    // floor. Old raw-dry comb leaves a residual only ~6 dB below signal, and a
    // clearly audible per-sample delta (~0.3). Enforce BOTH a deep null and a
    // near-zero peak sample delta so neither metric can mask a regression.
    const float kNullDb    = -60.0f;
    const float kPeakDelta = 1.0e-4f;
    if (g_paramError)
    {
        std::cout << "  RESULT: FAIL (one or more parameters were missing — see errors above)\n";
        return 2;
    }
    const bool pass = (nullDb < kNullDb) && (peakDelta < kPeakDelta);
    std::cout << "  RESULT: " << (pass ? "PASS" : "FAIL")
              << " (need null < " << kNullDb << " dB rel signal AND peak delta < "
              << std::scientific << std::setprecision(1) << kPeakDelta << ")\n"
              << std::defaultfloat;
    return pass ? 0 : 1;
}
