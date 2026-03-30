#pragma once

#include "AnalogEmulation/TubeEmulation.h"
#include "AnalogEmulation/DCBlocker.h"

class PreampDSP
{
public:
    enum class Channel { Clean = 0, Crunch = 1, Lead = 2 };

    void prepare (double sampleRate);
    void reset();
    void setGain (float gain01);
    void setChannel (Channel ch);
    void setBright (bool on);
    void process (float* buffer, int numSamples);

private:
    // 3 cascaded 12AX7 stages (Lead uses all 3, Crunch uses 2, Clean uses 1)
    static constexpr int kMaxStages = 3;
    AnalogEmulation::TubeEmulation stages_[kMaxStages];
    AnalogEmulation::DCBlocker interStageDC_[kMaxStages];

    Channel currentChannel_ = Channel::Crunch;
    float gain_ = 0.5f;
    bool bright_ = false;
    int numActiveStages_ = 2;
    double sampleRate_ = 44100.0;

    // Coupling cap HPF between stages (simple one-pole)
    float couplingCapState_[kMaxStages] = {};
    float couplingCapCoeff_ = 0.995f;

    // Bright cap boost (treble boost when bright switch on)
    float brightBoostState_ = 0.0f;
    float brightBoostCoeff_ = 0.0f;

    void updateGainStaging();
    void updateCouplingCapCoeff();
    void updateBrightCoeff();
};
