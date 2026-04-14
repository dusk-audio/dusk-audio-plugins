/**
 * Mid/Side Per-Band Routing Test
 *
 * Verifies that per-band channel routing correctly applies EQ to only the
 * mid or side component of a stereo signal. Tests:
 *
 * 1. Mid routing: boost applied to mid (L+R), side (L-R) unchanged
 * 2. Side routing: boost applied to side (L-R), mid (L+R) unchanged
 * 3. Stereo routing: boost applied to both channels equally
 * 4. Left/Right routing: boost applied to only one channel
 * 5. Global routing follows global processing mode
 * 6. Parameter state roundtrip (save/restore routing)
 */

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
                  << ": " << std::fixed << std::setprecision(2) << actual
                  << " dB (expected " << expected << " dB, tol " << tolerance << ")\n";
        ++passed;
    } else {
        std::cout << "\033[31m[FAIL]\033[0m " << name
                  << ": " << std::fixed << std::setprecision(2) << actual
                  << " dB (expected " << expected << " dB, tol " << tolerance << ")\n";
        ++failed;
    }
}

// Generate a mono (center-panned) sine: L=R=sin — 100% mid, 0% side
static juce::AudioBuffer<float> generateMonoSine(float freq, double sampleRate, int numSamples)
{
    juce::AudioBuffer<float> buf(2, numSamples);
    float phase = 0.0f;
    float inc = static_cast<float>(2.0 * juce::MathConstants<double>::pi * freq / sampleRate);
    for (int i = 0; i < numSamples; ++i)
    {
        float val = std::sin(phase);
        buf.setSample(0, i, val);   // L
        buf.setSample(1, i, val);   // R (same = pure mid)
        phase += inc;
        if (phase > juce::MathConstants<float>::twoPi)
            phase -= juce::MathConstants<float>::twoPi;
    }
    return buf;
}

// Generate a pure side sine: L=+sin, R=-sin — 0% mid, 100% side
static juce::AudioBuffer<float> generateSideSine(float freq, double sampleRate, int numSamples)
{
    juce::AudioBuffer<float> buf(2, numSamples);
    float phase = 0.0f;
    float inc = static_cast<float>(2.0 * juce::MathConstants<double>::pi * freq / sampleRate);
    for (int i = 0; i < numSamples; ++i)
    {
        float val = std::sin(phase);
        buf.setSample(0, i,  val);  // L
        buf.setSample(1, i, -val);  // R (inverted = pure side)
        phase += inc;
        if (phase > juce::MathConstants<float>::twoPi)
            phase -= juce::MathConstants<float>::twoPi;
    }
    return buf;
}

// Generate L-only sine: L=sin, R=0 — has both mid and side content
static juce::AudioBuffer<float> generateLeftSine(float freq, double sampleRate, int numSamples)
{
    juce::AudioBuffer<float> buf(2, numSamples);
    buf.clear();
    float phase = 0.0f;
    float inc = static_cast<float>(2.0 * juce::MathConstants<double>::pi * freq / sampleRate);
    for (int i = 0; i < numSamples; ++i)
    {
        buf.setSample(0, i, std::sin(phase));
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

// Reset plugin state and settle
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
        // Reset all routing to Global (index 0)
        setParam(plugin, ParamIDs::bandChannelRouting(i), 0.0f);
    }
    setParam(plugin, ParamIDs::masterGain, 0.0f);
    setParam(plugin, ParamIDs::bypass, 0.0f);
    setParam(plugin, ParamIDs::hqEnabled, 0.0f);
    setParam(plugin, ParamIDs::autoGainEnabled, 0.0f);
    setParam(plugin, ParamIDs::limiterEnabled, 0.0f);
    setParam(plugin, ParamIDs::processingMode, 0.0f);  // Global: Stereo

    // Warmup
    juce::AudioBuffer<float> warmup(2, blockSize);
    juce::MidiBuffer midi;
    for (int i = 0; i < 10; ++i)
    {
        warmup.clear();
        plugin.processBlock(warmup, midi);
    }
}

// Settle filters by processing low-level signal
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

// ===== TEST: Mid Routing — boost only affects mid content =====
static void testMidRouting(MultiQ& plugin)
{
    std::cout << "\n--- Test: Mid Routing (boost only mid) ---\n";

    double sr = 44100.0;
    int numSamples = static_cast<int>(sr * 2);
    int skip = static_cast<int>(sr);

    // Baseline: flat response (routing=Stereo, no boost)
    resetPlugin(plugin);
    settleParams(plugin);
    auto bufFlat = generateMonoSine(1000.0f, sr, numSamples);
    processWithPlugin(plugin, bufFlat);
    float flatMidRMS = measureRMSdB(bufFlat, 0, skip);

    // Mid routing + boost on pure mid signal (L=R): should see the boost
    resetPlugin(plugin);
    setParam(plugin, ParamIDs::bandFreq(4), 1000.0f);
    setParam(plugin, ParamIDs::bandGain(4), 6.0f);
    setParam(plugin, ParamIDs::bandQ(4), 1.0f);
    setParam(plugin, ParamIDs::bandChannelRouting(4), 4.0f);  // "Mid" (index 4)
    settleParams(plugin);

    auto bufMidBoost = generateMonoSine(1000.0f, sr, numSamples);
    processWithPlugin(plugin, bufMidBoost);
    float midBoostRMS_L = measureRMSdB(bufMidBoost, 0, skip);
    float midBoostRMS_R = measureRMSdB(bufMidBoost, 1, skip);

    float boostAmount = midBoostRMS_L - flatMidRMS;
    checkDb("Mid route + mono signal: L boosted", boostAmount, 6.0f, 1.5f);
    checkDb("Mid route + mono signal: L==R (symmetric)", midBoostRMS_L - midBoostRMS_R, 0.0f, 0.5f);

    // Mid routing + boost on pure side signal (L=-R): should see NO boost
    resetPlugin(plugin);
    setParam(plugin, ParamIDs::bandFreq(4), 1000.0f);
    setParam(plugin, ParamIDs::bandGain(4), 6.0f);
    setParam(plugin, ParamIDs::bandQ(4), 1.0f);
    setParam(plugin, ParamIDs::bandChannelRouting(4), 4.0f);  // "Mid"
    settleParams(plugin);

    auto bufSideOnly = generateSideSine(1000.0f, sr, numSamples);
    float sideRefRMS = measureRMSdB(bufSideOnly, 0, 0);

    auto bufSideThruMid = generateSideSine(1000.0f, sr, numSamples);
    processWithPlugin(plugin, bufSideThruMid);
    float sideMidRMS = measureRMSdB(bufSideThruMid, 0, skip);

    // Side signal should pass through unchanged when only mid is boosted
    checkDb("Mid route + side signal: no boost", sideMidRMS - sideRefRMS, 0.0f, 1.0f);
}

// ===== TEST: Side Routing — boost only affects side content =====
static void testSideRouting(MultiQ& plugin)
{
    std::cout << "\n--- Test: Side Routing (boost only side) ---\n";

    double sr = 44100.0;
    int numSamples = static_cast<int>(sr * 2);
    int skip = static_cast<int>(sr);

    // Side routing + boost on pure side signal (L=-R): should see the boost
    resetPlugin(plugin);
    settleParams(plugin);
    auto bufSideFlat = generateSideSine(1000.0f, sr, numSamples);
    processWithPlugin(plugin, bufSideFlat);
    float flatSideRMS = measureRMSdB(bufSideFlat, 0, skip);

    resetPlugin(plugin);
    setParam(plugin, ParamIDs::bandFreq(4), 1000.0f);
    setParam(plugin, ParamIDs::bandGain(4), 6.0f);
    setParam(plugin, ParamIDs::bandQ(4), 1.0f);
    setParam(plugin, ParamIDs::bandChannelRouting(4), 5.0f);  // "Side" (index 5)
    settleParams(plugin);

    auto bufSideBoost = generateSideSine(1000.0f, sr, numSamples);
    processWithPlugin(plugin, bufSideBoost);
    float sideBoostRMS = measureRMSdB(bufSideBoost, 0, skip);

    float boostAmount = sideBoostRMS - flatSideRMS;
    checkDb("Side route + side signal: L boosted", boostAmount, 6.0f, 1.5f);

    // Side routing + boost on pure mid signal (L=R): should see NO boost
    resetPlugin(plugin);
    settleParams(plugin);
    auto bufMidFlat = generateMonoSine(1000.0f, sr, numSamples);
    processWithPlugin(plugin, bufMidFlat);
    float flatMidRMS = measureRMSdB(bufMidFlat, 0, skip);

    resetPlugin(plugin);
    setParam(plugin, ParamIDs::bandFreq(4), 1000.0f);
    setParam(plugin, ParamIDs::bandGain(4), 6.0f);
    setParam(plugin, ParamIDs::bandQ(4), 1.0f);
    setParam(plugin, ParamIDs::bandChannelRouting(4), 5.0f);  // "Side"
    settleParams(plugin);

    auto bufMidThruSide = generateMonoSine(1000.0f, sr, numSamples);
    processWithPlugin(plugin, bufMidThruSide);
    float midSideRMS = measureRMSdB(bufMidThruSide, 0, skip);

    checkDb("Side route + mono signal: no boost", midSideRMS - flatMidRMS, 0.0f, 1.0f);
}

// ===== TEST: Left/Right Routing =====
static void testLeftRightRouting(MultiQ& plugin)
{
    std::cout << "\n--- Test: Left/Right Routing ---\n";

    double sr = 44100.0;
    int numSamples = static_cast<int>(sr * 2);
    int skip = static_cast<int>(sr);

    // Baseline flat
    resetPlugin(plugin);
    settleParams(plugin);
    auto bufFlat = generateMonoSine(1000.0f, sr, numSamples);
    processWithPlugin(plugin, bufFlat);
    float flatRMS = measureRMSdB(bufFlat, 0, skip);

    // Left-only routing with boost: L should be boosted, R unchanged
    resetPlugin(plugin);
    setParam(plugin, ParamIDs::bandFreq(4), 1000.0f);
    setParam(plugin, ParamIDs::bandGain(4), 6.0f);
    setParam(plugin, ParamIDs::bandQ(4), 1.0f);
    setParam(plugin, ParamIDs::bandChannelRouting(4), 2.0f);  // "Left" (index 2)
    settleParams(plugin);

    auto bufLeft = generateMonoSine(1000.0f, sr, numSamples);
    processWithPlugin(plugin, bufLeft);
    float leftRMS_L = measureRMSdB(bufLeft, 0, skip);
    float leftRMS_R = measureRMSdB(bufLeft, 1, skip);

    checkDb("Left route: L boosted", leftRMS_L - flatRMS, 6.0f, 1.5f);
    checkDb("Left route: R unchanged", leftRMS_R - flatRMS, 0.0f, 1.0f);

    // Right-only routing with boost: R should be boosted, L unchanged
    resetPlugin(plugin);
    setParam(plugin, ParamIDs::bandFreq(4), 1000.0f);
    setParam(plugin, ParamIDs::bandGain(4), 6.0f);
    setParam(plugin, ParamIDs::bandQ(4), 1.0f);
    setParam(plugin, ParamIDs::bandChannelRouting(4), 3.0f);  // "Right" (index 3)
    settleParams(plugin);

    auto bufRight = generateMonoSine(1000.0f, sr, numSamples);
    processWithPlugin(plugin, bufRight);
    float rightRMS_L = measureRMSdB(bufRight, 0, skip);
    float rightRMS_R = measureRMSdB(bufRight, 1, skip);

    checkDb("Right route: L unchanged", rightRMS_L - flatRMS, 0.0f, 1.0f);
    checkDb("Right route: R boosted", rightRMS_R - flatRMS, 6.0f, 1.5f);
}

// ===== TEST: Stereo routing = both channels boosted equally =====
static void testStereoRouting(MultiQ& plugin)
{
    std::cout << "\n--- Test: Stereo Routing (both channels) ---\n";

    double sr = 44100.0;
    int numSamples = static_cast<int>(sr * 2);
    int skip = static_cast<int>(sr);

    resetPlugin(plugin);
    settleParams(plugin);
    auto bufFlat = generateMonoSine(1000.0f, sr, numSamples);
    processWithPlugin(plugin, bufFlat);
    float flatRMS = measureRMSdB(bufFlat, 0, skip);

    // Explicit Stereo routing (index 1)
    resetPlugin(plugin);
    setParam(plugin, ParamIDs::bandFreq(4), 1000.0f);
    setParam(plugin, ParamIDs::bandGain(4), 6.0f);
    setParam(plugin, ParamIDs::bandQ(4), 1.0f);
    setParam(plugin, ParamIDs::bandChannelRouting(4), 1.0f);  // "Stereo" (index 1)
    settleParams(plugin);

    auto buf = generateMonoSine(1000.0f, sr, numSamples);
    processWithPlugin(plugin, buf);
    float stereoRMS_L = measureRMSdB(buf, 0, skip);
    float stereoRMS_R = measureRMSdB(buf, 1, skip);

    checkDb("Stereo route: L boosted", stereoRMS_L - flatRMS, 6.0f, 1.5f);
    checkDb("Stereo route: R boosted", stereoRMS_R - flatRMS, 6.0f, 1.5f);
    checkDb("Stereo route: L==R", stereoRMS_L - stereoRMS_R, 0.0f, 0.5f);
}

// ===== TEST: Global routing follows global processing mode =====
static void testGlobalRouting(MultiQ& plugin)
{
    std::cout << "\n--- Test: Global Routing (follows global mode) ---\n";

    double sr = 44100.0;
    int numSamples = static_cast<int>(sr * 2);
    int skip = static_cast<int>(sr);

    // Set global processing mode to Mid (index 3), per-band routing to Global (index 0)
    resetPlugin(plugin);
    setParam(plugin, ParamIDs::processingMode, 3.0f);  // Global: Mid
    setParam(plugin, ParamIDs::bandFreq(4), 1000.0f);
    setParam(plugin, ParamIDs::bandGain(4), 6.0f);
    setParam(plugin, ParamIDs::bandQ(4), 1.0f);
    setParam(plugin, ParamIDs::bandChannelRouting(4), 0.0f);  // "Global" — should follow global Mid
    settleParams(plugin);

    // Pure side signal should NOT be boosted (global is Mid)
    auto bufSide = generateSideSine(1000.0f, sr, numSamples);
    float sideRefRMS = measureRMSdB(bufSide, 0, 0);
    processWithPlugin(plugin, bufSide);
    float sideOutRMS = measureRMSdB(bufSide, 0, skip);

    checkDb("Global=Mid + side signal: no boost", sideOutRMS - sideRefRMS, 0.0f, 1.0f);

    // Pure mid signal SHOULD be boosted (global is Mid)
    resetPlugin(plugin);
    setParam(plugin, ParamIDs::processingMode, 3.0f);
    setParam(plugin, ParamIDs::bandFreq(4), 1000.0f);
    setParam(plugin, ParamIDs::bandGain(4), 6.0f);
    setParam(plugin, ParamIDs::bandQ(4), 1.0f);
    setParam(plugin, ParamIDs::bandChannelRouting(4), 0.0f);
    settleParams(plugin);

    // Flat reference
    resetPlugin(plugin);
    setParam(plugin, ParamIDs::processingMode, 3.0f);
    settleParams(plugin);
    auto bufMidFlat = generateMonoSine(1000.0f, sr, numSamples);
    processWithPlugin(plugin, bufMidFlat);
    float midFlatRMS = measureRMSdB(bufMidFlat, 0, skip);

    resetPlugin(plugin);
    setParam(plugin, ParamIDs::processingMode, 3.0f);
    setParam(plugin, ParamIDs::bandFreq(4), 1000.0f);
    setParam(plugin, ParamIDs::bandGain(4), 6.0f);
    setParam(plugin, ParamIDs::bandQ(4), 1.0f);
    setParam(plugin, ParamIDs::bandChannelRouting(4), 0.0f);
    settleParams(plugin);

    auto bufMid = generateMonoSine(1000.0f, sr, numSamples);
    processWithPlugin(plugin, bufMid);
    float midOutRMS = measureRMSdB(bufMid, 0, skip);

    checkDb("Global=Mid + mono signal: boosted", midOutRMS - midFlatRMS, 6.0f, 1.5f);
}

// ===== TEST: Routing parameter state roundtrip =====
static void testRoutingStateRoundtrip(MultiQ& plugin)
{
    std::cout << "\n--- Test: Routing State Roundtrip ---\n";

    resetPlugin(plugin);

    // Set routing for each band to different values
    setParam(plugin, ParamIDs::bandChannelRouting(1), 0.0f);  // Global
    setParam(plugin, ParamIDs::bandChannelRouting(2), 1.0f);  // Stereo
    setParam(plugin, ParamIDs::bandChannelRouting(3), 2.0f);  // Left
    setParam(plugin, ParamIDs::bandChannelRouting(4), 3.0f);  // Right
    setParam(plugin, ParamIDs::bandChannelRouting(5), 4.0f);  // Mid
    setParam(plugin, ParamIDs::bandChannelRouting(6), 5.0f);  // Side
    setParam(plugin, ParamIDs::bandChannelRouting(7), 4.0f);  // Mid
    setParam(plugin, ParamIDs::bandChannelRouting(8), 5.0f);  // Side

    // Save state
    juce::MemoryBlock state;
    plugin.getStateInformation(state);

    // Reset all routing to Global
    for (int i = 1; i <= 8; ++i)
        setParam(plugin, ParamIDs::bandChannelRouting(i), 0.0f);

    // Restore state
    plugin.setStateInformation(state.getData(), static_cast<int>(state.getSize()));

    // Verify restored values
    float expected[] = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 4.0f, 5.0f};
    const char* names[] = {"Global", "Stereo", "Left", "Right", "Mid", "Side", "Mid", "Side"};
    bool allOk = true;

    for (int i = 0; i < 8; ++i)
    {
        auto* raw = plugin.parameters.getRawParameterValue(ParamIDs::bandChannelRouting(i + 1));
        float val = raw ? raw->load() : -1.0f;
        bool ok = std::abs(val - expected[i]) < 0.01f;
        if (!ok)
        {
            std::cout << "\033[31m[FAIL]\033[0m Band " << (i + 1) << " routing: expected "
                      << names[i] << " (" << expected[i] << "), got " << val << "\n";
            ++failed;
            allOk = false;
        }
    }
    if (allOk)
        check("All 8 band routings restored correctly", true);

    // Verify resetToInit() clears all routing back to Global (0)
    plugin.resetToInit();
    bool initOk = true;
    for (int i = 0; i < 8; ++i)
    {
        auto* raw = plugin.parameters.getRawParameterValue(ParamIDs::bandChannelRouting(i + 1));
        float val = raw ? raw->load() : -1.0f;
        if (std::abs(val - 0.0f) >= 0.01f)
        {
            std::cout << "\033[31m[FAIL]\033[0m Band " << (i + 1)
                      << " routing after resetToInit: expected Global (0), got " << val << "\n";
            ++failed;
            initOk = false;
        }
    }
    if (initOk)
        check("resetToInit() clears all band routings to Global", true);
}

// ===== MAIN =====

class TestApp : public juce::JUCEApplicationBase
{
public:
    const juce::String getApplicationName() override { return "MultiQMSRoutingTest"; }
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
        std::cout << "========================================\n";
        std::cout << "  Multi-Q: M/S Per-Band Routing Tests\n";
        std::cout << "========================================\n";

        auto plugin = std::make_unique<MultiQ>();

        auto layouts = plugin->getBusesLayout();
        layouts.getMainInputChannelSet() = juce::AudioChannelSet::stereo();
        layouts.getMainOutputChannelSet() = juce::AudioChannelSet::stereo();
        plugin->setBusesLayout(layouts);
        plugin->setPlayConfigDetails(2, 2, 44100.0, 512);
        plugin->prepareToPlay(44100.0, 512);

        std::cout << "Plugin: " << plugin->getTotalNumInputChannels() << " in, "
                  << plugin->getTotalNumOutputChannels() << " out, "
                  << plugin->getSampleRate() << " Hz\n";

        testMidRouting(*plugin);
        testSideRouting(*plugin);
        testLeftRightRouting(*plugin);
        testStereoRouting(*plugin);
        testGlobalRouting(*plugin);
        testRoutingStateRoundtrip(*plugin);

        std::cout << "\n========================================\n";
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
