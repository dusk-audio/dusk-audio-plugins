#pragma once

#include "AnalogEmulation/TubeEmulation.h"
#include "AnalogEmulation/DCBlocker.h"

class PreampDSP
{
public:
    enum class AmpModel { Round = 0, Chime = 1, Punch = 2 };

    void prepare (double sampleRate);
    void reset();
    void setDrive (float drive01);
    void setAmpModel (AmpModel model);
    void setSagMultiplier (float sag);  // from power supply model (0.7-1.0)
    void initializeKorenLUTs();         // call from message thread (expensive)
    void process (float* buffer, int numSamples);

    static constexpr int kMaxStages = 3;

    // Per-stage configuration (from real amp schematics)
    struct StageConfig
    {
        float couplingCapF;      // Coupling capacitor (Farads)
        float gridLeakR;         // Grid leak resistor (Ohms)
        float cathodeBypassHz;   // Cathode bypass -3dB frequency
        float cathodeBypassDB;   // Extra gain from bypass cap (dB above cutoff)
        float driveScale;        // How much of the drive knob goes to this stage
    };

private:
    static constexpr float kPi = 3.14159265358979323846f;

    AnalogEmulation::TubeEmulation stages_[kMaxStages];
    AnalogEmulation::DCBlocker interStageDC_[kMaxStages];

    AmpModel currentModel_ = AmpModel::Round;
    float drive_ = 0.5f;
    float sagMultiplier_ = 1.0f;
    float sagBiasScale_ = 0.08f;   // per-model: how much sag shifts bias point
    int numActiveStages_ = 2;
    double sampleRate_ = 44100.0;

    // Per-model stage configs (set by setAmpModel)
    StageConfig stageConfigs_[kMaxStages] = {};

    // --- Tight input HPF (2nd-order Butterworth, ~80Hz) ---
    // Kills sub-lows before they cause IMD in cascading gain stages
    struct ButterworthHP
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;

        float process (float x)
        {
            float out = b0 * x + z1;
            z1 = b1 * x - a1 * out + z2;
            z2 = b2 * x - a2 * out;
            if (std::abs (z1) < 1e-15f) z1 = 0.0f;
            if (std::abs (z2) < 1e-15f) z2 = 0.0f;
            return out;
        }

        void reset() { z1 = z2 = 0.0f; }
    };

    ButterworthHP inputHPF_;

    // Bright cap (peaking EQ biquad — models resonant peak of cap across volume pot)
    bool brightActive_ = true;
    float brightFreq_ = 1500.0f;
    float brightGainDB_ = 3.0f;
    float brightQ_ = 1.5f;

    struct BrightBiquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;

        float process (float x)
        {
            float out = b0 * x + z1;
            z1 = b1 * x - a1 * out + z2;
            z2 = b2 * x - a2 * out;
            return out;
        }

        void reset() { z1 = z2 = 0.0f; }
    };

    BrightBiquad brightFilter_;

    // Treble bleed: compensates for HF loss at higher drive
    float trebleBleedState_ = 0.0f;
    float trebleBleedCoeff_ = 0.0f;
    float trebleBleedMix_ = 0.0f;       // proportional to drive
    float trebleBleedMaxDB_ = 3.0f;     // per-model max boost

    // --- Bias-shifting coupling cap with grid-leak model ---
    // When the grid goes positive, current flows through the coupling cap,
    // charging it and shifting the bias point negatively (blocking distortion).
    // The cap slowly discharges through a high-impedance grid-leak resistor.
    struct CouplingCapState
    {
        float hpfState = 0.0f;     // One-pole HPF state
        float biasShift = 0.0f;    // Accumulated negative bias from grid current
    };

    CouplingCapState couplingCaps_[kMaxStages] = {};
    float couplingCapCoeff_[kMaxStages] = {};
    float gridLeakDischargeCoeff_[kMaxStages] = {};  // Per-stage discharge rate

    static constexpr float kBiasShiftChargeRate = 0.005f;   // How fast coupling cap charges
    static constexpr float kMaxBiasShift = 0.8f;            // Max negative bias shift

    // --- Dynamic Miller capacitance scaling ---
    // TubeEmulation has static millerCapEffect; we add drive-dependent extra HF rolloff
    // to remove digital fizziness at high gain settings
    float millerDynamicState_[kMaxStages] = {};
    float millerDynamicCoeff_ = 0.0f;  // One-pole LPF coefficient (~6kHz)

    // Per-stage cathode bypass (one-pole high-shelf)
    float cathodeShelfState_[kMaxStages] = {};
    float cathodeShelfCoeff_[kMaxStages] = {};
    float cathodeShelfAttenuation_[kMaxStages] = {};  // LF gain reduction (linear, <1.0)

    // Pre-computed Koren LUTs for all 3 models (built at prepare time)
    static constexpr int kLUTSize = 4096;
    float korenLUTs_[3][kLUTSize] = {};
    bool lutsReady_ = false;

    void precomputeAllKorenLUTs();  // called once from prepare()
    void applyPrecomputedLUT (int modelIndex);  // swap LUT into stages

    void updateGainStaging();
    void updateBiasFromSag();
    void updateInputHPF();
    void updateCouplingCapCoeffs();
    void updateBrightCoeff();
    void updateTrebleBleed();
    void updateCathodeBypassCoeffs();
    void updateMillerDynamicCoeff();
};
