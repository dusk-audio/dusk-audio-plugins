#include "PreampChain.h"
#include <algorithm>

void PreampChain::configure (const PreampChainConfig& config)
{
    config_ = config;
    numActiveStages_ = std::clamp (config_.numStages, 1, kMaxGainStages);

    for (int i = 0; i < numActiveStages_; ++i)
        stages_[static_cast<size_t> (i)].configure (config_.stages[i]);
}

void PreampChain::prepare (double sampleRate)
{
    for (int i = 0; i < numActiveStages_; ++i)
        stages_[static_cast<size_t> (i)].prepare (sampleRate);
}

void PreampChain::reset()
{
    for (int i = 0; i < numActiveStages_; ++i)
        stages_[static_cast<size_t> (i)].reset();
}

void PreampChain::setGain (float gain01)
{
    gain_ = std::clamp (gain01, 0.0f, 1.0f);

    // Map gain knob through the model's gain range
    float mappedGain = config_.masterGainMin + gain_ * (config_.masterGainMax - config_.masterGainMin);

    for (int i = 0; i < numActiveStages_; ++i)
        stages_[static_cast<size_t> (i)].setGain (mappedGain);
}

void PreampChain::setBright (bool on)
{
    bright_ = on && config_.hasBrightSwitch;

    for (int i = 0; i < numActiveStages_; ++i)
        stages_[static_cast<size_t> (i)].setBright (bright_);
}

void PreampChain::process (float* buffer, int numSamples)
{
    const float outGain = config_.outputGain;

    for (int i = 0; i < numSamples; ++i)
    {
        float sample = buffer[i];

        for (int stage = 0; stage < numActiveStages_; ++stage)
            sample = stages_[static_cast<size_t> (stage)].processSample (sample);

        buffer[i] = sample * outGain;
    }
}
