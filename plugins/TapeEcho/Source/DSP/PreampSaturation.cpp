#include "PreampSaturation.h"

void PreampSaturation::prepare(double sr, int maxBlockSize)
{
    sampleRate = static_cast<float>(sr);

    oversampler.initProcessing(static_cast<size_t>(maxBlockSize));
    oversampler.reset();

    reset();
}

void PreampSaturation::reset()
{
    dcBlockerX1 = 0.0f;
    dcBlockerY1 = 0.0f;
    oversampler.reset();
}

void PreampSaturation::setInputGain(float gain)
{
    inputGain = juce::jlimit(0.0f, 4.0f, gain);
}

void PreampSaturation::setSaturationAmount(float amount)
{
    saturationAmount = juce::jlimit(0.0f, 1.0f, amount);
}

void PreampSaturation::setCharacter(float ch)
{
    character = juce::jlimit(0.0f, 1.0f, ch);
}

float PreampSaturation::processTanhSaturation(float input)
{
    // Classic tanh saturation
    float drive = 1.0f + saturationAmount * 4.0f;
    return std::tanh(input * drive) / drive;
}

float PreampSaturation::processAsymmetricSaturation(float input)
{
    // Asymmetric saturation for more even harmonics
    float drive = 1.0f + saturationAmount * 3.0f;
    float biased = input + saturationAmount * 0.1f; // Slight DC bias

    float saturated;
    if (biased > 0.0f)
    {
        saturated = std::tanh(biased * drive * 1.2f) / (drive * 1.2f);
    }
    else
    {
        saturated = std::tanh(biased * drive * 0.8f) / (drive * 0.8f);
    }

    return saturated;
}

float PreampSaturation::processVintageSaturation(float input)
{
    // Vintage transistor-style saturation with soft knee
    float drive = 1.0f + saturationAmount * 5.0f;
    float threshold = 0.7f;

    float absInput = std::abs(input);
    float sign = input < 0.0f ? -1.0f : 1.0f;

    float output;
    if (absInput < threshold)
    {
        // Linear region with slight coloration
        output = input * (1.0f + saturationAmount * 0.2f);
    }
    else
    {
        // Soft compression region
        float excess = absInput - threshold;
        float compressed = threshold + std::tanh(excess * drive) / drive;
        output = sign * compressed;
    }

    // Add subtle second harmonic
    output += std::sin(input * juce::MathConstants<float>::pi) * saturationAmount * 0.05f;

    return output;
}

float PreampSaturation::processDCBlocker(float input)
{
    // First-order DC blocking filter
    float output = input - dcBlockerX1 + dcBlockerCoeff * dcBlockerY1;
    dcBlockerX1 = input;
    dcBlockerY1 = output;
    return output;
}

float PreampSaturation::processSample(float input)
{
    if (saturationAmount < 0.001f && std::abs(inputGain - 1.0f) < 0.001f)
    {
        return input; // Bypass if no processing needed
    }

    // Apply input gain
    float processed = input * inputGain;

    // Oversample for better saturation quality
    float* processedPtr = &processed;
    juce::dsp::AudioBlock<float> inputBlock(&processedPtr, 1, 1);
    juce::dsp::AudioBlock<float> oversampledBlock = oversampler.processSamplesUp(inputBlock);

    float* oversampledData = oversampledBlock.getChannelPointer(0);
    size_t numSamples = oversampledBlock.getNumSamples();

    for (size_t i = 0; i < numSamples; ++i)
    {
        float sample = oversampledData[i];

        // Blend between different saturation types based on character
        float clean = sample;
        float vintage = processVintageSaturation(sample);
        float asymmetric = processAsymmetricSaturation(sample);

        float saturated;
        if (character < 0.5f)
        {
            // Blend between clean and vintage
            float blend = character * 2.0f;
            saturated = clean * (1.0f - blend) + vintage * blend;
        }
        else
        {
            // Blend between vintage and asymmetric
            float blend = (character - 0.5f) * 2.0f;
            saturated = vintage * (1.0f - blend) + asymmetric * blend;
        }

        oversampledData[i] = saturated;
    }

    // Downsample back to original rate
    oversampler.processSamplesDown(inputBlock);

    // DC blocking to remove any DC offset from saturation
    processed = processDCBlocker(processed);

    // Makeup gain compensation
    float makeupGain = 1.0f / (1.0f + saturationAmount * 0.3f);
    return processed * makeupGain;
}