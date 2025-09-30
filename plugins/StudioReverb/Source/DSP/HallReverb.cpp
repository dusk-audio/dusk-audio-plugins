#include "HallReverb.h"

HallReverbProcessor::HallReverbProcessor()
{
}

HallReverbProcessor::~HallReverbProcessor() = default;

void HallReverbProcessor::prepare(double sr, int bs)
{
    sampleRate = sr;
    blockSize = bs;

    // Scale tuning values to current sample rate
    float sampleRateScale = static_cast<float>(sampleRate) / 44100.0f;

    // Initialize comb filters
    for (int i = 0; i < NUM_COMBS; ++i)
    {
        int sizeL = static_cast<int>(COMB_TUNING[i] * sampleRateScale);
        int sizeR = static_cast<int>((COMB_TUNING[i] + STEREO_SPREAD) * sampleRateScale);

        combFiltersL[i].setSize(sizeL);
        combFiltersR[i].setSize(sizeR);
        combFiltersL[i].setDamp(damping);
        combFiltersR[i].setDamp(damping);
    }

    // Initialize allpass filters
    for (int i = 0; i < NUM_ALLPASSES; ++i)
    {
        int sizeL = static_cast<int>(ALLPASS_TUNING[i] * sampleRateScale);
        int sizeR = static_cast<int>((ALLPASS_TUNING[i] + STEREO_SPREAD) * sampleRateScale);

        allpassFiltersL[i].setSize(sizeL);
        allpassFiltersR[i].setSize(sizeR);
        allpassFiltersL[i].setFeedback(0.5f);
        allpassFiltersR[i].setFeedback(0.5f);
    }

    // Initialize pre-delay
    int preDelaySamples = static_cast<int>(preDelay * sampleRate / 1000.0f);
    preDelayL.setDelay(preDelaySamples);
    preDelayR.setDelay(preDelaySamples);

    // Initialize filters
    lowShelfL.reset();
    lowShelfR.reset();
    highShelfL.reset();
    highShelfR.reset();

    updateFilters();
    reset();
}

void HallReverbProcessor::reset()
{
    for (auto& comb : combFiltersL) comb.clear();
    for (auto& comb : combFiltersR) comb.clear();
    for (auto& allpass : allpassFiltersL) allpass.clear();
    for (auto& allpass : allpassFiltersR) allpass.clear();

    preDelayL.clear();
    preDelayR.clear();

    lowShelfL.reset();
    lowShelfR.reset();
    highShelfL.reset();
    highShelfR.reset();

    modPhase = 0.0f;
}

void HallReverbProcessor::process(float* leftChannel, float* rightChannel, int numSamples)
{
    updateFilters();

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // Input with pre-delay
        float inputL = preDelayL.process(leftChannel[sample]);
        float inputR = preDelayR.process(rightChannel[sample]);

        // Apply input filtering
        inputL = lowShelfL.processSample(inputL);
        inputR = lowShelfR.processSample(inputR);
        inputL = highShelfL.processSample(inputL);
        inputR = highShelfR.processSample(inputR);

        // Apply modulation
        if (modulation > 0.0f)
        {
            float modAmount = std::sin(modPhase) * modulation * 0.001f;
            modPhase += 2.0f * juce::MathConstants<float>::pi * modRate / static_cast<float>(sampleRate);
            if (modPhase > 2.0f * juce::MathConstants<float>::pi)
                modPhase -= 2.0f * juce::MathConstants<float>::pi;

            inputL *= (1.0f + modAmount);
            inputR *= (1.0f + modAmount);
        }

        // Process through parallel comb filters
        float outputL = 0.0f;
        float outputR = 0.0f;

        for (int i = 0; i < NUM_COMBS; ++i)
        {
            outputL += combFiltersL[i].process(inputL);
            outputR += combFiltersR[i].process(inputR);
        }

        outputL /= static_cast<float>(NUM_COMBS);
        outputR /= static_cast<float>(NUM_COMBS);

        // Process through series allpass filters
        for (int i = 0; i < NUM_ALLPASSES; ++i)
        {
            outputL = allpassFiltersL[i].process(outputL);
            outputR = allpassFiltersR[i].process(outputR);
        }

        // Scale by room size
        outputL *= roomSize;
        outputR *= roomSize;

        // Mix with original (this will be handled by the main processor)
        leftChannel[sample] = outputL;
        rightChannel[sample] = outputR;
    }
}

void HallReverbProcessor::updateFilters()
{
    // Update comb filter parameters based on decay time
    float feedback = 0.84f + (decay / 10.0f) * 0.14f; // Scale feedback from 0.84 to 0.98

    for (auto& comb : combFiltersL)
    {
        comb.setFeedback(feedback);
        comb.setDamp(damping);
    }

    for (auto& comb : combFiltersR)
    {
        comb.setFeedback(feedback);
        comb.setDamp(damping);
    }

    // Update allpass diffusion
    for (auto& allpass : allpassFiltersL)
        allpass.setFeedback(diffusion * 0.5f);

    for (auto& allpass : allpassFiltersR)
        allpass.setFeedback(diffusion * 0.5f);

    // Update pre-delay
    int preDelaySamples = static_cast<int>(preDelay * sampleRate / 1000.0f);
    preDelayL.setDelay(preDelaySamples);
    preDelayR.setDelay(preDelaySamples);

    // Update shelf filters
    auto lowShelfCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, lowCutFreq);
    auto highShelfCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, highCutFreq);

    lowShelfL.coefficients = lowShelfCoeffs;
    lowShelfR.coefficients = lowShelfCoeffs;
    highShelfL.coefficients = highShelfCoeffs;
    highShelfR.coefficients = highShelfCoeffs;
}

// AllpassFilter implementation
void HallReverbProcessor::AllpassFilter::setSize(int size)
{
    bufferSize = size;
    buffer.resize(size, 0.0f);
    bufferIndex = 0;
}

float HallReverbProcessor::AllpassFilter::process(float input)
{
    if (buffer.empty() || bufferSize <= 0)
        return input;

    // Ensure bufferIndex is within bounds
    if (bufferIndex >= bufferSize)
        bufferIndex = 0;

    float buffOut = buffer[bufferIndex];
    float output = -input + buffOut;
    buffer[bufferIndex] = input + (buffOut * feedback);

    if (++bufferIndex >= bufferSize)
        bufferIndex = 0;

    return output;
}

void HallReverbProcessor::AllpassFilter::clear()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    bufferIndex = 0;
}

// CombFilter implementation
void HallReverbProcessor::CombFilter::setSize(int size)
{
    bufferSize = size;
    buffer.resize(size, 0.0f);
    bufferIndex = 0;
}

float HallReverbProcessor::CombFilter::process(float input)
{
    if (buffer.empty() || bufferSize <= 0)
        return input;

    // Ensure bufferIndex is within bounds
    if (bufferIndex >= bufferSize)
        bufferIndex = 0;

    float output = buffer[bufferIndex];
    filterStore = (output * damp2) + (filterStore * damp1);
    buffer[bufferIndex] = input + (filterStore * feedback);

    if (++bufferIndex >= bufferSize)
        bufferIndex = 0;

    return output;
}

void HallReverbProcessor::CombFilter::clear()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    filterStore = 0.0f;
    bufferIndex = 0;
}

// DelayLine implementation
void HallReverbProcessor::DelayLine::setDelay(int samples)
{
    bufferSize = samples + 1;
    if (bufferSize <= 0) {
        bufferSize = 1;
    }

    buffer.resize(bufferSize, 0.0f);

    // Safe modulo operation
    if (bufferSize > 0) {
        readIndex = (writeIndex - samples + bufferSize) % bufferSize;
    } else {
        readIndex = 0;
    }
}

float HallReverbProcessor::DelayLine::process(float input)
{
    if (buffer.empty() || bufferSize <= 0)
        return input;

    // Ensure indices are within bounds
    if (readIndex >= bufferSize) readIndex = 0;
    if (writeIndex >= bufferSize) writeIndex = 0;

    float output = buffer[readIndex];
    buffer[writeIndex] = input;

    writeIndex = (writeIndex + 1) % bufferSize;
    readIndex = (readIndex + 1) % bufferSize;

    return output;
}

void HallReverbProcessor::DelayLine::clear()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writeIndex = 0;
    readIndex = 0;
}