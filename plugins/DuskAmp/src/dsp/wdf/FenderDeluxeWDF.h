// FenderDeluxeWDF.h — Fender Deluxe Reverb 65 (AB763) preamp circuit simulation.
//
// Topology: V1A (12AX7) → volume pot → cathode follower (soft tanh compression)
//
// Component values from the AB763 schematic:
//   V1A: Rp=100k, Rk=1.5k, Ck=25uF (fully bypassed), Rg=1M, Cin=0.047uF
//   Volume: 1M audio taper
//   Cathode follower modeled as soft tanh compressor (unity gain buffer)
//   B+ = 420V

#pragma once

#include "WDFPreamp.h"
#include "TriodeWDF.h"

class FenderDeluxeWDF : public WDFPreamp
{
public:
    FenderDeluxeWDF();

    void prepare (double sampleRate) override;
    void reset() override;
    void setGain (float gain01) override;
    void setBright (bool on) override;
    void process (float* buffer, int numSamples) override;

private:
    DuskAmpWDF::TriodeStageWDF v1a_;   // Preamp stage

    float volumeAtten_ = 0.5f;

    // Cathode follower: simple soft compression (unity gain buffer)
    float cfEnvelope_ = 0.0f;

    static constexpr float kInputScale = 100.0f;
    static constexpr float kOutputScale = 1.0f;

    double sampleRate_ = 48000.0;
};
