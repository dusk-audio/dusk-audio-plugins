// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// ModMatrix.hpp — 8-slot modulation matrix routing sources to destinations.
//
// Framework-free port of the JUCE ModMatrix.h. The source/destination orderings
// are preserved exactly so the shell's choice-parameter indices still map. All
// destinations are now consumed (see Voice.hpp / MultiSynthDSP.cpp): the JUCE
// build left LFO1Rate, LFO2Rate, EffectsMix and UnisonDetune accumulated but
// unread — the port wires every one (mandatory fix #5).
//
// The enums are APPEND-ONLY. The destination index is a host-automatable choice
// parameter (modDst0..7) and a value in every saved session and preset, so an
// enumerator inserted mid-list would silently re-route every patch after it.

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
    // Appended for issue #303 (the classic "mod wheel brings in vibrato" patch had
    // no destination to land on). Scale an LFO's output; see
    // SynthVoice::applyLfoDepth for the exact rule.
    LFO1Depth,
    LFO2Depth,
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

    // Whether any slot process() reads lands on `dest` (a source set; the amount
    // does not matter, a zero amount still contributes its zero). For the LFO depth
    // destinations this is the rule that decides whether a depth is applied at all,
    // so a depth slot at 0 % holds its LFO at zero. The editor's "N active" badge
    // (MultiSynthUI.cpp, activeModSlots) counts depth slots by the same rule;
    // lfo_depth_gate's [rule] checks pin the engine side.
    bool targets(ModDest dest) const noexcept
    {
        for (const auto& slot : slots)
            if (slot.destination == dest && slot.source != ModSource::None)
                return true;
        return false;
    }

    // What process() would accumulate into `dest` from the source values as they
    // stand now (same slots, same order, same arithmetic), without touching
    // destValues. The LFO depth destinations need their sum BEFORE the LFO sources
    // they scale are handed to process() (SynthVoice::applyLfoDepth).
    float sumFor(const ModulationState& state, ModDest dest) const noexcept
    {
        float sum = 0.0f;
        for (const auto& slot : slots)
            if (slot.destination == dest && slot.source != ModSource::None)
                sum += state.sourceValues[(size_t)slot.source] * slot.amount;
        return sum;
    }

private:
    std::array<ModSlot, kNumModSlots> slots {};
};

} // namespace msynth
