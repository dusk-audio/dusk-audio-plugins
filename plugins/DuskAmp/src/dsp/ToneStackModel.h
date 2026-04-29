// SPDX-License-Identifier: GPL-3.0-or-later

// ToneStackModel.h — Circuit-modeled guitar amp tone stacks
//
// Implements the Yeh/Smith bilinear transform method (Stanford DAFx 2006-2008)
// for passive RC tone stack networks. Each topology is a 3rd-order (or 2nd-order
// for Vox) IIR filter whose coefficients are polynomial functions of the
// Bass/Mid/Treble pot positions, producing the interactive knob behavior of
// real amplifiers.
//
// References:
//   David Yeh, "Digital Implementation of Musical Distortion Circuits by
//   Analysis and Simulation", PhD Thesis, Stanford University, 2009.
//   Yeh, Abel, Smith — DAFx 2006, 2008.

#pragma once

class ToneStackModel
{
public:
    enum class Topology
    {
        Fender = 0,     // Fender Twin Reverb AB763 tone stack
        Marshall = 1,   // Marshall JTM45/Plexi "James" tone stack
        Vox = 2         // Vox AC30 Top Boost cut circuit
    };

    void prepare (double sampleRate);
    void reset();

    void setTopology (Topology t);
    void setBass (float value01);
    void setMid (float value01);
    void setTreble (float value01);

    void process (float* buffer, int numSamples);

    Topology getTopology() const { return topology_; }

private:
    Topology topology_ = Topology::Marshall;
    double sampleRate_ = 44100.0;

    // Pot positions [0.01, 0.99] (clamped to avoid degenerate coefficients)
    float bass_   = 0.5f;
    float mid_    = 0.5f;
    float treble_ = 0.5f;
    bool coeffsDirty_ = true;

    // 3rd-order IIR filter state (Transposed Direct Form II)
    // Fender and Marshall use all 3 sections; Vox uses only 2
    double b0_ = 1.0, b1_ = 0.0, b2_ = 0.0, b3_ = 0.0;
    double a1_ = 0.0, a2_ = 0.0, a3_ = 0.0;
    double z1_ = 0.0, z2_ = 0.0, z3_ = 0.0;

    // Component value structs for each topology
    struct FenderMarshallComponents
    {
        double R1, R2, R3, R4;
        double C1, C2, C3;
    };

    struct VoxComponents
    {
        double R1, R2, R3;  // R_bass, R_treble, R_mix
        double C1, C2, C3;
    };

    static FenderMarshallComponents getFenderComponents();
    static FenderMarshallComponents getMarshallComponents();
    static VoxComponents getVoxComponents();

    void recomputeCoefficients();
    void computeFenderMarshall (const FenderMarshallComponents& comp);
    void computeVox (const VoxComponents& comp);
};
