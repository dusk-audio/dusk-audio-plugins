/*
  ==============================================================================

    VintageColoration.h - Era-specific processing and artifacts

    Provides coloration modes inspired by different reverb eras:
    - 1970s: Dark, noisy, lo-fi with analog artifacts
    - 1980s: Bright, funky with early digital artifacts
    - Now: Clean, transparent, modern processing

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <random>

class VintageColoration
{
public:
    VintageColoration();
    ~VintageColoration() = default;

    // Preparation
    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    // Processing
    void process(juce::AudioBuffer<float>& buffer, int numSamples);
    void processStereo(float* leftIn, float* rightIn,
                      float* leftOut, float* rightOut, int numSamples);

    // Color modes
    enum ColorMode
    {
        Color1970s = 0,
        Color1980s,
        ColorNow
    };

    // Parameters
    void setColorMode(ColorMode mode);
    void setIntensity(float amount);  // 0.0 - 1.0
    void setNoiseAmount(float amount);
    void setArtifactAmount(float amount);

private:
    // 1970s Processing - Analog tape/tube artifacts
    struct Vintage1970s
    {
        // Tape saturation
        float saturate(float input, float amount);

        // Transformer emulation
        struct TransformerModel
        {
            float process(float input);
            void reset();

        private:
            float lastOut = 0.0f;
            float hysteresis = 0.0f;
        };

        // Noise generation
        struct NoiseGenerator
        {
            float pink = 0.0f;
            float brown = 0.0f;
            std::array<float, 7> pinkFilters = {};

            float generatePink();
            float generateBrown();
            float generate60HzHum(float phase);
        };

        TransformerModel transformerL, transformerR;
        NoiseGenerator noise;
        float humPhase = 0.0f;

        void process(float& left, float& right, float intensity);
    };

    // 1980s Processing - Early digital artifacts
    struct Vintage1980s
    {
        // Bit reduction
        float bitCrush(float input, int bits);

        // Sample rate reduction with ZOH
        struct SampleRateReducer
        {
            float lastSample = 0.0f;
            int counter = 0;
            int holdTime = 1;

            float process(float input);
            void setSampleRate(double hostRate, double targetRate);
        };

        // Aliasing artifacts
        struct AliasingGenerator
        {
            float process(float input);

        private:
            float lastIn = 0.0f;
            float lastOut = 0.0f;
        };

        // Early digital companding artifacts
        struct CompandingArtifacts
        {
            float muLawEncode(float input);
            float muLawDecode(float input);
            float process(float input, float amount);
        };

        SampleRateReducer decimatorL, decimatorR;
        AliasingGenerator aliasingL, aliasingR;
        CompandingArtifacts compander;

        void process(float& left, float& right, float intensity);
    };

    // Modern Processing - Clean with optional enhancement
    struct ModernProcessing
    {
        // Harmonic exciter for presence
        struct HarmonicExciter
        {
            float process(float input);
            void setFrequency(float freq);
            void prepare(const juce::dsp::ProcessSpec& spec);

            juce::dsp::StateVariableTPTFilter<float> highpass;
            float frequency = 8000.0f;
        };

        // Stereo width enhancer
        struct StereoEnhancer
        {
            void process(float& left, float& right, float width);
            void prepare(const juce::dsp::ProcessSpec& spec);

            juce::dsp::DelayLine<float> delayL{4800};
            juce::dsp::DelayLine<float> delayR{4800};
        };

        HarmonicExciter exciterL, exciterR;
        StereoEnhancer widener;

        void process(float& left, float& right, float intensity);
        void prepare(double sampleRate);
    };

    // Filtering for era-specific frequency response
    struct EraFilter
    {
        juce::dsp::StateVariableTPTFilter<float> lowpass;
        juce::dsp::StateVariableTPTFilter<float> highpass;
        juce::dsp::IIR::Filter<float> tiltEQ;

        void prepare(const juce::dsp::ProcessSpec& spec);
        void configure1970s(double sampleRate);
        void configure1980s(double sampleRate);
        void configureModern(double sampleRate);
        float process(float input);
    };

    // Processing components
    Vintage1970s vintage70s;
    Vintage1980s vintage80s;
    ModernProcessing modern;
    EraFilter filterL, filterR;

    // State variables
    ColorMode currentMode = Color1980s;
    float intensity = 0.5f;
    float noiseAmount = 0.0f;
    float artifactAmount = 0.5f;
    double sampleRate = 44100.0;

    // Random number generation
    std::mt19937 randomGen{std::random_device{}()};
    std::uniform_real_distribution<float> randomDist{-1.0f, 1.0f};

    // Helper functions
    float softClip(float input);
    float crossfade(float a, float b, float mix);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VintageColoration)
};