#include "EarlyReflections.h"
#include <cmath>

// Define reflection patterns for different room sizes
const EarlyReflectionsProcessor::Reflection
EarlyReflectionsProcessor::SMALL_ROOM_PATTERN[NUM_REFLECTIONS] = {
    {4.3f, 0.841f, 0.841f, -0.3f},
    {21.5f, 0.504f, 0.491f, 0.5f},
    {35.8f, 0.393f, 0.402f, -0.6f},
    {56.7f, 0.325f, 0.317f, 0.7f},
    {68.9f, 0.286f, 0.294f, -0.4f},
    {78.2f, 0.227f, 0.219f, 0.2f},
    {91.4f, 0.182f, 0.190f, -0.8f},
    {106.5f, 0.140f, 0.135f, 0.9f},
    {115.7f, 0.120f, 0.125f, -0.5f},
    {128.3f, 0.105f, 0.100f, 0.3f},
    {139.8f, 0.091f, 0.095f, -0.7f},
    {152.4f, 0.078f, 0.074f, 0.6f},
    {167.1f, 0.064f, 0.068f, -0.2f},
    {179.5f, 0.052f, 0.049f, 0.4f},
    {193.2f, 0.041f, 0.044f, -0.9f},
    {208.6f, 0.032f, 0.029f, 0.8f}
};

const EarlyReflectionsProcessor::Reflection
EarlyReflectionsProcessor::MEDIUM_ROOM_PATTERN[NUM_REFLECTIONS] = {
    {8.6f, 0.741f, 0.741f, -0.3f},
    {32.1f, 0.404f, 0.391f, 0.5f},
    {53.7f, 0.293f, 0.302f, -0.6f},
    {78.9f, 0.225f, 0.217f, 0.7f},
    {95.3f, 0.186f, 0.194f, -0.4f},
    {112.8f, 0.147f, 0.139f, 0.2f},
    {134.5f, 0.112f, 0.120f, -0.8f},
    {156.9f, 0.090f, 0.085f, 0.9f},
    {172.4f, 0.075f, 0.080f, -0.5f},
    {189.7f, 0.061f, 0.056f, 0.3f},
    {208.3f, 0.048f, 0.052f, -0.7f},
    {225.6f, 0.039f, 0.035f, 0.6f},
    {244.2f, 0.031f, 0.034f, -0.2f},
    {261.8f, 0.024f, 0.021f, 0.4f},
    {280.5f, 0.019f, 0.022f, -0.9f},
    {298.9f, 0.015f, 0.012f, 0.8f}
};

const EarlyReflectionsProcessor::Reflection
EarlyReflectionsProcessor::LARGE_ROOM_PATTERN[NUM_REFLECTIONS] = {
    {12.9f, 0.641f, 0.641f, -0.3f},
    {48.2f, 0.304f, 0.291f, 0.5f},
    {80.5f, 0.193f, 0.202f, -0.6f},
    {118.4f, 0.125f, 0.117f, 0.7f},
    {143.0f, 0.096f, 0.104f, -0.4f},
    {169.2f, 0.077f, 0.069f, 0.2f},
    {201.7f, 0.062f, 0.070f, -0.8f},
    {235.4f, 0.050f, 0.045f, 0.9f},
    {258.6f, 0.041f, 0.045f, -0.5f},
    {284.5f, 0.033f, 0.029f, 0.3f},
    {312.4f, 0.026f, 0.030f, -0.7f},
    {338.4f, 0.021f, 0.018f, 0.6f},
    {366.3f, 0.017f, 0.019f, -0.2f},
    {392.7f, 0.013f, 0.011f, 0.4f},
    {420.7f, 0.010f, 0.012f, -0.9f},
    {448.3f, 0.008f, 0.006f, 0.8f}
};

EarlyReflectionsProcessor::EarlyReflectionsProcessor() = default;
EarlyReflectionsProcessor::~EarlyReflectionsProcessor() = default;

void EarlyReflectionsProcessor::prepare(double sr, int bs)
{
    sampleRate = sr;
    blockSize = bs;

    // Initialize multi-tap delays
    int maxDelay = static_cast<int>(0.5 * sampleRate); // 500ms max
    earlyTapsL.setMaxDelay(maxDelay);
    earlyTapsR.setMaxDelay(maxDelay);

    // Initialize diffusion
    float srScale = static_cast<float>(sampleRate) / 44100.0f;
    int diffuserSizes[NUM_DIFFUSERS] = {341, 613};

    for (int i = 0; i < NUM_DIFFUSERS; ++i)
    {
        diffusersL[i].setSize(static_cast<int>(diffuserSizes[i] * srScale));
        diffusersR[i].setSize(static_cast<int>((diffuserSizes[i] + 23) * srScale));
        diffusersL[i].setFeedback(0.5f);
        diffusersR[i].setFeedback(0.5f);
    }

    // Initialize pre-delay buffer
    preDelayBufferSize = static_cast<int>(0.2 * sampleRate);
    preDelayBufferL.resize(preDelayBufferSize, 0.0f);
    preDelayBufferR.resize(preDelayBufferSize, 0.0f);
    preDelayWriteIndex = 0;

    updateReflectionPattern();
    updateFilters();
    reset();
}

void EarlyReflectionsProcessor::reset()
{
    earlyTapsL.clear();
    earlyTapsR.clear();

    for (auto& diff : diffusersL) diff.clear();
    for (auto& diff : diffusersR) diff.clear();

    std::fill(preDelayBufferL.begin(), preDelayBufferL.end(), 0.0f);
    std::fill(preDelayBufferR.begin(), preDelayBufferR.end(), 0.0f);
    preDelayWriteIndex = 0;
    preDelayReadIndex = 0;

    inputFilterL.reset();
    inputFilterR.reset();
    outputFilterL.reset();
    outputFilterR.reset();
}

void EarlyReflectionsProcessor::process(float* leftChannel, float* rightChannel, int numSamples)
{
    updateReflectionPattern();
    updateFilters();

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // Apply pre-delay with safety checks
        if (preDelayBufferL.empty() || preDelayBufferR.empty() || preDelayBufferSize <= 0)
        {
            leftChannel[sample] = 0.0f;
            rightChannel[sample] = 0.0f;
            continue;
        }

        // Ensure indices are within bounds
        if (preDelayWriteIndex >= preDelayBufferSize) preDelayWriteIndex = 0;
        if (preDelayReadIndex >= preDelayBufferSize) preDelayReadIndex = 0;

        preDelayBufferL[preDelayWriteIndex] = leftChannel[sample];
        preDelayBufferR[preDelayWriteIndex] = rightChannel[sample];

        float inputL = preDelayBufferL[preDelayReadIndex];
        float inputR = preDelayBufferR[preDelayReadIndex];

        preDelayWriteIndex = (preDelayWriteIndex + 1) % preDelayBufferSize;
        preDelayReadIndex = (preDelayReadIndex + 1) % preDelayBufferSize;

        // Apply input filtering
        inputL = inputFilterL.processSample(inputL);
        inputR = inputFilterR.processSample(inputR);

        // Process through multi-tap delays for early reflections
        float earlyL = earlyTapsL.process(inputL);
        float earlyR = earlyTapsR.process(inputR);

        // Apply diffusion
        for (int i = 0; i < NUM_DIFFUSERS; ++i)
        {
            earlyL = diffusersL[i].process(earlyL);
            earlyR = diffusersR[i].process(earlyR);
        }

        // Apply output filtering
        earlyL = outputFilterL.processSample(earlyL);
        earlyR = outputFilterR.processSample(earlyR);

        // Scale by room size
        earlyL *= roomSize;
        earlyR *= roomSize;

        leftChannel[sample] = earlyL;
        rightChannel[sample] = earlyR;
    }
}

void EarlyReflectionsProcessor::updateReflectionPattern()
{
    // Select pattern based on room size
    if (roomSize < 0.33f)
        currentPattern = SMALL_ROOM_PATTERN;
    else if (roomSize < 0.67f)
        currentPattern = MEDIUM_ROOM_PATTERN;
    else
        currentPattern = LARGE_ROOM_PATTERN;

    // Update multi-tap delays with current pattern
    earlyTapsL.clearTaps();
    earlyTapsR.clearTaps();

    for (int i = 0; i < NUM_REFLECTIONS; ++i)
    {
        const auto& reflection = currentPattern[i];
        int delaySamples = static_cast<int>(reflection.delay * sampleRate / 1000.0f);

        // Apply stereo spreading based on pan angle
        float leftGain = reflection.gainL * (1.0f + reflection.panAngle * 0.5f);
        float rightGain = reflection.gainR * (1.0f - reflection.panAngle * 0.5f);

        earlyTapsL.addTap(delaySamples, leftGain);
        earlyTapsR.addTap(delaySamples + static_cast<int>(reflection.panAngle * 10), rightGain);
    }

    // Update pre-delay
    int preDelaySamples = static_cast<int>(preDelay * sampleRate / 1000.0f);
    if (preDelayBufferSize > 0) {
        preDelayReadIndex = (preDelayWriteIndex - preDelaySamples + preDelayBufferSize) % preDelayBufferSize;
    } else {
        preDelayReadIndex = 0;
    }
}

void EarlyReflectionsProcessor::updateFilters()
{
    // Update input/output filters
    auto highpassCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, lowCutFreq);
    auto lowpassCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, highCutFreq);

    inputFilterL.coefficients = highpassCoeffs;
    inputFilterR.coefficients = highpassCoeffs;
    outputFilterL.coefficients = lowpassCoeffs;
    outputFilterR.coefficients = lowpassCoeffs;

    // Update diffusion based on parameter
    for (auto& diff : diffusersL)
        diff.setFeedback(0.3f + diffusion * 0.4f);
    for (auto& diff : diffusersR)
        diff.setFeedback(0.3f + diffusion * 0.4f);
}

// MultiTapDelay implementation
void EarlyReflectionsProcessor::MultiTapDelay::setMaxDelay(int maxSamples)
{
    bufferSize = maxSamples + 1;
    buffer.resize(bufferSize, 0.0f);
    writeIndex = 0;
}

void EarlyReflectionsProcessor::MultiTapDelay::addTap(int delaySamples, float gain)
{
    if (delaySamples >= 0 && delaySamples < bufferSize && bufferSize > 0)
        taps.push_back({delaySamples, gain});
}

void EarlyReflectionsProcessor::MultiTapDelay::clearTaps()
{
    taps.clear();
}

float EarlyReflectionsProcessor::MultiTapDelay::process(float input)
{
    if (buffer.empty() || bufferSize <= 0)
        return input;

    // Ensure writeIndex is within bounds
    if (writeIndex >= bufferSize) writeIndex = 0;

    buffer[writeIndex] = input;

    float output = 0.0f;
    for (const auto& tap : taps)
    {
        int readIdx = writeIndex - tap.delay;
        if (readIdx < 0) readIdx += bufferSize;

        // Additional safety check for readIdx
        if (readIdx >= 0 && readIdx < bufferSize) {
            output += buffer[readIdx] * tap.gain;
        }
    }

    writeIndex = (writeIndex + 1) % bufferSize;
    return output;
}

void EarlyReflectionsProcessor::MultiTapDelay::clear()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writeIndex = 0;
}

// AllpassDiffuser implementation
void EarlyReflectionsProcessor::AllpassDiffuser::setSize(int size)
{
    bufferSize = size;
    buffer.resize(size, 0.0f);
    bufferIndex = 0;
}

float EarlyReflectionsProcessor::AllpassDiffuser::process(float input)
{
    if (buffer.empty() || bufferSize <= 0)
        return input;

    // Ensure bufferIndex is within bounds
    if (bufferIndex >= bufferSize) bufferIndex = 0;

    float buffOut = buffer[bufferIndex];
    float output = -input + buffOut;
    buffer[bufferIndex] = input + (buffOut * feedback);

    bufferIndex = (bufferIndex + 1) % bufferSize;
    return output;
}

void EarlyReflectionsProcessor::AllpassDiffuser::clear()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    bufferIndex = 0;
}