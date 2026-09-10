// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// ModMatrix.hpp — 8-slot modulation matrix routing sources to destinations.
//
// Framework-free port of the JUCE ModMatrix.h. The source/destination orderings
// are preserved exactly so the shell's choice-parameter indices still map. All
// destinations are now consumed (see Voice.hpp / MultiSynthDSP.cpp): the JUCE
// build left LFO1Rate, LFO2Rate, EffectsMix and UnisonDetune accumulated but
// unread — the port wires every one (mandatory fix #5).

#pragma once

#include <array>

namespace msynth
{

enum class ModSource
{
    None = 0,
    LFO1,
    LFO2,
    Envelope2,     // filter envelope
    ModWheel,
    Aftertouch,
    Velocity,
    KeyTracking,
    Random,        // per-note random value
    PitchBend,
    SampleAndHold, // S&H output
    NumSources
};

enum class ModDest
{
    None = 0,
    Osc1Pitch,
    Osc2Pitch,
    Osc1PWM,
    Osc2PWM,
    FilterCutoff,
    FilterResonance,
    Amplitude,
    Pan,
    LFO1Rate,      // wired: scales LFO1 rate (1-sample-delayed, see Voice)
    LFO2Rate,      // wired: scales LFO2 rate
    EffectsMix,    // wired: scales effect wet mix (engine-global, see MultiSynthDSP)
    UnisonDetune,  // wired: scales unison detune spread
    NumDestinations
};

static constexpr int kNumModSlots = 8;
static constexpr int kNumModSources = (int)ModSource::NumSources;
static constexpr int kNumModDests   = (int)ModDest::NumDestinations;

struct ModSlot
{
    ModSource source = ModSource::None;
    ModDest   destination = ModDest::None;
    float     amount = 0.0f; // -1..+1
};

// Per-voice modulation state: current source values -> accumulated dest values.
struct ModulationState
{
    std::array<float, kNumModSources> sourceValues {};
    std::array<float, kNumModDests>   destValues {};

    void clearDestinations() noexcept { destValues.fill(0.0f); }

    float getDestValue(ModDest dest) const noexcept
    {
        const int idx = (int)dest;
        return (idx >= 0 && idx < kNumModDests) ? destValues[(size_t)idx] : 0.0f;
    }

    void setSourceValue(ModSource src, float value) noexcept
    {
        const int idx = (int)src;
        if (idx >= 0 && idx < kNumModSources)
            sourceValues[(size_t)idx] = value;
    }
};

class ModMatrix
{
public:
    ModSlot&       getSlot(int i) noexcept       { return slots[(size_t)i]; }
    const ModSlot& getSlot(int i) const noexcept { return slots[(size_t)i]; }

    void process(ModulationState& state) const noexcept
    {
        state.clearDestinations();
        for (const auto& slot : slots)
        {
            if (slot.source == ModSource::None || slot.destination == ModDest::None)
                continue;
            const float srcVal = state.sourceValues[(size_t)slot.source];
            state.destValues[(size_t)slot.destination] += srcVal * slot.amount;
        }
    }

private:
    std::array<ModSlot, kNumModSlots> slots {};
};

} // namespace msynth
