/*
  ==============================================================================

    ReverbEngine.cpp - Implementation of FDN-based reverb engine

  ==============================================================================
*/

#include "ReverbEngine.h"
#include <cmath>

ReverbEngine::ReverbEngine()
{
    initializeMixMatrix();
}

void ReverbEngine::prepare(double sr, int maxBlock)
{
    sampleRate = sr;
    blockSize = maxBlock;

    // Prepare delay lines with scaled prime delays
    for (int i = 0; i < NUM_DELAY_LINES; ++i)
    {
        int scaledDelay = static_cast<int>(primeDelays[i] * (sampleRate / 44100.0));
        delayLines[i].prepare(MAX_DELAY_SAMPLES);
        delayLines[i].setDelayTime(scaledDelay);
    }

    // Prepare diffusion allpass filters
    for (int i = 0; i < NUM_ALLPASS; ++i)
    {
        int diffuserDelay = 113 + i * 37;  // Prime-based spacing
        diffuserDelay = static_cast<int>(diffuserDelay * (sampleRate / 44100.0));

        inputDiffusers[i].prepare(2000);
        inputDiffusers[i].setDelayTime(diffuserDelay);

        outputDiffusers[i].prepare(2000);
        outputDiffusers[i].setDelayTime(diffuserDelay + 50);
    }

    // Prepare early reflections
    earlyReflections.prepare(static_cast<int>(sampleRate * 0.2));  // 200ms max
    earlyReflections.generateTaps(size, shape);

    // Setup modulation LFOs with different frequencies
    for (int i = 0; i < 4; ++i)
    {
        modulationLFOs[i].setSampleRate(sampleRate);
        modulationLFOs[i].frequency = 0.1f + i * 0.07f;  // 0.1Hz to 0.31Hz
        modulationLFOs[i].depth = modulation * 0.001f;   // Small pitch variations
    }

    // Setup damping filters
    for (auto& filter : dampingFilters)
    {
        filter.setSampleRate(sampleRate);
    }

    reset();
}

void ReverbEngine::reset()
{
    for (auto& line : delayLines)
    {
        std::fill(line.buffer.begin(), line.buffer.end(), 0.0f);
        line.lastOut = 0.0f;
        line.writePos = 0;
    }

    for (auto& ap : inputDiffusers)
    {
        std::fill(ap.buffer.begin(), ap.buffer.end(), 0.0f);
        ap.writePos = 0;
    }

    for (auto& ap : outputDiffusers)
    {
        std::fill(ap.buffer.begin(), ap.buffer.end(), 0.0f);
        ap.writePos = 0;
    }

    std::fill(earlyReflections.buffer.begin(), earlyReflections.buffer.end(), 0.0f);
    earlyReflections.writePos = 0;
}

void ReverbEngine::process(juce::AudioBuffer<float>& buffer, int numSamples)
{
    auto* leftIn = buffer.getReadPointer(0);
    auto* rightIn = buffer.getReadPointer(1);
    auto* leftOut = buffer.getWritePointer(0);
    auto* rightOut = buffer.getWritePointer(1);

    processStereo(const_cast<float*>(leftIn), const_cast<float*>(rightIn),
                  leftOut, rightOut, numSamples);
}

void ReverbEngine::processStereo(float* leftIn, float* rightIn,
                                 float* leftOut, float* rightOut, int numSamples)
{
    for (int sample = 0; sample < numSamples; ++sample)
    {
        // Mix input to mono for processing
        float input = (leftIn[sample] + rightIn[sample]) * 0.5f;

        // Process early reflections
        auto [earlyL, earlyR] = earlyReflections.process(input);

        // Input diffusion network
        float diffused = input;
        for (int i = 0; i < 4; ++i)  // Use first 4 diffusers for input
        {
            diffused = inputDiffusers[i].process(diffused);
        }

        // Process through FDN
        float fdnOutL = processFDN(diffused, 0);
        float fdnOutR = processFDN(diffused, 1);

        // Output diffusion network
        for (int i = 4; i < 8; ++i)  // Use last 4 diffusers for output
        {
            fdnOutL = outputDiffusers[i].process(fdnOutL);
            fdnOutR = outputDiffusers[i].process(fdnOutR);
        }

        // Mix early and late reflections based on shape parameter
        float lateAmount = shape;
        float earlyAmount = 1.0f - shape;

        leftOut[sample] = earlyL * earlyAmount + fdnOutL * lateAmount;
        rightOut[sample] = earlyR * earlyAmount + fdnOutR * lateAmount;

        // Apply stereo width
        if (spread < 1.0f)
        {
            float mid = (leftOut[sample] + rightOut[sample]) * 0.5f;
            float side = (leftOut[sample] - rightOut[sample]) * 0.5f;
            side *= spread;
            leftOut[sample] = mid + side;
            rightOut[sample] = mid - side;
        }
    }
}

float ReverbEngine::processFDN(float input, int channel)
{
    std::array<float, NUM_DELAY_LINES> delayOutputs;

    // Read from delay lines
    for (int i = 0; i < NUM_DELAY_LINES; ++i)
    {
        delayOutputs[i] = delayLines[i].buffer[delayLines[i].writePos];
    }

    // Apply Hadamard matrix mixing
    std::array<float, NUM_DELAY_LINES> mixed;
    for (int i = 0; i < NUM_DELAY_LINES; ++i)
    {
        mixed[i] = 0.0f;
        for (int j = 0; j < NUM_DELAY_LINES; ++j)
        {
            mixed[i] += delayOutputs[j] * mixMatrix[i][j];
        }
        mixed[i] *= 0.25f;  // Normalize
    }

    // Write to delay lines with feedback, damping, and modulation
    for (int i = 0; i < NUM_DELAY_LINES; ++i)
    {
        // Apply modulation to delay time
        float modAmount = modulationLFOs[i % 4].process();
        delayLines[i].modulate(modAmount);

        // Apply damping
        float damped = dampingFilters[i].process(mixed[i]);

        // Write to delay line with input injection
        float toWrite = input * 0.0625f + damped * delayLines[i].feedback;
        toWrite = softClip(toWrite);  // Prevent overflow

        delayLines[i].buffer[delayLines[i].writePos] = toWrite;
        delayLines[i].lastOut = toWrite;

        // Advance write position
        delayLines[i].writePos = (delayLines[i].writePos + 1) % delayLines[i].size;
    }

    // Sum outputs with decorrelation for stereo
    float output = 0.0f;
    for (int i = 0; i < NUM_DELAY_LINES; ++i)
    {
        // Use different taps for left/right channels
        int tap = channel == 0 ? i : (i + NUM_DELAY_LINES / 2) % NUM_DELAY_LINES;
        output += delayOutputs[tap] * (1.0f / NUM_DELAY_LINES);
    }

    return output;
}

void ReverbEngine::initializeMixMatrix()
{
    // Initialize Hadamard-like orthogonal matrix for even energy distribution
    // This creates a householder reflection matrix
    float n = static_cast<float>(NUM_DELAY_LINES);
    float factor = 2.0f / n;

    for (int i = 0; i < NUM_DELAY_LINES; ++i)
    {
        for (int j = 0; j < NUM_DELAY_LINES; ++j)
        {
            if (i == j)
            {
                mixMatrix[i][j] = 1.0f - factor;
            }
            else
            {
                mixMatrix[i][j] = -factor;
            }
        }
    }
}

void ReverbEngine::setSize(float newSize)
{
    size = juce::jlimit(0.0f, 1.0f, newSize);
    updateDelayTimes();
    earlyReflections.generateTaps(size, shape);

    // Update feedback based on size for proper decay
    float targetRT60 = 0.5f + size * 9.5f;  // 0.5 to 10 seconds
    currentDecayTime = targetRT60;

    for (auto& line : delayLines)
    {
        // RT60 = -60dB / (feedback_per_second * 20 * log10(feedback))
        float feedback = std::pow(0.001f, 1.0f / (targetRT60 * sampleRate / line.size));
        line.feedback = juce::jlimit(0.0f, 0.99f, feedback);
    }
}

void ReverbEngine::setDiffusion(float newDiffusion)
{
    diffusion = juce::jlimit(0.0f, 1.0f, newDiffusion);

    // Update allpass feedback coefficients
    for (auto& ap : inputDiffusers)
    {
        ap.feedback = 0.3f + diffusion * 0.4f;  // 0.3 to 0.7
    }

    for (auto& ap : outputDiffusers)
    {
        ap.feedback = 0.2f + diffusion * 0.5f;  // 0.2 to 0.7
    }
}

void ReverbEngine::setDensity(float newDensity)
{
    density = juce::jlimit(0.0f, 1.0f, newDensity);

    // Adjust delay line distribution
    updateDelayTimes();
}

void ReverbEngine::setDamping(float newDamping)
{
    damping = juce::jlimit(0.0f, 1.0f, newDamping);

    // Update damping filter frequencies
    for (auto& filter : dampingFilters)
    {
        filter.frequency = 20000.0f - damping * 19000.0f;  // 20kHz to 1kHz
        filter.amount = damping;
    }
}

void ReverbEngine::setModulation(float newModulation)
{
    modulation = juce::jlimit(0.0f, 1.0f, newModulation);

    for (auto& lfo : modulationLFOs)
    {
        lfo.depth = modulation * 0.002f;  // Up to 2 samples variation
    }
}

void ReverbEngine::setShape(float newShape)
{
    shape = juce::jlimit(0.0f, 1.0f, newShape);
    earlyReflections.generateTaps(size, shape);
}

void ReverbEngine::setSpread(float newSpread)
{
    spread = juce::jlimit(0.0f, 1.0f, newSpread);
}

void ReverbEngine::setAttack(float newAttack)
{
    attack = juce::jlimit(0.0f, 1.0f, newAttack);

    // Adjust input diffusion based on attack
    for (int i = 0; i < NUM_ALLPASS; ++i)
    {
        int baseDelay = 113 + i * 37;
        int attackDelay = static_cast<int>(baseDelay * (1.0f + attack * 2.0f));
        attackDelay = static_cast<int>(attackDelay * (sampleRate / 44100.0));
        inputDiffusers[i].setDelayTime(attackDelay);
    }
}

void ReverbEngine::updateDelayTimes()
{
    // Scale delay times based on size and density
    for (int i = 0; i < NUM_DELAY_LINES; ++i)
    {
        float scaleFactor = 0.5f + size * 2.0f;  // 0.5x to 2.5x
        float densityFactor = 0.8f + density * 0.4f;  // 0.8x to 1.2x

        int newDelay = static_cast<int>(primeDelays[i] * scaleFactor * densityFactor);
        newDelay = static_cast<int>(newDelay * (sampleRate / 44100.0));
        newDelay = juce::jlimit(10, MAX_DELAY_SAMPLES - 1, newDelay);

        delayLines[i].setDelayTime(newDelay);
    }
}

float ReverbEngine::softClip(float input)
{
    // Soft saturation to prevent harsh clipping
    const float threshold = 0.95f;

    if (std::abs(input) < threshold)
        return input;

    float sign = input < 0.0f ? -1.0f : 1.0f;
    float amount = std::abs(input) - threshold;
    float clipped = threshold + std::tanh(amount * 2.0f) * (1.0f - threshold);

    return sign * clipped;
}

float ReverbEngine::crossfade(float a, float b, float mix)
{
    return a * (1.0f - mix) + b * mix;
}

void ReverbEngine::configureForMode(int mode)
{
    // Configure engine for specific reverb modes
    // This would set various internal parameters to emulate different algorithms

    switch (mode)
    {
        case 0:  // Concert Hall
            setSize(0.8f);
            setDiffusion(0.85f);
            setDensity(0.7f);
            setDamping(0.3f);
            setModulation(0.2f);
            setShape(0.6f);
            break;

        case 1:  // Bright Hall
            setSize(0.7f);
            setDiffusion(0.75f);
            setDensity(0.6f);
            setDamping(0.1f);
            setModulation(0.15f);
            setShape(0.5f);
            break;

        case 2:  // Plate
            setSize(0.5f);
            setDiffusion(0.9f);
            setDensity(0.9f);
            setDamping(0.2f);
            setModulation(0.3f);
            setShape(0.3f);
            break;

        // ... Additional modes would be configured here

        default:
            break;
    }
}

// DelayLine implementation
void ReverbEngine::DelayLine::prepare(int maxSize)
{
    buffer.resize(maxSize);
    std::fill(buffer.begin(), buffer.end(), 0.0f);
}

float ReverbEngine::DelayLine::process(float input)
{
    float output = buffer[writePos];
    buffer[writePos] = input;
    writePos = (writePos + 1) % size;
    return output;
}

void ReverbEngine::DelayLine::setDelayTime(int samples)
{
    size = juce::jlimit(1, static_cast<int>(buffer.size()) - 1, samples);
    writePos = writePos % size;
}

void ReverbEngine::DelayLine::modulate(float modAmount)
{
    // Simple pitch modulation by varying read position
    // In a production implementation, this would use interpolation
    int modSamples = static_cast<int>(modAmount * size);
    int newSize = size + modSamples;
    newSize = juce::jlimit(1, static_cast<int>(buffer.size()) - 1, newSize);

    // Temporarily adjust size for this sample
    // A more sophisticated implementation would use fractional delays
    size = newSize;
}

// AllpassFilter implementation
void ReverbEngine::AllpassFilter::prepare(int maxSize)
{
    buffer.resize(maxSize);
    std::fill(buffer.begin(), buffer.end(), 0.0f);
}

float ReverbEngine::AllpassFilter::process(float input)
{
    float delayed = buffer[writePos];
    float output = -input + delayed;
    buffer[writePos] = input + delayed * feedback;
    writePos = (writePos + 1) % size;
    return output;
}

void ReverbEngine::AllpassFilter::setDelayTime(int samples)
{
    size = juce::jlimit(1, static_cast<int>(buffer.size()) - 1, samples);
    writePos = writePos % size;
}

// EarlyReflections implementation
void ReverbEngine::EarlyReflections::prepare(int maxSize)
{
    buffer.resize(maxSize);
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    generateTaps(0.5f, 0.5f);
}

void ReverbEngine::EarlyReflections::generateTaps(float size, float shape)
{
    // Generate early reflection tap pattern based on room size and shape
    std::mt19937 gen(42);  // Fixed seed for reproducible pattern
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    for (int i = 0; i < NUM_TAPS; ++i)
    {
        // Exponentially distributed delays
        float time = std::pow(static_cast<float>(i) / NUM_TAPS, 1.5f);
        tapDelays[i] = static_cast<int>(time * size * 4410);  // Up to 100ms at 44.1kHz

        // Decreasing amplitudes
        tapGains[i] = std::pow(0.8f, i * 0.5f) * (0.5f + dist(gen) * 0.5f);

        // Random panning
        tapPans[i] = dist(gen) * 2.0f - 1.0f;
    }
}

std::pair<float, float> ReverbEngine::EarlyReflections::process(float input)
{
    // Write input to buffer
    buffer[writePos] = input;

    float outputL = 0.0f;
    float outputR = 0.0f;

    // Sum taps with panning
    for (int i = 0; i < NUM_TAPS; ++i)
    {
        int readPos = (writePos - tapDelays[i] + buffer.size()) % buffer.size();
        float tap = buffer[readPos] * tapGains[i];

        float panL = (1.0f - tapPans[i]) * 0.5f;
        float panR = (1.0f + tapPans[i]) * 0.5f;

        outputL += tap * panL;
        outputR += tap * panR;
    }

    writePos = (writePos + 1) % buffer.size();

    return {outputL * 0.5f, outputR * 0.5f};  // Scale down
}

// ModulationLFO implementation
float ReverbEngine::ModulationLFO::process()
{
    phase += phaseIncrement;
    if (phase >= 1.0f)
        phase -= 1.0f;

    return std::sin(phase * 2.0f * juce::MathConstants<float>::pi) * depth;
}

void ReverbEngine::ModulationLFO::setSampleRate(double sr)
{
    sampleRate = static_cast<float>(sr);
    phaseIncrement = frequency / sampleRate;
}

// DampingFilter implementation
float ReverbEngine::DampingFilter::process(float input)
{
    // Simple one-pole lowpass filter
    float output = input * (1.0f - coefficient) + lastOut * coefficient;
    lastOut = output;
    return output * (1.0f - amount) + input * amount;  // Dry/wet mix
}

void ReverbEngine::DampingFilter::setSampleRate(double sr)
{
    sampleRate = static_cast<float>(sr);
    coefficient = std::exp(-2.0f * juce::MathConstants<float>::pi * frequency / sampleRate);
}