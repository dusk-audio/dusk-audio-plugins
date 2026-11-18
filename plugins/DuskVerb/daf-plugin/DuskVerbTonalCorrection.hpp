// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// DuskVerbTonalCorrection.hpp — where the TONAL CORRECTION switch changes the
// sound, so the editor shows it exactly there. Header-only, because the editor
// links without the DSP. DuskVerbTonalCorrectionTests renders the DSP on both
// sides of the switch and holds both rules below to what it measures.

#pragma once

#include "dsp/AlgorithmConfig.h"

#include <string_view>

namespace duskverb::ui
{

// tonal_correction reaches only accurateHall_'s output GEQ, and these are the
// engines that run that tank (DuskVerbEngine's process switch): Vintage Hall,
// Hall, Sparse, Concert Hall and the Tiled Room composite tail. Dense Hall and
// Parallel Hall have tanks of their own.
inline bool engineRunsAccurateHall(EngineType engine) noexcept
{
    return engine == EngineType::VintageTank || engine == EngineType::AccurateHall
        || engine == EngineType::SparseField || engine == EngineType::AccurateHall32
        || engine == EngineType::TiledRoom;
}

// The GEQ runs only while an octave decay curve is installed, and only these
// preset identities install one (kAccurateHallT60ByName in
// core/DuskVerbPresetEngineConfig.cpp; the DUSKVERB_AHOCT tuning variable can
// force one, but only when the host process's environment sets it, as a
// calibration setup does). A user preset keeps the factory identity it was made
// from, so it counts too.
inline bool identityHasOctaveCurve(std::string_view presetName) noexcept
{
    return presetName == "Vocal Plate" || presetName == "Ambience" || presetName == "Tiled Room";
}

inline bool tonalCorrectionActs(EngineType engine, std::string_view presetName) noexcept
{
    return engineRunsAccurateHall(engine) && identityHasOctaveCurve(presetName);
}

} // namespace duskverb::ui
