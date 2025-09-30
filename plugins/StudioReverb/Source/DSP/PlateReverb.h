#pragma once

#include "ReverbProcessor.h"
#include <array>

/**
 * Plate Reverb Processor
 * Implements a vintage plate reverb emulation
 * Based on Dragonfly Plate algorithm using a lattice network
 */
class PlateReverbProcessor : public ReverbProcessor
{
public:
    PlateReverbProcessor();
    ~PlateReverbProcessor() override;

    void prepare(double sampleRate, int blockSize) override;
    void reset() override;
    void process(float* leftChannel, float* rightChannel, int numSamples) override;
    double getTailLength() const override { return 4.0; } // 4 second tail

    ParameterVisibility getParameterVisibility() const override
    {
        ParameterVisibility vis;
        vis.showDecay = true;
        vis.showPreDelay = true;
        vis.showDamping = true;
        vis.showDiffusion = true;
        vis.showRoomSize = false;  // Not applicable for plate
        vis.showModulation = true;
        vis.showEarlyMix = false;
        vis.showLateMix = false;
        vis.showLowCut = true;
        vis.showHighCut = true;
        return vis;
    }

    const char* getTypeName() const override { return "Plate Reverb"; }

private:
    // Lattice allpass filter for plate simulation
    class LatticeAllpass
    {
    public:
        void setDelay(int samples);
        void setFeedback(float fb) { feedback = fb; }
        void setDecay(float d) { decayFactor = d; }
        float process(float input);
        void clear();
        void modulate(float amount);

    private:
        std::vector<float> buffer;
        int bufferSize = 0;
        int writeIndex = 0;
        int readIndex = 0;
        float feedback = 0.5f;
        float decayFactor = 0.95f;
        float z1 = 0.0f; // State for interpolation
    };

    // One-pole lowpass filter for damping
    class OnePole
    {
    public:
        void setCutoff(float freq, float sampleRate);
        float process(float input);
        void clear() { state = 0.0f; }

    private:
        float a0 = 1.0f;
        float b1 = 0.0f;
        float state = 0.0f;
    };

    // Pre-delay
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
    };

    // Plate network configuration
    static constexpr int NUM_LATTICE_STAGES = 4;
    static constexpr int NUM_PARALLEL_PLATES = 2;

    // Delay times for lattice stages (in samples at 44100Hz)
    static constexpr int LATTICE_DELAYS[NUM_LATTICE_STAGES] = {
        142, 107, 379, 277
    };

    // Input diffusion
    std::array<LatticeAllpass, 2> inputDiffusionL;
    std::array<LatticeAllpass, 2> inputDiffusionR;

    // Main plate network (parallel lattice structures)
    std::array<std::array<LatticeAllpass, NUM_LATTICE_STAGES>, NUM_PARALLEL_PLATES> plateNetworkL;
    std::array<std::array<LatticeAllpass, NUM_LATTICE_STAGES>, NUM_PARALLEL_PLATES> plateNetworkR;

    // Pre-delay
    DelayLine preDelayL;
    DelayLine preDelayR;

    // Damping filters
    std::array<OnePole, NUM_PARALLEL_PLATES> dampingFiltersL;
    std::array<OnePole, NUM_PARALLEL_PLATES> dampingFiltersR;

    // Input/output filters
    juce::dsp::IIR::Filter<float> inputHighpassL, inputHighpassR;
    juce::dsp::IIR::Filter<float> outputLowpassL, outputLowpassR;

    // Modulation
    float modPhase = 0.0f;
    float modRate = 1.0f; // Hz
    std::array<float, NUM_LATTICE_STAGES> modPhaseOffsets;

    // Update filter parameters
    void updateFilters();
    void updateModulation();
};