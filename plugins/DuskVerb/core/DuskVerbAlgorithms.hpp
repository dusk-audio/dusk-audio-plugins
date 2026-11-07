// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// DuskVerb 2's engine list: the engines it shares with the legacy plugin (AlgorithmConfig.h,
// indices 0-15, frozen) followed by the engines only DuskVerb 2 has, inside a fixed number of
// slots. Every DAF call site that needs an algorithm's name, visibility or control labels asks
// here, never getAlgorithmConfig() directly: an own engine's index is past the end of the shared
// table. Every lookup accepts any value, as one restored from a session or automation may be
// anything.

#pragma once

#include "../src/dsp/AlgorithmConfig.h"

namespace duskverb
{

inline constexpr int kNumSharedAlgorithms = 16;
inline constexpr int kNonLinAlgorithm = 16;      // the classic nonlinear burst (NonLinBurstEngine)
inline constexpr int kNumAlgorithms = 17;        // engines that exist

// The size of the Algorithm parameter as hosts see it. VST3 stores automation as a fraction
// of a parameter's range, so this range must never change once a release is public: an engine
// added later takes the next Reserved slot in place, and every recorded automation lane keeps
// pointing at the same slot. 32 slots: the 17 engines, then room for 15 more.
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

inline constexpr bool isOwnAlgorithm (int slot) noexcept { return resolveAlgorithm (slot) >= kNumSharedAlgorithms; }

// The name of the engine a slot plays.
inline const char* algorithmName (int slot) noexcept
{
    const int engine = resolveAlgorithm (slot);
    return engine == kNonLinAlgorithm ? "Nonlinear" : getAlgorithmConfig (engine).name;
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
    return slot >= kNumSharedAlgorithms || getAlgorithmConfig (slot).visible;
}

// Whether the engine a slot plays can hold its tail (Freeze). The reverse engine's tail is a
// feed-forward velvet FIR and the nonlinear burst has no loop: there is nothing to hold, so
// Freeze leaves them, and the early field beside them, alone.
inline bool algorithmFreezes (int slot) noexcept
{
    const int engine = resolveAlgorithm (slot);
    return engine < kNumSharedAlgorithms && getAlgorithmConfig (engine).engine != EngineType::ReverseRoom;
}

// The family whose control labels and value texts a slot uses. An own engine keeps the
// standard labels, so it reports a shared type that relabels nothing (FDN).
inline EngineType controlEngine (int slot) noexcept
{
    const int engine = resolveAlgorithm (slot);
    return engine >= kNumSharedAlgorithms ? EngineType::FDN : getAlgorithmConfig (engine).engine;
}

} // namespace duskverb
