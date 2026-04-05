// MarshallPlexiWDF.h — Marshall 1959/JTM45 Plexi preamp circuit simulation.
//
// Topology: V1A (12AX7) → volume pot + bright cap → V1B (12AX7)
//
// Component values from the 1959 schematic:
//   V1A: Rp=100k, Rk=820Ω, Ck=0.68uF (partially bypassed ~285Hz), Rg=1M, Cin=0.022uF
//   Volume: 1M audio taper, Cbright=120pF in parallel
//   V1B: same tube circuit, Cin=0.022uF (inter-stage coupling)
//   B+ = 330V

#pragma once

#include "WDFPreamp.h"
#include "TriodeWDF.h"

class MarshallPlexiWDF : public WDFPreamp
{
public:
    MarshallPlexiWDF();

    void prepare (double sampleRate) override;
    void reset() override;
    void setGain (float gain01) override;
    void setBright (bool on) override;
    void process (float* buffer, int numSamples) override;

private:
    DuskAmpWDF::TriodeStageWDF v1a_;   // First preamp stage
    DuskAmpWDF::TriodeStageWDF v1b_;   // Second preamp stage

    // Volume pot between stages (variable attenuation, audio taper)
    float volumeAtten_ = 0.5f;      // 0 = full cut, 1 = full open

    // Bright cap: 120pF across volume pot (treble bypass at low volume)
    bool brightEnabled_ = false;
    float brightCapState_ = 0.0f;
    float brightCapCoeff_ = 0.0f;

    // Input/output scaling
    static constexpr float kInputScale = 100.0f;  // Must drive grid hard enough to overdrive tube
    static constexpr float kOutputScale = 1.0f;

    double sampleRate_ = 48000.0;

    void updateBrightCap();
};
