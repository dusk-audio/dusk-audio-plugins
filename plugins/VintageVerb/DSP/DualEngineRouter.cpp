/*
  ==============================================================================

    DualEngineRouter.cpp - Implementation of dual engine routing system

  ==============================================================================
*/

#include "DualEngineRouter.h"
#include <cmath>

DualEngineRouter::DualEngineRouter()
{
    smoothEngineMix.reset(50);
    smoothCrossFeed.reset(50);
    smoothSeriesBlend.reset(50);
    smoothWidth.reset(50);
}

void DualEngineRouter::prepare(double sr, int maxBlockSize)
{
    sampleRate = sr;
    blockSize = maxBlockSize;

    // Prepare internal buffers
    bufferA.setSize(2, maxBlockSize);
    bufferB.setSize(2, maxBlockSize);
    tempBuffer.setSize(2, maxBlockSize);
    feedbackBuffer.setSize(2, maxBlockSize);

    // Prepare delay lines for cross-feed
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sr;
    spec.maximumBlockSize = static_cast<juce::uint32>(maxBlockSize);
    spec.numChannels = 1;

    crossDelayL.prepare(spec);
    crossDelayR.prepare(spec);
    crossDelayL.setMaximumDelayInSamples(192000);
    crossDelayR.setMaximumDelayInSamples(192000);

    stereoProcessor.prepare(spec);

    // Setup smooth value parameters
    smoothEngineMix.reset(sr, 0.05);  // 50ms smoothing
    smoothCrossFeed.reset(sr, 0.05);
    smoothSeriesBlend.reset(sr, 0.05);
    smoothWidth.reset(sr, 0.02);      // Faster for width

    reset();
}

void DualEngineRouter::reset()
{
    bufferA.clear();
    bufferB.clear();
    tempBuffer.clear();
    feedbackBuffer.clear();

    crossDelayL.reset();
    crossDelayR.reset();

    stereoProcessor.reset();

    modMatrix.reset();
}

void DualEngineRouter::setEngines(ReverbEngine* a, ReverbEngine* b)
{
    engineA = a;
    engineB = b;
}

void DualEngineRouter::process(juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (!engineA || !engineB) return;

    const int numChannels = buffer.getNumChannels();
    if (numChannels < 2) return;

    auto* leftChannel = buffer.getWritePointer(0);
    auto* rightChannel = buffer.getWritePointer(1);

    processStereo(leftChannel, rightChannel, leftChannel, rightChannel, numSamples);
}

void DualEngineRouter::processStereo(float* leftIn, float* rightIn,
                                    float* leftOut, float* rightOut, int numSamples)
{
    if (!engineA || !engineB) return;

    // Process based on routing mode
    switch (currentMode)
    {
        case Series:
            processSeries(leftIn, rightIn, leftOut, rightOut, numSamples);
            break;

        case Parallel:
            processParallel(leftIn, rightIn, leftOut, rightOut, numSamples);
            break;

        case AtoB:
            processAtoB(leftIn, rightIn, leftOut, rightOut, numSamples);
            break;

        case BtoA:
            processBtoA(leftIn, rightIn, leftOut, rightOut, numSamples);
            break;

        case Nested:
            processNested(leftIn, rightIn, leftOut, rightOut, numSamples);
            break;

        case CrossFeed:
            processCrossFeed(leftIn, rightIn, leftOut, rightOut, numSamples);
            break;
    }

    // Apply stereo width processing
    for (int i = 0; i < numSamples; ++i)
    {
        float width = smoothWidth.getNextValue();
        stereoProcessor.process(leftOut[i], rightOut[i], width);
    }
}

void DualEngineRouter::processSeries(float* leftIn, float* rightIn,
                                    float* leftOut, float* rightOut, int numSamples)
{
    // Copy input to bufferA
    bufferA.clear();
    bufferA.copyFrom(0, 0, leftIn, numSamples);
    bufferA.copyFrom(1, 0, rightIn, numSamples);

    // Process through engine A
    engineA->process(bufferA, numSamples);

    // Get the mix parameter smoothed
    float blend = smoothSeriesBlend.getNextValue();

    // Copy engine A output to temp buffer
    tempBuffer.clear();
    tempBuffer.copyFrom(0, 0, bufferA.getReadPointer(0), numSamples);
    tempBuffer.copyFrom(1, 0, bufferA.getReadPointer(1), numSamples);

    // Process engine A output through engine B
    engineB->process(bufferA, numSamples);

    // Mix between A only and A->B based on series blend
    float mix = smoothEngineMix.getNextValue();

    for (int i = 0; i < numSamples; ++i)
    {
        float aLeft = tempBuffer.getSample(0, i);
        float aRight = tempBuffer.getSample(1, i);
        float bLeft = bufferA.getSample(0, i);
        float bRight = bufferA.getSample(1, i);

        // Blend between A output and A->B output
        float blendedLeft = aLeft * (1.0f - blend) + bLeft * blend;
        float blendedRight = aRight * (1.0f - blend) + bRight * blend;

        // Mix with dry signal based on engine mix
        leftOut[i] = leftIn[i] * (1.0f - mix) + blendedLeft * mix;
        rightOut[i] = rightIn[i] * (1.0f - mix) + blendedRight * mix;

        // Apply gain compensation
        applyGainCompensation(leftOut[i], Series);
        applyGainCompensation(rightOut[i], Series);
    }
}

void DualEngineRouter::processParallel(float* leftIn, float* rightIn,
                                      float* leftOut, float* rightOut, int numSamples)
{
    // Process input through both engines in parallel
    bufferA.clear();
    bufferA.copyFrom(0, 0, leftIn, numSamples);
    bufferA.copyFrom(1, 0, rightIn, numSamples);

    bufferB.clear();
    bufferB.copyFrom(0, 0, leftIn, numSamples);
    bufferB.copyFrom(1, 0, rightIn, numSamples);

    // Process both engines
    engineA->process(bufferA, numSamples);
    engineB->process(bufferB, numSamples);

    // Mix the outputs
    for (int i = 0; i < numSamples; ++i)
    {
        float mix = smoothEngineMix.getNextValue();

        float aLeft = bufferA.getSample(0, i);
        float aRight = bufferA.getSample(1, i);
        float bLeft = bufferB.getSample(0, i);
        float bRight = bufferB.getSample(1, i);

        // Crossfade between engines
        leftOut[i] = aLeft * (1.0f - mix) + bLeft * mix;
        rightOut[i] = aRight * (1.0f - mix) + bRight * mix;

        // Apply gain compensation
        applyGainCompensation(leftOut[i], Parallel);
        applyGainCompensation(rightOut[i], Parallel);
    }
}

void DualEngineRouter::processAtoB(float* leftIn, float* rightIn,
                                  float* leftOut, float* rightOut, int numSamples)
{
    // Process input through engine A first
    bufferA.clear();
    bufferA.copyFrom(0, 0, leftIn, numSamples);
    bufferA.copyFrom(1, 0, rightIn, numSamples);

    engineA->process(bufferA, numSamples);

    // Use engine A output to modulate engine B input
    bufferB.clear();
    float crossFeed = smoothCrossFeed.getNextValue();

    for (int i = 0; i < numSamples; ++i)
    {
        float aLeft = bufferA.getSample(0, i);
        float aRight = bufferA.getSample(1, i);

        // Modulate input with engine A output
        float modLeft = modMatrix.process(leftIn[i], aLeft, crossFeed);
        float modRight = modMatrix.process(rightIn[i], aRight, crossFeed);

        bufferB.setSample(0, i, modLeft);
        bufferB.setSample(1, i, modRight);
    }

    // Process modulated signal through engine B
    engineB->process(bufferB, numSamples);

    // Mix outputs
    for (int i = 0; i < numSamples; ++i)
    {
        float mix = smoothEngineMix.getNextValue();

        float aLeft = bufferA.getSample(0, i);
        float aRight = bufferA.getSample(1, i);
        float bLeft = bufferB.getSample(0, i);
        float bRight = bufferB.getSample(1, i);

        leftOut[i] = aLeft * (1.0f - mix) + bLeft * mix;
        rightOut[i] = aRight * (1.0f - mix) + bRight * mix;

        applyGainCompensation(leftOut[i], AtoB);
        applyGainCompensation(rightOut[i], AtoB);
    }
}

void DualEngineRouter::processBtoA(float* leftIn, float* rightIn,
                                  float* leftOut, float* rightOut, int numSamples)
{
    // Process input through engine B first
    bufferB.clear();
    bufferB.copyFrom(0, 0, leftIn, numSamples);
    bufferB.copyFrom(1, 0, rightIn, numSamples);

    engineB->process(bufferB, numSamples);

    // Use engine B output to modulate engine A input
    bufferA.clear();
    float crossFeed = smoothCrossFeed.getNextValue();

    for (int i = 0; i < numSamples; ++i)
    {
        float bLeft = bufferB.getSample(0, i);
        float bRight = bufferB.getSample(1, i);

        // Modulate input with engine B output
        float modLeft = modMatrix.process(leftIn[i], bLeft, crossFeed);
        float modRight = modMatrix.process(rightIn[i], bRight, crossFeed);

        bufferA.setSample(0, i, modLeft);
        bufferA.setSample(1, i, modRight);
    }

    // Process modulated signal through engine A
    engineA->process(bufferA, numSamples);

    // Mix outputs
    for (int i = 0; i < numSamples; ++i)
    {
        float mix = smoothEngineMix.getNextValue();

        float aLeft = bufferA.getSample(0, i);
        float aRight = bufferA.getSample(1, i);
        float bLeft = bufferB.getSample(0, i);
        float bRight = bufferB.getSample(1, i);

        leftOut[i] = bLeft * (1.0f - mix) + aLeft * mix;
        rightOut[i] = bRight * (1.0f - mix) + aRight * mix;

        applyGainCompensation(leftOut[i], BtoA);
        applyGainCompensation(rightOut[i], BtoA);
    }
}

void DualEngineRouter::processNested(float* leftIn, float* rightIn,
                                    float* leftOut, float* rightOut, int numSamples)
{
    // Process engine A first
    bufferA.clear();
    bufferA.copyFrom(0, 0, leftIn, numSamples);
    bufferA.copyFrom(1, 0, rightIn, numSamples);

    engineA->process(bufferA, numSamples);

    // Feed engine A output into engine B's feedback path
    float crossFeed = smoothCrossFeed.getNextValue();

    for (int i = 0; i < numSamples; ++i)
    {
        float aLeft = bufferA.getSample(0, i);
        float aRight = bufferA.getSample(1, i);

        // Add delayed feedback
        crossDelayL.pushSample(0, aLeft * crossFeed);
        crossDelayR.pushSample(0, aRight * crossFeed);

        float feedbackL = crossDelayL.popSample(0, static_cast<int>(sampleRate * 0.037f));
        float feedbackR = crossDelayR.popSample(0, static_cast<int>(sampleRate * 0.041f));

        bufferB.setSample(0, i, leftIn[i] + feedbackL);
        bufferB.setSample(1, i, rightIn[i] + feedbackR);
    }

    // Process through engine B with nested feedback
    engineB->process(bufferB, numSamples);

    // Mix outputs
    for (int i = 0; i < numSamples; ++i)
    {
        float mix = smoothEngineMix.getNextValue();

        float aLeft = bufferA.getSample(0, i);
        float aRight = bufferA.getSample(1, i);
        float bLeft = bufferB.getSample(0, i);
        float bRight = bufferB.getSample(1, i);

        leftOut[i] = aLeft * (1.0f - mix) + bLeft * mix;
        rightOut[i] = aRight * (1.0f - mix) + bRight * mix;

        applyGainCompensation(leftOut[i], Nested);
        applyGainCompensation(rightOut[i], Nested);
    }
}

void DualEngineRouter::processCrossFeed(float* leftIn, float* rightIn,
                                       float* leftOut, float* rightOut, int numSamples)
{
    float crossFeed = smoothCrossFeed.getNextValue();

    // Process with cross-coupled feedback
    for (int block = 0; block < numSamples; block += 32)
    {
        int samplesThisBlock = juce::jmin(32, numSamples - block);

        // Copy input to both buffers
        for (int i = 0; i < samplesThisBlock; ++i)
        {
            bufferA.setSample(0, i, leftIn[block + i]);
            bufferA.setSample(1, i, rightIn[block + i]);
            bufferB.setSample(0, i, leftIn[block + i]);
            bufferB.setSample(1, i, rightIn[block + i]);
        }

        // Add cross-feedback from previous block
        for (int i = 0; i < samplesThisBlock; ++i)
        {
            float fbLeft = feedbackBuffer.getSample(0, i) * crossFeed;
            float fbRight = feedbackBuffer.getSample(1, i) * crossFeed;

            bufferA.setSample(0, i, bufferA.getSample(0, i) + fbRight * 0.3f);
            bufferA.setSample(1, i, bufferA.getSample(1, i) + fbLeft * 0.3f);

            bufferB.setSample(0, i, bufferB.getSample(0, i) - fbLeft * 0.3f);
            bufferB.setSample(1, i, bufferB.getSample(1, i) - fbRight * 0.3f);
        }

        // Process both engines
        juce::AudioBuffer<float> tempA(2, samplesThisBlock);
        juce::AudioBuffer<float> tempB(2, samplesThisBlock);

        tempA.copyFrom(0, 0, bufferA, 0, 0, samplesThisBlock);
        tempA.copyFrom(1, 0, bufferA, 1, 0, samplesThisBlock);
        tempB.copyFrom(0, 0, bufferB, 0, 0, samplesThisBlock);
        tempB.copyFrom(1, 0, bufferB, 1, 0, samplesThisBlock);

        engineA->process(tempA, samplesThisBlock);
        engineB->process(tempB, samplesThisBlock);

        // Store feedback for next block
        feedbackBuffer.clear();
        for (int i = 0; i < samplesThisBlock; ++i)
        {
            feedbackBuffer.setSample(0, i,
                tempA.getSample(0, i) - tempB.getSample(0, i));
            feedbackBuffer.setSample(1, i,
                tempA.getSample(1, i) - tempB.getSample(1, i));
        }

        // Mix outputs
        for (int i = 0; i < samplesThisBlock; ++i)
        {
            float mix = smoothEngineMix.getNextValue();

            float aLeft = tempA.getSample(0, i);
            float aRight = tempA.getSample(1, i);
            float bLeft = tempB.getSample(0, i);
            float bRight = tempB.getSample(1, i);

            leftOut[block + i] = aLeft * (1.0f - mix) + bLeft * mix;
            rightOut[block + i] = aRight * (1.0f - mix) + bRight * mix;

            applyGainCompensation(leftOut[block + i], CrossFeed);
            applyGainCompensation(rightOut[block + i], CrossFeed);
        }
    }
}

void DualEngineRouter::setRoutingMode(RoutingMode mode)
{
    currentMode = mode;
}

void DualEngineRouter::setEngineMix(float mix)
{
    engineMix = juce::jlimit(0.0f, 1.0f, mix);
    smoothEngineMix.setTargetValue(engineMix);
}

void DualEngineRouter::setCrossFeedAmount(float amount)
{
    crossFeedAmount = juce::jlimit(0.0f, 1.0f, amount);
    smoothCrossFeed.setTargetValue(crossFeedAmount);
}

void DualEngineRouter::setSeriesBlend(float blend)
{
    seriesBlend = juce::jlimit(0.0f, 1.0f, blend);
    smoothSeriesBlend.setTargetValue(seriesBlend);
}

void DualEngineRouter::setWidth(float width)
{
    stereoWidth = juce::jlimit(0.0f, 2.0f, width);
    smoothWidth.setTargetValue(stereoWidth);
}

// ModulationMatrix implementation
float DualEngineRouter::ModulationMatrix::process(float inputA, float inputB, float modAmount)
{
    // Simple ring modulation with LFO
    lfo1Phase += 0.3f * 2.0f * juce::MathConstants<float>::pi / sampleRate;
    if (lfo1Phase > juce::MathConstants<float>::twoPi)
        lfo1Phase -= juce::MathConstants<float>::twoPi;

    float lfo = std::sin(lfo1Phase);
    float modulated = inputA + (inputB * lfo * modAmount);

    return std::tanh(modulated);  // Soft clip
}

void DualEngineRouter::ModulationMatrix::reset()
{
    lfo1Phase = 0.0f;
    lfo2Phase = 0.0f;
}

// StereoProcessor implementation
void DualEngineRouter::StereoProcessor::process(float& left, float& right, float width)
{
    // Haas effect for width
    float mid = (left + right) * 0.5f;
    float side = (left - right) * 0.5f * width;

    // Add micro delays for width
    delayL.pushSample(0, side);
    delayR.pushSample(0, side);

    float delayedL = delayL.popSample(0, 0.2f);
    float delayedR = delayR.popSample(0, 0.5f);

    // Apply width with filtering
    lastL = lastL * 0.95f + delayedL * 0.05f;
    lastR = lastR * 0.95f + delayedR * 0.05f;

    left = mid + side + lastL * 0.1f;
    right = mid - side - lastR * 0.1f;
}

void DualEngineRouter::StereoProcessor::prepare(const juce::dsp::ProcessSpec& spec)
{
    delayL.prepare(spec);
    delayR.prepare(spec);
}

void DualEngineRouter::StereoProcessor::reset()
{
    delayL.reset();
    delayR.reset();
    lastL = 0.0f;
    lastR = 0.0f;
}

// Helper functions
float DualEngineRouter::softClip(float input)
{
    return std::tanh(input * 0.7f) * 1.43f;
}

void DualEngineRouter::applyGainCompensation(float& sample, RoutingMode mode)
{
    // Apply mode-specific gain compensation to maintain consistent levels
    switch (mode)
    {
        case Series:
            sample *= 0.7f;  // Series can build up
            break;
        case Parallel:
            sample *= 0.85f;  // Parallel sum
            break;
        case CrossFeed:
            sample *= 0.75f;  // Complex feedback
            break;
        default:
            sample *= 0.9f;
            break;
    }

    // Final soft clipping for safety
    sample = softClip(sample);
}