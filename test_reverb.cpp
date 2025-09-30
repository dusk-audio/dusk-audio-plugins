/*
  ==============================================================================

    StudioReverb Test Program
    Automated test to verify Room reverb is producing output

  ==============================================================================
*/

#include <JuceHeader.h>
#include "plugins/StudioReverb/Source/PluginProcessor.h"
#include <memory>
#include <iostream>
#include <cmath>

// Test configuration
const int SAMPLE_RATE = 44100;
const int BUFFER_SIZE = 512;
const int TEST_DURATION_SAMPLES = SAMPLE_RATE * 2; // 2 seconds

// Helper function to generate a test impulse
void generateImpulse(juce::AudioBuffer<float>& buffer, int position)
{
    if (position < buffer.getNumSamples())
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            buffer.setSample(ch, position, 1.0f);
        }
    }
}

// Helper function to calculate RMS level
float calculateRMS(const juce::AudioBuffer<float>& buffer, int startSample = 0, int numSamples = -1)
{
    if (numSamples == -1)
        numSamples = buffer.getNumSamples() - startSample;

    float sum = 0.0f;
    int totalSamples = 0;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        for (int i = startSample; i < startSample + numSamples && i < buffer.getNumSamples(); ++i)
        {
            float sample = buffer.getSample(ch, i);
            sum += sample * sample;
            totalSamples++;
        }
    }

    return totalSamples > 0 ? std::sqrt(sum / totalSamples) : 0.0f;
}

// Test a specific reverb algorithm
bool testReverbAlgorithm(int algorithmIndex, const juce::String& algorithmName)
{
    std::cout << "\n=== Testing " << algorithmName.toStdString() << " Algorithm ===" << std::endl;

    // Create processor
    auto processor = std::make_unique<StudioReverbAudioProcessor>();

    // Prepare processor
    processor->prepareToPlay(SAMPLE_RATE, BUFFER_SIZE);

    // Set to the specified algorithm
    auto* reverbType = dynamic_cast<juce::AudioParameterChoice*>(
        processor->apvts.getParameter("reverbType"));
    if (reverbType)
    {
        reverbType->setValueNotifyingHost(algorithmIndex / 3.0f); // Normalize to 0-1
        std::cout << "Set algorithm to: " << reverbType->getCurrentChoiceName().toStdString() << std::endl;
    }

    // Set reverb parameters for testing
    auto* dryLevel = dynamic_cast<juce::AudioParameterFloat*>(
        processor->apvts.getParameter("dryLevel"));
    auto* lateLevel = dynamic_cast<juce::AudioParameterFloat*>(
        processor->apvts.getParameter("lateLevel"));
    auto* earlyLevel = dynamic_cast<juce::AudioParameterFloat*>(
        processor->apvts.getParameter("earlyLevel"));
    auto* decay = dynamic_cast<juce::AudioParameterFloat*>(
        processor->apvts.getParameter("decay"));

    if (dryLevel) dryLevel->setValueNotifyingHost(0.0f);     // 0% dry (mute dry signal)
    if (lateLevel) lateLevel->setValueNotifyingHost(1.0f);   // 100% late reverb
    if (earlyLevel) earlyLevel->setValueNotifyingHost(0.5f); // 50% early reflections
    if (decay) decay->setValueNotifyingHost(0.5f);           // Medium decay

    std::cout << "Parameters set: Dry=0%, Late=100%, Early=50%, Decay=medium" << std::endl;

    // Create test buffer with impulse
    juce::AudioBuffer<float> testBuffer(2, TEST_DURATION_SAMPLES);
    testBuffer.clear();

    // Add impulse at the beginning
    generateImpulse(testBuffer, 100); // Small delay to let reverb initialize

    // Calculate dry signal RMS (should be near zero except for impulse)
    float dryRMS = calculateRMS(testBuffer, 0, SAMPLE_RATE / 2);
    std::cout << "Dry signal RMS: " << dryRMS << std::endl;

    // Process in blocks
    int samplesProcessed = 0;
    while (samplesProcessed < TEST_DURATION_SAMPLES)
    {
        int samplesToProcess = std::min(BUFFER_SIZE, TEST_DURATION_SAMPLES - samplesProcessed);

        // Create a temporary buffer for this block
        juce::AudioBuffer<float> blockBuffer(2, samplesToProcess);

        // Copy from test buffer
        for (int ch = 0; ch < 2; ++ch)
        {
            blockBuffer.copyFrom(ch, 0, testBuffer, ch, samplesProcessed, samplesToProcess);
        }

        // Process block
        juce::MidiBuffer midiBuffer;
        processor->processBlock(blockBuffer, midiBuffer);

        // Copy back to test buffer
        for (int ch = 0; ch < 2; ++ch)
        {
            testBuffer.copyFrom(ch, samplesProcessed, blockBuffer, ch, 0, samplesToProcess);
        }

        samplesProcessed += samplesToProcess;
    }

    // Analyze output
    // Skip first 0.5 seconds (impulse region) and measure reverb tail
    int tailStart = SAMPLE_RATE / 2;
    int tailLength = SAMPLE_RATE; // Measure 1 second of tail

    float tailRMS = calculateRMS(testBuffer, tailStart, tailLength);
    float peakLevel = testBuffer.getMagnitude(tailStart, tailLength);

    std::cout << "Reverb tail RMS: " << tailRMS << std::endl;
    std::cout << "Reverb tail peak: " << peakLevel << std::endl;

    // Check if we have significant reverb output
    bool hasReverb = tailRMS > 0.001f; // Threshold for detecting reverb

    if (hasReverb)
    {
        std::cout << "✓ " << algorithmName.toStdString() << " reverb is producing output!" << std::endl;

        // Additional analysis
        // Find reverb decay time (RT60)
        float maxLevel = testBuffer.getMagnitude(200, 1000); // Find peak after impulse
        float threshold60dB = maxLevel * 0.001f; // -60dB threshold

        int decaySample = -1;
        for (int i = 1000; i < TEST_DURATION_SAMPLES; i++)
        {
            float level = std::abs(testBuffer.getSample(0, i)) + std::abs(testBuffer.getSample(1, i));
            if (level < threshold60dB)
            {
                decaySample = i;
                break;
            }
        }

        if (decaySample > 0)
        {
            float decayTime = (decaySample - 100) / (float)SAMPLE_RATE;
            std::cout << "Estimated decay time: " << decayTime << " seconds" << std::endl;
        }
    }
    else
    {
        std::cout << "✗ " << algorithmName.toStdString() << " reverb is NOT producing output!" << std::endl;
        std::cout << "  This indicates the reverb algorithm may not be working correctly." << std::endl;
    }

    return hasReverb;
}

int main(int argc, char* argv[])
{
    // Initialize JUCE
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "StudioReverb Automated Test" << std::endl;
    std::cout << "============================" << std::endl;

    // Test each algorithm
    bool allPassed = true;

    allPassed &= testReverbAlgorithm(0, "Room");
    allPassed &= testReverbAlgorithm(1, "Hall");
    allPassed &= testReverbAlgorithm(2, "Plate");
    allPassed &= testReverbAlgorithm(3, "Early Reflections");

    // Summary
    std::cout << "\n=== Test Summary ===" << std::endl;
    if (allPassed)
    {
        std::cout << "✓ All reverb algorithms are working!" << std::endl;
        return 0;
    }
    else
    {
        std::cout << "✗ Some reverb algorithms are not working correctly." << std::endl;
        std::cout << "Check the implementation of the failing algorithms." << std::endl;
        return 1;
    }
}