#include <JuceHeader.h>
#include "../MultiQ.h"
#include <cmath>
#include <iostream>
#include <iomanip>

static int passed = 0, failed = 0;

static void check(const char* name, bool condition)
{
    if (condition) {
        std::cout << "\033[32m[PASS]\033[0m " << name << "\n";
        ++passed;
    } else {
        std::cout << "\033[31m[FAIL]\033[0m " << name << "\n";
        ++failed;
    }
}

static void checkDb(const char* name, float actual, float expected, float tolerance)
{
    bool ok = std::abs(actual - expected) <= tolerance;
    if (ok) {
        std::cout << "\033[32m[PASS]\033[0m " << name
                  << ": " << std::fixed << std::setprecision(1) << actual
                  << " dB (expected " << expected << " dB, tol " << tolerance << ")\n";
        ++passed;
    } else {
        std::cout << "\033[31m[FAIL]\033[0m " << name
                  << ": " << std::fixed << std::setprecision(1) << actual
                  << " dB (expected " << expected << " dB, tol " << tolerance << ")\n";
        ++failed;
    }
}

// Generate stereo sine wave
static juce::AudioBuffer<float> generateSine(float freq, double sampleRate, int numSamples)
{
    juce::AudioBuffer<float> buf(2, numSamples);
    float phase = 0.0f;
    float inc = static_cast<float>(2.0 * juce::MathConstants<double>::pi * freq / sampleRate);
    for (int i = 0; i < numSamples; ++i)
    {
        float val = std::sin(phase);
        buf.setSample(0, i, val);
        buf.setSample(1, i, val);
        phase += inc;
        if (phase > juce::MathConstants<float>::twoPi)
            phase -= juce::MathConstants<float>::twoPi;
    }
    return buf;
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

// Set a parameter by ID
static void setParam(MultiQ& plugin, const juce::String& paramID, float value)
{
    if (auto* param = plugin.parameters.getParameter(paramID))
        param->setValueNotifyingHost(plugin.parameters.getParameterRange(paramID).convertTo0to1(value));
}

// Reset plugin state between tests and run warmup blocks
static void resetPlugin(MultiQ& plugin, double sampleRate = 44100.0, int blockSize = 512)
{
    plugin.releaseResources();
    plugin.prepareToPlay(sampleRate, blockSize);

    for (int i = 1; i <= 8; ++i)
    {
        setParam(plugin, ParamIDs::bandGain(i), 0.0f);
        setParam(plugin, ParamIDs::bandEnabled(i), (i >= 2 && i <= 7) ? 1.0f : 0.0f);
        setParam(plugin, ParamIDs::bandInvert(i), 0.0f);
        setParam(plugin, ParamIDs::bandPhaseInvert(i), 0.0f);
        setParam(plugin, ParamIDs::bandPan(i), 0.0f);
    }
    setParam(plugin, ParamIDs::masterGain, 0.0f);
    setParam(plugin, ParamIDs::bypass, 0.0f);
    setParam(plugin, ParamIDs::hqEnabled, 0.0f);
    setParam(plugin, ParamIDs::autoGainEnabled, 0.0f);
    setParam(plugin, ParamIDs::limiterEnabled, 0.0f);

    juce::AudioBuffer<float> warmup(2, blockSize);
    juce::MidiBuffer midi;
    for (int i = 0; i < 10; ++i)
    {
        warmup.clear();
        plugin.processBlock(warmup, midi);
    }
}

// Apply params and settle by processing low-level audio
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

// ===== TEST: Oversampling preserves EQ response at high frequencies =====
static void testOversamplingFrequencyResponse(MultiQ& plugin)
{
    std::cout << "\n--- Test: Oversampling Frequency Response ---\n";

    double sr = 44100.0;
    int numSamples = static_cast<int>(sr * 2);  // 2 seconds
    int skip = static_cast<int>(sr);  // Skip first second for settling

    // Test with a 15kHz sine and a boost at 15kHz on band 4
    // Compare no oversampling vs 2x vs 4x

    float boostResults[3] = {};
    float flatResults[3] = {};
    const char* modeNames[3] = { "Off", "2x", "4x" };

    for (int osMode = 0; osMode < 3; ++osMode)
    {
        // Flat reference at 15kHz
        resetPlugin(plugin, sr);
        setParam(plugin, ParamIDs::hqEnabled, static_cast<float>(osMode));
        settleParams(plugin, 512, 30);

        auto bufFlat = generateSine(15000.0f, sr, numSamples);
        processWithPlugin(plugin, bufFlat);
        flatResults[osMode] = measureRMSdB(bufFlat, 0, skip);

        // Boosted at 15kHz
        resetPlugin(plugin, sr);
        setParam(plugin, ParamIDs::hqEnabled, static_cast<float>(osMode));
        setParam(plugin, ParamIDs::bandFreq(4), 15000.0f);
        setParam(plugin, ParamIDs::bandGain(4), 6.0f);
        setParam(plugin, ParamIDs::bandQ(4), 1.0f);
        settleParams(plugin, 512, 30);

        auto bufBoosted = generateSine(15000.0f, sr, numSamples);
        processWithPlugin(plugin, bufBoosted);
        boostResults[osMode] = measureRMSdB(bufBoosted, 0, skip);
    }

    float deltaOff = boostResults[0] - flatResults[0];
    float delta2x  = boostResults[1] - flatResults[1];
    float delta4x  = boostResults[2] - flatResults[2];

    std::cout << "  OS Off: boost delta = " << std::fixed << std::setprecision(1)
              << deltaOff << " dB\n";
    std::cout << "  OS 2x:  boost delta = " << delta2x << " dB\n";
    std::cout << "  OS 4x:  boost delta = " << delta4x << " dB\n";

    // Without oversampling at 44.1kHz, 15kHz is near Nyquist so the EQ response
    // will be cramped/reduced. With oversampling, it should be closer to the
    // target 6dB boost.
    check("OS Off: 15kHz boost produces some gain", deltaOff > 1.0f);
    check("OS 2x: 15kHz boost is closer to 6dB than OS Off",
          std::abs(delta2x - 6.0f) < std::abs(deltaOff - 6.0f) + 0.5f);
    checkDb("OS 4x: 15kHz boost near target", delta4x, 6.0f, 2.0f);
}

// ===== TEST: Oversampling boost at 15kHz is at the correct frequency =====
static void testOversamplingBoostFrequency(MultiQ& plugin)
{
    std::cout << "\n--- Test: Oversampling Boost at Correct Frequency ---\n";

    double sr = 44100.0;
    int numSamples = static_cast<int>(sr * 2);
    int skip = static_cast<int>(sr);

    // With 4x oversampling, boost at 15kHz should affect 15kHz, not a cramped frequency
    resetPlugin(plugin, sr);
    setParam(plugin, ParamIDs::hqEnabled, 2.0f);  // 4x
    setParam(plugin, ParamIDs::bandFreq(4), 15000.0f);
    setParam(plugin, ParamIDs::bandGain(4), 6.0f);
    setParam(plugin, ParamIDs::bandQ(4), 1.0f);
    settleParams(plugin, 512, 30);

    // Flat reference with 4x
    resetPlugin(plugin, sr);
    setParam(plugin, ParamIDs::hqEnabled, 2.0f);
    settleParams(plugin, 512, 30);

    auto bufFlat5k = generateSine(5000.0f, sr, numSamples);
    processWithPlugin(plugin, bufFlat5k);
    float flat5k = measureRMSdB(bufFlat5k, 0, skip);

    auto bufFlat15k = generateSine(15000.0f, sr, numSamples);
    processWithPlugin(plugin, bufFlat15k);
    float flat15k = measureRMSdB(bufFlat15k, 0, skip);

    // Boost at 15kHz with 4x OS
    resetPlugin(plugin, sr);
    setParam(plugin, ParamIDs::hqEnabled, 2.0f);
    setParam(plugin, ParamIDs::bandFreq(4), 15000.0f);
    setParam(plugin, ParamIDs::bandGain(4), 6.0f);
    setParam(plugin, ParamIDs::bandQ(4), 1.0f);
    settleParams(plugin, 512, 30);

    auto bufBoosted5k = generateSine(5000.0f, sr, numSamples);
    processWithPlugin(plugin, bufBoosted5k);
    float boosted5k = measureRMSdB(bufBoosted5k, 0, skip);

    auto bufBoosted15k = generateSine(15000.0f, sr, numSamples);
    processWithPlugin(plugin, bufBoosted15k);
    float boosted15k = measureRMSdB(bufBoosted15k, 0, skip);

    float delta5k = boosted5k - flat5k;
    float delta15k = boosted15k - flat15k;

    // The boost at 15kHz should be significantly larger than at 5kHz
    // (5kHz is far from the 15kHz center frequency)
    check("OS 4x: 15kHz sees more boost than 5kHz", delta15k > delta5k + 2.0f);
    checkDb("OS 4x: 5kHz is near flat (far from boost center)", delta5k, 0.0f, 2.0f);
    check("OS 4x: 15kHz has significant boost", delta15k > 3.0f);
}

// ===== TEST: Oversampling does not alter low-frequency response =====
static void testOversamplingLowFreqUnchanged(MultiQ& plugin)
{
    std::cout << "\n--- Test: Oversampling Low-Freq Response Unchanged ---\n";

    double sr = 44100.0;
    int numSamples = static_cast<int>(sr * 2);
    int skip = static_cast<int>(sr);

    // A 1kHz boost should produce the same result with and without oversampling
    float deltas[3] = {};
    const char* modeNames[3] = { "Off", "2x", "4x" };

    for (int osMode = 0; osMode < 3; ++osMode)
    {
        // Flat reference
        resetPlugin(plugin, sr);
        setParam(plugin, ParamIDs::hqEnabled, static_cast<float>(osMode));
        settleParams(plugin, 512, 30);

        auto bufFlat = generateSine(1000.0f, sr, numSamples);
        processWithPlugin(plugin, bufFlat);
        float flatRMS = measureRMSdB(bufFlat, 0, skip);

        // Boosted
        resetPlugin(plugin, sr);
        setParam(plugin, ParamIDs::hqEnabled, static_cast<float>(osMode));
        setParam(plugin, ParamIDs::bandFreq(4), 1000.0f);
        setParam(plugin, ParamIDs::bandGain(4), 6.0f);
        setParam(plugin, ParamIDs::bandQ(4), 1.0f);
        settleParams(plugin, 512, 30);

        auto bufBoosted = generateSine(1000.0f, sr, numSamples);
        processWithPlugin(plugin, bufBoosted);
        float boostRMS = measureRMSdB(bufBoosted, 0, skip);

        deltas[osMode] = boostRMS - flatRMS;
    }

    for (int osMode = 0; osMode < 3; ++osMode)
    {
        juce::String label = juce::String("OS ") + modeNames[osMode] + ": 1kHz boost ~6dB";
        checkDb(label.toRawUTF8(), deltas[osMode], 6.0f, 1.5f);
    }

    // All modes should produce similar results at 1kHz
    check("OS consistency: Off vs 2x at 1kHz",
          std::abs(deltas[0] - deltas[1]) < 1.5f);
    check("OS consistency: 2x vs 4x at 1kHz",
          std::abs(deltas[1] - deltas[2]) < 1.5f);
}

// ===== MAIN =====

class TestApp : public juce::JUCEApplicationBase
{
public:
    const juce::String getApplicationName() override { return "MultiQOversamplingTest"; }
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
        auto plugin = std::make_unique<MultiQ>();

        auto layouts = plugin->getBusesLayout();
        layouts.getMainInputChannelSet() = juce::AudioChannelSet::stereo();
        layouts.getMainOutputChannelSet() = juce::AudioChannelSet::stereo();
        plugin->setBusesLayout(layouts);
        plugin->setPlayConfigDetails(2, 2, 44100.0, 512);
        plugin->prepareToPlay(44100.0, 512);

        std::cout << "Multi-Q Oversampling Tests\n";
        std::cout << "=========================\n";
        std::cout << "Plugin: " << plugin->getTotalNumInputChannels() << " in, "
                  << plugin->getTotalNumOutputChannels() << " out, "
                  << plugin->getSampleRate() << " Hz\n";

        testOversamplingFrequencyResponse(*plugin);
        testOversamplingBoostFrequency(*plugin);
        testOversamplingLowFreqUnchanged(*plugin);

        std::cout << "\n=============================\n";
        std::cout << "Results: " << passed << "/" << (passed + failed) << " passed\n";

        if (failed > 0)
            std::cout << "\033[31m" << failed << " FAILED\033[0m\n";
        else
            std::cout << "\033[32mAll tests passed!\033[0m\n";

        setApplicationReturnValue(failed > 0 ? 1 : 0);
        quit();
    }
};

START_JUCE_APPLICATION(TestApp)
