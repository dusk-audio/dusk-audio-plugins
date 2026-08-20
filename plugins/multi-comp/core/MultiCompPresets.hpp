// Copyright (C) 2026 Dusk Audio , GNU GPL v3.0 or later (see repository LICENSE).
// Framework-free factory preset data.  The values are copied from the JUCE
// factory table; the JUCE state/application types are intentionally absent.
#pragma once

#include "MultiCompParams.hpp"
#include <array>

namespace duskaudio
{

struct MultiCompPreset
{
    const char* name;
    const char* category;
    int mode;
    float threshold, ratio, attack, release, makeup, mix, sidechainHP;
    bool autoMakeup;
    int saturationMode, fetRatio, busAttack, busRelease;
    float vcaOverEasy, peakReduction;
    bool limitMode;
};

inline constexpr std::array<MultiCompPreset, 13> kMultiCompPresets = {{
    {"Smooth Opto Vocal", "Vocals", 0, -18, 4, 10, 300, 5, 100, 60, false, 0, 0, 2, 2, 0, 50, false},
    {"Vocal Presence", "Vocals", 1, -20, 4, 0.5f, 60, -11, 100, 100, false, 0, 0, 2, 2, 0, 0, false},
    {"Modern Pop Control", "Vocals", 4, -15, 4, 0.3f, 120, 3, 100, 90, true, 1, 1, 2, 2, 0, 0, false},
    {"Classic Drum Glue", "Drums", 3, -15, 4, 30, 100, 3, 100, 90, true, 0, 0, 5, 4, 0, 0, false},
    {"Room Nuke (FET All)", "Drums", 1, -24, 20, 0.8f, 150, -8, 100, 60, false, 0, 4, 2, 2, 0, 0, false},
    {"Snare Snap", "Drums", 2, -18, 4, 15, 50, 4, 100, 100, false, 1, 0, 2, 2, 0, 0, false},
    {"Rock Bass Anchor", "Bass", 1, -15, 4, 0.8f, 250, -8, 100, 40, false, 0, 0, 2, 2, 0, 0, false},
    {"Vintage Pinned Bass", "Bass", 0, -20, 4, 10, 300, 6, 100, 30, false, 0, 0, 2, 2, 0, 65, false},
    {"Acoustic Strum Tamer", "Guitars", 5, -22, 3, 2, 100, 2, 100, 80, true, 2, 0, 2, 2, 0, 0, false},
    {"Funk Rhythm Guitar", "Guitars", 1, -12, 4, 0.3f, 50, 4, 100, 100, false, 0, 0, 2, 2, 0, 0, false},
    {"Console-Style Glue", "Mix Bus", 3, -20, 4, 10, 100, 0, 100, 60, true, 0, 0, 4, 4, 0, 0, false},
    {"Gentle Master", "Mix Bus", 5, -24, 1.5f, 30, 100, 0, 100, 30, true, 2, 0, 2, 2, 0, 0, false},
    {"EDM Pump (115-130 BPM)", "Creative", 1, -10, 20, 0.1f, 250, 6, 100, 150, false, 0, 3, 2, 2, 0, 0, false}
}};

inline constexpr const char* const kMultiCompModeNames[kMultiCompModes] = {
    "Opto", "FET", "VCA", "Bus", "Studio FET", "Studio VCA", "Digital", "Multiband"
};

} // namespace duskaudio
