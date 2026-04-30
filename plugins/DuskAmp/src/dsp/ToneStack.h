// ToneStack.h — Circuit-derived tone stack using Yeh/Smith transfer functions
//
// Implements the 3rd-order transfer function derived from real passive RC
// tone stack circuits. All three controls interact — turning up bass changes
// the treble response — because the transfer function is derived from the
// actual circuit equations, not independent parametric EQ bands.
//
// Discretized via bilinear transform, implemented as a direct 3rd-order
// Transposed Direct Form II filter (3 state variables, no biquad factoring).
//
// Reference: Yeh & Smith, "Discretization of the '59 Fender Bassman Tone Stack"
//            Proceedings of DAFx-06, Montreal, Canada.

#pragma once

#include <cmath>
#include <algorithm>

class ToneStack
{
public:
    // American/British = Yeh/Smith 3-knob passive Fender/Marshall network.
    // AC = Vox AC30 Top Boost — a cathode-follower-driven James network
    // (2-band Baxandall: bass + treble shelves, NO mid control).
    enum class Type { American = 0, British = 1, AC = 2 };

    void prepare (double sampleRate);
    void reset();
    void setType (Type type);
    void setBass (float value01);
    void setMid (float value01);     // ignored for AC (Top Boost has no mid)
    void setTreble (float value01);
    void process (float* buffer, int numSamples);

private:
    Type currentType_ = Type::British;
    double sampleRate_ = 44100.0;
    float bass_ = 0.5f, mid_ = 0.5f, treble_ = 0.5f;
    bool coeffsDirty_ = true;

    // Component values per topology
    struct Components
    {
        double R1;   // Treble pot max (Ω)
        double R2;   // Bass pot max (Ω)
        double R3;   // Mid pot max (Ω)
        double R4;   // Fixed resistor (Ω)
        double C1;   // Treble cap (F)
        double C2;   // Bass cap (F)
        double C3;   // Mid cap (F)
    };

    static Components getComponents (Type type);

    // American / British path: 3rd-order IIR (Yeh/Smith bilinear transform).
    // H(z) = (B0 + B1*z^-1 + B2*z^-2 + B3*z^-3) / (1 + A1*z^-1 + A2*z^-2 + A3*z^-3)
    float B0_ = 0, B1_ = 0, B2_ = 0, B3_ = 0;
    float A1_ = 0, A2_ = 0, A3_ = 0;
    float w1_ = 0, w2_ = 0, w3_ = 0;

    // AC / Top Boost path: two RBJ shelving biquads cascaded (bass then treble).
    // Corner frequencies and max boost/cut match the Top Boost James-network
    // behavior without modeling every coupling interaction — good first-order
    // emulation that fixes the wrong-topology issue (mid-knob-kills-volume).
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float x1 = 0, x2 = 0, y1 = 0, y2 = 0;

        float processSample (float x)
        {
            float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            return y;
        }

        void clear() { x1 = x2 = y1 = y2 = 0; }
    };
    Biquad tbBass_, tbTreble_;

    static constexpr float kTopBoostBassHz      = 100.0f;
    static constexpr float kTopBoostTrebleHz    = 6500.0f; // peak of the AC30 chime
    static constexpr float kTopBoostTrebleQ     = 1.4f;    // peaking filter Q
    static constexpr float kTopBoostBassMaxDb   = 12.0f;
    static constexpr float kTopBoostTrebleMaxDb = 15.0f; // Top Boost is bright

    // Fixed per-type midband makeup. Computed ONCE at prepare()/setType() using
    // flat knob settings (0.5/0.5/0.5): flatMakeup_ = 1 / |H(1kHz)|_flat. This
    // restores ~unity midband at flat positions without fighting the knobs —
    // so the mid control actually scoops mids, and bass/treble shape the
    // circuit's real analog EQ curve rather than being flattened by a
    // knob-tracking compensator.
    float flatMakeup_ = 1.0f;

    static constexpr float kCompensationFreqHz = 1000.0f;
    static constexpr float kFlatMakeupMaxGain  = 64.0f; // +36 dB safety cap

    void recomputeCoefficients();
    void recomputeTopBoost();
    void recomputeFlatMakeup();
    static void designLowShelf  (Biquad& bq, float fc, float gainDb, double sr);
    static void designHighShelf (Biquad& bq, float fc, float gainDb, double sr);
    static void designPeakingEQ (Biquad& bq, float fc, float gainDb, float q, double sr);
};
