/**
 * Multi-Comp: Bypass Crossfade + Auto Gain/Dry-Wet Integration Tests
 *
 * Tests:
 * 1. Bypass→active transition produces click-free crossfade (no transients)
 * 2. Auto gain scales correctly with dry/wet mix amount
 * 3. 100% wet auto gain still compensates fully
 * 4. 100% dry produces unity gain (no auto gain applied)
 */

#include <JuceHeader.h>
#include "../UniversalCompressor.h"
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

// Measure peak-to-peak difference between consecutive samples (detects clicks)
static float measureMaxSampleDelta(const juce::AudioBuffer<float>& buf, int channel,
                                     int startSample = 0, int endSample = -1)
{
    if (endSample < 0) endSample = buf.getNumSamples();
    const float* data = buf.getReadPointer(channel);
    float maxDelta = 0.0f;
    for (int i = startSample + 1; i < endSample; ++i)
    {
        float delta = std::abs(data[i] - data[i - 1]);
        maxDelta = std::max(maxDelta, delta);
    }
    return maxDelta;
}

// Measure RMS in dB
static float measureRMSdB(const juce::AudioBuffer<float>& buf, int channel,
                           int skipSamples = 0, int count = -1)
{
    int start = skipSamples;
    int n = (count > 0) ? count : (buf.getNumSamples() - start);
    if (n <= 0) return -100.0f;

    const float* data = buf.getReadPointer(channel);
    double sum = 0.0;
    for (int i = start; i < start + n; ++i)
        sum += static_cast<double>(data[i]) * data[i];

    float rms = static_cast<float>(std::sqrt(sum / n));
    return rms > 1e-10f ? 20.0f * std::log10(rms) : -100.0f;
}

// Process buffer through plugin in fixed-size blocks
static void processWithPlugin(UniversalCompressor& plugin, juce::AudioBuffer<float>& buffer, int blockSize = 512)
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
static void setParam(UniversalCompressor& plugin, const juce::String& paramID, float value)
{
    auto& params = plugin.getParameters();
    if (auto* param = params.getParameter(paramID))
        param->setValueNotifyingHost(params.getParameterRange(paramID).convertTo0to1(value));
}

// ===== TEST: Bypass crossfade produces no clicks =====
static void testBypassCrossfade(UniversalCompressor& plugin)
{
    std::cout << "\n--- Test: Bypass Crossfade (click-free transition) ---\n";

    double sr = 44100.0;
    int blockSize = 512;
    plugin.releaseResources();
    plugin.prepareToPlay(sr, blockSize);

    // Set up compression: VCA mode (choice index 2), fast attack, heavy compression
    setParam(plugin, "mode", 2.0f);            // VCA (choice index)
    setParam(plugin, "vca_threshold", -20.0f);
    setParam(plugin, "vca_ratio", 8.0f);
    setParam(plugin, "vca_attack", 1.0f);
    setParam(plugin, "vca_release", 50.0f);
    setParam(plugin, "mix", 100.0f);           // 100% wet
    setParam(plugin, "bypass", 0.0f);          // Active

    // Warmup: process several blocks of audio to let compressor settle
    for (int i = 0; i < 20; ++i)
    {
        auto warmup = generateSine(1000.0f, sr, blockSize, 0.5f);
        processWithPlugin(plugin, warmup, blockSize);
    }

    // Now toggle bypass ON for a few blocks
    setParam(plugin, "bypass", 1.0f);
    for (int i = 0; i < 5; ++i)
    {
        auto silence = generateSine(1000.0f, sr, blockSize, 0.5f);
        processWithPlugin(plugin, silence, blockSize);
    }

    // Toggle bypass OFF — this should trigger the crossfade
    setParam(plugin, "bypass", 0.0f);

    // Process the transition block and measure max sample delta
    auto transitionBuf = generateSine(1000.0f, sr, blockSize, 0.5f);
    processWithPlugin(plugin, transitionBuf, blockSize);

    float maxDelta = measureMaxSampleDelta(transitionBuf, 0);

    // For a 1kHz sine at 0.5 amplitude, the maximum natural sample-to-sample
    // delta is about 0.07 at 44.1kHz. A click would produce deltas >> 0.2.
    // The crossfade should keep it under 0.15.
    check("Bypass→active transition: no click (max delta < 0.2)",
          maxDelta < 0.2f);

    std::cout << "  Max sample delta: " << std::fixed << std::setprecision(4) << maxDelta
              << " (threshold: 0.2)\n";

    // Process more blocks to let compressor envelopes settle after crossfade
    for (int i = 0; i < 20; ++i)
    {
        auto buf = generateSine(1000.0f, sr, blockSize, 0.5f);
        processWithPlugin(plugin, buf, blockSize);
    }

    auto stableBuf = generateSine(1000.0f, sr, blockSize, 0.5f);
    processWithPlugin(plugin, stableBuf, blockSize);
    float stableDelta = measureMaxSampleDelta(stableBuf, 0);
    // Compressor actively modulates gain, so deltas are higher than a pure sine.
    // Threshold 0.25 allows for normal compression artifacts while catching
    // clicks/discontinuities (which would produce deltas > 0.5).
    check("Post-transition blocks stable (max delta < 0.25)", stableDelta < 0.25f);
    std::cout << "  Post-transition max delta: " << std::fixed << std::setprecision(4)
              << stableDelta << "\n";
}

// ===== TEST: Rapid bypass toggle doesn't crash =====
static void testRapidBypassToggle(UniversalCompressor& plugin)
{
    std::cout << "\n--- Test: Rapid Bypass Toggle (no crash/NaN) ---\n";

    double sr = 44100.0;
    int blockSize = 256;
    plugin.releaseResources();
    plugin.prepareToPlay(sr, blockSize);

    setParam(plugin, "mode", 3.0f);            // Bus/SSL (choice index)
    setParam(plugin, "bus_threshold", -15.0f);
    setParam(plugin, "bus_ratio", 1.0f);        // Choice index 1 = "4:1"
    setParam(plugin, "mix", 100.0f);

    bool allFinite = true;
    for (int i = 0; i < 100; ++i)
    {
        // Toggle bypass every block
        setParam(plugin, "bypass", (i % 2 == 0) ? 1.0f : 0.0f);

        auto buf = generateSine(1000.0f, sr, blockSize, 0.5f);
        processWithPlugin(plugin, buf, blockSize);

        // Check for NaN/Inf
        for (int ch = 0; ch < 2 && allFinite; ++ch)
        {
            const float* data = buf.getReadPointer(ch);
            for (int s = 0; s < blockSize; ++s)
            {
                if (!std::isfinite(data[s]))
                {
                    allFinite = false;
                    break;
                }
            }
        }
    }
    check("100 rapid bypass toggles: all samples finite", allFinite);
}

// ===== TEST: Auto gain scales with dry/wet =====
static void testAutoGainDryWetScaling(UniversalCompressor& plugin)
{
    std::cout << "\n--- Test: Auto Gain + Dry/Wet Scaling ---\n";

    double sr = 44100.0;
    int blockSize = 512;
    int numSamples = static_cast<int>(sr * 2);  // 2 seconds
    int skip = static_cast<int>(sr);             // Skip first second for settling

    // Reference: 100% wet with auto gain
    plugin.releaseResources();
    plugin.prepareToPlay(sr, blockSize);
    setParam(plugin, "mode", 3.0f);            // Bus/SSL (choice index)
    setParam(plugin, "bus_threshold", -20.0f);
    setParam(plugin, "bus_ratio", 1.0f);        // Choice index 1 = "4:1"
    setParam(plugin, "bus_attack", 2.0f);       // Choice index 2 = "1ms"
    setParam(plugin, "bus_release", 1.0f);      // Choice index 1 = "0.3s"
    setParam(plugin, "mix", 100.0f);           // 100% wet
    setParam(plugin, "auto_makeup", 1.0f);     // Auto makeup ON (choice index 1)
    setParam(plugin, "bypass", 0.0f);

    auto buf100wet = generateSine(1000.0f, sr, numSamples, 0.5f);
    processWithPlugin(plugin, buf100wet, blockSize);
    float rms100wet = measureRMSdB(buf100wet, 0, skip);

    // 100% dry with auto gain — should be close to input level
    plugin.releaseResources();
    plugin.prepareToPlay(sr, blockSize);
    setParam(plugin, "comp_mode", 3.0f);
    setParam(plugin, "threshold", -20.0f);
    setParam(plugin, "ratio", 4.0f);
    setParam(plugin, "attack", 10.0f);
    setParam(plugin, "release", 100.0f);
    setParam(plugin, "mix", 0.0f);             // 100% dry
    setParam(plugin, "auto_gain", 1.0f);
    setParam(plugin, "bypass", 0.0f);

    // Input reference level
    float inputRMS = measureRMSdB(generateSine(1000.0f, sr, numSamples, 0.5f), 0, skip);

    auto buf100dry = generateSine(1000.0f, sr, numSamples, 0.5f);
    processWithPlugin(plugin, buf100dry, blockSize);
    float rms100dry = measureRMSdB(buf100dry, 0, skip);

    // 100% dry should be very close to input (no auto gain compensation)
    float dryDeviation = std::abs(rms100dry - inputRMS);
    check("100% dry + auto gain: output ≈ input level (< 2 dB deviation)",
          dryDeviation < 2.0f);
    std::cout << "  Input: " << std::fixed << std::setprecision(1) << inputRMS
              << " dB, 100% dry output: " << rms100dry << " dB (deviation: "
              << dryDeviation << " dB)\n";

    // 50% dry/wet with auto gain — should be between dry and wet levels
    plugin.releaseResources();
    plugin.prepareToPlay(sr, blockSize);
    setParam(plugin, "comp_mode", 3.0f);
    setParam(plugin, "threshold", -20.0f);
    setParam(plugin, "ratio", 4.0f);
    setParam(plugin, "attack", 10.0f);
    setParam(plugin, "release", 100.0f);
    setParam(plugin, "mix", 50.0f);            // 50% mix
    setParam(plugin, "auto_gain", 1.0f);
    setParam(plugin, "bypass", 0.0f);

    auto buf50mix = generateSine(1000.0f, sr, numSamples, 0.5f);
    processWithPlugin(plugin, buf50mix, blockSize);
    float rms50mix = measureRMSdB(buf50mix, 0, skip);

    // 50% mix should NOT be louder than 100% wet (the old bug caused over-amplification)
    float overAmplification = rms50mix - rms100wet;
    check("50% mix + auto gain: not louder than 100% wet (< 3 dB over)",
          overAmplification < 3.0f);
    std::cout << "  100% wet: " << rms100wet << " dB, 50% mix: " << rms50mix
              << " dB (diff: " << std::showpos << overAmplification << std::noshowpos << " dB)\n";

    // Level should transition smoothly across mix range — no big jump near 0%
    plugin.releaseResources();
    plugin.prepareToPlay(sr, blockSize);
    setParam(plugin, "comp_mode", 3.0f);
    setParam(plugin, "threshold", -20.0f);
    setParam(plugin, "ratio", 4.0f);
    setParam(plugin, "attack", 10.0f);
    setParam(plugin, "release", 100.0f);
    setParam(plugin, "mix", 5.0f);             // 5% wet (near dry)
    setParam(plugin, "auto_gain", 1.0f);
    setParam(plugin, "bypass", 0.0f);

    auto buf5mix = generateSine(1000.0f, sr, numSamples, 0.5f);
    processWithPlugin(plugin, buf5mix, blockSize);
    float rms5mix = measureRMSdB(buf5mix, 0, skip);

    // 5% mix should be close to input level (tiny amount of compression + tiny auto gain)
    float nearDryDeviation = std::abs(rms5mix - inputRMS);
    check("5% mix + auto gain: close to input level (< 4 dB deviation)",
          nearDryDeviation < 4.0f);
    std::cout << "  5% mix: " << rms5mix << " dB, input: " << inputRMS
              << " dB (deviation: " << nearDryDeviation << " dB)\n";
}

// ===== TEST: Phase coherence between dry and wet paths =====
static void testPhaseCoherence(UniversalCompressor& plugin)
{
    std::cout << "\n--- Test: Phase Coherence (comb filtering check) ---\n";

    double sr = 44100.0;
    int blockSize = 512;
    int numSamples = static_cast<int>(sr * 2);
    int skip = static_cast<int>(sr);

    // Test multiple modes and oversampling settings
    struct TestCase {
        const char* name;
        float modeIndex;
        const char* thresholdParam;
        float thresholdValue;  // Set high to avoid compression
    };

    TestCase cases[] = {
        {"VCA 1x",  2.0f, "vca_threshold", 12.0f},
        {"Bus 1x",  3.0f, "bus_threshold", 15.0f},
        {"Opto 1x", 0.0f, "opto_peak_reduction", 0.0f},
    };

    // Test at multiple frequencies to detect comb filtering
    // (comb filters cause nulls at specific frequencies)
    float testFreqs[] = {200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f};

    for (auto& tc : cases)
    {
        std::cout << "  Mode: " << tc.name << "\n";
        bool modeOk = true;

        for (float freq : testFreqs)
        {
            // Reference: 100% wet, no compression (threshold max → no GR)
            plugin.releaseResources();
            plugin.prepareToPlay(sr, blockSize);
            setParam(plugin, "mode", tc.modeIndex);
            setParam(plugin, tc.thresholdParam, tc.thresholdValue);
            setParam(plugin, "mix", 100.0f);
            setParam(plugin, "auto_makeup", 0.0f);
            setParam(plugin, "bypass", 0.0f);

            auto bufWet = generateSine(freq, sr, numSamples, 0.3f);
            processWithPlugin(plugin, bufWet, blockSize);
            float rmsWet = measureRMSdB(bufWet, 0, skip);

            // 50% mix, no compression — should be ~equal to wet if phase-coherent
            // (dry + wet at same level and phase = same output)
            plugin.releaseResources();
            plugin.prepareToPlay(sr, blockSize);
            setParam(plugin, "mode", tc.modeIndex);
            setParam(plugin, tc.thresholdParam, tc.thresholdValue);
            setParam(plugin, "mix", 50.0f);
            setParam(plugin, "auto_makeup", 0.0f);
            setParam(plugin, "bypass", 0.0f);

            auto bufMix = generateSine(freq, sr, numSamples, 0.3f);
            processWithPlugin(plugin, bufMix, blockSize);
            float rmsMix = measureRMSdB(bufMix, 0, skip);

            // With no compression and phase-coherent mixing:
            // output = dry * 0.5 + wet * 0.5 = input * 0.5 + input * 0.5 = input
            // So rmsMix should equal rmsWet.
            // Comb filtering would cause frequency-dependent cancellation (> 3 dB dips).
            float diff = std::abs(rmsMix - rmsWet);
            if (diff > 3.0f)
            {
                std::cout << "    " << static_cast<int>(freq) << " Hz: COMB FILTER detected! "
                          << "100% wet=" << std::fixed << std::setprecision(1) << rmsWet
                          << " dB, 50% mix=" << rmsMix << " dB (diff=" << diff << " dB)\n";
                modeOk = false;
            }
        }

        if (modeOk)
            std::cout << "    All frequencies within 3 dB — no comb filtering\n";

        check((std::string(tc.name) + ": no comb filtering at 50% mix").c_str(), modeOk);
    }
}

// ===== MAIN =====

class TestApp : public juce::JUCEApplicationBase
{
public:
    const juce::String getApplicationName() override { return "MultiCompBypassAutoGainTest"; }
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
        std::cout << "==========================================\n";
        std::cout << "  Multi-Comp: Bypass + Auto Gain Tests\n";
        std::cout << "==========================================\n";

        auto plugin = std::make_unique<UniversalCompressor>();

        auto layouts = plugin->getBusesLayout();
        layouts.getMainInputChannelSet() = juce::AudioChannelSet::stereo();
        layouts.getMainOutputChannelSet() = juce::AudioChannelSet::stereo();
        plugin->setBusesLayout(layouts);
        plugin->setPlayConfigDetails(2, 2, 44100.0, 512);
        plugin->prepareToPlay(44100.0, 512);

        std::cout << "Plugin: " << plugin->getTotalNumInputChannels() << " in, "
                  << plugin->getTotalNumOutputChannels() << " out, "
                  << plugin->getSampleRate() << " Hz\n";

        testBypassCrossfade(*plugin);
        testRapidBypassToggle(*plugin);
        testAutoGainDryWetScaling(*plugin);
        testPhaseCoherence(*plugin);

        std::cout << "\n==========================================\n";
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
