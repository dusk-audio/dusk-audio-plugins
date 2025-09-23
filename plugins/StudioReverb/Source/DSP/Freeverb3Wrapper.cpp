#include "Freeverb3Wrapper.h"

Freeverb3Wrapper::Freeverb3Wrapper()
{
    initializeReverbs();
}

Freeverb3Wrapper::~Freeverb3Wrapper() = default;

void Freeverb3Wrapper::initializeReverbs()
{
    // Initialize all reverb types
    earlyReflections = std::make_unique<fv3::earlyref_f>();
    roomReverb = std::make_unique<fv3::revmodel_f>();
    plateReverb = std::make_unique<fv3::progenitor_f>();
    hallReverb = std::make_unique<fv3::zrev_f>();
}

void Freeverb3Wrapper::prepare(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;

    // Prepare buffers
    leftBuffer.resize(samplesPerBlock);
    rightBuffer.resize(samplesPerBlock);

    // Set sample rate for all reverbs
    earlyReflections->setSampleRate(sampleRate);
    roomReverb->setSampleRate(sampleRate);
    plateReverb->setSampleRate(sampleRate);
    hallReverb->setSampleRate(sampleRate);

    // Apply current parameters
    updateParameters();
}

void Freeverb3Wrapper::reset()
{
    // Clear all reverb buffers
    earlyReflections->mute();
    roomReverb->mute();
    plateReverb->mute();
    hallReverb->mute();

    // Clear processing buffers
    std::fill(leftBuffer.begin(), leftBuffer.end(), 0.0f);
    std::fill(rightBuffer.begin(), rightBuffer.end(), 0.0f);
}

void Freeverb3Wrapper::processBlock(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numChannels < 2)
        return;

    // Get input pointers
    const float* inputL = buffer.getReadPointer(0);
    const float* inputR = numChannels > 1 ? buffer.getReadPointer(1) : inputL;

    float* outputL = buffer.getWritePointer(0);
    float* outputR = numChannels > 1 ? buffer.getWritePointer(1) : outputL;

    // Copy input to processing buffers
    std::copy(inputL, inputL + numSamples, leftBuffer.data());
    std::copy(inputR, inputR + numSamples, rightBuffer.data());

    // Process based on selected reverb type
    switch (currentType)
    {
        case ReverbType::EarlyReflections:
            earlyReflections->processreplace(leftBuffer.data(), rightBuffer.data(),
                                            leftBuffer.data(), rightBuffer.data(),
                                            numSamples);
            break;

        case ReverbType::Room:
            roomReverb->processreplace(leftBuffer.data(), rightBuffer.data(),
                                       leftBuffer.data(), rightBuffer.data(),
                                       numSamples);
            break;

        case ReverbType::Plate:
            plateReverb->processreplace(leftBuffer.data(), rightBuffer.data(),
                                        leftBuffer.data(), rightBuffer.data(),
                                        numSamples);
            break;

        case ReverbType::Hall:
            hallReverb->processreplace(leftBuffer.data(), rightBuffer.data(),
                                       leftBuffer.data(), rightBuffer.data(),
                                       numSamples);
            break;
    }

    // Mix wet and dry signals
    for (int i = 0; i < numSamples; ++i)
    {
        outputL[i] = inputL[i] * dryLevel + leftBuffer[i] * wetLevel;
        if (numChannels > 1)
            outputR[i] = inputR[i] * dryLevel + rightBuffer[i] * wetLevel;
    }

    // Apply width control for stereo
    if (numChannels > 1 && width < 1.0f)
    {
        const float w = width;
        const float iw = 1.0f - w;

        for (int i = 0; i < numSamples; ++i)
        {
            float mid = (outputL[i] + outputR[i]) * 0.5f;
            float side = (outputL[i] - outputR[i]) * 0.5f;

            outputL[i] = mid + side * w;
            outputR[i] = mid - side * w;
        }
    }
}

void Freeverb3Wrapper::setReverbType(ReverbType type)
{
    if (currentType != type)
    {
        currentType = type;
        reset(); // Clear buffers when switching
        updateParameters(); // Apply parameters to new reverb
    }
}

void Freeverb3Wrapper::updateParameters()
{
    // Update parameters based on reverb type
    switch (currentType)
    {
        case ReverbType::EarlyReflections:
            earlyReflections->setRSFactor(roomSize);
            earlyReflections->setwidth(width);
            earlyReflections->setLRDelay(preDelay * 0.001f * currentSampleRate);
            earlyReflections->setDiffusion(diffusion);
            break;

        case ReverbType::Room:
            roomReverb->setroomsize(roomSize);
            roomReverb->setdamp(damping);
            roomReverb->setwidth(width);
            roomReverb->setwet(wetLevel);
            roomReverb->setdry(dryLevel);
            break;

        case ReverbType::Plate:
            plateReverb->setdecay(decayTime);
            plateReverb->setdiffusion1(diffusion);
            plateReverb->setdiffusion2(diffusion);
            plateReverb->setdamping(damping);
            plateReverb->setinputdamp(damping);
            plateReverb->setbassbandwidth(0.5f);
            plateReverb->setbassboost(1.0f);
            break;

        case ReverbType::Hall:
            hallReverb->setrt60(decayTime);
            hallReverb->setdiffusion(diffusion);
            hallReverb->setinputdamp(damping);
            hallReverb->setdamp(damping);
            hallReverb->setoutputdamp(damping);
            break;
    }
}

void Freeverb3Wrapper::setRoomSize(float value)
{
    roomSize = juce::jlimit(0.0f, 1.0f, value);
    updateParameters();
}

void Freeverb3Wrapper::setDamping(float value)
{
    damping = juce::jlimit(0.0f, 1.0f, value);
    updateParameters();
}

void Freeverb3Wrapper::setPreDelay(float ms)
{
    preDelay = juce::jlimit(0.0f, 200.0f, ms);
    updateParameters();
}

void Freeverb3Wrapper::setDecayTime(float seconds)
{
    decayTime = juce::jlimit(0.1f, 30.0f, seconds);
    updateParameters();
}

void Freeverb3Wrapper::setDiffusion(float value)
{
    diffusion = juce::jlimit(0.0f, 1.0f, value);
    updateParameters();
}

void Freeverb3Wrapper::setModulation(float value)
{
    modulation = juce::jlimit(0.0f, 1.0f, value);
    updateParameters();
}

void Freeverb3Wrapper::setWetLevel(float value)
{
    wetLevel = juce::jlimit(0.0f, 1.0f, value);
}

void Freeverb3Wrapper::setDryLevel(float value)
{
    dryLevel = juce::jlimit(0.0f, 1.0f, value);
}

void Freeverb3Wrapper::setWidth(float value)
{
    width = juce::jlimit(0.0f, 1.0f, value);
    updateParameters();
}