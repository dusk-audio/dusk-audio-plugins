/*
  ==============================================================================

    Suede200Presets.h
    Suede 200 — Factory presets with IR-optimized coefficients

    Copyright (c) 2025 Dusk Audio - All rights reserved.

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include <array>
#include <cstring>

namespace Suede200Presets
{

struct Preset
{
    juce::String name;
    juce::String category;

    int program;        // 0-5
    float predelay;     // ms
    float reverbtime;   // seconds
    float size;         // meters
    int preechoes;      // 0 or 1
    int diffusion;      // 0=Lo, 1=Med, 2=Hi
    int rtlow;          // 0=X0.5, 1=X1.0, 2=X1.5
    int rthigh;         // 0=X0.25, 1=X0.5, 2=X1.0
    int rolloff;        // 0=3kHz, 1=7kHz, 2=10kHz
    float mix;          // 0-1

    // Optimized WCS coefficients (16 C-codes + rolloff frequency).
    // When hasOptimizedCoeffs is true, the reverb engine should use these
    // instead of the formula-based calculation.
    bool hasOptimizedCoeffs = false;
    std::array<float, 16> coefficients = {};
    float coeffRolloffHz = 10000.0f;
};

inline std::vector<Preset> getFactoryPresets()
{
    std::vector<Preset> presets;

    // =====================================================================
    // Concert Hall (Program 0, Algorithm A) — IR-matched coefficients
    // Scores: 57-64/100 against real Lexicon 200 hardware
    // =====================================================================
    presets.push_back({ "Hall 1", "Hall", 0, 7.0f, 2.9f, 35.0f, 0, 2, 1, 1, 2, 0.35f,
        true, {{ -0.895f, +0.010f, -0.045f, +0.812f, +0.026f, -0.109f,
                 +0.752f, -0.397f, +0.947f, -0.200f, +0.702f, -0.255f,
                 +0.502f, +0.735f, -0.698f, +0.778f }}, 8123.0f });

    presets.push_back({ "Hall 3", "Hall", 0, 10.0f, 1.7f, 28.0f, 0, 2, 1, 1, 2, 0.35f,
        true, {{ +0.421f, +0.354f, +0.738f, +0.638f, -0.176f, -0.500f,
                 +0.212f, -0.597f, +0.188f, +0.387f, +0.480f, +0.684f,
                 +0.141f, +0.058f, -0.513f, +0.091f }}, 7636.0f });

    presets.push_back({ "Hall 4", "Hall", 0, 12.0f, 2.2f, 32.0f, 0, 2, 1, 1, 2, 0.35f,
        true, {{ +0.883f, +0.596f, +0.328f, +0.912f, -0.471f, -0.452f,
                 +0.354f, -0.664f, +0.007f, -0.511f, +0.699f, +0.757f,
                 +0.228f, -0.091f, +0.046f, +0.449f }}, 5087.0f });

    presets.push_back({ "Hall 5", "Hall", 0, 15.0f, 1.8f, 30.0f, 0, 2, 1, 1, 1, 0.35f,
        true, {{ +0.474f, -0.609f, -0.628f, +0.840f, -0.899f, -0.523f,
                 +0.324f, -0.189f, -0.159f, +0.307f, +0.569f, +0.584f,
                 +0.577f, +0.866f, -0.455f, -0.698f }}, 2292.0f });

    presets.push_back({ "Hall 9", "Hall", 0, 8.0f, 3.0f, 40.0f, 0, 2, 1, 1, 2, 0.35f,
        true, {{ +0.958f, -0.292f, +0.739f, +0.838f, +0.498f, +0.064f,
                 +0.270f, -0.559f, -0.175f, -0.980f, +0.165f, +0.593f,
                 -0.019f, -0.388f, +0.888f, -0.479f }}, 2043.0f });

    // =====================================================================
    // Plate (Program 1, Algorithm B) — IR-matched coefficients
    // Scores: 69-74/100 against real Lexicon 200 hardware
    // Best program match: RT60 99-100%, Band RT60 92-99%, Stereo 97-100%
    // =====================================================================
    presets.push_back({ "Plate 1", "Plate", 1, 5.0f, 1.6f, 18.0f, 0, 2, 1, 1, 2, 0.35f,
        true, {{ -0.390f, -0.747f, -0.695f, +0.417f, +0.209f, -0.940f,
                 +0.353f, +0.602f, -0.501f, +0.271f, +0.217f, +0.829f,
                 -0.223f, -0.932f, +0.631f, +0.652f }}, 8908.0f });

    presets.push_back({ "Plate 5", "Plate", 1, 21.0f, 1.0f, 14.0f, 0, 2, 1, 1, 2, 0.35f,
        true, {{ +0.139f, +0.432f, -0.635f, +0.519f, -0.866f, -0.863f,
                 +0.751f, +0.876f, -0.743f, +0.160f, +0.552f, -0.963f,
                 -0.085f, -0.420f, +0.253f, -0.971f }}, 8430.0f });

    presets.push_back({ "Plate 6", "Plate", 1, 21.0f, 0.7f, 12.0f, 0, 2, 1, 1, 0, 0.30f,
        true, {{ -0.464f, +0.130f, -0.354f, +0.965f, -0.154f, -0.823f,
                 +0.507f, -0.252f, +0.227f, -0.314f, +0.114f, +0.480f,
                 +0.381f, -0.930f, +0.144f, +0.989f }}, 2000.0f });

    presets.push_back({ "Plate 7", "Plate", 1, 4.0f, 0.9f, 14.0f, 0, 2, 1, 1, 1, 0.30f,
        true, {{ -0.025f, +0.940f, -0.068f, +0.503f, +0.251f, -0.150f,
                 +0.921f, -0.116f, +0.583f, -0.837f, +0.898f, -0.713f,
                 +0.089f, +0.246f, -0.411f, +0.429f }}, 3132.0f });

    presets.push_back({ "Plate 9", "Plate", 1, 21.0f, 0.3f, 10.0f, 0, 2, 1, 1, 2, 0.30f,
        true, {{ -0.038f, -0.615f, -0.136f, +0.697f, +0.721f, +0.738f,
                 +0.920f, -0.735f, -0.388f, -0.319f, +0.131f, -0.743f,
                 -0.194f, +0.725f, +0.154f, -0.723f }}, 8760.0f });

    // =====================================================================
    // Chamber (Program 2, Algorithm C) — IR-matched coefficients
    // Scores: 37-45/100 (lower match due to Algorithm C complexity)
    // =====================================================================
    presets.push_back({ "Chamber 1", "Chamber", 2, 3.0f, 2.9f, 30.0f, 0, 1, 1, 1, 1, 0.30f,
        true, {{ +0.990f, -0.990f, +0.127f, +0.627f, -0.733f, -0.401f,
                 +0.815f, -0.073f, -0.217f, +0.990f, +0.650f, -0.601f,
                 +0.460f, -0.840f, -0.746f, -0.990f }}, 3885.0f });

    presets.push_back({ "Chamber 4", "Chamber", 2, 5.0f, 2.2f, 25.0f, 0, 1, 1, 1, 1, 0.30f,
        true, {{ +0.589f, -0.862f, +0.854f, +0.740f, +0.814f, +0.974f,
                 +0.383f, +0.399f, -0.752f, +0.640f, +0.275f, -0.271f,
                 -0.020f, +0.120f, -0.401f, +0.886f }}, 3760.0f });

    presets.push_back({ "Chamber 7", "Chamber", 2, 3.0f, 1.1f, 18.0f, 0, 1, 1, 1, 1, 0.30f,
        true, {{ -0.292f, +0.874f, +0.297f, +0.930f, -0.696f, -0.979f,
                 +0.758f, -0.392f, +0.922f, +0.715f, +0.933f, +0.368f,
                 +0.000f, +0.652f, -0.160f, -0.874f }}, 4989.0f });

    // =====================================================================
    // Generic presets (Programs 3-5: engine needs debugging for these)
    // These use formula-based coefficients until the engine is fixed
    // =====================================================================
    presets.push_back({ "Rich Vocal Plate", "Plate",   3,  0.0f, 2.5f, 20.0f, 0, 2, 2, 1, 2, 0.35f });
    presets.push_back({ "Wide Splits",      "Splits",  4, 25.0f, 2.0f, 28.0f, 1, 2, 1, 1, 2, 0.35f });
    presets.push_back({ "Reverse Wash",     "Inverse", 5,  0.0f, 1.2f, 16.0f, 0, 1, 1, 0, 1, 0.40f });

    return presets;
}

inline void applyPreset(juce::AudioProcessorValueTreeState& apvts, const Preset& preset)
{
    if (auto* p = apvts.getParameter("program"))     p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(preset.program)));
    if (auto* p = apvts.getParameter("predelay"))     p->setValueNotifyingHost(p->convertTo0to1(preset.predelay));
    if (auto* p = apvts.getParameter("reverbtime"))   p->setValueNotifyingHost(p->convertTo0to1(preset.reverbtime));
    if (auto* p = apvts.getParameter("size"))          p->setValueNotifyingHost(p->convertTo0to1(preset.size));
    if (auto* p = apvts.getParameter("preechoes"))    p->setValueNotifyingHost(preset.preechoes > 0 ? 1.0f : 0.0f);
    if (auto* p = apvts.getParameter("diffusion"))    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(preset.diffusion)));
    if (auto* p = apvts.getParameter("rtlow"))        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(preset.rtlow)));
    if (auto* p = apvts.getParameter("rthigh"))       p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(preset.rthigh)));
    if (auto* p = apvts.getParameter("rolloff"))      p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(preset.rolloff)));
    if (auto* p = apvts.getParameter("mix"))          p->setValueNotifyingHost(p->convertTo0to1(preset.mix));
}

} // namespace Suede200Presets
