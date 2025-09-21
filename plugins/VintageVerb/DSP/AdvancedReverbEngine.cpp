/*
  ==============================================================================

    AdvancedReverbEngine.cpp - Implementation of professional-grade reverb

  ==============================================================================
*/

#include "AdvancedReverbEngine.h"
#include <algorithm>
#include <cmath>

AdvancedReverbEngine::AdvancedReverbEngine()
{
}

void AdvancedReverbEngine::prepare(double sr, int maxBlock)
{
    sampleRate = sr;
    blockSize = maxBlock;

    // Prepare all components
    fdnNetwork.prepare(sampleRate);
    modulation.prepare(sampleRate);
    psychoacoustics.prepare(sampleRate);
    earlyReflections.prepare(sampleRate);

    // Prepare diffusers with varied sizes for rich texture
    const int baseDiffuserSize = static_cast<int>(sampleRate * 0.01);  // 10ms base
    for (int i = 0; i < 4; ++i)
    {
        inputDiffusers[i].prepare(baseDiffuserSize * (i + 1));
        outputDiffusers[i].prepare(baseDiffuserSize * (i + 2));
    }

    // Setup oversampling if needed
    if (useOversampling)
    {
        oversamplerL.prepare(sampleRate, OVERSAMPLE_FACTOR);
        oversamplerR.prepare(sampleRate, OVERSAMPLE_FACTOR);
    }

    reset();
    updateAllParameters();
}

void AdvancedReverbEngine::reset()
{
    // Reset all delay lines and filters
    fdnNetwork = StereoFDN();
    fdnNetwork.prepare(sampleRate);

    for (auto& diffuser : inputDiffusers)
        diffuser = NestedAllpassDiffuser();

    for (auto& diffuser : outputDiffusers)
        diffuser = NestedAllpassDiffuser();

    modulation = ModulationSystem();
    modulation.prepare(sampleRate);

    psychoacoustics = PsychoacousticProcessor();
    psychoacoustics.prepare(sampleRate);

    earlyReflections = RoomEarlyReflections();
    earlyReflections.prepare(sampleRate);
}

void AdvancedReverbEngine::processStereo(float* leftIn, float* rightIn,
                                         float* leftOut, float* rightOut, int numSamples)
{
    for (int sample = 0; sample < numSamples; ++sample)
    {
        float inputL = leftIn[sample];
        float inputR = rightIn[sample];

        // Apply psychoacoustic pre-processing
        psychoacoustics.process(inputL, inputR);

        // Process early reflections
        float earlyL = 0.0f, earlyR = 0.0f;
        earlyReflections.process(inputL, inputR, earlyL, earlyR);

        // Input diffusion (creates density)
        float diffusedL = inputL;
        float diffusedR = inputR;

        // Process through nested allpass diffusers
        for (int i = 0; i < 2; ++i)
        {
            diffusedL = inputDiffusers[i * 2].process(diffusedL);
            diffusedR = inputDiffusers[i * 2 + 1].process(diffusedR);
        }

        // Process through stereo FDN
        float lateL = 0.0f, lateR = 0.0f;
        fdnNetwork.process(diffusedL, diffusedR, lateL, lateR, modulation);

        // Output diffusion for smoothness
        for (int i = 0; i < 2; ++i)
        {
            lateL = outputDiffusers[i * 2].process(lateL);
            lateR = outputDiffusers[i * 2 + 1].process(lateR);
        }

        // Mix early and late reflections
        leftOut[sample] = earlyL * (1.0f - earlyLateMix) + lateL * earlyLateMix;
        rightOut[sample] = earlyR * (1.0f - earlyLateMix) + lateR * earlyLateMix;

        // Apply final stereo width adjustment
        if (stereoWidth != 1.0f)
        {
            float mid = (leftOut[sample] + rightOut[sample]) * 0.5f;
            float side = (leftOut[sample] - rightOut[sample]) * 0.5f * stereoWidth;
            leftOut[sample] = mid + side;
            rightOut[sample] = mid - side;
        }

        // Soft clipping for safety
        leftOut[sample] = softClip(leftOut[sample]);
        rightOut[sample] = softClip(rightOut[sample]);
    }
}

void AdvancedReverbEngine::setSize(float newSize)
{
    size = juce::jlimit(0.0f, 1.0f, newSize);
    fdnNetwork.setDecayTime(0.5f + size * 9.5f);  // 0.5 to 10 seconds
    earlyReflections.setRoomSize(size);
    updateAllParameters();
}

void AdvancedReverbEngine::setDiffusion(float newDiffusion)
{
    diffusion = juce::jlimit(0.0f, 1.0f, newDiffusion);
    for (auto& diffuser : inputDiffusers)
        diffuser.setDiffusion(diffusion);
    for (auto& diffuser : outputDiffusers)
        diffuser.setDiffusion(diffusion * 0.8f);  // Slightly less for output
}

void AdvancedReverbEngine::setDamping(float newDamping)
{
    damping = juce::jlimit(0.0f, 1.0f, newDamping);
    updateAllParameters();
}

void AdvancedReverbEngine::setModulationDepth(float depth)
{
    modDepth = juce::jlimit(0.0f, 1.0f, depth);
    modulation.setChorusDepth(modDepth * 0.002f);  // Scale to samples
}

void AdvancedReverbEngine::setModulationRate(float rate)
{
    modRate = juce::jlimit(0.0f, 1.0f, rate);
    modulation.setSpinRate(0.1f + rate * 2.0f);  // 0.1 to 2.1 Hz
}

void AdvancedReverbEngine::setLowDecay(float decay)
{
    lowDecay = juce::jlimit(0.1f, 2.0f, decay);
    updateAllParameters();
}

void AdvancedReverbEngine::setMidDecay(float decay)
{
    midDecay = juce::jlimit(0.1f, 2.0f, decay);
    updateAllParameters();
}

void AdvancedReverbEngine::setHighDecay(float decay)
{
    highDecay = juce::jlimit(0.1f, 2.0f, decay);
    updateAllParameters();
}

void AdvancedReverbEngine::setCrossoverLow(float freq)
{
    crossoverLow = juce::jlimit(50.0f, 1000.0f, freq);
    updateAllParameters();
}

void AdvancedReverbEngine::setCrossoverHigh(float freq)
{
    crossoverHigh = juce::jlimit(1000.0f, 10000.0f, freq);
    updateAllParameters();
}

void AdvancedReverbEngine::setSpinRate(float rate)
{
    spinRate = juce::jlimit(0.0f, 1.0f, rate);
    modulation.setSpinRate(rate * 2.0f);  // 0 to 2 Hz
}

void AdvancedReverbEngine::setWander(float amount)
{
    wander = juce::jlimit(0.0f, 1.0f, amount);
    modulation.setWanderAmount(amount);
}

void AdvancedReverbEngine::setChorus(float amount)
{
    chorus = juce::jlimit(0.0f, 1.0f, amount);
    modulation.setChorusDepth(amount * 0.003f);  // Scale to samples
}

void AdvancedReverbEngine::setPreDelay(float ms)
{
    preDelay = juce::jlimit(0.0f, 200.0f, ms);
    psychoacoustics.setPreDelay(ms);
}

void AdvancedReverbEngine::setStereoWidth(float width)
{
    stereoWidth = juce::jlimit(0.0f, 2.0f, width);
    psychoacoustics.setWidth(stereoWidth);
}

void AdvancedReverbEngine::setEarlyLateMix(float mix)
{
    earlyLateMix = juce::jlimit(0.0f, 1.0f, mix);
}

void AdvancedReverbEngine::setDensity(float newDensity)
{
    density = juce::jlimit(0.0f, 1.0f, newDensity);
    updateAllParameters();
}

void AdvancedReverbEngine::configureForMode(int mode)
{
    switch (mode)
    {
        case 0:  // Concert Hall - Long, warm, spacious
            setSize(0.85f);
            setDiffusion(0.88f);
            setDamping(0.3f);
            setLowDecay(1.2f);
            setMidDecay(1.0f);
            setHighDecay(0.6f);
            setSpinRate(0.3f);
            setWander(0.4f);
            setChorus(0.2f);
            setPreDelay(25.0f);
            setStereoWidth(1.2f);
            setEarlyLateMix(0.7f);
            setDensity(0.8f);
            break;

        case 1:  // Plate - Dense, bright, metallic
            setSize(0.5f);
            setDiffusion(0.95f);
            setDamping(0.15f);
            setLowDecay(0.9f);
            setMidDecay(1.0f);
            setHighDecay(1.1f);
            setSpinRate(0.5f);
            setWander(0.2f);
            setChorus(0.4f);
            setPreDelay(0.0f);
            setStereoWidth(1.5f);
            setEarlyLateMix(0.3f);
            setDensity(1.0f);
            break;

        case 2:  // Room - Intimate, natural, controlled
            setSize(0.3f);
            setDiffusion(0.5f);
            setDamping(0.5f);
            setLowDecay(0.8f);
            setMidDecay(1.0f);
            setHighDecay(0.7f);
            setSpinRate(0.1f);
            setWander(0.1f);
            setChorus(0.05f);
            setPreDelay(5.0f);
            setStereoWidth(0.8f);
            setEarlyLateMix(0.4f);
            setDensity(0.5f);
            break;

        case 3:  // Chamber - Clear, precise, musical
            setSize(0.6f);
            setDiffusion(0.7f);
            setDamping(0.25f);
            setLowDecay(1.0f);
            setMidDecay(1.0f);
            setHighDecay(0.8f);
            setSpinRate(0.2f);
            setWander(0.3f);
            setChorus(0.15f);
            setPreDelay(15.0f);
            setStereoWidth(1.0f);
            setEarlyLateMix(0.5f);
            setDensity(0.6f);
            break;

        case 4:  // Cathedral - Massive, ethereal, long decay
            setSize(0.95f);
            setDiffusion(0.9f);
            setDamping(0.4f);
            setLowDecay(1.5f);
            setMidDecay(1.2f);
            setHighDecay(0.5f);
            setSpinRate(0.15f);
            setWander(0.5f);
            setChorus(0.3f);
            setPreDelay(40.0f);
            setStereoWidth(1.5f);
            setEarlyLateMix(0.8f);
            setDensity(0.9f);
            break;
    }
}

float AdvancedReverbEngine::softClip(float input)
{
    const float threshold = 0.95f;
    if (std::abs(input) < threshold)
        return input;

    float sign = input < 0.0f ? -1.0f : 1.0f;
    float amount = std::abs(input) - threshold;
    float clipped = threshold + std::tanh(amount * 2.0f) * (1.0f - threshold);
    return sign * clipped;
}

void AdvancedReverbEngine::updateAllParameters()
{
    // Update all frequency-dependent parameters
    for (int i = 0; i < NUM_DELAY_LINES; ++i)
    {
        fdnNetwork.dampingFilters[i].setDecayTimes(
            lowDecay * size, midDecay * size, highDecay * size);
        fdnNetwork.dampingFilters[i].setCrossoverFrequencies(
            crossoverLow, crossoverHigh);
    }
}

//==============================================================================
// InterpolatedDelayLine implementation
//==============================================================================

void AdvancedReverbEngine::InterpolatedDelayLine::prepare(int maxSize)
{
    buffer.resize(maxSize + 4);  // Extra samples for interpolation
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

float AdvancedReverbEngine::InterpolatedDelayLine::read(float delaySamples) const
{
    if (buffer.empty()) return 0.0f;

    int delayInt = static_cast<int>(delaySamples);
    float fraction = delaySamples - delayInt;

    int readPos = writePos - delayInt;
    while (readPos < 0) readPos += buffer.size();

    // Get 4 samples for Hermite interpolation
    int p0 = (readPos - 1 + buffer.size()) % buffer.size();
    int p1 = readPos;
    int p2 = (readPos + 1) % buffer.size();
    int p3 = (readPos + 2) % buffer.size();

    return hermiteInterpolate(fraction,
        buffer[p0], buffer[p1], buffer[p2], buffer[p3]);
}

void AdvancedReverbEngine::InterpolatedDelayLine::write(float input)
{
    if (!buffer.empty())
    {
        buffer[writePos] = input;
        writePos = (writePos + 1) % buffer.size();
    }
}

void AdvancedReverbEngine::InterpolatedDelayLine::setDelay(float samples)
{
    currentDelay = juce::jlimit(1.0f, static_cast<float>(buffer.size() - 4), samples);
}

float AdvancedReverbEngine::InterpolatedDelayLine::readWithModulation(
    float delaySamples, float modAmount) const
{
    float modulatedDelay = delaySamples + modAmount;
    modulatedDelay = juce::jlimit(1.0f, static_cast<float>(buffer.size() - 4), modulatedDelay);
    return read(modulatedDelay);
}

//==============================================================================
// MultibandDamping implementation
//==============================================================================

void AdvancedReverbEngine::MultibandDamping::prepare(double sr)
{
    sampleRate = sr;

    // Initialize crossover filters
    lowCrossover.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
    lowCrossover.setCutoffFrequency(250.0f);
    lowCrossover.prepare({sr, 512, 1});

    highCrossover.setType(juce::dsp::LinkwitzRileyFilterType::highpass);
    highCrossover.setCutoffFrequency(4000.0f);
    highCrossover.prepare({sr, 512, 1});

    // Initialize shelf filters for decay control
    auto lowShelfCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf(
        sr, 250.0f, 0.7f, 1.0f);
    lowShelf.coefficients = lowShelfCoeffs;

    auto highShelfCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        sr, 4000.0f, 0.7f, 1.0f);
    highShelf.coefficients = highShelfCoeffs;
}

float AdvancedReverbEngine::MultibandDamping::process(float input)
{
    // Split into three bands
    float low = lowCrossover.processSample(0, input);
    float high = highCrossover.processSample(0, input);
    float mid = input - low - high;

    // Apply band-specific decay
    low *= std::pow(0.001f, 1.0f / (lowDecay * sampleRate));
    mid *= std::pow(0.001f, 1.0f / (midDecay * sampleRate));
    high *= std::pow(0.001f, 1.0f / (highDecay * sampleRate));

    // Recombine bands
    return low + mid + high;
}

void AdvancedReverbEngine::MultibandDamping::setDecayTimes(float lowRT60, float midRT60, float highRT60)
{
    lowDecay = juce::jlimit(0.1f, 10.0f, lowRT60);
    midDecay = juce::jlimit(0.1f, 10.0f, midRT60);
    highDecay = juce::jlimit(0.1f, 10.0f, highRT60);
}

void AdvancedReverbEngine::MultibandDamping::setCrossoverFrequencies(float lowFreq, float highFreq)
{
    lowCrossover.setCutoffFrequency(lowFreq);
    highCrossover.setCutoffFrequency(highFreq);
}

//==============================================================================
// ModulationSystem implementation
//==============================================================================

void AdvancedReverbEngine::ModulationSystem::prepare(double sr)
{
    sampleRate = sr;

    // Initialize chorus LFO phases
    for (int i = 0; i < NUM_CHORUS_VOICES; ++i)
    {
        chorusPhases[i] = static_cast<float>(i) / NUM_CHORUS_VOICES;
        chorusRates[i] = 0.1f + i * 0.07f;  // Slightly different rates
    }

    // Initialize wander values
    for (int i = 0; i < NUM_DELAY_LINES; ++i)
    {
        wanderValues[i] = 0.0f;
        wanderTargets[i] = normalDist(rng) * 0.001f;
    }
}

float AdvancedReverbEngine::ModulationSystem::getSpinModulation(int channel)
{
    float phase = spinPhase[channel];
    phase += spinRate / sampleRate;
    if (phase >= 1.0f) phase -= 1.0f;
    spinPhase[channel] = phase;

    // Use complex exponential for smooth circular motion
    float angle = phase * 2.0f * juce::MathConstants<float>::pi;
    return std::sin(angle) * 0.002f;  // ±2 samples modulation
}

float AdvancedReverbEngine::ModulationSystem::getWanderModulation(int delayIndex)
{
    // Smooth random walk
    float& current = wanderValues[delayIndex];
    float& target = wanderTargets[delayIndex];

    // Interpolate towards target
    current += (target - current) * wanderRate;

    // Generate new target occasionally
    if (std::abs(current - target) < 0.0001f)
    {
        target = normalDist(rng) * wanderAmount * 0.003f;
    }

    return current;
}

float AdvancedReverbEngine::ModulationSystem::getChorusModulation(int voice)
{
    float& phase = chorusPhases[voice % NUM_CHORUS_VOICES];
    float rate = chorusRates[voice % NUM_CHORUS_VOICES];

    phase += rate / sampleRate;
    if (phase >= 1.0f) phase -= 1.0f;

    float angle = phase * 2.0f * juce::MathConstants<float>::pi;
    return std::sin(angle) * chorusDepth;
}

void AdvancedReverbEngine::ModulationSystem::setSpinRate(float hz)
{
    spinRate = juce::jlimit(0.0f, 5.0f, hz);
}

void AdvancedReverbEngine::ModulationSystem::setWanderAmount(float amount)
{
    wanderAmount = juce::jlimit(0.0f, 1.0f, amount);
    wanderRate = 0.001f + amount * 0.01f;  // Faster response with more wander
}

void AdvancedReverbEngine::ModulationSystem::setChorusDepth(float depth)
{
    chorusDepth = juce::jlimit(0.0f, 0.01f, depth);  // Max 10 samples
}

//==============================================================================
// NestedAllpassDiffuser implementation
//==============================================================================

void AdvancedReverbEngine::NestedAllpassDiffuser::prepare(int maxSize)
{
    // Outer delay is full size
    outerDelay.prepare(maxSize);

    // Inner delays are smaller, prime-ratio sizes
    const int innerSizes[4] = {
        maxSize / 7,
        maxSize / 11,
        maxSize / 13,
        maxSize / 17
    };

    for (int i = 0; i < 4; ++i)
    {
        innerDelays[i].prepare(innerSizes[i]);
        innerDelays[i].setDelay(static_cast<float>(innerSizes[i] - 1));
    }

    outerDelay.setDelay(static_cast<float>(maxSize - 1));
}

float AdvancedReverbEngine::NestedAllpassDiffuser::process(float input)
{
    // Process through inner allpasses first
    float innerSum = 0.0f;
    for (int i = 0; i < 4; ++i)
    {
        float delayed = innerDelays[i].read(innerDelays[i].currentDelay);
        float output = -input * innerFeedback + delayed;
        innerDelays[i].write(input * innerFeedback + delayed);
        innerSum += output * 0.25f;
        innerStates[i] = output;
    }

    // Mix input with inner diffusion
    float diffused = input * (1.0f - diffusionAmount) + innerSum * diffusionAmount;

    // Process through outer allpass
    float outerDelayed = outerDelay.read(outerDelay.currentDelay);
    float output = -diffused * outerFeedback + outerDelayed;
    outerDelay.write(diffused * outerFeedback + outerDelayed);
    outerState = output;

    return output;
}

void AdvancedReverbEngine::NestedAllpassDiffuser::setDiffusion(float amount)
{
    diffusionAmount = juce::jlimit(0.0f, 1.0f, amount);

    // Scale feedback coefficients based on diffusion
    innerFeedback = 0.3f + diffusionAmount * 0.4f;  // 0.3 to 0.7
    outerFeedback = 0.4f + diffusionAmount * 0.3f;  // 0.4 to 0.7
}

void AdvancedReverbEngine::NestedAllpassDiffuser::modulate(float amount)
{
    // Modulate inner delays for richness
    for (int i = 0; i < 4; ++i)
    {
        float modulation = std::sin((i + 1) * amount) * 0.5f;
        innerDelays[i].setDelay(innerDelays[i].currentDelay + modulation);
    }
}

//==============================================================================
// StereoFDN implementation
//==============================================================================

void AdvancedReverbEngine::StereoFDN::prepare(double sampleRate)
{
    // Initialize delay lines with golden ratio based spacing
    const float goldenRatio = 1.618033988749895f;
    float delay = 100.0f;  // Start at 100 samples

    for (int i = 0; i < NUM_DELAY_LINES/2; ++i)
    {
        leftDelays[i].prepare(MAX_DELAY_SAMPLES);
        rightDelays[i].prepare(MAX_DELAY_SAMPLES);

        // Use golden ratio for inharmonic delay distribution
        delay *= goldenRatio;
        if (delay > MAX_DELAY_SAMPLES / 2) delay /= goldenRatio * goldenRatio;

        leftDelays[i].setDelay(delay);
        rightDelays[i].setDelay(delay * 1.07f);  // Slight offset for stereo

        // Prepare damping filters
        dampingFilters[i].prepare(sampleRate);
        dampingFilters[i + NUM_DELAY_LINES/2].prepare(sampleRate);
    }

    initializeMatrix();
}

void AdvancedReverbEngine::StereoFDN::process(float inputL, float inputR,
                                              float& outputL, float& outputR,
                                              ModulationSystem& modulation)
{
    std::array<float, NUM_DELAY_LINES> delayOutputs;

    // Read from all delay lines
    for (int i = 0; i < NUM_DELAY_LINES/2; ++i)
    {
        // Apply modulation for movement
        float spinMod = modulation.getSpinModulation(0);
        float wanderMod = modulation.getWanderModulation(i);
        float chorusMod = modulation.getChorusModulation(i);

        float totalModL = spinMod + wanderMod + chorusMod;
        float totalModR = -spinMod + wanderMod + chorusMod;  // Opposite spin for stereo

        delayOutputs[i] = leftDelays[i].readWithModulation(
            leftDelays[i].currentDelay, totalModL);
        delayOutputs[i + NUM_DELAY_LINES/2] = rightDelays[i].readWithModulation(
            rightDelays[i].currentDelay, totalModR);
    }

    // Apply mixing matrix
    std::array<float, NUM_DELAY_LINES> mixed;
    for (int i = 0; i < NUM_DELAY_LINES; ++i)
    {
        mixed[i] = 0.0f;
        for (int j = 0; j < NUM_DELAY_LINES; ++j)
        {
            mixed[i] += delayOutputs[j] * mixMatrix[i][j];
        }
    }

    // Apply damping and write back
    for (int i = 0; i < NUM_DELAY_LINES/2; ++i)
    {
        float dampedL = dampingFilters[i].process(mixed[i]);
        float dampedR = dampingFilters[i + NUM_DELAY_LINES/2].process(
            mixed[i + NUM_DELAY_LINES/2]);

        // Inject input and apply feedback
        float toWriteL = inputL * 0.125f + dampedL * feedbackGains[i];
        float toWriteR = inputR * 0.125f + dampedR * feedbackGains[i + NUM_DELAY_LINES/2];

        leftDelays[i].write(toWriteL);
        rightDelays[i].write(toWriteR);
    }

    // Sum outputs
    outputL = 0.0f;
    outputR = 0.0f;

    for (int i = 0; i < NUM_DELAY_LINES/2; ++i)
    {
        outputL += delayOutputs[i] * (2.0f / NUM_DELAY_LINES);
        outputR += delayOutputs[i + NUM_DELAY_LINES/2] * (2.0f / NUM_DELAY_LINES);
    }
}

void AdvancedReverbEngine::StereoFDN::setDecayTime(float rt60)
{
    // Calculate feedback gains for target RT60
    for (int i = 0; i < NUM_DELAY_LINES; ++i)
    {
        float delayTime = (i < NUM_DELAY_LINES/2) ?
            leftDelays[i].currentDelay : rightDelays[i - NUM_DELAY_LINES/2].currentDelay;

        // RT60 formula: feedback = 10^(-3 * delayTime / (rt60 * sampleRate))
        float feedback = std::pow(0.001f, delayTime / (rt60 * 44100.0f));
        feedbackGains[i] = juce::jlimit(0.0f, 0.999f, feedback);
    }
}

void AdvancedReverbEngine::StereoFDN::initializeMatrix()
{
    // Create orthogonal matrix using Householder reflection
    // This ensures energy preservation and good mixing

    // Start with identity matrix
    for (int i = 0; i < NUM_DELAY_LINES; ++i)
    {
        for (int j = 0; j < NUM_DELAY_LINES; ++j)
        {
            mixMatrix[i][j] = (i == j) ? 1.0f : 0.0f;
        }
    }

    // Apply Householder transformations for orthogonality
    std::vector<float> v(NUM_DELAY_LINES);
    for (int k = 0; k < NUM_DELAY_LINES - 1; ++k)
    {
        // Create Householder vector
        float norm = 0.0f;
        for (int i = k; i < NUM_DELAY_LINES; ++i)
        {
            v[i] = (i == k) ? 1.0f : 0.5f;
            norm += v[i] * v[i];
        }
        norm = std::sqrt(norm);

        // Normalize
        for (int i = k; i < NUM_DELAY_LINES; ++i)
        {
            v[i] /= norm;
        }

        // Apply transformation: H = I - 2*v*v^T
        for (int i = 0; i < NUM_DELAY_LINES; ++i)
        {
            for (int j = 0; j < NUM_DELAY_LINES; ++j)
            {
                if (i >= k && j >= k)
                {
                    mixMatrix[i][j] -= 2.0f * v[i] * v[j];
                }
            }
        }
    }

    // Scale for unity gain
    float scale = 1.0f / std::sqrt(static_cast<float>(NUM_DELAY_LINES));
    for (int i = 0; i < NUM_DELAY_LINES; ++i)
    {
        for (int j = 0; j < NUM_DELAY_LINES; ++j)
        {
            mixMatrix[i][j] *= scale;
        }
    }
}

//==============================================================================
// PsychoacousticProcessor implementation
//==============================================================================

void AdvancedReverbEngine::PsychoacousticProcessor::prepare(double sr)
{
    sampleRate = sr;

    // Prepare pre-delay lines
    int maxPreDelay = static_cast<int>(sr * 0.2);  // 200ms max
    preDelayL.prepare(maxPreDelay);
    preDelayR.prepare(maxPreDelay);

    // Prepare Haas effect delays for width
    int maxHaasDelay = static_cast<int>(sr * 0.04);  // 40ms max
    haasDelayL.prepare(maxHaasDelay);
    haasDelayR.prepare(maxHaasDelay);
}

void AdvancedReverbEngine::PsychoacousticProcessor::process(float& left, float& right)
{
    // Apply pre-delay
    float delayedL = preDelayL.read(preDelayL.currentDelay);
    float delayedR = preDelayR.read(preDelayR.currentDelay);

    preDelayL.write(left);
    preDelayR.write(right);

    // Apply Haas effect for width enhancement
    float haasL = haasDelayL.read(haasDelayL.currentDelay);
    float haasR = haasDelayR.read(haasDelayR.currentDelay);

    haasDelayL.write(delayedR * crossFeedAmount);
    haasDelayR.write(delayedL * crossFeedAmount);

    // Mix original with delayed and cross-fed signals
    left = delayedL + haasL * 0.3f;
    right = delayedR + haasR * 0.3f;
}

void AdvancedReverbEngine::PsychoacousticProcessor::setPreDelay(float ms)
{
    float samples = (ms / 1000.0f) * sampleRate;
    preDelayL.setDelay(samples);
    preDelayR.setDelay(samples);
}

void AdvancedReverbEngine::PsychoacousticProcessor::setWidth(float amount)
{
    crossFeedAmount = juce::jlimit(0.0f, 0.5f, amount * 0.3f);

    // Adjust Haas delays for width (3-30ms range)
    float haasMs = 3.0f + amount * 27.0f;
    setHaasDelay(haasMs);
}

void AdvancedReverbEngine::PsychoacousticProcessor::setHaasDelay(float ms)
{
    float samples = (ms / 1000.0f) * sampleRate;
    haasDelayL.setDelay(samples);
    haasDelayR.setDelay(samples * 1.1f);  // Slight asymmetry
}

//==============================================================================
// RoomEarlyReflections implementation
//==============================================================================

void AdvancedReverbEngine::RoomEarlyReflections::prepare(double sr)
{
    sampleRate = sr;

    int maxDelay = static_cast<int>(sr * 0.1);  // 100ms max for early reflections
    delayLineL.prepare(maxDelay);
    delayLineR.prepare(maxDelay);

    generateReflectionPattern(0.5f, 0.5f);
}

void AdvancedReverbEngine::RoomEarlyReflections::process(float inputL, float inputR,
                                                         float& outputL, float& outputR)
{
    // Write input to delay lines
    delayLineL.write(inputL);
    delayLineR.write(inputR);

    outputL = 0.0f;
    outputR = 0.0f;

    // Sum early reflection taps
    for (const auto& tap : taps)
    {
        float delaySamples = (tap.delayMs / 1000.0f) * sampleRate;

        float tapL = delayLineL.read(delaySamples);
        float tapR = delayLineR.read(delaySamples);

        // Apply distance filtering (air absorption)
        float filteredL = tapL + (tapL - tapL) * tap.filterCoeff;
        float filteredR = tapR + (tapR - tapR) * tap.filterCoeff;

        outputL += filteredL * tap.gainL;
        outputR += filteredR * tap.gainR;
    }

    outputL *= 0.5f;  // Scale down
    outputR *= 0.5f;
}

void AdvancedReverbEngine::RoomEarlyReflections::setRoomSize(float size)
{
    generateReflectionPattern(size, 0.5f);
}

void AdvancedReverbEngine::RoomEarlyReflections::setRoomShape(float shape)
{
    generateReflectionPattern(0.5f, shape);
}

void AdvancedReverbEngine::RoomEarlyReflections::generateReflectionPattern(float size, float shape)
{
    // Generate physically-inspired early reflection pattern
    std::mt19937 gen(42);  // Fixed seed for consistency
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    for (int i = 0; i < NUM_EARLY_TAPS; ++i)
    {
        // Time distribution follows room size
        float normalizedTime = static_cast<float>(i) / NUM_EARLY_TAPS;
        float timeExponent = 1.0f + shape;  // Shape affects time distribution

        taps[i].delayMs = std::pow(normalizedTime, timeExponent) * size * 100.0f;

        // Amplitude follows inverse square law with randomization
        float distance = taps[i].delayMs / 10.0f;  // Approximate distance
        float baseGain = 1.0f / (1.0f + distance * distance);

        // Randomize gain and panning
        float randomFactor = 0.7f + dist(gen) * 0.3f;
        float pan = dist(gen);

        taps[i].gainL = baseGain * randomFactor * (1.0f - pan) * 0.5f;
        taps[i].gainR = baseGain * randomFactor * (1.0f + pan) * 0.5f;

        // High frequency damping based on distance
        taps[i].filterCoeff = std::exp(-distance * 0.1f);
    }
}

//==============================================================================
// Oversampler implementation
//==============================================================================

void AdvancedReverbEngine::Oversampler::prepare(double sampleRate, int factor)
{
    oversampling.initProcessing(512);
    oversampling.reset();
}

void AdvancedReverbEngine::Oversampler::upsample(const float* input, float* output, int numSamples)
{
    // This would use the JUCE oversampling class
    // For now, simple copy (no actual oversampling)
    std::copy(input, input + numSamples, output);
}

void AdvancedReverbEngine::Oversampler::downsample(const float* input, float* output, int numSamples)
{
    // This would use the JUCE oversampling class
    // For now, simple copy (no actual downsampling)
    std::copy(input, input + numSamples, output);
}