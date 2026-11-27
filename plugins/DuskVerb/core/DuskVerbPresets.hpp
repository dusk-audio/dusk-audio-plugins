// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// DuskVerb 2's factory presets: the list it shares with the legacy plugin (FactoryPresets.h,
// frozen) followed by the categories only DuskVerb 2 has. Every DAF call site reads this, never
// getFactoryPresets() directly, so a preset of an engine only DuskVerb 2 has cannot reach the
// legacy plugin. Own categories are appended whole, after the shared list, which keeps each
// category contiguous (the browser opens a section per category) and leaves every shared
// preset's program number where it was. A shared preset's parameters reach the host through
// collectDv2Parameters(), which is its row's except where Decay is calibrated to read the tail
// the preset makes (kDecayShownByName) and where DuskVerb 2 sets its own value (kPresetOverrides).

#pragma once

#include "DuskVerbAlgorithms.hpp"
#include "DuskVerbSectionPresets.hpp"
#include "DuskVerbParamTable.hpp"
#include "../src/FactoryPresets.h"

#include <array>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

namespace duskverb
{

// Older imports and saved sessions carry a chamber identity and a room voicing tag.
inline constexpr std::string_view kBigDrumRoomBase = "79 Vocal Chamber";
// The current import has its own identity. It does not occupy a host program number.
inline constexpr FactoryPreset kBigDrumRoomPreset {
    "Big Drum Room", "Rooms", kRoomAlgorithm, 1.0f, true, 0.0f, 0, 1.32f, 0.90f,
    0.30f, 0.20f, 1.0f, 1.0f, 2325.0f, 1.0f, 0.50f, 0.75f, 5.0f, 20000.0f,
    1.0f, false, 0.0f, 20.0f, 1.0f, 4000.0f, 0.0f, -0.1f
};

// The classic nonlinear burst. Decay is the time to -60 dB; the four lengths are the engine's
// burst lengths (its shortest, two middle and longest).
// Field order: name, category, algorithm, mix, busMode, predelay, predelaySync, decay, size,
// modDepth, modRate, damping, bassMult, crossover, diffusion, erLevel, erSize, loCut, hiCut,
// width, freeze, gainTrim.
inline const std::vector<FactoryPreset>& ownFactoryPresets()
{
    static const std::vector<FactoryPreset> presets = [] {
        std::vector<FactoryPreset> bank {
            { "Nonlinear Small",  "Nonlinear", kNonLinAlgorithm, 1.00f, false, 0.0f, 0,
              0.27f, 0.5f, 0.0f, 0.5f, 1.0f, 1.0f, 500.0f, 0.5f, 0.0f, 0.0f, 20.0f, 20000.0f, 1.0f, false, 0.0f },
            { "Nonlinear Medium", "Nonlinear", kNonLinAlgorithm, 1.00f, false, 0.0f, 0,
              0.29f, 0.5f, 0.0f, 0.5f, 1.0f, 1.0f, 500.0f, 0.5f, 0.0f, 0.0f, 20.0f, 20000.0f, 1.0f, false, 0.0f },
            { "Nonlinear Large",  "Nonlinear", kNonLinAlgorithm, 1.00f, false, 0.0f, 0,
              0.36f, 0.5f, 0.0f, 0.5f, 1.0f, 1.0f, 500.0f, 0.5f, 0.0f, 0.0f, 20.0f, 20000.0f, 1.0f, false, -1.0f },
            { "Nonlinear Huge",   "Nonlinear", kNonLinAlgorithm, 1.00f, false, 0.0f, 0,
              0.46f, 0.5f, 0.0f, 0.5f, 1.0f, 1.0f, 500.0f, 0.5f, 0.0f, 0.0f, 20.0f, 20000.0f, 1.0f, false, -0.7f },
        };
        struct Family { int algorithm; const char* category; };
        constexpr Family families[] {
            {kRoomAlgorithm, "Rooms II"}, {kChamberAlgorithm, "Chambers II"},
            {kSectionPlateAlgorithm, "Vintage Plates"}, {kSectionHallAlgorithm, "Halls II"},
            {kStudioPlateAlgorithm, "Studio Plates"}, {kModulatedHallAlgorithm, "Modulated Halls"},
            {kAuditoriumAlgorithm, "Auditoriums"}
        };
        static_assert(std::size(families) == kSectionReverbModels);
        for (const auto& row : kStudioPresets)
        {
            const auto family = families[row.config.model];
            bank.push_back({row.name.data(), family.category, family.algorithm, 0.35f, false,
                row.predelayMs, 0, row.decay, row.size, row.modDepth, row.modRate, row.damping,
                row.bassMult, row.crossover, row.diffusion, row.erLevel, row.erSize, 5.0f,
                20000.0f, 1.0f, false, 0.0f, 20.0f, 1.0f, 4000.0f, 0.0f, -0.1f});
        }
        return bank;
    }();
    return presets;
}

// Shared presets DuskVerb 2 plays on one of its own engines, in place (the program number and
// every other row value stay; the legacy plugin keeps its engine): the room, the chamber and the
// vintage plate (kSectionReverbByName).
inline constexpr std::array<std::pair<std::string_view, int>, 10> kOwnEngineByName {{
    { "Vocal Plate", kCompactPlateAlgorithm },
    { "Vocal Hall", kRoomAlgorithm },
    { "Cathedral Large Hall", kSectionHallAlgorithm },
    { "Live Room", kRoomAlgorithm },
    { "Medium Drum Room", kRoomAlgorithm },
    { "Tiled Room", kRoomAlgorithm },
    { "Reverse Taps", kRoomAlgorithm },
    { "Large Chamber", kChamberAlgorithm },
    { "Vintage Vocal Plate", kSectionPlateAlgorithm },
    { "Vintage Gold Plate", kSectionPlateAlgorithm },
}};

inline const std::vector<FactoryPreset>& dv2FactoryPresets()
{
    static const std::vector<FactoryPreset> presets = [] {
        std::vector<FactoryPreset> all = getFactoryPresets();
        for (auto& preset : all)
        {
            for (const auto& [name, algorithm] : kOwnEngineByName)
                if (preset.name == name)
                    preset.algorithm = algorithm;
            // This shimmer's Decay control drives the tank directly.
            if (std::string_view (preset.name) == "Deep Blue Day")
                preset.decay = 22.468516f;
        }
        const auto& own = ownFactoryPresets();
        all.insert (all.end(), own.begin(), own.end());
        return all;
    }();
    return presets;
}

// Where Decay shows the tail a preset makes instead of its row's value. A legacy row holds the
// decay its engine is given, and the shared engine config then reshapes the tail per preset
// (octave decay tables keyed to that value, band multipliers, shimmer feedback), so on these
// presets the row was more than 5 % off the measured tail. Here Decay shows the preset's
// mid-frequency RT60, T_mid (ISO 3382: the mean of the 500 Hz and 1 kHz octave-band RT60s of a
// sustained-noise release), measured 2026-10-25 on DuskVerb 2's own renders. At that value the
// engine is still given the row's decay, so the preset sounds exactly as before; any other value
// is given in proportion (DuskVerbDSP).
//
// Away from its own value the knob reads the tail to within 15 % at half and at double, except
// where part of the tail does not follow the engine's decay (measured 2026-10-25): halved, Small
// Drum Room rings longer than it reads. Doubled, 79 Vocal Chamber stops 20 % short.
// The presets on DuskVerb 2's own room, chamber and
// vintage plate were measured 2026-11-23 as the mean over six start phases: Live Room 0.604 to
// 0.681 s, Large Chamber 2.835 to 2.961 s, Vintage Gold Plate 1.752 to 1.833 s, Vintage Vocal
// Plate 0.741 to 0.818 s. Doubled they read within 9 %; halved, Large Chamber within 1 %, the two
// vintage plates 18 to 22 % and Live Room 28 % long (at that loop gain the sections' lengths and
// the early pattern, not the gain, set the tail). Across six noise releases the mid-frequency
// RT60 averaged 0.695 s for Medium Drum Room, 0.468 s for Tiled Room,
// 3.660 s for Cathedral Large Hall and 2.831 s for Vocal Hall.
//
// None on the Gated, Reverse and Nonlinear engines, where Decay sets a gate, a swell or a burst
// length rather than an exponential tail, nor on Deep Blue Day, whose tail (about 34 s) is longer
// than Decay can show. Read on the audio thread at every preset swap, like the voicing tables:
// constexpr std::array and a linear scan.
inline constexpr std::array<std::pair<std::string_view, float>, 15> kDecayShownByName {{
    { "79 Vocal Chamber",       4.03f },
    { "Ambience",               0.85f },
    { "Black Hole",             7.82f },
    { "Blade Runner 224",       11.02f },
    { "Bright Hall",            4.94f },
    { "Cathedral Large Hall",   3.66f },
    { "Large Chamber",          2.88f },
    { "Live Room",              0.64f },
    { "Medium Drum Room",       0.70f },
    { "Small Drum Room",        0.49f },
    { "Tiled Room",             0.47f },
    { "Vintage Gold Plate",     1.79f },
    { "Vintage Vocal Plate",    0.78f },
    { "Vocal Hall",             2.83f },
    { "Vocal Plate",            vocalplate::shownDecay },
}};

constexpr bool decayShownTableValid() noexcept
{
    for (auto row = kDecayShownByName.begin(); row != kDecayShownByName.end(); ++row)
    {
        if (row->first.empty() || ! (row->second > 0.0f))
            return false;
        for (auto earlier = kDecayShownByName.begin(); earlier != row; ++earlier)
            if (earlier->first == row->first)
                return false;
    }
    return true;
}
static_assert (decayShownTableValid(), "every Decay reading names its preset once and is a time");

// The Decay preset `name` shows where it is calibrated, else nullptr: it shows its row's.
inline const float* decayShownFor (std::string_view name) noexcept
{
    for (const auto& row : kDecayShownByName)
        if (row.first == name)
            return &row.second;
    return nullptr;
}

// DuskVerb 2's own value for one parameter of a shared preset, over its row's: (preset, parameter
// id, plain value). Tuned on real renders against the preset's target response, kept only where
// the failing checks drop with none pushed further off; never Decay, whose reading
// kDecayShownByName sets. The gain_trim rows set the preset's loudness instead (they change no
// check: every render is scaled to the target's level before it is measured). The legacy rows stay as they are, so the
// engine config keyed to them (octave decay references, the damping scaler's stored knobs) is the
// row's: an override moves the preset from there, as the knob would.
struct PresetOverride
{
    std::string_view preset, id;
    float value;
};

inline constexpr std::array<PresetOverride, 133> kPresetOverrides {{
    { "79 Vocal Chamber", "mod_depth", 0.647f },
    { "79 Vocal Chamber", "tank_level", 1.24f },
    { "Ambience", "size", 0.29f },
    { "Ambience", "width", 1.16f },
    { "Black Hole", "hi_cut", 11900.0f },
    { "Black Hole", "lo_cut", 26.0f },
    { "Black Hole", "bass_mult", 0.652f },
    { "Black Hole", "diffusion", 0.875f },
    { "Black Hole", "size", 0.71922f },
    { "Black Hole", "gain_trim", 9.3f },
    { "Black Hole", "width", 1.09f },
    { "Blade Runner 224", "lo_cut", 31.5f },
    { "Blade Runner 224", "width", 1.116f },
    { "Deep Blue Day", "mid_mult", 1.02f },
    { "Deep Blue Day", "diffusion", 0.79f },
    { "Vocal Plate", "predelay", 24.0f },
    { "Vocal Plate", "bass_mult", 0.682068f },
    { "Vocal Plate", "mod_depth", 0.96f },
    { "Vocal Plate", "mod_rate", 7.55f },
    { "Vocal Plate", "diffusion", 0.61075f },
    { "Vocal Plate", "mid_mult", 0.8493f },
    { "Vocal Plate", "damping", 0.8926208f },
    { "Vocal Plate", "crossover", 215.677f },
    { "Vocal Plate", "high_crossover", 5200.0f },
    { "Vocal Plate", "er_size", 0.515f },
    { "Vocal Plate", "er_level", 0.52216f },
    { "Vocal Plate", "lo_cut", 5.0f },
    { "Vocal Plate", "hi_cut", 20000.0f },
    { "Vocal Plate", "width", 0.98f },
    { "79 Vocal Chamber", "crossover", 898.375106f },
    { "Black Hole", "crossover", 227.5569f },
    { "79 Vocal Chamber", "edt_lowmid_attack_db", 3.0f },
    { "79 Vocal Chamber", "qt_air_mult", 0.15f },
    { "Ambience", "edt_midhi_attack_db", 3.0f },
    { "Deep Blue Day", "tank_level", 1.0976f },
    { "Drum Plate", "er_boost", 1.33f },
    { "Ambience", "edt_sub_attack_db", 2.4362f },
    { "Ambience", "er_decorr", 0.6128f },
    { "Cathedral Large Hall", "xtalk", 0.1665f },
    { "Drum Plate", "er_size", 0.4143f },
    { "Drum Plate", "mod_rate", 1.8f },
    { "Small Drum Room", "crossover", 617.7464f },
    { "Small Drum Room", "edt_midhi_attack_db", -3.6204f },
    { "Small Drum Room", "edt_sub_attack_db", -3.0f },
    { "Small Drum Room", "edt_air_attack_db", 3.0f },
    // 6 kHz, not the 3.6 kHz the October screen picked: the shelf began inside the 4-8 kHz band and
    // left the music 4.9 dB dull at 4-6 kHz against the target, whose roll-off starts higher. The
    // corner is the lever: a shallower shelf or a brighter early bus lifts 12-16 kHz, where the
    // preset was already at or over the target (its band limit in DuskVerbDv2Voicing.hpp now ends
    // it at 15 kHz, as the target does).
    { "Small Drum Room", "hi_cut", 6000.0f },
    { "Small Drum Room", "width", 1.05f },     // 1.135 left the music's side/mid 2.6 dB over the target with the 2026-11-20 trim
    // Mid decay up by one step (2026-11-20): with the hit on the early
    // field the tank let go of 500 Hz-8 kHz 23-25 % early on sustained sound; this closes those decay
    // gates and the tail envelope with them.
    { "Small Drum Room", "mid_mult", 1.400689f },
    { "Vocal Plate", "hi_cut_shelf_db", -0.1f },
    { "Deep Blue Day", "lo_cut", 18.8475f },
    // The snare's loudness
    // (BS.1770: K-weighted, both channels) at the fleet mix equals the target's. gain_trim is a
    // wet-path gain, so the match holds at any mix. The parameter steps in 0.1 dB (the rows' values
    // snap to it too), so these are written at the step. Presets within 0.5 LU keep their row's trim.
    { "Vocal Plate", "gain_trim", 5.1f },
    { "Drum Plate", "gain_trim", 8.9f },               // row 6.87, -2.0
    { "Bright Hall", "gain_trim", 3.6f },              // row 5.78, +2.2
    { "Blade Runner 224", "gain_trim", 7.7f },         // snare and music trim reduced by 2.1 dB
    { "79 Vocal Chamber", "gain_trim", -2.0f },        // snare level with the stronger late tank
    { "Small Drum Room", "gain_trim", 7.9f },          // row 12.19, +4.2: snare +0.5 LU, music -0.7 LU
    { "Ambience", "gain_trim", 3.6f },                 // snare level at the wider room setting
    // Per-preset snare and music matches, checked against the full parity sheet.
    { "Deep Blue Day", "width", 1.14f },
    { "Deep Blue Day", "hi_cut", 14287.3594f },
    { "Deep Blue Day", "hi_cut_shelf_db", -13.4396973f },
    { "Deep Blue Day", "predelay", 20.4054508f },
    { "Deep Blue Day", "damping", 0.961438954f },
    { "Deep Blue Day", "er_level", 0.203092337f },
    // The presets on DuskVerb 2's own room, chamber and vintage plate (kOwnEngineByName): Pre-Delay
    // shows the engine's own; the engine's output filters shape the top, so Hi Cut sits at 20 kHz
    // with a shelf all but transparent there (not 0, which leaves Hi Cut nothing to move); Lo Cut is
    // off (the engines set their own low end, down to content under 10 Hz); Width is neutral (they
    // build their own stereo field); and the trim is 0 dB, as they render at their target's level.
    { "Vocal Hall", "size", kVocalHallRoom.size },
    { "Vocal Hall", "predelay", kVocalHallRoom.predelayMs },
    { "Vocal Hall", "damping", kVocalHallRoom.damping },
    { "Vocal Hall", "bass_mult", kVocalHallRoom.bassMult },
    { "Vocal Hall", "crossover", kVocalHallRoom.crossover },
    { "Vocal Hall", "mod_depth", kVocalHallRoom.modDepth },
    { "Vocal Hall", "mod_rate", kVocalHallRoom.modRate },
    { "Vocal Hall", "diffusion", kVocalHallRoom.diffusion },
    { "Vocal Hall", "er_level", kVocalHallRoom.erLevel },
    { "Vocal Hall", "er_size", kVocalHallRoom.erSize },
    { "Vocal Hall", "lo_cut", 5.0f },
    { "Vocal Hall", "hi_cut", 20000.0f },
    { "Vocal Hall", "hi_cut_shelf_db", -0.1f },
    { "Vocal Hall", "width", 1.0f },
    { "Vocal Hall", "gain_trim", 0.0f },
    { "Vocal Hall", "mid_mult", 1.0f },
    { "Vocal Hall", "high_crossover", 4000.0f },
    { "Cathedral Large Hall", "predelay", 21.0f },
    { "Cathedral Large Hall", "size", 0.825f },
    { "Cathedral Large Hall", "damping", 1.0f },
    { "Cathedral Large Hall", "bass_mult", 1.0f },
    { "Cathedral Large Hall", "crossover", 850.0f },
    { "Cathedral Large Hall", "mod_depth", 0.88f },
    { "Cathedral Large Hall", "mod_rate", 1.8f },
    { "Cathedral Large Hall", "diffusion", 0.19f },
    { "Cathedral Large Hall", "er_level", 0.50f },
    { "Cathedral Large Hall", "er_size", 0.50f },
    { "Cathedral Large Hall", "hi_cut", 20000.0f },
    { "Cathedral Large Hall", "hi_cut_shelf_db", -0.1f },
    { "Cathedral Large Hall", "lo_cut", 5.0f },
    { "Cathedral Large Hall", "width", 1.0f },
    { "Cathedral Large Hall", "gain_trim", 0.0f },
    { "Medium Drum Room", "predelay", 3.0f },
    { "Medium Drum Room", "width", 1.0f },
    { "Medium Drum Room", "hi_cut", 20000.0f },
    { "Medium Drum Room", "hi_cut_shelf_db", -0.1f },
    { "Medium Drum Room", "lo_cut", 5.0f },
    { "Medium Drum Room", "gain_trim", 0.0f },
    { "Tiled Room", "predelay", 0.0f },
    { "Tiled Room", "mod_depth", 0.29f },
    { "Tiled Room", "hi_cut", 20000.0f },
    { "Tiled Room", "hi_cut_shelf_db", -0.5f },
    { "Tiled Room", "lo_cut", 5.0f },
    { "Tiled Room", "gain_trim", 0.0f },
    { "Live Room", "predelay", 3.0f },
    { "Live Room", "width", 1.0f },
    { "Live Room", "hi_cut", 20000.0f },
    { "Live Room", "hi_cut_shelf_db", -0.5f },
    { "Live Room", "lo_cut", 5.0f },
    { "Live Room", "gain_trim", 0.0f },
    { "Reverse Taps", "predelay", 0.0f },
    { "Reverse Taps", "width", 1.0f },
    { "Reverse Taps", "hi_cut", 20000.0f },
    { "Reverse Taps", "hi_cut_shelf_db", -0.5f },
    { "Reverse Taps", "lo_cut", 5.0f },
    { "Reverse Taps", "gain_trim", 0.0f },
    { "Large Chamber", "predelay", 6.0f },
    { "Large Chamber", "hi_cut", 20000.0f },
    { "Large Chamber", "hi_cut_shelf_db", -0.5f },
    { "Large Chamber", "lo_cut", 5.0f },
    { "Large Chamber", "gain_trim", 0.0f },
    { "Vintage Vocal Plate", "predelay", 20.0f },
    { "Vintage Vocal Plate", "hi_cut", 20000.0f },
    { "Vintage Vocal Plate", "hi_cut_shelf_db", -0.5f },
    { "Vintage Vocal Plate", "lo_cut", 5.0f },
    { "Vintage Vocal Plate", "gain_trim", 0.0f },
    { "Vintage Gold Plate", "hi_cut", 20000.0f },
    { "Vintage Gold Plate", "hi_cut_shelf_db", -0.5f },
    { "Vintage Gold Plate", "lo_cut", 5.0f },
    { "Vintage Gold Plate", "gain_trim", 0.0f },
}};

constexpr bool presetOverridesValid() noexcept
{
    for (auto row = kPresetOverrides.begin(); row != kPresetOverrides.end(); ++row)
    {
        if (row->preset.empty() || row->id.empty() || row->id == "decay")
            return false;
        for (auto earlier = kPresetOverrides.begin(); earlier != row; ++earlier)
            if (earlier->preset == row->preset && earlier->id == row->id)
                return false;
    }
    return true;
}
static_assert (presetOverridesValid(), "every override names a preset and a parameter once, and never Decay");

// Emits `preset`'s parameters as DuskVerb 2 recalls them, (parameter id, PLAIN value) in the
// order FactoryPreset::collectParameters gives them: the row's, with Decay at its reading where
// kDecayShownByName has one and kPresetOverrides' values over the rest. Every DAF path that
// turns a factory preset into parameter values goes through here, never through
// collectParameters() directly.
template <typename SetFn>
void collectDv2Parameters (const FactoryPreset& preset, SetFn&& set)
{
    const std::string_view name (preset.name);
    const float* shown = decayShownFor (name);
    const char* decayId = paramDesc (Decay).id;
    preset.collectParameters ([&] (const char* id, float plain) {
        if (shown != nullptr && std::strcmp (id, decayId) == 0)
            plain = *shown;
        for (const auto& row : kPresetOverrides)
            if (row.preset == name && row.id == id)
                plain = row.value;
        set (id, plain);
    });
}

} // namespace duskverb
