#include "SimpleReverbEngine.h"

SimpleReverbEngine::SimpleReverbEngine()
{
    // Initialize with default sizes
    for (int i = 0; i < numCombs; ++i)
    {
        combFiltersL[i].setSize(combTunings[i]);
        combFiltersR[i].setSize(combTunings[i] + stereoSpread);
    }

    for (int i = 0; i < numAllpasses; ++i)
    {
        allpassFiltersL[i].setSize(allpassTunings[i]);
        allpassFiltersR[i].setSize(allpassTunings[i] + stereoSpread);
    }

    updateParameters();
}

void SimpleReverbEngine::prepare(double sampleRate, int blockSize)
{
    currentSampleRate = sampleRate;

    // Scale the buffer sizes based on sample rate
    float sampleRateScale = static_cast<float>(sampleRate) / 44100.0f;

    for (int i = 0; i < numCombs; ++i)
    {
        combFiltersL[i].setSize(static_cast<int>(combTunings[i] * sampleRateScale));
        combFiltersR[i].setSize(static_cast<int>((combTunings[i] + stereoSpread) * sampleRateScale));
    }

    for (int i = 0; i < numAllpasses; ++i)
    {
        allpassFiltersL[i].setSize(static_cast<int>(allpassTunings[i] * sampleRateScale));
        allpassFiltersR[i].setSize(static_cast<int>((allpassTunings[i] + stereoSpread) * sampleRateScale));
    }

    reset();
}

void SimpleReverbEngine::reset()
{
    for (auto& comb : combFiltersL)
        comb.reset();
    for (auto& comb : combFiltersR)
        comb.reset();
    for (auto& allpass : allpassFiltersL)
        allpass.reset();
    for (auto& allpass : allpassFiltersR)
        allpass.reset();
}

void SimpleReverbEngine::process(juce::AudioBuffer<float>& buffer)
{
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    if (numChannels == 0 || numSamples == 0)
        return;

    // Get pointers to audio data
    float* leftChannel = buffer.getWritePointer(0);
    float* rightChannel = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // Get input (mono sum for reverb processing)
        float input = leftChannel[sample];
        if (rightChannel)
            input = (input + rightChannel[sample]) * 0.5f;

        // Scale input
        input *= 0.015f; // Prevent clipping

        // Process through parallel comb filters
        float outputL = 0.0f;
        float outputR = 0.0f;

        for (int i = 0; i < numCombs; ++i)
        {
            outputL += combFiltersL[i].process(input, feedback, damp1);
            outputR += combFiltersR[i].process(input, feedback, damp1);
        }

        // Process through series allpass filters
        for (int i = 0; i < numAllpasses; ++i)
        {
            outputL = allpassFiltersL[i].process(outputL);
            outputR = allpassFiltersR[i].process(outputR);
        }

        // Apply stereo width
        float wet1 = outputL * (1.0f + width);
        float wet2 = outputR * (1.0f - width);

        // Mix wet and dry signals
        leftChannel[sample] = leftChannel[sample] * (1.0f - mix) + (wet1 + wet2) * mix;

        if (rightChannel)
        {
            wet1 = outputR * (1.0f + width);
            wet2 = outputL * (1.0f - width);
            rightChannel[sample] = rightChannel[sample] * (1.0f - mix) + (wet1 + wet2) * mix;
        }
    }
}

void SimpleReverbEngine::updateParameters()
{
    // Calculate feedback and damping from room size and damping parameters
    feedback = roomSize * 0.28f + 0.7f;  // Range: 0.7 to 0.98
    damp1 = damping * 0.4f;
    damp2 = 1.0f - damp1;
}