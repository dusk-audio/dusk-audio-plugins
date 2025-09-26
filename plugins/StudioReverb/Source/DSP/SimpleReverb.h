// Simple working reverb for testing
#pragma once
#include <JuceHeader.h>
#include <vector>
#include <cstring>

class SimpleReverb
{
public:
    SimpleReverb()
    {
        // Initialize delay lines
        delayBufferL.resize(maxDelaySize, 0.0f);
        delayBufferR.resize(maxDelaySize, 0.0f);
        combBufferL.resize(combSize, 0.0f);
        combBufferR.resize(combSize, 0.0f);
    }

    void prepare(double sampleRate, int /*blockSize*/)
    {
        this->sampleRate = sampleRate;

        // Calculate delay times in samples
        delayTime1 = static_cast<int>(0.037f * sampleRate);  // 37ms
        delayTime2 = static_cast<int>(0.041f * sampleRate);  // 41ms
        delayTime3 = static_cast<int>(0.043f * sampleRate);  // 43ms
        delayTime4 = static_cast<int>(0.047f * sampleRate);  // 47ms

        // Make sure delays fit in buffer
        delayTime1 = juce::jmin(delayTime1, maxDelaySize - 1);
        delayTime2 = juce::jmin(delayTime2, maxDelaySize - 1);
        delayTime3 = juce::jmin(delayTime3, maxDelaySize - 1);
        delayTime4 = juce::jmin(delayTime4, maxDelaySize - 1);

        reset();
    }

    void reset()
    {
        std::fill(delayBufferL.begin(), delayBufferL.end(), 0.0f);
        std::fill(delayBufferR.begin(), delayBufferR.end(), 0.0f);
        std::fill(combBufferL.begin(), combBufferL.end(), 0.0f);
        std::fill(combBufferR.begin(), combBufferR.end(), 0.0f);
        delayIndex = 0;
        combIndex = 0;
    }

    void processBlock(juce::AudioBuffer<float>& buffer)
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        if (numChannels < 2) return;

        float* left = buffer.getWritePointer(0);
        float* right = buffer.getWritePointer(1);

        for (int i = 0; i < numSamples; ++i)
        {
            // Get input samples
            float inL = left[i];
            float inR = right[i];

            // Read from delay lines at different times for diffusion
            int idx1 = (delayIndex - delayTime1 + maxDelaySize) % maxDelaySize;
            int idx2 = (delayIndex - delayTime2 + maxDelaySize) % maxDelaySize;
            int idx3 = (delayIndex - delayTime3 + maxDelaySize) % maxDelaySize;
            int idx4 = (delayIndex - delayTime4 + maxDelaySize) % maxDelaySize;

            float delay1L = delayBufferL[idx1];
            float delay2L = delayBufferL[idx2];
            float delay3L = delayBufferL[idx3];
            float delay4L = delayBufferL[idx4];

            float delay1R = delayBufferR[idx1];
            float delay2R = delayBufferR[idx2];
            float delay3R = delayBufferR[idx3];
            float delay4R = delayBufferR[idx4];

            // Comb filter output
            float combL = combBufferL[combIndex];
            float combR = combBufferR[combIndex];

            // Mix delays (simple all-pass network simulation)
            float reverbL = (delay1L + delay2L + delay3L + delay4L) * 0.25f + combL * 0.3f;
            float reverbR = (delay1R + delay2R + delay3R + delay4R) * 0.25f + combR * 0.3f;

            // Feed delays with input and feedback
            delayBufferL[delayIndex] = inL + reverbL * feedback;
            delayBufferR[delayIndex] = inR + reverbR * feedback;

            // Update comb filters
            combBufferL[combIndex] = inL + combL * combFeedback;
            combBufferR[combIndex] = inR + combR * combFeedback;

            // Output = dry + wet reverb
            left[i] = inL * dryLevel + reverbL * wetLevel;
            right[i] = inR * dryLevel + reverbR * wetLevel;

            // Update indices
            delayIndex = (delayIndex + 1) % maxDelaySize;
            combIndex = (combIndex + 1) % combSize;
        }
    }

    void setDryLevel(float level) { dryLevel = level; }
    void setWetLevel(float level) { wetLevel = level; }

private:
    double sampleRate = 44100.0;

    // Delay buffers
    static constexpr int maxDelaySize = 8192;
    static constexpr int combSize = 4096;
    std::vector<float> delayBufferL;
    std::vector<float> delayBufferR;
    std::vector<float> combBufferL;
    std::vector<float> combBufferR;

    int delayIndex = 0;
    int combIndex = 0;

    // Delay times in samples
    int delayTime1 = 1633;
    int delayTime2 = 1811;
    int delayTime3 = 1897;
    int delayTime4 = 2073;

    // Parameters
    float dryLevel = 0.8f;
    float wetLevel = 0.2f;
    float feedback = 0.6f;
    float combFeedback = 0.4f;
};