#include <JuceHeader.h>
#include "../MultiQ.h"
#include "../MultiQPresets.h"
#include <cmath>
#include <iostream>
#include <iomanip>

// Generate stereo sine wave
static juce::AudioBuffer<float> generateSine(float freq, double sampleRate, int numSamples, float amplitude = 0.5f)
{
    juce::AudioBuffer<float> buf(2, numSamples);
    float phase = 0.0f;
    float inc = static_cast<float>(2.0 * juce::MathConstants<double>::pi * freq / sampleRate);
    for (int i = 0; i < numSamples; ++i)
    {
        float val = std::sin(phase) * amplitude;
        buf.setSample(0, i, val);
        buf.setSample(1, i, val);
        phase += inc;
        if (phase > juce::MathConstants<float>::twoPi)
            phase -= juce::MathConstants<float>::twoPi;
    }
    return buf;
}

// Generate white noise
static juce::AudioBuffer<float> generateNoise(double sampleRate, int numSamples, float amplitude = 0.5f)
{
    juce::AudioBuffer<float> buf(2, numSamples);
    juce::Random rng(42);
    for (int i = 0; i < numSamples; ++i)
    {
        float valL = (rng.nextFloat() * 2.0f - 1.0f) * amplitude;
        float valR = (rng.nextFloat() * 2.0f - 1.0f) * amplitude;
        buf.setSample(0, i, valL);
        buf.setSample(1, i, valR);
    }
    return buf;
}

// Process buffer through plugin in fixed-size blocks
static void processWithPlugin(MultiQ& plugin, juce::AudioBuffer<float>& buffer, int blockSize = 512)
{
    juce::MidiBuffer midi;
    int numSamples = buffer.getNumSamples();
    int numChannels = buffer.getNumChannels();

    juce::AudioBuffer<float> block(numChannels, blockSize);

    for (int offset = 0; offset < numSamples; offset += blockSize)
    {
        int thisBlock = std::min(blockSize, numSamples - offset);

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

// Measure RMS in dB for a channel, skipping initial transient
static float measureRMSdB(const juce::AudioBuffer<float>& buf, int channel, int skipSamples = 0)
{
    int start = skipSamples;
    int count = buf.getNumSamples() - start;
    if (count <= 0) return -100.0f;

    const float* data = buf.getReadPointer(channel);
    double sum = 0.0;
    for (int i = start; i < start + count; ++i)
        sum += static_cast<double>(data[i]) * data[i];

    float rms = static_cast<float>(std::sqrt(sum / count));
    return rms > 1e-10f ? 20.0f * std::log10(rms) : -100.0f;
}

// Measure peak in dB
static float measurePeakdB(const juce::AudioBuffer<float>& buf, int channel, int skipSamples = 0)
{
    int start = skipSamples;
    int count = buf.getNumSamples() - start;
    if (count <= 0) return -100.0f;

    const float* data = buf.getReadPointer(channel);
    float peak = 0.0f;
    for (int i = start; i < start + count; ++i)
        peak = std::max(peak, std::abs(data[i]));

    return peak > 1e-10f ? 20.0f * std::log10(peak) : -100.0f;
}

// Print first/last few samples
static void printSamples(const juce::AudioBuffer<float>& buf, int channel, int from, int count, const char* label)
{
    std::cout << "  " << label << " [" << from << ".." << (from + count - 1) << "]: ";
    for (int i = from; i < from + count && i < buf.getNumSamples(); ++i)
        std::cout << std::fixed << std::setprecision(6) << buf.getSample(channel, i) << " ";
    std::cout << "\n";
}

// Check for NaN/Inf
static bool hasNonFinite(const juce::AudioBuffer<float>& buf)
{
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        const float* data = buf.getReadPointer(ch);
        for (int i = 0; i < buf.getNumSamples(); ++i)
            if (!std::isfinite(data[i]))
                return true;
    }
    return false;
}

static void setParam(MultiQ& plugin, const juce::String& paramID, float value)
{
    if (auto* param = plugin.parameters.getParameter(paramID))
        param->setValueNotifyingHost(plugin.parameters.getParameterRange(paramID).convertTo0to1(value));
}

static void settleParams(MultiQ& plugin, int blockSize = 512, int numBlocks = 20)
{
    juce::AudioBuffer<float> warmup(2, blockSize);
    juce::MidiBuffer midi;
    float phase = 0.0f;
    float inc = static_cast<float>(2.0 * juce::MathConstants<double>::pi * 1000.0 / 44100.0);
    for (int b = 0; b < numBlocks; ++b)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            float val = std::sin(phase) * 0.1f;
            warmup.setSample(0, i, val);
            warmup.setSample(1, i, val);
            phase += inc;
        }
        plugin.processBlock(warmup, midi);
    }
}

class TestApp : public juce::JUCEApplicationBase
{
public:
    const juce::String getApplicationName() override { return "MultiQPresetAudioTest"; }
    const juce::String getApplicationVersion() override { return "1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted(const juce::String&) override {}
    void suspended() override {}
    void resumed() override {}
    void systemRequestedQuit() override { quit(); }
    void shutdown() override {}
    void unhandledException(const std::exception*, const juce::String&, int) override {}

    void initialise(const juce::String&) override
    {
        double sr = 44100.0;
        int blockSize = 512;
        int numSamples = static_cast<int>(sr * 2);  // 2 seconds
        int skip = static_cast<int>(sr * 0.5);       // Skip first 0.5s for settling

        auto plugin = std::make_unique<MultiQ>();
        auto layouts = plugin->getBusesLayout();
        layouts.getMainInputChannelSet() = juce::AudioChannelSet::stereo();
        layouts.getMainOutputChannelSet() = juce::AudioChannelSet::stereo();
        plugin->setBusesLayout(layouts);
        plugin->setPlayConfigDetails(2, 2, sr, blockSize);
        plugin->prepareToPlay(sr, blockSize);

        std::cout << "Multi-Q Preset Audio Test\n";
        std::cout << "=========================\n";
        std::cout << "Sample rate: " << sr << " Hz, Block size: " << blockSize << "\n";
        std::cout << "Signal: 2s stereo, mixed (1kHz sine + noise)\n\n";

        // ===== INIT PRESET (flat EQ) =====
        std::cout << "--- Init Preset (Flat EQ, Digital Mode) ---\n";
        plugin->setCurrentProgram(0);  // Index 0 = Init
        settleParams(*plugin, blockSize, 40);

        // Generate test signal: 1kHz sine + low-level noise
        auto inputBuf = generateSine(1000.0f, sr, numSamples, 0.5f);
        auto noiseBuf = generateNoise(sr, numSamples, 0.05f);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < numSamples; ++i)
                inputBuf.setSample(ch, i, inputBuf.getSample(ch, i) + noiseBuf.getSample(ch, i));

        // Keep a copy of the input for reference
        juce::AudioBuffer<float> inputCopy(inputBuf);

        // Process through Init preset
        auto initOutput = juce::AudioBuffer<float>(inputBuf);
        processWithPlugin(*plugin, initOutput);

        float initRmsL = measureRMSdB(initOutput, 0, skip);
        float initRmsR = measureRMSdB(initOutput, 1, skip);
        float initPeakL = measurePeakdB(initOutput, 0, skip);
        float inputRms = measureRMSdB(inputCopy, 0, skip);

        std::cout << "  Input:  RMS = " << std::fixed << std::setprecision(1) << inputRms << " dB\n";
        std::cout << "  Output: RMS L = " << initRmsL << " dB, R = " << initRmsR << " dB\n";
        std::cout << "  Output: Peak L = " << initPeakL << " dB\n";
        std::cout << "  Delta:  " << std::showpos << (initRmsL - inputRms) << " dB (should be ~0 for flat)\n" << std::noshowpos;
        std::cout << "  Finite: " << (hasNonFinite(initOutput) ? "NO (has NaN/Inf!)" : "Yes") << "\n";
        printSamples(initOutput, 0, skip, 5, "L samples");
        printSamples(initOutput, 1, skip, 5, "R samples");

        // ===== LOAD "Vocal Presence" PRESET =====
        std::cout << "\n--- Preset: Vocal Presence (Digital Mode, no re-init) ---\n";

        // Switch preset without releaseResources/prepareToPlay (like a DAW)
        plugin->setCurrentProgram(1);

        // Read back what preset was loaded
        std::cout << "  Loaded program index: " << plugin->getCurrentProgram()
                  << " name: \"" << plugin->getProgramName(plugin->getCurrentProgram()) << "\"\n";

        // Check OS mode
        {
            auto* osParam = plugin->parameters.getRawParameterValue(ParamIDs::hqEnabled);
            std::cout << "  OS mode param value: " << (osParam ? osParam->load() : -1.0f) << "\n";
        }

        // Debug: process one block at a time and check for explosion
        {
            juce::AudioBuffer<float> dbgBuf(2, blockSize);
            juce::MidiBuffer midi;
            for (int blk = 0; blk < 10; ++blk)
            {
                for (int i = 0; i < blockSize; ++i) {
                    float val = std::sin(static_cast<float>(blk * blockSize + i) * 0.14f) * 0.1f;
                    dbgBuf.setSample(0, i, val);
                    dbgBuf.setSample(1, i, val);
                }
                plugin->processBlock(dbgBuf, midi);
                float peak = dbgBuf.getMagnitude(0, 0, blockSize);
                std::cout << "  Block " << blk << ": peak = " << std::fixed << std::setprecision(4) << peak;
                if (peak > 1.0f) std::cout << " ** OVER **";
                if (peak > 100.0f) std::cout << " ** EXPLOSION **";
                std::cout << "\n";
                if (peak > 1e10f) break;  // Stop if already exploded
            }
        }

        // Check for explosion during settling
        {
            juce::AudioBuffer<float> probe(2, blockSize);
            juce::MidiBuffer midi;
            for (int i = 0; i < blockSize; ++i) {
                probe.setSample(0, i, std::sin(static_cast<float>(i) * 0.14f) * 0.5f);
                probe.setSample(1, i, std::sin(static_cast<float>(i) * 0.14f) * 0.5f);
            }
            plugin->processBlock(probe, midi);
            float probePeak = measurePeakdB(probe, 0);
            std::cout << "  Post-settle probe peak: " << std::fixed << std::setprecision(1) << probePeak << " dB\n";
        }

        // Check curve magnitudes after settling
        {
            std::cout << "  Curve check:\n";
            for (int b = 0; b < 8; ++b)
            {
                float mag500 = plugin->getPerBandMagnitude(b, 500.0f);
                float mag1k = plugin->getPerBandMagnitude(b, 1000.0f);
                float mag5k = plugin->getPerBandMagnitude(b, 5000.0f);
                std::cout << "    band " << b << ": 500Hz=" << std::fixed << std::setprecision(1) << mag500
                          << " dB, 1kHz=" << mag1k << " dB, 5kHz=" << mag5k << " dB\n";
            }
            float total500 = plugin->getFrequencyResponseMagnitude(500.0f);
            float total1k = plugin->getFrequencyResponseMagnitude(1000.0f);
            std::cout << "    TOTAL: 500Hz=" << total500 << " dB, 1kHz=" << total1k << " dB\n";
        }

        auto vocalOutput = juce::AudioBuffer<float>(inputCopy);  // Fresh copy of input
        processWithPlugin(*plugin, vocalOutput);

        float vocalRmsL = measureRMSdB(vocalOutput, 0, skip);
        float vocalRmsR = measureRMSdB(vocalOutput, 1, skip);
        float vocalPeakL = measurePeakdB(vocalOutput, 0, skip);

        std::cout << "  Output: RMS L = " << std::fixed << std::setprecision(1) << vocalRmsL << " dB, R = " << vocalRmsR << " dB\n";
        std::cout << "  Output: Peak L = " << vocalPeakL << " dB\n";
        std::cout << "  Delta vs flat: " << std::showpos << (vocalRmsL - initRmsL) << " dB\n" << std::noshowpos;
        std::cout << "  Finite: " << (hasNonFinite(vocalOutput) ? "NO (has NaN/Inf!)" : "Yes") << "\n";
        printSamples(vocalOutput, 0, skip, 5, "L samples");
        printSamples(vocalOutput, 1, skip, 5, "R samples");

        // ===== LOAD "Drum Bus Punch" BRITISH PRESET =====
        std::cout << "\n--- Preset: Drum Bus Punch (British Mode) ---\n";

        plugin->releaseResources();
        plugin->prepareToPlay(sr, blockSize);

        // Find the Drum Bus Punch preset index
        int drumPresetIdx = -1;
        for (int i = 0; i < plugin->getNumPrograms(); ++i)
        {
            if (plugin->getProgramName(i) == "Drum Bus Punch")
            {
                drumPresetIdx = i;
                break;
            }
        }

        if (drumPresetIdx >= 0)
        {
            plugin->setCurrentProgram(drumPresetIdx);
            std::cout << "  Loaded program index: " << drumPresetIdx
                      << " name: \"" << plugin->getProgramName(drumPresetIdx) << "\"\n";

            settleParams(*plugin, blockSize, 40);

            auto drumOutput = juce::AudioBuffer<float>(inputCopy);
            processWithPlugin(*plugin, drumOutput);

            float drumRmsL = measureRMSdB(drumOutput, 0, skip);
            float drumRmsR = measureRMSdB(drumOutput, 1, skip);
            float drumPeakL = measurePeakdB(drumOutput, 0, skip);

            std::cout << "  Output: RMS L = " << std::fixed << std::setprecision(1) << drumRmsL << " dB, R = " << drumRmsR << " dB\n";
            std::cout << "  Output: Peak L = " << drumPeakL << " dB\n";
            std::cout << "  Delta vs flat: " << std::showpos << (drumRmsL - initRmsL) << " dB\n" << std::noshowpos;
            std::cout << "  Finite: " << (hasNonFinite(drumOutput) ? "NO (has NaN/Inf!)" : "Yes") << "\n";
            printSamples(drumOutput, 0, skip, 5, "L samples");
            printSamples(drumOutput, 1, skip, 5, "R samples");
        }
        else
        {
            std::cout << "  [SKIP] Drum Bus Punch preset not found\n";
        }

        // ===== LOAD "Vintage Warmth" TUBE PRESET =====
        std::cout << "\n--- Preset: Vintage Warmth (Tube Mode) ---\n";

        plugin->releaseResources();
        plugin->prepareToPlay(sr, blockSize);

        int tubePresetIdx = -1;
        for (int i = 0; i < plugin->getNumPrograms(); ++i)
        {
            if (plugin->getProgramName(i) == "Vintage Warmth")
            {
                tubePresetIdx = i;
                break;
            }
        }

        if (tubePresetIdx >= 0)
        {
            plugin->setCurrentProgram(tubePresetIdx);
            std::cout << "  Loaded program index: " << tubePresetIdx
                      << " name: \"" << plugin->getProgramName(tubePresetIdx) << "\"\n";

            settleParams(*plugin, blockSize, 40);

            auto tubeOutput = juce::AudioBuffer<float>(inputCopy);
            processWithPlugin(*plugin, tubeOutput);

            float tubeRmsL = measureRMSdB(tubeOutput, 0, skip);
            float tubeRmsR = measureRMSdB(tubeOutput, 1, skip);
            float tubePeakL = measurePeakdB(tubeOutput, 0, skip);

            std::cout << "  Output: RMS L = " << std::fixed << std::setprecision(1) << tubeRmsL << " dB, R = " << tubeRmsR << " dB\n";
            std::cout << "  Output: Peak L = " << tubePeakL << " dB\n";
            std::cout << "  Delta vs flat: " << std::showpos << (tubeRmsL - initRmsL) << " dB\n" << std::noshowpos;
            std::cout << "  Finite: " << (hasNonFinite(tubeOutput) ? "NO (has NaN/Inf!)" : "Yes") << "\n";
            printSamples(tubeOutput, 0, skip, 5, "L samples");
            printSamples(tubeOutput, 1, skip, 5, "R samples");
        }
        else
        {
            std::cout << "  [SKIP] Vintage Warmth preset not found\n";
        }

        // ===== LOAD "De-Esser" DYNAMIC PRESET =====
        std::cout << "\n--- Preset: De-Esser (Dynamic EQ) ---\n";

        plugin->releaseResources();
        plugin->prepareToPlay(sr, blockSize);

        int deEsserIdx = -1;
        for (int i = 0; i < plugin->getNumPrograms(); ++i)
        {
            if (plugin->getProgramName(i) == "De-Esser")
            {
                deEsserIdx = i;
                break;
            }
        }

        if (deEsserIdx >= 0)
        {
            plugin->setCurrentProgram(deEsserIdx);
            std::cout << "  Loaded program index: " << deEsserIdx
                      << " name: \"" << plugin->getProgramName(deEsserIdx) << "\"\n";

            settleParams(*plugin, blockSize, 40);

            // Use a 6kHz sine (sibilance range) for de-esser test
            auto sibilanceInput = generateSine(6000.0f, sr, numSamples, 0.5f);
            auto deEsserOutput = juce::AudioBuffer<float>(sibilanceInput);
            processWithPlugin(*plugin, deEsserOutput);

            float sibilanceInputRms = measureRMSdB(sibilanceInput, 0, skip);
            float deEsserRmsL = measureRMSdB(deEsserOutput, 0, skip);

            std::cout << "  Input (6kHz sine): RMS = " << std::fixed << std::setprecision(1) << sibilanceInputRms << " dB\n";
            std::cout << "  Output: RMS L = " << deEsserRmsL << " dB\n";
            std::cout << "  Delta: " << std::showpos << (deEsserRmsL - sibilanceInputRms) << " dB\n" << std::noshowpos;
            std::cout << "  Finite: " << (hasNonFinite(deEsserOutput) ? "NO (has NaN/Inf!)" : "Yes") << "\n";
            printSamples(deEsserOutput, 0, skip, 5, "L samples");
        }
        else
        {
            std::cout << "  [SKIP] De-Esser preset not found\n";
        }

        // List all available presets
        std::cout << "\n--- All Available Presets ---\n";
        for (int i = 0; i < plugin->getNumPrograms(); ++i)
            std::cout << "  [" << std::setw(2) << i << "] " << plugin->getProgramName(i) << "\n";

        std::cout << "\n=========================\n";
        std::cout << "Preset audio test complete.\n";

        setApplicationReturnValue(0);
        quit();
    }
};

START_JUCE_APPLICATION(TestApp)
