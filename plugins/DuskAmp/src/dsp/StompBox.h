// SPDX-License-Identifier: GPL-3.0-or-later

// StompBox.h — Tube Screamer-style boost/overdrive pedal
//
// Signal chain: Input gain → bandpass "mid hump" (720Hz) → asymmetric
// diode clipper → tone control (variable hi-cut) → output level.
//
// Nearly every guitarist uses a boost or TS-style pedal in front of their
// amp for tighter low end, mid-forward character, and compressed sustain.

#pragma once

#include "AnalogEmulation/DCBlocker.h"
#include <cmath>

class StompBox
{
public:
    void prepare (double sampleRate);
    void reset();

    void setEnabled (bool on)      { enabled_ = on; }
    void setGain (float gain01);   // Drive amount (0-1)
    void setTone (float tone01);   // Hi-cut tone control (0=dark, 1=bright)
    void setLevel (float level01); // Output level (0-1)

    void process (float* buffer, int numSamples);

    bool isEnabled() const { return enabled_; }

private:
    bool enabled_ = false;
    double sampleRate_ = 44100.0;

    float gain_ = 0.5f;
    float driveGain_ = 1.0f;
    float tone_ = 0.5f;
    float level_ = 0.5f;
    float outputGain_ = 0.5f;

    // Mid-hump bandpass: 2nd-order BPF at 720Hz (Q=0.7)
    // This is THE Tube Screamer character — it cuts bass and extreme highs,
    // pushing the midrange into the amp's sweet spot.
    struct Biquad
    {
        float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;

        float process (float x)
        {
            float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            if (std::abs (z1) < 1e-15f) z1 = 0.0f;
            if (std::abs (z2) < 1e-15f) z2 = 0.0f;
            return y;
        }

        void reset() { z1 = z2 = 0.0f; }
    };

    Biquad midHump_;      // Bandpass at 720Hz

    // One-pole LPF for tone control (simpler, fewer artifacts)
    float toneLPState_ = 0.0f;
    float toneLPCoeff_ = 0.5f;

    AnalogEmulation::DCBlocker dcBlocker_;

    void updateMidHump();
    void updateToneCoeff();
    void updateGains();
};
