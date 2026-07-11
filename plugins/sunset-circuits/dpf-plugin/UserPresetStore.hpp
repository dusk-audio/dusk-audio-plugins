// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DPF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
//
// UserPresetStore.hpp — Sunset Circuits user-preset library (UI-side only).
//
// A file-based patch bank. The host already persists the 222 core params inside
// its session (DISTRHO_PLUGIN_WANT_STATE stays 0), so this is purely a personal
// library the player can save to / recall from, independent of any DAW.
//
// Format: versioned plain text, one preset per file:
//     # SunsetCircuits preset v1
//     name=<display name>
//     <symbol>=<value>          (one per core param, order irrelevant)
// Parser contract:
//   * fail-closed on an unknown format version (load returns false);
//   * unknown symbols are warned about on stderr and skipped (forward compat);
//   * missing symbols keep their factory default (loadProgram-style
//     reset-then-apply — load() pre-fills the output array with defaults).
//
// Location (per-user app-data dir):
//   Linux:   $XDG_CONFIG_HOME (or ~/.config)/DuskAudio/SunsetCircuits/presets/
//   macOS:   ~/Library/Application Support/DuskAudio/SunsetCircuits/presets/
//   Windows: %APPDATA%/DuskAudio/SunsetCircuits/presets/
// Filename = sanitized display name + ".scpreset".
//
// Pure C++17 <filesystem>. No exceptions escape (all APIs return bool/optional);
// every method is intended to be called from the UI thread only — never run().

#pragma once

#include "MultiSynthParams.hpp"   // kParamDefs, kNumCoreParams, ParamDef

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace scpreset {

inline constexpr int   kFormatVersion   = 1;
inline constexpr char  kFileExt[]       = ".scpreset";
inline constexpr int   kMaxUserPresets  = 512;   // hard cap; extras ignored

// One entry in the on-disk library.
struct Entry
{
    std::string           name;   // display name (from the `name=` header, else stem)
    std::filesystem::path path;   // absolute path to the .scpreset file
};

// --------------------------------------------------------------------------
// Directory resolver — portable across the three desktop OSes. Only Linux is
// exercised in CI, but the Windows/macOS branches are written now so a later
// cross-compile needs no edit here.
// --------------------------------------------------------------------------
inline std::filesystem::path presetDir()
{
    namespace fs = std::filesystem;
    fs::path base;
   #if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA"); appdata && *appdata)
        base = fs::path(appdata);
    else
        base = fs::current_path();
    base /= "DuskAudio"; base /= "SunsetCircuits"; base /= "presets";
   #elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    base = (home && *home) ? fs::path(home) : fs::current_path();
    base /= "Library"; base /= "Application Support";
    base /= "DuskAudio"; base /= "SunsetCircuits"; base /= "presets";
   #else // Linux / other Unix
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        base = fs::path(xdg);
    else if (const char* home = std::getenv("HOME"); home && *home)
        base = fs::path(home) / ".config";
    else
        base = fs::current_path();
    base /= "DuskAudio"; base /= "SunsetCircuits"; base /= "presets";
   #endif
    return base;
}

// --------------------------------------------------------------------------
// Name sanitization: strip path separators and control chars, collapse to a
// safe filename stem. Returns the empty string if nothing usable remains
// (caller must reject an empty result).
// --------------------------------------------------------------------------
inline std::string sanitize(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in)
    {
        if (c < 0x20 || c == 0x7f) continue;          // control chars
        if (c == '/' || c == '\\') continue;          // path separators
        if (c == ':' || c == '*' || c == '?' || c == '"' ||
            c == '<' || c == '>' || c == '|') continue; // Windows-reserved
        out.push_back((char)c);
    }
    // Trim leading/trailing whitespace and dots (a trailing dot is invalid on
    // Windows; a leading dot would hide the file on Unix).
    const auto notTrim = [](unsigned char c) { return !(std::isspace(c) || c == '.'); };
    auto b = std::find_if(out.begin(), out.end(), notTrim);
    auto e = std::find_if(out.rbegin(), out.rend(), notTrim).base();
    if (b >= e) return {};
    return std::string(b, e);
}

// --------------------------------------------------------------------------
// The library. Call refresh() to (re)scan the directory; list() returns the
// cached, alphabetically-sorted entries. All file IO is synchronous and
// UI-thread only.
// --------------------------------------------------------------------------
class Store
{
public:
    const std::vector<Entry>& list() const { return entries_; }

    // (Re)scan the preset directory into the cached list. Never throws.
    void refresh()
    {
        namespace fs = std::filesystem;
        entries_.clear();
        std::error_code ec;
        const fs::path dir = presetDir();
        if (!fs::exists(dir, ec)) return;
        for (fs::directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec))
        {
            if (!it->is_regular_file(ec)) continue;
            const fs::path& p = it->path();
            if (p.extension() != kFileExt) continue;
            Entry e;
            e.path = p;
            e.name = readName(p);
            if (e.name.empty()) e.name = p.stem().string();
            entries_.push_back(std::move(e));
        }
        std::sort(entries_.begin(), entries_.end(),
                  [](const Entry& a, const Entry& b) { return a.name < b.name; });
        if ((int)entries_.size() > kMaxUserPresets)
        {
            std::fprintf(stderr,
                "[SunsetCircuits] %zu user presets exceed the %d cap; ignoring the rest.\n",
                entries_.size(), kMaxUserPresets);
            entries_.resize(kMaxUserPresets);
        }
    }

    // Write the 222 core params to <sanitized name>.scpreset. Returns false on
    // an empty/invalid name or any IO error. Overwrites an existing file (the
    // UI runs the overwrite-confirm flow before calling this).
    bool save(const std::string& displayName, const float* values, int nValues)
    {
        namespace fs = std::filesystem;
        if (nValues < (int)kNumCoreParams || values == nullptr) return false;
        const std::string stem = sanitize(displayName);
        if (stem.empty()) return false;

        std::error_code ec;
        const fs::path dir = presetDir();
        fs::create_directories(dir, ec);   // no-op if present; ec ignored, open catches failure

        const fs::path file = dir / (stem + kFileExt);
        std::ofstream os(file, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!os) return false;

        os << "# SunsetCircuits preset v" << kFormatVersion << '\n';
        os << "name=" << stem << '\n';
        char line[128];
        for (int i = 0; i < (int)kNumCoreParams; ++i)
        {
            // %.9g round-trips every float32 exactly on read-back (strtof).
            std::snprintf(line, sizeof line, "%s=%.9g\n", kParamDefs[i].symbol, values[i]);
            os << line;
        }
        os.flush();
        return (bool)os;
    }

    // Load a preset by file path into out[0..kNumCoreParams-1]. The array is
    // first filled with factory defaults (reset-then-apply), then every valid
    // symbol found in the file overrides its slot. Returns false on a missing
    // file or an unknown format version; unknown symbols warn + skip.
    bool loadInto(const std::filesystem::path& file, float* out, int nOut) const
    {
        if (nOut < (int)kNumCoreParams || out == nullptr) return false;
        std::ifstream is(file, std::ios::in | std::ios::binary);
        if (!is) return false;

        std::string firstLine;
        if (!std::getline(is, firstLine)) return false;
        stripCR(firstLine);
        int ver = -1;
        // Accept exactly "# SunsetCircuits preset v<N>".
        if (std::sscanf(firstLine.c_str(), "# SunsetCircuits preset v%d", &ver) != 1)
            return false;
        if (ver != kFormatVersion)
        {
            std::fprintf(stderr,
                "[SunsetCircuits] preset '%s' has unsupported version %d (expected %d); rejected.\n",
                file.string().c_str(), ver, kFormatVersion);
            return false;
        }

        // Reset-then-apply: every slot starts at its factory default.
        for (int i = 0; i < (int)kNumCoreParams; ++i) out[i] = kParamDefs[i].def;

        std::string ln;
        while (std::getline(is, ln))
        {
            stripCR(ln);
            if (ln.empty() || ln[0] == '#') continue;
            const auto eq = ln.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = ln.substr(0, eq);
            const std::string val = ln.substr(eq + 1);
            if (key == "name") continue;   // header, not a param
            const int idx = indexOfSymbol(key.c_str());
            if (idx < 0)
            {
                std::fprintf(stderr,
                    "[SunsetCircuits] preset '%s': unknown symbol '%s' skipped.\n",
                    file.string().c_str(), key.c_str());
                continue;
            }
            char* endp = nullptr;
            const float v = std::strtof(val.c_str(), &endp);
            if (endp == val.c_str()) continue;   // not a number; skip
            out[idx] = v;
        }
        return true;
    }

    // Convenience: load by display name using the cached list.
    bool loadByName(const std::string& name, float* out, int nOut) const
    {
        for (const auto& e : entries_)
            if (e.name == name) return loadInto(e.path, out, nOut);
        return false;
    }

    // Delete a preset file by path. Returns true if the file no longer exists
    // afterwards (removed, or was already gone).
    bool remove(const std::filesystem::path& file)
    {
        std::error_code ec;
        std::filesystem::remove(file, ec);
        return !std::filesystem::exists(file, ec);
    }

    // True if a preset with this sanitized name already exists on disk.
    bool exists(const std::string& displayName) const
    {
        const std::string stem = sanitize(displayName);
        if (stem.empty()) return false;
        std::error_code ec;
        return std::filesystem::exists(presetDir() / (stem + kFileExt), ec);
    }

private:
    static void stripCR(std::string& s)
    { if (!s.empty() && s.back() == '\r') s.pop_back(); }

    static int indexOfSymbol(const char* sym)
    {
        for (int i = 0; i < (int)kNumCoreParams; ++i)
            if (std::strcmp(kParamDefs[i].symbol, sym) == 0) return i;
        return -1;
    }

    // Read just the `name=` header of a file (for the listing). Empty on miss.
    static std::string readName(const std::filesystem::path& file)
    {
        std::ifstream is(file, std::ios::in | std::ios::binary);
        if (!is) return {};
        std::string ln;
        int guard = 0;
        while (std::getline(is, ln) && guard++ < 8)
        {
            stripCR(ln);
            if (ln.rfind("name=", 0) == 0) return ln.substr(5);
        }
        return {};
    }

    std::vector<Entry> entries_;
};

} // namespace scpreset
