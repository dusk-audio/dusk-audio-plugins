#include <JuceHeader.h>
#include "../MultiQ.h"
#include <cmath>
#include <iostream>
#include <iomanip>
#include <random>

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
    setParam(plugin, ParamIDs::linearPhaseEnabled, 0.0f);
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

// Check that all samples in a buffer are finite (not NaN or Inf)
static bool allSamplesFinite(const juce::AudioBuffer<float>& buf)
{
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        const float* data = buf.getReadPointer(ch);
        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            if (!std::isfinite(data[i]))
                return false;
        }
    }
    return true;
}

// ===== TEST: Rapid parameter changes =====
static void testRapidParamChanges(MultiQ& plugin)
{
    std::cout << "\n--- Test: Rapid Parameter Changes ---\n";
    resetPlugin(plugin);

    // Enable all parametric bands
    for (int i = 1; i <= 8; ++i)
        setParam(plugin, ParamIDs::bandEnabled(i), 1.0f);

    int blockSize = 512;
    int numBlocks = 1000;
    juce::AudioBuffer<float> block(2, blockSize);
    juce::MidiBuffer midi;
    std::mt19937 rng(42);  // Fixed seed for reproducibility

    std::uniform_real_distribution<float> freqDist(20.0f, 20000.0f);
    std::uniform_real_distribution<float> gainDist(-18.0f, 18.0f);
    std::uniform_real_distribution<float> qDist(0.1f, 10.0f);

    bool allFinite = true;
    int badBlock = -1;

    for (int b = 0; b < numBlocks; ++b)
    {
        // Rapidly change parameters on every block
        int band = (b % 8) + 1;  // Cycle through bands 1-8
        setParam(plugin, ParamIDs::bandFreq(band), freqDist(rng));
        setParam(plugin, ParamIDs::bandGain(band), gainDist(rng));
        setParam(plugin, ParamIDs::bandQ(band), qDist(rng));

        // Fill block with low-level white noise
        for (int i = 0; i < blockSize; ++i)
        {
            float val = (static_cast<float>(rng()) / static_cast<float>(rng.max()) - 0.5f) * 0.5f;
            block.setSample(0, i, val);
            block.setSample(1, i, val);
        }

        plugin.processBlock(block, midi);

        if (!allSamplesFinite(block))
        {
            allFinite = false;
            badBlock = b;
            break;
        }
    }

    if (!allFinite)
        std::cout << "  First bad block: " << badBlock << "\n";

    check("Rapid param changes: 1000 blocks, all samples finite", allFinite);
}

// ===== TEST: Rapid oversampling mode toggles =====
static void testRapidOversamplingToggle(MultiQ& plugin)
{
    std::cout << "\n--- Test: Rapid Oversampling Toggle ---\n";
    resetPlugin(plugin);

    // Enable a few bands with various settings
    for (int i = 2; i <= 5; ++i)
    {
        setParam(plugin, ParamIDs::bandEnabled(i), 1.0f);
        setParam(plugin, ParamIDs::bandGain(i), 3.0f);
        setParam(plugin, ParamIDs::bandQ(i), 1.0f);
    }
    setParam(plugin, ParamIDs::bandFreq(2), 200.0f);
    setParam(plugin, ParamIDs::bandFreq(3), 1000.0f);
    setParam(plugin, ParamIDs::bandFreq(4), 4000.0f);
    setParam(plugin, ParamIDs::bandFreq(5), 10000.0f);

    int blockSize = 512;
    int numBlocks = 500;
    juce::AudioBuffer<float> block(2, blockSize);
    juce::MidiBuffer midi;
    std::mt19937 rng(123);

    bool allFinite = true;
    int badBlock = -1;

    for (int b = 0; b < numBlocks; ++b)
    {
        // Toggle oversampling every 10 blocks
        if (b % 10 == 0)
        {
            float osMode = static_cast<float>(b / 10 % 3);  // Cycle 0, 1, 2
            setParam(plugin, ParamIDs::hqEnabled, osMode);
        }

        // Fill block with sine + noise
        for (int i = 0; i < blockSize; ++i)
        {
            float val = std::sin(static_cast<float>(i) * 0.1f) * 0.3f
                      + (static_cast<float>(rng()) / static_cast<float>(rng.max()) - 0.5f) * 0.1f;
            block.setSample(0, i, val);
            block.setSample(1, i, val);
        }

        plugin.processBlock(block, midi);

        if (!allSamplesFinite(block))
        {
            allFinite = false;
            badBlock = b;
            break;
        }
    }

    if (!allFinite)
        std::cout << "  First bad block: " << badBlock << "\n";

    check("Rapid OS toggle: 500 blocks, all samples finite", allFinite);
}

// ===== TEST: Rapid band enable/disable =====
static void testRapidBandToggle(MultiQ& plugin)
{
    std::cout << "\n--- Test: Rapid Band Enable/Disable ---\n";
    resetPlugin(plugin);

    // Set up bands with various gain/freq settings
    for (int i = 1; i <= 8; ++i)
    {
        setParam(plugin, ParamIDs::bandFreq(i), 100.0f * static_cast<float>(i));
        setParam(plugin, ParamIDs::bandGain(i), (i % 2 == 0) ? 6.0f : -6.0f);
        setParam(plugin, ParamIDs::bandQ(i), 1.5f);
    }

    int blockSize = 512;
    int numBlocks = 1000;
    juce::AudioBuffer<float> block(2, blockSize);
    juce::MidiBuffer midi;
    std::mt19937 rng(456);

    bool allFinite = true;
    int badBlock = -1;

    for (int b = 0; b < numBlocks; ++b)
    {
        // Toggle random bands on/off every block
        int band = (rng() % 8) + 1;
        float enabled = (rng() % 2 == 0) ? 1.0f : 0.0f;
        setParam(plugin, ParamIDs::bandEnabled(band), enabled);

        // Also randomly toggle invert and phase invert
        if (b % 5 == 0)
        {
            int invBand = (rng() % 8) + 1;
            setParam(plugin, ParamIDs::bandInvert(invBand),
                     (rng() % 2 == 0) ? 1.0f : 0.0f);
        }
        if (b % 7 == 0)
        {
            int phBand = (rng() % 8) + 1;
            setParam(plugin, ParamIDs::bandPhaseInvert(phBand),
                     (rng() % 2 == 0) ? 1.0f : 0.0f);
        }

        // Fill block with audio
        for (int i = 0; i < blockSize; ++i)
        {
            float val = std::sin(static_cast<float>(i) * 0.05f) * 0.5f;
            block.setSample(0, i, val);
            block.setSample(1, i, val);
        }

        plugin.processBlock(block, midi);

        if (!allSamplesFinite(block))
        {
            allFinite = false;
            badBlock = b;
            break;
        }
    }

    if (!allFinite)
        std::cout << "  First bad block: " << badBlock << "\n";

    check("Rapid band toggle: 1000 blocks, all samples finite", allFinite);
}

// ===== TEST: All features active simultaneously =====
static void testAllFeaturesActive(MultiQ& plugin)
{
    std::cout << "\n--- Test: All Features Active Simultaneously ---\n";
    resetPlugin(plugin);

    // Enable everything: all bands, dynamics, oversampling, various gains
    for (int i = 1; i <= 8; ++i)
    {
        setParam(plugin, ParamIDs::bandEnabled(i), 1.0f);
        setParam(plugin, ParamIDs::bandFreq(i), 50.0f * std::pow(2.0f, static_cast<float>(i)));
        setParam(plugin, ParamIDs::bandGain(i), (i % 2 == 0) ? 6.0f : -6.0f);
        setParam(plugin, ParamIDs::bandQ(i), 0.5f + static_cast<float>(i) * 0.3f);
        setParam(plugin, ParamIDs::bandInvert(i), (i == 3) ? 1.0f : 0.0f);
        setParam(plugin, ParamIDs::bandPhaseInvert(i), (i == 5) ? 1.0f : 0.0f);
        setParam(plugin, ParamIDs::bandPan(i), (i % 3 == 0) ? -0.5f : (i % 3 == 1) ? 0.5f : 0.0f);
    }

    setParam(plugin, ParamIDs::masterGain, -2.0f);
    setParam(plugin, ParamIDs::hqEnabled, 1.0f);  // 2x oversampling
    setParam(plugin, ParamIDs::limiterEnabled, 1.0f);
    setParam(plugin, ParamIDs::autoGainEnabled, 1.0f);

    int blockSize = 512;
    int numBlocks = 500;
    juce::AudioBuffer<float> block(2, blockSize);
    juce::MidiBuffer midi;
    std::mt19937 rng(789);

    bool allFinite = true;
    int badBlock = -1;

    for (int b = 0; b < numBlocks; ++b)
    {
        // Fill block with varied content: mix of sine and noise
        for (int i = 0; i < blockSize; ++i)
        {
            float val = std::sin(static_cast<float>(b * blockSize + i) * 0.02f) * 0.4f
                      + (static_cast<float>(rng()) / static_cast<float>(rng.max()) - 0.5f) * 0.2f;
            block.setSample(0, i, val);
            block.setSample(1, i, val);
        }

        plugin.processBlock(block, midi);

        if (!allSamplesFinite(block))
        {
            allFinite = false;
            badBlock = b;
            break;
        }
    }

    if (!allFinite)
        std::cout << "  First bad block: " << badBlock << "\n";

    check("All features active: 500 blocks, all samples finite", allFinite);
}

// ===== MAIN =====

class TestApp : public juce::JUCEApplicationBase
{
public:
    const juce::String getApplicationName() override { return "MultiQStressTest"; }
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

        std::cout << "Multi-Q Stress Tests\n";
        std::cout << "====================\n";
        std::cout << "Plugin: " << plugin->getTotalNumInputChannels() << " in, "
                  << plugin->getTotalNumOutputChannels() << " out, "
                  << plugin->getSampleRate() << " Hz\n";

        testRapidParamChanges(*plugin);
        testRapidOversamplingToggle(*plugin);
        testRapidBandToggle(*plugin);
        testAllFeaturesActive(*plugin);

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
