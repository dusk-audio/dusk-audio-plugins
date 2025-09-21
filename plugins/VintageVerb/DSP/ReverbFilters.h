/*
  ==============================================================================

    ReverbFilters.h - Professional filtering and EQ for reverb

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>

class ReverbFilters
{
public:
    ReverbFilters();

    void prepare(double sampleRate);
    void reset();

    // Input stage filters
    void setInputHighpass(float freq);
    void setInputLowpass(float freq);
    void setInputTilt(float gainDb);  // Spectral tilt -6dB to +6dB

    // Output stage filters
    void setOutputHighpass(float freq);
    void setOutputLowpass(float freq);
    void setOutputEQ(float lowGain, float midFreq, float midGain, float highGain);

    // Ducking/gating
    void setDuckingAmount(float amount);  // 0-1, reduces reverb when input present
    void setGateThreshold(float threshold);  // Kills reverb tail below threshold

    // Process filters
    void processInput(float& left, float& right);
    void processOutput(float& left, float& right);
    void updateDucking(float inputLevel);

    // Freeze mode (infinite reverb)
    void setFreeze(bool frozen);
    bool isFrozen() const { return freezeMode; }

private:
    double sampleRate = 44100.0;

    // Input filters
    juce::dsp::StateVariableTPTFilter<float> inputHighpassL, inputHighpassR;
    juce::dsp::StateVariableTPTFilter<float> inputLowpassL, inputLowpassR;

    // Tilt filter (shelf combination)
    juce::dsp::IIR::Filter<float> tiltLowShelfL, tiltLowShelfR;
    juce::dsp::IIR::Filter<float> tiltHighShelfL, tiltHighShelfR;

    // Output filters
    juce::dsp::StateVariableTPTFilter<float> outputHighpassL, outputHighpassR;
    juce::dsp::StateVariableTPTFilter<float> outputLowpassL, outputLowpassR;

    // 3-band output EQ
    juce::dsp::IIR::Filter<float> outputLowShelfL, outputLowShelfR;
    juce::dsp::IIR::Filter<float> outputMidBellL, outputMidBellR;
    juce::dsp::IIR::Filter<float> outputHighShelfL, outputHighShelfR;

    // Ducking
    float duckingAmount = 0.0f;
    float currentDuckGain = 1.0f;
    float targetDuckGain = 1.0f;
    juce::SmoothedValue<float> duckingSmoother;

    // Gate
    float gateThreshold = -60.0f;
    float currentGateGain = 1.0f;
    juce::SmoothedValue<float> gateSmoother;

    // Freeze
    bool freezeMode = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverbFilters)
};

//==============================================================================
// Shimmer effect processor (pitch shifting for ethereal sounds)
//==============================================================================
class ShimmerProcessor
{
public:
    ShimmerProcessor();

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    void setShimmerAmount(float amount);  // 0-1
    void setShimmerPitch(float semitones);  // Usually +12 (octave up)
    void setShimmerDecay(float decay);  // How quickly shimmer fades

    void process(float* left, float* right, int numSamples);

private:
    // Pitch shifter using granular synthesis
    class GranularPitchShifter
    {
    public:
        void prepare(double sampleRate);
        float process(float input, float pitchRatio);

    private:
        static constexpr int GRAIN_SIZE = 2048;
        static constexpr int NUM_GRAINS = 4;

        std::array<std::vector<float>, NUM_GRAINS> grainBuffers;
        std::array<int, NUM_GRAINS> grainPositions;
        std::array<float, NUM_GRAINS> grainPhases;

        float crossfadeWindow[GRAIN_SIZE];
        double sampleRate = 44100.0;

        void initializeWindow();
    };

    GranularPitchShifter pitchShifterL, pitchShifterR;

    // Feedback delay for shimmer tail
    juce::dsp::DelayLine<float> shimmerDelayL{192000};
    juce::dsp::DelayLine<float> shimmerDelayR{192000};

    // Parameters
    float shimmerAmount = 0.0f;
    float shimmerPitch = 12.0f;  // Semitones
    float shimmerDecay = 0.5f;

    // Filters for shimmer
    juce::dsp::StateVariableTPTFilter<float> shimmerHighpassL, shimmerHighpassR;

    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShimmerProcessor)
};

//==============================================================================
// Envelope follower for dynamic control
//==============================================================================
class EnvelopeFollower
{
public:
    EnvelopeFollower();

    void setSampleRate(double sampleRate);
    void setAttack(float ms);
    void setRelease(float ms);

    float process(float input);
    float getEnvelope() const { return currentEnvelope; }

private:
    double sampleRate = 44100.0;
    float attackCoeff = 0.001f;
    float releaseCoeff = 0.001f;
    float currentEnvelope = 0.0f;
};

//==============================================================================
// Compression for reverb density control
//==============================================================================
class ReverbCompressor
{
public:
    ReverbCompressor();

    void prepare(double sampleRate);
    void reset();

    void setThreshold(float dB);
    void setRatio(float ratio);
    void setAttack(float ms);
    void setRelease(float ms);
    void setKnee(float dB);

    float processSample(float input);

private:
    juce::dsp::Compressor<float> compressor;
    double sampleRate = 44100.0;
};