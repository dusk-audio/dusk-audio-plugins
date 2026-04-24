#pragma once

// Per-preset reverb engine for "Drum Chamber".
// Base engine: QuadTank
//
// This header only exposes a factory function. The concrete engine class and
// full DSP implementation live inside DrumChamberPreset.cpp in an anonymous namespace,
// so modifying one preset's DSP cannot affect any other preset.

#include "PresetEngineBase.h"
#include <memory>

std::unique_ptr<PresetEngineBase> createDrumChamberPreset();
