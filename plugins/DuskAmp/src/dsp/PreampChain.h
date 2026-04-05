#pragma once

#include "GainStage.h"
#include <array>

static constexpr int kMaxGainStages = 5;

// Complete preamp configuration: array of gain stages that define an amp model's preamp
struct PreampChainConfig
{
    const char* name = "Default";
    int numStages = 1;
    GainStageConfig stages[kMaxGainStages] = {};

    // Global gain range for this amp model
    float masterGainMin = 0.0f;    // Minimum drive when gain knob = 0
    float masterGainMax = 1.0f;    // Maximum drive when gain knob = 1
    bool hasBrightSwitch = true;

    // Output gain applied after the preamp chain (linear multiplier).
    // Models the real voltage gain of the tube stages that the normalized
    // transfer function doesn't capture.  Calibrate per amp model.
    float outputGain = 1.0f;
};

class PreampChain
{
public:
    void configure (const PreampChainConfig& config);
    void prepare (double sampleRate);
    void reset();
    void process (float* buffer, int numSamples);
    void setGain (float gain01);
    void setBright (bool on);

    const PreampChainConfig& getConfig() const { return config_; }

private:
    PreampChainConfig config_;
    std::array<GainStage, kMaxGainStages> stages_;
    int numActiveStages_ = 0;
    float gain_ = 0.5f;
    bool bright_ = false;
};
