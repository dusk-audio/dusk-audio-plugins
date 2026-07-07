// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// DuskSVF.hpp — topology-preserving-transform state-variable filter.
//
// Framework-free replacement for juce::dsp::StateVariableTPTFilter
// (Zavalishin/Cytomic TPT SVF). The per-sample recurrence and the g/R2/h
// coefficient definitions are ported verbatim from JUCE's
// StateVariableTPTFilter so a straight LP/BP/HP swap reproduces the JUCE
// magnitude and phase response (A/B target, not bit-exact — trig is computed
// in the same order but JUCE uses double internally for the coeffs).
//
// One SVF instance = one channel of state (ic1eq/ic2eq). Use one per channel.

#pragma once

#include <cmath>

namespace duskaudio
{

// M_PI is not guaranteed by the C++ standard (POSIX / needs _USE_MATH_DEFINES on
// MSVC). Use a portable local constant, matching kDuskPi in DuskFilters.hpp.
constexpr float kDuskSvfPi = 3.14159265358979323846f;

class DuskSVF
{
public:
    enum class Type { lowpass, bandpass, highpass };

    void setType(Type t) noexcept { type = t; }

    // Q is the resonance (JUCE: resonance = 1/R2). cutoff in Hz.
    void prepare(double sampleRate, float cutoffHz, float Q) noexcept
    {
        fs = sampleRate;
        setCutoff(cutoffHz);
        setResonance(Q);
    }

    void setCutoff(float cutoffHz) noexcept
    {
        // clamp away from 0 and Nyquist exactly like JUCE's update()
        const float nyq = (float)(0.5 * fs);
        cutoff = cutoffHz < 20.0f ? 20.0f : (cutoffHz > nyq - 1.0f ? nyq - 1.0f : cutoffHz);
        g = std::tan(kDuskSvfPi * cutoff / (float)fs);
        updateH();
    }

    void setResonance(float Q) noexcept
    {
        reso = Q < 0.05f ? 0.05f : Q;
        R2 = 1.0f / reso;
        updateH();
    }

    void reset() noexcept { ic1eq = ic2eq = 0.0f; }

    float process(float x) noexcept
    {
        const float yHP = h * (x - (g + R2) * ic1eq - ic2eq);
        const float yBP = g * yHP + ic1eq;
        ic1eq = g * yHP + yBP;
        const float yLP = g * yBP + ic2eq;
        ic2eq = g * yBP + yLP;
        switch (type)
        {
            case Type::lowpass:  return yLP;
            case Type::bandpass: return yBP;
            case Type::highpass: return yHP;
        }
        return yLP;
    }

private:
    void updateH() noexcept { h = 1.0f / (1.0f + R2 * g + g * g); }

    Type   type   = Type::lowpass;
    double fs     = 44100.0;
    float  cutoff = 1000.0f, reso = 0.70710678f;
    float  g = 0.0f, R2 = 1.41421356f, h = 1.0f;
    float  ic1eq = 0.0f, ic2eq = 0.0f;
};

} // namespace duskaudio
