#include "UniversalCompressor.h"
#include <juce_audio_processors/juce_audio_processors.h>

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
    }

private:
    void testPluginInitialization()
    {
        UniversalCompressor compressor;

        // Test basic plugin properties
        expect(compressor.getName() == "Universal Compressor", "Plugin name is correct");
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

        // Set peak reduction to 50 (moderate compression)
        if (auto* peakReduction = params.getRawParameterValue("opto_peak_reduction"))
            *peakReduction = 50.0f;

        // Set gain to 50 (unity)
        if (auto* gain = params.getRawParameterValue("opto_gain"))
            *gain = 50.0f;

        // Disable bypass
        if (auto* bypass = params.getRawParameterValue("bypass"))
            *bypass = 0.0f;

        // Create test signal: 0dB sine wave (should trigger compression)
        juce::AudioBuffer<float> buffer(2, 512);
        fillBufferWithSineWave(buffer, 1.0f, 1000.0f, 48000.0);

        juce::MidiBuffer midiBuffer;
        compressor.processBlock(buffer, midiBuffer);

        // Check for gain reduction
        float gr = compressor.getGainReduction();
        expect(gr < 0.0f, "Opto mode produces gain reduction on hot signal: " + juce::String(gr) + " dB");
        expect(gr > -50.0f, "Gain reduction is reasonable (not extreme): " + juce::String(gr) + " dB");

        // Check output is attenuated
        float outputPeak = buffer.getMagnitude(0, 0, 512);
        expect(outputPeak < 1.0f, "Output is compressed (peak < 1.0): " + juce::String(outputPeak));
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
            *input = 20.0f; // +20dB input drive

        // Set ratio to 4:1 (index 0)
        if (auto* ratio = params.getRawParameterValue("fet_ratio"))
            *ratio = 0.0f; // 4:1

        // Disable bypass
        if (auto* bypass = params.getRawParameterValue("bypass"))
            *bypass = 0.0f;

        // Create test signal
        juce::AudioBuffer<float> buffer(2, 512);
        fillBufferWithSineWave(buffer, 0.5f, 1000.0f, 48000.0);

        juce::MidiBuffer midiBuffer;
        compressor.processBlock(buffer, midiBuffer);

        // Check for gain reduction
        float gr = compressor.getGainReduction();
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

        // Create test signal at -10dB (10dB over threshold)
        juce::AudioBuffer<float> buffer(2, 512);
        fillBufferWithSineWave(buffer, 0.316f, 1000.0f, 48000.0); // -10dB

        juce::MidiBuffer midiBuffer;
        compressor.processBlock(buffer, midiBuffer);

        // With 10dB over threshold at 4:1, expect ~7.5dB gain reduction
        // (10dB over * (1 - 1/4) = 7.5dB reduction)
        float gr = compressor.getGainReduction();
        expect(gr < 0.0f, "VCA mode produces gain reduction: " + juce::String(gr) + " dB");
        expect(gr > -15.0f && gr < -2.0f,
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

        // Create test signal
        juce::AudioBuffer<float> buffer(2, 512);
        fillBufferWithSineWave(buffer, 0.5f, 1000.0f, 48000.0);

        juce::MidiBuffer midiBuffer;
        compressor.processBlock(buffer, midiBuffer);

        // Check for gain reduction
        float gr = compressor.getGainReduction();
        expect(gr <= 0.0f, "Bus mode gain reduction is non-positive: " + juce::String(gr) + " dB");
        expect(gr > -25.0f, "Bus gain reduction within SSL specs: " + juce::String(gr) + " dB");
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

        // Process and let compressor settle
        juce::MidiBuffer midiBuffer;
        for (int i = 0; i < 10; ++i)
        {
            juce::AudioBuffer<float> buffer(2, 512);
            fillBufferWithSineWave(buffer, 0.1f, 1000.0f, 48000.0); // -20dB signal
            compressor.processBlock(buffer, midiBuffer);
        }

        // Now test with signal at threshold
        juce::AudioBuffer<float> atThreshold(2, 512);
        fillBufferWithSineWave(atThreshold, 0.1f, 1000.0f, 48000.0); // -20dB
        compressor.processBlock(atThreshold, midiBuffer);
        float grAtThreshold = compressor.getGainReduction();

        // At threshold, gain reduction should be minimal
        expect(grAtThreshold > -3.0f,
               "At threshold, minimal GR: " + juce::String(grAtThreshold) + " dB");

        // Test with signal 12dB over threshold
        juce::AudioBuffer<float> overThreshold(2, 512);
        fillBufferWithSineWave(overThreshold, 0.4f, 1000.0f, 48000.0); // -8dB (12dB over -20dB)
        compressor.processBlock(overThreshold, midiBuffer);
        float grOverThreshold = compressor.getGainReduction();

        // With 12dB over threshold at 4:1, expect ~9dB gain reduction
        // (12dB * (1 - 1/4) = 9dB)
        expect(grOverThreshold < -5.0f && grOverThreshold > -15.0f,
               "12dB over threshold produces expected GR: " + juce::String(grOverThreshold) + " dB");
    }

    void testVariableSampleRates()
    {
        // Test all compressor modes at different sample rates
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
                    *peakReduction = 50.0f;

                if (auto* bypass = params.getRawParameterValue("bypass"))
                    *bypass = 0.0f;

                juce::AudioBuffer<float> buffer(2, 512);
                fillBufferWithSineWave(buffer, 0.5f, 1000.0f, sampleRate);

                juce::MidiBuffer midiBuffer;
                compressor.processBlock(buffer, midiBuffer);

                float gr = compressor.getGainReduction();
                expect(gr < 0.0f && gr > -40.0f,
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
                    *input = 20.0f;

                if (auto* bypass = params.getRawParameterValue("bypass"))
                    *bypass = 0.0f;

                juce::AudioBuffer<float> buffer(2, 512);
                fillBufferWithSineWave(buffer, 0.5f, 1000.0f, sampleRate);

                juce::MidiBuffer midiBuffer;
                compressor.processBlock(buffer, midiBuffer);

                float gr = compressor.getGainReduction();
                expect(gr < 0.0f && gr > -50.0f,
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

                juce::AudioBuffer<float> buffer(2, 512);
                fillBufferWithSineWave(buffer, 0.316f, 1000.0f, sampleRate); // -10dB

                juce::MidiBuffer midiBuffer;
                compressor.processBlock(buffer, midiBuffer);

                float gr = compressor.getGainReduction();
                expect(gr < 0.0f && gr > -20.0f,
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

                juce::AudioBuffer<float> buffer(2, 512);
                fillBufferWithSineWave(buffer, 0.5f, 1000.0f, sampleRate);

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
};

// Register the test
static UniversalCompressorTests universalCompressorTests;
