#pragma once

// Per-preset reverb engine for "Drum Room".
// Base engine: DattorroTank
//
// This header only exposes a factory function. The concrete engine class and
// full DSP implementation live inside DrumRoomPreset.cpp in an anonymous namespace,
// so modifying one preset's DSP cannot affect any other preset.

#include "PresetEngineBase.h"
#include <memory>

std::unique_ptr<PresetEngineBase> createDrumRoomPreset();
