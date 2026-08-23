// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).

#include "DuskUserPresetStore.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

namespace
{
struct CommaDecimal : std::numpunct<char>
{
    char do_decimal_point() const override { return ','; }
};

struct Preset
{
    std::string name;
    std::string path;
    std::string value;
};

struct TempDirectory
{
    std::filesystem::path path;

    TempDirectory()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path()
            / ("dusk-user-preset-store-" + std::to_string(stamp));
        std::filesystem::create_directories(path);
    }

    ~TempDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

std::string fileContents(const std::filesystem::path& path)
{
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
} // namespace

int main()
{
    TempDirectory temporary;
    const auto library = temporary.path / "library";

    const std::locale previousLocale = std::locale();
    std::locale::global(std::locale(std::locale::classic(), new CommaDecimal));
    const auto first = duskdaf::writeUserPreset(
        library, ".preset", "  Alpha/Beta  ",
        [](std::ostream& output) { output << "value=" << 1.5 << '\n'; });
    const auto collision = duskdaf::writeUserPreset(
        library, ".preset", "Alpha-Beta",
        [](std::ostream& output) { output << "value=" << 3.0 << '\n'; });
    const auto overwritten = duskdaf::writeUserPreset(
        library, ".preset", "Alpha/Beta",
        [](std::ostream& output) { output << "value=" << 2.5 << '\n'; });
    std::locale::global(previousLocale);

    {
        std::ofstream ignored(library / "Ignored.txt");
        ignored << "name=Ignored\nvalue=9\n";
    }

    std::vector<Preset> presets;
    duskdaf::scanUserPresets(
        library, ".preset", presets,
        [](const std::filesystem::path& path, Preset& preset)
        {
            std::ifstream input(path);
            if (!input)
                return false;
            std::string line;
            while (std::getline(input, line))
            {
                if (line.compare(0, 5, "name=") == 0)
                    preset.name = line.substr(5);
                else if (line.compare(0, 6, "value=") == 0)
                    preset.value = line.substr(6);
            }
            return !preset.name.empty() && !preset.value.empty();
        });

    const auto blocked = temporary.path / "blocked";
    {
        std::ofstream file(blocked);
        file << "not a directory";
    }
    const auto failed = duskdaf::writeUserPreset(
        blocked / "child", ".preset", "Cannot Save",
        [](std::ostream& output) { output << "value=0\n"; });

    std::ostringstream actual;
    actual << "first=" << static_cast<bool>(first) << ','
           << std::filesystem::path(first.path).filename().string() << ','
           << first.name << '\n';
    actual << "collision=" << static_cast<bool>(collision) << ','
           << std::filesystem::path(collision.path).filename().string() << '\n';
    actual << "overwrite=" << (overwritten.path == first.path) << ','
           << fileContents(first.path);
    actual << "stored=" << duskdaf::storedUserPresetName(first.path) << '\n';
    actual << "scan=" << presets.size();
    for (const auto& preset : presets)
        actual << ',' << preset.name << ':' << preset.value;
    actual << '\n';
    actual << "failed=" << static_cast<bool>(failed) << '\n';

    const std::string expected =
        "first=1,Alpha_Beta.preset,Alpha/Beta\n"
        "collision=1,Alpha_Beta_2.preset\n"
        "overwrite=1,name=Alpha/Beta\n"
        "value=2.5\n"
        "stored=Alpha/Beta\n"
        "scan=2,Alpha-Beta:3,Alpha/Beta:2.5\n"
        "failed=0\n";
    if (actual.str() != expected)
    {
        std::cerr << "FAIL: shared user-preset store contract\n"
                  << "expected:\n" << expected
                  << "actual:\n" << actual.str();
        return 1;
    }

    std::cout << "PASS: shared user-preset store contract\n" << actual.str();
    return 0;
}
