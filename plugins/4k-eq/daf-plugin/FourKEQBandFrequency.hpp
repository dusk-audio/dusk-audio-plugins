// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// FourKEQBandFrequency.hpp — the two ways a 4K EQ 2 band or filter frequency
// can be set, shared by the DSP shell, the UI and the tests (framework-free).
//
// Each band has a Hz parameter (kLfHz..kHfHz, what the knob shows and the band
// plays) and a legacy dial parameter (kLfFreq..kHfFreq, the shipped index and
// symbol, a position on the reference's printed dial played through the
// measured dial law). Sessions and automation from before
// dusk-audio-plugins#288 address the dial parameter, so it keeps its meaning.
// The last one written wins; kLegacyDialBands records which that was. The HPF
// and LPF work the same way: kHpfHz / kLpfHz (each filter's -3 dB point),
// kHpfFreq / kLpfFreq, and kLegacyDialFilters.
//
// A selector's value is its bits (bit set: that band or filter follows its
// legacy dial) plus an optional flag, kSelectorStated or kSelectorStatedAlt,
// that only marks a deliberate write. DAF's LV2 wrapper passes on a control
// port only when its value changes, so a band whose Hz port already holds the
// value a preset or reset wants would otherwise stay on its dial. Presets and
// programs write the selector with a flag, and the editor alternates the two
// flags, so an editor write always differs from the port's last value. A
// preset's does not: it writes the same flagged value every time. Port order
// makes the selector land after the frequencies it arbitrates.
//
// LV2 leaves one case this cannot reach. A plugin cannot write its own input
// ports, and hosts such as Ardour do not implement the control-input change
// request, so a legacy dial write made after a preset (from a generic UI, or
// old automation on a "(Legacy Dial)" parameter) moves the plugin's selector
// but not the host's selector port, which still holds the preset's value.
// Reloading the session restores that port and the band returns to its Hz
// parameter, and applying the same preset again changes no port, so the band
// stays on its dial. manuals/4k-eq-2.md documents it.

#pragma once

#include <atomic>
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

static constexpr uint32_t kSelectorStated = 1u << 4;
static constexpr uint32_t kSelectorStatedAlt = 1u << 5;
static constexpr float kLegacyDialBandsMax = (float)(kSelectorStatedAlt | 15u);
static constexpr float kLegacyDialFiltersMax = (float)(kSelectorStatedAlt | 3u);
static_assert(kFourKParams[kLegacyDialBands].max == kLegacyDialBandsMax
              && kFourKParams[kLegacyDialFilters].max == kLegacyDialFiltersMax,
              "the selector parameters must hold every bit and flag");

inline uint32_t fkSelectorValue(float stored, float max) noexcept
{
    if (!(stored > 0.0f))
        return 0u;
    return (uint32_t)std::lround(stored < max ? stored : max);
}

inline uint32_t fkLegacyDialBits(float stored) noexcept
{
    return fkSelectorValue(stored, kLegacyDialBandsMax) & 15u;
}

// A selector value stating bits, with the flag the previous value did not
// carry, so a host that forwards only changes cannot drop it.
inline float fkStatedSelector(uint32_t bits, float previous, float max) noexcept
{
    const bool stated = fkSelectorValue(previous, max) & kSelectorStated;
    return (float)((stated ? kSelectorStatedAlt : kSelectorStated) | bits);
}

inline bool fkBandFollowsLegacyDial(const float* values, int band) noexcept
{
    return (fkLegacyDialBits(values[kLegacyDialBands]) >> band) & 1u;
}

struct FourKEQFilterIds
{
    uint32_t hz, legacyDial;
    bool highPass;
};

static constexpr FourKEQFilterIds kFourKEQFilters[2] = {
    { kHpfHz, kHpfFreq, true },
    { kLpfHz, kLpfFreq, false },
};

constexpr int fkFilterOfHzParam(uint32_t index)
{
    return index == kHpfHz ? 0 : index == kLpfHz ? 1 : -1;
}

constexpr int fkFilterOfLegacyDialParam(uint32_t index)
{
    return index == kHpfFreq ? 0 : index == kLpfFreq ? 1 : -1;
}

inline uint32_t fkLegacyDialFilterBits(float stored) noexcept
{
    return fkSelectorValue(stored, kLegacyDialFiltersMax) & 3u;
}

inline bool fkFilterFollowsLegacyDial(const float* values, int filter) noexcept
{
    return (fkLegacyDialFilterBits(values[kLegacyDialFilters]) >> filter) & 1u;
}

inline bool fkBandIsBell(const float* values, int band) noexcept
{
    const int sw = kFourKEQBands[band].bellSwitch;
    return sw < 0 || values[sw] > 0.5f;
}

// What a parameter write does to a selector: a legacy dial write sets its
// band's or filter's bit, an Hz write clears it, and a write to the selector
// replaces it, flag and all. Any other write leaves both selectors alone.
struct FourKEQSelectorWrite
{
    enum Op { None, SetBit, ClearBit, Replace };
    Op op;
    uint32_t selector; // kLegacyDialBands or kLegacyDialFilters
    uint32_t operand;  // the bit, or the whole selector value

    uint32_t applyTo(uint32_t current) const noexcept
    {
        switch (op)
        {
        case SetBit:   return current | operand;
        case ClearBit: return current & ~operand;
        case Replace:  return operand;
        case None:     break;
        }
        return current;
    }
};

inline FourKEQSelectorWrite fkSelectorWrite(uint32_t index, float value) noexcept
{
    if (const int b = fkBandOfLegacyDialParam(index); b >= 0)
        return { FourKEQSelectorWrite::SetBit, kLegacyDialBands, 1u << b };
    if (const int b = fkBandOfHzParam(index); b >= 0)
        return { FourKEQSelectorWrite::ClearBit, kLegacyDialBands, 1u << b };
    if (index == kLegacyDialBands)
        return { FourKEQSelectorWrite::Replace, kLegacyDialBands, fkSelectorValue(value, kLegacyDialBandsMax) };
    if (const int f = fkFilterOfLegacyDialParam(index); f >= 0)
        return { FourKEQSelectorWrite::SetBit, kLegacyDialFilters, 1u << f };
    if (const int f = fkFilterOfHzParam(index); f >= 0)
        return { FourKEQSelectorWrite::ClearBit, kLegacyDialFilters, 1u << f };
    if (index == kLegacyDialFilters)
        return { FourKEQSelectorWrite::Replace, kLegacyDialFilters, fkSelectorValue(value, kLegacyDialFiltersMax) };
    return { FourKEQSelectorWrite::None, 0u, 0u };
}

// Stores a parameter write in a copy of the parameters that one thread owns
// (the editor's, a preset file's), moving its selector as fkSelectorWrite
// says. The plugin's own parameters are written by more than one thread and
// keep their selectors in FourKEQSelectors.
inline void fkStoreParam(float* values, uint32_t index, float value) noexcept
{
    values[index] = value;
    const FourKEQSelectorWrite w = fkSelectorWrite(index, value);
    if (w.op != FourKEQSelectorWrite::None)
        values[w.selector] = (float)w.applyTo(fkSelectorValue(values[w.selector], kFourKParams[w.selector].max));
}

// The selectors of a plugin instance. Hosts write parameters from two threads
// at once: DAF's CLAP and VST3 wrappers apply a state or a program on the main
// thread while the audio thread applies automation. Each write therefore moves
// its selector in one atomic read-modify-write that changes only what the
// write owns: fetch_or or fetch_and of its band's or filter's bit, which keeps
// the other bits and the flag, or exchange for a selector write. However the
// writes interleave, a selector ends where some order of them would leave it,
// so no band or filter is left following a parameter its last write did not
// choose, and a stated selector keeps its flag.
//
// The writes release and the reads acquire: the plugin stores a frequency
// before it moves the selector, so a thread that reads the selector as moved
// also reads the frequency written with it. Word is std::atomic<uint32_t>
// (FourKEQSelectors); the logic test substitutes one that runs another write
// between any two of its operations.
template <typename Word>
struct FourKEQSelectorsOf
{
    Word bands { 0u }, filters { 0u };

    // Moves the selector the write concerns. False when it concerns neither.
    bool record(uint32_t index, float value) noexcept
    {
        const FourKEQSelectorWrite w = fkSelectorWrite(index, value);
        Word& word = w.selector == kLegacyDialBands ? bands : filters;
        switch (w.op)
        {
        case FourKEQSelectorWrite::SetBit:   word.fetch_or(w.operand, std::memory_order_release); return true;
        case FourKEQSelectorWrite::ClearBit: word.fetch_and(~w.operand, std::memory_order_release); return true;
        case FourKEQSelectorWrite::Replace:  word.exchange(w.operand, std::memory_order_release); return true;
        case FourKEQSelectorWrite::None:     break;
        }
        return false;
    }

    // The selector parameter's value, bits and flag.
    uint32_t value(uint32_t selector) const noexcept
    {
        return (selector == kLegacyDialBands ? bands : filters).load(std::memory_order_acquire);
    }
    uint32_t bandBits() const noexcept { return value(kLegacyDialBands) & 15u; }
    uint32_t filterBits() const noexcept { return value(kLegacyDialFilters) & 3u; }
};

using FourKEQSelectors = FourKEQSelectorsOf<std::atomic<uint32_t>>;

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

// Routes each band to the core through the parameter it follows (bit b of
// dialBands, kLegacyDialBands' bits): the dial API for a legacy dial, so a
// pre-#288 session plays exactly what it did.
inline void fkApplyBandFrequencies(duskaudio::FourKEQDSP& dsp, const float* values, uint32_t dialBands) noexcept
{
    using Band = duskaudio::FourKEQDSP::Band;
    for (int b = 0; b < 4; ++b)
    {
        const FourKEQBandIds& ids = kFourKEQBands[b];
        const bool dial = (dialBands >> b) & 1u;
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

// The Hz a filter plays (its -3 dB point): its Hz parameter, or the Hz its
// legacy dial position plays.
inline float fkFilterHz(const float* values, int filter) noexcept
{
    const FourKEQFilterIds& ids = kFourKEQFilters[filter];
    if (!fkFilterFollowsLegacyDial(values, filter))
        return values[ids.hz];
    return duskaudio::FourKEQDSP::hzForCalibratedFilterControl(
        values[ids.legacyDial], ids.highPass, values[kEqType] > 0.5f);
}

// dialFilters: kLegacyDialFilters' bits.
inline void fkApplyFilterFrequencies(duskaudio::FourKEQDSP& dsp, const float* values, uint32_t dialFilters) noexcept
{
    if (dialFilters & 1u)
        dsp.setHpfFreq(values[kHpfFreq]);
    else
        dsp.setHpfFreqHz(values[kHpfHz]);
    if (dialFilters & 2u)
        dsp.setLpfFreq(values[kLpfFreq]);
    else
        dsp.setLpfFreqHz(values[kLpfHz]);
}

inline void fkSetCurveFilterFrequencies(duskaudio::FourKEQDSP::CurveControls& c, const float* values) noexcept
{
    c.hpfFreqInHz = !fkFilterFollowsLegacyDial(values, 0);
    c.lpfFreqInHz = !fkFilterFollowsLegacyDial(values, 1);
    c.hpfFreq = values[c.hpfFreqInHz ? kHpfHz : kHpfFreq];
    c.lpfFreq = values[c.lpfFreqInHz ? kLpfHz : kLpfFreq];
}
