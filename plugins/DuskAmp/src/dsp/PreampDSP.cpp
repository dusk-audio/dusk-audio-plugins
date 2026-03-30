#include "PreampDSP.h"
#include <cmath>
#include <algorithm>

void PreampDSP::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;

    for (int i = 0; i < kMaxStages; ++i)
    {
        stages_[i].setTubeType (AnalogEmulation::TubeEmulation::TubeType::Triode_12AX7);
        stages_[i].prepare (sampleRate, 1); // mono
        interStageDC_[i].prepare (sampleRate, 10.0f);
    }

    updateCouplingCapCoeff();
    updateBrightCoeff();
    updateGainStaging();
    reset();
}

void PreampDSP::reset()
{
    for (int i = 0; i < kMaxStages; ++i)
    {
        stages_[i].reset();
        interStageDC_[i].reset();
        couplingCapState_[i] = 0.0f;
    }

    brightBoostState_ = 0.0f;
}

void PreampDSP::setGain (float gain01)
{
    gain_ = std::clamp (gain01, 0.0f, 1.0f);
    updateGainStaging();
}

void PreampDSP::setChannel (Channel ch)
{
    currentChannel_ = ch;

    switch (ch)
    {
        case Channel::Clean:  numActiveStages_ = 1; break;
        case Channel::Crunch: numActiveStages_ = 2; break;
        case Channel::Lead:   numActiveStages_ = 3; break;
    }

    updateGainStaging();
}

void PreampDSP::setBright (bool on)
{
    bright_ = on;
}

void PreampDSP::process (float* buffer, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        float sample = buffer[i];

        // Bright cap: mix in a highpass-filtered version before the first stage
        if (bright_)
        {
            float hpOut = sample - brightBoostState_;
            brightBoostState_ += hpOut * brightBoostCoeff_;
            // Mix treble boost in (add ~30% of the highpassed signal)
            sample += hpOut * 0.3f;
        }

        // Process through each active tube stage
        for (int stage = 0; stage < numActiveStages_; ++stage)
        {
            // Coupling cap highpass (removes DC, rolls off bass like a real amp)
            float hpOut = sample - couplingCapState_[stage];
            couplingCapState_[stage] += hpOut * (1.0f - couplingCapCoeff_);
            sample = hpOut;

            // Tube stage (mono, channel 0)
            sample = stages_[stage].processSample (sample, 0);

            // DC block between stages
            sample = interStageDC_[stage].processSample (sample);
        }

        buffer[i] = sample;
    }
}

void PreampDSP::updateGainStaging()
{
    // Distribute gain across active stages
    // Clean: single stage gets moderate drive
    // Crunch: two stages, first medium, second higher
    // Lead: three stages, cascaded gain builds up
    float drivePerStage = 0.0f;

    switch (currentChannel_)
    {
        case Channel::Clean:
            // Single stage: map gain 0-1 to drive 0-0.4
            drivePerStage = gain_ * 0.4f;
            stages_[0].setDrive (drivePerStage);
            break;

        case Channel::Crunch:
            // Two stages: first gets moderate drive, second gets more
            stages_[0].setDrive (gain_ * 0.3f);
            stages_[1].setDrive (gain_ * 0.6f);
            break;

        case Channel::Lead:
            // Three stages: progressive drive increase
            stages_[0].setDrive (gain_ * 0.25f);
            stages_[1].setDrive (gain_ * 0.5f);
            stages_[2].setDrive (gain_ * 0.8f);
            break;
    }
}

void PreampDSP::updateCouplingCapCoeff()
{
    // Coupling cap HPF: ~30Hz cutoff
    // coeff = exp(-2*pi*fc/fs)
    float fc = 30.0f;
    couplingCapCoeff_ = std::exp (-2.0f * 3.14159265359f * fc / static_cast<float> (sampleRate_));
}

void PreampDSP::updateBrightCoeff()
{
    // Bright cap HPF: ~1.5kHz cutoff for treble boost
    float fc = 1500.0f;
    float w = 2.0f * 3.14159265359f * fc / static_cast<float> (sampleRate_);
    brightBoostCoeff_ = w / (w + 1.0f);
}
