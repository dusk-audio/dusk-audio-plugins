// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
#pragma once

#include "DuskVerbPresets.hpp"
#include <cstdlib>

namespace duskverb::test
{

inline const FactoryPreset& sharedPresetNamed (std::string_view name)
{
    for (const auto& preset : getFactoryPresets())
        if (preset.name == name)
            return preset;
    std::abort();
}

// A host restoring an older session writes its saved values before applying the preset identity.
// Recreate the pre-port parameters for tests of the old PMB and Tiled Room engines.
template <typename SetFn>
void collectSavedOrFactoryParameters (const FactoryPreset& preset, SetFn&& set)
{
    const std::string_view name = preset.name;
    if (preset.algorithm == kRoomAlgorithm || (name != "Medium Drum Room" && name != "Tiled Room" && name != "Vocal Hall"))
    {
        collectDv2Parameters (preset, set);
        return;
    }
    static constexpr PresetOverride saved[] {
        { "Medium Drum Room", "decay", 0.78f },
        { "Medium Drum Room", "mono_below", 40.0f },
        { "Medium Drum Room", "lo_cut", 17.499996f },
        { "Medium Drum Room", "predelay", 8.1f },
        { "Medium Drum Room", "gain_trim", 14.1f },
        { "Vocal Hall", "decay", 3.64f },
        { "Vocal Hall", "high_crossover", 3452.854f },
        { "Vocal Hall", "lo_cut", 28.74f },
        { "Vocal Hall", "predelay", 9.5f },
        { "Vocal Hall", "gain_trim", 11.2f },
        { "Tiled Room", "bass_mult", 1.51685f },
        { "Tiled Room", "edt_sub_attack_db", -3.0f },
    };
    preset.collectParameters ([&] (const char* id, float value) {
        for (const auto& row : saved)
            if (row.preset == name && row.id == id)
                value = row.value;
        set (id, value);
    });
}

} // namespace duskverb::test
