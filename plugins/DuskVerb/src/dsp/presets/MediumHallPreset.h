#pragma once

// Per-preset reverb engine for "Medium Hall".
// Base engine: FDNReverb
//
// This header only exposes a factory function. The concrete engine class and
// full DSP implementation live inside MediumHallPreset.cpp in an anonymous namespace,
// so modifying one preset's DSP cannot affect any other preset.

#include "PresetEngineBase.h"
#include <memory>

std::unique_ptr<PresetEngineBase> createMediumHallPreset();
