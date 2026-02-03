// Test for LinearPhaseEQProcessor using actual JUCE FFT
// Build: cmake --build . --target LinearPhaseTest

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <iostream>
#include <vector>
#include <cmath>
#include <array>
#include <thread>
#include <chrono>

// Include the actual LinearPhaseEQProcessor
#include "LinearPhaseEQProcessor.h"

int main()
{
    std::cout << "=== LinearPhaseEQProcessor Test with JUCE FFT ===" << std::endl;
    std::cout << "Testing actual plugin Linear Phase mode" << std::endl << std::endl;

    // Create processor
    LinearPhaseEQProcessor processor;

    // Setup
    const double sampleRate = 44100.0;
    const int blockSize = 512;
    const int testDurationSamples = static_cast<int>(sampleRate * 0.5); // 0.5 seconds

    std::cout << "Preparing processor..." << std::endl;
    processor.prepare(sampleRate, blockSize);

    // Get filter length info
    int filterLength = processor.getFilterLength();
    int latency = processor.getLatencyInSamples();
    std::cout << "Filter length: " << filterLength << " samples" << std::endl;
    std::cout << "Reported latency: " << latency << " samples ("
              << (latency / sampleRate * 1000.0) << " ms)" << std::endl;

    // Disable all bands for flat response (unity gain passthrough)
    std::array<bool, 8> bandEnabled = {false, false, false, false, false, false, false, false};
    std::array<float, 8> bandFreq = {100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 15000.0f};
    std::array<float, 8> bandGain = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    std::array<float, 8> bandQ = {0.71f, 0.71f, 0.71f, 0.71f, 0.71f, 0.71f, 0.71f, 0.71f};
    std::array<int, 2> bandSlope = {0, 0};
    float masterGain = 0.0f;

    std::cout << "Updating IR with flat response (all bands disabled, master gain 0 dB)..." << std::endl;
    processor.updateImpulseResponse(bandEnabled, bandFreq, bandGain, bandQ, bandSlope, masterGain);

    // Wait for background thread to rebuild IR with timeout
    const int maxWaitMs = 2000;  // 2 second timeout
    const int pollIntervalMs = 10;
    int waitedMs = 0;
    while (!processor.isIRReady() && waitedMs < maxWaitMs)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs));
        waitedMs += pollIntervalMs;
    }

    if (!processor.isIRReady())
    {
        std::cout << "*** FAIL: IR not ready after " << maxWaitMs << "ms timeout ***" << std::endl;
        return 1;
    }
    std::cout << "IR ready after " << waitedMs << "ms" << std::endl;

    // Generate test signal: 1kHz sine wave
    std::cout << "Generating 1kHz test tone..." << std::endl;
    std::vector<float> testSignal(testDurationSamples);
    for (int i = 0; i < testDurationSamples; ++i)
    {
        testSignal[i] = std::sin(2.0 * M_PI * 1000.0 * i / sampleRate);
    }

    // Process through linear phase EQ
    std::cout << "Processing " << testDurationSamples << " samples through Linear Phase EQ..." << std::endl;
    std::vector<float> output(testDurationSamples);

    // Copy input to output buffer (processChannel modifies in place)
    std::copy(testSignal.begin(), testSignal.end(), output.begin());

    // Process in blocks
    int samplesProcessed = 0;
    while (samplesProcessed < testDurationSamples)
    {
        int samplesToProcess = std::min(blockSize, testDurationSamples - samplesProcessed);
        processor.processChannel(output.data() + samplesProcessed, samplesToProcess);
        samplesProcessed += samplesToProcess;
    }

    // Analyze output
    std::cout << std::endl << "=== Results ===" << std::endl;

    // Find max output amplitude
    float maxOutput = 0.0f;
    float sumOutput = 0.0f;
    int nonZeroCount = 0;

    // Skip initial latency period
    int analysisStart = latency + filterLength; // Extra settling time
    int analysisCount = testDurationSamples - analysisStart;

    // Guard against invalid analysis range
    if (analysisStart >= testDurationSamples || analysisCount <= 0)
    {
        std::cout << "*** FAIL: Analysis range invalid (analysisStart=" << analysisStart
                  << " >= testDurationSamples=" << testDurationSamples << ") ***" << std::endl;
        std::cout << "Increase test duration or reduce filter length." << std::endl;
        return 1;
    }

    for (int i = analysisStart; i < testDurationSamples; ++i)
    {
        float absVal = std::abs(output[i]);
        maxOutput = std::max(maxOutput, absVal);
        sumOutput += absVal;
        if (absVal > 0.001f) nonZeroCount++;
    }

    float avgOutput = sumOutput / static_cast<float>(analysisCount);

    std::cout << "Max output amplitude: " << maxOutput << std::endl;
    std::cout << "Average output amplitude: " << avgOutput << std::endl;
    std::cout << "Non-zero samples (after latency): " << nonZeroCount
              << " / " << (testDurationSamples - analysisStart) << std::endl;

    // Cross-correlation to find actual latency
    int maxLag = filterLength * 2;
    float bestCorr = 0.0f;
    int detectedLatency = 0;

    for (int lag = 0; lag < maxLag && lag < testDurationSamples; ++lag)
    {
        float corr = 0.0f;
        int count = 0;
        for (int i = lag; i < testDurationSamples - lag && i - lag < testDurationSamples; ++i)
        {
            corr += output[i] * testSignal[i - lag];
            count++;
        }
        if (count > 0)
        {
            corr /= count;
            if (corr > bestCorr)
            {
                bestCorr = corr;
                detectedLatency = lag;
            }
        }
    }

    std::cout << "Detected latency (cross-correlation): " << detectedLatency << " samples" << std::endl;

    // Check correlation with delayed input
    float sumSquaredError = 0.0f;
    float maxError = 0.0f;
    int validSamples = 0;

    int startIdx = detectedLatency + filterLength / 2; // After settling
    int endIdx = testDurationSamples - filterLength / 2;

    for (int i = startIdx; i < endIdx; ++i)
    {
        int inputIdx = i - detectedLatency;
        if (inputIdx >= 0 && inputIdx < testDurationSamples)
        {
            float expected = testSignal[inputIdx];
            float actual = output[i];
            float error = std::abs(actual - expected);

            maxError = std::max(maxError, error);
            sumSquaredError += error * error;
            validSamples++;
        }
    }

    float rmsError = validSamples > 0 ? std::sqrt(sumSquaredError / validSamples) : 0.0f;

    std::cout << "Valid samples compared: " << validSamples << std::endl;
    std::cout << "Max absolute error: " << maxError << std::endl;
    std::cout << "RMS error: " << rmsError << std::endl;

    // Show first few samples after detected latency
    std::cout << std::endl << "First 10 samples after detected latency:" << std::endl;
    for (int i = 0; i < 10 && detectedLatency + i < testDurationSamples; ++i)
    {
        int outIdx = detectedLatency + filterLength / 2 + i;
        int inIdx = outIdx - detectedLatency;
        if (outIdx < testDurationSamples && inIdx >= 0 && inIdx < testDurationSamples)
        {
            std::cout << "  output[" << outIdx << "] = " << output[outIdx]
                      << " (expected " << testSignal[inIdx]
                      << ", error = " << std::abs(output[outIdx] - testSignal[inIdx]) << ")" << std::endl;
        }
    }

    // Pass/Fail criteria
    std::cout << std::endl << "=== Test Result ===" << std::endl;

    if (maxOutput < 0.001f)
    {
        std::cout << "*** FAIL: No output detected! Linear Phase mode is producing silence. ***" << std::endl;
        return 1;
    }
    else if (maxOutput < 0.5f)
    {
        std::cout << "*** FAIL: Output too quiet (max=" << maxOutput << "). IR normalization issue. ***" << std::endl;
        return 1;
    }
    else if (maxError > 0.1f)
    {
        std::cout << "*** FAIL: High error (max=" << maxError << "). Possible comb filtering or algorithm issue. ***" << std::endl;
        return 1;
    }
    else
    {
        std::cout << "*** PASS: Linear Phase mode working correctly! ***" << std::endl;
        std::cout << "Output amplitude: " << maxOutput << " (expected ~1.0)" << std::endl;
        std::cout << "Max error: " << maxError << " (acceptable < 0.1)" << std::endl;
        return 0;
    }
}
