#pragma once

#include "ReverbProcessor.h"
#include <memory>
#include <array>

/**
 * Hall Reverb Processor
 * Implements a large concert hall reverb using cascaded allpass and comb filters
 * Based on Dragonfly Hall algorithm
 */
class HallReverbProcessor : public ReverbProcessor
{
public:
    HallReverbProcessor();
    ~HallReverbProcessor() override;

    void prepare(double sampleRate, int blockSize) override;
    void reset() override;
    void process(float* leftChannel, float* rightChannel, int numSamples) override;
    double getTailLength() const override { return 5.0; } // 5 second tail

    ParameterVisibility getParameterVisibility() const override
    {
        ParameterVisibility vis;
        vis.showDecay = true;
        vis.showPreDelay = true;
        vis.showDamping = true;
        vis.showDiffusion = true;
        vis.showRoomSize = true;
        vis.showModulation = true;
        vis.showEarlyMix = false;
        vis.showLateMix = false;
        vis.showLowCut = true;
        vis.showHighCut = true;
        return vis;
    }

    const char* getTypeName() const override { return "Hall Reverb"; }

private:
    // Allpass filter for diffusion
    class AllpassFilter
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

    // Comb filter for reverb tail
    class CombFilter
    {
    public:
        void setSize(int size);
        void setDamp(float d) { damp1 = d; damp2 = 1.0f - d; }
        void setFeedback(float fb) { feedback = fb; }
        float process(float input);
        void clear();

    private:
        std::vector<float> buffer;
        int bufferSize = 0;
        int bufferIndex = 0;
        float feedback = 0.8f;
        float filterStore = 0.0f;
        float damp1 = 0.5f;
        float damp2 = 0.5f;
    };

    // Pre-delay line
    class DelayLine
    {
    public:
        void setDelay(int samples);
        float process(float input);
        void clear();

    private:
        std::vector<float> buffer;
        int bufferSize = 0;
        int writeIndex = 0;
        int readIndex = 0;
    };

    // Filter coefficients for Hall reverb
    static constexpr int NUM_COMBS = 8;
    static constexpr int NUM_ALLPASSES = 4;

    // Tuning values (in samples at 44100 Hz)
    static constexpr int COMB_TUNING[NUM_COMBS] = {
        1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116
    };

    static constexpr int ALLPASS_TUNING[NUM_ALLPASSES] = {
        556, 441, 341, 225
    };

    // Processing components
    std::array<CombFilter, NUM_COMBS> combFiltersL;
    std::array<CombFilter, NUM_COMBS> combFiltersR;
    std::array<AllpassFilter, NUM_ALLPASSES> allpassFiltersL;
    std::array<AllpassFilter, NUM_ALLPASSES> allpassFiltersR;

    DelayLine preDelayL;
    DelayLine preDelayR;

    // High/low shelf filters
    juce::dsp::IIR::Filter<float> lowShelfL, lowShelfR;
    juce::dsp::IIR::Filter<float> highShelfL, highShelfR;

    // Modulation LFOs
    float modPhase = 0.0f;
    float modRate = 0.5f; // Hz

    // Update filter parameters
    void updateFilters();

    // Stereo spreading
    static constexpr int STEREO_SPREAD = 23;
};