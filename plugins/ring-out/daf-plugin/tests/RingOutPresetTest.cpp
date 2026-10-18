// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#include "RingOutPreset.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>

namespace
{
int failures = 0;
void check(bool ok, const std::string& message)
{
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message.c_str()); }
}

const std::string valid = "name=Stage left\nsense=1\nglobal_q=1.5\nglobal_amp=-3\nlink=1\ngain_out=3\nfilters=1,1000,-12,8;0,250,-6,2.5\n";

std::string replaceLine(const std::string& text, const std::string& key, const std::string& replacement)
{
    std::string result = text;
    const auto start = result.find(key + "=");
    const auto end = result.find('\n', start);
    result.replace(start, end + 1 - start, replacement);
    return result;
}

RoUserPreset sentinel()
{
    RoUserPreset p;
    p.name = "Keep me";
    p.path = "keep.ropreset";
    for (uint32_t i = 0; i < kParamCount; ++i) p.vals[i] = 42.0f + (float)i;
    auto filter = duskaudio::ringout::defaultFilter();
    filter.freqHz = 500.0f;
    filter.cutDb = -9.0f;
    p.filters.add(filter);
    return p;
}

bool unchanged(const RoUserPreset& a, const RoUserPreset& b)
{
    if (a.name != b.name || a.path != b.path || !duskaudio::ringout::tablesEqual(a.filters, b.filters)) return false;
    for (uint32_t i = 0; i < kParamCount; ++i) if (a.vals[i] != b.vals[i]) return false;
    return true;
}
}

int main()
{
    const auto path = std::filesystem::temp_directory_path()
        / ("ring-out-preset-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".ropreset");
    const auto read = [&](const std::string& text, RoUserPreset& out)
    {
        { std::ofstream file(path); file << text; }
        return roReadUserPresetFile(path, out);
    };
    const auto rejects = [&](const std::string& text, const std::string& name)
    {
        auto p = sentinel();
        const auto before = p;
        check(!read(text, p), name + " rejected");
        check(unchanged(p, before), name + " leaves destination untouched");
    };

    auto p = sentinel();
    check(read(valid, p), "complete preset loads");
    check(p.name == "Stage left" && p.path == "keep.ropreset", "preset identity");
    check(p.vals[kParamSense] == 1.0f && p.vals[kParamGlobalQ] == 1.5f
          && p.vals[kParamGlobalAmp] == -3.0f && p.vals[kParamLink] == 1.0f
          && p.vals[kParamGainOut] == 3.0f, "all five preset parameters applied");
    check(p.filters.count == 2 && p.filters.f[0].freqHz == 1000.0f && p.filters.f[0].cutDb == -12.0f
          && p.filters.f[0].q == 8.0f && p.filters.f[0].on && !p.filters.f[1].on, "filter table applied");
    check(p.vals[kParamSetup] == 0.0f && p.vals[kParamAdd] == 0.0f && p.vals[kParamReset] == 0.0f
          && p.vals[kParamBypass] == 0.0f && p.vals[kParamInLevel] == -120.0f
          && p.vals[kParamOutLevel] == -120.0f, "non-preset parameters retain defaults");

    for (const std::string key : {"sense", "global_q", "global_amp", "link", "gain_out", "filters"})
    {
        rejects(replaceLine(valid, key, key + "=garbage\n"), "malformed " + key);
        rejects(replaceLine(valid, key, ""), "missing " + key);
        const auto start = valid.find(key + "=");
        const auto end = valid.find('\n', start);
        rejects(valid + valid.substr(start, end + 1 - start), "duplicate " + key);
    }
    rejects("sense=0\n", "truncated preset");
    rejects("name=Empty\n", "name-only preset");
    rejects("", "empty preset");
    rejects(replaceLine(valid, "global_q", "global_q=NaN\n"), "non-finite value");
    rejects(replaceLine(valid, "gain_out", "gain_out=3oops\n"), "trailing garbage");
    check(read(replaceLine(valid, "filters", "filters=\n"), p) && p.filters.count == 0,
          "explicit empty filter table remains a valid preset");
    check(read(valid + "future_key=hello\nbypass=1\nsetup=1\n", p) && p.vals[kParamBypass] == 0.0f
          && p.vals[kParamSetup] == 0.0f, "unknown and non-preset keys remain ignored");
    std::string crlf = valid;
    for (std::size_t at = 0; (at = crlf.find('\n', at)) != std::string::npos; at += 2) crlf.insert(at, 1, '\r');
    check(read(crlf, p), "CRLF file loads");
    check(read(valid.substr(0, valid.size() - 1), p), "final newline optional");
    std::error_code error;
    std::filesystem::remove(path, error);
    check(failures == 0, "preset validation suite");
    if (failures == 0) std::puts("PASS: Ring Out preset validation (complete, malformed, duplicate, truncated, transactional)");
    return failures == 0 ? 0 : 1;
}
