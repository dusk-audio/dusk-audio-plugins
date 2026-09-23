// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// FourKEQBandFrequency.hpp — the two ways a 4K EQ 2 band frequency can be set,
// shared by the DSP shell, the UI and the tests (framework-free).
//
// Each band has a Hz parameter (kLfHz..kHfHz, what the knob shows and the band
// plays) and a legacy dial parameter (kLfFreq..kHfFreq, the shipped index and
// symbol, a position on the reference's printed dial played through the
// measured dial law). Sessions and automation from before
// dusk-audio-plugins#288 address the dial parameter, so it keeps its meaning.
// The last one written wins; kLegacyDialBands records which that was.

#pragma once

#include <cmath>
#include <cstdint>

#include "FourKEQDSP.hpp"
#include "FourKEQParams.hpp"

struct FourKEQBandIds
{
    uint32_t hz, legacyDial, gain;
    int bellSwitch; // -1: always a bell
    duskaudio::FourKEQDSP::Band band;
};

static constexpr FourKEQBandIds kFourKEQBands[4] = {
    { kLfHz, kLfFreq, kLfGain, kLfBell, duskaudio::FourKEQDSP::Band::LF },
    { kLmHz, kLmFreq, kLmGain, -1,      duskaudio::FourKEQDSP::Band::LM },
    { kHmHz, kHmFreq, kHmGain, -1,      duskaudio::FourKEQDSP::Band::HM },
    { kHfHz, kHfFreq, kHfGain, kHfBell, duskaudio::FourKEQDSP::Band::HF },
};

constexpr int fkBandOfHzParam(uint32_t index)
{
    for (int b = 0; b < 4; ++b)
        if (kFourKEQBands[b].hz == index)
            return b;
    return -1;
}

constexpr int fkBandOfLegacyDialParam(uint32_t index)
{
    for (int b = 0; b < 4; ++b)
        if (kFourKEQBands[b].legacyDial == index)
            return b;
    return -1;
}

inline uint32_t fkLegacyDialBits(float stored) noexcept
{
    if (!(stored > 0.0f))
        return 0u;
    return (uint32_t)std::lround(stored < 15.0f ? stored : 15.0f);
}

inline bool fkBandFollowsLegacyDial(const float* values, int band) noexcept
{
    return (fkLegacyDialBits(values[kLegacyDialBands]) >> band) & 1u;
}

inline bool fkBandIsBell(const float* values, int band) noexcept
{
    const int sw = kFourKEQBands[band].bellSwitch;
    return sw < 0 || values[sw] > 0.5f;
}

// Stores a parameter write and, for a band frequency, records which of the
// band's two parameters it came through.
inline void fkStoreParam(float* values, uint32_t index, float value) noexcept
{
    values[index] = value;
    uint32_t bits = fkLegacyDialBits(values[kLegacyDialBands]);
    if (const int b = fkBandOfLegacyDialParam(index); b >= 0)
        bits |= 1u << b;
    else if (const int h = fkBandOfHzParam(index); h >= 0)
        bits &= ~(1u << h);
    else if (index == kLegacyDialBands)
        bits = fkLegacyDialBits(value);
    else
        return;
    values[kLegacyDialBands] = (float)bits;
}

// The Hz a band plays: its Hz parameter, or the Hz its legacy dial position
// plays. Gain-independent, so the readout does not move with the gain knob.
inline float fkBandHz(const float* values, int band) noexcept
{
    const FourKEQBandIds& ids = kFourKEQBands[band];
    if (!fkBandFollowsLegacyDial(values, band))
        return values[ids.hz];
    return duskaudio::FourKEQDSP::hzForCalibratedEqControl(
        values[ids.legacyDial], ids.band, values[kEqType] > 0.5f, fkBandIsBell(values, band));
}

// The legacy dial position that plays hz, clamped to the dial's ends where
// the dial cannot reach it.
inline float fkLegacyDialForHz(int band, float hz, bool black, bool bell) noexcept
{
    using duskaudio::FourKEQDSP;
    const FourKEQBandIds& ids = kFourKEQBands[band];
    float lo = kFourKParams[ids.legacyDial].min, hi = kFourKParams[ids.legacyDial].max;
    if (!(hz > FourKEQDSP::hzForCalibratedEqControl(lo, ids.band, black, bell)))
        return lo;
    if (!(hz < FourKEQDSP::hzForCalibratedEqControl(hi, ids.band, black, bell)))
        return hi;
    for (int i = 0; i < 48; ++i)
    {
        const float mid = 0.5f * (lo + hi);
        (FourKEQDSP::hzForCalibratedEqControl(mid, ids.band, black, bell) < hz ? lo : hi) = mid;
    }
    return 0.5f * (lo + hi);
}

// Routes each band to the core through the parameter it follows: the dial
// API for a legacy dial, so a pre-#288 session plays exactly what it did.
inline void fkApplyBandFrequencies(duskaudio::FourKEQDSP& dsp, const float* values) noexcept
{
    using Band = duskaudio::FourKEQDSP::Band;
    for (int b = 0; b < 4; ++b)
    {
        const FourKEQBandIds& ids = kFourKEQBands[b];
        const bool dial = fkBandFollowsLegacyDial(values, b);
        const float v = values[dial ? ids.legacyDial : ids.hz];
        switch (ids.band)
        {
        case Band::LF: dial ? dsp.setLfFreq(v) : dsp.setLfFreqHz(v); break;
        case Band::LM: dial ? dsp.setLmFreq(v) : dsp.setLmFreqHz(v); break;
        case Band::HM: dial ? dsp.setHmFreq(v) : dsp.setHmFreqHz(v); break;
        case Band::HF: dial ? dsp.setHfFreq(v) : dsp.setHfFreqHz(v); break;
        }
    }
}

// The response curve's band frequencies, drawn the way fkApplyBandFrequencies
// plays them.
inline void fkSetCurveBandFrequencies(duskaudio::FourKEQDSP::CurveControls& c, const float* values) noexcept
{
    c.bandFrequenciesInHz = true;
    c.dialBands = fkLegacyDialBits(values[kLegacyDialBands]);
    float* const freq[4] = { &c.lfFreq, &c.lmFreq, &c.hmFreq, &c.hfFreq };
    for (int b = 0; b < 4; ++b)
        *freq[b] = values[((c.dialBands >> b) & 1u) ? kFourKEQBands[b].legacyDial : kFourKEQBands[b].hz];
}

// The band knobs' read-out of fkBandHz.
static constexpr const char* kFourKBandHzFormat = "%.0f Hz";
