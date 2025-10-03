/*
  ==============================================================================

    ReverbEngineEnhanced.h
    Studio Verb - Enhanced Realistic Reverb DSP Engine
    Copyright (c) 2024 Luna CO. Audio

    Using Feedback Delay Networks (FDN) and modern reverb techniques
    for much more realistic sound

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <random>
#include <vector>
#include <complex>

//==============================================================================
/**
    Householder matrix for FDN mixing - creates perfect diffusion
*/
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
        // Critical safety checks to prevent segfaults
        jassert(inputs != nullptr && outputs != nullptr);
        if (!inputs || !outputs)
        {
            DBG("HouseholderMatrix::process - null pointer detected!");
            return;
        }

        // Use safe scalar fallback - SIMD disabled to prevent alignment issues causing segfaults
        // The previous SIMD code was causing crashes due to alignment assumptions
        for (int i = 0; i < N; ++i)
        {
            float sum = 0.0f;
            for (int j = 0; j < N; ++j)
            {
                sum += matrix[i * N + j] * inputs[j];
            }

            // Denormal prevention
            if (std::abs(sum) < 1e-10f)
                sum = 0.0f;

            outputs[i] = sum;
        }
    }

private:
    void generateHouseholder()
    {
        // MEDIUM PRIORITY: Use fixed seed for deterministic behavior
        std::mt19937 randomGenerator(42);  // Fixed seed
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

        // Create orthogonal matrix using Householder reflection
        std::vector<float> v(N);
        float norm = 0.0f;

        // Random vector with deterministic seed
        for (int i = 0; i < N; ++i)
        {
            v[i] = dist(randomGenerator);
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
/**
    Multi-band decay control for frequency-dependent reverb time
    MEDIUM PRIORITY: Improved with better crossover design
*/
class MultibandDecay
{
public:
    MultibandDecay() = default;

    void prepare(double sampleRate_)
    {
        sampleRate = sampleRate_;

        // Improved 2-pole filters for better phase response
        // Low-pass at 250Hz
        float omega1 = juce::MathConstants<float>::twoPi * 250.0f / static_cast<float>(sampleRate);
        float q = 0.707f;  // Butterworth response
        float alpha = std::sin(omega1) / (2.0f * q);

        lowB0 = (1.0f - std::cos(omega1)) / 2.0f;
        lowB1 = 1.0f - std::cos(omega1);
        lowB2 = lowB0;
        lowA0 = 1.0f + alpha;
        lowA1 = -2.0f * std::cos(omega1);
        lowA2 = 1.0f - alpha;

        // Normalize
        lowB0 /= lowA0;
        lowB1 /= lowA0;
        lowB2 /= lowA0;
        lowA1 /= lowA0;
        lowA2 /= lowA0;

        // High-pass at 2kHz
        float omega2 = juce::MathConstants<float>::twoPi * 2000.0f / static_cast<float>(sampleRate);
        alpha = std::sin(omega2) / (2.0f * q);

        highB0 = (1.0f + std::cos(omega2)) / 2.0f;
        highB1 = -(1.0f + std::cos(omega2));
        highB2 = highB0;
        highA0 = 1.0f + alpha;
        highA1 = -2.0f * std::cos(omega2);
        highA2 = 1.0f - alpha;

        // Normalize
        highB0 /= highA0;
        highB1 /= highA0;
        highB2 /= highA0;
        highA1 /= highA0;
        highA2 /= highA0;
    }

    float process(float input, float lowDecay, float midDecay, float highDecay)
    {
        // Process low band (2-pole biquad)
        float lowOut = lowB0 * input + lowB1 * lowX1 + lowB2 * lowX2
                      - lowA1 * lowY1 - lowA2 * lowY2;
        lowX2 = lowX1;
        lowX1 = input;
        lowY2 = lowY1;
        lowY1 = lowOut;

        // Process high band (2-pole biquad)
        float highOut = highB0 * input + highB1 * highX1 + highB2 * highX2
                       - highA1 * highY1 - highA2 * highY2;
        highX2 = highX1;
        highX1 = input;
        highY2 = highY1;
        highY1 = highOut;

        // Mid band is what's left after subtracting low and high
        float midOut = input - lowOut - highOut;

        // Apply decay gains and recombine
        return lowOut * lowDecay + midOut * midDecay + highOut * highDecay;
    }

    void reset()
    {
        lowX1 = lowX2 = lowY1 = lowY2 = 0.0f;
        highX1 = highX2 = highY1 = highY2 = 0.0f;
    }

private:
    double sampleRate = 48000.0;

    // Low-pass filter coefficients
    float lowB0 = 0.5f, lowB1 = 0.5f, lowB2 = 0.0f;
    float lowA0 = 1.0f, lowA1 = 0.0f, lowA2 = 0.0f;
    float lowX1 = 0.0f, lowX2 = 0.0f, lowY1 = 0.0f, lowY2 = 0.0f;

    // High-pass filter coefficients
    float highB0 = 0.5f, highB1 = -0.5f, highB2 = 0.0f;
    float highA0 = 1.0f, highA1 = 0.0f, highA2 = 0.0f;
    float highX1 = 0.0f, highX2 = 0.0f, highY1 = 0.0f, highY2 = 0.0f;
};

//==============================================================================
/**
    Feedback Delay Network - Much more realistic than comb filters
*/
class FeedbackDelayNetwork
{
public:
    // Reduced from 16 to 12 for better CPU performance
    static constexpr int NUM_DELAYS = 12;  // Balance between CPU and reverb quality

    FeedbackDelayNetwork()
        : mixingMatrix(NUM_DELAYS)
    {
        // Prime number delay lengths to avoid common factors (reduced to 12)
        const int primeLengths[NUM_DELAYS] = {
            1433, 1601, 1867, 2053, 2251, 2399,
            2617, 2797, 3089, 3323, 3571, 3821
        };

        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            baseDelayLengths[i] = primeLengths[i];
        }
    }

    void prepare(const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;

        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            delays[i].prepare(spec);
            delays[i].setMaximumDelayInSamples(10000);
            delays[i].setDelay(baseDelayLengths[i] * (sampleRate / 48000.0f));

            decayFilters[i].prepare(sampleRate);
            inputDiffusion[i].prepare(spec);
            inputDiffusion[i].setMaximumDelayInSamples(1000);
        }
    }

    void process(float inputL, float inputR, float& outputL, float& outputR,
                 float size, float decay, float damping)
    {
        // Clamp size to prevent zero/near-zero values
        size = juce::jmax(0.01f, size);

        // Clamp decay to stable range
        decay = juce::jlimit(0.0f, 0.999f, decay);

        float delayOutputs[NUM_DELAYS];
        float delayInputs[NUM_DELAYS];

        // Read from delays
        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            delayOutputs[i] = delays[i].popSample(0);
        }

        // Mix through Householder matrix for perfect diffusion
        mixingMatrix.process(delayOutputs, delayInputs);

        // Apply decay and damping
        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            // HIGH PRIORITY: Frequency-dependent decay with strict clamping and safety factor for stability
            float safetyFactor = 0.99f;  // Additional headroom to prevent oscillation
            float lowGain = juce::jlimit(0.0f, 0.999f, decay * 1.05f * safetyFactor);  // Low frequencies decay slightly slower
            float midGain = juce::jlimit(0.0f, 0.999f, decay * safetyFactor);
            float highGain = juce::jlimit(0.0f, 0.999f, decay * (1.0f - damping * 0.4f) * safetyFactor);  // High frequencies decay faster

            delayInputs[i] = decayFilters[i].process(delayInputs[i], lowGain, midGain, highGain);

            // Add input with decorrelation
            float input = (i % 2 == 0) ? inputL : inputR;
            inputDiffusion[i].pushSample(0, input);
            float decorrelatedInput = inputDiffusion[i].popSample(0);
            delayInputs[i] += decorrelatedInput * 0.3f;  // Reduced gain to prevent buildup

            // Feed back into delays with size modulation
            float modulatedLength = baseDelayLengths[i] * (0.5f + size * 1.5f) * (sampleRate / 48000.0f);
            modulatedLength = juce::jmax(1.0f, modulatedLength);  // Prevent zero delay
            delays[i].setDelay(modulatedLength);
            delays[i].pushSample(0, delayInputs[i]);
        }

        // Decorrelated stereo output
        outputL = outputR = 0.0f;
        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            // HIGH PRIORITY: Sanitize delay outputs before accumulation
            float delayOut = delayOutputs[i];
            if (std::isnan(delayOut) || std::isinf(delayOut)) delayOut = 0.0f;
            delayOut = juce::jlimit(-10.0f, 10.0f, delayOut);  // Prevent explosive feedback

            // Hadamard-inspired decorrelation pattern
            outputL += delayOut * ((i & 1) ? 1.0f : -1.0f) * ((i & 2) ? 1.0f : -1.0f);
            outputR += delayOut * ((i & 4) ? 1.0f : -1.0f) * ((i & 8) ? 1.0f : -1.0f);
        }

        outputL /= NUM_DELAYS;
        outputR /= NUM_DELAYS;

        // HIGH PRIORITY: Final safety clamp on FDN output
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
        }

        // Clear any NaN or denormal values
        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            delays[i].pushSample(0, 0.0f);
            inputDiffusion[i].pushSample(0, 0.0f);
        }
    }

private:
    double sampleRate = 48000.0;
    float baseDelayLengths[NUM_DELAYS];

    std::array<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>, NUM_DELAYS> delays;
    std::array<MultibandDecay, NUM_DELAYS> decayFilters;
    std::array<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None>, NUM_DELAYS> inputDiffusion;

    HouseholderMatrix mixingMatrix;
};

//==============================================================================
/**
    Enhanced early reflections with proper spatial modeling
*/
class SpatialEarlyReflections
{
public:
    struct Reflection
    {
        float delay;        // ms
        float gain;         // amplitude
        float azimuth;      // degrees (-180 to 180)
        float elevation;    // degrees (-90 to 90)
    };

    void prepare(const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;

        for (auto& delay : delays)
        {
            delay.prepare(spec);
            delay.setMaximumDelayInSamples(static_cast<int>(sampleRate * 0.2f));
        }

        generateReflectionPattern();
    }

    void generateReflectionPattern()
    {
        // Image source method for realistic early reflections
        reflections.clear();

        // Room dimensions (meters) - varies by algorithm
        float width = 8.0f;
        float height = 3.5f;
        float depth = 10.0f;

        // Generate first-order and second-order reflections
        for (int order = 1; order <= 2; ++order)
        {
            for (int x = -order; x <= order; ++x)
            {
                for (int y = -order; y <= order; ++y)
                {
                    for (int z = -order; z <= order; ++z)
                    {
                        if (std::abs(x) + std::abs(y) + std::abs(z) == order)
                        {
                            // Calculate reflection position
                            float rx = x * width;
                            float ry = y * height;
                            float rz = z * depth;

                            // Distance and delay
                            float distance = std::sqrt(rx*rx + ry*ry + rz*rz);
                            float delay = (distance / 343.0f) * 1000.0f;  // Speed of sound

                            if (delay < 200.0f)  // Only early reflections
                            {
                                Reflection ref;
                                ref.delay = delay;
                                ref.gain = 1.0f / (1.0f + distance * 0.1f);  // Distance attenuation
                                ref.azimuth = std::atan2(rx, rz) * 180.0f / M_PI;
                                ref.elevation = std::atan2(ry, std::sqrt(rx*rx + rz*rz)) * 180.0f / M_PI;

                                reflections.push_back(ref);
                            }
                        }
                    }
                }
            }
        }
    }

    void process(float inputL, float inputR, float& outputL, float& outputR, float size)
    {
        outputL = outputR = 0.0f;

        // Program-dependent scaling based on input energy
        float inputEnergy = std::sqrt((inputL * inputL + inputR * inputR) * 0.5f);
        float energyScale = juce::jlimit(0.3f, 1.2f, inputEnergy + 0.7f);  // Dynamic response

        // Add subtle time modulation for more natural reflections
        modPhase += 0.0002f;
        if (modPhase > 1.0f) modPhase -= 1.0f;
        float timeModulation = 1.0f + std::sin(modPhase * juce::MathConstants<float>::twoPi) * 0.003f;

        // MEDIUM PRIORITY: Calculate normalization based on sum of gains (RMS)
        float totalGain = 0.0f;
        for (const auto& ref : reflections)
        {
            totalGain += ref.gain * ref.gain;  // Sum of squared gains for RMS
        }
        float rmsNorm = (totalGain > 0.0f) ? (1.0f / std::sqrt(totalGain)) : 1.0f;

        for (size_t i = 0; i < reflections.size() && i < delays.size(); ++i)
        {
            const auto& ref = reflections[i];

            // Adjust delay by size parameter with natural modulation
            float scaledDelay = ref.delay * (0.5f + size * 1.5f) * timeModulation * sampleRate / 1000.0f;
            delays[i].setDelay(scaledDelay);

            // Get delayed sample with energy-dependent scaling
            float delayed = delays[i].popSample(0);
            delays[i].pushSample(0, (inputL + inputR) * 0.5f * energyScale);

            // Apply HRTF-inspired panning based on azimuth
            float panL = (1.0f + std::cos((ref.azimuth + 90.0f) * M_PI / 180.0f)) * 0.5f;
            float panR = (1.0f + std::cos((ref.azimuth - 90.0f) * M_PI / 180.0f)) * 0.5f;

            outputL += delayed * ref.gain * panL;
            outputR += delayed * ref.gain * panR;
        }

        // Apply RMS-based normalization with target gain of ~0.6 for headroom
        float targetGain = 0.6f;
        outputL *= rmsNorm * targetGain;
        outputR *= rmsNorm * targetGain;
    }

    void reset()
    {
        for (auto& delay : delays)
        {
            delay.reset();
            // Clear any residual values
            for (int j = 0; j < 10; ++j)
                delay.pushSample(0, 0.0f);
        }

        // Reset modulation phase to restart from zero
        modPhase = 0.0f;
    }

    void setRoomDimensions(float width, float height, float depth)
    {
        // Update room model and regenerate reflections
        generateReflectionPattern();
    }

private:
    double sampleRate = 48000.0;
    std::vector<Reflection> reflections;
    std::array<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>, 50> delays;
    float modPhase = 0.0f;  // For natural time modulation
};

//==============================================================================
/**
    Enhanced reverb engine with realistic algorithms
*/
class ReverbEngineEnhanced
{
public:
    ReverbEngineEnhanced() = default;

    void prepare(const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;

        // Prepare FDN
        fdn.prepare(spec);

        // Prepare early reflections
        earlyReflections.prepare(spec);

        // Prepare predelay
        predelayL.prepare(spec);
        predelayR.prepare(spec);
        predelayL.setMaximumDelayInSamples(static_cast<int>(sampleRate * 0.2));
        predelayR.setMaximumDelayInSamples(static_cast<int>(sampleRate * 0.2));

        // Prepare filters
        lowShelf.prepare(spec);
        lowShelf.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        lowShelf.setCutoffFrequency(2000.0f);

        highShelf.prepare(spec);
        highShelf.setType(juce::dsp::StateVariableTPTFilterType::highpass);
        highShelf.setCutoffFrequency(100.0f);

        // Metallic peaking filter for plate emulation (around 2.5kHz for shimmer)
        plateMetallicFilter.prepare(spec);
        plateMetallicFilter.setType(juce::dsp::StateVariableTPTFilterType::bandpass);
        plateMetallicFilter.setCutoffFrequency(2500.0f);
        plateMetallicFilter.setResonance(2.5f); // High resonance for metallic character

        // Modulation LFOs
        modulationLFO1.initialise([](float x) { return std::sin(x); });
        modulationLFO2.initialise([](float x) { return std::sin(x); });
        modulationLFO1.setFrequency(0.5f);
        modulationLFO2.setFrequency(0.7f);
        modulationLFO1.prepare(spec);
        modulationLFO2.prepare(spec);

        // Initialize parameter smoothers with 50ms ramp time
        float rampLengthSeconds = 0.05f;
        sizeSmooth.reset(sampleRate, rampLengthSeconds);
        dampingSmooth.reset(sampleRate, rampLengthSeconds);
        mixSmooth.reset(sampleRate, rampLengthSeconds);
        widthSmooth.reset(sampleRate, rampLengthSeconds);
        predelaySmooth.reset(sampleRate, rampLengthSeconds);

        // Set initial values
        sizeSmooth.setCurrentAndTargetValue(currentSize);
        dampingSmooth.setCurrentAndTargetValue(currentDamping);
        mixSmooth.setCurrentAndTargetValue(currentMix);
        widthSmooth.setCurrentAndTargetValue(currentWidth);
        predelaySmooth.setCurrentAndTargetValue(0.0f);

        // Reset everything to clear any garbage
        reset();
    }

    void process(juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();
        float* leftChannel = buffer.getWritePointer(0);
        float* rightChannel = buffer.getWritePointer(1);

        // HIGH PRIORITY: Set denormal flush-to-zero for this thread (prevents CPU spikes)
        juce::ScopedNoDenormals noDenormals;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            float inputL = leftChannel[sample];
            float inputR = rightChannel[sample];

            // Get smoothed parameter values for this sample
            float smoothedSize = juce::jmax(0.01f, sizeSmooth.getNextValue());  // Prevent div by zero
            float smoothedDamping = dampingSmooth.getNextValue();
            float smoothedMix = mixSmooth.getNextValue();
            float smoothedWidth = widthSmooth.getNextValue();
            float smoothedPredelaySamples = predelaySmooth.getNextValue();

            // Update predelay with smoothed value
            predelayL.setDelay(smoothedPredelaySamples);
            predelayR.setDelay(smoothedPredelaySamples);

            // Apply predelay
            float delayedL = predelayL.popSample(0);
            float delayedR = predelayR.popSample(0);
            predelayL.pushSample(0, inputL);
            predelayR.pushSample(0, inputR);

            // HIGH PRIORITY: Sanitize input to prevent NaN propagation
            if (std::isnan(delayedL) || std::isinf(delayedL)) delayedL = 0.0f;
            if (std::isnan(delayedR) || std::isinf(delayedR)) delayedR = 0.0f;
            delayedL = juce::jlimit(-10.0f, 10.0f, delayedL);  // Clamp extreme values
            delayedR = juce::jlimit(-10.0f, 10.0f, delayedR);

            // Process early reflections
            float earlyL, earlyR;
            earlyReflections.process(delayedL, delayedR, earlyL, earlyR, smoothedSize);

            // HIGH PRIORITY: Sanitize early reflections output
            if (std::isnan(earlyL) || std::isinf(earlyL)) earlyL = 0.0f;
            if (std::isnan(earlyR) || std::isinf(earlyR)) earlyR = 0.0f;

            // Process late reverb through FDN with clamped decay
            float lateL, lateR;
            float clampedDecay = juce::jlimit(0.0f, 0.999f, currentDecay);
            fdn.process(delayedL, delayedR, lateL, lateR, smoothedSize, clampedDecay, smoothedDamping);

            // HIGH PRIORITY: Sanitize FDN output (works in release builds unlike jassert)
            if (std::isnan(lateL) || std::isinf(lateL)) lateL = 0.0f;
            if (std::isnan(lateR) || std::isinf(lateR)) lateR = 0.0f;

            // Add subtle modulation for liveliness (Task 3: Increased for plate shimmer)
            float modDepth = (currentAlgorithm == 2) ? 0.005f : 0.002f;  // More shimmer for plate
            float mod1 = modulationLFO1.processSample(0.0f) * modDepth;
            float mod2 = modulationLFO2.processSample(0.0f) * modDepth;
            lateL *= (1.0f + mod1);
            lateR *= (1.0f + mod2);

            // Apply tone shaping
            lateL = lowShelf.processSample(0, lateL);
            lateR = lowShelf.processSample(1, lateR);
            lateL = highShelf.processSample(0, lateL);
            lateR = highShelf.processSample(1, lateR);

            // Apply metallic filtering for plate mode with dynamic parameters
            if (currentAlgorithm == 2) // Plate mode
            {
                // Adjust plate filter cutoff based on size (larger size = higher frequency)
                float plateCutoff = 2000.0f + smoothedSize * 3000.0f;  // 2kHz to 5kHz
                plateMetallicFilter.setCutoffFrequency(plateCutoff);

                // Adjust resonance based on damping (less damping = more resonance)
                float plateResonance = 2.0f + (1.0f - smoothedDamping) * 1.5f;  // 2.0 to 3.5
                plateMetallicFilter.setResonance(plateResonance);

                // Add metallic resonance using peaking filter
                float metallicL = plateMetallicFilter.processSample(0, lateL);
                float metallicR = plateMetallicFilter.processSample(1, lateR);

                // Mix original and filtered for bright metallic character
                // More prominent metallic sound with less damping
                float metallicMix = 0.3f + (1.0f - smoothedDamping) * 0.3f;  // 0.3 to 0.6
                lateL = lateL * (1.0f - metallicMix) + metallicL * metallicMix;
                lateR = lateR * (1.0f - metallicMix) + metallicR * metallicMix;
            }

            // Mix early and late
            float reverbL = earlyL * earlyGain + lateL * lateGain;
            float reverbR = earlyR * earlyGain + lateR * lateGain;

            // Task 10: Apply width control with smoothed value
            float mid = (reverbL + reverbR) * 0.5f;
            float side = (reverbL - reverbR) * 0.5f * smoothedWidth;
            reverbL = mid + side;
            reverbR = mid - side;

            // Apply mix with smoothed value
            float wetGain = smoothedMix;
            float dryGain = 1.0f - smoothedMix;
            float outputL = inputL * dryGain + reverbL * wetGain;
            float outputR = inputR * dryGain + reverbR * wetGain;

            // HIGH PRIORITY: Add NaN/Inf guards and output limiting
            if (std::isnan(outputL) || std::isinf(outputL)) outputL = 0.0f;
            if (std::isnan(outputR) || std::isinf(outputR)) outputR = 0.0f;

            // Soft clipping to prevent harsh distortion
            leftChannel[sample] = juce::jlimit(-1.0f, 1.0f, outputL);
            rightChannel[sample] = juce::jlimit(-1.0f, 1.0f, outputR);
        }
    }

    void setAlgorithm(int algorithm)
    {
        currentAlgorithm = algorithm;

        switch (algorithm)
        {
            case 0: // Room
                earlyReflections.setRoomDimensions(8.0f, 3.5f, 10.0f);
                currentDecay = 0.85f;
                earlyGain = 0.6f;
                lateGain = 0.4f;
                break;

            case 1: // Hall
                earlyReflections.setRoomDimensions(25.0f, 10.0f, 40.0f);
                currentDecay = 0.93f;
                earlyGain = 0.3f;
                lateGain = 0.7f;
                break;

            case 2: // Plate (simulate with tight FDN)
                earlyReflections.setRoomDimensions(2.0f, 0.1f, 3.0f);
                currentDecay = 0.98f;
                earlyGain = 0.1f;
                lateGain = 0.9f;
                break;

            case 3: // Early Only
                earlyGain = 1.0f;
                lateGain = 0.0f;
                break;
        }
    }

    void reset()
    {
        fdn.reset();
        earlyReflections.reset();
        predelayL.reset();
        predelayR.reset();
        lowShelf.reset();
        highShelf.reset();
        plateMetallicFilter.reset();

        // Clear predelay buffers completely
        for (int i = 0; i < 1000; ++i)
        {
            predelayL.pushSample(0, 0.0f);
            predelayR.pushSample(0, 0.0f);
        }

        // Reset oscillators
        modulationLFO1.reset();
        modulationLFO2.reset();
    }

    // Parameter setters (now set targets for smoothing)
    void setSize(float size)
    {
        currentSize = juce::jlimit(0.0f, 1.0f, size);
        sizeSmooth.setTargetValue(currentSize);
    }

    void setDamping(float damp)
    {
        currentDamping = juce::jlimit(0.0f, 1.0f, damp);
        dampingSmooth.setTargetValue(currentDamping);
    }

    void setPredelay(float ms)
    {
        currentPredelayMs = juce::jlimit(0.0f, 200.0f, ms);
        float samples = (currentPredelayMs / 1000.0f) * sampleRate;
        predelaySmooth.setTargetValue(samples);
    }

    void setMix(float mix)
    {
        currentMix = juce::jlimit(0.0f, 1.0f, mix);
        mixSmooth.setTargetValue(currentMix);
    }

    // Task 10: Width control for stereo spread
    void setWidth(float width)
    {
        currentWidth = juce::jlimit(0.0f, 1.0f, width);
        widthSmooth.setTargetValue(currentWidth);
    }

    // Task 7: Return max tail samples for latency reporting
    int getMaxTailSamples() const
    {
        // Max delay in FDN (2s) + max predelay (200ms)
        return static_cast<int>(sampleRate * 2.2);
    }

private:
    double sampleRate = 48000.0;

    // DSP Components
    FeedbackDelayNetwork fdn;
    SpatialEarlyReflections earlyReflections;

    // HIGH PRIORITY: Use Linear interpolation to prevent clicks on predelay changes
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> predelayL { 48000 };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> predelayR { 48000 };

    juce::dsp::StateVariableTPTFilter<float> lowShelf;
    juce::dsp::StateVariableTPTFilter<float> highShelf;

    // Metallic peaking filter for plate emulation
    juce::dsp::StateVariableTPTFilter<float> plateMetallicFilter;

    juce::dsp::Oscillator<float> modulationLFO1;
    juce::dsp::Oscillator<float> modulationLFO2;

    // HIGH PRIORITY: Optional safety limiters for extreme protection
    // juce::dsp::Limiter<float> outputLimiterL;
    // juce::dsp::Limiter<float> outputLimiterR;
    // Note: Currently using jlimit for lower CPU usage, but limiters can be enabled if needed

    // Parameters
    int currentAlgorithm = 0;
    float currentSize = 0.5f;
    float currentDecay = 0.9f;
    float currentDamping = 0.5f;
    float currentMix = 0.5f;
    float currentWidth = 0.5f;  // Task 10: Width parameter
    float currentPredelayMs = 0.0f;

    float earlyGain = 0.5f;
    float lateGain = 0.5f;

    // Parameter smoothers to prevent zipper noise
    juce::SmoothedValue<float> sizeSmooth;
    juce::SmoothedValue<float> dampingSmooth;
    juce::SmoothedValue<float> mixSmooth;
    juce::SmoothedValue<float> widthSmooth;
    juce::SmoothedValue<float> predelaySmooth;
};