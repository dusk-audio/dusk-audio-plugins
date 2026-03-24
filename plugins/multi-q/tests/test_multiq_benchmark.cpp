#include <JuceHeader.h>
#include "../MultiQ.h"
#include <cmath>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <random>

// Set a parameter by ID
static void setParam(MultiQ& plugin, const juce::String& paramID, float value)
{
    if (auto* param = plugin.parameters.getParameter(paramID))
        param->setValueNotifyingHost(plugin.parameters.getParameterRange(paramID).convertTo0to1(value));
}

// Reset plugin state and run warmup blocks
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

// Fill buffer with white noise
static void fillWithNoise(juce::AudioBuffer<float>& buf, std::mt19937& rng)
{
    std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        float* data = buf.getWritePointer(ch);
        for (int i = 0; i < buf.getNumSamples(); ++i)
            data[i] = dist(rng);
    }
}

// Run a benchmark scenario and print results
static void runBenchmark(MultiQ& plugin, const char* name, double sampleRate, int blockSize,
                         double durationSeconds)
{
    int totalSamples = static_cast<int>(sampleRate * durationSeconds);
    int numBlocks = totalSamples / blockSize;
    juce::AudioBuffer<float> block(2, blockSize);
    juce::MidiBuffer midi;
    std::mt19937 rng(42);

    // Warmup: process a few blocks to stabilize
    for (int i = 0; i < 20; ++i)
    {
        fillWithNoise(block, rng);
        plugin.processBlock(block, midi);
    }

    // Timed run
    auto start = std::chrono::high_resolution_clock::now();

    for (int b = 0; b < numBlocks; ++b)
    {
        fillWithNoise(block, rng);
        plugin.processBlock(block, midi);
    }

    auto end = std::chrono::high_resolution_clock::now();
    double elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();

    double actualAudioSeconds = static_cast<double>(numBlocks * blockSize) / sampleRate;
    double cpuPercent = (elapsedMs / 1000.0) / actualAudioSeconds * 100.0;

    std::cout << std::fixed << std::setprecision(1);
    std::cout << "  [" << name << "] Processed " << actualAudioSeconds << "s of audio in "
              << elapsedMs << " ms (" << cpuPercent << "% CPU at "
              << static_cast<int>(sampleRate) << " Hz)\n";
}

// ===== MAIN =====

class TestApp : public juce::JUCEApplicationBase
{
public:
    const juce::String getApplicationName() override { return "MultiQBenchmark"; }
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

        std::cout << "Multi-Q CPU Benchmark\n";
        std::cout << "=====================\n";
        std::cout << "Plugin: " << plugin->getTotalNumInputChannels() << " in, "
                  << plugin->getTotalNumOutputChannels() << " out\n\n";

        double sr = 44100.0;
        int blockSize = 512;
        double duration = 10.0;  // 10 seconds of audio

        // --- Scenario 1: Minimal (flat, no processing) ---
        std::cout << "Scenario 1: Flat (no EQ, baseline)\n";
        resetPlugin(*plugin, sr, blockSize);
        runBenchmark(*plugin, "Flat", sr, blockSize, duration);

        // --- Scenario 2: All 8 bands with varied gains/Q ---
        std::cout << "\nScenario 2: All 8 bands active\n";
        resetPlugin(*plugin, sr, blockSize);
        for (int i = 1; i <= 8; ++i)
        {
            setParam(*plugin, ParamIDs::bandEnabled(i), 1.0f);
            setParam(*plugin, ParamIDs::bandFreq(i), 50.0f * std::pow(2.0f, static_cast<float>(i)));
            setParam(*plugin, ParamIDs::bandGain(i), (i % 2 == 0) ? 4.0f : -3.0f);
            setParam(*plugin, ParamIDs::bandQ(i), 0.5f + static_cast<float>(i) * 0.3f);
        }
        runBenchmark(*plugin, "8 bands", sr, blockSize, duration);

        // --- Scenario 3: 8 bands + dynamics on 4 bands ---
        std::cout << "\nScenario 3: 8 bands + dynamics on bands 2-5\n";
        resetPlugin(*plugin, sr, blockSize);
        for (int i = 1; i <= 8; ++i)
        {
            setParam(*plugin, ParamIDs::bandEnabled(i), 1.0f);
            setParam(*plugin, ParamIDs::bandFreq(i), 50.0f * std::pow(2.0f, static_cast<float>(i)));
            setParam(*plugin, ParamIDs::bandGain(i), (i % 2 == 0) ? 4.0f : -3.0f);
            setParam(*plugin, ParamIDs::bandQ(i), 1.0f);
        }
        for (int i = 2; i <= 5; ++i)
        {
            setParam(*plugin, ParamIDs::bandDynEnabled(i), 1.0f);
            setParam(*plugin, ParamIDs::bandDynThreshold(i), -20.0f);
            setParam(*plugin, ParamIDs::bandDynRatio(i), 4.0f);
            setParam(*plugin, ParamIDs::bandDynAttack(i), 10.0f);
            setParam(*plugin, ParamIDs::bandDynRelease(i), 100.0f);
        }
        runBenchmark(*plugin, "8 bands + 4 dyn", sr, blockSize, duration);

        // --- Scenario 4: Full load + analyzer ---
        std::cout << "\nScenario 4: 8 bands + dynamics + analyzer\n";
        resetPlugin(*plugin, sr, blockSize);
        for (int i = 1; i <= 8; ++i)
        {
            setParam(*plugin, ParamIDs::bandEnabled(i), 1.0f);
            setParam(*plugin, ParamIDs::bandFreq(i), 50.0f * std::pow(2.0f, static_cast<float>(i)));
            setParam(*plugin, ParamIDs::bandGain(i), (i % 2 == 0) ? 4.0f : -3.0f);
            setParam(*plugin, ParamIDs::bandQ(i), 1.0f);
        }
        for (int i = 2; i <= 5; ++i)
        {
            setParam(*plugin, ParamIDs::bandDynEnabled(i), 1.0f);
            setParam(*plugin, ParamIDs::bandDynThreshold(i), -20.0f);
            setParam(*plugin, ParamIDs::bandDynRatio(i), 4.0f);
        }
        setParam(*plugin, ParamIDs::analyzerEnabled, 1.0f);
        runBenchmark(*plugin, "Full + analyzer", sr, blockSize, duration);

        // --- Scenario 5: 2x oversampling ---
        std::cout << "\nScenario 5: 8 bands + 2x oversampling\n";
        resetPlugin(*plugin, sr, blockSize);
        for (int i = 1; i <= 8; ++i)
        {
            setParam(*plugin, ParamIDs::bandEnabled(i), 1.0f);
            setParam(*plugin, ParamIDs::bandFreq(i), 50.0f * std::pow(2.0f, static_cast<float>(i)));
            setParam(*plugin, ParamIDs::bandGain(i), (i % 2 == 0) ? 4.0f : -3.0f);
            setParam(*plugin, ParamIDs::bandQ(i), 1.0f);
        }
        setParam(*plugin, ParamIDs::hqEnabled, 1.0f);  // 2x oversampling
        // Process a few blocks so the OS mode change takes effect
        {
            juce::AudioBuffer<float> warmup(2, blockSize);
            juce::MidiBuffer midi;
            for (int w = 0; w < 20; ++w)
            {
                warmup.clear();
                plugin->processBlock(warmup, midi);
            }
        }
        runBenchmark(*plugin, "8 bands + 2x OS", sr, blockSize, duration);

        // --- Scenario 6: 4x oversampling ---
        std::cout << "\nScenario 6: 8 bands + 4x oversampling\n";
        resetPlugin(*plugin, sr, blockSize);
        for (int i = 1; i <= 8; ++i)
        {
            setParam(*plugin, ParamIDs::bandEnabled(i), 1.0f);
            setParam(*plugin, ParamIDs::bandFreq(i), 50.0f * std::pow(2.0f, static_cast<float>(i)));
            setParam(*plugin, ParamIDs::bandGain(i), (i % 2 == 0) ? 4.0f : -3.0f);
            setParam(*plugin, ParamIDs::bandQ(i), 1.0f);
        }
        setParam(*plugin, ParamIDs::hqEnabled, 2.0f);  // 4x oversampling
        {
            juce::AudioBuffer<float> warmup(2, blockSize);
            juce::MidiBuffer midi;
            for (int w = 0; w < 20; ++w)
            {
                warmup.clear();
                plugin->processBlock(warmup, midi);
            }
        }
        runBenchmark(*plugin, "8 bands + 4x OS", sr, blockSize, duration);

        std::cout << "\n=============================\n";
        std::cout << "Benchmark complete (informational, no pass/fail)\n";

        setApplicationReturnValue(0);  // Always pass - benchmark is informational
        quit();
    }
};

START_JUCE_APPLICATION(TestApp)
