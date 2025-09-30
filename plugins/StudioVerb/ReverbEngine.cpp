/*
  ==============================================================================

    ReverbEngine.cpp
    Studio Verb - Reverb DSP Engine
    Copyright (c) 2024 Luna CO. Audio

  ==============================================================================
*/

#include "ReverbEngine.h"

//==============================================================================
ReverbEngine::ReverbEngine()
    : randomGenerator(std::random_device{}())
{
    // Initialize early reflection taps
    for (auto& tap : earlyTapsL)
        tap.setMaximumDelayInSamples(9600); // 200ms at 48kHz

    for (auto& tap : earlyTapsR)
        tap.setMaximumDelayInSamples(9600);

    // Initialize with room algorithm
    configureRoomAlgorithm();
}

//==============================================================================
void ReverbEngine::prepare(const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    blockSize = static_cast<int>(spec.maximumBlockSize);

    // Prepare predelay
    predelayL.prepare(spec);
    predelayR.prepare(spec);
    predelayL.setMaximumDelayInSamples(static_cast<int>(sampleRate * 0.2)); // 200ms max
    predelayR.setMaximumDelayInSamples(static_cast<int>(sampleRate * 0.2));

    // Prepare comb filters
    for (auto& comb : combFiltersL)
    {
        comb.prepare(spec);
        comb.setSize(static_cast<int>(sampleRate * 2.0)); // 2 second max delay
    }

    for (auto& comb : combFiltersR)
    {
        comb.prepare(spec);
        comb.setSize(static_cast<int>(sampleRate * 2.0));
    }

    // Prepare allpass filters
    for (auto& allpass : allpassFiltersL)
    {
        allpass.prepare(spec);
        allpass.setSize(static_cast<int>(sampleRate * 0.05)); // 50ms max
    }

    for (auto& allpass : allpassFiltersR)
    {
        allpass.prepare(spec);
        allpass.setSize(static_cast<int>(sampleRate * 0.05));
    }

    // Prepare early reflection taps
    for (auto& tap : earlyTapsL)
        tap.prepare(spec);

    for (auto& tap : earlyTapsR)
        tap.prepare(spec);

    // Prepare limiters
    limiterL.prepare(spec);
    limiterR.prepare(spec);
    limiterL.setThreshold(0.99f);
    limiterR.setThreshold(0.99f);
    limiterL.setRelease(50.0f);
    limiterR.setRelease(50.0f);

    // Reset everything
    reset();

    // Configure based on current algorithm
    setAlgorithm(currentAlgorithm);
}

//==============================================================================
void ReverbEngine::reset()
{
    predelayL.reset();
    predelayR.reset();

    for (auto& comb : combFiltersL)
        comb.reset();

    for (auto& comb : combFiltersR)
        comb.reset();

    for (auto& allpass : allpassFiltersL)
        allpass.reset();

    for (auto& allpass : allpassFiltersR)
        allpass.reset();

    for (auto& tap : earlyTapsL)
        tap.reset();

    for (auto& tap : earlyTapsR)
        tap.reset();

    limiterL.reset();
    limiterR.reset();
}

//==============================================================================
void ReverbEngine::setAlgorithm(int algorithm)
{
    currentAlgorithm = algorithm;

    switch (algorithm)
    {
        case 0: // Room
            configureRoomAlgorithm();
            break;
        case 1: // Hall
            configureHallAlgorithm();
            break;
        case 2: // Plate
            configurePlateAlgorithm();
            break;
        case 3: // Early Reflections
            configureEarlyReflectionsAlgorithm();
            break;
        default:
            configureRoomAlgorithm();
            break;
    }

    updateCombFilters();
    updateAllpassFilters();
    updateEarlyReflections();
}

//==============================================================================
void ReverbEngine::configureRoomAlgorithm()
{
    numActiveCombs = 10;
    numActiveAllpasses = 4;
    numActiveEarlyTaps = 8;

    // Room: 8-12 combs with shorter delays (20-100ms base)
    const float baseDelays[] = { 0.020f, 0.030f, 0.037f, 0.041f, 0.047f,
                                  0.053f, 0.061f, 0.071f, 0.083f, 0.097f };

    for (int i = 0; i < numActiveCombs; ++i)
    {
        float jitter = 1.0f + jitterDistribution(randomGenerator);
        float delayL = baseDelays[i] * jitter;
        float delayR = baseDelays[i] * (1.0f + jitterDistribution(randomGenerator));

        combFiltersL[i].setBaseDelay(delayL * sampleRate);
        combFiltersR[i].setBaseDelay(delayR * sampleRate);
        combFiltersL[i].setFeedback(0.85f);
        combFiltersR[i].setFeedback(0.85f);
    }

    // Room allpass delays (5-20ms)
    const float allpassDelays[] = { 0.005f, 0.008f, 0.013f, 0.017f };

    for (int i = 0; i < numActiveAllpasses; ++i)
    {
        allpassFiltersL[i].setSize(static_cast<int>(allpassDelays[i] * sampleRate));
        allpassFiltersR[i].setSize(static_cast<int>(allpassDelays[i] * sampleRate * 1.1f));
    }

    // Room early reflections (quick buildup)
    for (int i = 0; i < numActiveEarlyTaps; ++i)
    {
        float delay = (i + 1) * 0.005f; // 5ms spacing
        earlyReflectionData[i].delay = delay * sampleRate;
        earlyReflectionData[i].gain = std::pow(0.8f, i);
        earlyReflectionData[i].panLeft = 0.5f + (i % 2 == 0 ? 0.3f : -0.3f);
        earlyReflectionData[i].panRight = 0.5f + (i % 2 == 0 ? -0.3f : 0.3f);
    }
}

//==============================================================================
void ReverbEngine::configureHallAlgorithm()
{
    numActiveCombs = 14;
    numActiveAllpasses = 6;
    numActiveEarlyTaps = 12;

    // Hall: 12-16 combs with longer delays (50-300ms base)
    const float baseDelays[] = { 0.050f, 0.067f, 0.083f, 0.097f, 0.113f,
                                  0.127f, 0.139f, 0.151f, 0.167f, 0.181f,
                                  0.197f, 0.211f, 0.229f, 0.241f };

    for (int i = 0; i < numActiveCombs; ++i)
    {
        float jitter = 1.0f + jitterDistribution(randomGenerator);
        float delayL = baseDelays[i] * jitter;
        float delayR = baseDelays[i] * (1.0f + jitterDistribution(randomGenerator));

        combFiltersL[i].setBaseDelay(delayL * sampleRate);
        combFiltersR[i].setBaseDelay(delayR * sampleRate);
        combFiltersL[i].setFeedback(0.77f);
        combFiltersR[i].setFeedback(0.77f);
    }

    // Hall allpass delays (10-30ms) - more diffusion
    const float allpassDelays[] = { 0.010f, 0.013f, 0.017f, 0.021f, 0.025f, 0.029f };

    for (int i = 0; i < numActiveAllpasses; ++i)
    {
        allpassFiltersL[i].setSize(static_cast<int>(allpassDelays[i] * sampleRate));
        allpassFiltersR[i].setSize(static_cast<int>(allpassDelays[i] * sampleRate * 1.1f));
    }

    // Hall early reflections (sparser, building to diffuse tail)
    for (int i = 0; i < numActiveEarlyTaps; ++i)
    {
        float delay = (i + 1) * 0.008f + i * 0.003f; // Increasing spacing
        earlyReflectionData[i].delay = delay * sampleRate;
        earlyReflectionData[i].gain = std::pow(0.75f, i);
        earlyReflectionData[i].panLeft = 0.5f + std::sin(i * 0.5f) * 0.4f;
        earlyReflectionData[i].panRight = 0.5f + std::cos(i * 0.5f) * 0.4f;
    }
}

//==============================================================================
void ReverbEngine::configurePlateAlgorithm()
{
    numActiveCombs = 6;
    numActiveAllpasses = 2;
    numActiveEarlyTaps = 0; // No early reflections for plate

    // Plate: 4-8 combs with very short delays (10-50ms base) but high feedback
    const float baseDelays[] = { 0.010f, 0.017f, 0.023f, 0.031f, 0.037f, 0.043f };

    for (int i = 0; i < numActiveCombs; ++i)
    {
        float jitter = 1.0f + jitterDistribution(randomGenerator) * 0.05f; // Less jitter
        float delayL = baseDelays[i] * jitter;
        float delayR = baseDelays[i] * (1.0f + jitterDistribution(randomGenerator) * 0.05f);

        combFiltersL[i].setBaseDelay(delayL * sampleRate);
        combFiltersR[i].setBaseDelay(delayR * sampleRate);
        combFiltersL[i].setFeedback(0.96f); // Very high feedback
        combFiltersR[i].setFeedback(0.96f);

        // Add modulation for shimmer
        float modRate = 0.5f + i * 0.3f; // 0.5-2Hz
        float modDepth = 0.02f + i * 0.005f; // 2-5% depth
        combFiltersL[i].setModulation(modDepth, modRate, sampleRate);
        combFiltersR[i].setModulation(modDepth, modRate * 1.1f, sampleRate);
    }

    // Minimal allpass for plate
    allpassFiltersL[0].setSize(static_cast<int>(0.003f * sampleRate));
    allpassFiltersR[0].setSize(static_cast<int>(0.004f * sampleRate));
    allpassFiltersL[1].setSize(static_cast<int>(0.005f * sampleRate));
    allpassFiltersR[1].setSize(static_cast<int>(0.006f * sampleRate));
}

//==============================================================================
void ReverbEngine::configureEarlyReflectionsAlgorithm()
{
    numActiveCombs = 0; // No comb filters
    numActiveAllpasses = 0; // No allpass filters
    numActiveEarlyTaps = 20; // Only early reflections

    // Early reflections: geometrically spaced delays with 1/r^2 decay
    for (int i = 0; i < numActiveEarlyTaps; ++i)
    {
        float delay = 0.005f * std::pow(1.5f, i); // Geometric spacing
        if (delay > 0.1f) delay = 0.1f; // Cap at 100ms

        earlyReflectionData[i].delay = delay * sampleRate;
        earlyReflectionData[i].gain = 1.0f / ((i + 1) * (i + 1)); // 1/r^2 decay

        // Stereo spread based on reflection order
        float angle = i * 0.4f;
        earlyReflectionData[i].panLeft = 0.5f + std::sin(angle) * 0.5f;
        earlyReflectionData[i].panRight = 0.5f + std::cos(angle) * 0.5f;
    }
}

//==============================================================================
void ReverbEngine::setSize(float size)
{
    currentSize = juce::jlimit(0.0f, 1.0f, size);
    updateCombFilters();
    updateEarlyReflections();
}

void ReverbEngine::setDamping(float damp)
{
    currentDamping = juce::jlimit(0.0f, 1.0f, damp);
    updateCombFilters();
}

void ReverbEngine::setPredelay(float predelayMs)
{
    currentPredelayMs = juce::jlimit(0.0f, 200.0f, predelayMs);
    float delaySamples = (currentPredelayMs / 1000.0f) * sampleRate;
    predelayL.setDelay(delaySamples);
    predelayR.setDelay(delaySamples);
}

void ReverbEngine::setMix(float mix)
{
    currentMix = juce::jlimit(0.0f, 1.0f, mix);
}

//==============================================================================
float ReverbEngine::calculateDampingFrequency(float dampParam) const
{
    // Map 0-1 to 20kHz-500Hz (inverse relationship)
    float minFreq = 500.0f;
    float maxFreq = 20000.0f;
    return maxFreq - (dampParam * (maxFreq - minFreq));
}

//==============================================================================
void ReverbEngine::updateCombFilters()
{
    float dampFreq = calculateDampingFrequency(currentDamping);

    for (int i = 0; i < numActiveCombs; ++i)
    {
        // Scale delay by size parameter
        float baseDelay = combFiltersL[i].getBaseDelay();
        float scaledDelay = baseDelay * (0.5f + currentSize * 1.5f); // 0.5x to 2x
        combFiltersL[i].setSize(static_cast<int>(scaledDelay));

        baseDelay = combFiltersR[i].getBaseDelay();
        scaledDelay = baseDelay * (0.5f + currentSize * 1.5f);
        combFiltersR[i].setSize(static_cast<int>(scaledDelay));

        // Set damping
        combFiltersL[i].setDamping(dampFreq, sampleRate);
        combFiltersR[i].setDamping(dampFreq, sampleRate);

        // Adjust feedback based on size (longer = slightly less feedback)
        float feedbackAdjust = 1.0f - (currentSize * 0.1f);
        float currentFeedback = combFiltersL[i].getFeedback();
        combFiltersL[i].setFeedback(currentFeedback * feedbackAdjust);
        combFiltersR[i].setFeedback(currentFeedback * feedbackAdjust);
    }
}

//==============================================================================
void ReverbEngine::updateAllpassFilters()
{
    // Allpass filters don't need much updating beyond initial configuration
    // Could add modulation here if desired
}

//==============================================================================
void ReverbEngine::updateEarlyReflections()
{
    for (int i = 0; i < numActiveEarlyTaps; ++i)
    {
        float scaledDelay = earlyReflectionData[i].delay * (0.5f + currentSize * 1.5f);
        earlyTapsL[i].setDelay(scaledDelay);
        earlyTapsR[i].setDelay(scaledDelay * 1.05f); // Slight stereo offset
    }
}

//==============================================================================
void ReverbEngine::process(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();

    // Safety check
    if (buffer.getNumChannels() < 2)
        return;

    // Choose processing method based on algorithm
    if (currentAlgorithm == 3) // Early Reflections
    {
        processEarlyReflections(buffer);
    }
    else if (currentAlgorithm == 2) // Plate
    {
        processPlate(buffer);
    }
    else // Room or Hall
    {
        processRoomHall(buffer);
    }
}

//==============================================================================
// Added Householder mixing for better diffusion (Task 2)
class HouseholderMatrix
{
public:
    HouseholderMatrix(int size) : N(size)
    {
        matrix.resize(N * N);
        generateHouseholder();
    }

    void process(float* inputs, float* outputs)
    {
        for (int i = 0; i < N; ++i)
        {
            outputs[i] = 0.0f;
            for (int j = 0; j < N; ++j)
            {
                outputs[i] += matrix[i * N + j] * inputs[j];
            }
        }
    }

private:
    void generateHouseholder()
    {
        // Create orthogonal matrix using Householder reflection
        std::vector<float> v(N);
        float norm = 0.0f;

        for (int i = 0; i < N; ++i)
        {
            v[i] = (rand() / float(RAND_MAX)) * 2.0f - 1.0f;
            norm += v[i] * v[i];
        }

        norm = std::sqrt(norm);
        for (int i = 0; i < N; ++i)
            v[i] /= norm;

        // H = I - 2vv^T
        for (int i = 0; i < N; ++i)
        {
            for (int j = 0; j < N; ++j)
            {
                matrix[i * N + j] = (i == j ? 1.0f : 0.0f) - 2.0f * v[i] * v[j];
            }
        }
    }

    int N;
    std::vector<float> matrix;
};

//==============================================================================
void ReverbEngine::processRoomHall(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    float* leftChannel = buffer.getWritePointer(0);
    float* rightChannel = buffer.getWritePointer(1);

    // Added Householder matrix for enhanced diffusion
    static HouseholderMatrix householderL(MAX_COMBS);
    static HouseholderMatrix householderR(MAX_COMBS);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float inputL = leftChannel[sample];
        float inputR = rightChannel[sample];

        // Apply predelay
        float delayedL = predelayL.popSample(0);
        float delayedR = predelayR.popSample(0);
        predelayL.pushSample(0, inputL);
        predelayR.pushSample(0, inputR);

        // Process through allpass filters (diffusion)
        float diffusedL = delayedL;
        float diffusedR = delayedR;

        for (int i = 0; i < numActiveAllpasses; ++i)
        {
            diffusedL = allpassFiltersL[i].process(diffusedL);
            diffusedR = allpassFiltersR[i].process(diffusedR);
        }

        // Process through comb filters and collect outputs
        float combOutL[MAX_COMBS] = {0.0f};
        float combOutR[MAX_COMBS] = {0.0f};

        for (int i = 0; i < numActiveCombs; ++i)
        {
            combOutL[i] = combFiltersL[i].process(diffusedL);
            combOutR[i] = combFiltersR[i].process(diffusedR);
        }

        // Apply Householder matrix mix for better diffusion (Task 2)
        float mixedCombL[MAX_COMBS];
        float mixedCombR[MAX_COMBS];
        householderL.process(combOutL, mixedCombL);
        householderR.process(combOutR, mixedCombR);

        // Sum mixed comb outputs
        float reverbL = 0.0f;
        float reverbR = 0.0f;

        for (int i = 0; i < numActiveCombs; ++i)
        {
            reverbL += mixedCombL[i];
            reverbR += mixedCombR[i];
        }

        // Add early reflections
        for (int i = 0; i < numActiveEarlyTaps; ++i)
        {
            float earlyL = earlyTapsL[i].popSample(0);
            float earlyR = earlyTapsR[i].popSample(0);

            earlyTapsL[i].pushSample(0, diffusedL);
            earlyTapsR[i].pushSample(0, diffusedR);

            reverbL += earlyL * earlyReflectionData[i].gain * earlyReflectionData[i].panLeft;
            reverbR += earlyR * earlyReflectionData[i].gain * earlyReflectionData[i].panRight;
        }

        // Scale and mix
        reverbL /= std::sqrt(static_cast<float>(numActiveCombs + numActiveEarlyTaps));
        reverbR /= std::sqrt(static_cast<float>(numActiveCombs + numActiveEarlyTaps));

        // Apply mix
        float wetL = reverbL * currentMix;
        float wetR = reverbR * currentMix;
        float dryL = inputL * (1.0f - currentMix);
        float dryR = inputR * (1.0f - currentMix);

        // Apply limiter to prevent clipping
        leftChannel[sample] = juce::jlimit(-1.0f, 1.0f, dryL + wetL);
        rightChannel[sample] = juce::jlimit(-1.0f, 1.0f, dryR + wetR);
    }
}

//==============================================================================
void ReverbEngine::processPlate(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    float* leftChannel = buffer.getWritePointer(0);
    float* rightChannel = buffer.getWritePointer(1);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float inputL = leftChannel[sample];
        float inputR = rightChannel[sample];

        // Apply predelay
        float delayedL = predelayL.popSample(0);
        float delayedR = predelayR.popSample(0);
        predelayL.pushSample(0, inputL);
        predelayR.pushSample(0, inputR);

        // Minimal diffusion for plate
        float diffusedL = delayedL;
        float diffusedR = delayedR;

        for (int i = 0; i < numActiveAllpasses; ++i)
        {
            diffusedL = allpassFiltersL[i].process(diffusedL, 0.5f); // Less diffusion
            diffusedR = allpassFiltersR[i].process(diffusedR, 0.5f);
        }

        // Process through modulated comb filters
        float reverbL = 0.0f;
        float reverbR = 0.0f;

        for (int i = 0; i < numActiveCombs; ++i)
        {
            combFiltersL[i].updateModulation(); // Update modulation
            combFiltersR[i].updateModulation();

            reverbL += combFiltersL[i].process(diffusedL);
            reverbR += combFiltersR[i].process(diffusedR);
        }

        // Scale
        reverbL /= std::sqrt(static_cast<float>(numActiveCombs));
        reverbR /= std::sqrt(static_cast<float>(numActiveCombs));

        // Plate character: bright and metallic (less damping on highs)
        // Could add a shelving filter here for more authentic plate sound

        // Apply mix
        float wetL = reverbL * currentMix * 1.2f; // Slightly boost plate output
        float wetR = reverbR * currentMix * 1.2f;
        float dryL = inputL * (1.0f - currentMix);
        float dryR = inputR * (1.0f - currentMix);

        // Apply limiter
        leftChannel[sample] = juce::jlimit(-1.0f, 1.0f, dryL + wetL);
        rightChannel[sample] = juce::jlimit(-1.0f, 1.0f, dryR + wetR);
    }
}

//==============================================================================
void ReverbEngine::processEarlyReflections(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    float* leftChannel = buffer.getWritePointer(0);
    float* rightChannel = buffer.getWritePointer(1);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float inputL = leftChannel[sample];
        float inputR = rightChannel[sample];

        // Apply predelay
        float delayedL = predelayL.popSample(0);
        float delayedR = predelayR.popSample(0);
        predelayL.pushSample(0, inputL);
        predelayR.pushSample(0, inputR);

        // Process only early reflections
        float reverbL = 0.0f;
        float reverbR = 0.0f;

        for (int i = 0; i < numActiveEarlyTaps; ++i)
        {
            float earlyL = earlyTapsL[i].popSample(0);
            float earlyR = earlyTapsR[i].popSample(0);

            earlyTapsL[i].pushSample(0, delayedL);
            earlyTapsR[i].pushSample(0, delayedR);

            // Apply gain and panning
            reverbL += earlyL * earlyReflectionData[i].gain * earlyReflectionData[i].panLeft;
            reverbR += earlyR * earlyReflectionData[i].gain * earlyReflectionData[i].panRight;
        }

        // Quick fade out (300ms max decay)
        float decayFactor = 1.0f - (currentSize * 0.7f); // Size controls decay speed
        reverbL *= decayFactor;
        reverbR *= decayFactor;

        // Apply mix
        float wetL = reverbL * currentMix;
        float wetR = reverbR * currentMix;
        float dryL = inputL * (1.0f - currentMix);
        float dryR = inputR * (1.0f - currentMix);

        // Output (no limiter needed for early reflections)
        leftChannel[sample] = dryL + wetL;
        rightChannel[sample] = dryR + wetR;
    }
}