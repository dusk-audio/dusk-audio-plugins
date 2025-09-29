#pragma once

#include "ReverbProcessor.h"
#include <array>

/**
 * Early Reflections Processor
 * Implements only the early reflections stage without late reverb
 * Useful for adding space without long reverb tails
 */
class EarlyReflectionsProcessor : public ReverbProcessor
{
public:
    EarlyReflectionsProcessor();
    ~EarlyReflectionsProcessor() override;

    void prepare(double sampleRate, int blockSize) override;
    void reset() override;
    void process(float* leftChannel, float* rightChannel, int numSamples) override;
    double getTailLength() const override { return 0.2; } // 200ms tail

    ParameterVisibility getParameterVisibility() const override
    {
        ParameterVisibility vis;
        vis.showDecay = false;      // Not applicable
        vis.showPreDelay = true;
        vis.showDamping = false;    // Not applicable
        vis.showDiffusion = true;
        vis.showRoomSize = true;
        vis.showModulation = false;
        vis.showEarlyMix = false;   // Always 100%
        vis.showLateMix = false;    // No late reverb
        vis.showLowCut = true;
        vis.showHighCut = true;
        return vis;
    }

    const char* getTypeName() const override { return "Early Reflections"; }

private:
    // Reflection pattern (simulates room geometry)
    static constexpr int NUM_REFLECTIONS = 16;

    struct Reflection
    {
        float delay;    // in milliseconds
        float gainL;    // left channel gain
        float gainR;    // right channel gain
        float panAngle; // stereo position
    };

    // Reflection patterns for different room sizes
    static const Reflection SMALL_ROOM_PATTERN[NUM_REFLECTIONS];
    static const Reflection MEDIUM_ROOM_PATTERN[NUM_REFLECTIONS];
    static const Reflection LARGE_ROOM_PATTERN[NUM_REFLECTIONS];

    // Multi-tap delay for reflections
    class MultiTapDelay
    {
    public:
        void setMaxDelay(int maxSamples);
        void addTap(int delaySamples, float gain);
        void clearTaps();
        float process(float input);
        void clear();

    private:
        struct Tap
        {
            int delay;
            float gain;
        };

        std::vector<float> buffer;
        std::vector<Tap> taps;
        int bufferSize = 0;
        int writeIndex = 0;
    };

    // Diffusion allpass filters
    class AllpassDiffuser
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
    MultiTapDelay earlyTapsL;
    MultiTapDelay earlyTapsR;

    // Diffusion network
    static constexpr int NUM_DIFFUSERS = 2;
    std::array<AllpassDiffuser, NUM_DIFFUSERS> diffusersL;
    std::array<AllpassDiffuser, NUM_DIFFUSERS> diffusersR;

    // Pre-delay
    std::vector<float> preDelayBufferL;
    std::vector<float> preDelayBufferR;
    int preDelayWriteIndex = 0;
    int preDelayReadIndex = 0;
    int preDelayBufferSize = 0;

    // Filters
    juce::dsp::IIR::Filter<float> inputFilterL, inputFilterR;
    juce::dsp::IIR::Filter<float> outputFilterL, outputFilterR;

    // Update reflection pattern based on room size
    void updateReflectionPattern();
    void updateFilters();

    // Current reflection pattern
    const Reflection* currentPattern = MEDIUM_ROOM_PATTERN;
};