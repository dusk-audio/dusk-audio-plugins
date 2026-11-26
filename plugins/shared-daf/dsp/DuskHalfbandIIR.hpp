// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// DuskHalfbandIIR.hpp — polyphase IIR halfband x2 stages.
//
// An elliptic halfband built from two allpass branches (coefficients from the
// closed-form design, even-indexed on one branch, odd on the other), run at
// the low rate: an interpolator feeds one input to both branches and emits
// their outputs in turn; a decimator feeds the later input sample to the
// even branch and the earlier to the odd one and averages. Magnitude is flat
// in the passband to rounding (both branches are allpass); phase is not
// linear. Against DuskOversampler's FIR halfbands this reaches the same
// rejection with far fewer multiplies (10 coefficients: passband 0.21 fs,
// stopband 0.27 fs at -115 dB), at the price of phase dispersion near the
// band edge.
//
// Independent chains with the same section count run as lanes of one loop
// (lane innermost), so a compiler vectorizes them: two signals through one
// interpolator stage are four lanes (each signal's even and odd branch).

#pragma once

namespace duskaudio
{

// `Lanes` independent chains of `Sections` first-order allpass sections in
// the low-rate domain: y = a (x - y[n-1]) + x[n-1].
template <int Sections, int Lanes>
struct AllpassLanes
{
    float coef[Sections][Lanes] {};
    float x1[Sections][Lanes] {};
    float y1[Sections][Lanes] {};

    void reset() noexcept
    {
        for (int k = 0; k < Sections; ++k)
            for (int l = 0; l < Lanes; ++l)
                x1[k][l] = y1[k][l] = 0.0f;
    }

    void process(float (&v)[Lanes]) noexcept
    {
        for (int k = 0; k < Sections; ++k)
            for (int l = 0; l < Lanes; ++l)
            {
                const float out = coef[k][l] * (v[l] - y1[k][l]) + x1[k][l];
                x1[k][l] = v[l];
                y1[k][l] = out;
                v[l] = out;
            }
    }
};

// x2 interpolator for two signals at once, from a halfband of 2 * Sections
// coefficients. Lanes: a-even, a-odd, b-even, b-odd.
template <int Sections>
struct HalfbandUpsampler2xPair
{
    AllpassLanes<Sections, 4> branches;

    void set(const float (&halfband)[2 * Sections]) noexcept
    {
        for (int k = 0; k < Sections; ++k)
        {
            branches.coef[k][0] = branches.coef[k][2] = halfband[2 * k];
            branches.coef[k][1] = branches.coef[k][3] = halfband[2 * k + 1];
        }
    }
    void reset() noexcept { branches.reset(); }

    // a, b at the low rate -> each one's two high-rate samples, in time order.
    void process(float a, float b, float& a0, float& a1, float& b0, float& b1) noexcept
    {
        float v[4] = { a, a, b, b };
        branches.process(v);
        a0 = v[0];
        a1 = v[1];
        b0 = v[2];
        b1 = v[3];
    }
};

// x2 decimator from a halfband of 2 * Sections coefficients. Lanes: even
// (fed the later sample), odd (fed the earlier one).
template <int Sections>
struct HalfbandDownsampler2x
{
    AllpassLanes<Sections, 2> branches;

    void set(const float (&halfband)[2 * Sections]) noexcept
    {
        for (int k = 0; k < Sections; ++k)
        {
            branches.coef[k][0] = halfband[2 * k];
            branches.coef[k][1] = halfband[2 * k + 1];
        }
    }
    void reset() noexcept { branches.reset(); }

    float process(float first, float second) noexcept
    {
        float v[2] = { second, first };
        branches.process(v);
        return 0.5f * (v[0] + v[1]);
    }
};

} // namespace duskaudio
