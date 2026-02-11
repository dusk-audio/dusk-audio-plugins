/*
  ==============================================================================

    Suede200Programs.h
    Suede 200 — Program data

    Contains the 6 reverb program definitions. Each program defines a WCS
    microcode topology that controls the custom DSP hardware.

    Copyright (c) 2025 Dusk Audio - All rights reserved.

  ==============================================================================
*/

#pragma once

#include <array>
#include <cstdint>

namespace Suede200
{

//==============================================================================
// Program names matching original Suede 200 front panel
static constexpr const char* programNames[] = {
    "Concert Hall",   // Program 1 — Algorithm A
    "Plate",          // Program 2 — Algorithm B
    "Chamber",        // Program 3 — Algorithm C
    "Rich Plate",     // Program 4 — Algorithm C variant
    "Rich Splits",    // Program 5 — Algorithm C variant
    "Inverse Rooms",  // Program 6 — Algorithm C variant
};

static constexpr int NUM_PROGRAMS = 6;

//==============================================================================
// Algorithm topology identifiers
enum class AlgorithmType
{
    A = 0,  // Concert Hall: 97 active steps, 7 diffusion + 25 FDN taps
    B = 1,  // Plate: 104 active steps, 10 diffusion + 32 FDN taps
    C = 2,  // Chamber family: 83-89 active steps, 6-7 diffusion + 19-26 FDN taps
};

//==============================================================================
// Map program index (0-5) to algorithm type
static constexpr AlgorithmType programAlgorithm[] = {
    AlgorithmType::A,  // Concert Hall
    AlgorithmType::B,  // Plate
    AlgorithmType::C,  // Chamber
    AlgorithmType::C,  // Rich Plate
    AlgorithmType::C,  // Rich Splits
    AlgorithmType::C,  // Inverse Rooms
};

//==============================================================================
// Factory preset parameter sets for each program
// Original Model 200 has 10 factory presets per program.
// These are representative defaults (full set in Phase 3).
struct ProgramDefaults
{
    float preDelayMs;
    float reverbTimeSec;
    float sizeMeters;
    int diffusion;       // 0=Lo, 1=Med, 2=Hi
    int rtContourLow;    // 0=X0.5, 1=X1.0, 2=X1.5
    int rtContourHigh;   // 0=X0.25, 1=X0.5, 2=X1.0
    int rolloff;         // 0=3kHz, 1=7kHz, 2=10kHz
    bool preEchoes;
};

static constexpr ProgramDefaults programDefaults[] = {
    // Concert Hall: large room, long tail, warm
    { 39.0f, 2.5f, 30.0f, 2, 1, 1, 2, false },
    // Plate: no pre-delay, bright, dense
    { 0.0f,  1.8f, 18.0f, 2, 1, 2, 2, false },
    // Chamber: medium room, natural
    { 15.0f, 1.5f, 22.0f, 1, 1, 1, 1, false },
    // Rich Plate: lush, extended decay
    { 0.0f,  3.0f, 20.0f, 2, 2, 1, 2, false },
    // Rich Splits: wide stereo, complex early reflections
    { 25.0f, 2.0f, 28.0f, 2, 1, 1, 2, true  },
    // Inverse Rooms: reverse envelope
    { 0.0f,  1.2f, 16.0f, 1, 1, 0, 1, false },
};

} // namespace Suede200
