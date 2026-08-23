// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Shared message-thread user-preset file handling for DAF plugin UIs.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <locale>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace duskdaf
{

// Keep the path used by the shipped DAF plugins. In particular, macOS stays
// under ~/.config even when XDG_CONFIG_HOME is present, so an update cannot
// orphan an existing preset library.
inline std::filesystem::path userPresetDirectory(std::string_view productDirectory)
{
    std::filesystem::path base;
   #if defined(_WIN32)
    for (const char* variable : {"APPDATA", "LOCALAPPDATA"})
        if (const char* value = std::getenv(variable);
            value != nullptr && value[0] != '\0')
        {
            base = value;
            break;
        }
   #elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0')
        base = std::filesystem::path(home) / ".config";
   #else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME");
        xdg != nullptr && xdg[0] != '\0')
        base = xdg;
    else if (const char* home = std::getenv("HOME");
             home != nullptr && home[0] != '\0')
        base = std::filesystem::path(home) / ".config";
   #endif
    if (base.empty())
        base = ".";
    return base / "DuskAudio" / productDirectory / "presets";
}

inline std::string normaliseUserPresetName(const char* rawName)
{
    std::string name(rawName != nullptr ? rawName : "");
    while (!name.empty()
           && std::isspace(static_cast<unsigned char>(name.front())) != 0)
        name.erase(name.begin());
    while (!name.empty()
           && std::isspace(static_cast<unsigned char>(name.back())) != 0)
        name.pop_back();
    return name;
}

inline std::string userPresetFilenameStem(const std::string& name)
{
    std::string stem;
    stem.reserve(name.size());
    for (const unsigned char character : name)
        stem += std::isalnum(character) != 0 ? static_cast<char>(character) : '_';
    return stem.empty() ? std::string("Preset") : stem;
}

// Return only the display name. Format-specific readers remain responsible for
// deciding whether a complete preset is valid and safe to load.
inline std::string storedUserPresetName(const std::filesystem::path& path)
{
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line))
        if (line.compare(0, 5, "name=") == 0)
            return line.substr(5);
    return {};
}

struct SavedUserPreset
{
    std::string name;
    std::string path;

    explicit operator bool() const noexcept { return !path.empty(); }
};

// The writer appends the plugin-specific payload after the shared name= line.
// nameReader exists because a format may require a complete decode before it
// considers an existing file eligible for an in-place re-save.
template <typename Writer, typename NameReader>
SavedUserPreset writeUserPreset(const std::filesystem::path& directory,
                                std::string_view extension,
                                const char* rawName,
                                Writer&& writer,
                                NameReader&& nameReader)
{
    const std::string name = normaliseUserPresetName(rawName);
    if (name.empty())
        return {};

    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error)
        return {};

    const std::string stem = userPresetFilenameStem(name);
    std::filesystem::path selectedPath;
    for (int suffix = 1; suffix <= 99; ++suffix)
    {
        const std::string filename = stem
            + (suffix == 1 ? std::string() : "_" + std::to_string(suffix))
            + std::string(extension);
        const std::filesystem::path candidate = directory / filename;
        error.clear();
        const bool exists = std::filesystem::exists(candidate, error);
        if (!error && (!exists || nameReader(candidate) == name))
        {
            selectedPath = candidate;
            break;
        }
    }
    if (selectedPath.empty())
        return {};

    std::ofstream output(selectedPath, std::ios::trunc);
    if (!output)
        return {};
    output.imbue(std::locale::classic());
    output << "name=" << name << '\n';
    writer(output);
    output.close();
    if (!output)
        return {};

    return {name, selectedPath.string()};
}

template <typename Writer>
SavedUserPreset writeUserPreset(const std::filesystem::path& directory,
                                std::string_view extension,
                                const char* rawName,
                                Writer&& writer)
{
    return writeUserPreset(
        directory, extension, rawName, std::forward<Writer>(writer),
        [](const std::filesystem::path& path) { return storedUserPresetName(path); });
}

// Preset records intentionally use the common `name` and `path` members. The
// reader fills or validates the plugin-specific cached parameter payload and
// returns false for a file that must not appear in the browser.
template <typename Preset, typename Reader>
void scanUserPresets(const std::filesystem::path& directory,
                     std::string_view extension,
                     std::vector<Preset>& presets,
                     Reader&& reader)
{
    presets.clear();
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory, error), end;
         !error && it != end; it.increment(error))
    {
        if (it->path().extension() != extension)
            continue;
        Preset preset{};
        preset.path = it->path().string();
        preset.name = it->path().stem().string();
        if (reader(it->path(), preset))
            presets.push_back(std::move(preset));
    }
    std::sort(presets.begin(), presets.end(),
              [](const Preset& a, const Preset& b) { return a.name < b.name; });
}

} // namespace duskdaf
