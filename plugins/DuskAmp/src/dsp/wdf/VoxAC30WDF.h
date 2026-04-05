// VoxAC30WDF.h — Vox AC30 Top Boost preamp circuit simulation.
//
// Topology: V1 (12AX7) → volume → V2 (12AX7, 220k plate load)
//
// Component values from the Top Boost schematic:
//   V1: Rp=220k, Rk=1.5k, Ck=25uF, Rg=1M, Cin=0.047uF
//   Volume: 1M audio taper
//   V2: Rp=220k, Rk=1.5k, Ck=25uF, Cin=0.047uF
//   B+ = 275V
//   No global NFB

#pragma once

#include "WDFPreamp.h"
#include "TriodeWDF.h"

class VoxAC30WDF : public WDFPreamp
{
public:
    VoxAC30WDF();

    void prepare (double sampleRate) override;
    void reset() override;
    void setGain (float gain01) override;
    void setBright (bool on) override;
    void process (float* buffer, int numSamples) override;

private:
    DuskAmpWDF::TriodeStageWDF v1_;    // First preamp stage
    DuskAmpWDF::TriodeStageWDF v2_;    // Second preamp stage (220k Rp)

    float volumeAtten_ = 0.5f;

    static constexpr float kInputScale = 100.0f;
    static constexpr float kOutputScale = 1.0f;

    double sampleRate_ = 48000.0;
};
