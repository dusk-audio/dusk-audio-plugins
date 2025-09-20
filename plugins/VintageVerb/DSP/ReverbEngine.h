/*
  ==============================================================================

    ReverbEngine.h - Core FDN-based reverb processing engine

    This implements a high-quality Feedback Delay Network (FDN) reverb
    with multiple delay lines, diffusion networks, and modulation.
    Inspired by classic Lexicon and EMT algorithms.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include <random>

class ReverbEngine
{
public:
    ReverbEngine();
    ~ReverbEngine() = default;

    // Preparation
    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    // Processing
    void process(juce::AudioBuffer<float>& buffer, int numSamples);
    void processStereo(float* leftIn, float* rightIn,
                      float* leftOut, float* rightOut, int numSamples);

    // Parameters (0.0 - 1.0 normalized)
    void setSize(float newSize);               // Room size / decay time
    void setDiffusion(float newDiffusion);     // Input diffusion amount
    void setDensity(float newDensity);         // Echo density
    void setDamping(float newDamping);         // High frequency damping
    void setModulation(float newModulation);   // Modulation depth
    void setShape(float newShape);             // Early/late balance
    void setSpread(float newSpread);           // Stereo spread
    void setAttack(float newAttack);           // Build-up time

    // Mode-specific configurations
    void configureForMode(int mode);

    // Get current decay time estimate
    float getDecayTime() const { return currentDecayTime; }

private:
    // FDN Configuration
    static constexpr int NUM_DELAY_LINES = 16;  // 16-line FDN for dense reverb
    static constexpr int NUM_ALLPASS = 8;       // Allpass diffusers
    static constexpr int MAX_DELAY_SAMPLES = 192000;  // ~4 seconds at 48kHz

    // Delay line structure
    struct DelayLine
    {
        std::vector<float> buffer;
        int writePos = 0;
        int size = 0;
        float feedback = 0.5f;
        float damping = 0.5f;
        float lastOut = 0.0f;

        void prepare(int maxSize);
        float process(float input);
        void setDelayTime(int samples);
        void modulate(float modAmount);
    };

    // Allpass filter for diffusion
    struct AllpassFilter
    {
        std::vector<float> buffer;
        int writePos = 0;
        int size = 0;
        float feedback = 0.618f;  // Golden ratio for smooth diffusion

        void prepare(int maxSize);
        float process(float input);
        void setDelayTime(int samples);
    };

    // Early reflections generator
    struct EarlyReflections
    {
        static constexpr int NUM_TAPS = 24;

        std::vector<float> buffer;
        int writePos = 0;
        std::array<int, NUM_TAPS> tapDelays;
        std::array<float, NUM_TAPS> tapGains;
        std::array<float, NUM_TAPS> tapPans;

        void prepare(int maxSize);
        void generateTaps(float size, float shape);
        std::pair<float, float> process(float input);
    };

    // DSP Components
    std::array<DelayLine, NUM_DELAY_LINES> delayLines;
    std::array<AllpassFilter, NUM_ALLPASS> inputDiffusers;
    std::array<AllpassFilter, NUM_ALLPASS> outputDiffusers;
    EarlyReflections earlyReflections;

    // Hadamard mixing matrix for FDN
    std::array<std::array<float, NUM_DELAY_LINES>, NUM_DELAY_LINES> mixMatrix;
    void initializeMixMatrix();

    // Modulation LFOs
    struct ModulationLFO
    {
        float phase = 0.0f;
        float frequency = 0.5f;
        float depth = 0.0f;

        float process();
        void setSampleRate(double sr);

    private:
        float sampleRate = 44100.0f;
        float phaseIncrement = 0.0f;
    };

    std::array<ModulationLFO, 4> modulationLFOs;

    // Damping filters
    struct DampingFilter
    {
        float lastOut = 0.0f;
        float frequency = 8000.0f;
        float amount = 0.5f;

        float process(float input);
        void setSampleRate(double sr);

    private:
        float coefficient = 0.5f;
        float sampleRate = 44100.0f;
    };

    std::array<DampingFilter, NUM_DELAY_LINES> dampingFilters;

    // State variables
    double sampleRate = 44100.0;
    int blockSize = 512;
    float currentDecayTime = 2.0f;

    // Parameters
    float size = 0.5f;
    float diffusion = 0.5f;
    float density = 0.5f;
    float damping = 0.5f;
    float modulation = 0.3f;
    float shape = 0.5f;
    float spread = 1.0f;
    float attack = 0.1f;

    // Prime numbers for delay times (in samples at 44.1kHz)
    static constexpr std::array<int, NUM_DELAY_LINES> primeDelays = {
        2003, 2011, 2017, 2027, 2029, 2039, 2053, 2063,
        2069, 2081, 2083, 2087, 2089, 2099, 2111, 2113
    };

    // Helper functions
    void updateDelayTimes();
    float processFDN(float input, int channel);
    float softClip(float input);
    float crossfade(float a, float b, float mix);

    // Random number generation for decorrelation
    std::mt19937 randomGen{std::random_device{}()};
    std::uniform_real_distribution<float> randomDist{-1.0f, 1.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverbEngine)
};