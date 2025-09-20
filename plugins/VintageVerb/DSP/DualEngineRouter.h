/*
  ==============================================================================

    DualEngineRouter.h - Routing and mixing for dual reverb engines

    Provides flexible routing configurations inspired by high-end reverbs:
    - Series: Engine A -> Engine B
    - Parallel: Engine A + Engine B
    - A to B: A processed by B
    - B to A: B processed by A

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "ReverbEngine.h"

class DualEngineRouter
{
public:
    DualEngineRouter();
    ~DualEngineRouter() = default;

    // Routing modes
    enum RoutingMode
    {
        Series = 0,      // A -> B (classic cascade)
        Parallel,        // A + B (parallel processing)
        AtoB,           // A fed into B's modulation
        BtoA,           // B fed into A's modulation
        Nested,         // A inside B's feedback loop
        CrossFeed,      // Cross-coupled feedback
        NumModes
    };

    // Preparation
    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    // Set the reverb engines to route
    void setEngines(ReverbEngine* engineA, ReverbEngine* engineB);

    // Processing
    void process(juce::AudioBuffer<float>& buffer, int numSamples);
    void processStereo(float* leftIn, float* rightIn,
                      float* leftOut, float* rightOut, int numSamples);

    // Parameters
    void setRoutingMode(RoutingMode mode);
    void setEngineMix(float mix);          // 0.0 = all A, 1.0 = all B
    void setCrossFeedAmount(float amount); // For cross-feed modes
    void setSeriesBlend(float blend);      // Blend amount for series mode
    void setWidth(float width);            // Stereo width control

    // Get current routing info
    RoutingMode getCurrentMode() const { return currentMode; }
    float getEngineMix() const { return engineMix; }

private:
    // Routing implementations
    void processSeries(float* leftIn, float* rightIn,
                      float* leftOut, float* rightOut, int numSamples);

    void processParallel(float* leftIn, float* rightIn,
                        float* leftOut, float* rightOut, int numSamples);

    void processAtoB(float* leftIn, float* rightIn,
                    float* leftOut, float* rightOut, int numSamples);

    void processBtoA(float* leftIn, float* rightIn,
                    float* leftOut, float* rightOut, int numSamples);

    void processNested(float* leftIn, float* rightIn,
                      float* leftOut, float* rightOut, int numSamples);

    void processCrossFeed(float* leftIn, float* rightIn,
                         float* leftOut, float* rightOut, int numSamples);

    // Internal buffers for routing
    juce::AudioBuffer<float> bufferA;
    juce::AudioBuffer<float> bufferB;
    juce::AudioBuffer<float> tempBuffer;
    juce::AudioBuffer<float> feedbackBuffer;

    // Cross-feed delay lines for complex routing
    juce::dsp::DelayLine<float> crossDelayL{192000};
    juce::dsp::DelayLine<float> crossDelayR{192000};

    // Modulation matrix for cross-coupling
    struct ModulationMatrix
    {
        float process(float inputA, float inputB, float modAmount);
        void reset();

        float lfo1Phase = 0.0f;
        float lfo2Phase = 0.0f;
        float sampleRate = 44100.0f;
    };

    ModulationMatrix modMatrix;

    // Stereo width processor
    struct StereoProcessor
    {
        void process(float& left, float& right, float width);
        void prepare(const juce::dsp::ProcessSpec& spec);
        void reset();

        juce::dsp::DelayLine<float> delayL{4800};
        juce::dsp::DelayLine<float> delayR{4800};
        float lastL = 0.0f;
        float lastR = 0.0f;
    };

    StereoProcessor stereoProcessor;

    // Smooth parameter changes
    juce::SmoothedValue<float> smoothEngineMix;
    juce::SmoothedValue<float> smoothCrossFeed;
    juce::SmoothedValue<float> smoothSeriesBlend;
    juce::SmoothedValue<float> smoothWidth;

    // State variables
    ReverbEngine* engineA = nullptr;
    ReverbEngine* engineB = nullptr;
    RoutingMode currentMode = Parallel;
    float engineMix = 0.5f;
    float crossFeedAmount = 0.0f;
    float seriesBlend = 0.5f;
    float stereoWidth = 1.0f;
    double sampleRate = 44100.0;
    int blockSize = 512;

    // Helper functions
    float softClip(float input);
    void applyGainCompensation(float& sample, RoutingMode mode);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DualEngineRouter)
};