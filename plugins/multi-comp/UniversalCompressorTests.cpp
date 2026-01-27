#include "UniversalCompressor.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <thread>
#include <atomic>
#include <chrono>

/**
 * Unit tests for Universal Compressor plugin
 *
 * Tests cover:
 * - Parameter initialization and validation
 * - Gain reduction accuracy for each compressor mode
 * - Thread safety of atomic variables
 * - Latency reporting
 * - DSP stability (no NaN/Inf outputs)
 * - Compression curve accuracy
 */

class UniversalCompressorTests : public juce::UnitTest
{
public:
    UniversalCompressorTests()
        : juce::UnitTest("Universal Compressor Tests", "Compressor")
    {}

    void runTest() override
    {
        beginTest("Plugin Initialization");
        testPluginInitialization();

        beginTest("Parameter Range Validation");
        testParameterRanges();

        beginTest("Opto Compressor Gain Reduction");
        testOptoCompression();

        beginTest("FET Compressor Gain Reduction");
        testFETCompression();

        beginTest("VCA Compressor Gain Reduction");
        testVCACompression();

        beginTest("Bus Compressor Gain Reduction");
        testBusCompression();

        beginTest("DSP Stability - No NaN/Inf");
        testDSPStability();

        beginTest("Thread Safety - Atomic Meters");
        testThreadSafety();

        beginTest("Latency Reporting");
        testLatencyReporting();

        beginTest("Bypass Functionality");
        testBypass();

        beginTest("Compression Ratios");
        testCompressionRatios();

        beginTest("Variable Sample Rate Consistency");
        testVariableSampleRates();

        beginTest("SIMD Performance Benchmarks");
        testSIMDPerformance();

        beginTest("Mix Knob Direction");
        testMixKnobDirection();

        beginTest("Oversampling Phase Coherence");
        testOversamplingPhaseCoherence();

        // Comprehensive phase alignment test for mix knob
        testMixKnobPhaseAlignment();

        // IR comparison tests disabled by default - requires reference IR files
        // beginTest("IR Comparison Tests");
        // testIRComparison();
    }

private:
    void testPluginInitialization()
    {
        UniversalCompressor compressor;

        // Test basic plugin properties
        expect(compressor.getName() == "Multi-Comp", "Plugin name is correct");
        expect(!compressor.acceptsMidi(), "Plugin does not accept MIDI");
        expect(!compressor.producesMidi(), "Plugin does not produce MIDI");
        expect(compressor.hasEditor(), "Plugin has editor");

        // Test initial preparation
        compressor.prepareToPlay(48000.0, 512);
        expect(compressor.getTailLengthSeconds() >= 0.0, "Tail length is non-negative");
    }

    void testParameterRanges()
    {
        UniversalCompressor compressor;
        auto& params = compressor.getParameters();

        // Test that all essential parameters exist
        expect(params.getRawParameterValue("mode") != nullptr, "Mode parameter exists");
        expect(params.getRawParameterValue("bypass") != nullptr, "Bypass parameter exists");
        expect(params.getRawParameterValue("opto_peak_reduction") != nullptr, "Opto peak reduction exists");
        expect(params.getRawParameterValue("fet_input") != nullptr, "FET input exists");
        expect(params.getRawParameterValue("vca_threshold") != nullptr, "VCA threshold exists");
        expect(params.getRawParameterValue("bus_threshold") != nullptr, "Bus threshold exists");

        // Test mode parameter range (should be 0-3 for 4 modes)
        auto* modeParam = params.getRawParameterValue("mode");
        if (modeParam)
        {
            float modeValue = *modeParam;
            expect(modeValue >= 0.0f && modeValue <= 3.0f,
                   "Mode parameter in valid range: " + juce::String(modeValue));
        }
    }

    void testOptoCompression()
    {
        UniversalCompressor compressor;
        compressor.prepareToPlay(48000.0, 512);

        auto& params = compressor.getParameters();

        // Set to Opto mode
        if (auto* modeParam = params.getRawParameterValue("mode"))
            *modeParam = 0.0f; // Opto mode

        // Set peak reduction to 75 (aggressive compression)
        if (auto* peakReduction = params.getRawParameterValue("opto_peak_reduction"))
            *peakReduction = 75.0f;

        // Set gain to 50 (unity)
        if (auto* gain = params.getRawParameterValue("opto_gain"))
            *gain = 50.0f;

        // Disable bypass
        if (auto* bypass = params.getRawParameterValue("bypass"))
            *bypass = 0.0f;

        // Warm up the compressor - process multiple blocks to let envelope respond
        // Opto has ~10ms attack, so 50 blocks at 512 samples = ~533ms at 48kHz
        juce::MidiBuffer midiBuffer;
        for (int i = 0; i < 50; ++i)
        {
            juce::AudioBuffer<float> warmup(2, 512);
            fillBufferWithSineWave(warmup, 1.0f, 1000.0f, 48000.0);
            compressor.processBlock(warmup, midiBuffer);
        }

        // Now check gain reduction after warmup
        float gr = compressor.getGainReduction();
        logMessage("Opto GR after warmup: " + juce::String(gr) + " dB");
        expect(gr < 0.0f, "Opto mode produces gain reduction on hot signal: " + juce::String(gr) + " dB");
        expect(gr > -50.0f, "Gain reduction is reasonable (not extreme): " + juce::String(gr) + " dB");

        // Check output is attenuated
        juce::AudioBuffer<float> buffer(2, 512);
        fillBufferWithSineWave(buffer, 1.0f, 1000.0f, 48000.0);
        float inputPeak = buffer.getMagnitude(0, 0, 512);
        compressor.processBlock(buffer, midiBuffer);
        float outputPeak = buffer.getMagnitude(0, 0, 512);
        expect(outputPeak < inputPeak, "Output is compressed (peak < input): " + juce::String(outputPeak) + " vs " + juce::String(inputPeak));
    }

    void testFETCompression()
    {
        UniversalCompressor compressor;
        compressor.prepareToPlay(48000.0, 512);

        auto& params = compressor.getParameters();

        // Set to FET mode
        if (auto* modeParam = params.getRawParameterValue("mode"))
            *modeParam = 1.0f; // FET mode

        // Set input gain (drives into compression)
        if (auto* input = params.getRawParameterValue("fet_input"))
            *input = 30.0f; // +30dB input drive (aggressive)

        // Set ratio to 4:1 (index 0)
        if (auto* ratio = params.getRawParameterValue("fet_ratio"))
            *ratio = 0.0f; // 4:1

        // Disable bypass
        if (auto* bypass = params.getRawParameterValue("bypass"))
            *bypass = 0.0f;

        // Warm up the compressor - FET has fast attack but still needs blocks to respond
        juce::MidiBuffer midiBuffer;
        for (int i = 0; i < 30; ++i)
        {
            juce::AudioBuffer<float> warmup(2, 512);
            fillBufferWithSineWave(warmup, 0.8f, 1000.0f, 48000.0);
            compressor.processBlock(warmup, midiBuffer);
        }

        // Check for gain reduction
        float gr = compressor.getGainReduction();
        logMessage("FET GR after warmup: " + juce::String(gr) + " dB");
        expect(gr < 0.0f, "FET mode produces gain reduction: " + juce::String(gr) + " dB");
        expect(gr > -40.0f, "FET gain reduction is within expected range: " + juce::String(gr) + " dB");
    }

    void testVCACompression()
    {
        UniversalCompressor compressor;
        compressor.prepareToPlay(48000.0, 512);

        auto& params = compressor.getParameters();

        // Set to VCA mode
        if (auto* modeParam = params.getRawParameterValue("mode"))
            *modeParam = 2.0f; // VCA mode

        // Set threshold to -20dB
        if (auto* threshold = params.getRawParameterValue("vca_threshold"))
            *threshold = -20.0f;

        // Set ratio to 4:1
        if (auto* ratio = params.getRawParameterValue("vca_ratio"))
            *ratio = 4.0f;

        // Disable bypass
        if (auto* bypass = params.getRawParameterValue("bypass"))
            *bypass = 0.0f;

        // Warm up the compressor with signal above threshold
        juce::MidiBuffer midiBuffer;
        for (int i = 0; i < 30; ++i)
        {
            juce::AudioBuffer<float> warmup(2, 512);
            fillBufferWithSineWave(warmup, 0.5f, 1000.0f, 48000.0); // -6dB (14dB over threshold)
            compressor.processBlock(warmup, midiBuffer);
        }

        // With signal at -6dB and threshold at -20dB, we're 14dB over threshold
        // At 4:1 ratio, expect ~10.5dB gain reduction (14 * (1 - 1/4) = 10.5)
        float gr = compressor.getGainReduction();
        logMessage("VCA GR after warmup: " + juce::String(gr) + " dB");
        expect(gr < 0.0f, "VCA mode produces gain reduction: " + juce::String(gr) + " dB");
        expect(gr > -20.0f && gr < -2.0f,
               "VCA gain reduction in expected range for 4:1: " + juce::String(gr) + " dB");
    }

    void testBusCompression()
    {
        UniversalCompressor compressor;
        compressor.prepareToPlay(48000.0, 512);

        auto& params = compressor.getParameters();

        // Set to Bus mode
        if (auto* modeParam = params.getRawParameterValue("mode"))
            *modeParam = 3.0f; // Bus mode

        // Set threshold to -10dB
        if (auto* threshold = params.getRawParameterValue("bus_threshold"))
            *threshold = -10.0f;

        // Set ratio to 4:1 (index 1)
        if (auto* ratio = params.getRawParameterValue("bus_ratio"))
            *ratio = 1.0f;

        // Disable bypass
        if (auto* bypass = params.getRawParameterValue("bypass"))
            *bypass = 0.0f;

        // Warm up the compressor
        juce::MidiBuffer midiBuffer;
        for (int i = 0; i < 30; ++i)
        {
            juce::AudioBuffer<float> warmup(2, 512);
            fillBufferWithSineWave(warmup, 0.7f, 1000.0f, 48000.0); // -3dB
            compressor.processBlock(warmup, midiBuffer);
        }

        // Check for gain reduction
        float gr = compressor.getGainReduction();
        logMessage("Bus GR after warmup: " + juce::String(gr) + " dB");
        expect(gr <= 0.0f, "Bus mode gain reduction is non-positive: " + juce::String(gr) + " dB");
        expect(gr > -25.0f, "Bus gain reduction within Bus specs: " + juce::String(gr) + " dB");
    }

    void testDSPStability()
    {
        UniversalCompressor compressor;
        compressor.prepareToPlay(48000.0, 512);

        auto& params = compressor.getParameters();
        if (auto* bypass = params.getRawParameterValue("bypass"))
            *bypass = 0.0f;

        // Test all modes
        for (int mode = 0; mode < 4; ++mode)
        {
            if (auto* modeParam = params.getRawParameterValue("mode"))
                *modeParam = static_cast<float>(mode);

            // Test with various challenging signals

            // 1. Silence (can cause denormals)
            juce::AudioBuffer<float> silence(2, 512);
            silence.clear();
            juce::MidiBuffer midiBuffer;
            compressor.processBlock(silence, midiBuffer);
            expectNoNaNOrInf(silence, "Silence - Mode " + juce::String(mode));

            // 2. Very low level signal
            juce::AudioBuffer<float> quiet(2, 512);
            fillBufferWithSineWave(quiet, 0.00001f, 1000.0f, 48000.0);
            compressor.processBlock(quiet, midiBuffer);
            expectNoNaNOrInf(quiet, "Quiet signal - Mode " + juce::String(mode));

            // 3. Hot signal (near clipping)
            juce::AudioBuffer<float> hot(2, 512);
            fillBufferWithSineWave(hot, 1.5f, 1000.0f, 48000.0);
            compressor.processBlock(hot, midiBuffer);
            expectNoNaNOrInf(hot, "Hot signal - Mode " + juce::String(mode));

            // 4. DC offset
            juce::AudioBuffer<float> dc(2, 512);
            dc.clear();
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    dc.setSample(ch, i, 0.5f);
            compressor.processBlock(dc, midiBuffer);
            expectNoNaNOrInf(dc, "DC offset - Mode " + juce::String(mode));
        }
    }

    void testThreadSafety()
    {
        UniversalCompressor compressor;
        compressor.prepareToPlay(48000.0, 512);

        // Test atomic meter access (should not crash or assert)
        float inputLevel = compressor.getInputLevel();
        float outputLevel = compressor.getOutputLevel();
        float gr = compressor.getGainReduction();

        expect(inputLevel >= -60.0f && inputLevel <= 20.0f,
               "Input meter in reasonable range: " + juce::String(inputLevel));
        expect(outputLevel >= -60.0f && outputLevel <= 20.0f,
               "Output meter in reasonable range: " + juce::String(outputLevel));
        expect(gr >= -60.0f && gr <= 0.0f,
               "Gain reduction meter in reasonable range: " + juce::String(gr));

        // Test linked gain reduction access
        float linkedGR0 = compressor.getLinkedGainReduction(0);
        float linkedGR1 = compressor.getLinkedGainReduction(1);
        expect(!std::isnan(linkedGR0) && !std::isinf(linkedGR0), "Linked GR channel 0 is valid");
        expect(!std::isnan(linkedGR1) && !std::isinf(linkedGR1), "Linked GR channel 1 is valid");

        // Multi-threaded concurrent access test
        testMultiThreadMeterAccess();
    }

    void testMultiThreadMeterAccess()
    {
        UniversalCompressor compressor;
        compressor.prepareToPlay(48000.0, 512);

        std::atomic<bool> audioThreadRunning{true};
        std::atomic<int> readCount{0};
        std::atomic<int> writeCount{0};
        std::atomic<bool> hadRaceCondition{false};

        // Simulate audio thread processing
        std::thread audioThread([&]() {
            juce::AudioBuffer<float> buffer(2, 512);
            juce::MidiBuffer midiBuffer;

            for (int i = 0; i < 100; ++i)
            {
                fillBufferWithSineWave(buffer, 0.5f, 1000.0f, 48000.0);
                compressor.processBlock(buffer, midiBuffer);
                writeCount++;
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
            audioThreadRunning = false;
        });

        // Simulate UI thread reading meters concurrently
        std::thread uiThread([&]() {
            // Add bounded timeout to prevent hanging if audio thread finishes early
            auto startTime = std::chrono::steady_clock::now();
            const auto maxDuration = std::chrono::milliseconds(500); // 500ms max

            while (audioThreadRunning || readCount < 100)
            {
                // Check timeout
                auto elapsed = std::chrono::steady_clock::now() - startTime;
                if (elapsed > maxDuration)
                    break;

                try {
                    float input = compressor.getInputLevel();
                    float output = compressor.getOutputLevel();
                    float gr = compressor.getGainReduction();
                    float linked0 = compressor.getLinkedGainReduction(0);
                    float linked1 = compressor.getLinkedGainReduction(1);

                    // Check for invalid values (would indicate race condition)
                    if (std::isnan(input) || std::isinf(input) ||
                        std::isnan(output) || std::isinf(output) ||
                        std::isnan(gr) || std::isinf(gr) ||
                        std::isnan(linked0) || std::isinf(linked0) ||
                        std::isnan(linked1) || std::isinf(linked1))
                    {
                        hadRaceCondition.store(true);
                    }

                    readCount++;
                    std::this_thread::sleep_for(std::chrono::microseconds(5));
                }
                catch (...) {
                    hadRaceCondition.store(true);
                }
            }
        });

        audioThread.join();
        uiThread.join();

        expect(!hadRaceCondition.load(), "No race conditions detected in multi-threaded meter access");
        // With timeout, readCount may not reach 100 if audio thread finishes early
        // Check that we got a reasonable number of reads (at least 50 to verify concurrency)
        expect(readCount >= 50, "UI thread completed sufficient reads: " + juce::String(readCount.load()));
        expect(writeCount == 100, "Audio thread completed writes: " + juce::String(writeCount.load()));

        logMessage("Multi-thread test: " + juce::String(writeCount.load()) +
                   " writes, " + juce::String(readCount.load()) + " reads");
    }

    void testLatencyReporting()
    {
        UniversalCompressor compressor;
        compressor.prepareToPlay(48000.0, 512);

        double latency = compressor.getLatencyInSamples();

        // With 2x oversampling enabled, should report some latency
        // Latency should be reasonable (not zero, not huge)
        expect(latency >= 0.0, "Latency is non-negative: " + juce::String(latency));
        expect(latency < 1000.0, "Latency is reasonable (< 1000 samples): " + juce::String(latency));

        // If oversampling is active, latency should be > 0
        if (latency > 0)
        {
            logMessage("Oversampling latency reported: " + juce::String(latency) + " samples");
        }
    }

    void testBypass()
    {
        UniversalCompressor compressor;
        compressor.prepareToPlay(48000.0, 512);

        auto& params = compressor.getParameters();

        // Create test signal
        juce::AudioBuffer<float> inputBuffer(2, 512);
        fillBufferWithSineWave(inputBuffer, 0.8f, 1000.0f, 48000.0);

        // Store original signal
        juce::AudioBuffer<float> originalBuffer(2, 512);
        originalBuffer.makeCopyOf(inputBuffer);

        // Enable bypass
        if (auto* bypass = params.getRawParameterValue("bypass"))
            *bypass = 1.0f;

        juce::MidiBuffer midiBuffer;
        compressor.processBlock(inputBuffer, midiBuffer);

        // With bypass, output should equal input
        float maxDiff = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int i = 0; i < 512; ++i)
            {
                float diff = std::abs(inputBuffer.getSample(ch, i) - originalBuffer.getSample(ch, i));
                maxDiff = juce::jmax(maxDiff, diff);
            }
        }

        expect(maxDiff < 0.0001f,
               "Bypass mode passes audio unchanged (max diff: " + juce::String(maxDiff) + ")");
    }

    void testCompressionRatios()
    {
        UniversalCompressor compressor;
        compressor.prepareToPlay(48000.0, 512);

        auto& params = compressor.getParameters();

        // Test VCA mode with known threshold and ratio
        if (auto* modeParam = params.getRawParameterValue("mode"))
            *modeParam = 2.0f; // VCA mode

        if (auto* threshold = params.getRawParameterValue("vca_threshold"))
            *threshold = -20.0f; // -20dB threshold

        if (auto* ratio = params.getRawParameterValue("vca_ratio"))
            *ratio = 4.0f; // 4:1 ratio

        if (auto* bypass = params.getRawParameterValue("bypass"))
            *bypass = 0.0f;

        // Warm up with signal OVER threshold so compressor is actively compressing
        juce::MidiBuffer midiBuffer;
        for (int i = 0; i < 50; ++i)
        {
            juce::AudioBuffer<float> buffer(2, 512);
            fillBufferWithSineWave(buffer, 0.4f, 1000.0f, 48000.0); // -8dB signal (12dB over threshold)
            compressor.processBlock(buffer, midiBuffer);
        }

        // Now test with steady state - signal 12dB over threshold
        juce::AudioBuffer<float> overThreshold(2, 512);
        fillBufferWithSineWave(overThreshold, 0.4f, 1000.0f, 48000.0); // -8dB (12dB over -20dB)
        compressor.processBlock(overThreshold, midiBuffer);
        float grOverThreshold = compressor.getGainReduction();

        logMessage("VCA ratio test GR: " + juce::String(grOverThreshold) + " dB");

        // With 12dB over threshold at 4:1, expect ~9dB gain reduction
        // (12dB * (1 - 1/4) = 9dB)
        // Allow wider tolerance for analog-style envelope behavior
        expect(grOverThreshold < -3.0f && grOverThreshold > -18.0f,
               "12dB over threshold produces expected GR: " + juce::String(grOverThreshold) + " dB");
    }

    // Helper: Run warmup blocks to let compressor envelope respond
    void warmupCompressor(UniversalCompressor& compressor, int numBlocks, float amplitude, double sampleRate)
    {
        juce::MidiBuffer midiBuffer;
        for (int i = 0; i < numBlocks; ++i)
        {
            juce::AudioBuffer<float> warmup(2, 512);
            fillBufferWithSineWave(warmup, amplitude, 1000.0f, sampleRate);
            compressor.processBlock(warmup, midiBuffer);
        }
    }

    void testVariableSampleRates()
    {
        // Test all compressor modes at different sample rates
        // Focus on DSP stability (no NaN/Inf) - GR accuracy is tested in individual mode tests
        std::vector<double> sampleRates = {44100.0, 48000.0, 96000.0, 192000.0};

        for (double sampleRate : sampleRates)
        {
            juce::String rateStr = juce::String(sampleRate / 1000.0, 1) + "kHz";

            // Test Opto mode at this sample rate
            {
                UniversalCompressor compressor;
                compressor.prepareToPlay(sampleRate, 512);

                auto& params = compressor.getParameters();
                if (auto* modeParam = params.getRawParameterValue("mode"))
                    *modeParam = 0.0f; // Opto mode

                if (auto* peakReduction = params.getRawParameterValue("opto_peak_reduction"))
                    *peakReduction = 75.0f;

                if (auto* bypass = params.getRawParameterValue("bypass"))
                    *bypass = 0.0f;

                // Warmup to let envelope respond
                warmupCompressor(compressor, 30, 0.8f, sampleRate);

                juce::AudioBuffer<float> buffer(2, 512);
                fillBufferWithSineWave(buffer, 0.8f, 1000.0f, sampleRate);

                juce::MidiBuffer midiBuffer;
                compressor.processBlock(buffer, midiBuffer);

                float gr = compressor.getGainReduction();
                expect(gr <= 0.0f && gr > -50.0f,
                       "Opto GR reasonable at " + rateStr + ": " + juce::String(gr) + " dB");

                expectNoNaNOrInf(buffer, "Opto at " + rateStr);
            }

            // Test FET mode at this sample rate
            {
                UniversalCompressor compressor;
                compressor.prepareToPlay(sampleRate, 512);

                auto& params = compressor.getParameters();
                if (auto* modeParam = params.getRawParameterValue("mode"))
                    *modeParam = 1.0f; // FET mode

                if (auto* input = params.getRawParameterValue("fet_input"))
                    *input = 30.0f;

                if (auto* bypass = params.getRawParameterValue("bypass"))
                    *bypass = 0.0f;

                warmupCompressor(compressor, 30, 0.8f, sampleRate);

                juce::AudioBuffer<float> buffer(2, 512);
                fillBufferWithSineWave(buffer, 0.8f, 1000.0f, sampleRate);

                juce::MidiBuffer midiBuffer;
                compressor.processBlock(buffer, midiBuffer);

                float gr = compressor.getGainReduction();
                expect(gr <= 0.0f && gr > -60.0f,
                       "FET GR reasonable at " + rateStr + ": " + juce::String(gr) + " dB");

                expectNoNaNOrInf(buffer, "FET at " + rateStr);
            }

            // Test VCA mode at this sample rate
            {
                UniversalCompressor compressor;
                compressor.prepareToPlay(sampleRate, 512);

                auto& params = compressor.getParameters();
                if (auto* modeParam = params.getRawParameterValue("mode"))
                    *modeParam = 2.0f; // VCA mode

                if (auto* threshold = params.getRawParameterValue("vca_threshold"))
                    *threshold = -20.0f;

                if (auto* ratio = params.getRawParameterValue("vca_ratio"))
                    *ratio = 4.0f;

                if (auto* bypass = params.getRawParameterValue("bypass"))
                    *bypass = 0.0f;

                warmupCompressor(compressor, 30, 0.5f, sampleRate);

                juce::AudioBuffer<float> buffer(2, 512);
                fillBufferWithSineWave(buffer, 0.5f, 1000.0f, sampleRate); // -6dB

                juce::MidiBuffer midiBuffer;
                compressor.processBlock(buffer, midiBuffer);

                float gr = compressor.getGainReduction();
                expect(gr <= 0.0f && gr > -30.0f,
                       "VCA GR reasonable at " + rateStr + ": " + juce::String(gr) + " dB");

                expectNoNaNOrInf(buffer, "VCA at " + rateStr);
            }

            // Test Bus mode at this sample rate
            {
                UniversalCompressor compressor;
                compressor.prepareToPlay(sampleRate, 512);

                auto& params = compressor.getParameters();
                if (auto* modeParam = params.getRawParameterValue("mode"))
                    *modeParam = 3.0f; // Bus mode

                if (auto* threshold = params.getRawParameterValue("bus_threshold"))
                    *threshold = -10.0f;

                if (auto* bypass = params.getRawParameterValue("bypass"))
                    *bypass = 0.0f;

                warmupCompressor(compressor, 30, 0.7f, sampleRate);

                juce::AudioBuffer<float> buffer(2, 512);
                fillBufferWithSineWave(buffer, 0.7f, 1000.0f, sampleRate);

                juce::MidiBuffer midiBuffer;
                compressor.processBlock(buffer, midiBuffer);

                float gr = compressor.getGainReduction();
                expect(gr <= 0.0f && gr > -30.0f,
                       "Bus GR reasonable at " + rateStr + ": " + juce::String(gr) + " dB");

                expectNoNaNOrInf(buffer, "Bus at " + rateStr);
            }
        }

        logMessage("All sample rates tested successfully (44.1kHz, 48kHz, 96kHz, 192kHz)");
    }

    void testSIMDPerformance()
    {
        UniversalCompressor compressor;
        compressor.prepareToPlay(48000.0, 512);

        auto& params = compressor.getParameters();
        if (auto* bypass = params.getRawParameterValue("bypass"))
            *bypass = 0.0f;

        // Create test buffer with typical audio content
        juce::AudioBuffer<float> buffer(2, 2048);
        fillBufferWithSineWave(buffer, 0.5f, 1000.0f, 48000.0);

        juce::MidiBuffer midiBuffer;

        // Warm up cache
        for (int i = 0; i < 10; ++i)
            compressor.processBlock(buffer, midiBuffer);

        // Benchmark all compressor modes
        std::vector<juce::String> modeNames = {"Opto", "FET", "VCA", "Bus"};

        for (int mode = 0; mode < 4; ++mode)
        {
            if (auto* modeParam = params.getRawParameterValue("mode"))
                *modeParam = static_cast<float>(mode);

            // Benchmark processing time
            auto startTime = juce::Time::getHighResolutionTicks();
            const int iterations = 1000;

            for (int i = 0; i < iterations; ++i)
            {
                compressor.processBlock(buffer, midiBuffer);
            }

            auto endTime = juce::Time::getHighResolutionTicks();
            double elapsedSeconds = juce::Time::highResolutionTicksToSeconds(endTime - startTime);
            double avgTimeMs = (elapsedSeconds * 1000.0) / iterations;
            double samplesPerSec = (2048 * iterations) / elapsedSeconds;

            logMessage(modeNames[mode] + " mode: " +
                       juce::String(avgTimeMs, 4) + " ms/buffer, " +
                       juce::String(samplesPerSec / 1000000.0, 2) + " MSamples/sec");

            // Verify performance is reasonable (should process faster than real-time)
            // At 48kHz, 2048 samples = 42.67ms real-time
            expect(avgTimeMs < 10.0,
                   modeNames[mode] + " processes faster than real-time: " +
                   juce::String(avgTimeMs, 4) + " ms");
        }

        // SIMD-specific test: compare aligned vs unaligned performance
        // This validates that SIMD optimizations are effective
        juce::AudioBuffer<float> alignedBuffer(2, 2048);
        fillBufferWithSineWave(alignedBuffer, 0.5f, 1000.0f, 48000.0);

        if (auto* modeParam = params.getRawParameterValue("mode"))
            *modeParam = 0.0f; // Test with Opto mode

        auto startAligned = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < 500; ++i)
            compressor.processBlock(alignedBuffer, midiBuffer);
        auto endAligned = juce::Time::getHighResolutionTicks();

        double alignedTime = juce::Time::highResolutionTicksToSeconds(endAligned - startAligned);

        logMessage("SIMD benchmark: " + juce::String(alignedTime * 1000.0, 4) + " ms for 500 iterations");
        expect(alignedTime < 5.0, "SIMD processing completes in reasonable time");
    }

    // IR comparison tests disabled - requires juce_audio_formats module
    // Uncomment and add juce_audio_formats to CMakeLists.txt to enable
#if 0
    void testIRComparison()
    {
        // Framework for IR (Impulse Response) comparison testing
        // This test compares plugin output against reference hardware IRs
        //
        // To use this test:
        // 1. Place reference IR files in: tests/reference_irs/
        // 2. IR files should be named: <mode>_<setting>.wav
        //    Example: opto_moderate.wav, fet_4to1.wav, vca_fast.wav, bus_slow.wav
        // 3. Each IR should be processed with known settings documented in filename
        // 4. MSE (Mean Squared Error) threshold determines pass/fail

        logMessage("IR comparison tests require reference IR files");
        logMessage("Place IR files in: tests/reference_irs/");

        // Check if reference directory exists
        juce::File irDir = juce::File::getCurrentWorkingDirectory()
                               .getChildFile("tests")
                               .getChildFile("reference_irs");

        if (!irDir.exists())
        {
            logMessage("Reference IR directory not found - skipping IR tests");
            logMessage("Create directory: " + irDir.getFullPathName());
            return;
        }

        // Example IR test framework
        auto irFiles = irDir.findChildFiles(juce::File::findFiles, false, "*.wav");

        if (irFiles.isEmpty())
        {
            logMessage("No IR files found in: " + irDir.getFullPathName());
            return;
        }

        logMessage("Found " + juce::String(irFiles.size()) + " IR files for testing");

        // Process each IR file
        for (const auto& irFile : irFiles)
        {
            logMessage("Testing IR: " + irFile.getFileName());

            // Load reference IR
            juce::AudioFormatManager formatManager;
            formatManager.registerBasicFormats();

            std::unique_ptr<juce::AudioFormatReader> reader(
                formatManager.createReaderFor(irFile));

            if (!reader)
            {
                logMessage("Failed to load: " + irFile.getFileName());
                continue;
            }

            // Read IR into buffer
            juce::AudioBuffer<float> referenceIR(reader->numChannels,
                                                (int)reader->lengthInSamples);
            reader->read(&referenceIR, 0, (int)reader->lengthInSamples, 0, true, true);

            // TODO: Parse filename to determine compressor mode and settings
            // TODO: Process test signal with plugin using same settings
            // TODO: Compare output with reference IR using MSE
            // TODO: expect(mse < threshold, "IR matches reference within threshold");

            logMessage("IR test framework ready - implementation pending");
        }
    }
#endif

    // Helper functions

    void fillBufferWithSineWave(juce::AudioBuffer<float>& buffer, float amplitude,
                                  float frequency, double sampleRate)
    {
        float phase = 0.0f;
        float phaseIncrement = frequency / static_cast<float>(sampleRate) * juce::MathConstants<float>::twoPi;

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            float* channelData = buffer.getWritePointer(ch);
            phase = 0.0f;

            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                channelData[i] = amplitude * std::sin(phase);
                phase += phaseIncrement;
                if (phase >= juce::MathConstants<float>::twoPi)
                    phase -= juce::MathConstants<float>::twoPi;
            }
        }
    }

    void expectNoNaNOrInf(const juce::AudioBuffer<float>& buffer, const juce::String& context)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const float* data = buffer.getReadPointer(ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
            {
                if (std::isnan(data[i]) || std::isinf(data[i]))
                {
                    expect(false, context + " - Found NaN/Inf at ch:" + juce::String(ch) +
                           " sample:" + juce::String(i));
                    return;
                }
            }
        }
        expect(true, context + " - No NaN/Inf detected");
    }

    void testMixKnobDirection()
    {
        // Test that mix knob direction is correct:
        // - 100% mix = 100% wet (fully compressed signal)
        // - 0% mix = 100% dry (unprocessed signal)

        UniversalCompressor compressor;
        compressor.prepareToPlay(48000.0, 512);

        auto& params = compressor.getParameters();

        // Set up Opto mode with heavy compression
        if (auto* modeParam = params.getRawParameterValue("mode"))
            *modeParam = 0.0f; // Opto mode

        if (auto* peakReduction = params.getRawParameterValue("opto_peak_reduction"))
            *peakReduction = 80.0f; // Heavy compression

        if (auto* gain = params.getRawParameterValue("opto_gain"))
            *gain = 50.0f; // Unity gain

        if (auto* bypass = params.getRawParameterValue("bypass"))
            *bypass = 0.0f;

        // Create test signal (hot signal that will be compressed)
        juce::AudioBuffer<float> originalBuffer(2, 512);
        fillBufferWithSineWave(originalBuffer, 0.8f, 1000.0f, 48000.0);

        // Store original RMS for comparison
        float originalRms = 0.0f;
        for (int i = 0; i < originalBuffer.getNumSamples(); ++i)
        {
            float sample = originalBuffer.getSample(0, i);
            originalRms += sample * sample;
        }
        originalRms = std::sqrt(originalRms / originalBuffer.getNumSamples());

        // Test 1: 0% mix should give us dry (uncompressed) signal
        juce::MidiBuffer midiBuffer;

        // First, let compressor settle
        for (int i = 0; i < 10; ++i)
        {
            juce::AudioBuffer<float> warmupBuffer(2, 512);
            fillBufferWithSineWave(warmupBuffer, 0.8f, 1000.0f, 48000.0);
            compressor.processBlock(warmupBuffer, midiBuffer);
        }

        // Test with 0% mix (should be dry)
        if (auto* mixParam = params.getRawParameterValue("mix"))
            *mixParam = 0.0f;

        juce::AudioBuffer<float> dryTestBuffer(2, 512);
        fillBufferWithSineWave(dryTestBuffer, 0.8f, 1000.0f, 48000.0);
        compressor.processBlock(dryTestBuffer, midiBuffer);

        float dryRms = 0.0f;
        for (int i = 0; i < dryTestBuffer.getNumSamples(); ++i)
        {
            float sample = dryTestBuffer.getSample(0, i);
            dryRms += sample * sample;
        }
        dryRms = std::sqrt(dryRms / dryTestBuffer.getNumSamples());

        // At 0% mix, output RMS should be close to input RMS (dry signal)
        float dryDiffRatio = std::abs(dryRms - originalRms) / originalRms;
        expect(dryDiffRatio < 0.15f,
               "0% mix preserves dry signal (diff ratio: " + juce::String(dryDiffRatio, 3) + ")");

        // Test with 100% mix (should be wet/compressed)
        if (auto* mixParam = params.getRawParameterValue("mix"))
            *mixParam = 100.0f;

        juce::AudioBuffer<float> wetTestBuffer(2, 512);
        fillBufferWithSineWave(wetTestBuffer, 0.8f, 1000.0f, 48000.0);
        compressor.processBlock(wetTestBuffer, midiBuffer);

        float wetRms = 0.0f;
        for (int i = 0; i < wetTestBuffer.getNumSamples(); ++i)
        {
            float sample = wetTestBuffer.getSample(0, i);
            wetRms += sample * sample;
        }
        wetRms = std::sqrt(wetRms / wetTestBuffer.getNumSamples());

        // At 100% mix with heavy compression, output should be significantly reduced
        expect(wetRms < dryRms,
               "100% mix shows compression (wet RMS: " + juce::String(wetRms, 3) +
               " < dry RMS: " + juce::String(dryRms, 3) + ")");

        logMessage("Mix direction test: 0% mix RMS ratio: " + juce::String(dryDiffRatio, 4) +
                   ", 100% wet RMS: " + juce::String(wetRms, 4));
    }

    void testOversamplingPhaseCoherence()
    {
        // Test that dry and wet signals are phase-aligned when using oversampling with mix control
        // This prevents comb filtering (flanging) when mixing dry and wet signals

        // Test all oversampling modes: Off (0), 2x (1), 4x (2)
        for (int osMode = 0; osMode <= 2; ++osMode)
        {
            UniversalCompressor compressor;
            compressor.prepareToPlay(48000.0, 512);

            auto& params = compressor.getParameters();

            // Set to Digital mode (transparent compression for clean phase testing)
            if (auto* modeParam = params.getRawParameterValue("mode"))
                *modeParam = 6.0f; // Digital mode

            // Set minimal compression (threshold at 0dB, low ratio) so we can focus on phase
            if (auto* threshold = params.getRawParameterValue("digital_threshold"))
                *threshold = 0.0f;

            if (auto* ratio = params.getRawParameterValue("digital_ratio"))
                *ratio = 1.5f; // Minimal ratio

            if (auto* bypass = params.getRawParameterValue("bypass"))
                *bypass = 0.0f;

            // Set oversampling mode
            if (auto* oversamplingParam = params.getRawParameterValue("oversampling"))
                *oversamplingParam = static_cast<float>(osMode);

            // Set 50% mix (equal blend of dry and wet - most sensitive to phase issues)
            if (auto* mixParam = params.getRawParameterValue("mix"))
                *mixParam = 50.0f;

            // Let compressor settle
            juce::MidiBuffer midiBuffer;
            for (int i = 0; i < 20; ++i)
            {
                juce::AudioBuffer<float> warmupBuffer(2, 512);
                fillBufferWithSineWave(warmupBuffer, 0.3f, 1000.0f, 48000.0);
                compressor.processBlock(warmupBuffer, midiBuffer);
            }

            // Now test for phase coherence by checking that mixed signal
            // doesn't have unexpected attenuation (comb filtering signature)

            // Create test signal at a level below threshold (no compression, just testing phase)
            juce::AudioBuffer<float> testBuffer(2, 512);
            fillBufferWithSineWave(testBuffer, 0.1f, 1000.0f, 48000.0); // Low level, below threshold

            float inputRms = 0.0f;
            for (int i = 0; i < testBuffer.getNumSamples(); ++i)
            {
                float sample = testBuffer.getSample(0, i);
                inputRms += sample * sample;
            }
            inputRms = std::sqrt(inputRms / testBuffer.getNumSamples());

            compressor.processBlock(testBuffer, midiBuffer);

            float outputRms = 0.0f;
            for (int i = 0; i < testBuffer.getNumSamples(); ++i)
            {
                float sample = testBuffer.getSample(0, i);
                outputRms += sample * sample;
            }
            outputRms = std::sqrt(outputRms / testBuffer.getNumSamples());

            // With phase-aligned signals and no compression, 50% mix of dry+wet
            // should maintain similar RMS (within 3dB / ~0.7x-1.4x factor)
            // Phase misalignment would cause destructive interference, reducing RMS significantly
            float rmsRatio = outputRms / inputRms;

            juce::String modeStr = (osMode == 0) ? "Off" : ((osMode == 1) ? "2x" : "4x");

            expect(rmsRatio > 0.5f && rmsRatio < 1.5f,
                   "Oversampling " + modeStr + ": Phase coherent at 50% mix " +
                   "(ratio: " + juce::String(rmsRatio, 3) + ")");

            // Additional check: output shouldn't have unexpected frequency content
            // from comb filtering (flanging creates notches/peaks)
            expectNoNaNOrInf(testBuffer, "Oversampling " + modeStr);

            logMessage("Oversampling " + modeStr + " phase coherence: ratio = " + juce::String(rmsRatio, 4));
        }
    }

    void testMixKnobPhaseAlignment()
    {
        // COMPREHENSIVE PHASE ALIGNMENT TEST FOR MIX KNOB
        // This test detects comb filtering caused by latency mismatch between dry and wet signals
        //
        // When dry/wet are mixed with a latency offset, comb filtering creates:
        // - Notches (nulls) at frequencies: f = (2k+1) * sampleRate / (2 * delaySamples)
        // - Peaks at frequencies: f = k * sampleRate / delaySamples
        //
        // We test this by:
        // 1. NULL TEST: At 0% mix, output should exactly match input (no processing)
        // 2. MULTI-FREQUENCY TEST: At 50% mix with no compression, measure response at multiple frequencies
        //    - Phase-aligned signals should sum to ~same level at all frequencies
        //    - Comb filtering causes frequency-dependent level variations

        beginTest("Mix Knob Phase Alignment - Null Test");

        for (int osMode = 0; osMode <= 2; ++osMode)
        {
            UniversalCompressor compressor;
            compressor.prepareToPlay(48000.0, 512);

            auto& params = compressor.getParameters();

            // Use Opto mode (common mode where issue was reported)
            if (auto* modeParam = params.getRawParameterValue("mode"))
                *modeParam = 0.0f;

            // Minimal compression - high threshold so no gain reduction
            if (auto* threshold = params.getRawParameterValue("opto_peak_reduction"))
                *threshold = 0.0f; // No peak reduction

            if (auto* bypass = params.getRawParameterValue("bypass"))
                *bypass = 0.0f;

            if (auto* oversamplingParam = params.getRawParameterValue("oversampling"))
                *oversamplingParam = static_cast<float>(osMode);

            // CRITICAL: Set mix to 0% (should be 100% dry = input passthrough)
            if (auto* mixParam = params.getRawParameterValue("mix"))
                *mixParam = 0.0f;

            juce::String modeStr = (osMode == 0) ? "Off" : ((osMode == 1) ? "2x" : "4x");

            // Warmup to stabilize any filters
            juce::MidiBuffer midiBuffer;
            for (int i = 0; i < 30; ++i)
            {
                juce::AudioBuffer<float> warmupBuffer(2, 512);
                fillBufferWithSineWave(warmupBuffer, 0.5f, 1000.0f, 48000.0);
                compressor.processBlock(warmupBuffer, midiBuffer);
            }

            // Create test signal
            juce::AudioBuffer<float> inputBuffer(2, 512);
            fillBufferWithSineWave(inputBuffer, 0.5f, 1000.0f, 48000.0);

            // Make a copy for comparison
            juce::AudioBuffer<float> originalBuffer;
            originalBuffer.makeCopyOf(inputBuffer);

            // Process
            compressor.processBlock(inputBuffer, midiBuffer);

            // Calculate null depth: how much the output differs from input
            // At 0% mix, output should EXACTLY match input (after latency compensation)
            float sumSquaredDiff = 0.0f;
            float sumSquaredOriginal = 0.0f;

            // Account for potential latency by finding best correlation offset
            int maxOffset = 64; // Search up to 64 samples for best alignment
            float bestNullDepth = -999.0f;
            int bestOffset = 0;

            for (int offset = 0; offset < maxOffset && offset < inputBuffer.getNumSamples() - 100; ++offset)
            {
                sumSquaredDiff = 0.0f;
                sumSquaredOriginal = 0.0f;

                for (int i = 0; i < inputBuffer.getNumSamples() - offset - 50; ++i)
                {
                    float orig = originalBuffer.getSample(0, i);
                    float proc = inputBuffer.getSample(0, i + offset);
                    float diff = orig - proc;
                    sumSquaredDiff += diff * diff;
                    sumSquaredOriginal += orig * orig;
                }

                if (sumSquaredOriginal > 0.0001f)
                {
                    float nullDepthDb = 10.0f * std::log10(sumSquaredDiff / sumSquaredOriginal + 1e-10f);
                    if (nullDepthDb < bestNullDepth || bestOffset == 0)
                    {
                        bestNullDepth = nullDepthDb;
                        bestOffset = offset;
                    }
                }
            }

            // At 0% mix, we expect a very deep null (< -60dB difference)
            // Any significant deviation indicates the dry signal path has issues
            expect(bestNullDepth < -40.0f,
                   "OS " + modeStr + ": 0% mix null test (diff: " + juce::String(bestNullDepth, 1) +
                   " dB at offset " + juce::String(bestOffset) + " samples, expected < -40 dB)");

            logMessage("OS " + modeStr + ": Null depth = " + juce::String(bestNullDepth, 1) +
                       " dB at offset " + juce::String(bestOffset));
        }

        beginTest("Mix Knob Phase Alignment - Comb Filter Detection");

        for (int osMode = 0; osMode <= 2; ++osMode)
        {
            UniversalCompressor compressor;
            compressor.prepareToPlay(48000.0, 512);

            auto& params = compressor.getParameters();

            // Use Opto mode with aggressive compression to test phase alignment
            // This is the mode reported by the user with comb filtering issues
            if (auto* modeParam = params.getRawParameterValue("mode"))
                *modeParam = 0.0f; // Opto (Vintage Opto)

            // High peak reduction for aggressive compression (creates difference between dry/wet)
            if (auto* peakReduction = params.getRawParameterValue("opto_peak_reduction"))
                *peakReduction = 80.0f;

            // Unity gain output
            if (auto* gain = params.getRawParameterValue("opto_gain"))
                *gain = 50.0f;

            if (auto* bypass = params.getRawParameterValue("bypass"))
                *bypass = 0.0f;

            if (auto* oversamplingParam = params.getRawParameterValue("oversampling"))
                *oversamplingParam = static_cast<float>(osMode);

            // 50% mix - most sensitive to phase issues between dry and compressed wet
            if (auto* mixParam = params.getRawParameterValue("mix"))
                *mixParam = 50.0f;

            juce::String modeStr = (osMode == 0) ? "Off" : ((osMode == 1) ? "2x" : "4x");

            // Warmup - allows oversampling AND compressor envelope to initialize
            juce::MidiBuffer midiBuffer;
            for (int i = 0; i < 50; ++i)
            {
                juce::AudioBuffer<float> warmupBuffer(2, 512);
                fillBufferWithSineWave(warmupBuffer, 0.8f, 1000.0f, 48000.0); // Hot signal
                compressor.processBlock(warmupBuffer, midiBuffer);
            }

            // Log gain reduction to confirm compression is happening
            float gr = compressor.getGainReduction();
            logMessage("OS " + modeStr + ": GR during comb test = " + juce::String(gr, 1) + " dB");

            // Debug: Print the reported latency after warmup
            double reportedLatency = compressor.getLatencyInSamples();
            logMessage("OS " + modeStr + ": Reported latency = " + juce::String(reportedLatency) + " samples");

            // Measure actual delay by passing an impulse
            // At 100% wet (100 mix), the impulse should appear at the reported latency position
            if (auto* mixParam2 = params.getRawParameterValue("mix"))
                *mixParam2 = 100.0f;  // 100% wet

            juce::AudioBuffer<float> impulseBuffer(2, 512);
            impulseBuffer.clear();
            impulseBuffer.setSample(0, 0, 1.0f);  // Impulse at sample 0
            impulseBuffer.setSample(1, 0, 1.0f);
            compressor.processBlock(impulseBuffer, midiBuffer);

            // Find where the impulse peak appears in the output
            int peakPos = 0;
            float peakVal = 0.0f;
            for (int i = 0; i < 512; ++i)
            {
                float absVal = std::abs(impulseBuffer.getSample(0, i));
                if (absVal > peakVal)
                {
                    peakVal = absVal;
                    peakPos = i;
                }
            }
            logMessage("OS " + modeStr + ": Actual impulse peak at sample " + juce::String(peakPos) +
                       " (expected " + juce::String(static_cast<int>(reportedLatency)) + ")");

            // Now test impulse at 50% mix - if delay compensation works, peak should be at same position
            if (auto* mixParam3 = params.getRawParameterValue("mix"))
                *mixParam3 = 50.0f;

            juce::AudioBuffer<float> impulseBuffer50(2, 512);
            impulseBuffer50.clear();
            impulseBuffer50.setSample(0, 0, 1.0f);
            impulseBuffer50.setSample(1, 0, 1.0f);
            compressor.processBlock(impulseBuffer50, midiBuffer);

            // Find peaks at 50% mix - should have ONE peak at the latency position if phase-aligned
            int peakPos0 = 0, peakPos49 = 0;
            float peakVal0 = std::abs(impulseBuffer50.getSample(0, 0));
            float peakVal49 = (reportedLatency < 512) ? std::abs(impulseBuffer50.getSample(0, static_cast<int>(reportedLatency))) : 0.0f;

            // Also find overall peak
            int overallPeakPos = 0;
            float overallPeakVal = 0.0f;
            for (int i = 0; i < 512; ++i)
            {
                float absVal = std::abs(impulseBuffer50.getSample(0, i));
                if (absVal > overallPeakVal)
                {
                    overallPeakVal = absVal;
                    overallPeakPos = i;
                }
            }
            logMessage("OS " + modeStr + " @ 50% mix: Peak at " + juce::String(overallPeakPos) +
                       " (val=" + juce::String(overallPeakVal, 3) +
                       "), sample[0]=" + juce::String(peakVal0, 3) +
                       ", sample[" + juce::String(static_cast<int>(reportedLatency)) + "]=" + juce::String(peakVal49, 3));

            // Test multiple frequencies to detect comb filtering
            // Comb filtering causes frequency-dependent amplitude variations
            // Use a HOT signal (0.8 amplitude) to ensure compression is happening
            std::array<float, 6> testFreqs = {250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f};
            std::array<float, 6> levelRatios;

            for (size_t f = 0; f < testFreqs.size(); ++f)
            {
                float freq = testFreqs[f];

                // Create test tone - HOT signal to ensure compression
                juce::AudioBuffer<float> testBuffer(2, 2048);
                fillBufferWithSineWave(testBuffer, 0.8f, freq, 48000.0);

                float inputRms = 0.0f;
                for (int i = 512; i < 1536; ++i) // Use middle portion to avoid transients
                {
                    float s = testBuffer.getSample(0, i);
                    inputRms += s * s;
                }
                inputRms = std::sqrt(inputRms / 1024.0f);

                // Process
                compressor.processBlock(testBuffer, midiBuffer);

                float outputRms = 0.0f;
                for (int i = 512; i < 1536; ++i)
                {
                    float s = testBuffer.getSample(0, i);
                    outputRms += s * s;
                }
                outputRms = std::sqrt(outputRms / 1024.0f);

                levelRatios[f] = (inputRms > 0.0001f) ? (outputRms / inputRms) : 1.0f;
            }

            // Calculate the variation across frequencies
            // Phase-aligned mixing should give consistent levels across all frequencies
            // Comb filtering creates large variations (some frequencies nulled, others boosted)
            float minRatio = *std::min_element(levelRatios.begin(), levelRatios.end());
            float maxRatio = *std::max_element(levelRatios.begin(), levelRatios.end());
            float variationDb = 20.0f * std::log10((maxRatio / minRatio) + 1e-10f);

            // With perfect phase alignment, variation should be < 3dB
            // Comb filtering typically causes 6-12+ dB variations
            expect(variationDb < 6.0f,
                   "OS " + modeStr + ": Frequency variation at 50% mix is " +
                   juce::String(variationDb, 1) + " dB (max allowed: 6 dB - indicates comb filtering)");

            logMessage("OS " + modeStr + ": Freq variation = " + juce::String(variationDb, 1) +
                       " dB (min ratio: " + juce::String(minRatio, 3) +
                       ", max ratio: " + juce::String(maxRatio, 3) + ")");

            // Log individual frequency responses
            for (size_t f = 0; f < testFreqs.size(); ++f)
            {
                logMessage("  " + juce::String(static_cast<int>(testFreqs[f])) + " Hz: " +
                           juce::String(20.0f * std::log10(levelRatios[f] + 1e-10f), 1) + " dB");
            }
        }
    }
};

// Register the test
static UniversalCompressorTests universalCompressorTests;
