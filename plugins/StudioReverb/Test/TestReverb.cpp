// Test program for StudioReverb - to be built with CMake
#include "../Source/PluginProcessor.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <memory>

class ReverbTester
{
public:
    bool runTest()
    {
        std::cout << "\n========================================\n";
        std::cout << "StudioReverb Plugin Test Suite\n";
        std::cout << "========================================\n\n";

        // Create the plugin processor directly
        auto processor = std::make_unique<StudioReverbAudioProcessor>();
        if (!processor)
        {
            std::cerr << "❌ Failed to create plugin processor!" << std::endl;
            return false;
        }
        std::cout << "✓ Plugin created successfully\n";

        // Setup audio parameters
        const double sampleRate = 44100.0;
        const int samplesPerBlock = 512;

        // Prepare processor
        processor->setRateAndBufferSizeDetails(sampleRate, samplesPerBlock);
        processor->prepareToPlay(sampleRate, samplesPerBlock);
        std::cout << "✓ Plugin prepared (SR: " << sampleRate << " Hz, Block: " << samplesPerBlock << " samples)\n\n";

        // Get the APVTS to set parameters
        auto& apvts = processor->getAPVTS();

        // Test each reverb algorithm
        bool allTestsPassed = true;

        std::vector<std::pair<int, std::string>> algorithms = {
            {0, "Room"},
            {1, "Hall"},
            {2, "Plate"},
            {3, "Early Reflections"}
        };

        for (const auto& [algIndex, algName] : algorithms)
        {
            std::cout << "----------------------------------------\n";
            std::cout << "Testing " << algName << " Reverb\n";
            std::cout << "----------------------------------------\n";

            // Set reverb type
            if (auto* param = apvts.getParameter("reverbType"))
            {
                param->setValue(algIndex / 3.0f);  // Normalize to 0-1
                std::cout << "  Set algorithm to " << algName << "\n";
            }

            // Set mix levels for testing
            if (auto* param = apvts.getParameter("dryLevel"))
            {
                param->setValue(0.5f);  // 50% dry
                std::cout << "  Set Dry Level to 50%\n";
            }
            if (auto* param = apvts.getParameter("wetLevel"))
            {
                param->setValue(0.5f);  // 50% wet
                std::cout << "  Set Wet Level to 50%\n";
            }

            // Force parameter update
            processor->updateReverbParameters();

            // Run test for this algorithm
            if (!testAlgorithm(processor.get(), algName, sampleRate, samplesPerBlock))
            {
                allTestsPassed = false;
            }
            std::cout << "\n";
        }

        // Clean up
        processor->releaseResources();

        // Final report
        std::cout << "========================================\n";
        if (allTestsPassed)
        {
            std::cout << "✅ ALL TESTS PASSED - Reverb is working!\n";
        }
        else
        {
            std::cout << "❌ SOME TESTS FAILED - Reverb needs fixing!\n";
        }
        std::cout << "========================================\n\n";

        return allTestsPassed;
    }

private:
    bool testAlgorithm(StudioReverbAudioProcessor* processor, const std::string& algName,
                       double sampleRate, int samplesPerBlock)
    {
        // Create test buffer
        const int totalSamples = samplesPerBlock * 20;  // Process 20 blocks
        juce::AudioBuffer<float> buffer(2, totalSamples);
        buffer.clear();

        // Generate test signal: impulse + tone burst
        buffer.setSample(0, 0, 1.0f);  // Impulse left
        buffer.setSample(1, 0, 1.0f);  // Impulse right

        // Add 440Hz tone burst (100 samples)
        for (int i = 10; i < 110; ++i)
        {
            float sample = 0.5f * std::sin(2.0f * M_PI * 440.0f * i / sampleRate);
            buffer.setSample(0, i, sample);
            buffer.setSample(1, i, sample);
        }

        // Store original for comparison
        juce::AudioBuffer<float> originalBuffer(2, totalSamples);
        originalBuffer.makeCopyOf(buffer);

        // Process the audio in blocks
        juce::MidiBuffer midiMessages;
        int processed = 0;

        while (processed < totalSamples)
        {
            int samplesToProcess = std::min(samplesPerBlock, totalSamples - processed);

            // Create temporary buffer for this block
            juce::AudioBuffer<float> blockBuffer(2, samplesToProcess);

            // Copy from main buffer
            for (int ch = 0; ch < 2; ++ch)
            {
                blockBuffer.copyFrom(ch, 0, buffer, ch, processed, samplesToProcess);
            }

            // Process this block
            processor->processBlock(blockBuffer, midiMessages);

            // Copy back to main buffer
            for (int ch = 0; ch < 2; ++ch)
            {
                buffer.copyFrom(ch, processed, blockBuffer, ch, 0, samplesToProcess);
            }

            processed += samplesToProcess;
        }

        // Analyze results
        return analyzeResults(originalBuffer, buffer, algName);
    }

    bool analyzeResults(const juce::AudioBuffer<float>& original,
                       const juce::AudioBuffer<float>& processed,
                       const std::string& algName)
    {
        bool testPassed = false;

        // 1. Check if output is non-zero
        float outputMagnitude = processed.getMagnitude(0, processed.getNumSamples());
        std::cout << "  Output magnitude: " << std::fixed << std::setprecision(6) << outputMagnitude;

        if (outputMagnitude < 0.0001f)
        {
            std::cout << " ❌ (No output detected!)\n";
            return false;
        }
        std::cout << " ✓\n";

        // 2. Check for reverb tail
        int tailStart = processed.getNumSamples() * 3 / 4;
        int tailLength = processed.getNumSamples() / 4;
        float tailMagnitude = processed.getMagnitude(tailStart, tailLength);

        std::cout << "  Reverb tail magnitude: " << tailMagnitude;
        if (tailMagnitude > 0.0001f)
        {
            std::cout << " ✓ (Reverb tail detected!)\n";
            testPassed = true;
        }
        else
        {
            std::cout << " ❌ (No reverb tail)\n";
        }

        // 3. Compare with original
        float difference = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int i = 0; i < processed.getNumSamples(); ++i)
            {
                float orig = original.getSample(ch, i);
                float proc = processed.getSample(ch, i);
                difference += std::abs(proc - orig);
            }
        }

        std::cout << "  Total difference from dry: " << difference;
        if (difference > 0.01f)
        {
            std::cout << " ✓ (Signal modified)\n";
        }
        else
        {
            std::cout << " ❌ (Signal unchanged)\n";
            testPassed = false;
        }

        // 4. Sample analysis
        std::cout << "  Sample points:\n";
        for (int i = 0; i < 5; ++i)
        {
            int idx = i * 2000;
            if (idx < processed.getNumSamples())
            {
                float L = processed.getSample(0, idx);
                float R = processed.getSample(1, idx);
                std::cout << "    [" << std::setw(5) << idx << "]: L=" << std::setw(10) << L
                          << ", R=" << std::setw(10) << R;
                if (idx > 200 && (std::abs(L) > 0.0001f || std::abs(R) > 0.0001f))
                {
                    std::cout << " ← reverb activity";
                }
                std::cout << "\n";
            }
        }

        // Final verdict
        if (testPassed)
        {
            std::cout << "  ✅ " << algName << " reverb WORKING\n";
        }
        else
        {
            std::cout << "  ❌ " << algName << " reverb NOT WORKING\n";
        }

        return testPassed;
    }
};

int main()
{
    ReverbTester tester;
    bool success = tester.runTest();
    return success ? 0 : 1;
}