// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Ring Out's preset record and file reader, shared by the editor and its tests.
#pragma once

#include "RingOutParams.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <locale>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

struct RoUserPreset
{
    std::string name, path;
    float vals[kParamCount] = {};
    duskaudio::ringout::FilterTable filters;
};

inline bool roParsePresetValue(const std::string& line, std::size_t valueStart, uint32_t param, float& out)
{
    std::istringstream field(line.substr(valueStart));
    field.imbue(std::locale::classic());
    double d = 0.0;
    field >> d;
    if (field.fail() || !std::isfinite(d)) return false;
    char trailing = '\0';
    if (field >> trailing) return false;
    // Reject conversion overflow before narrowing; domain clamping stays the
    // same as the host parameter path.
    if (d < -std::numeric_limits<float>::max() || d > std::numeric_limits<float>::max()) return false;
    out = roNormalizeParamValue(param, (float)d);
    return true;
}

// The writer emits every preset parameter and a filters= line, including an
// explicit empty table. Require that complete payload before publishing it:
// defaulting a missing/corrupt field could discard a saved ring-out.
inline bool roReadUserPresetFile(const std::filesystem::path& path, RoUserPreset& up)
{
    std::ifstream input(path);
    if (!input)
        return false;
    RoUserPreset parsed = up;
    for (uint32_t i = 0; i < kParamCount; ++i) parsed.vals[i] = kRoParams[i].def;
    parsed.filters.clear();
    bool seen[kParamCount] = {};
    bool seenFilters = false;
    bool seenName = false;
    std::string line;
    while (std::getline(input, line))
    {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        if (key == "name")
        {
            if (seenName) return false;
            seenName = true;
            parsed.name = line.substr(eq + 1);
            if (!parsed.name.empty() && parsed.name.back() == '\r') parsed.name.pop_back();
            continue;
        }
        if (key == "filters")
        {
            if (seenFilters || !duskaudio::ringout::parseTable(line.c_str() + eq + 1, parsed.filters))
                return false;
            seenFilters = true;
            continue;
        }
        for (uint32_t i = 0; i < kParamCount; ++i)
            if (roIsPresetParam(i) && key == kRoParams[i].id)
            {
                if (seen[i] || !roParsePresetValue(line, eq + 1, i, parsed.vals[i])) return false;
                seen[i] = true;
                break;
            }
    }
    if (input.bad() || !seenFilters) return false;
    for (uint32_t i = 0; i < kParamCount; ++i)
        if (roIsPresetParam(i) && !seen[i]) return false;
    up = std::move(parsed);
    return true;
}
