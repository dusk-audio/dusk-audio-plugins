// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// DuskVerb 2's engine list: the engines it shares with the legacy plugin (AlgorithmConfig.h,
// indices 0-15, frozen) followed by the engines only DuskVerb 2 has, inside a fixed number of
// slots. Slots 1 and 3 keep their names but play DuskVerb 2's own vintage plate and chamber
// (SectionReverbEngine) in place of the shared ones; the legacy plugin keeps its own. Every DAF
// call site that needs an algorithm's name, visibility or control labels asks here, never
// getAlgorithmConfig() directly: an own engine has no shared row it can use. Every lookup
// accepts any value, as one restored from a session or automation may be anything.

#pragma once

#include "../src/dsp/AlgorithmConfig.h"

namespace duskverb
{

inline constexpr int kNumSharedAlgorithms = 16;
inline constexpr int kSectionPlateAlgorithm = 1;    // the vintage plate (SectionReverbEngine, Vintage Plate model)
inline constexpr int kChamberAlgorithm = 3;      // the chamber (SectionReverbEngine, Chamber model)
inline constexpr int kNonLinAlgorithm = 16;      // the classic nonlinear burst (NonLinBurstEngine)
// The longest Decay that burst honours (NonLinBurstEngine::kMaxDecaySeconds, asserted equal in
// DuskVerbDSP.cpp), here for the editor and host text, which do not include the engine.
inline constexpr float kNonLinMaxDecaySeconds = 2.5f;
inline constexpr int kRoomAlgorithm = 17;        // the room (SectionReverbEngine, Room model)
inline constexpr int kNumAlgorithms = 18;        // engines that exist

// The size of the Algorithm parameter as hosts see it. VST3 stores automation as a fraction
// of a parameter's range, so this range must never change once a release is public: an engine
// added later takes the next Reserved slot in place, and every recorded automation lane keeps
// pointing at the same slot. 32 slots: the 18 engines, then room for 14 more.
inline constexpr int kNumAlgorithmSlots = 32;
static_assert (kNumAlgorithms <= kNumAlgorithmSlots, "every engine needs a slot");

// What a Reserved slot plays. Such a value can only come from automation drawn there or from
// a session saved by a later version whose engine this build does not have; a general-purpose
// hall is the least surprising stand-in.
inline constexpr int kReservedFallbackAlgorithm = 10;   // Hall

// The engine a slot plays.
inline constexpr int resolveAlgorithm (int slot) noexcept
{
    return slot < 0 ? 0 : (slot < kNumAlgorithms ? slot : kReservedFallbackAlgorithm);
}

// The engines SectionReverbEngine plays, and the model each one is (SectionReverbModel's values).
inline constexpr bool isSectionAlgorithm (int slot) noexcept
{
    const int engine = resolveAlgorithm (slot);
    return engine == kRoomAlgorithm || engine == kChamberAlgorithm || engine == kSectionPlateAlgorithm;
}

// An engine DuskVerb 2 runs outside the shared DuskVerbEngine: past the shared table, or one of
// the shared slots it plays with its own engine.
inline constexpr bool isOwnAlgorithm (int slot) noexcept
{
    return resolveAlgorithm (slot) >= kNumSharedAlgorithms || isSectionAlgorithm (slot);
}
inline constexpr int sectionModelFor (int slot) noexcept
{
    const int engine = resolveAlgorithm (slot);
    return engine == kChamberAlgorithm ? 1 : (engine == kSectionPlateAlgorithm ? 2 : 0);
}

// The name of the engine a slot plays.
inline const char* algorithmName (int slot) noexcept
{
    const int engine = resolveAlgorithm (slot);
    if (engine == kNonLinAlgorithm)
        return "Nonlinear";
    if (engine == kRoomAlgorithm)
        return "Room";
    if (engine == kChamberAlgorithm)
        return "Chamber";
    if (engine == kSectionPlateAlgorithm)
        return "Vintage Plate";
    return getAlgorithmConfig (engine).name;
}

// A slot as hosts list it: the engine's name, or "Reserved".
inline const char* algorithmLabel (int slot) noexcept
{
    return slot >= kNumAlgorithms && slot < kNumAlgorithmSlots ? "Reserved" : algorithmName (slot);
}

// Offered in the engine menu: the curated shared engines and every own engine, never a
// Reserved slot.
inline bool algorithmVisible (int slot) noexcept
{
    if (slot < 0 || slot >= kNumAlgorithms)
        return false;
    return isOwnAlgorithm (slot) || getAlgorithmConfig (slot).visible;
}

// Whether the engine a slot plays can hold its tail (Freeze). The reverse engine's tail is a
// feed-forward velvet FIR and the nonlinear burst has no loop: there is nothing to hold. DuskVerb
// 2's room, chamber and vintage plate have no hold either. Freeze leaves all of them, and the
// early field beside them, alone.
inline bool algorithmFreezes (int slot) noexcept
{
    const int engine = resolveAlgorithm (slot);
    return ! isOwnAlgorithm (engine) && getAlgorithmConfig (engine).engine != EngineType::ReverseRoom;
}

// The family whose control labels and value texts a slot uses. An own engine keeps the
// standard labels, so it reports a shared type that relabels nothing (FDN).
inline EngineType controlEngine (int slot) noexcept
{
    const int engine = resolveAlgorithm (slot);
    return isOwnAlgorithm (engine) ? EngineType::FDN : getAlgorithmConfig (engine).engine;
}

} // namespace duskverb
