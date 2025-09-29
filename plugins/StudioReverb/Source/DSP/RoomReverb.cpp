#include "RoomReverb.h"
#include <cmath>

RoomReverbProcessor::RoomReverbProcessor() = default;
RoomReverbProcessor::~RoomReverbProcessor() = default;

void RoomReverbProcessor::prepare(double sr, int bs)
{
    sampleRate = sr;
    blockSize = bs;

    // Initialize pre-delay
    int maxPreDelay = static_cast<int>(0.2 * sampleRate); // 200ms max
    preDelayL.setMaxDelay(maxPreDelay);
    preDelayR.setMaxDelay(maxPreDelay);

    // Initialize early reflections delay
    earlyReflectionsL.setMaxDelay(static_cast<int>(0.15 * sampleRate));
    earlyReflectionsR.setMaxDelay(static_cast<int>(0.15 * sampleRate));

    // Initialize diffusion allpasses
    float srScale = static_cast<float>(sampleRate) / 44100.0f;
    int diffuserSizes[NUM_DIFFUSERS] = {113, 337, 613, 797};

    for (int i = 0; i < NUM_DIFFUSERS; ++i)
    {
        diffusersL[i].setSize(static_cast<int>(diffuserSizes[i] * srScale));
        diffusersR[i].setSize(static_cast<int>((diffuserSizes[i] + 23) * srScale));
        diffusersL[i].setFeedback(0.5f);
        diffusersR[i].setFeedback(0.5f);
    }

    // Initialize late reverb delays
    for (int i = 0; i < NUM_DELAYS; ++i)
    {
        lateDelaysL[i].setMaxDelay(static_cast<int>(LATE_DELAY_TIMES[i] * srScale * 2));
        lateDelaysR[i].setMaxDelay(static_cast<int>((LATE_DELAY_TIMES[i] + 37) * srScale * 2));
        lateDelaysL[i].setDelay(static_cast<int>(LATE_DELAY_TIMES[i] * srScale));
        lateDelaysR[i].setDelay(static_cast<int>((LATE_DELAY_TIMES[i] + 37) * srScale));
    }

    updateFilters();
    reset();
}

void RoomReverbProcessor::reset()
{
    preDelayL.clear();
    preDelayR.clear();
    earlyReflectionsL.clear();
    earlyReflectionsR.clear();

    for (auto& diff : diffusersL) diff.clear();
    for (auto& diff : diffusersR) diff.clear();
    for (auto& delay : lateDelaysL) delay.clear();
    for (auto& delay : lateDelaysR) delay.clear();

    lowCutFilterL.reset();
    lowCutFilterR.reset();
    highCutFilterL.reset();
    highCutFilterR.reset();

    for (auto& filter : dampingFiltersL) filter.reset();
    for (auto& filter : dampingFiltersR) filter.reset();
}

void RoomReverbProcessor::process(float* leftChannel, float* rightChannel, int numSamples)
{
    updateFilters();

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // Apply pre-delay
        float inputL = preDelayL.process(leftChannel[sample]);
        float inputR = preDelayR.process(rightChannel[sample]);

        // Apply input filtering
        inputL = lowCutFilterL.processSample(inputL);
        inputR = lowCutFilterR.processSample(inputR);
        inputL = highCutFilterL.processSample(inputL);
        inputR = highCutFilterR.processSample(inputR);

        // Process early reflections
        float earlyL = 0.0f, earlyR = 0.0f;
        processEarlyReflections(inputL, inputR, earlyL, earlyR);

        // Mix early into input for late processing
        float lateInputL = inputL + earlyL * earlyMix;
        float lateInputR = inputR + earlyR * earlyMix;

        // Process through diffusion network
        for (int i = 0; i < NUM_DIFFUSERS; ++i)
        {
            lateInputL = diffusersL[i].process(lateInputL);
            lateInputR = diffusersR[i].process(lateInputR);
        }

        // Process late reverb
        float lateL = 0.0f, lateR = 0.0f;
        processLateReverb(lateInputL, lateInputR, lateL, lateR);

        // Output is the reverb signal (wet)
        leftChannel[sample] = earlyL * earlyMix + lateL * lateMix;
        rightChannel[sample] = earlyR * earlyMix + lateR * lateMix;
    }
}

void RoomReverbProcessor::processEarlyReflections(float inputL, float inputR,
                                                  float& outputL, float& outputR)
{
    // Simple early reflections using tapped delays
    outputL = outputR = 0.0f;

    for (int i = 0; i < NUM_EARLY_TAPS; ++i)
    {
        int delaySamples = static_cast<int>(EARLY_TAP_DELAYS[i] * sampleRate / 1000.0f);
        float tapL = earlyReflectionsL.tap(delaySamples);
        float tapR = earlyReflectionsR.tap(delaySamples);

        outputL += tapL * EARLY_TAP_GAINS[i];
        outputR += tapR * EARLY_TAP_GAINS[i];
    }

    // Update delay lines
    earlyReflectionsL.process(inputL);
    earlyReflectionsR.process(inputR);
}

void RoomReverbProcessor::processLateReverb(float inputL, float inputR,
                                           float& outputL, float& outputR)
{
    // FDN (Feedback Delay Network) processing

    // Read from delays
    for (int i = 0; i < NUM_DELAYS; ++i)
    {
        delayOutputsL[i] = lateDelaysL[i].process(0.0f);
        delayOutputsR[i] = lateDelaysR[i].process(0.0f);
    }

    // Apply feedback matrix and write back to delays
    outputL = outputR = 0.0f;

    for (int i = 0; i < NUM_DELAYS; ++i)
    {
        float sumL = inputL * 0.25f;
        float sumR = inputR * 0.25f;

        for (int j = 0; j < NUM_DELAYS; ++j)
        {
            sumL += delayOutputsL[j] * FDN_MATRIX[i][j] * 0.5f;
            sumR += delayOutputsR[j] * FDN_MATRIX[i][j] * 0.5f;
        }

        // Apply damping
        sumL = dampingFiltersL[i].processSample(sumL) * (0.85f + decay * 0.14f);
        sumR = dampingFiltersR[i].processSample(sumR) * (0.85f + decay * 0.14f);

        // Write to delay
        lateDelaysL[i].process(sumL);
        lateDelaysR[i].process(sumR);

        // Accumulate output
        outputL += delayOutputsL[i];
        outputR += delayOutputsR[i];
    }

    outputL *= roomSize / static_cast<float>(NUM_DELAYS);
    outputR *= roomSize / static_cast<float>(NUM_DELAYS);
}

void RoomReverbProcessor::updateFilters()
{
    // Update pre-delay
    int preDelaySamples = static_cast<int>(preDelay * sampleRate / 1000.0f);
    preDelayL.setDelay(preDelaySamples);
    preDelayR.setDelay(preDelaySamples);

    // Update input filters
    auto lowCutCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, lowCutFreq);
    auto highCutCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, highCutFreq);

    lowCutFilterL.coefficients = lowCutCoeffs;
    lowCutFilterR.coefficients = lowCutCoeffs;
    highCutFilterL.coefficients = highCutCoeffs;
    highCutFilterR.coefficients = highCutCoeffs;

    // Update damping filters
    float dampFreq = 20000.0f * (1.0f - damping);
    auto dampCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, dampFreq);

    for (auto& filter : dampingFiltersL)
        filter.coefficients = dampCoeffs;
    for (auto& filter : dampingFiltersR)
        filter.coefficients = dampCoeffs;

    // Update diffusion
    for (auto& diff : diffusersL)
        diff.setFeedback(diffusion * 0.7f);
    for (auto& diff : diffusersR)
        diff.setFeedback(diffusion * 0.7f);
}

// SimpleDelay implementation
void RoomReverbProcessor::SimpleDelay::setMaxDelay(int maxSamples)
{
    bufferSize = maxSamples + 1;
    buffer.resize(bufferSize, 0.0f);
    writeIndex = 0;
}

void RoomReverbProcessor::SimpleDelay::setDelay(int samples)
{
    if (bufferSize > 0)
        currentDelay = std::min(samples, bufferSize - 1);
    else
        currentDelay = 0;
}

float RoomReverbProcessor::SimpleDelay::process(float input)
{
    if (bufferSize == 0 || buffer.empty())
        return 0.0f;

    buffer[writeIndex] = input;

    int readIdx = writeIndex - currentDelay;
    if (readIdx < 0) readIdx += bufferSize;

    float output = buffer[readIdx];

    writeIndex = (writeIndex + 1) % bufferSize;
    return output;
}

float RoomReverbProcessor::SimpleDelay::tap(int delaySamples) const
{
    if (bufferSize == 0 || buffer.empty())
        return 0.0f;

    int readIdx = writeIndex - delaySamples;
    if (readIdx < 0) readIdx += bufferSize;
    return buffer[readIdx];
}

void RoomReverbProcessor::SimpleDelay::clear()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writeIndex = 0;
}

// DiffusionAllpass implementation
void RoomReverbProcessor::DiffusionAllpass::setSize(int size)
{
    bufferSize = size;
    buffer.resize(size, 0.0f);
    bufferIndex = 0;
}

float RoomReverbProcessor::DiffusionAllpass::process(float input)
{
    if (bufferSize == 0 || buffer.empty())
        return input;

    float buffOut = buffer[bufferIndex];
    float output = -input + buffOut;
    buffer[bufferIndex] = input + (buffOut * feedback);

    bufferIndex = (bufferIndex + 1) % bufferSize;
    return output;
}

void RoomReverbProcessor::DiffusionAllpass::clear()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    bufferIndex = 0;
}