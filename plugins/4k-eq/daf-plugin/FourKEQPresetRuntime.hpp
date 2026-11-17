// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Runtime expansion of factory-preset data. Preset tables are authored in
// audible Hz; the host parameters retain their calibrated control coordinates
// so old sessions and automation remain bit-for-bit compatible.

#pragma once

#include "FourKEQDSP.hpp"
#include "FourKEQParams.hpp"

template <typename Fn>
inline void forEachFourKEQFactoryPresetParam(int idx, Fn&& fn)
{
    using duskaudio::FourKEQDSP;

    const FourKEQPreset& p = kFactoryPresets[idx];
    const bool black = p.eqType > 0.5f;

    fn((uint32_t)kEqType, p.eqType);

    fn((uint32_t)kLfGain, p.lfGain);
    fn((uint32_t)kLfBell, p.lfBell);
    fn((uint32_t)kLfFreq, FourKEQDSP::controlForCalibratedEqFrequency(
        p.lfFreq, p.lfGain, FourKEQDSP::Band::LF, black, p.lfBell > 0.5f));

    fn((uint32_t)kLmGain, p.lmGain);
    fn((uint32_t)kLmQ, p.lmQ);
    fn((uint32_t)kLmFreq, FourKEQDSP::controlForCalibratedEqFrequency(
        p.lmFreq, p.lmGain, FourKEQDSP::Band::LM, black, true));

    fn((uint32_t)kHmGain, p.hmGain);
    fn((uint32_t)kHmQ, p.hmQ);
    fn((uint32_t)kHmFreq, FourKEQDSP::controlForCalibratedEqFrequency(
        p.hmFreq, p.hmGain, FourKEQDSP::Band::HM, black, true));

    fn((uint32_t)kHfGain, p.hfGain);
    fn((uint32_t)kHfBell, p.hfBell);
    fn((uint32_t)kHfFreq, FourKEQDSP::controlForCalibratedEqFrequency(
        p.hfFreq, p.hfGain, FourKEQDSP::Band::HF, black, p.hfBell > 0.5f));

    fn((uint32_t)kHpfFreq, FourKEQDSP::controlForCalibratedFilterFrequency(
        p.hpfFreq, true, black));
    fn((uint32_t)kLpfFreq, FourKEQDSP::controlForCalibratedFilterFrequency(
        p.lpfFreq, false, black));
    fn((uint32_t)kHpfEnabled, p.hpfFreq > 16.5f ? 1.0f : 0.0f);
    fn((uint32_t)kLpfEnabled, p.lpfFreq < 15200.5f ? 1.0f : 0.0f);

    fn((uint32_t)kInputGain, p.inputGain);
    fn((uint32_t)kOutputGain, p.outputGain);
    // Factory programs are reference-authored curves. Keep the optional
    // convenience compensation out of their sound unless the user enables it
    // after recall.
    fn((uint32_t)kAutoGain, 0.0f);
}
