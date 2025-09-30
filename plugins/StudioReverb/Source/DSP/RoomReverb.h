#pragma once

#include "ReverbProcessor.h"
#include <array>
#include <memory>

/**
 * Room Reverb Processor
 * Implements a small to medium room reverb with early reflections
 * Based on Dragonfly Room algorithm
 */
class RoomReverbProcessor : public ReverbProcessor
{
public:
    RoomReverbProcessor();
    ~RoomReverbProcessor() override;

    void prepare(double sampleRate, int blockSize) override;
    void reset() override;
    void process(float* leftChannel, float* rightChannel, int numSamples) override;
    double getTailLength() const override { return 2.0; } // 2 second tail

    ParameterVisibility getParameterVisibility() const override
    {
        ParameterVisibility vis;
        vis.showDecay = true;
        vis.showPreDelay = true;
        vis.showDamping = true;
        vis.showDiffusion = true;
        vis.showRoomSize = true;
        vis.showModulation = false;
        vis.showEarlyMix = true;
        vis.showLateMix = true;
        vis.showLowCut = true;
        vis.showHighCut = true;
        return vis;
    }

    const char* getTypeName() const override { return "Room Reverb"; }

private:
    // Early reflections tap delays (in milliseconds)
    static constexpr int NUM_EARLY_TAPS = 8;
    static constexpr float EARLY_TAP_DELAYS[NUM_EARLY_TAPS] = {
        4.3f, 21.5f, 35.8f, 56.7f, 68.9f, 78.2f, 91.4f, 106.5f
    };
    static constexpr float EARLY_TAP_GAINS[NUM_EARLY_TAPS] = {
        0.841f, 0.504f, 0.393f, 0.325f, 0.286f, 0.227f, 0.182f, 0.140f
    };

    // Simple delay line class
    class SimpleDelay
    {
    public:
        void setMaxDelay(int maxSamples);
        void setDelay(int samples);
        float process(float input);
        float tap(int delaySamples) const;
        void clear();

    private:
        std::vector<float> buffer;
        int bufferSize = 0;
        int writeIndex = 0;
        int currentDelay = 0;
    };

    // Diffusion allpass
    class DiffusionAllpass
    {
    public:
        void setSize(int size);
        void setFeedback(float fb) { feedback = fb; }
        float process(float input);
        void clear();

    private:
        std::vector<float> buffer;
        int bufferSize = 0;
        int bufferIndex = 0;
        float feedback = 0.5f;
    };

    // Processing components
    SimpleDelay preDelayL, preDelayR;
    SimpleDelay earlyReflectionsL, earlyReflectionsR;

    // Diffusion network (4 allpass filters in series)
    static constexpr int NUM_DIFFUSERS = 4;
    std::array<DiffusionAllpass, NUM_DIFFUSERS> diffusersL;
    std::array<DiffusionAllpass, NUM_DIFFUSERS> diffusersR;

    // Late reverb network (simplified FDN - Feedback Delay Network)
    static constexpr int NUM_DELAYS = 4;
    std::array<SimpleDelay, NUM_DELAYS> lateDelaysL;
    std::array<SimpleDelay, NUM_DELAYS> lateDelaysR;

    // Delay times for late reverb (in samples at 44100Hz)
    static constexpr int LATE_DELAY_TIMES[NUM_DELAYS] = {
        341, 613, 899, 1187
    };

    // Feedback matrix for FDN (Hadamard matrix)
    static constexpr float FDN_MATRIX[NUM_DELAYS][NUM_DELAYS] = {
        { 0.5f,  0.5f,  0.5f,  0.5f},
        { 0.5f, -0.5f,  0.5f, -0.5f},
        { 0.5f,  0.5f, -0.5f, -0.5f},
        { 0.5f, -0.5f, -0.5f,  0.5f}
    };

    // Filters
    juce::dsp::IIR::Filter<float> lowCutFilterL, lowCutFilterR;
    juce::dsp::IIR::Filter<float> highCutFilterL, highCutFilterR;

    // Damping filters for late reverb
    std::array<juce::dsp::IIR::Filter<float>, NUM_DELAYS> dampingFiltersL;
    std::array<juce::dsp::IIR::Filter<float>, NUM_DELAYS> dampingFiltersR;

    // Working buffers
    std::array<float, NUM_DELAYS> delayOutputsL;
    std::array<float, NUM_DELAYS> delayOutputsR;

    void updateFilters();
    void processEarlyReflections(float inputL, float inputR, float& outputL, float& outputR);
    void processLateReverb(float inputL, float inputR, float& outputL, float& outputR);
};