#pragma once

#include <cmath>
#include <algorithm>

// Circuit-modeled tone stack based on David Yeh's Stanford thesis
// "Digital Implementation of Musical Distortion Circuits by Analysis and Simulation" (2009)
// and DAFx-06 paper "Discretization of the '59 Fender Bassman Tone Stack"
//
// Uses actual component values from real amp schematics. The bass/mid/treble
// controls are interactive (changing bass affects mid response) exactly like
// the real amps.

class ToneStack
{
public:
    enum class Model { Round = 0, Chime = 1, Punch = 2 };

    void prepare (double sampleRate);
    void reset();
    void setModel (Model model);
    void setBass (float value01);
    void setMid (float value01);
    void setTreble (float value01);
    void process (float* buffer, int numSamples);

private:
    // Component values for the TMB tone stack circuit
    struct Components
    {
        float R1;         // Slope resistor (Ohms)
        float R2;         // Bass pot total resistance (Ohms)
        float R3;         // Mid pot total resistance (Ohms)
        float R4;         // Treble pot total resistance (Ohms)
        float C1;         // Treble cap (Farads)
        float C2;         // Mid cap (Farads)
        float C3;         // Bass cap (Farads)
        bool  hasMidPot;  // Vox AC30 has no mid pot
    };

    // 3rd-order Direct Form II Transposed filter
    struct ThirdOrderFilter
    {
        float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, b3 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;  // a0 normalized to 1.0
        float z1 = 0.0f, z2 = 0.0f, z3 = 0.0f;

        float process (float x)
        {
            float out = b0 * x + z1;
            z1 = b1 * x - a1 * out + z2;
            z2 = b2 * x - a2 * out + z3;
            z3 = b3 * x - a3 * out;
            return out;
        }

        void reset() { z1 = z2 = z3 = 0.0f; }
    };

    // Peaking biquad for Chime model mid overlay
    struct Biquad
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

    Model currentModel_ = Model::Round;
    double sampleRate_ = 44100.0;
    float bass_ = 0.5f, mid_ = 0.5f, treble_ = 0.5f;

    ThirdOrderFilter filter_;
    Biquad midOverlay_;          // Only used for Chime model (which has no mid pot)
    bool coeffsDirty_ = true;

    // Coefficient smoothing: interpolate over kSmoothBlocks blocks to prevent clicks
    static constexpr int kSmoothBlocks = 4;
    int smoothBlocksRemaining_ = 0;

    // Target coefficients (smoothing interpolates filter_ toward these)
    struct TargetCoeffs
    {
        float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, b3 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    };
    TargetCoeffs targetCoeffs_;

    static const Components kRoundComponents;
    static const Components kChimeComponents;
    static const Components kPunchComponents;

    const Components& getCurrentComponents() const;
    void recomputeCoefficients();
    void computeTMBCoefficients (const Components& comp, float bass, float mid, float treble);
    void computeChimeMidOverlay (float mid);
};
