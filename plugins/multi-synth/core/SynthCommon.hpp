// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// SynthCommon.hpp — framework-free shared helpers for the Multi-Synth core.
//
// Zero JUCE/DPF includes. Constants, clamps, a MIDI-note frequency helper and a
// per-instance xorshift PRNG (replaces juce::Random / std::mt19937 / rand()).
// Every stateful object seeds its own PRNG so voices are decorrelated without a
// shared global generator (audio-thread safe, no rand()).

#pragma once

#include <cmath>
#include <cstdint>

namespace msynth
{

constexpr float kPi     = 3.14159265358979323846f;
constexpr float kTwoPi  = 6.28318530717958647692f;
constexpr float kHalfPi = 1.57079632679489661923f;

// juce::jlimit is jlimit(lo, hi, x); our clamp is clamp(x, lo, hi).
inline float clampf(float x, float lo, float hi) noexcept
{
    return x < lo ? lo : (x > hi ? hi : x);
}
inline int clampi(int x, int lo, int hi) noexcept
{
    return x < lo ? lo : (x > hi ? hi : x);
}
inline float maxf(float a, float b) noexcept { return a > b ? a : b; }

// 440 Hz at note 69 (A4) — replaces juce::MidiMessage::getMidiNoteInHertz.
inline float midiToHz(float note) noexcept
{
    return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
}

inline bool isBad(float x) noexcept { return std::isnan(x) || std::isinf(x); }

// xorshift32 — small, fast, allocation-free. Each owner seeds it uniquely.
struct Xorshift
{
    uint32_t s = 0x9E3779B9u;

    void seed(uint32_t v) noexcept { s = v ? v : 0x1234567u; }

    uint32_t nextU32() noexcept
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }

    // Uniform [0, 1).
    float nextFloat() noexcept { return (float)(nextU32() >> 8) * (1.0f / 16777216.0f); }
    // Uniform [-1, 1).
    float nextBipolar() noexcept { return nextFloat() * 2.0f - 1.0f; }
    // Uniform integer in [0, n).
    int nextInt(int n) noexcept { return n > 0 ? (int)(nextFloat() * (float)n) : 0; }
};

// Tempo-sync note divisions (shared by the arpeggiator and the tempo-synced
// delay). Order matches the JUCE choice parameter, so shell indices still map.
enum class ArpRateDivision
{
    Whole = 0,     // 1/1
    Half,          // 1/2
    Quarter,       // 1/4
    Eighth,        // 1/8
    Sixteenth,     // 1/16
    ThirtySecond,  // 1/32
    DottedHalf,
    DottedQuarter,
    DottedEighth,
    DottedSixteenth,
    TripletHalf,
    TripletQuarter,
    TripletEighth,
    TripletSixteenth,
    NumDivisions
};

inline double getBeatsPerStep(ArpRateDivision div) noexcept
{
    switch (div)
    {
        case ArpRateDivision::Whole:            return 4.0;
        case ArpRateDivision::Half:             return 2.0;
        case ArpRateDivision::Quarter:          return 1.0;
        case ArpRateDivision::Eighth:           return 0.5;
        case ArpRateDivision::Sixteenth:        return 0.25;
        case ArpRateDivision::ThirtySecond:     return 0.125;
        case ArpRateDivision::DottedHalf:       return 3.0;
        case ArpRateDivision::DottedQuarter:    return 1.5;
        case ArpRateDivision::DottedEighth:     return 0.75;
        case ArpRateDivision::DottedSixteenth:  return 0.375;
        case ArpRateDivision::TripletHalf:      return 4.0 / 3.0;
        case ArpRateDivision::TripletQuarter:   return 2.0 / 3.0;
        case ArpRateDivision::TripletEighth:    return 1.0 / 3.0;
        case ArpRateDivision::TripletSixteenth: return 1.0 / 6.0;
        default:                                return 1.0;
    }
}

} // namespace msynth
