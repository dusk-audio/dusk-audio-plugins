// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// UserPresetStore.hpp — Sunset Circuits user-preset library (UI-side only).
//
// A file-based patch bank. The host already persists all core params inside
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
// Test/portable installs may explicitly set SUNSET_CIRCUITS_CONFIG_HOME; normal
// platform environment variables retain the behavior documented above.
// Filename = sanitized display name + ".scpreset".
//
// Pure C++17 <filesystem>. No exceptions escape (all APIs return bool/optional);
// every method is intended to be called from the UI thread only — never run().

#pragma once

#include "MultiSynthParams.hpp"   // kParamDefs, kNumCoreParams, ParamDef

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
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
inline constexpr char  kPortableConfigEnv[] = "SUNSET_CIRCUITS_CONFIG_HOME";
// First bytes of every file save() writes, before the version number. loadInto()
// validates the same prefix (plus the version) through its sscanf format; the
// listing scan uses it to fail closed on a foreign file before reading its body.
inline constexpr char  kHeaderPrefix[]  = "# SunsetCircuits preset v";

// One entry in the on-disk library.
struct Entry
{
    std::string           name;   // display name (from the `name=` header, else stem)
    std::filesystem::path path;   // absolute path to the .scpreset file
    // Engine the patch selects, read straight out of the file's `mode=` line. It
    // is DERIVED state, not new metadata: it is the value loadInto() would put in
    // out[kParamMode], cached here so a listing (the preset browser's mode badge
    // and mode filter) does not have to parse every symbol per file per frame.
    // Falls back to the Mode default when the file omits the symbol, which is
    // exactly what a load of that file would leave in place.
    int                   mode = (int)kParamDefs[kParamMode].def;
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

    // Dedicated opt-in for isolated tests and portable installations. Keep this
    // separate from XDG_CONFIG_HOME: XDG may be present in a normal macOS login,
    // but must not move the native Application Support preset library.
    if (const char* portable = std::getenv(kPortableConfigEnv);
        portable && *portable)
    {
        base = fs::path(portable);
        base /= "DuskAudio"; base /= "SunsetCircuits"; base /= "presets";
        return base;
    }

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
            readHeader(p, e.name, e.mode);
            if (e.name.empty()) e.name = p.stem().string();
            entries_.push_back(std::move(e));
        }
        std::sort(entries_.begin(), entries_.end(),
                  [](const Entry& a, const Entry& b) { return a.name < b.name; });
        // The cap is applied AFTER the sort on purpose: "the first 512 by name" is
        // reproducible, "the first 512 the directory iterator happened to hand back"
        // is not, and you cannot know which names sort first without reading every
        // name. What bounds the cost of an oversized directory is therefore the
        // per-file work, not an early exit — see readHeader(), which rejects a
        // foreign file on a 47-byte probe and caps a real one at a few lines.
        if ((int)entries_.size() > kMaxUserPresets)
        {
            std::fprintf(stderr,
                "[SunsetCircuits] %zu user presets exceed the %d cap; ignoring the rest.\n",
                entries_.size(), kMaxUserPresets);
            entries_.resize(kMaxUserPresets);
        }
    }

    // Write the core params to <sanitized name>.scpreset. Returns false on
    // an empty/invalid name or any IO error. Overwrites an existing file (the
    // UI runs the overwrite-confirm flow before calling this).
    //
    // On failure `errOut` (when given) receives a short, human-readable reason.
    // Every failure here is one the PLAYER can act on — a read-only preset
    // directory, a full disk, a name that sanitizes to nothing — so a bare false
    // is not enough: the caller has to be able to say WHY on screen instead of
    // silently dropping the patch. The three sources are kept distinct:
    //   * create_directories' error_code, which used to be discarded outright.
    //     "open catches failure" was true but lossy: a permission error on the
    //     directory surfaced as an unexplained open failure one line later;
    //   * the ofstream open, reported through errno (the stream itself carries no
    //     reason), which is where ENOSPC/EACCES/ENAMETOOLONG on the FILE land;
    //   * the final flush, which is where a full disk usually actually shows up —
    //     the writes above are buffered, so open can succeed and only the flush
    //     of the parameter body hits the wall.
    bool save(const std::string& displayName, const float* values, int nValues,
              std::string* errOut = nullptr)
    {
        namespace fs = std::filesystem;
        const auto fail = [errOut](const char* what, const std::string& why) -> bool
        {
            if (errOut != nullptr) *errOut = why.empty() ? std::string(what)
                                                         : std::string(what) + ": " + why;
            return false;
        };
        if (nValues < (int)kNumCoreParams || values == nullptr)
            return fail("internal error (short value array)", {});
        const std::string stem = sanitize(displayName);
        if (stem.empty()) return fail("that name has no usable characters", {});

        std::error_code ec;
        const fs::path dir = presetDir();
        fs::create_directories(dir, ec);   // no-op if already present
        if (ec) return fail("cannot create the preset folder", ec.message());

        const fs::path file = dir / (stem + kFileExt);
        errno = 0;
        std::ofstream os(file, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!os) return fail("cannot open the file for writing", errnoText());

        os << "# SunsetCircuits preset v" << kFormatVersion << '\n';
        os << "name=" << stem << '\n';
        char line[128];
        for (int i = 0; i < (int)kNumCoreParams; ++i)
        {
            // %.9g round-trips every float32 exactly on read-back (strtof).
            std::snprintf(line, sizeof line, "%s=%.9g\n", kParamDefs[i].symbol, values[i]);
            os << line;
        }
        errno = 0;
        os.flush();
        if (!os) return fail("the write failed", errnoText());
        if (errOut != nullptr) errOut->clear();
        return true;
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
            // The format is documented as safe to hand-edit: strtof happily
            // parses "nan"/"inf", which would feed the DSP directly. Reject
            // non-finite values (keep the default) and clamp the rest to the
            // param's declared range.
            if (!std::isfinite(v)) continue;
            out[idx] = std::max(kParamDefs[idx].min, std::min(kParamDefs[idx].max, v));
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
    // errno rendered through the same machinery as an error_code, so a filesystem
    // failure and a stream failure read the same way on screen. Empty when errno
    // was never set (a stream can fail without touching it).
    static std::string errnoText()
    {
        if (errno == 0) return {};
        return std::error_code(errno, std::generic_category()).message();
    }

    static void stripCR(std::string& s)
    { if (!s.empty() && s.back() == '\r') s.pop_back(); }

    static int indexOfSymbol(const char* sym)
    {
        for (int i = 0; i < (int)kNumCoreParams; ++i)
            if (std::strcmp(kParamDefs[i].symbol, sym) == 0) return i;
        return -1;
    }

    // Read the two listing fields — display name and Mode — out of a preset file
    // without a full parameter-symbol parse. Both sit at the top of anything save() wrote
    // (`name=` is line 2 and `mode=` line 3, Mode being core param 0), so the scan
    // normally stops after three lines. `name` is left empty when the header is
    // missing (the caller falls back to the filename stem) and `mode` keeps the
    // Mode default, which is what loadInto() would leave in place for a file with
    // no `mode=` line.
    //
    // A listing scan runs over a directory the plugin does not control, so it is
    // bounded twice over. The FORMAT HEADER is checked first, out of a fixed-size
    // read: getline() on a file with no newline in it would pull the entire thing
    // into one std::string, and refusing a foreign file costs 40 bytes here rather
    // than however large that file is. The line loop is then capped at one line per
    // core param plus header slack, so a truncated or hand-mangled preset cannot
    // turn the scan unbounded either.
    static void readHeader(const std::filesystem::path& file, std::string& name, int& mode)
    {
        name.clear();
        mode = (int)kParamDefs[kParamMode].def;
        std::ifstream is(file, std::ios::in | std::ios::binary);
        if (!is) return;

        char hdr[48] = {};
        is.read(hdr, (std::streamsize)sizeof(hdr) - 1);
        if (std::strncmp(hdr, kHeaderPrefix, std::strlen(kHeaderPrefix)) != 0) return;
        is.clear();        // a file shorter than the probe leaves eofbit set
        is.seekg(0);
        if (!is) return;

        const int  kMaxLines = (int)kNumCoreParams + 8;
        int        lines = 0;
        bool       haveName = false, haveMode = false;
        std::string ln;
        while ((!haveName || !haveMode) && lines++ < kMaxLines && std::getline(is, ln))
        {
            stripCR(ln);
            const auto eq = ln.find('=');
            if (eq == std::string::npos) continue;
            if (!haveName && ln.compare(0, eq, "name") == 0)
            {
                name = ln.substr(eq + 1);
                haveName = true;
            }
            else if (!haveMode && ln.compare(0, eq, kParamDefs[kParamMode].symbol) == 0)
            {
                // Same contract as loadInto(): a line whose value is not a number,
                // or is not finite, is SKIPPED — the default stands and a later
                // well-formed line can still win. strtof() happily returns 0 for
                // "mode=banana", which would otherwise read as a real Cosmos tag.
                const char* const vs = ln.c_str() + eq + 1;
                char* endp = nullptr;
                const float v = std::strtof(vs, &endp);
                if (endp != vs && std::isfinite(v))
                {
                    const int m = (int)std::lround(v);
                    mode = std::max((int)kParamDefs[kParamMode].min,
                                    std::min((int)kParamDefs[kParamMode].max, m));
                    haveMode = true;
                }
            }
        }
    }

    std::vector<Entry> entries_;
};

} // namespace scpreset
