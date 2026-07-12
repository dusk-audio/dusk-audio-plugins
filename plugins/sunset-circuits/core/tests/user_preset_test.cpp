// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// user_preset_test — unit gate for dpf-plugin/UserPresetStore.hpp. Framework-free
// (no JUCE/DPF); redirects the preset dir to a throwaway temp tree via
// XDG_CONFIG_HOME so it never touches the real ~/.config. Exercised by
// core/tests/user_preset_gate.py and wired into run_all.sh.
//
// Covers: 222-float bit-exact round-trip, reset-then-apply defaults for missing
// symbols, malformed/unknown-version rejection, unknown-symbol skip, name
// sanitization, overwrite, delete, exists().

#include "UserPresetStore.hpp"

#include <cmath>      // std::round
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>
#include <unistd.h>   // getpid, setenv

namespace fs = std::filesystem;

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "  FAIL: %s\n", (msg)); ++g_fail; } \
    else         { std::fprintf(stderr, "  ok:   %s\n", (msg)); } } while (0)

int main()
{
    using namespace scpreset;

    // Redirect the app-data dir into a unique temp tree (Linux XDG path).
    const fs::path root = fs::temp_directory_path() /
        ("sc_upreset_" + std::to_string((unsigned long)::getpid()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    if (::setenv("XDG_CONFIG_HOME", root.c_str(), 1) != 0)
    {
        std::fprintf(stderr, "FATAL: setenv(XDG_CONFIG_HOME) failed — aborting "
                             "before touching any preset directory\n");
        return 1;
    }

    const fs::path expectDir =
        root / "DuskAudio" / "SunsetCircuits" / "presets";
    CHECK(presetDir() == expectDir, "presetDir resolves under XDG_CONFIG_HOME");
    if (presetDir() != expectDir)
    {
        // Isolation failed: every later step writes/overwrites/DELETES presets,
        // which must never run against the user's real config directory.
        std::fprintf(stderr, "FATAL: preset dir not isolated (%s) — aborting\n",
                     presetDir().c_str());
        return 1;
    }

    const int N = (int)kNumCoreParams;
    std::fprintf(stderr, "kNumCoreParams = %d\n", N);
    CHECK(N == 222, "222 core params");

    Store store;

    // ---- 1. bit-exact round-trip of all 222 floats -------------------------
    std::vector<float> vin(N), vout(N, -12345.0f);
    std::mt19937 rng(0xC0FFEE);
    for (int i = 0; i < N; ++i)
    {
        const ParamDef& d = kParamDefs[i];
        std::uniform_real_distribution<float> dist(d.min, d.max);
        float v = dist(rng);
        if (d.kind == PK_INT || d.kind == PK_BOOL) v = std::round(v);
        vin[i] = v;
    }
    CHECK(store.save("Round Trip", vin.data(), N), "save Round Trip");
    store.refresh();
    CHECK(store.loadByName("Round Trip", vout.data(), N), "load Round Trip");
    int mism = 0, firstBad = -1;
    for (int i = 0; i < N; ++i)
        if (std::memcmp(&vin[i], &vout[i], sizeof(float)) != 0)
        { if (firstBad < 0) firstBad = i; ++mism; }
    if (mism)
        std::fprintf(stderr, "  round-trip mismatches=%d first=%d (%s: %.9g vs %.9g)\n",
                     mism, firstBad, kParamDefs[firstBad].symbol,
                     vin[firstBad], vout[firstBad]);
    CHECK(mism == 0, "222/222 floats bit-exact after save+load");

    // ---- 2. missing symbols keep factory defaults --------------------------
    {
        fs::path f = expectDir / "Partial.scpreset";
        std::ofstream os(f, std::ios::trunc);
        os << "# SunsetCircuits preset v1\n";
        os << "name=Partial\n";
        os << "filterCutoff=1234.5\n";   // only ONE param overridden
        os.close();
        std::vector<float> vp(N, -1.0f);
        CHECK(store.loadInto(f, vp.data(), N), "load Partial");
        const int fc = kParamFilterCutoff;
        CHECK(vp[fc] == 1234.5f, "overridden symbol applied");
        // Every other slot must equal its factory default.
        bool allDef = true;
        for (int i = 0; i < N; ++i)
            if (i != fc && vp[i] != kParamDefs[i].def) { allDef = false; break; }
        CHECK(allDef, "missing symbols reset to defaults");
    }

    // ---- 3. malformed / unknown-version rejection --------------------------
    {
        std::vector<float> vp(N, 0.0f);
        fs::path bad1 = expectDir / "NoHeader.scpreset";
        { std::ofstream os(bad1, std::ios::trunc); os << "filterCutoff=100\n"; }
        CHECK(!store.loadInto(bad1, vp.data(), N), "reject file with no version header");

        fs::path bad2 = expectDir / "FutureVer.scpreset";
        { std::ofstream os(bad2, std::ios::trunc);
          os << "# SunsetCircuits preset v99\nname=Future\nfilterCutoff=100\n"; }
        CHECK(!store.loadInto(bad2, vp.data(), N), "reject unsupported version 99");

        fs::path bad3 = expectDir / "Missing.scpreset";
        CHECK(!store.loadInto(bad3, vp.data(), N), "reject nonexistent file");
    }

    // ---- 4. unknown symbol warned + skipped, known applied -----------------
    {
        fs::path f = expectDir / "Unknown.scpreset";
        { std::ofstream os(f, std::ios::trunc);
          os << "# SunsetCircuits preset v1\nname=Unknown\n";
          os << "bogusParamXYZ=42\n";
          os << "filterRes=0.9\n"; }
        std::vector<float> vp(N, -1.0f);
        CHECK(store.loadInto(f, vp.data(), N), "load with unknown symbol (returns true)");
        CHECK(vp[kParamFilterRes] == 0.9f, "known symbol applied despite unknown");
    }

    // ---- 4b. hand-edited hostile values: nan/inf rejected, range clamped ----
    {
        fs::path f = expectDir / "Hostile.scpreset";
        { std::ofstream os(f, std::ios::trunc);
          os << "# SunsetCircuits preset v1\nname=Hostile\n";
          os << "filterCutoff=nan\n";        // non-finite -> keep default
          os << "filterRes=inf\n";           // non-finite -> keep default
          os << "masterVol=99999\n"; }       // out of range -> clamp to max
        std::vector<float> vp(N, -1.0f);
        CHECK(store.loadInto(f, vp.data(), N), "load hostile file (returns true)");
        CHECK(vp[kParamFilterCutoff] == kParamDefs[kParamFilterCutoff].def,
              "nan value rejected, default kept");
        CHECK(vp[kParamFilterRes] == kParamDefs[kParamFilterRes].def,
              "inf value rejected, default kept");
        CHECK(vp[kParamMasterVol] == kParamDefs[kParamMasterVol].max,
              "out-of-range value clamped to param max");
    }

    // ---- 5. name sanitization ----------------------------------------------
    CHECK(sanitize("a/b\\c:d") == "abcd", "strip path/reserved chars");
    CHECK(sanitize("  Lead  ") == "Lead", "trim surrounding whitespace");
    CHECK(sanitize("...") == "", "all-dots sanitizes to empty");
    CHECK(sanitize("") == "", "empty stays empty");
    CHECK(!store.save("", vin.data(), N), "save rejects empty name");
    CHECK(!store.save("///", vin.data(), N), "save rejects name that sanitizes empty");
    {
        CHECK(store.save("My/Cool:Patch", vin.data(), N), "save name with separators");
        CHECK(fs::exists(expectDir / "MyCoolPatch.scpreset"), "file uses sanitized stem");
    }

    // ---- 6. overwrite --------------------------------------------------------
    {
        std::vector<float> a(N, 0.0f), b(N, 0.0f), r(N, -1.0f);
        a[kParamFilterCutoff] = 500.0f;
        b[kParamFilterCutoff] = 9000.0f;
        CHECK(store.save("Dup", a.data(), N), "save Dup v1");
        CHECK(store.exists("Dup"), "exists() sees Dup");
        CHECK(store.save("Dup", b.data(), N), "save Dup v2 (overwrite)");
        store.refresh();
        CHECK(store.loadByName("Dup", r.data(), N), "load Dup");
        CHECK(r[kParamFilterCutoff] == 9000.0f, "overwrite kept the second write");
    }

    // ---- 7. delete -----------------------------------------------------------
    {
        store.refresh();
        fs::path target;
        for (const auto& e : store.list()) if (e.name == "Dup") target = e.path;
        CHECK(!target.empty(), "found Dup in listing");
        CHECK(store.remove(target), "delete Dup");
        CHECK(!store.exists("Dup"), "Dup gone after delete");
    }

    // ---- 8. listing is sorted + capped-aware --------------------------------
    {
        store.refresh();
        const auto& L = store.list();
        bool sorted = true;
        for (size_t i = 1; i < L.size(); ++i) if (L[i - 1].name > L[i].name) sorted = false;
        CHECK(sorted, "listing alphabetically sorted");
    }

    fs::remove_all(root, ec);

    std::fprintf(stderr, "\n%s\n", g_fail == 0 ? "USER_PRESET_TEST PASS" : "USER_PRESET_TEST FAIL");
    return g_fail == 0 ? 0 : 1;
}
