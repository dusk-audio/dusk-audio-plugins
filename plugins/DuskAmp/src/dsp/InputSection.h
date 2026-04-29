// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cmath>
#include <algorithm>

class InputSection
{
public:
    void prepare (double sampleRate)
    {
        sampleRate_ = sampleRate;

        // Gate attack: ~1ms
        gateAttackCoeff_ = std::exp (-1000.0f / (1.0f * static_cast<float> (sampleRate)));
        updateGateReleaseCoeff();

        reset();
    }

    void reset()
    {
        gateEnvelope_ = 0.0f;
        gateOpen_ = false;
    }

    void setInputGain (float dB)
    {
        if (dB == prevInputGainDB_) return;
        prevInputGainDB_ = dB;
        inputGain_ = std::pow (10.0f, dB / 20.0f);
    }

    void setGateThreshold (float dB)
    {
        if (dB == prevGateThreshDB_) return;
        prevGateThreshDB_ = dB;
        gateThresholdLinear_ = std::pow (10.0f, dB / 20.0f);
    }

    void setGateRelease (float ms)
    {
        if (ms == gateRelease_) return;
        gateRelease_ = ms;
        updateGateReleaseCoeff();
    }

    void process (float* buffer, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            // Apply input gain
            float sample = buffer[i] * inputGain_;

            // Envelope follower for gate
            float absVal = std::abs (sample);

            if (absVal > gateEnvelope_)
                gateEnvelope_ = gateAttackCoeff_ * gateEnvelope_ + (1.0f - gateAttackCoeff_) * absVal;
            else
                gateEnvelope_ = gateReleaseCoeff_ * gateEnvelope_;

            // Gate logic: open above threshold, close below with hysteresis
            if (gateEnvelope_ > gateThresholdLinear_)
                gateOpen_ = true;
            else if (gateEnvelope_ < gateThresholdLinear_ * 0.5f)
                gateOpen_ = false;

            if (! gateOpen_)
            {
                // Smooth fade to zero using envelope as attenuation
                float gateGain = gateEnvelope_ / std::max (gateThresholdLinear_, 1e-10f);
                gateGain = std::min (gateGain, 1.0f);
                sample *= gateGain;
            }

            buffer[i] = sample;
        }
    }

private:
    float inputGain_ = 1.0f;
    float prevInputGainDB_ = -999.0f;
    float gateThresholdLinear_ = 0.001f; // -60dB
    float prevGateThreshDB_ = -999.0f;
    float gateRelease_ = 50.0f;
    float gateEnvelope_ = 0.0f;
    float gateAttackCoeff_ = 0.0f;
    float gateReleaseCoeff_ = 0.0f;
    double sampleRate_ = 44100.0;
    bool gateOpen_ = false;

    void updateGateReleaseCoeff()
    {
        if (sampleRate_ > 0.0 && gateRelease_ > 0.0f)
            gateReleaseCoeff_ = std::exp (-1000.0f / (gateRelease_ * static_cast<float> (sampleRate_)));
        else
            gateReleaseCoeff_ = 0.999f;
    }
};
