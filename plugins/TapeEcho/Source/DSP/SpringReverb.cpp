#include "SpringReverb.h"
#include <cmath>

SpringReverb::SpringReverb()
{
    // Prime number delay times for natural sound
    const int baseSampleRate = 44100;

    // Comb filter delay times (in samples at 44.1kHz)
    const int combDelays[] = {1613, 1867, 2053, 2251};

    // Allpass filter delay times
    const int allpassDelays[] = {307, 613, 919, 1223, 1531, 1837};

    // Initialize delay lines with scaled sizes
    for (int i = 0; i < NUM_DELAY_LINES; ++i)
    {
        delayLinesL[i].init(combDelays[i]);
        delayLinesR[i].init(combDelays[i] + 23); // Slight offset for stereo width
    }

    // Initialize allpass filters
    for (int i = 0; i < NUM_ALLPASS; ++i)
    {
        allpassL[i].init(allpassDelays[i]);
        allpassR[i].init(allpassDelays[i] + 13);
    }
}

void SpringReverb::prepare(double sr, int maxBlockSize)
{
    sampleRate = static_cast<float>(sr);

    // Allocate pre-delay buffer (up to 200ms)
    int maxPreDelaySamples = static_cast<int>(0.2f * sampleRate);
    preDelayBufferL.resize(maxPreDelaySamples);
    preDelayBufferR.resize(maxPreDelaySamples);

    // Scale delay line sizes for current sample rate
    float sampleRateRatio = sampleRate / 44100.0f;

    // Reinitialize delay lines with scaled sizes
    const int combDelays[] = {1613, 1867, 2053, 2251};
    const int allpassDelays[] = {307, 613, 919, 1223, 1531, 1837};

    for (int i = 0; i < NUM_DELAY_LINES; ++i)
    {
        delayLinesL[i].init(static_cast<int>(combDelays[i] * sampleRateRatio));
        delayLinesR[i].init(static_cast<int>((combDelays[i] + 23) * sampleRateRatio));
    }

    for (int i = 0; i < NUM_ALLPASS; ++i)
    {
        allpassL[i].init(static_cast<int>(allpassDelays[i] * sampleRateRatio));
        allpassR[i].init(static_cast<int>((allpassDelays[i] + 13) * sampleRateRatio));
    }

    // Setup character filters for spring sound
    // Bandpass filter centered around 2-3kHz to simulate spring resonance
    characterFilterL.setCoefficients(juce::IIRCoefficients::makeBandPass(sampleRate, 2500.0f, 2.0f));
    characterFilterR.setCoefficients(juce::IIRCoefficients::makeBandPass(sampleRate, 2500.0f, 2.0f));

    reset();
    updateDelayTimes();
}

void SpringReverb::reset()
{
    std::fill(preDelayBufferL.begin(), preDelayBufferL.end(), 0.0f);
    std::fill(preDelayBufferR.begin(), preDelayBufferR.end(), 0.0f);
    preDelayWritePos = 0;
    lfoPhase = 0.0f;

    for (auto& dl : delayLinesL) dl.reset();
    for (auto& dl : delayLinesR) dl.reset();
    for (auto& ap : allpassL) ap.reset();
    for (auto& ap : allpassR) ap.reset();

    characterFilterL.reset();
    characterFilterR.reset();
}

void SpringReverb::setDecayTime(float seconds)
{
    decayTime = juce::jlimit(0.1f, 10.0f, seconds);
    updateDelayTimes();
}

void SpringReverb::setDamping(float amount)
{
    damping = juce::jlimit(0.0f, 1.0f, amount);

    for (auto& dl : delayLinesL) dl.damping = damping * 0.8f;
    for (auto& dl : delayLinesR) dl.damping = damping * 0.8f;
}

void SpringReverb::setPreDelay(float ms)
{
    preDelayMs = juce::jlimit(0.0f, 200.0f, ms);
    preDelaySize = static_cast<int>(preDelayMs * sampleRate / 1000.0f);
}

void SpringReverb::setDiffusion(float amount)
{
    diffusion = juce::jlimit(0.0f, 1.0f, amount);

    float gain = 0.3f + diffusion * 0.4f; // Range: 0.3 to 0.7
    for (auto& ap : allpassL) ap.gain = gain;
    for (auto& ap : allpassR) ap.gain = gain;
}

void SpringReverb::setModulation(float depth, float rate)
{
    modulationDepth = juce::jlimit(0.0f, 1.0f, depth);
    modulationRate = juce::jlimit(0.1f, 5.0f, rate);
}

void SpringReverb::updateDelayTimes()
{
    // Calculate feedback based on decay time
    // Using the formula: feedback = pow(0.001, delayTime / (decayTime * sampleRate))
    for (int i = 0; i < NUM_DELAY_LINES; ++i)
    {
        float delayTimeSeconds = delayLinesL[i].size / sampleRate;
        float feedback = std::pow(0.001f, delayTimeSeconds / decayTime);

        delayLinesL[i].feedback = feedback;
        delayLinesR[i].feedback = feedback;
    }
}

float SpringReverb::processSpringCharacter(float input, int channel)
{
    // Apply characteristic spring reverb coloration
    // Springs have a metallic, slightly resonant quality

    float output = input;

    // Add some non-linearity to simulate spring physics
    output = std::tanh(output * springTension) / springTension;

    // Apply bandpass filtering for spring resonance
    if (channel == 0)
        output = characterFilterL.processSingleSampleRaw(output);
    else
        output = characterFilterR.processSingleSampleRaw(output);

    // Mix with original for subtle effect
    return input * 0.7f + output * 0.3f;
}

float SpringReverb::processSample(float input, int channel)
{
    float output = 0.0f;

    // Pre-delay
    float preDelayedSample = input;
    if (preDelaySize > 0)
    {
        int readPos = preDelayWritePos - preDelaySize;
        if (readPos < 0) readPos += static_cast<int>(preDelayBufferL.size());

        if (channel == 0)
        {
            preDelayedSample = preDelayBufferL[readPos];
            preDelayBufferL[preDelayWritePos] = input;
        }
        else
        {
            preDelayedSample = preDelayBufferR[readPos];
            preDelayBufferR[preDelayWritePos] = input;
        }
    }

    // Apply spring character to input
    float springInput = processSpringCharacter(preDelayedSample, channel);

    // Process through parallel comb filters
    if (channel == 0)
    {
        for (auto& dl : delayLinesL)
        {
            output += dl.process(springInput) * 0.25f; // Scale for 4 delay lines
        }
    }
    else
    {
        for (auto& dl : delayLinesR)
        {
            output += dl.process(springInput) * 0.25f;
        }
    }

    // Apply series allpass filters for diffusion
    if (channel == 0)
    {
        for (auto& ap : allpassL)
        {
            output = ap.process(output);
        }
    }
    else
    {
        for (auto& ap : allpassR)
        {
            output = ap.process(output);
        }
    }

    // Apply modulation (subtle pitch variation)
    if (modulationDepth > 0.0f)
    {
        lfoPhase += modulationRate / sampleRate;
        if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;

        float modulation = std::sin(lfoPhase * 2.0f * juce::MathConstants<float>::pi);
        output *= (1.0f + modulation * modulationDepth * 0.02f);
    }

    // Update pre-delay write position (once per stereo pair)
    if (channel == 1)
    {
        preDelayWritePos++;
        if (preDelayWritePos >= static_cast<int>(preDelayBufferL.size()))
            preDelayWritePos = 0;
    }

    // Soft clipping to prevent harsh distortion
    return std::tanh(output * 0.7f) / 0.7f;
}