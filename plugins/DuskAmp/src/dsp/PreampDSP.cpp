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

        buffer[i] = sample * outputMakeup_;
    }
}

void PreampDSP::updateGainStaging()
{
    // Distribute gain across active stages.
    // Real preamp tubes have ~35-70x voltage gain per stage, but we're
    // working with normalized ±1 signals. The drive parameter controls
    // how hard each stage is hit, but we need to keep the overall output
    // reasonable so the power amp (and eventually the DAC) isn't clipped.
    //
    // At gain=0 (clean), the preamp should add minimal distortion (<5% THD).
    // At gain=1 (full), significant harmonic content is expected.
    //
    // Key: TubeEmulation::setDrive maps to inputGain = 1 + drive*2,
    // and outputScaling = 0.8 for 12AX7. So at drive=0, throughput ≈ 0.8x.
    // Multiple stages cascade this gain, so we need lower per-stage drive
    // values to keep the total chain under control.

    // Makeup gains chosen so post-preamp peak at moderate drive sits roughly at
    // Clean=0.35, Crunch=0.65, Lead=1.0 — giving Lead more headroom loss into
    // the waveshaper downstream, matching user expectation that higher-gain
    // channels sound both louder and more saturated.
    switch (currentChannel_)
    {
        case Channel::Clean:
            stages_[0].setDrive (gain_ * 0.25f);
            outputMakeup_ = 2.0f;
            break;

        case Channel::Crunch:
            stages_[0].setDrive (gain_ * 0.15f);
            stages_[1].setDrive (gain_ * 0.35f);
            outputMakeup_ = 4.0f;
            break;

        case Channel::Lead:
            stages_[0].setDrive (gain_ * 0.12f);
            stages_[1].setDrive (gain_ * 0.25f);
            stages_[2].setDrive (gain_ * 0.5f);
            outputMakeup_ = 8.0f;
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
