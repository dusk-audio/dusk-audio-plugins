/*
 * Factory-preset level gate.
 *
 * "Smooth Opto Vocal" muted the plugin: applyPreset wrote preset.makeup (a dB
 * figure, 5) straight into "opto_gain", which is a 0-100 dial where 50 = unity
 * and each unit is 0.8 dB — so 5 landed at -36 dB. The host "Default" program
 * had the same class of bug (opto_gain set to knob 0 = -40 dB). Below 100 % Mix
 * the dry path masked it, which is why it read as "silent at 100 %".
 *
 * This gate loads EVERY program (Default + all factory presets), pushes noise
 * through it, and fails if the output level is nowhere near the input. It is
 * deliberately loose: it is not a voicing check, it catches units-mismatch bugs
 * that leave a preset unusable.
 */

#include <JuceHeader.h>
#include "../UniversalCompressor.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>

static void processWithPlugin(UniversalCompressor& plugin, juce::AudioBuffer<float>& buffer, int blockSize)
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

static float rmsDb(const juce::AudioBuffer<float>& buf, int startSample, int numSamples)
{
    double sum = 0.0;
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        const float* d = buf.getReadPointer(ch);
        for (int i = startSample; i < startSample + numSamples; ++i)
            sum += static_cast<double>(d[i]) * d[i];
    }
    const auto n = static_cast<double>(numSamples) * std::max(1, buf.getNumChannels());
    const double r = std::sqrt(sum / std::max(1.0, n));
    return r > 1e-12 ? 20.0f * static_cast<float>(std::log10(r)) : -240.0f;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    constexpr double sr = 48000.0;
    constexpr int block = 512;
    constexpr int warmup = 24576;    // envelopes + auto-gain estimator settle
    constexpr int measure = 49152;
    constexpr int total = warmup + measure;

    // Deterministic noise at roughly -18 dBFS RMS — hot enough to engage the
    // presets' thresholds, quiet enough to stay off the output limiter.
    juce::AudioBuffer<float> input(2, total);
    juce::Random rng(0x9E5E75);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = input.getWritePointer(ch);
        for (int i = 0; i < total; ++i)
            d[i] = 0.22f * (rng.nextFloat() * 2.0f - 1.0f);
    }
    const float inDb = rmsDb(input, warmup, measure);

    // Asymmetric on purpose. Losing level is always a bug: no preset has a
    // reason to be 12 dB down, and the units mismatch this gate was written for
    // buried the Opto presets 36 dB down. Gaining level can be intentional (FET
    // presets drive the input hard by design), so only runaway gain — enough to
    // sit on the output limiter — fails, and anything merely hot is reported.
    constexpr float kMinDeviationDb  = -12.0f;   // below this: fail (mute class)
    constexpr float kMaxDeviationDb  =  24.0f;   // above this: fail (runaway)
    constexpr float kHotWarningDb    =  12.0f;   // above this: report, do not fail

    int numPrograms = 0;
    {
        UniversalCompressor probe;
        numPrograms = probe.getNumPrograms();
    }

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n--- Factory preset level gate ---\n";
    std::cout << "  input RMS: " << inDb << " dB, allowed delta "
              << kMinDeviationDb << " .. " << kMaxDeviationDb
              << " dB (hot warning above " << kHotWarningDb << " dB)\n\n";

    int failures = 0;
    int hot = 0;
    for (int program = 0; program < numPrograms; ++program)
    {
        UniversalCompressor plugin;
        plugin.setPlayConfigDetails(2, 2, sr, block);
        plugin.prepareToPlay(sr, block);
        plugin.setCurrentProgram(program);

        juce::AudioBuffer<float> buf;
        buf.makeCopyOf(input);
        processWithPlugin(plugin, buf, block);

        const float outDb = rmsDb(buf, warmup, measure);
        const float delta = outDb - inDb;
        const bool ok = (delta >= kMinDeviationDb) && (delta <= kMaxDeviationDb);
        const bool isHot = ok && (delta > kHotWarningDb);
        if (! ok)
            ++failures;
        if (isHot)
            ++hot;

        std::cout << (ok ? (isHot ? "  hot  " : "  ok   ") : "  FAIL ")
                  << std::setw(28) << std::left << plugin.getProgramName(program).toStdString()
                  << std::right << " out " << std::setw(8) << outDb
                  << " dB   delta " << std::setw(7) << delta << " dB\n";
    }

    std::cout << "\n  RESULT: " << (failures == 0 ? "PASS" : "FAIL")
              << " (" << failures << " of " << numPrograms << " programs outside tolerance, "
              << hot << " hot)\n";
    return failures == 0 ? 0 : 1;
}
