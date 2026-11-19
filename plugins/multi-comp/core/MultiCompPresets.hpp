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
    int fetRatio, busAttack, busRelease;   // fetRatio: the mode's own Ratio position (FET BLUE's 3 is 8:1)
    float vcaOverEasy, peakReduction;
    bool limitMode;
};

// For FET BLACK (mode 1) and FET BLUE (4), attack and release are the knobs' positions in their legacy plain domains,
// milliseconds in name only (fetAttackPositionFromMilliseconds, fetReleasePositionFromMilliseconds): the face's dial
// reads 1 + 6 * position, 7 the fastest. The values copied from the JUCE table were times, and as positions they put
// every FET preset at about Attack 2 and Release 1 to 2, the slow end (2026-11-06). Each now sets the dials in its
// comment; testFetPresetsSetTheirDials pins them.
inline constexpr std::array<MultiCompPreset, 13> kMultiCompPresets = {{
    {"Smooth Opto Vocal", "Vocals", 0, -18, 4, 10, 300, 5, 100, 60, false, 0, 2, 2, 0, 50, false},
    {"Vocal Presence", "Vocals", 1, -20, 4, 2.0739f, 1100, -11, 100, 100, false, 0, 2, 2, 0, 0, false},   // A 3, R 7
    {"Modern Pop Control", "Vocals", 4, -15, 4, 7.955f, 750, 3, 100, 90, true, 3, 2, 2, 0, 0, false},   // A 4, R 5
    {"Classic Drum Glue", "Drums", 3, -15, 4, 30, 100, 3, 100, 90, true, 0, 5, 4, 0, 0, false},
    {"Room Nuke (FET All)", "Drums", 1, -24, 20, 43.5756f, 1100, -8, 100, 60, false, 4, 2, 2, 0, 0, false},   // A 6, R 7
    {"Snare Snap", "Drums", 2, -18, 4, 15, 50, 4, 100, 100, false, 0, 2, 2, 0, 0, false},
    {"Rock Bass Anchor", "Bass", 1, -15, 4, 2.0739f, 575, -8, 100, 40, false, 0, 2, 2, 0, 0, false},   // A 3, R 4
    {"Vintage Pinned Bass", "Bass", 0, -20, 4, 10, 300, 6, 100, 30, false, 0, 2, 2, 0, 65, false},
    {"Acoustic Strum Tamer", "Guitars", 5, -22, 3, 2, 100, 2, 100, 80, true, 0, 2, 2, 0, 0, false},
    {"Funk Rhythm Guitar", "Guitars", 1, -12, 4, 7.955f, 1100, 4, 100, 100, false, 0, 2, 2, 0, 0, false},   // A 4, R 7
    {"Console-Style Glue", "Mix Bus", 3, -20, 4, 10, 100, 0, 100, 60, true, 0, 4, 4, 0, 0, false},
    {"Gentle Master", "Mix Bus", 5, -24, 1.5f, 30, 100, 0, 100, 30, true, 0, 2, 2, 0, 0, false},
    {"EDM Pump (115-130 BPM)", "Creative", 1, -10, 20, 43.5756f, 575, 6, 100, 150, false, 3, 2, 2, 0, 0, false}   // A 6, R 4
}};

inline constexpr const char* const kMultiCompModeNames[kMultiCompModes] = {
    "Opto", "FET Black", "VCA", "Bus", "FET Blue", "Studio VCA", "Digital", "Multiband"
};

} // namespace duskaudio
