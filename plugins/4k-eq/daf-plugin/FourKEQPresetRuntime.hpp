// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Runtime expansion of factory-preset data. Band and filter frequencies go to
// the Hz parameters as authored.

#pragma once

#include "FourKEQParams.hpp"

template <typename Fn>
inline void forEachFourKEQFactoryPresetParam(int idx, Fn&& fn)
{
    const FourKEQPreset& p = kFactoryPresets[idx];

    fn((uint32_t)kEqType, p.eqType);

    fn((uint32_t)kLfGain, p.lfGain);
    fn((uint32_t)kLfBell, p.lfBell);
    fn((uint32_t)kLfHz, p.lfFreq);

    fn((uint32_t)kLmGain, p.lmGain);
    fn((uint32_t)kLmQ, p.lmQ);
    fn((uint32_t)kLmHz, p.lmFreq);

    fn((uint32_t)kHmGain, p.hmGain);
    fn((uint32_t)kHmQ, p.hmQ);
    fn((uint32_t)kHmHz, p.hmFreq);

    fn((uint32_t)kHfGain, p.hfGain);
    fn((uint32_t)kHfBell, p.hfBell);
    fn((uint32_t)kHfHz, p.hfFreq);

    fn((uint32_t)kHpfHz, p.hpfFreq);
    fn((uint32_t)kLpfHz, p.lpfFreq);
    fn((uint32_t)kHpfEnabled, p.hpfFreq > 16.5f ? 1.0f : 0.0f);
    fn((uint32_t)kLpfEnabled, p.lpfFreq < 15200.5f ? 1.0f : 0.0f);

    fn((uint32_t)kInputGain, p.inputGain);
    fn((uint32_t)kOutputGain, p.outputGain);
    // Factory programs are reference-authored curves. Keep the optional
    // convenience compensation out of their sound unless the user enables it
    // after recall.
    fn((uint32_t)kAutoGain, 0.0f);
}
