#pragma once

// GENERATED FILE - do not edit by hand (use generate_preset_engines.py)
//
// Per-preset reverb engine for "Exciting Snare room".
// Base engine: DattorroTank
//
// This header only exposes a factory function. The concrete engine class and
// full DSP implementation live inside ExcitingSnareRoomPreset.cpp in an anonymous namespace,
// so modifying one preset's DSP cannot affect any other preset.

#include "PresetEngineBase.h"
#include <memory>

std::unique_ptr<PresetEngineBase> createExcitingSnareRoomPreset();
