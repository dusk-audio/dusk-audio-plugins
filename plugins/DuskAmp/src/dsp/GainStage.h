#pragma once

#include "AnalogEmulation/TubeEmulation.h"
#include "AnalogEmulation/DCBlocker.h"
#include <cmath>
#include <algorithm>

// Configuration for a single tube gain stage with associated passive components
struct GainStageConfig
{
    // Tube type and bias
    AnalogEmulation::TubeEmulation::TubeType tubeType =
        AnalogEmulation::TubeEmulation::TubeType::Triode_12AX7;
    float biasPoint = 0.0f;       // -1 to +1 (cold to hot)
    float driveScale = 0.5f;      // How much of the master gain maps to this stage (0-1)

    // Coupling cap HPF (before this stage, blocks DC from previous stage)
    float couplingCapHz = 30.0f;  // HPF cutoff frequency

    // Cathode bypass cap (boosts low-freq gain below this freq)
    float cathodeBypassHz = 80.0f;   // 0 = no bypass cap
    float cathodeBypassAmount = 0.15f; // Strength of bass boost (0-0.5)

    // Bright cap (treble boost when bright switch on)
    float brightCapHz = 0.0f;     // 0 = no bright cap on this stage
    float brightCapAmount = 0.25f; // Strength of treble boost

    // Inter-stage filtering (AFTER this stage, BEFORE next)
    float postStageLPFHz = 0.0f;  // 0 = no LPF (e.g., 5000Hz for high-gain filtering)
    float postStageHPFHz = 0.0f;  // 0 = no HPF (e.g., 100Hz to tighten before next stage)

    // Cathode follower (unity-gain buffer with compression)
    bool hasCathodeFollower = false;
    float cfAttackMs = 2.0f;
    float cfReleaseMs = 50.0f;
    float cfMaxReduction = 0.3f;  // Maximum gain reduction (0-1)

    // Output attenuation (simulates voltage divider between stages)
    float outputAttenuation = 1.0f; // 1.0 = full, 0.5 = -6dB

    // Volume cleanup: dynamic bias/grid-current modulation based on input level
    float biasSensitivity = 0.3f;         // 0-1: how much bias shifts toward linear at low input
    float gridCurrentSensitivity = 0.4f;  // 0-1: how much grid threshold rises at low input
    float cleanupSpeed = 0.15f;           // 0-1: RMS time constant (higher = slower response)
};

// A single configurable tube gain stage with all associated passive components
class GainStage
{
public:
    void configure (const GainStageConfig& config);
    void prepare (double sampleRate);
    void reset();
    void setGain (float gain01);
    void setBright (bool on);
    float processSample (float input);

private:
    GainStageConfig config_;
    AnalogEmulation::TubeEmulation tube_;
    AnalogEmulation::DCBlocker dc_;
    double sampleRate_ = 44100.0;
    float gain_ = 0.5f;
    bool bright_ = false;

    // One-pole filter states
    float couplingCapState_ = 0.0f;
    float couplingCapCoeff_ = 0.0f; // HPF: exp(-2*pi*fc/fs)

    float cathodeBypassState_ = 0.0f;
    float cathodeBypassCoeff_ = 0.0f; // LPF: w/(w+1)

    float brightCapState_ = 0.0f;
    float brightCapCoeff_ = 0.0f; // HPF coefficient

    float postLPFState_ = 0.0f;
    float postLPFCoeff_ = 0.0f;

    float postHPFState_ = 0.0f;
    float postHPFCoeff_ = 0.0f;

    // Cathode follower envelope
    float cfEnvelope_ = 0.0f;
    float cfAttackCoeff_ = 0.0f;
    float cfReleaseCoeff_ = 0.0f;

    // Volume cleanup: input RMS tracking for dynamic bias/grid-current
    float cleanupRMS_ = 0.0f;
    float cleanupAttackCoeff_ = 0.0f;
    float cleanupReleaseCoeff_ = 0.0f;

    static constexpr float kPi = 3.14159265358979323846f;

    void updateFilterCoefficients();
};
