#pragma once

#include "DspUtils.h"

#include <vector>

// Single modulated allpass filter with circular buffer and cubic interpolation.
// Uses the Schroeder allpass topology: H(z) = (z^-D - g) / (1 - g*z^-D)
class ModulatedAllpass
{
public:
    void prepare (int bufferSize, float delayInSamples, float lfoRateHz,
                  float lfoDepthSamples, float lfoStartPhase, double sampleRate);
    float process (float input, float g);
    void clear();

private:
    std::vector<float> buffer_;
    int writePos_ = 0;
    int mask_ = 0;
    float delaySamples_ = 0.0f;
    float lfoPhase_ = 0.0f;
    float lfoPhaseInc_ = 0.0f;
    float lfoDepth_ = 0.0f;

    static constexpr float kTwoPi = 6.283185307179586f;
};

// Cascaded modulated allpass input diffuser (4 stages per channel, stereo).
// Smears transients into a dense wash before the FDN.
class DiffusionStage
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void process (float* left, float* right, int numSamples);
    void setDiffusion (float amount);
    void setMaxCoefficients (float max12, float max34);

private:
    static constexpr int kNumStages = 4;
    static constexpr int kBaseDelays[kNumStages] = { 142, 107, 379, 277 };

    ModulatedAllpass leftAP_[kNumStages];
    ModulatedAllpass rightAP_[kNumStages];

    float diffusionCoeff12_ = 0.45f;  // Stages 1-2: higher diffusion (Dattorro: max 0.75)
    float diffusionCoeff34_ = 0.375f; // Stages 3-4: lower for transient clarity (Dattorro: max 0.625)
    float maxCoeff12_ = 0.75f;
    float maxCoeff34_ = 0.625f;
    float lastDiffusionAmount_ = 0.6f;
};
