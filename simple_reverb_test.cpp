/*
  ==============================================================================

    Simple StudioReverb Room Algorithm Test
    Direct test of the Dragonfly reverb engine

  ==============================================================================
*/

#include <iostream>
#include <cmath>
#include <memory>
#include <cstring>

// Forward declare the reverb class
#include "plugins/StudioReverb/Source/DSP/DragonflyReverb.h"

const int SAMPLE_RATE = 44100;
const int BUFFER_SIZE = 512;
const int TEST_SAMPLES = SAMPLE_RATE * 2; // 2 seconds

// Helper to calculate RMS
float calculateRMS(float* buffer, int numSamples)
{
    float sum = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / numSamples);
}

int main()
{
    std::cout << "Simple StudioReverb Room Algorithm Test\n";
    std::cout << "========================================\n\n";

    // Create reverb processor
    auto reverb = std::make_unique<DragonflyReverb>();

    // Initialize
    reverb->prepare(SAMPLE_RATE, BUFFER_SIZE);

    // Test each algorithm
    for (int algIndex = 0; algIndex < 4; ++algIndex)
    {
        const char* algNames[] = {"Room", "Hall", "Plate", "Early Reflections"};
        std::cout << "Testing " << algNames[algIndex] << " algorithm...\n";

        // Set algorithm
        reverb->setAlgorithm(static_cast<DragonflyReverb::Algorithm>(algIndex));

        // Set parameters for maximum reverb effect
        reverb->setDryLevel(0.0f);    // No dry signal
        reverb->setLateLevel(1.0f);   // Full reverb
        reverb->setEarlyLevel(0.5f);  // Some early reflections
        reverb->setSize(40.0f);       // Medium-large room
        reverb->setDecay(2.0f);       // 2 second decay
        reverb->setDiffuse(75.0f);    // Good diffusion

        // Create test signal (impulse)
        float* testL = new float[TEST_SAMPLES];
        float* testR = new float[TEST_SAMPLES];
        std::memset(testL, 0, sizeof(float) * TEST_SAMPLES);
        std::memset(testR, 0, sizeof(float) * TEST_SAMPLES);

        // Add impulse
        testL[100] = 1.0f;
        testR[100] = 1.0f;

        // Process in blocks using JUCE AudioBuffer
        int processed = 0;
        while (processed < TEST_SAMPLES)
        {
            int toProcess = std::min(BUFFER_SIZE, TEST_SAMPLES - processed);

            // Create JUCE AudioBuffer
            juce::AudioBuffer<float> buffer(2, toProcess);

            // Copy input
            buffer.copyFrom(0, 0, testL + processed, toProcess);
            buffer.copyFrom(1, 0, testR + processed, toProcess);

            // Process
            reverb->processBlock(buffer);

            // Copy output back
            std::memcpy(testL + processed, buffer.getReadPointer(0), sizeof(float) * toProcess);
            std::memcpy(testR + processed, buffer.getReadPointer(1), sizeof(float) * toProcess);

            processed += toProcess;
        }

        // Analyze output - check reverb tail (skip first 0.5 seconds)
        int tailStart = SAMPLE_RATE / 2;
        int tailLength = SAMPLE_RATE;

        float rmsL = calculateRMS(testL + tailStart, tailLength);
        float rmsR = calculateRMS(testR + tailStart, tailLength);
        float avgRMS = (rmsL + rmsR) / 2.0f;

        // Find peak in tail
        float peak = 0.0f;
        for (int i = tailStart; i < tailStart + tailLength; ++i)
        {
            peak = std::max(peak, std::abs(testL[i]));
            peak = std::max(peak, std::abs(testR[i]));
        }

        std::cout << "  Tail RMS: " << avgRMS << " (L:" << rmsL << " R:" << rmsR << ")\n";
        std::cout << "  Tail Peak: " << peak << "\n";

        if (avgRMS > 0.001f)
        {
            std::cout << "  ✓ " << algNames[algIndex] << " is producing reverb!\n";
        }
        else
        {
            std::cout << "  ✗ " << algNames[algIndex] << " is NOT producing reverb!\n";
        }

        std::cout << "\n";

        delete[] testL;
        delete[] testR;
    }

    std::cout << "Test complete.\n";
    return 0;
}