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
#include <memory>

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
    Multi-band decay control with Linkwitz-Riley crossovers for frequency-dependent reverb time
    Uses 4th-order Linkwitz-Riley filters for flat magnitude response and linear phase
*/
class MultibandDecay
{
public:
    MultibandDecay() = default;

    void prepare(double sampleRate_)
    {
        sampleRate = sampleRate_;

        // Linkwitz-Riley 4th order = two cascaded Butterworth 2nd order filters
        // Low-pass at 250Hz
        float omega1 = juce::MathConstants<float>::twoPi * 250.0f / static_cast<float>(sampleRate);
        float q = 0.707f;  // Butterworth Q for each stage
        float alpha = std::sin(omega1) / (2.0f * q);

        // First stage lowpass
        lowB0_stage1 = (1.0f - std::cos(omega1)) / 2.0f;
        lowB1_stage1 = 1.0f - std::cos(omega1);
        lowB2_stage1 = lowB0_stage1;
        float lowA0 = 1.0f + alpha;
        lowA1_stage1 = -2.0f * std::cos(omega1);
        lowA2_stage1 = 1.0f - alpha;

        // Normalize first stage
        lowB0_stage1 /= lowA0;
        lowB1_stage1 /= lowA0;
        lowB2_stage1 /= lowA0;
        lowA1_stage1 /= lowA0;
        lowA2_stage1 /= lowA0;

        // Second stage lowpass (same coefficients)
        lowB0_stage2 = lowB0_stage1;
        lowB1_stage2 = lowB1_stage1;
        lowB2_stage2 = lowB2_stage1;
        lowA1_stage2 = lowA1_stage1;
        lowA2_stage2 = lowA2_stage1;

        // High-pass at 2kHz (Linkwitz-Riley 4th order)
        float omega2 = juce::MathConstants<float>::twoPi * 2000.0f / static_cast<float>(sampleRate);
        alpha = std::sin(omega2) / (2.0f * q);

        // First stage highpass
        highB0_stage1 = (1.0f + std::cos(omega2)) / 2.0f;
        highB1_stage1 = -(1.0f + std::cos(omega2));
        highB2_stage1 = highB0_stage1;
        float highA0 = 1.0f + alpha;
        highA1_stage1 = -2.0f * std::cos(omega2);
        highA2_stage1 = 1.0f - alpha;

        // Normalize first stage
        highB0_stage1 /= highA0;
        highB1_stage1 /= highA0;
        highB2_stage1 /= highA0;
        highA1_stage1 /= highA0;
        highA2_stage1 /= highA0;

        // Second stage highpass (same coefficients)
        highB0_stage2 = highB0_stage1;
        highB1_stage2 = highB1_stage1;
        highB2_stage2 = highB2_stage1;
        highA1_stage2 = highA1_stage1;
        highA2_stage2 = highA2_stage1;
    }

    float process(float input, float lowDecay, float midDecay, float highDecay)
    {
        // Process low band with 4th-order Linkwitz-Riley (two cascaded 2nd-order)
        // Stage 1
        float lowOut1 = lowB0_stage1 * input + lowB1_stage1 * lowX1_s1 + lowB2_stage1 * lowX2_s1
                       - lowA1_stage1 * lowY1_s1 - lowA2_stage1 * lowY2_s1;
        lowX2_s1 = lowX1_s1;
        lowX1_s1 = input;
        lowY2_s1 = lowY1_s1;
        lowY1_s1 = lowOut1;

        // Stage 2
        float lowOut = lowB0_stage2 * lowOut1 + lowB1_stage2 * lowX1_s2 + lowB2_stage2 * lowX2_s2
                      - lowA1_stage2 * lowY1_s2 - lowA2_stage2 * lowY2_s2;
        lowX2_s2 = lowX1_s2;
        lowX1_s2 = lowOut1;
        lowY2_s2 = lowY1_s2;
        lowY1_s2 = lowOut;

        // Process high band with 4th-order Linkwitz-Riley
        // Stage 1
        float highOut1 = highB0_stage1 * input + highB1_stage1 * highX1_s1 + highB2_stage1 * highX2_s1
                        - highA1_stage1 * highY1_s1 - highA2_stage1 * highY2_s1;
        highX2_s1 = highX1_s1;
        highX1_s1 = input;
        highY2_s1 = highY1_s1;
        highY1_s1 = highOut1;

        // Stage 2
        float highOut = highB0_stage2 * highOut1 + highB1_stage2 * highX1_s2 + highB2_stage2 * highX2_s2
                       - highA1_stage2 * highY1_s2 - highA2_stage2 * highY2_s2;
        highX2_s2 = highX1_s2;
        highX1_s2 = highOut1;
        highY2_s2 = highY1_s2;
        highY1_s2 = highOut;

        // Mid band is what remains (perfect reconstruction with Linkwitz-Riley)
        float midOut = input - lowOut - highOut;

        // Apply decay gains and recombine
        return lowOut * lowDecay + midOut * midDecay + highOut * highDecay;
    }

    void reset()
    {
        // Reset low band state variables
        lowX1_s1 = lowX2_s1 = lowY1_s1 = lowY2_s1 = 0.0f;
        lowX1_s2 = lowX2_s2 = lowY1_s2 = lowY2_s2 = 0.0f;

        // Reset high band state variables
        highX1_s1 = highX2_s1 = highY1_s1 = highY2_s1 = 0.0f;
        highX1_s2 = highX2_s2 = highY1_s2 = highY2_s2 = 0.0f;
    }

private:
    double sampleRate = 48000.0;

    // Linkwitz-Riley low-pass filter coefficients (two stages)
    float lowB0_stage1 = 0.5f, lowB1_stage1 = 0.5f, lowB2_stage1 = 0.0f;
    float lowA1_stage1 = 0.0f, lowA2_stage1 = 0.0f;
    float lowB0_stage2 = 0.5f, lowB1_stage2 = 0.5f, lowB2_stage2 = 0.0f;
    float lowA1_stage2 = 0.0f, lowA2_stage2 = 0.0f;

    // Low-pass state variables (two stages)
    float lowX1_s1 = 0.0f, lowX2_s1 = 0.0f, lowY1_s1 = 0.0f, lowY2_s1 = 0.0f;
    float lowX1_s2 = 0.0f, lowX2_s2 = 0.0f, lowY1_s2 = 0.0f, lowY2_s2 = 0.0f;

    // Linkwitz-Riley high-pass filter coefficients (two stages)
    float highB0_stage1 = 0.5f, highB1_stage1 = -0.5f, highB2_stage1 = 0.0f;
    float highA1_stage1 = 0.0f, highA2_stage1 = 0.0f;
    float highB0_stage2 = 0.5f, highB1_stage2 = -0.5f, highB2_stage2 = 0.0f;
    float highA1_stage2 = 0.0f, highA2_stage2 = 0.0f;

    // High-pass state variables (two stages)
    float highX1_s1 = 0.0f, highX2_s1 = 0.0f, highY1_s1 = 0.0f, highY2_s1 = 0.0f;
    float highX1_s2 = 0.0f, highX2_s2 = 0.0f, highY1_s2 = 0.0f, highY2_s2 = 0.0f;
};

//==============================================================================
/**
    Feedback Delay Network - Much more realistic than comb filters
*/
class FeedbackDelayNetwork
{
public:
    // Increased to 32 for Valhalla-level density and lushness
    static constexpr int NUM_DELAYS = 32;

    FeedbackDelayNetwork()
        : mixingMatrix(NUM_DELAYS)
    {
        // Extended prime number delay lengths for 32 channels (same as EnhancedFeedbackDelayNetwork)
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

        // Calculate max delay needed accounting for size modulation (up to 2x) and sample rate scaling
        // Base delay * 2.0 (max modulation) * (sampleRate/48000) with safety margin
        int maxNeededDelay = static_cast<int>(baseDelayLengths[NUM_DELAYS - 1] * 2.0f * (sampleRate / 48000.0) * 1.2);  // 20% safety margin

        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            delays[i].prepare(spec);
            delays[i].setMaximumDelayInSamples(maxNeededDelay);

            // Set initial delay
            float initialDelay = baseDelayLengths[i] * (sampleRate / 48000.0f);
            initialDelay = juce::jlimit(1.0f, static_cast<float>(maxNeededDelay - 1), initialDelay);
            delays[i].setDelay(initialDelay);

            decayFilters[i].prepare(sampleRate);
            inputDiffusion[i].prepare(spec);
            inputDiffusion[i].setMaximumDelayInSamples(1000);

            // Initialize per-channel modulation LFOs for lush, detuned character
            modulationLFOs[i].initialise([](float x) { return std::sin(x); });

            // Multiple LFO rates for rich modulation - mix of sine and random-like modulation
            if (i < NUM_DELAYS / 2)
            {
                // First half: slow sine waves (0.1 Hz to 1.5 Hz)
                float rate = 0.1f + (i * 0.045f);
                modulationLFOs[i].setFrequency(rate);
            }
            else
            {
                // Second half: complex waveforms for random-like modulation
                modulationLFOs[i].initialise([](float x) {
                    return (std::sin(x) + std::sin(x * 3.7f) * 0.3f + std::sin(x * 7.3f) * 0.1f) / 1.4f;
                });
                float rate = 0.05f + ((i - NUM_DELAYS/2) * 0.04f);
                modulationLFOs[i].setFrequency(rate);
            }

            modulationLFOs[i].prepare(spec);
        }
    }

    void process(float inputL, float inputR, float& outputL, float& outputR,
                 float size, float decay, float damping, float modDepth = 1.0f)
    {
        // Clamp size to prevent zero/near-zero values
        size = juce::jmax(0.01f, size);

        // Clamp decay to stable range
        decay = juce::jlimit(0.0f, 0.999f, decay);

        float delayOutputs[NUM_DELAYS];
        float delayInputs[NUM_DELAYS];

        // Read from delays with per-channel modulation for lush character
        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            // Apply per-channel modulation
            float modulation = modulationLFOs[i].processSample(0.0f);
            float modAmount = modulation * modDepth * 10.0f * (0.5f + size * 0.5f);

            // Modulate delay time
            float modulatedLength = baseDelayLengths[i] * (0.5f + size * 1.5f) * (sampleRate / 48000.0f);
            modulatedLength += modAmount;

            int maxDelayInSamples = delays[i].getMaximumDelayInSamples();
            modulatedLength = juce::jlimit(1.0f, static_cast<float>(maxDelayInSamples - 1), modulatedLength);
            delays[i].setDelay(modulatedLength);

            delayOutputs[i] = delays[i].popSample(0);
        }

        // Mix through Householder matrix for perfect diffusion
        mixingMatrix.process(delayOutputs, delayInputs);

        // Apply decay and damping
        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            // Use per-band feedback coefficients if set, otherwise fall back to decay parameter
            float lowGain, midGain, highGain;

            if (usePerBandRT60)
            {
                // Use pre-calculated per-band feedback coefficients for accurate RT60 control
                lowGain = lowBandFeedback;
                midGain = midBandFeedback;
                highGain = highBandFeedback;
            }
            else
            {
                // Legacy mode: derive from single decay parameter
                float safetyFactor = 0.99f;  // Additional headroom to prevent oscillation
                lowGain = juce::jlimit(0.0f, 0.999f, decay * 1.05f * safetyFactor);  // Low frequencies decay slightly slower
                midGain = juce::jlimit(0.0f, 0.999f, decay * safetyFactor);
                highGain = juce::jlimit(0.0f, 0.999f, decay * (1.0f - damping * 0.4f) * safetyFactor);  // High frequencies decay faster
            }

            delayInputs[i] = decayFilters[i].process(delayInputs[i], lowGain, midGain, highGain);

            // Add input with decorrelation
            float input = (i % 2 == 0) ? inputL : inputR;
            inputDiffusion[i].pushSample(0, input);
            float decorrelatedInput = inputDiffusion[i].popSample(0);
            delayInputs[i] += decorrelatedInput * 0.3f;  // Reduced gain to prevent buildup

            // Feed back into delays - already modulated above
            delays[i].pushSample(0, delayInputs[i]);
        }

        // Enhanced decorrelated stereo output with better spatial imaging
        outputL = outputR = 0.0f;
        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            // HIGH PRIORITY: Sanitize delay outputs before accumulation
            float delayOut = delayOutputs[i];
            if (std::isnan(delayOut) || std::isinf(delayOut)) delayOut = 0.0f;
            delayOut = juce::jlimit(-10.0f, 10.0f, delayOut);  // Prevent explosive feedback

            // More sophisticated decorrelation using circular panning
            float angle = (i * juce::MathConstants<float>::pi * 2.0f) / NUM_DELAYS;
            outputL += delayOut * std::cos(angle);
            outputR += delayOut * std::sin(angle);
        }

        // Energy-normalized output
        outputL /= std::sqrt(static_cast<float>(NUM_DELAYS));
        outputR /= std::sqrt(static_cast<float>(NUM_DELAYS));

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
            modulationLFOs[i].reset();
        }

        // Clear any NaN or denormal values
        for (int i = 0; i < NUM_DELAYS; ++i)
        {
            delays[i].pushSample(0, 0.0f);
            inputDiffusion[i].pushSample(0, 0.0f);
        }
    }

    // Set per-band RT60 times for accurate frequency-dependent decay control
    void setPerBandRT60(float lowRT60, float midRT60, float highRT60)
    {
        // Convert RT60 times to feedback coefficients using proper formula
        // RT60 is the time for signal to decay by 60dB (-60dB = 0.001 in linear)
        // For a feedback system: output(n) = input * feedback^n
        // After RT60 seconds: 0.001 = feedback^(RT60 * sampleRate)
        // feedback = 0.001^(1 / (RT60 * sampleRate))
        // Using natural log: feedback = exp(ln(0.001) / (RT60 * sampleRate))
        // ln(0.001) ≈ -6.9078

        const float ln_001 = -6.9078f;  // ln(0.001)
        float safetyFactor = 0.99f;  // Additional headroom to prevent oscillation

        // Low band feedback
        if (lowRT60 > 0.01f)
        {
            lowBandFeedback = std::exp(ln_001 / (lowRT60 * static_cast<float>(sampleRate)));
            lowBandFeedback = juce::jlimit(0.0f, 0.999f, lowBandFeedback * safetyFactor);
        }
        else
        {
            lowBandFeedback = 0.0f;
        }

        // Mid band feedback
        if (midRT60 > 0.01f)
        {
            midBandFeedback = std::exp(ln_001 / (midRT60 * static_cast<float>(sampleRate)));
            midBandFeedback = juce::jlimit(0.0f, 0.999f, midBandFeedback * safetyFactor);
        }
        else
        {
            midBandFeedback = 0.0f;
        }

        // High band feedback
        if (highRT60 > 0.01f)
        {
            highBandFeedback = std::exp(ln_001 / (highRT60 * static_cast<float>(sampleRate)));
            highBandFeedback = juce::jlimit(0.0f, 0.999f, highBandFeedback * safetyFactor);
        }
        else
        {
            highBandFeedback = 0.0f;
        }

        usePerBandRT60 = true;
    }

    // Disable per-band RT60 and use legacy decay parameter
    void disablePerBandRT60()
    {
        usePerBandRT60 = false;
    }

private:
    double sampleRate = 48000.0;
    float baseDelayLengths[NUM_DELAYS];

    std::array<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>, NUM_DELAYS> delays;
    std::array<MultibandDecay, NUM_DELAYS> decayFilters;
    std::array<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None>, NUM_DELAYS> inputDiffusion;
    std::array<juce::dsp::Oscillator<float>, NUM_DELAYS> modulationLFOs;

    HouseholderMatrix mixingMatrix;

    // Per-band RT60 feedback coefficients for accurate frequency-dependent decay
    bool usePerBandRT60 = false;
    float lowBandFeedback = 0.9f;
    float midBandFeedback = 0.9f;
    float highBandFeedback = 0.85f;
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

        // Use member variables for room dimensions
        float width = roomWidth;
        float height = roomHeight;
        float depth = roomDepth;

        // Adjust reflection order based on density (more density = more reflections)
        int maxOrder = static_cast<int>(std::ceil(1.0f + reflectionDensity));
        maxOrder = juce::jlimit(1, 3, maxOrder);  // Limit 1-3 for performance

        // Generate reflections up to calculated order
        for (int order = 1; order <= maxOrder; ++order)
        {
            for (int x = -order; x <= order; ++x)
            {
                for (int y = -order; y <= order; ++y)
                {
                    for (int z = -order; z <= order; ++z)
                    {
                        if (std::abs(x) + std::abs(y) + std::abs(z) == order)
                        {
                            // Skip some reflections for lower density
                            if (reflectionDensity < 1.0f && (std::abs(x) + std::abs(y) + std::abs(z)) % 2 == 0)
                                continue;

                            // Calculate reflection position
                            float rx = x * width;
                            float ry = y * height;
                            float rz = z * depth;

                            // Distance and delay
                            float distance = std::sqrt(rx*rx + ry*ry + rz*rz);
                            float delay = (distance / 343.0f) * 1000.0f;  // Speed of sound = 343 m/s

                            if (delay < 200.0f)  // Only early reflections
                            {
                                Reflection ref;
                                ref.delay = delay;

                                // Apply distance attenuation and wall absorption
                                float distanceAtten = 1.0f / (1.0f + distance * 0.1f);
                                float absorptionFactor = std::pow(1.0f - wallAbsorption, static_cast<float>(order));
                                ref.gain = distanceAtten * absorptionFactor;

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

            // Adjust delay by size parameter with natural modulation - CRITICAL: Clamp to prevent crashes
            float scaledDelay = ref.delay * (0.5f + size * 1.5f) * timeModulation * sampleRate / 1000.0f;
            int maxDelayInSamples = delays[i].getMaximumDelayInSamples();
            scaledDelay = juce::jlimit(0.0f, static_cast<float>(maxDelayInSamples - 1), scaledDelay);
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
        roomWidth = width;
        roomHeight = height;
        roomDepth = depth;
        generateReflectionPattern();
    }

    // Enhanced room shape presets with realistic acoustic characteristics
    void setRoomShape(int shape)
    {
        switch (shape)
        {
            case 0: // Studio Room - balanced, tight reflections
                setRoomDimensions(8.0f, 3.5f, 10.0f);
                reflectionDensity = 1.0f;
                wallAbsorption = 0.3f;  // Moderate absorption
                break;
            case 1: // Small Room - intimate, fast buildup
                setRoomDimensions(5.0f, 2.5f, 6.0f);
                reflectionDensity = 1.5f;  // Dense reflections
                wallAbsorption = 0.4f;  // More absorption (soft furnishings)
                break;
            case 2: // Large Hall - spacious, slow buildup
                setRoomDimensions(25.0f, 10.0f, 40.0f);
                reflectionDensity = 0.7f;  // Sparser reflections
                wallAbsorption = 0.15f;  // Less absorption (hard walls)
                break;
            case 3: // Cathedral - enormous, diffuse
                setRoomDimensions(40.0f, 18.0f, 60.0f);
                reflectionDensity = 0.5f;  // Very sparse
                wallAbsorption = 0.1f;  // Very reflective (stone)
                break;
            case 4: // Chamber - small, live
                setRoomDimensions(6.0f, 4.0f, 7.0f);
                reflectionDensity = 1.3f;  // Fairly dense
                wallAbsorption = 0.2f;  // Live (wood/tile)
                break;
            case 5: // Warehouse - large, asymmetric
                setRoomDimensions(30.0f, 8.0f, 35.0f);
                reflectionDensity = 0.8f;  // Moderate density
                wallAbsorption = 0.25f;  // Mixed surfaces
                break;
            case 6: // Booth - tiny, dead
                setRoomDimensions(3.0f, 2.2f, 3.5f);
                reflectionDensity = 2.0f;  // Very dense (close walls)
                wallAbsorption = 0.7f;  // Highly absorptive (foam)
                break;
            case 7: // Tunnel - long, narrow
                setRoomDimensions(4.0f, 3.0f, 50.0f);
                reflectionDensity = 0.6f;  // Sparse
                wallAbsorption = 0.2f;  // Concrete
                break;
            default:
                setRoomDimensions(8.0f, 3.5f, 10.0f);
                reflectionDensity = 1.0f;
                wallAbsorption = 0.3f;
        }
    }

protected:
    double sampleRate = 48000.0;
    std::vector<Reflection> reflections;
    std::array<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>, 50> delays;
    float modPhase = 0.0f;  // For natural time modulation

    // Room dimensions for early reflections
    float roomWidth = 8.0f;
    float roomHeight = 3.5f;
    float roomDepth = 10.0f;

    // Room acoustic characteristics
    float reflectionDensity = 1.0f;  // Controls reflection spacing/count (0.5 = sparse, 2.0 = dense)
    float wallAbsorption = 0.3f;     // Controls reflection amplitude (0.0 = none, 1.0 = full)
};

//==============================================================================
/**
    Enhanced reverb engine with realistic algorithms
*/
class ReverbEngineEnhanced
{
public:
    ReverbEngineEnhanced()
        : oversampling2x(2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR),
          oversampling4x(2, 3, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR)
    {
    }

    void prepare(const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;

        // Prepare oversampling (both 2x and 4x)
        oversampling2x.initProcessing(spec.maximumBlockSize);
        oversampling2x.reset();
        oversampling4x.initProcessing(spec.maximumBlockSize);
        oversampling4x.reset();

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

        // Cascade of 5 peaking filters for realistic plate emulation (EMT 140 characteristics)
        // Each filter models specific resonant modes of the metal plate
        // Frequencies based on physical plate resonances: 800Hz, 1.5kHz, 2.8kHz, 5kHz, 8kHz
        const float plateFrequencies[5] = { 800.0f, 1500.0f, 2800.0f, 5000.0f, 8000.0f };
        // Convert Q values to normalized resonance (resonance = 1/Q, clamped to valid range)
        // Original Q values: 3.0, 4.0, 5.0, 3.5, 2.5
        const float plateResonances[5] = {
            std::min(1.0f, 1.0f / 3.0f),   // 0.333
            std::min(1.0f, 1.0f / 4.0f),   // 0.250
            std::min(1.0f, 1.0f / 5.0f),   // 0.200
            std::min(1.0f, 1.0f / 3.5f),   // 0.286
            std::min(1.0f, 1.0f / 2.5f)    // 0.400
        };

        for (int i = 0; i < 5; ++i)
        {
            plateCascadeFilters[i].prepare(spec);
            plateCascadeFilters[i].setType(juce::dsp::StateVariableTPTFilterType::bandpass);
            plateCascadeFilters[i].setCutoffFrequency(plateFrequencies[i]);
            plateCascadeFilters[i].setResonance(plateResonances[i]);
        }

        // Modulation LFOs - frequencies will be updated based on size parameter
        modulationLFO1.initialise([](float x) { return std::sin(x); });
        modulationLFO2.initialise([](float x) { return std::sin(x); });
        // Initial frequencies - will be modulated by size
        modulationLFO1.setFrequency(0.3f);
        modulationLFO2.setFrequency(0.5f);
        modulationLFO1.prepare(spec);
        modulationLFO2.prepare(spec);

        // Prepare vintage/saturation
        saturator.functionToUse = [](float x) { return std::tanh(x * 1.5f) / 1.5f; };  // Soft saturation
        saturator.prepare(spec);

        // Prepare wow/flutter LFO for analog tape character
        wowFlutterLFO.initialise([](float x) {
            // Combine sine with subtle randomness for organic wow/flutter
            return std::sin(x) * 0.7f + std::sin(x * 2.3f) * 0.3f;
        });
        wowFlutterLFO.setFrequency(0.3f);  // Slow, tape-like modulation (0.3 Hz)
        wowFlutterLFO.prepare(spec);

        // Initialize parameter smoothers with optimized ramp times per parameter type
        // Faster smoothing for mix/width (10ms) - need immediate response for mix changes
        // Medium smoothing for size/damping (20ms) - balance between response and artifacts
        // Slower smoothing for predelay (50ms) - prevent pitch artifacts from delay modulation
        sizeSmooth.reset(sampleRate, 0.020f);      // 20ms - moderate smoothing for reverb density
        dampingSmooth.reset(sampleRate, 0.020f);   // 20ms - moderate smoothing for tone changes
        mixSmooth.reset(sampleRate, 0.010f);       // 10ms - fast response for dry/wet balance
        widthSmooth.reset(sampleRate, 0.010f);     // 10ms - fast response for stereo width
        predelaySmooth.reset(sampleRate, 0.050f);  // 50ms - slow smoothing prevents pitch artifacts

        // Set initial values
        sizeSmooth.setCurrentAndTargetValue(currentSize);
        dampingSmooth.setCurrentAndTargetValue(currentDamping);
        mixSmooth.setCurrentAndTargetValue(currentMix);
        widthSmooth.setCurrentAndTargetValue(currentWidth);
        predelaySmooth.setCurrentAndTargetValue(0.0f);

        // Initialize reverse buffers for reverse reverb mode (1 second buffer)
        reverseBufferSize = static_cast<int>(sampleRate);
        reverseBufferL.resize(reverseBufferSize, 0.0f);
        reverseBufferR.resize(reverseBufferSize, 0.0f);
        reverseBufferPos = 0;

        // Reset everything to clear any garbage
        reset();
    }

    void process(juce::AudioBuffer<float>& buffer)
    {
        // HIGH PRIORITY: Set denormal flush-to-zero for this thread (prevents CPU spikes)
        juce::ScopedNoDenormals noDenormals;

        // Apply variable oversampling based on oversamplingFactor
        juce::dsp::AudioBlock<float> block(buffer);

        // Only oversample in plate mode (non-linear) to reduce aliasing
        if (oversamplingEnabled && currentAlgorithm == 2 && oversamplingFactor > 1)
        {
            if (oversamplingFactor == 2)
            {
                // 2x oversampling
                auto oversampledBlock = oversampling2x.processSamplesUp(block);
                processInternal(oversampledBlock);
                oversampling2x.processSamplesDown(block);
            }
            else if (oversamplingFactor == 4)
            {
                // 4x oversampling
                auto oversampledBlock = oversampling4x.processSamplesUp(block);
                processInternal(oversampledBlock);
                oversampling4x.processSamplesDown(block);
            }
        }
        else
        {
            // No oversampling (direct processing)
            processInternal(block);
        }
    }

    void processInternal(juce::dsp::AudioBlock<float>& block)
    {
        const int numSamples = static_cast<int>(block.getNumSamples());
        float* leftChannel = block.getChannelPointer(0);
        float* rightChannel = block.getChannelPointer(1);

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

            // Update predelay with wow/flutter modulation for analog tape character
            int maxPredelayInSamples = predelayL.getMaximumDelayInSamples();
            smoothedPredelaySamples = juce::jlimit(0.0f, static_cast<float>(maxPredelayInSamples - 1), smoothedPredelaySamples);

            // Apply wow/flutter when vintage is enabled
            float wowFlutterMod = 0.0f;
            if (currentVintage > 0.001f)
            {
                // Subtle delay modulation (0.2% max depth) for tape-like wow/flutter
                wowFlutterMod = wowFlutterLFO.processSample(0.0f) * 0.002f * currentVintage;
            }

            float modulatedPredelayL = smoothedPredelaySamples * (1.0f + wowFlutterMod);
            float modulatedPredelayR = smoothedPredelaySamples * (1.0f + wowFlutterMod * 0.9f);  // Slightly decorrelated

            modulatedPredelayL = juce::jlimit(1.0f, static_cast<float>(maxPredelayInSamples - 1), modulatedPredelayL);
            modulatedPredelayR = juce::jlimit(1.0f, static_cast<float>(maxPredelayInSamples - 1), modulatedPredelayR);

            predelayL.setDelay(modulatedPredelayL);
            predelayR.setDelay(modulatedPredelayR);

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

            // Process late reverb through FDN with clamped decay and modulation
            float lateL, lateR;
            float clampedDecay = juce::jlimit(0.0f, 0.999f, currentDecay);

            // Reduce modulation depth in infinite mode to prevent buildup from constructive interference
            float fdnModDepth = infiniteMode ? 0.3f : 1.0f;  // Conservative in infinite mode, full otherwise

            fdn.process(delayedL, delayedR, lateL, lateR, smoothedSize, clampedDecay, smoothedDamping, fdnModDepth);

            // HIGH PRIORITY: Sanitize FDN output (works in release builds unlike jassert)
            if (std::isnan(lateL) || std::isinf(lateL)) lateL = 0.0f;
            if (std::isnan(lateR) || std::isinf(lateR)) lateR = 0.0f;

            // Size-dependent modulation for realistic shimmer (larger spaces = slower modulation)
            // Update LFO rates based on size (smaller size = faster rates for tighter spaces)
            float lfoRate1 = 0.2f + (1.0f - smoothedSize) * 0.6f;  // 0.2Hz to 0.8Hz
            float lfoRate2 = 0.3f + (1.0f - smoothedSize) * 0.8f;  // 0.3Hz to 1.1Hz
            modulationLFO1.setFrequency(lfoRate1);
            modulationLFO2.setFrequency(lfoRate2);

            // Depth also scales with size (larger spaces = more shimmer)
            float baseDepth = (currentAlgorithm == 2) ? 0.005f : 0.002f;  // More shimmer for plate
            float shimmerModDepth = baseDepth * (0.5f + smoothedSize * 0.5f);  // Scale depth with size
            float mod1 = modulationLFO1.processSample(0.0f) * shimmerModDepth;
            float mod2 = modulationLFO2.processSample(0.0f) * shimmerModDepth;
            lateL *= (1.0f + mod1);
            lateR *= (1.0f + mod2);

            // Apply tone shaping
            lateL = lowShelf.processSample(0, lateL);
            lateR = lowShelf.processSample(1, lateR);
            lateL = highShelf.processSample(0, lateL);
            lateR = highShelf.processSample(1, lateR);

            // Apply cascade of metallic resonances for plate mode (EMT 140 modeling)
            if (currentAlgorithm == 2) // Plate mode
            {
                // Damping affects the Q-factor of all resonances (less damping = sharper peaks)
                float qScale = 0.5f + (1.0f - smoothedDamping) * 1.5f;  // 0.5 to 2.0

                // Size affects frequency scaling (larger = higher resonances)
                float freqScale = 0.8f + smoothedSize * 0.4f;  // 0.8 to 1.2

                float metallicL = lateL;
                float metallicR = lateR;

                // Process through cascade of 5 resonant filters
                const float baseFreqs[5] = { 800.0f, 1500.0f, 2800.0f, 5000.0f, 8000.0f };
                const float baseQs[5] = { 3.0f, 4.0f, 5.0f, 3.5f, 2.5f };

                for (int i = 0; i < 5; ++i)
                {
                    float scaledFreq = baseFreqs[i] * freqScale;
                    float scaledQ = baseQs[i] * qScale;

                    plateCascadeFilters[i].setCutoffFrequency(scaledFreq);
                    plateCascadeFilters[i].setResonance(scaledQ);

                    metallicL = plateCascadeFilters[i].processSample(0, metallicL);
                    metallicR = plateCascadeFilters[i].processSample(1, metallicR);
                }

                // Mix cascaded resonances with dry for natural plate character
                // More resonance with less damping
                float metallicMix = 0.25f + (1.0f - smoothedDamping) * 0.35f;  // 0.25 to 0.6
                lateL = lateL * (1.0f - metallicMix) + metallicL * metallicMix;
                lateR = lateR * (1.0f - metallicMix) + metallicR * metallicMix;
            }

            // Apply non-linear reverb modes (Gated or Reverse)
            if (currentAlgorithm == 4) // Gated mode
            {
                // Envelope follower tracks input amplitude
                float inputEnvelope = std::max(std::abs(inputL), std::abs(inputR));

                // Fast attack, slow release
                if (inputEnvelope > envelopeFollower)
                    envelopeFollower = inputEnvelope;  // Instant attack
                else
                    envelopeFollower = envelopeFollower * gateRelease;  // Slow release (0.99 = ~100ms @ 48kHz)

                // Apply gate - reverb only audible when envelope above threshold
                float gateGain = (envelopeFollower > gateThreshold) ? 1.0f : 0.0f;

                // Smooth gate transitions with small amount of hysteresis
                gateGain = this->lastGateGain * 0.95f + gateGain * 0.05f;  // 95% previous, 5% new
                this->lastGateGain = gateGain;

                lateL *= gateGain;
                lateR *= gateGain;
            }
            else if (currentAlgorithm == 5) // Reverse mode
            {
                // Write current reverb to circular buffer
                reverseBufferL[reverseBufferPos] = lateL;
                reverseBufferR[reverseBufferPos] = lateR;

                // Read from buffer in reverse (1 second ago, going backwards)
                int readPos = (reverseBufferPos - 1 + reverseBufferSize) % reverseBufferSize;
                lateL = reverseBufferL[readPos];
                lateR = reverseBufferR[readPos];

                // Calculate sample age relative to current write position
                int age = (reverseBufferPos - readPos + reverseBufferSize) % reverseBufferSize;
                float normalizedAge = static_cast<float>(age) / static_cast<float>(reverseBufferSize);

                // Create reverse swell based on sample age (newer samples are louder)
                // Using 1.0 - normalizedAge so newer samples (low age) have higher gain
                float reverseFade = 1.0f - normalizedAge;
                // Optional: apply a curve for more dramatic shaping
                // reverseFade = powf(reverseFade, 2.0f);  // Square for faster buildup

                lateL *= reverseFade;
                lateR *= reverseFade;

                // Advance write position
                reverseBufferPos = (reverseBufferPos + 1) % reverseBufferSize;
            }

            // Mix early and late
            float reverbL = earlyL * earlyGain + lateL * lateGain;
            float reverbR = earlyR * earlyGain + lateR * lateGain;

            // Task 10: Apply width control with smoothed value
            float mid = (reverbL + reverbR) * 0.5f;
            float side = (reverbL - reverbR) * 0.5f * smoothedWidth;
            reverbL = mid + side;
            reverbR = mid - side;

            // Apply vintage character (analog noise + saturation + hysteresis) to wet signal only
            if (currentVintage > 0.001f)
            {
                // Add subtle pink-noise-like character
                float noise = (noiseGenerator.nextFloat() * 2.0f - 1.0f) * 0.001f * currentVintage;
                reverbL += noise;
                reverbR += noise * 0.9f;  // Slightly decorrelated

                // Apply soft tape-like saturation
                float satAmount = currentVintage * 0.3f;
                reverbL = saturator.processSample(reverbL * (1.0f + satAmount)) / (1.0f + satAmount);
                reverbR = saturator.processSample(reverbR * (1.0f + satAmount)) / (1.0f + satAmount);

                // Apply tape-like hysteresis (magnetic memory effect)
                // This creates a subtle low-pass filtering and smearing characteristic of tape
                float hysteresisAlpha = 0.05f + (currentVintage * 0.15f);  // 5-20% blend based on vintage amount
                hysteresisStateL = reverbL * hysteresisAlpha + hysteresisStateL * (1.0f - hysteresisAlpha);
                hysteresisStateR = reverbR * hysteresisAlpha + hysteresisStateR * (1.0f - hysteresisAlpha);

                // Blend hysteresis with direct signal for subtle effect
                reverbL = reverbL * (1.0f - currentVintage * 0.3f) + hysteresisStateL * (currentVintage * 0.3f);
                reverbR = reverbR * (1.0f - currentVintage * 0.3f) + hysteresisStateR * (currentVintage * 0.3f);
            }

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

        // Reset plate cascade filters
        for (auto& filter : plateCascadeFilters)
            filter.reset();

        // Reset gate smoothing state
        lastGateGain = 0.0f;

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

    // Tempo-synced predelay
    void setPredelayBeats(float beats, double bpm)
    {
        if (bpm > 0.0)
        {
            float msPerBeat = 60000.0f / static_cast<float>(bpm);
            float ms = beats * msPerBeat;
            setPredelay(ms);
        }
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

    // Multiband RT60 control
    void setLowDecayTime(float seconds)
    {
        lowRT60 = juce::jlimit(0.1f, 10.0f, seconds);
        updateMultibandDecay();
    }

    void setMidDecayTime(float seconds)
    {
        midRT60 = juce::jlimit(0.1f, 10.0f, seconds);
        updateMultibandDecay();
    }

    void setHighDecayTime(float seconds)
    {
        highRT60 = juce::jlimit(0.1f, 10.0f, seconds);
        updateMultibandDecay();
    }

    // Infinite decay mode
    void setInfiniteDecay(bool infinite)
    {
        infiniteMode = infinite;
        if (infinite)
        {
            // Set to 0.995 with additional headroom for modulation-induced energy buildup
            // Per-delay modulation can cause constructive interference, so we need lower feedback
            currentDecay = 0.995f; // Stable near-infinite feedback (0.999 risks instability at high SR)

            // Also set FDN to use conservative per-band feedback for infinite mode
            // This prevents modulation-induced buildup across frequency bands
            fdn.setPerBandRT60(100.0f, 100.0f, 80.0f);  // Very long but stable RT60 times
        }
        else
        {
            updateMultibandDecay();
        }
    }

    // Enable/disable oversampling with variable factor
    void setOversamplingEnabled(bool enabled)
    {
        oversamplingEnabled = enabled;
    }

    void setOversamplingFactor(int factor)
    {
        // 1 = off, 2 = 2x, 4 = 4x
        oversamplingFactor = juce::jlimit(1, 4, factor);
    }

    // Room shape presets
    void setRoomShape(int shape)
    {
        earlyReflections.setRoomShape(shape);
    }

    // Vintage/warmth control
    void setVintage(float vintage)
    {
        currentVintage = juce::jlimit(0.0f, 1.0f, vintage);
    }

    // Get oversampling latency for host reporting
    int getOversamplingLatency() const
    {
        if (!oversamplingEnabled || oversamplingFactor <= 1)
            return 0;

        // Approximate latency values for polyphase IIR oversampling
        return (oversamplingFactor == 2) ? 128 : 256;
    }

    // Return max tail samples for accurate DAW rendering (dynamic based on RT60 and size)
    int getMaxTailSamples() const
    {
        // Return a reasonable default if not prepared yet
        if (sampleRate <= 0)
            return static_cast<int>(48000 * 5.0);  // Assume 48kHz, 5s default tail

        // Calculate tail based on longest RT60 band (60dB decay time)
        // Add safety factor for size parameter which scales delay times
        float maxRT60 = std::max(lowRT60, std::max(midRT60, highRT60));

        // Account for size parameter scaling (size can extend reverb up to 2x)
        float sizeScale = 0.5f + currentSize * 1.5f;  // Range: 0.5 to 2.0

        // Add predelay to total tail length (max 200ms = 0.2s)
        float totalTailSeconds = (maxRT60 * sizeScale) + 0.2f;

        // Ensure minimum tail of 1 second for short RT60 settings
        totalTailSeconds = std::max(1.0f, totalTailSeconds);

        return static_cast<int>(sampleRate * totalTailSeconds);
    }

    void updateMultibandDecay()
    {
        if (!infiniteMode)
        {
            // Use accurate per-band RT60 control for FDN
            fdn.setPerBandRT60(lowRT60, midRT60, highRT60);

            // Also update legacy currentDecay for algorithm switching (average for compatibility)
            float avgRT60 = (lowRT60 + midRT60 + highRT60) / 3.0f;
            currentDecay = std::exp(-6.9078f / (avgRT60 * sampleRate));
            currentDecay = juce::jlimit(0.0f, 0.999f, currentDecay);
        }
        // Note: Infinite mode sets its own per-band RT60 values in setInfiniteDecay()
        // to prevent modulation-induced buildup, so we don't override it here
    }

    double sampleRate = 48000.0;

    // DSP Components
    FeedbackDelayNetwork fdn;
    SpatialEarlyReflections earlyReflections;

    // Variable oversampling for anti-aliasing (2x and 4x)
    juce::dsp::Oversampling<float> oversampling2x;
    juce::dsp::Oversampling<float> oversampling4x;
    bool oversamplingEnabled = false;
    int oversamplingFactor = 1;  // 1=off, 2=2x, 4=4x

    // HIGH PRIORITY: Use Linear interpolation to prevent clicks on predelay changes
    // Predelay buffers sized for 200ms at 192kHz (38400 samples, rounded up to 40000 for safety)
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> predelayL { 40000 };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> predelayR { 40000 };

    juce::dsp::StateVariableTPTFilter<float> lowShelf;
    juce::dsp::StateVariableTPTFilter<float> highShelf;

    // Cascade of peaking filters for realistic plate emulation
    // Models complex frequency response of physical EMT 140 plate
    std::array<juce::dsp::StateVariableTPTFilter<float>, 5> plateCascadeFilters;

    juce::dsp::Oscillator<float> modulationLFO1;
    juce::dsp::Oscillator<float> modulationLFO2;

    // Vintage/analog character
    juce::dsp::WaveShaper<float> saturator;
    juce::Random noiseGenerator;
    float currentVintage = 0.0f;

    // Hysteresis for tape-like saturation
    float hysteresisStateL = 0.0f;
    float hysteresisStateR = 0.0f;

    // Wow/flutter LFO for analog tape character
    juce::dsp::Oscillator<float> wowFlutterLFO;
    juce::Random wowFlutterRandom;

    // Non-linear reverb modes (Gated and Reverse)
    std::vector<float> reverseBufferL;
    std::vector<float> reverseBufferR;
    int reverseBufferPos = 0;
    int reverseBufferSize = 0;
    float envelopeFollower = 0.0f;
    float gateThreshold = 0.1f;
    float gateRelease = 0.99f;  // Envelope release coefficient
    float lastGateGain = 0.0f;  // Per-instance gate smoothing state

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

    // Multiband RT60 parameters
    float lowRT60 = 2.0f;
    float midRT60 = 2.0f;
    float highRT60 = 1.5f;

    // Infinite decay mode
    bool infiniteMode = false;

    float earlyGain = 0.5f;
    float lateGain = 0.5f;

    // Parameter smoothers to prevent zipper noise
    juce::SmoothedValue<float> sizeSmooth;
    juce::SmoothedValue<float> dampingSmooth;
    juce::SmoothedValue<float> mixSmooth;
    juce::SmoothedValue<float> widthSmooth;
    juce::SmoothedValue<float> predelaySmooth;
};