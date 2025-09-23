#include "MultiReverbProcessor.h"

MultiReverbProcessor::MultiReverbProcessor()
{
}

void MultiReverbProcessor::prepare(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;

    // Prepare pre-delay (up to 200ms)
    int maxPreDelaySamples = int(sampleRate * 0.2);
    preDelayL.setSize(maxPreDelaySamples);
    preDelayR.setSize(maxPreDelaySamples);

    // Prepare all reverb types
    earlyReflections.prepare(sampleRate);
    roomReverb.prepare(sampleRate);
    plateReverb.prepare(sampleRate);
    hallReverb.prepare(sampleRate);

    // Prepare temp buffers
    tempBufferL.resize(samplesPerBlock);
    tempBufferR.resize(samplesPerBlock);

    // Update parameters
    updateParameters();
}

void MultiReverbProcessor::reset()
{
    preDelayL.clear();
    preDelayR.clear();

    earlyReflections.clear();
    roomReverb.clear();
    plateReverb.clear();
    hallReverb.clear();

    std::fill(tempBufferL.begin(), tempBufferL.end(), 0.0f);
    std::fill(tempBufferR.begin(), tempBufferR.end(), 0.0f);
}

void MultiReverbProcessor::processBlock(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numChannels < 1 || numSamples == 0)
        return;

    // Get pointers
    float* left = buffer.getWritePointer(0);
    float* right = numChannels > 1 ? buffer.getWritePointer(1) : left;

    // Store dry signal
    std::vector<float> dryL(left, left + numSamples);
    std::vector<float> dryR;
    if (numChannels > 1)
        dryR.assign(right, right + numSamples);
    else
        dryR = dryL;

    // Copy to temp buffers for processing
    std::copy(left, left + numSamples, tempBufferL.data());
    if (numChannels > 1)
        std::copy(right, right + numSamples, tempBufferR.data());
    else
        std::copy(left, left + numSamples, tempBufferR.data());

    // Apply pre-delay if needed
    if (preDelay > 0.0f)
    {
        int delaySamples = int(preDelay * 0.001f * currentSampleRate);
        for (int i = 0; i < numSamples; ++i)
        {
            float delayedL = preDelayL.read(delaySamples);
            float delayedR = preDelayR.read(delaySamples);

            preDelayL.write(tempBufferL[i]);
            preDelayR.write(tempBufferR[i]);

            tempBufferL[i] = delayedL;
            tempBufferR[i] = delayedR;
        }
    }

    // Process based on selected reverb type
    switch (currentType)
    {
        case ReverbType::EarlyReflections:
            earlyReflections.process(tempBufferL.data(), tempBufferR.data(), numSamples);
            break;

        case ReverbType::Room:
            roomReverb.process(tempBufferL.data(), tempBufferR.data(), numSamples);
            break;

        case ReverbType::Plate:
            plateReverb.process(tempBufferL.data(), tempBufferR.data(), numSamples);
            break;

        case ReverbType::Hall:
            hallReverb.process(tempBufferL.data(), tempBufferR.data(), numSamples);
            break;
    }

    // Mix wet and dry signals
    for (int i = 0; i < numSamples; ++i)
    {
        left[i] = dryL[i] * dryLevel + tempBufferL[i] * wetLevel;
        if (numChannels > 1)
            right[i] = dryR[i] * dryLevel + tempBufferR[i] * wetLevel;
    }

    // Apply width control for stereo
    if (numChannels > 1 && width < 1.0f)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float mid = (left[i] + right[i]) * 0.5f;
            float side = (left[i] - right[i]) * 0.5f * width;

            left[i] = mid + side;
            right[i] = mid - side;
        }
    }
}

void MultiReverbProcessor::updateParameters()
{
    // Update reverb-specific parameters based on type
    switch (currentType)
    {
        case ReverbType::EarlyReflections:
            // Early reflections use room size to control tap spacing and decay time for tap gains
            earlyReflections.setParameters(roomSize, decayTime / 5.0f, diffusion);
            break;

        case ReverbType::Room:
            // Room reverb uses traditional Freeverb parameters
            roomReverb.setParameters(roomSize, damping);
            // Also apply decay time by adjusting feedback
            roomReverb.setDecayFactor(decayTime / 3.0f);
            break;

        case ReverbType::Plate:
            // Plate reverb uses decay time and damping directly
            plateReverb.setParameters(decayTime / 5.0f, damping);
            plateReverb.setInputDiffusion(diffusion);
            break;

        case ReverbType::Hall:
            // Hall reverb uses all parameters
            hallReverb.setParameters(decayTime / 10.0f, damping);
            hallReverb.setDiffusion(diffusion);
            hallReverb.setRoomSize(roomSize);
            break;
    }
}

void MultiReverbProcessor::setRoomSize(float value)
{
    roomSize = juce::jlimit(0.0f, 1.0f, value);
    updateParameters();
}

void MultiReverbProcessor::setDamping(float value)
{
    damping = juce::jlimit(0.0f, 1.0f, value);
    updateParameters();
}

void MultiReverbProcessor::setPreDelay(float ms)
{
    preDelay = juce::jlimit(0.0f, 200.0f, ms);
}

void MultiReverbProcessor::setDecayTime(float seconds)
{
    decayTime = juce::jlimit(0.1f, 30.0f, seconds);
    updateParameters();
}

void MultiReverbProcessor::setDiffusion(float value)
{
    diffusion = juce::jlimit(0.0f, 1.0f, value);
    updateParameters();
}

void MultiReverbProcessor::setWetLevel(float value)
{
    wetLevel = juce::jlimit(0.0f, 1.0f, value);
}

void MultiReverbProcessor::setDryLevel(float value)
{
    dryLevel = juce::jlimit(0.0f, 1.0f, value);
}

void MultiReverbProcessor::setWidth(float value)
{
    width = juce::jlimit(0.0f, 1.0f, value);
}