/*
  ==============================================================================

    ReverbEngineEnhanced.cpp
    Studio Verb - Enhanced Realistic Reverb DSP Engine Implementation
    Copyright (c) 2024 Luna Co. Audio

    Implementation of high-quality reverb algorithms including:
    - 32-channel FDN with per-channel modulation
    - Dattorro plate reverb topology
    - Enhanced early reflections with diffusion
    - Oversampled non-linear processing

  ==============================================================================
*/

#include "ReverbEngineEnhanced.h"
#include <cmath>

// The header file already contains MultibandDecay and HouseholderMatrix definitions

//==============================================================================
// Dattorro Plate Reverb Implementation
//==============================================================================

class DattorroPlateReverb
{
public:
    DattorroPlateReverb() = default;

    void prepare(const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;

        // Input diffusion stage (4 cascaded allpass filters)
        for (int i = 0; i < 4; ++i)
        {
            inputDiffusionAPF[i].prepare(spec);
            inputDiffusionAPF[i].setMaximumDelayInSamples(2000);
        }

        // Set input diffusion delays (scaled to sample rate)
        float baseDelays[4] = { 142.0f, 107.0f, 379.0f, 277.0f };
        for (int i = 0; i < 4; ++i)
        {
            float delaySamples = baseDelays[i] * (sampleRate / 48000.0f);
            inputDiffusionAPF[i].setDelay(delaySamples);
        }

        // Tank delays (modulated comb filters forming figure-8 topology)
        for (int i = 0; i < 8; ++i)
        {
            tankDelays[i].prepare(spec);
            tankDelays[i].setMaximumDelayInSamples(10000);

            // Tank allpass filters for additional diffusion
            tankAPF[i].prepare(spec);
            tankAPF[i].setMaximumDelayInSamples(3000);
        }

        // Set tank delay times (prime numbers for inharmonic response)
        float tankDelayTimes[8] = {
            4453.0f, 4217.0f, 3720.0f, 3163.0f,
            1800.0f, 2656.0f, 1580.0f, 1410.0f
        };

        float tankAPFTimes[8] = {
            908.0f, 672.0f, 1800.0f, 2320.0f,
            335.0f, 121.0f, 1913.0f, 1996.0f
        };

        for (int i = 0; i < 8; ++i)
        {
            float delaySamples = tankDelayTimes[i] * (sampleRate / 48000.0f);
            tankDelays[i].setDelay(delaySamples);

            float apfSamples = tankAPFTimes[i] * (sampleRate / 48000.0f);
            tankAPF[i].setDelay(apfSamples);
        }

        // Initialize modulation LFOs
        juce::Random phaseRandom;
        phaseRandom.setSeedRandomly();

        for (int i = 0; i < 8; ++i)
        {
            modulationLFOs[i].initialise([](float x) { return std::sin(x); });

            // Different rates for each LFO (0.2 Hz to 1.81 Hz) - wider spread to break up metallic artifacts
            float rate = 0.2f + (i * 0.23f);  // Spans 0.2 to 1.81 Hz (for i=0..7)
            modulationLFOs[i].setFrequency(rate);
            modulationLFOs[i].prepare(spec);

            // Truly random phase offset for each LFO to break periodic patterns
            modulationPhase[i] = phaseRandom.nextFloat() * juce::MathConstants<float>::twoPi;
            modulationLFOs[i].reset();

            // Advance LFO to random phase
            // Calculate exact number of samples needed to reach target phase
            // numSamples = (targetPhase / 2π) * (sampleRate / lfoFrequency)
            int numSamples = static_cast<int>((modulationPhase[i] / juce::MathConstants<float>::twoPi) *
                                               (sampleRate / rate));
            numSamples = juce::jmax(0, numSamples);  // Ensure non-negative

            for (int p = 0; p < numSamples; ++p)
                modulationLFOs[i].processSample(0.0f);
        }

        // Damping filters
        for (int i = 0; i < 8; ++i)
        {
            dampingFilters[i].prepare(spec);
            dampingFilters[i].setType(juce::dsp::StateVariableTPTFilterType::lowpass);
            dampingFilters[i].setCutoffFrequency(4000.0f);
        }

        // Output diffusion stage (per Dattorro paper for smoothness)
        for (int i = 0; i < 4; ++i)
        {
            outputDiffusionAPF[i].prepare(spec);
            outputDiffusionAPF[i].setMaximumDelayInSamples(1000);
        }

        // Set output diffusion delays (add smoothness to output)
        float outputDelays[4] = { 89.0f, 127.0f, 179.0f, 227.0f };
        for (int i = 0; i < 4; ++i)
        {
            float delaySamples = outputDelays[i] * (sampleRate / 48000.0f);
            outputDiffusionAPF[i].setDelay(delaySamples);
        }

        // Initialize envelope followers for amplitude-dependent damping
        envelopeAttackCoeff = std::exp(-1.0f / (0.001f * sampleRate));   // 1ms attack
        envelopeReleaseCoeff = std::exp(-1.0f / (0.050f * sampleRate));  // 50ms release

        reset();
    }

    void process(float inputL, float inputR, float& outputL, float& outputR,
                 float size, float decay, float damping, float modDepth)
    {
        // Input diffusion stage
        float diffusedL = inputL;
        float diffusedR = inputR;

        // Apply input diffusion APFs with feedback
        for (int i = 0; i < 4; ++i)
        {
            float delayed = inputDiffusionAPF[i].popSample(0);
            float feedback = delayed * 0.5f; // Fixed feedback coefficient

            float input = (i % 2 == 0) ? diffusedL : diffusedR;
            inputDiffusionAPF[i].pushSample(0, input + feedback);

            if (i % 2 == 0)
                diffusedL = delayed - input * 0.5f;
            else
                diffusedR = delayed - input * 0.5f;
        }

        // Tank processing (figure-8 topology with cross-coupling)
        float tankL = 0.0f;
        float tankR = 0.0f;

        // Process tank with modulation and cross-feedback
        for (int i = 0; i < 8; ++i)
        {
            // Get modulated delay time - scale depth by user parameter for stronger resonance breaking
            // modDepth is user parameter (0-1), scale to 5 samples max for effective detuning
            float modulation = modulationLFOs[i].processSample(0.0f) * modDepth * 5.0f;
            float delayTime = tankDelays[i].getDelay() + modulation;
            delayTime = juce::jmax(1.0f, delayTime);
            tankDelays[i].setDelay(delayTime);

            // Read from delay
            float delayOut = tankDelays[i].popSample(0);

            // Track signal envelope for amplitude-dependent damping (per-channel)
            float inputLevel = std::abs(delayOut);
            float envelopeCoeff = (inputLevel > envelopeState[i]) ? envelopeAttackCoeff : envelopeReleaseCoeff;
            envelopeState[i] = envelopeCoeff * envelopeState[i] + (1.0f - envelopeCoeff) * inputLevel;
            // Clamp envelope to prevent out-of-bounds values from hot input samples
            envelopeState[i] = std::max(0.0f, std::min(1.0f, envelopeState[i]));

            // Apply non-linear (amplitude-dependent) damping for plate "sizzle"
            // At high levels: more HF rolloff (darker)
            // At low levels: less HF rolloff (brighter, adding shimmer/sizzle to decay tail)
            float baseDampFreq = 8000.0f * (1.0f - damping * 0.85f) + 400.0f;  // 8kHz to 1.6kHz range

            // Amplitude modulation factor (0.5 to 1.5 range)
            // Higher envelope = lower cutoff (more damping at loud levels)
            float envelopeFactor = 1.5f - envelopeState[i];  // Inverted: loud = less bright
            envelopeFactor = juce::jlimit(0.5f, 1.5f, envelopeFactor);

            float dynamicDampFreq = baseDampFreq * envelopeFactor;
            dynamicDampFreq = juce::jlimit(300.0f, 12000.0f, dynamicDampFreq);

            dampingFilters[i].setCutoffFrequency(dynamicDampFreq);
            delayOut = dampingFilters[i].processSample(0, delayOut);

            // Process through tank APF for additional diffusion
            float apfOut = tankAPF[i].popSample(0);
            float apfFeedback = apfOut * 0.6f;
            tankAPF[i].pushSample(0, delayOut + apfFeedback);
            float diffused = apfOut - delayOut * 0.6f;

            // Cross-coupling feedback (figure-8 pattern)
            int crossIndex = (i + 4) % 8;
            float crossFeedback = tankDelays[crossIndex].popSample(0) * 0.3f;

            // Size affects delay scaling
            float sizeScale = 0.5f + size * 1.5f;
            float scaledDelay = diffused * decay + crossFeedback;

            // Feed into delay with input
            float tankInput = (i < 4) ? diffusedL : diffusedR;
            tankDelays[i].pushSample(0, scaledDelay + tankInput * 0.1f);

            // Accumulate output taps
            if (i % 2 == 0)
                tankL += diffused * outputTapGains[i];
            else
                tankR += diffused * outputTapGains[i];
        }

        // Initial output scaling
        tankL *= 0.25f;
        tankR *= 0.25f;

        // Apply output diffusion stage for smoothness (Dattorro paper)
        float outputDiffusedL = tankL;
        float outputDiffusedR = tankR;

        for (int i = 0; i < 4; ++i)
        {
            float apfOut = outputDiffusionAPF[i].popSample(0);
            float feedback = 0.4f;  // Fixed feedback for output APFs

            float input = (i % 2 == 0) ? outputDiffusedL : outputDiffusedR;
            outputDiffusionAPF[i].pushSample(0, input + apfOut * feedback);

            if (i % 2 == 0)
                outputDiffusedL = apfOut - input * feedback;
            else
                outputDiffusedR = apfOut - input * feedback;
        }

        outputL = outputDiffusedL;
        outputR = outputDiffusedR;
    }

    void reset()
    {
        for (int i = 0; i < 4; ++i)
        {
            inputDiffusionAPF[i].reset();
            outputDiffusionAPF[i].reset();
        }

        for (int i = 0; i < 8; ++i)
        {
            tankDelays[i].reset();
            tankAPF[i].reset();
            dampingFilters[i].reset();
            modulationLFOs[i].reset();
            envelopeState[i] = 0.0f;  // Reset envelope followers
        }
    }

private:
    double sampleRate = 48000.0;

    // Input diffusion stage
    std::array<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>, 4> inputDiffusionAPF;

    // Tank (8 modulated delays with cross-coupling)
    std::array<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>, 8> tankDelays;
    std::array<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>, 8> tankAPF;
    std::array<juce::dsp::StateVariableTPTFilter<float>, 8> dampingFilters;

    // Output diffusion stage (for smoothness)
    std::array<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>, 4> outputDiffusionAPF;

    // Modulation
    std::array<juce::dsp::Oscillator<float>, 8> modulationLFOs;
    std::array<float, 8> modulationPhase;

    // Envelope followers for amplitude-dependent damping (adds plate "sizzle")
    std::array<float, 8> envelopeState = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    float envelopeAttackCoeff = 0.999f;   // 1ms attack
    float envelopeReleaseCoeff = 0.990f;  // 50ms release

    // Output tap gains (decorrelated pattern)
    const float outputTapGains[8] = {
        0.6f, -0.6f, 0.4f, -0.4f,
        -0.6f, 0.6f, -0.4f, 0.4f
    };
};

//==============================================================================
// Enhanced FDN with 32 channels and per-channel modulation
//==============================================================================

class EnhancedFeedbackDelayNetwork
{
public:
    static constexpr int NUM_DELAYS = 32;  // Increased from 16

    EnhancedFeedbackDelayNetwork()
        : mixingMatrix(NUM_DELAYS)
    {
        // Extended prime number delay lengths for 32 channels
        const int primeLengths[NUM_DELAYS] = {
            1433, 1601, 1867, 2053, 2251, 2399, 2617, 2797,
            3089, 3323, 3571, 3821, 4073, 4337, 4603, 4871,
            5147, 5419, 5701, 5987, 6277, 6571, 6869, 7177,
            7489, 7793, 8111, 8423, 8741, 9067, 9391, 9719
        };

        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            baseDelayLengths[i] = primeLengths[i];
        }
    }

    void prepare(const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;

        int maxNeededDelay = static_cast<int>(baseDelayLengths[NUM_DELAYS - 1] * 2.0f * (sampleRate / 48000.0) * 1.2);

        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            delays[i].prepare(spec);
            delays[i].setMaximumDelayInSamples(maxNeededDelay);

            float initialDelay = baseDelayLengths[i] * (sampleRate / 48000.0f);
            initialDelay = juce::jlimit(1.0f, static_cast<float>(maxNeededDelay - 1), initialDelay);
            delays[i].setDelay(initialDelay);

            decayFilters[i].prepare(sampleRate);
            inputDiffusion[i].prepare(spec);
            inputDiffusion[i].setMaximumDelayInSamples(1000);

            // Initialize per-channel modulation LFOs
            modulationLFOs[i].initialise([](float x) { return std::sin(x); });

            // Multiple LFO rates for rich modulation (some sine, some random)
            if (i < NUM_DELAYS / 2)
            {
                // Sine LFOs with varied rates
                float rate = 0.1f + (i * 0.05f); // 0.1 Hz to 0.9 Hz
                modulationLFOs[i].setFrequency(rate);
            }
            else
            {
                // Random modulation for second half
                modulationLFOs[i].initialise([](float x) {
                    return (std::sin(x) + std::sin(x * 3.7f) * 0.3f + std::sin(x * 7.3f) * 0.1f) / 1.4f;
                });
                float rate = 0.05f + ((i - NUM_DELAYS/2) * 0.03f); // Slower random modulation
                modulationLFOs[i].setFrequency(rate);
            }

            modulationLFOs[i].prepare(spec);
        }
    }

    void process(float inputL, float inputR, float& outputL, float& outputR,
                 float size, float decay, float damping, float modDepth)
    {
        size = juce::jmax(0.01f, size);
        decay = juce::jlimit(0.0f, 0.999f, decay);

        float delayOutputs[NUM_DELAYS];
        float delayInputs[NUM_DELAYS];

        // Read from delays with per-channel modulation
        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            // Apply per-channel modulation
            float modulation = modulationLFOs[i].processSample(0.0f);
            float modAmount = modulation * modDepth * 10.0f * (0.5f + size * 0.5f);

            float modulatedDelay = baseDelayLengths[i] * (0.5f + size * 1.5f) * (sampleRate / 48000.0f);
            modulatedDelay += modAmount;

            int maxDelayInSamples = delays[i].getMaximumDelayInSamples();
            modulatedDelay = juce::jlimit(1.0f, static_cast<float>(maxDelayInSamples - 1), modulatedDelay);
            delays[i].setDelay(modulatedDelay);

            delayOutputs[i] = delays[i].popSample(0);
        }

        // Mix through extended Householder matrix
        mixingMatrix.process(delayOutputs, delayInputs);

        // Apply decay and damping with enhanced multiband control
        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            float safetyFactor = 0.98f;
            float lowGain = juce::jlimit(0.0f, 0.999f, decay * 1.05f * safetyFactor);
            float midGain = juce::jlimit(0.0f, 0.999f, decay * safetyFactor);
            float highGain = juce::jlimit(0.0f, 0.999f, decay * (1.0f - damping * 0.5f) * safetyFactor);

            delayInputs[i] = decayFilters[i].process(delayInputs[i], lowGain, midGain, highGain);

            // Add input with enhanced decorrelation
            float input = (i % 2 == 0) ? inputL : inputR;

            // Apply input diffusion
            inputDiffusion[i].pushSample(0, input);
            float decorrelatedInput = inputDiffusion[i].popSample(0);

            // Vary input gain per channel for better diffusion
            float inputGain = 0.2f * (1.0f + std::sin(i * 0.7f) * 0.3f);
            delayInputs[i] += decorrelatedInput * inputGain;

            // Prevent explosion
            delayInputs[i] = juce::jlimit(-10.0f, 10.0f, delayInputs[i]);

            delays[i].pushSample(0, delayInputs[i]);
        }

        // Enhanced decorrelated stereo output
        outputL = outputR = 0.0f;
        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            float delayOut = delayOutputs[i];
            if (std::isnan(delayOut) || std::isinf(delayOut)) delayOut = 0.0f;
            delayOut = juce::jlimit(-10.0f, 10.0f, delayOut);

            // More complex decorrelation pattern
            float angle = (i * juce::MathConstants<float>::pi * 2.0f) / NUM_DELAYS;
            outputL += delayOut * std::cos(angle);
            outputR += delayOut * std::sin(angle);
        }

        outputL /= std::sqrt(static_cast<float>(NUM_DELAYS));
        outputR /= std::sqrt(static_cast<float>(NUM_DELAYS));

        outputL = juce::jlimit(-10.0f, 10.0f, outputL);
        outputR = juce::jlimit(-10.0f, 10.0f, outputR);
    }

    void reset()
    {
        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            delays[i].reset();
            decayFilters[i].reset();
            inputDiffusion[i].reset();
            modulationLFOs[i].reset();
        }
    }

private:
    double sampleRate = 48000.0;
    float baseDelayLengths[NUM_DELAYS];

    std::array<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>, NUM_DELAYS> delays;
    std::array<MultibandDecay, NUM_DELAYS> decayFilters;
    std::array<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None>, NUM_DELAYS> inputDiffusion;
    std::array<juce::dsp::Oscillator<float>, NUM_DELAYS> modulationLFOs;

    HouseholderMatrix mixingMatrix;
};

//==============================================================================
// Enhanced Early Reflections with Diffusion
//==============================================================================

class EnhancedSpatialEarlyReflections : public SpatialEarlyReflections
{
public:
    void prepare(const juce::dsp::ProcessSpec& spec)
    {
        SpatialEarlyReflections::prepare(spec);

        // Prepare diffusion allpass filters
        for (int i = 0; i < 20; ++i)
        {
            diffusionAPF[i].prepare(spec);
            diffusionAPF[i].setMaximumDelayInSamples(500);

            // Set varying delays for diffusion
            float delay = 5.0f + i * 3.7f; // 5ms to 75ms range
            diffusionAPF[i].setDelay(delay * spec.sampleRate / 1000.0f);
        }

        // Prepare absorption filters
        for (int i = 0; i < 50; ++i)
        {
            absorptionFilters[i].prepare(spec);
            absorptionFilters[i].setType(juce::dsp::StateVariableTPTFilterType::lowpass);
            absorptionFilters[i].setCutoffFrequency(8000.0f); // Initial cutoff
        }
    }

    void processWithDiffusion(float inputL, float inputR, float& outputL, float& outputR,
                             float size, float absorption)
    {
        // First get basic early reflections
        process(inputL, inputR, outputL, outputR, size);

        // Apply diffusion to blur the discrete reflections
        float diffusedL = outputL;
        float diffusedR = outputR;

        for (int i = 0; i < 10; ++i)
        {
            float apfOutL = diffusionAPF[i * 2].popSample(0);
            float apfOutR = diffusionAPF[i * 2 + 1].popSample(0);

            // Feedback coefficient varies per filter
            float feedback = 0.3f + i * 0.02f;

            diffusionAPF[i * 2].pushSample(0, diffusedL + apfOutL * feedback);
            diffusionAPF[i * 2 + 1].pushSample(0, diffusedR + apfOutR * feedback);

            diffusedL = apfOutL - diffusedL * feedback;
            diffusedR = apfOutR - diffusedR * feedback;
        }

        // Apply frequency-dependent absorption
        float cutoff = 8000.0f * (1.0f - absorption * 0.7f);
        for (int i = 0; i < reflections.size() && i < 50; ++i)
        {
            // Higher order reflections get more absorption
            float orderAbsorption = 1.0f - (i / 50.0f) * absorption;
            float filterCutoff = cutoff * orderAbsorption;
            absorptionFilters[i].setCutoffFrequency(filterCutoff);

            if (i % 2 == 0)
                diffusedL = absorptionFilters[i].processSample(0, diffusedL);
            else
                diffusedR = absorptionFilters[i].processSample(1, diffusedR);
        }

        outputL = diffusedL;
        outputR = diffusedR;
    }

    void reset()
    {
        SpatialEarlyReflections::reset();

        for (auto& apf : diffusionAPF)
            apf.reset();

        for (auto& filter : absorptionFilters)
            filter.reset();
    }

private:
    std::array<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>, 20> diffusionAPF;
    std::array<juce::dsp::StateVariableTPTFilter<float>, 50> absorptionFilters;
};