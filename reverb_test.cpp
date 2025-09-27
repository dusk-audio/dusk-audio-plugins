/*
  StudioReverb Automated Test Program
  Tests actual reverb processing without requiring a DAW
*/

#include <iostream>
#include <cmath>
#include <vector>
#include <memory>
#include <cstring>
#include <iomanip>

// Include the actual plugin processor and reverb engine
#include "plugins/StudioReverb/Source/PluginProcessor.cpp"
#include "plugins/StudioReverb/Source/DSP/DragonflyReverb.cpp"
#include "plugins/StudioReverb/Source/DSP/PresetManager.cpp"

// Include all freeverb dependencies
#include "plugins/StudioReverb/Source/freeverb/earlyref.cpp"
#include "plugins/StudioReverb/Source/freeverb/zrev2.cpp"
#include "plugins/StudioReverb/Source/freeverb/progenitor2.cpp"
#include "plugins/StudioReverb/Source/freeverb/progenitor.cpp"
#include "plugins/StudioReverb/Source/freeverb/revbase.cpp"
#include "plugins/StudioReverb/Source/freeverb/nrev.cpp"
#include "plugins/StudioReverb/Source/freeverb/nrevb.cpp"
#include "plugins/StudioReverb/Source/freeverb/strev.cpp"
#include "plugins/StudioReverb/Source/freeverb/biquad_f.cpp"
#include "plugins/StudioReverb/Source/freeverb/efilter.cpp"
#include "plugins/StudioReverb/Source/freeverb/src.cpp"
#include "plugins/StudioReverb/Source/freeverb/fir.cpp"
#include "plugins/StudioReverb/Source/freeverb/blockDelay.cpp"
#include "plugins/StudioReverb/Source/freeverb/allpass.cpp"
#include "plugins/StudioReverb/Source/freeverb/comb.cpp"
#include "plugins/StudioReverb/Source/freeverb/delay.cpp"
#include "plugins/StudioReverb/Source/freeverb/delayline.cpp"
#include "plugins/StudioReverb/Source/freeverb/utils.cpp"

const int SAMPLE_RATE = 44100;
const int BUFFER_SIZE = 512;

class ReverbTester {
public:
    static float calculateRMS(const float* buffer, int numSamples) {
        float sum = 0.0f;
        for (int i = 0; i < numSamples; ++i) {
            sum += buffer[i] * buffer[i];
        }
        return std::sqrt(sum / numSamples);
    }

    static float findPeak(const float* buffer, int numSamples) {
        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i) {
            peak = std::max(peak, std::abs(buffer[i]));
        }
        return peak;
    }

    static bool testAlgorithm(const std::string& algorithmName, int algorithmIndex) {
        std::cout << "\n========================================\n";
        std::cout << "Testing: " << algorithmName << " Algorithm\n";
        std::cout << "========================================\n";

        // Create the plugin processor
        auto processor = std::make_unique<StudioReverbAudioProcessor>();

        // Prepare the processor
        processor->prepareToPlay(SAMPLE_RATE, BUFFER_SIZE);

        // Set the algorithm
        auto* reverbType = processor->reverbType;
        if (reverbType) {
            float normalizedValue = algorithmIndex / 3.0f; // 4 algorithms: 0-3
            reverbType->setValueNotifyingHost(normalizedValue);
            std::cout << "Algorithm set to: " << reverbType->getCurrentChoiceName().toStdString() << "\n";
        }

        // Configure for maximum reverb effect
        if (processor->dryLevel) processor->dryLevel->setValueNotifyingHost(0.0f);      // 0% dry
        if (processor->lateLevel) processor->lateLevel->setValueNotifyingHost(1.0f);    // 100% late
        if (processor->earlyLevel) processor->earlyLevel->setValueNotifyingHost(0.5f);  // 50% early
        if (processor->decay) processor->decay->setValueNotifyingHost(0.5f);            // 2.5s decay
        if (processor->size) processor->size->setValueNotifyingHost(0.5f);              // Medium size
        if (processor->diffuse) processor->diffuse->setValueNotifyingHost(0.75f);       // 75% diffusion

        std::cout << "Parameters: Dry=0%, Late=100%, Early=50%, Decay=2.5s\n";

        // Force parameter update
        processor->updateReverbParameters();

        // Create test signal (impulse)
        const int testDuration = SAMPLE_RATE * 3; // 3 seconds
        juce::AudioBuffer<float> testBuffer(2, testDuration);
        testBuffer.clear();

        // Add impulse at 0.1 seconds
        int impulsePosition = SAMPLE_RATE / 10;
        testBuffer.setSample(0, impulsePosition, 1.0f);
        testBuffer.setSample(1, impulsePosition, 1.0f);

        std::cout << "Input: Impulse at sample " << impulsePosition << "\n";

        // Measure input energy
        float inputRMS = calculateRMS(testBuffer.getReadPointer(0), SAMPLE_RATE / 2);
        float inputPeak = findPeak(testBuffer.getReadPointer(0), SAMPLE_RATE / 2);
        std::cout << "Input RMS: " << inputRMS << ", Peak: " << inputPeak << "\n";

        // Process the buffer in chunks
        int processed = 0;
        while (processed < testDuration) {
            int toProcess = std::min(BUFFER_SIZE, testDuration - processed);

            // Create temporary buffer for this chunk
            juce::AudioBuffer<float> chunkBuffer(2, toProcess);

            // Copy input chunk
            for (int ch = 0; ch < 2; ++ch) {
                chunkBuffer.copyFrom(ch, 0, testBuffer, ch, processed, toProcess);
            }

            // Process
            juce::MidiBuffer midiBuffer;
            processor->processBlock(chunkBuffer, midiBuffer);

            // Copy processed chunk back
            for (int ch = 0; ch < 2; ++ch) {
                testBuffer.copyFrom(ch, processed, chunkBuffer, ch, 0, toProcess);
            }

            processed += toProcess;
        }

        // Analyze reverb tail (0.5s to 2.5s after impulse)
        int tailStart = SAMPLE_RATE / 2;
        int tailLength = SAMPLE_RATE * 2;

        float tailRMS_L = calculateRMS(testBuffer.getReadPointer(0) + tailStart, tailLength);
        float tailRMS_R = calculateRMS(testBuffer.getReadPointer(1) + tailStart, tailLength);
        float tailPeak_L = findPeak(testBuffer.getReadPointer(0) + tailStart, tailLength);
        float tailPeak_R = findPeak(testBuffer.getReadPointer(1) + tailStart, tailLength);

        float avgTailRMS = (tailRMS_L + tailRMS_R) / 2.0f;
        float avgTailPeak = std::max(tailPeak_L, tailPeak_R);

        std::cout << "\nReverb Tail Analysis (0.5s-2.5s):\n";
        std::cout << "  Left:  RMS=" << std::fixed << std::setprecision(6) << tailRMS_L
                  << ", Peak=" << tailPeak_L << "\n";
        std::cout << "  Right: RMS=" << std::fixed << std::setprecision(6) << tailRMS_R
                  << ", Peak=" << tailPeak_R << "\n";
        std::cout << "  Average RMS: " << avgTailRMS << "\n";

        // Determine if reverb is working
        const float RMS_THRESHOLD = 0.0001f;  // Minimum RMS to consider reverb present
        bool hasReverb = avgTailRMS > RMS_THRESHOLD;

        if (hasReverb) {
            std::cout << "\n✓ SUCCESS: " << algorithmName << " reverb is producing output!\n";

            // Estimate decay time (simplified RT60)
            float decayThreshold = avgTailPeak * 0.001f; // -60dB
            int decaySample = -1;

            for (int i = tailStart; i < testDuration; ++i) {
                float level = std::max(
                    std::abs(testBuffer.getSample(0, i)),
                    std::abs(testBuffer.getSample(1, i))
                );
                if (level < decayThreshold) {
                    decaySample = i;
                    break;
                }
            }

            if (decaySample > 0) {
                float decayTime = (decaySample - impulsePosition) / (float)SAMPLE_RATE;
                std::cout << "  Estimated decay time: " << decayTime << " seconds\n";
            }

            // Check stereo width
            float correlation = 0.0f;
            for (int i = tailStart; i < tailStart + SAMPLE_RATE; ++i) {
                correlation += testBuffer.getSample(0, i) * testBuffer.getSample(1, i);
            }
            correlation /= SAMPLE_RATE;
            std::cout << "  Stereo correlation: " << correlation << " (lower = wider)\n";

        } else {
            std::cout << "\n✗ FAILURE: " << algorithmName << " reverb is NOT producing output!\n";
            std::cout << "  RMS " << avgTailRMS << " is below threshold " << RMS_THRESHOLD << "\n";
        }

        return hasReverb;
    }
};

int main(int argc, char* argv[]) {
    std::cout << "StudioReverb Automated Processing Test\n";
    std::cout << "=======================================\n";
    std::cout << "This test processes audio through each reverb algorithm\n";
    std::cout << "and verifies that reverb output is generated.\n";

    bool allPassed = true;

    // Test each algorithm
    allPassed &= ReverbTester::testAlgorithm("Room", 0);
    allPassed &= ReverbTester::testAlgorithm("Hall", 1);
    allPassed &= ReverbTester::testAlgorithm("Plate", 2);
    allPassed &= ReverbTester::testAlgorithm("Early Reflections", 3);

    // Summary
    std::cout << "\n========================================\n";
    std::cout << "TEST SUMMARY\n";
    std::cout << "========================================\n";

    if (allPassed) {
        std::cout << "✓ ALL TESTS PASSED: All reverb algorithms are working!\n";
        return 0;
    } else {
        std::cout << "✗ SOME TESTS FAILED: Check the failing algorithms above.\n";
        std::cout << "\nPossible issues:\n";
        std::cout << "- Reverb processor not properly initialized\n";
        std::cout << "- Mix levels not being applied correctly\n";
        std::cout << "- Internal DSP processing error\n";
        return 1;
    }
}