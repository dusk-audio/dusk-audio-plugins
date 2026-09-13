// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).

#include "DuskUserPresetStore.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#if !defined(_WIN32)
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

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

    int failures = 0;
    const auto check = [&](bool condition, const char* message)
    {
        if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
    };
#if !defined(_WIN32)
    // Both independent processes finish writing before either publishes. This
    // deterministically exercises filenames selected concurrently by the old code.
    const auto concurrent = temporary.path / "concurrent";
    int ready[2], release[2];
    if (pipe(ready) != 0 || pipe(release) != 0) return 2;
    pid_t children[2]{};
    for (int i = 0; i < 2; ++i)
    {
        children[i] = fork();
        if (children[i] < 0) return 2;
        if (children[i] == 0)
        {
            close(ready[0]); close(release[1]);
            const auto saved = duskdaf::writeUserPreset(
                concurrent, ".preset", i == 0 ? "Alpha/Beta" : "Alpha-Beta",
                [&](std::ostream& output)
                {
                    output << "value=" << i << '\n';
                    const char token = 'x'; char go;
                    if (write(ready[1], &token, 1) != 1 || read(release[0], &go, 1) != 1)
                        _exit(2);
                });
            _exit(saved ? 0 : 1);
        }
    }
    close(ready[1]); close(release[0]);
    for (int i = 0; i < 2; ++i)
    {
        pollfd descriptor{ready[0], POLLIN, 0};
        char token;
        if (poll(&descriptor, 1, 10000) != 1 || read(ready[0], &token, 1) != 1) return 2;
    }
    if (write(release[1], "xx", 2) != 2) return 2;
    close(ready[0]); close(release[1]);
    for (const auto child : children)
    {
        int status = 0;
        check(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
              "both concurrent processes save successfully");
    }
    std::vector<std::string> concurrentContents;
    for (const auto& entry : std::filesystem::directory_iterator(concurrent))
        concurrentContents.push_back(fileContents(entry.path()));
    std::sort(concurrentContents.begin(), concurrentContents.end());
    check(concurrentContents == std::vector<std::string>{"name=Alpha-Beta\nvalue=1\n", "name=Alpha/Beta\nvalue=0\n"},
          "concurrent sanitized-name collision preserves both independent payloads");
    const auto otherSession = duskdaf::writeUserPreset(
        temporary.path / "other-session", ".preset", "Alpha/Beta",
        [](std::ostream& output) { output << "value=separate\n"; });
    check(otherSession && fileContents(otherSession.path) == "name=Alpha/Beta\nvalue=separate\n",
          "separate store/session has its own payload");
    const auto reopened = duskdaf::writeUserPreset(
        concurrent, ".preset", "Alpha/Beta",
        [](std::ostream& output) { output << "value=reopened\n"; });
    check(reopened && fileContents(reopened.path) == "name=Alpha/Beta\nvalue=reopened\n"
              && fileContents(otherSession.path) == "name=Alpha/Beta\nvalue=separate\n",
          "reopened store does not leak state into another session");
    concurrentContents.clear();
    for (const auto& entry : std::filesystem::directory_iterator(concurrent))
        concurrentContents.push_back(fileContents(entry.path()));
    std::sort(concurrentContents.begin(), concurrentContents.end());
    check(concurrentContents == std::vector<std::string>{"name=Alpha-Beta\nvalue=1\n", "name=Alpha/Beta\nvalue=reopened\n"},
          "re-saving one colliding name preserves the other instance payload");
    const auto readPreset = [](const std::filesystem::path& path, Preset& preset)
    {
        preset.name = duskdaf::storedUserPresetName(path);
        preset.value = fileContents(path);
        return !preset.name.empty();
    };
    std::vector<Preset> instanceA, instanceB;
    duskdaf::scanUserPresets(concurrent, ".preset", instanceA, readPreset);
    duskdaf::scanUserPresets(temporary.path / "other-session", ".preset", instanceB, readPreset);
    check(instanceA.size() == 2 && instanceB.size() == 1
              && instanceB.front().value == "name=Alpha/Beta\nvalue=separate\n",
          "independent instance scans retain only their own library payloads");
    duskdaf::scanUserPresets(temporary.path / "empty-session", ".preset", instanceA, readPreset);
    check(instanceA.empty() && instanceB.size() == 1,
          "switching sessions clears cached presets without altering another instance");
#endif
    const std::string savedContents = fileContents(first.path);
    const auto failedOverwrite = duskdaf::writeUserPreset(
        library, ".preset", "Alpha/Beta", [](std::ostream& output)
        {
            output << "incomplete replacement";
            output.setstate(std::ios::badbit);
        });
    check(!failedOverwrite, "failed overwrite reports failure");
    check(fileContents(first.path) == savedContents, "failed overwrite preserves the original bytes");
    try
    {
        duskdaf::writeUserPreset(library, ".preset", "Alpha/Beta", [](std::ostream& output)
        {
            output << "incomplete replacement";
            throw std::runtime_error("writer failed");
        });
        check(false, "writer exception propagates");
    }
    catch (const std::runtime_error&) {}
    check(fileContents(first.path) == savedContents, "throwing writer preserves the original bytes");
    const auto failedNew = duskdaf::writeUserPreset(
        library, ".preset", "Failed New", [](std::ostream& output) { output.setstate(std::ios::badbit); });
    check(!failedNew && !std::filesystem::exists(library / "Failed_New.preset"),
          "failed new save leaves no incomplete preset");
    check(std::distance(std::filesystem::directory_iterator(library), std::filesystem::directory_iterator()) == 2,
          "failed saves clean up temporary files and directories");

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
    return failures == 0 ? 0 : 1;
}
