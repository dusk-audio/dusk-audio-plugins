#pragma once

#include "DiffusionStage.h" // reuses ModulatedAllpass

// Post-FDN output diffusion: 2 cascaded allpass filters per channel.
// Lower coefficient than input diffusion to add density without smearing stereo image.
class OutputDiffusion
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void process (float* left, float* right, int numSamples);
    void setDiffusion (float amount);

private:
    static constexpr int kNumStages = 2;
    static constexpr int kBaseDelays[kNumStages] = { 523, 163 };

    ModulatedAllpass leftAP_[kNumStages];
    ModulatedAllpass rightAP_[kNumStages];

    float diffusionCoeff_ = 0.4f;
};
