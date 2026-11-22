// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// The DSP bridge (MultiSynthAccess.hpp) as an out-of-process UI sees it, for the
// editor tests that #include MultiSynthUI.cpp. Defined because a weak reference
// must still resolve when a macOS binary links. Include once, after the UI.
#pragma once

float multiSynthGetOutLevelL(void*) noexcept { return -60.0f; }
float multiSynthGetOutLevelR(void*) noexcept { return -60.0f; }
int multiSynthGetArpStep(void*) noexcept { return -1; }
msynth::MultiSynthDSP* multiSynthGetDSP(void*) noexcept { return nullptr; }
uint32_t multiSynthGetMidiProgramSignal(void*) noexcept { return 0; }
