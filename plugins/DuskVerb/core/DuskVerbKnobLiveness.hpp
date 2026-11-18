// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// Which of the editor's controls the engine a slot plays can hear. One table, read by the
// editor (a control the engine cannot hear is drawn dimmed, and the switches that would be dead
// are hidden) and by the core tests (a control declared live must change the render, one
// declared dead must not, on every factory program and every engine), so the two cannot drift
// apart.
//
// Live is a property of the engine, not of the preset: on the engines whose presets replace
// the three-band damping with a per-band decay table (the octave GEQs, the ParallelMultiband
// tank, the QuadTank hi-mid/air split) DuskVerbDampingScaler.hpp rescales that table, and on
// the Gated and Shimmer engines it drives the FDN high band their Treble setter misses. What
// is left dead here is a control the engine does not have at all.
//
// Saturation counts as live wherever the engine has the soft clip, though it is calibrated hot:
// at a full-scale sustained input most engines change by less than -66 dB, and it takes about
// +18 dBFS to reach -40 dB (measured 2026-10-25).

#pragma once

#include "DuskVerbAlgorithms.hpp"
#include "DuskVerbParamTable.hpp"

namespace duskverb
{

inline constexpr bool isDampingCardParam (int paramId) noexcept
{
    return paramId == BassMult || paramId == MidMult || paramId == Damping
        || paramId == Crossover || paramId == HighCrossover;
}

// Tonal Correction reaches the octave feedback network of the AccurateHall tail
// (DuskVerbEngine::setTonalCorrection), which these engines play alone or under a composite.
inline constexpr bool runsOctaveNetwork (EngineType e) noexcept
{
    return e == EngineType::AccurateHall || e == EngineType::AccurateHall32 || e == EngineType::SparseField
        || e == EngineType::TiledRoom || e == EngineType::VintageTank;
}

// The engines whose own early stage replaces the shared early reflections, so ER Level and
// ER Size reach nothing.
inline constexpr bool hasSharedEarlyReflections (EngineType e) noexcept
{
    return e != EngineType::DattorroVintage && e != EngineType::QuadTank && e != EngineType::ReverseRoom
        && e != EngineType::SparseField && e != EngineType::TiledRoom;
}

// The nonlinear burst (DuskVerbDSP::pushOwnParameters) has no loop, no early reflections, no
// modulation and no drive, and its output stage has no Mono Below. Of the damping card it has
// the HF damping alone.
inline constexpr bool nonLinHears (int paramId) noexcept
{
    switch (paramId)
    {
        case Size: case Diffusion: case ErLevel: case ErSize: case Freeze: case GateEnabled:
        case ModDepth: case ModRate: case Saturation: case Character:
        case MonoBelow: case MonoBelowDepth: case TonalCorrection:
        case BassMult: case MidMult: case Crossover: case HighCrossover:
            return false;
        default:
            return true;
    }
}

// True when moving `paramId` changes what the engine `algorithm` (any slot value) renders.
// Parameters this table does not cover always read live.
inline bool controlLive (int algorithm, int paramId) noexcept
{
    const int engine = resolveAlgorithm (algorithm);
    if (engine == kNonLinAlgorithm)
        return nonLinHears (paramId);

    const EngineType type = getAlgorithmConfig (engine).engine;
    const bool reverse = type == EngineType::ReverseRoom, parallel = type == EngineType::ParallelMultiband;
    switch (paramId)
    {
        case TonalCorrection:   return runsOctaveNetwork (type);
        case GateEnabled:       return type == EngineType::NonLinear;
        case Freeze:            return algorithmFreezes (engine);
        case ErLevel:
        case ErSize:            return hasSharedEarlyReflections (type);
        case Diffusion:         return type != EngineType::DenseHall;
        case Size:              return ! parallel;
        case ModDepth:
        case ModRate:           return ! reverse && ! parallel;
        case Saturation:        return ! reverse && ! parallel && type != EngineType::DenseHall;
        case Character:         return ! reverse && ! parallel;   // more modulation and drive
        default:                break;
    }

    if (! isDampingCardParam (paramId))
        return true;
    switch (type)
    {
        case EngineType::ReverseRoom:                           // every damping setter is empty
            return false;
        case EngineType::Spring:                                // output level and the HF damper;
            return paramId == BassMult || paramId == Damping;   // no mid band, no crossovers
        default:
            return true;
    }
}

} // namespace duskverb
