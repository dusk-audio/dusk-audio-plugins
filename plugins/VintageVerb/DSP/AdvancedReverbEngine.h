/*
  ==============================================================================

    AdvancedReverbEngine.h - Professional-grade FDN reverb with Valhalla/Lexicon quality

    Features:
    - Interpolated fractional delays with anti-aliasing
    - Frequency-dependent RT60 control (multi-band decay)
    - True stereo FDN topology (dual interleaved networks)
    - Advanced modulation (spin, wander, chorus)
    - Psychoacoustic enhancements and crossover processing

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include <random>
#include <complex>

class AdvancedReverbEngine
{
public:
    AdvancedReverbEngine();
    ~AdvancedReverbEngine() = default;

    // Preparation
    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    // Processing
    void processStereo(float* leftIn, float* rightIn,
                      float* leftOut, float* rightOut, int numSamples);

    // Main Parameters (0.0 - 1.0 normalized)
    void setSize(float newSize);
    void setDiffusion(float newDiffusion);
    void setDamping(float newDamping);
    void setModulationDepth(float depth);
    void setModulationRate(float rate);

    // Frequency-dependent decay controls
    void setLowDecay(float decay);      // RT60 multiplier for low frequencies
    void setMidDecay(float decay);      // RT60 multiplier for mid frequencies
    void setHighDecay(float decay);     // RT60 multiplier for high frequencies
    void setCrossoverLow(float freq);   // Low/mid crossover frequency
    void setCrossoverHigh(float freq);  // Mid/high crossover frequency

    // Advanced modulation controls
    void setSpinRate(float rate);       // Circular modulation rate
    void setWander(float amount);       // Random walk modulation
    void setChorus(float amount);       // Chorusing in reverb tail

    // Psychoacoustic enhancements
    void setPreDelay(float ms);
    void setStereoWidth(float width);
    void setEarlyLateMix(float mix);
    void setDensity(float density);

    // Mode configurations
    void configureForMode(int mode);

private:
    // Enhanced configuration for professional quality
    static constexpr int NUM_DELAY_LINES = 32;        // Doubled for true stereo
    static constexpr int NUM_ALLPASS = 12;            // More diffusers
    static constexpr int MAX_DELAY_SAMPLES = 192000;
    static constexpr int OVERSAMPLE_FACTOR = 2;       // 2x oversampling

    // Interpolated delay line with Hermite interpolation
    class InterpolatedDelayLine
    {
    public:
        void prepare(int maxSize);
        float read(float delaySamples) const;
        void write(float input);
        void setDelay(float samples);
        float readWithModulation(float delaySamples, float modAmount) const;

    private:
        std::vector<float> buffer;
        int writePos = 0;
        float currentDelay = 0.0f;

        // Hermite interpolation for smooth fractional delays
        float hermiteInterpolate(float x, float y0, float y1, float y2, float y3) const
        {
            float c0 = y1;
            float c1 = 0.5f * (y2 - y0);
            float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            return ((c3 * x + c2) * x + c1) * x + c0;
        }
    };

    // Multi-band damping filter
    class MultibandDamping
    {
    public:
        void prepare(double sampleRate);
        float process(float input);
        void setDecayTimes(float lowRT60, float midRT60, float highRT60);
        void setCrossoverFrequencies(float lowFreq, float highFreq);

    private:
        // Linkwitz-Riley crossover filters
        juce::dsp::LinkwitzRileyFilter<float> lowCrossover;
        juce::dsp::LinkwitzRileyFilter<float> highCrossover;

        // Band-specific damping
        juce::dsp::IIR::Filter<float> lowShelf;
        juce::dsp::IIR::Filter<float> highShelf;

        float lowDecay = 1.0f;
        float midDecay = 1.0f;
        float highDecay = 0.7f;
        double sampleRate = 44100.0;
    };

    // Advanced modulation system
    class ModulationSystem
    {
    public:
        void prepare(double sampleRate);

        // Get modulation values for different purposes
        float getSpinModulation(int channel);
        float getWanderModulation(int delayIndex);
        float getChorusModulation(int voice);

        void setSpinRate(float hz);
        void setWanderAmount(float amount);
        void setChorusDepth(float depth);

    private:
        double sampleRate = 44100.0;

        // Spin modulation (circular/orbital motion)
        float spinPhase[2] = {0.0f, 0.5f};  // 180° offset for L/R
        float spinRate = 0.5f;

        // Wander modulation (random walk)
        std::array<float, NUM_DELAY_LINES> wanderValues;
        std::array<float, NUM_DELAY_LINES> wanderTargets;
        float wanderRate = 0.1f;
        float wanderAmount = 0.0f;

        // Chorus modulation (multiple LFOs)
        static constexpr int NUM_CHORUS_VOICES = 4;
        std::array<float, NUM_CHORUS_VOICES> chorusPhases;
        std::array<float, NUM_CHORUS_VOICES> chorusRates;
        float chorusDepth = 0.0f;

        // Smooth random number generation
        std::mt19937 rng{std::random_device{}()};
        std::normal_distribution<float> normalDist{0.0f, 1.0f};
    };

    // Nested allpass diffuser network (Valhalla-style)
    class NestedAllpassDiffuser
    {
    public:
        void prepare(int maxSize);
        float process(float input);
        void setDiffusion(float amount);
        void modulate(float amount);

    private:
        // Nested structure: outer allpass contains inner allpasses
        InterpolatedDelayLine outerDelay;
        std::array<InterpolatedDelayLine, 4> innerDelays;

        float outerFeedback = 0.5f;
        float innerFeedback = 0.5f;
        float diffusionAmount = 0.7f;

        // State variables for feedback loops
        float outerState = 0.0f;
        std::array<float, 4> innerStates = {0.0f, 0.0f, 0.0f, 0.0f};
    };

    // True stereo FDN structure
    struct StereoFDN
    {
        // Separate left and right delay networks
        std::array<InterpolatedDelayLine, NUM_DELAY_LINES/2> leftDelays;
        std::array<InterpolatedDelayLine, NUM_DELAY_LINES/2> rightDelays;

        // Cross-coupling matrix for stereo interaction
        std::array<std::array<float, NUM_DELAY_LINES>, NUM_DELAY_LINES> mixMatrix;

        // Per-channel damping
        std::array<MultibandDamping, NUM_DELAY_LINES> dampingFilters;

        // Feedback gains
        std::array<float, NUM_DELAY_LINES> feedbackGains;

        void prepare(double sampleRate);
        void process(float inputL, float inputR, float& outputL, float& outputR,
                    ModulationSystem& modulation);
        void setDecayTime(float rt60);
        void initializeMatrix();
    };

    // Psychoacoustic enhancement processors
    class PsychoacousticProcessor
    {
    public:
        void prepare(double sampleRate);
        void process(float& left, float& right);

        void setPreDelay(float ms);
        void setWidth(float amount);
        void setHaasDelay(float ms);

    private:
        // Pre-delay lines
        InterpolatedDelayLine preDelayL, preDelayR;

        // Haas effect delays for width
        InterpolatedDelayLine haasDelayL, haasDelayR;

        // Cross-feed for natural stereo
        float crossFeedAmount = 0.3f;

        double sampleRate = 44100.0;
    };

    // Early reflections with room modeling
    class RoomEarlyReflections
    {
    public:
        void prepare(double sampleRate);
        void process(float inputL, float inputR, float& outputL, float& outputR);
        void setRoomSize(float size);
        void setRoomShape(float shape);  // 0=rectangular, 1=irregular

    private:
        static constexpr int NUM_EARLY_TAPS = 32;

        struct ReflectionTap
        {
            float delayMs;
            float gainL, gainR;
            float filterCoeff;  // Simple lowpass for distance absorption
        };

        std::array<ReflectionTap, NUM_EARLY_TAPS> taps;
        InterpolatedDelayLine delayLineL, delayLineR;

        void generateReflectionPattern(float size, float shape);
        double sampleRate = 44100.0;
    };

    // Oversampling for high-frequency quality
    class Oversampler
    {
    public:
        void prepare(double sampleRate, int factor);
        void upsample(const float* input, float* output, int numSamples);
        void downsample(const float* input, float* output, int numSamples);

    private:
        juce::dsp::Oversampling<float> oversampling{2, 2,
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR};
    };

    // Main DSP components
    StereoFDN fdnNetwork;
    ModulationSystem modulation;
    PsychoacousticProcessor psychoacoustics;
    RoomEarlyReflections earlyReflections;

    // Diffusion networks
    std::array<NestedAllpassDiffuser, 4> inputDiffusers;
    std::array<NestedAllpassDiffuser, 4> outputDiffusers;

    // Oversampling
    Oversampler oversamplerL, oversamplerR;
    bool useOversampling = false;

    // State variables
    double sampleRate = 44100.0;
    int blockSize = 512;

    // Parameters
    float size = 0.5f;
    float diffusion = 0.7f;
    float damping = 0.3f;
    float modDepth = 0.3f;
    float modRate = 0.5f;
    float lowDecay = 1.0f;
    float midDecay = 1.0f;
    float highDecay = 0.7f;
    float crossoverLow = 250.0f;
    float crossoverHigh = 4000.0f;
    float spinRate = 0.5f;
    float wander = 0.2f;
    float chorus = 0.1f;
    float preDelay = 10.0f;
    float stereoWidth = 1.0f;
    float earlyLateMix = 0.5f;
    float density = 0.7f;

    // Helper functions
    float softClip(float input);
    void updateAllParameters();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AdvancedReverbEngine)
};