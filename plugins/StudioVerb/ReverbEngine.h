/*
  ==============================================================================

    ReverbEngine.h
    Studio Verb - Reverb DSP Engine
    Copyright (c) 2024 Luna CO. Audio

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <random>
#include <vector>

//==============================================================================
/**
    One-pole low-pass filter for damping
*/
class DampingFilter
{
public:
    DampingFilter() = default;

    void reset() { state = 0.0f; }

    void setFrequency(float freq, float sampleRate)
    {
        float omega = juce::MathConstants<float>::twoPi * freq / sampleRate;
        coefficient = std::sin(omega) / (std::sin(omega) + std::cos(omega));
    }

    float process(float input)
    {
        state = input * coefficient + state * (1.0f - coefficient);
        return state;
    }

private:
    float coefficient = 0.5f;
    float state = 0.0f;
};

//==============================================================================
/**
    Allpass filter for diffusion
*/
class AllpassFilter
{
public:
    AllpassFilter() = default;

    void setSize(int samples)
    {
        delayLine.setMaximumDelayInSamples(samples);
        delayLine.setDelay(static_cast<float>(samples));
    }

    void reset()
    {
        delayLine.reset();
    }

    void prepare(const juce::dsp::ProcessSpec& spec)
    {
        delayLine.prepare(spec);
    }

    float process(float input, float coefficient = 0.7f)
    {
        float delayed = delayLine.popSample(0);
        float output = -input + delayed;
        delayLine.pushSample(0, input + (delayed * coefficient));
        return output;
    }

private:
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine { 48000 };
};

//==============================================================================
/**
    Comb filter for reverb tail
*/
class CombFilter
{
public:
    CombFilter() = default;

    void setSize(int samples)
    {
        delayLine.setMaximumDelayInSamples(samples);
        delayLine.setDelay(static_cast<float>(samples));
    }

    void setFeedback(float fb) { feedback = fb; }

    void setDamping(float freq, float sampleRate)
    {
        dampingFilter.setFrequency(freq, sampleRate);
    }

    void reset()
    {
        delayLine.reset();
        dampingFilter.reset();
    }

    void prepare(const juce::dsp::ProcessSpec& spec)
    {
        delayLine.prepare(spec);
    }

    float process(float input)
    {
        float delayed = delayLine.popSample(0);
        float filtered = dampingFilter.process(delayed);
        delayLine.pushSample(0, input + (filtered * feedback));
        return delayed;
    }

    // For modulation (plate reverb)
    void setModulation(float depth, float rate, float sampleRate)
    {
        modulationDepth = depth;
        modulationRate = rate;
        modulationPhase = 0.0f;
        modulationIncrement = juce::MathConstants<float>::twoPi * rate / sampleRate;
    }

    void updateModulation()
    {
        if (modulationDepth > 0.0f)
        {
            modulationPhase += modulationIncrement;
            if (modulationPhase >= juce::MathConstants<float>::twoPi)
                modulationPhase -= juce::MathConstants<float>::twoPi;

            float modAmount = std::sin(modulationPhase) * modulationDepth;
            float currentDelay = baseDelay * (1.0f + modAmount);
            delayLine.setDelay(currentDelay);
        }
    }

    void setBaseDelay(float delay) { baseDelay = delay; }

    // Getters for private members
    float getBaseDelay() const { return baseDelay; }
    float getFeedback() const { return feedback; }

private:
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine { 96000 };
    DampingFilter dampingFilter;
    float feedback = 0.5f;

    // Modulation
    float modulationDepth = 0.0f;
    float modulationRate = 0.0f;
    float modulationPhase = 0.0f;
    float modulationIncrement = 0.0f;
    float baseDelay = 0.0f;
};

//==============================================================================
/**
    Early reflection tap
*/
struct EarlyReflectionTap
{
    float delay = 0.0f;  // in samples
    float gain = 0.0f;   // amplitude
    float panLeft = 0.707f;
    float panRight = 0.707f;
};

//==============================================================================
/**
    Main reverb engine with multiple algorithms
*/
class ReverbEngine
{
public:
    ReverbEngine();
    ~ReverbEngine() = default;

    // Preparation
    void prepare(const juce::dsp::ProcessSpec& spec);
    void reset();

    // Processing
    void process(juce::AudioBuffer<float>& buffer);

    // Parameters
    void setAlgorithm(int algorithm);
    void setSize(float size);
    void setDamping(float damp);
    void setPredelay(float predelayMs);
    void setMix(float mix);

private:
    // Processing spec
    double sampleRate = 48000.0;
    int blockSize = 512;

    // Current parameters
    int currentAlgorithm = 0;
    float currentSize = 0.5f;
    float currentDamping = 0.5f;
    float currentPredelayMs = 0.0f;
    float currentMix = 0.5f;

    // Maximum counts
    static constexpr int MAX_COMBS = 16;
    static constexpr int MAX_ALLPASSES = 8;
    static constexpr int MAX_EARLY_TAPS = 20;

    // DSP components
    std::array<CombFilter, MAX_COMBS> combFiltersL;
    std::array<CombFilter, MAX_COMBS> combFiltersR;
    std::array<AllpassFilter, MAX_ALLPASSES> allpassFiltersL;
    std::array<AllpassFilter, MAX_ALLPASSES> allpassFiltersR;

    // Predelay
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> predelayL { 48000 };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> predelayR { 48000 };

    // Early reflections
    std::array<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>, MAX_EARLY_TAPS> earlyTapsL;
    std::array<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>, MAX_EARLY_TAPS> earlyTapsR;
    std::array<EarlyReflectionTap, MAX_EARLY_TAPS> earlyReflectionData;

    // Soft limiter
    juce::dsp::Limiter<float> limiterL;
    juce::dsp::Limiter<float> limiterR;

    // Algorithm-specific counts
    int numActiveCombs = 8;
    int numActiveAllpasses = 4;
    int numActiveEarlyTaps = 0;

    // Random number generation for jitter
    std::mt19937 randomGenerator;
    std::uniform_real_distribution<float> jitterDistribution { -0.1f, 0.1f };

    // Algorithm configuration methods
    void configureRoomAlgorithm();
    void configureHallAlgorithm();
    void configurePlateAlgorithm();
    void configureEarlyReflectionsAlgorithm();

    // Helper methods
    void updateCombFilters();
    void updateAllpassFilters();
    void updateEarlyReflections();
    float calculateDampingFrequency(float dampParam) const;

    // Processing methods
    void processRoomHall(juce::AudioBuffer<float>& buffer);
    void processPlate(juce::AudioBuffer<float>& buffer);
    void processEarlyReflections(juce::AudioBuffer<float>& buffer);
};