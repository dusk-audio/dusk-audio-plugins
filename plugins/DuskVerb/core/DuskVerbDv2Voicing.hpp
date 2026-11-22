// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// DuskVerb 2's own per-preset voicing: what it adds around the shared engines, keyed by preset
// name like the shared engine tables (DuskVerbPresetEngineConfig.cpp), which stay frozen with the
// legacy plugin. Today that is the early field (DuskVerbEarlyField.hpp), the octave trim on the
// wet (DuskVerbOctaveTrim.hpp), the band limit after it (DuskVerbBandLimit.hpp) and the engine
// patch (DuskVerb 2's own settings of the shared engines' features the legacy build never uses).
// A preset without a row has no field, a flat trim, no band limit and no patch, so it renders
// exactly as its shared engine does.
//
// The tuning hooks: in a build configured with -DDUSKVERB2_TUNING_HOOKS=ON, the environment
// variable DUSKVERB2_EARLYFIELD replaces the early-field table, DUSKVERB2_OCTAVETRIM the
// octave-trim table and DUSKVERB2_BANDLIMIT the band-limit table, for every preset that loads.
// DuskVerbDSP parses them once, at construction, never on the audio thread:
//
//   DUSKVERB2_EARLYFIELD="enabled=1,start_ms=4,window_ms=80,taps=24,slope_db=-6,attack_ms=10,
//     inject=0.4,direct_db=-14,refl_db=-18,refl_count=2,refl_ms=11/17/0/0,lowpass_hz=7000,
//     burst_db=-20,burst_start_ms=29,burst_ms=5,burst_lp_hz=10000,side_db=-6,side_start_ms=17,side_ms=30,
//     side_attack_ms=2,side_lp_hz=250"
//   DUSKVERB2_OCTAVETRIM="0.5,0,-1,0,0,1.25,0,0,-2"
//   DUSKVERB2_BANDLIMIT="8500,16,7000,4,0.9"   (low-pass corner, order; lift Hz, dB, Q; 0,0,0,0,0: none)
//   DUSKVERB2_PMBSIDE=1        (or 0: the engine patch's decorrelated side, on or off)
//   DUSKVERB2_SPARSEERLP=8000  (the engine patch's sparse-ER low-pass corner in Hz; 0: off)
//   DUSKVERB2_SPARSEERSHAPE=4,15,0,-1  (the engine patch's sparse early field: onset ms, decay ms, late-burst gain, level; below 0: the shared row's)
//   DUSKVERB2_DUALSLOPE=0.4,1.2  (the engine patch's dual slope: fast-line RT60 ratio, fast-line gain; 0,1: off)
//   DUSKVERB2_OCTDECAY="1,1,0.85,1,1,1,1,1,1.2"   (the engine patch's octave decay multipliers)
//   DUSKVERB2_PMBSMEARSHARE="0,1,0,0,0,0"   (the engine patch's ParallelMultiband smear shares)
//   DUSKVERB2_DHSTATIC=2       (the engine patch's DenseHall allpasses without modulation: 0 none, 1 input, 2 input and loop)
//   DUSKVERB2_STEREOBANDS="2,0.53,0.67"   (the engine patch's band width stage, side gains low/mid/high; "off": no stage)
//   DUSKVERB2_PMBAPK="-1,0.15,-1,0.15,-1,-1"   (the engine patch's ParallelMultiband densifier coefficients)
//   DUSKVERB2_PMBLEVEL="1,0.8,1,1.2,1,1"   (the engine patch's ParallelMultiband band level multipliers)
//
// The shared engine config's DUSKVERB_MODESMEAR, when set, also takes the mode smear from the patch.
//
// Early field: keys left out keep EarlyFieldConfig's defaults, so nothing sounds without
// enabled=1. refl_ms takes one to ten times, the rest 0; refl_trim_db one to ten levels over refl_db in the same order (dB, the rest 0).
// side_offset_ms: one direct-tap pattern on both sides, the left later by that much (negative: the
// right); shelf_db / shelf_hz: a low shelf on the direct taps and reflections; opposed=1: every
// direct tap and reflection negative on the left and positive on the right. Octave trim: all nine bands' dB, 63 Hz first. Each
// value is rounded to float, as a table row's is.

#pragma once

#include "DuskVerbBandLimit.hpp"
#include "DuskVerbEarlyField.hpp"
#include "DuskVerbOctaveTrim.hpp"
#include "DuskVerbTextParse.hpp"
#include "Dv2StereoBands.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string_view>
#include <utility>

namespace duskverb
{

// The fourth post-tank EQ band, on the engine's output only (the early field does not pass it).
// On the two plates it shapes the music release. On Live Room it lifts the tank's top octave,
// which sat 4 to 8 dB under the reference's from 50 ms on (9 dB at 14 kHz brings it within 3).
struct TankTrebleEq
{
    std::string_view name;
    float hz;
    float q;
    float gainDb;
};
inline constexpr TankTrebleEq kTankTrebleEq[] {
    { "Live Room",           14000.0f, 1.0f,  9.0f },
    { "Vintage Vocal Plate", 10800.0f, 2.2f, -1.7f },
    { "Vocal Plate",         10000.0f, 1.0f,  0.77f },
};

// Linear scans avoid allocation on audio-thread preset swaps. Reverse Taps has no early
// field because an added reflection would precede its 104 ms swell.
inline constexpr std::array<std::pair<std::string_view, EarlyFieldConfig>, 16> kEarlyFieldByName = {{
    { "79 Vocal Chamber", { true, 4.25756359f, 96.0729065f, 12, -9.77004528f, 10.4802465f, 0.31861499f, -28.9938297f, -30.0085945f, 1, { 139.600006f, 0.0f, 0.0f, 0.0f }, 8108.08154f } },
    { "Ambience", { true, 7.06542063f, 59.5682564f, 35, -11.0286026f, 11.4732685f, 0.25128606f, -19.8183575f, -3.19089723f, 0, { 0.0f, 0.0f, 0.0f, 0.0f }, 12539.1162f } },
    { "Black Hole", { true, 21.2365303f, 26.2922516f, 30, 6.44021177f, 7.83394241f, 0.0345296673f, -37.6771126f, -43.4104271f, 0, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 7663.49902f } },
    { "Blade Runner 224", { true, 18.7080059f, 139.413055f, 14, 17.8804531f, 26.7212734f, 0.195128068f, -33.1625214f, -35.396122f, 1, { 72.5f, 83.5f, 96.6999969f, 107.199997f }, 4371.63037f } },
    { "Bright Hall", { true, 24.3353748f, 43.1242447f, 24, -2.03443122f, 32.0085106f, 0.122109905f, -44.5202599f, -21.0369987f, 4, { 57.7999992f, 84.0f, 146.399994f, 158.199997f }, 9670.87305f } },
    { "Cathedral Large Hall", { true, 5.31908464f, 64.9212952f, 9, 25.4141865f, 6.62954283f, 0.110564433f, -33.2627449f, -25.7314243f, 1, { 75.8000031f, 89.1999969f, 100.400002f, 120.400002f }, 3043.54565f } },
    { "Drum Plate", { true, 23.7823124f, 95.547348f, 52, -6.96349955f, 13.4607134f, 0.0510928929f, -37.5638885f, -25.0350285f, 0, { 0.0f, 0.0f, 0.0f, 0.0f }, 3548.1543f } },
    { "Large Chamber", { true, 7.30000019f, 66.0f, 16, -12.0f, 12.0f, 0.0799999982f, -23.5f, -25.0f, 5, { 53.4000015f, 81.9000015f, 104.800003f, 142.199997f, 162.300003f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 10000.0f } },
    // The side burst supplies the opening anti-phase low reflection without making the tank tail anti-phase.
    { "Live Room", { true, 21.5f, 37.0f, 22, -6.0f, 0.0f, 0.0f, -26.0f, -31.0f, 10, { 62.0f, 63.0f, 75.0f, 76.1999969f, 92.0f, 93.0999985f, 112.0f, 113.300003f, 140.0f, 141.199997f }, 18000.0f, -120.0f, 0.0f, 4.0f, 12000.0f, -120.0f, 15.0f, 30.0f, 2.0f, 250.0f, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 0.0f, 12.0f, 160.0f, true } },
    { "Medium Drum Room", { true, 15.3603783f, 269.405121f, 37, -13.6343489f, 25.6628685f, 0.114372894f, -49.5636711f, -44.9116936f, 2, { 40.0f, 51.2000008f, 92.1999969f, 191.800003f }, 2712.16797f } },
    // The reference (a Size 0 hall) is six discrete arrivals a side between 5.5 and 17 ms that hold
    // 89 % of its energy, then faint repeats near 70, 100 and 155 ms. The field's direct taps sit on
    // that window (the October row had them there at -31 dB, under a sparse early field that carried
    // the hit instead): 27 taps, because the two sides' tap signs come out anti-phase below 300 Hz
    // at 12 (inter-channel correlation -0.62; 0.00 at 27, tools repo earlyfield_screen.taps_kernel).
    // Reflections: the reference's first repeat is a group of arrivals at 62-80 ms that holds more than
    // what follows it, so five sit there and one each at 101 and 157 ms. Three single ones at one
    // level (the October row) did not fall off from 70 to 157 ms, which read as a slow decay at
    // 125 and 250 Hz and left the reflection at 68 ms without its prominence.
    { "Small Drum Room", { true, 5.30000019f, 12.0f, 27, -6.0f, 4.0f, 0.000949187553f, -18.0f, -24.0f, 7, { 62.4000015f, 64.4000015f, 67.3000031f, 71.5f, 79.6999969f, 101.400002f, 156.800003f, 0.0f, 0.0f, 0.0f }, 2468.91699f } },
    { "Tiled Room", { true, 31.1826839f, 60.6504745f, 18, 8.46437454f, 17.4806652f, 0.0751035661f, -33.7343483f, -9.97643089f, 0, { 31.5f, 55.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 7302.31396f } },
    // A short velvet-pulse onset places the response peak without a low-frequency echo.
    { "Vintage Gold Plate", { true, 27.4996796f, 27.0168591f, 19, -13.9333677f, 36.6331558f, 0.47833398f, -48.1144867f, -13.3144732f, 0, { 0.0f, 0.0f, 0.0f, 0.0f }, 5850.14111f, -36.0f, 28.87f, 5.3f, 10028.0f } },
    { "Vintage Vocal Plate", { true, 13.057683f, 43.1177406f, 19, 6.80627155f, 32.5891571f, 0.139030248f, -51.124691f, -42.8224068f, 1, { 114.300003f, 125.800003f, 154.600006f, 0.0f }, 8124.59082f } },
    { "Vocal Hall", { true, 17.5882149f, 126.435921f, 34, -9.35655785f, 16.4620075f, 0.183764786f, -40.6726379f, -42.1176491f, 0, { 42.5999985f, 66.1999969f, 121.199997f, 133.600006f }, 9869.50977f } },
    // The low shelf takes 9 dB off the field under 300 Hz: with the lows' decay set to the
    // reference's (kEnginePatchByName) the field's early lows left the snare and the music 5 to 7 dB
    // over the reference around 100 Hz and the tail 4 to 15 dB under it at 0.5 to 1 s (2026-11-21).
    { "Vocal Plate", { true, 16.104126f, 33.4986496f, 31, 15.3803778f, 8.20212746f, 0.317118138f, -13.7928858f, -11.1442814f, 3, { 45.5f, 61.0f, 73.8000031f, 82.5999985f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 13705.5713f, -120.0f, 0.0f, 4.0f, 12000.0f, -120.0f, 15.0f, 30.0f, 2.0f, 250.0f, { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f }, 0.0f, -9.0f, 300.0f } },
}};

inline constexpr EarlyFieldConfig kNoEarlyField {};

constexpr bool earlyFieldTableValid() noexcept
{
    for (size_t i = 0; i < kEarlyFieldByName.size(); ++i)
    {
        if (kEarlyFieldByName[i].first.empty() || ! EarlyField::valid (kEarlyFieldByName[i].second))
            return false;
        for (size_t j = 0; j < i; ++j)
            if (kEarlyFieldByName[j].first == kEarlyFieldByName[i].first)
                return false;
    }
    return true;
}
static_assert (earlyFieldTableValid(), "every early-field row names its preset once and keeps EarlyField's ranges");

// The early field preset `name` plays: its row, or none.
inline const EarlyFieldConfig& earlyFieldFor (std::string_view name) noexcept
{
    for (const auto& row : kEarlyFieldByName)
        if (row.first == name)
            return row.second;
    return kNoEarlyField;
}

namespace detail
{
inline std::string_view trimmed (const char* begin, const char* end) noexcept
{
    while (begin < end && (*begin == ' ' || *begin == '\t')) ++begin;
    while (end > begin && (end[-1] == ' ' || end[-1] == '\t')) --end;
    return { begin, static_cast<size_t> (end - begin) };
}

// A plain decimal filling all of `text`, locale free (DuskVerbTextParse.hpp).
inline bool parseNumber (std::string_view text, double& value) noexcept
{
    char buf[64];
    if (text.empty() || text.size() >= sizeof (buf))
        return false;
    std::memcpy (buf, text.data(), text.size());
    buf[text.size()] = '\0';
    const char* end = nullptr;
    value = parseAsciiDouble (buf, &end);
    return end == buf + text.size();
}

inline bool parseInteger (std::string_view text, int& value) noexcept
{
    double v = 0.0;
    if (! parseNumber (text, v) || ! (v >= -1.0e6 && v <= 1.0e6) || v != std::floor (v))
        return false;
    value = static_cast<int> (v);
    return true;
}
} // namespace detail

// Parses the tuning hook's text (see the top of this file) into `out`. Returns false, leaving
// `out` alone, on an unknown or repeated key, a value that is not a plain number (refl_ms: one
// to ten of them), or a config outside EarlyField's ranges; `error`, when given, then says which.
inline bool parseEarlyFieldConfig (const char* text, EarlyFieldConfig& out,
                                   char* error = nullptr, size_t errorSize = 0) noexcept
{
    const auto fail = [&] (const char* what, std::string_view item) {
        if (error != nullptr && errorSize > 0)
            std::snprintf (error, errorSize, "%s '%.*s'", what, static_cast<int> (item.size()),
                           item.empty() ? "" : item.data());
        return false;
    };
    if (text == nullptr || detail::trimmed (text, text + std::strlen (text)).empty())
        return fail ("empty", {});

    static constexpr std::string_view kKeys[] = { "enabled", "start_ms", "window_ms", "taps", "slope_db",
                                                  "attack_ms", "inject", "direct_db", "refl_db",
                                                  "refl_count", "refl_ms", "lowpass_hz", "burst_db",
                                                  "burst_start_ms", "burst_ms", "burst_lp_hz", "side_db",
                                                  "side_start_ms", "side_ms", "side_attack_ms", "side_lp_hz",
                                                  "refl_trim_db", "side_offset_ms", "shelf_db", "shelf_hz", "opposed" };
    bool seen[std::size (kKeys)] {};
    EarlyFieldConfig c;
    for (const char* p = text;;)
    {
        const char* q = p;
        while (*q != '\0' && *q != ',') ++q;
        const std::string_view item = detail::trimmed (p, q);
        const size_t eq = item.find ('=');
        if (eq == std::string_view::npos)
            return fail ("no key=value in", item);
        const std::string_view key = detail::trimmed (item.data(), item.data() + eq);
        const std::string_view value = detail::trimmed (item.data() + eq + 1, item.data() + item.size());
        size_t k = 0;
        while (k < std::size (kKeys) && kKeys[k] != key) ++k;
        if (k == std::size (kKeys))
            return fail ("unknown key", key);
        if (seen[k])
            return fail ("repeated key", key);
        seen[k] = true;

        bool ok = true;
        double v = 0.0;
        int n = 0;
        if (key == "enabled")         { ok = detail::parseInteger (value, n) && (n == 0 || n == 1); c.enabled = n == 1; }
        else if (key == "opposed")    { ok = detail::parseInteger (value, n) && (n == 0 || n == 1); c.sidesOpposed = n == 1; }
        else if (key == "taps")       { ok = detail::parseInteger (value, n); c.taps = n; }
        else if (key == "refl_count") { ok = detail::parseInteger (value, n); c.reflCount = n; }
        else if (key == "refl_ms" || key == "refl_trim_db")
        {
            auto& list = key == "refl_ms" ? c.reflMs : c.reflTrimDb;
            std::string_view rest = value;
            for (size_t r = 0; ok; ++r)
            {
                if (r == list.size())
                {
                    ok = false;                                    // more than ten values
                    break;
                }
                const size_t slash = rest.find ('/');
                const bool last = slash == std::string_view::npos;
                ok = detail::parseNumber (detail::trimmed (rest.data(), rest.data() + (last ? rest.size() : slash)), v);
                list[r] = static_cast<float> (std::clamp (v, -1.0e30, 1.0e30));
                if (last)
                    break;
                rest.remove_prefix (slash + 1);
            }
        }
        else
        {
            ok = detail::parseNumber (value, v);
            const float f = static_cast<float> (std::clamp (v, -1.0e30, 1.0e30));   // out of range: refused below
            if (key == "start_ms")       c.startMs = f;
            else if (key == "window_ms") c.windowMs = f;
            else if (key == "slope_db")  c.slopeDb = f;
            else if (key == "attack_ms") c.attackMs = f;
            else if (key == "inject")    c.inject = f;
            else if (key == "direct_db") c.directDb = f;
            else if (key == "refl_db")   c.reflDb = f;
            else if (key == "burst_db")  c.burstDb = f;
            else if (key == "burst_start_ms") c.burstStartMs = f;
            else if (key == "burst_ms")  c.burstMs = f;
            else if (key == "burst_lp_hz") c.burstLowpassHz = f;
            else if (key == "side_db")   c.sideDb = f;
            else if (key == "side_start_ms") c.sideStartMs = f;
            else if (key == "side_ms")   c.sideMs = f;
            else if (key == "side_attack_ms") c.sideAttackMs = f;
            else if (key == "side_lp_hz") c.sideLowpassHz = f;
            else if (key == "side_offset_ms") c.sideOffsetMs = f;
            else if (key == "shelf_db")  c.shelfDb = f;
            else if (key == "shelf_hz")  c.shelfHz = f;
            else                         c.lowpassHz = f;
        }
        if (! ok)
            return fail ("bad value in", item);
        if (*q == '\0')
            break;
        p = q + 1;
    }
    if (! EarlyField::valid (c))
        return fail ("out of range:", text);
    out = c;
    return true;
}

// `c` as the hook's text, each value spelled so it parses back to the same float.
inline int formatEarlyFieldConfig (const EarlyFieldConfig& c, char* out, size_t size) noexcept
{
    char refl[16 * kMaxEarlyReflections] = {}, trim[16 * kMaxEarlyReflections] = {};
    size_t used = 0;
    for (size_t r = 0; r < c.reflMs.size() && used < sizeof (refl); ++r)
        used += static_cast<size_t> (std::snprintf (refl + used, sizeof (refl) - used, r == 0 ? "%.9g" : "/%.9g",
                                                    static_cast<double> (c.reflMs[r])));
    used = 0;
    for (size_t r = 0; r < c.reflTrimDb.size() && used < sizeof (trim); ++r)
        used += static_cast<size_t> (std::snprintf (trim + used, sizeof (trim) - used, r == 0 ? "%.9g" : "/%.9g",
                                                    static_cast<double> (c.reflTrimDb[r])));
    return std::snprintf (out, size,
                          "enabled=%d,start_ms=%.9g,window_ms=%.9g,taps=%d,slope_db=%.9g,attack_ms=%.9g,"
                          "inject=%.9g,direct_db=%.9g,refl_db=%.9g,refl_count=%d,refl_ms=%s,lowpass_hz=%.9g,"
                          "burst_db=%.9g,burst_start_ms=%.9g,burst_ms=%.9g,burst_lp_hz=%.9g,"
                          "side_db=%.9g,side_start_ms=%.9g,side_ms=%.9g,side_attack_ms=%.9g,side_lp_hz=%.9g,refl_trim_db=%s,"
                          "side_offset_ms=%.9g,shelf_db=%.9g,shelf_hz=%.9g,opposed=%d",
                          c.enabled ? 1 : 0, static_cast<double> (c.startMs), static_cast<double> (c.windowMs),
                          c.taps, static_cast<double> (c.slopeDb), static_cast<double> (c.attackMs),
                          static_cast<double> (c.inject), static_cast<double> (c.directDb),
                          static_cast<double> (c.reflDb), c.reflCount, refl, static_cast<double> (c.lowpassHz),
                          static_cast<double> (c.burstDb), static_cast<double> (c.burstStartMs),
                          static_cast<double> (c.burstMs), static_cast<double> (c.burstLowpassHz),
                          static_cast<double> (c.sideDb), static_cast<double> (c.sideStartMs),
                          static_cast<double> (c.sideMs), static_cast<double> (c.sideAttackMs),
                          static_cast<double> (c.sideLowpassHz), trim, static_cast<double> (c.sideOffsetMs),
                          static_cast<double> (c.shelfDb), static_cast<double> (c.shelfHz), c.sidesOpposed ? 1 : 0);
}

// Nine octave gains in dB, starting at 63 Hz; applied with the early-field table on preset swaps.
inline constexpr std::array<std::pair<std::string_view, OctaveTrimGains>, 17> kOctaveTrimByName {{
    { "79 Vocal Chamber", {{ 0.634247541f, 0.842392206f, -0.207816824f, 0.0676435381f, -0.0266304873f, 0.336835057f, 0.43775034f, 0.643186927f, 0.988179445f }} },
    { "Ambience", {{ -2.9757f, -1.8353f, -1.2269f, 1.5722f, 1.0831f, 2.9842f, -0.9412f, 1.686f, 0.4635f }} },
    { "Black Hole", {{ 0.442480981f, 1.24710643f, -0.75896579f, -0.185511813f, 0.490530103f, -0.0265136957f, -2.44492841f, -0.747641146f, 0.278195381f }} },
    { "Blade Runner 224", {{ -0.587038398f, -0.438739717f, -1.16214919f, -0.132971317f, -1.19541728f, -1.0167501f, 0.573673308f, -1.29499841f, -1.23172176f }} },
    { "Bright Hall", {{ 0.349530995f, 0.1005008f, 0.68877244f, 2.14985514f, 2.487679f, 1.83931005f, 0.688625991f, 1.53471124f, -2.24387121f }} },
    { "Deep Blue Day", {{ 0.515359402f, 1.46957862f, -0.612119913f, 1.24398839f, 1.23909926f, -0.224919796f, 0.0697899982f, 0.141160831f, 0.900486708f }} },
    { "Drum Plate", {{ 1.29575813f, -0.106312878f, -0.227102667f, -1.63319695f, 0.337353289f, -1.0894171f, -0.775684178f, 1.59402931f, -2.33097458f }} },
    { "Large Chamber", {{ -3.0f, 0.180000007f, -1.50999999f, -1.47000003f, -1.80999994f, -1.78999996f, -2.98000002f, -3.0f, 0.899999976f }} },
    { "Live Room", {{ 0.400000006f, -1.63f, 0.379999995f, -2.32999992f, -2.31999993f, -2.44000006f, -0.860000014f, -3.0f, -3.0f }} },
    // This tilt balances low-mid energy against the densifier and band-level settings.
    { "Medium Drum Room", {{ -0.298f, -0.982f, -1.917f, -1.799f, -1.011f, -0.531f, 1.716f, 0.614f, -2.985f }} },
    { "Reverse Taps", {{ 0.316647798f, 2.20800257f, 2.27045536f, 2.24588609f, 0.484986275f, 0.368917704f, -0.371552557f, 1.25592148f, -1.37708199f }} },
    // The least-squares row for the reference's match-EQ curve (the whole clip's third octaves, snare
    // and music at equal loudness; tools repo earlyfield_adopt.refit_trim), fitted on renders with the
    // early field row (seven reflections), no sparse early field and Width 1.05: every band of both clips inside 3 dB.
    // The October row cut 2 kHz by 2.2 dB where the preset was 5 dB short. 4 kHz, 8 kHz and 16 kHz
    // end on the range's limits.
    { "Small Drum Room", {{ -1.048f, -0.714f, -2.705f, 1.849f, 2.629f, 0.767f, 3.0f, 3.0f, -3.0f }} },
    { "Tiled Room", {{ 2.96405005f, -0.11537648f, -1.23354101f, 0.0184728466f, 0.38636291f, 0.236264035f, -0.0842026472f, 1.81604993f, 1.36784935f }} },
    { "Vintage Gold Plate", {{ 1.00932944f, -1.89307177f, -0.025091961f, 0.958136261f, -1.1617521f, 2.40072703f, -2.73682666f, -2.15866685f, -1.0f }} },
    { "Vintage Vocal Plate", {{ -0.785391271f, -0.0869825855f, 1.26347637f, 0.367175251f, 2.13951373f, -0.3122679f, -1.34776425f, -1.06126356f, 0.296430409f }} },
    { "Vocal Plate", {{ -1.8f, 3.0f, 3.0f, -2.29f, 0.07f, -1.43f, -0.62f, -0.57f, -2.72f }} },   // refit 2026-11-21 with the field shelf and the lows' decay
    { "Vocal Hall", {{ -0.3929f, -0.3537f, -2.5837f, -0.588f, -0.8468f, -2.499f, 0.1842f, -2.5404f, -2.462f }} },
}};

inline constexpr OctaveTrimGains kFlatOctaveTrim {};

constexpr bool octaveTrimTableValid() noexcept
{
    for (auto row = kOctaveTrimByName.begin(); row != kOctaveTrimByName.end(); ++row)
    {
        if (row->first.empty() || ! OctaveTrim::valid (row->second))
            return false;
        for (auto earlier = kOctaveTrimByName.begin(); earlier != row; ++earlier)
            if (earlier->first == row->first)
                return false;
    }
    return true;
}
static_assert (octaveTrimTableValid(), "every octave-trim row names its preset once and keeps OctaveTrim's range");

// The octave trim preset `name` plays: its row, or flat.
inline const OctaveTrimGains& octaveTrimFor (std::string_view name) noexcept
{
    for (const auto& row : kOctaveTrimByName)
        if (row.first == name)
            return row.second;
    return kFlatOctaveTrim;
}

// Parses the octave-trim hook's text into `out`: nine plain numbers, comma-separated, the bands'
// dB from 63 Hz up. Returns false, leaving `out` alone, on any other count, a value that is not a
// plain number, or one outside OctaveTrim's range; `error`, when given, then says which.
inline bool parseOctaveTrim (const char* text, OctaveTrimGains& out, char* error = nullptr, size_t errorSize = 0) noexcept
{
    const auto fail = [&] (const char* what, std::string_view item) {
        if (error != nullptr && errorSize > 0)
            std::snprintf (error, errorSize, "%s '%.*s'", what, static_cast<int> (item.size()),
                           item.empty() ? "" : item.data());
        return false;
    };
    const std::string_view all = text != nullptr ? detail::trimmed (text, text + std::strlen (text)) : std::string_view {};
    if (all.empty())
        return fail ("empty", {});

    OctaveTrimGains gains {};
    size_t count = 0;
    for (const char* p = text;;)
    {
        const char* q = p;
        while (*q != '\0' && *q != ',') ++q;
        const std::string_view item = detail::trimmed (p, q);
        if (count == gains.size())
            return fail ("more than nine values in", all);
        double v = 0.0;
        if (! detail::parseNumber (item, v))
            return fail ("bad value", item);
        const float db = static_cast<float> (std::clamp (v, -1.0e30, 1.0e30));   // out of range: refused next
        if (! (db >= -OctaveTrim::kMaxDb && db <= OctaveTrim::kMaxDb))
            return fail ("out of range:", item);
        gains[count++] = db;
        if (*q == '\0')
            break;
        p = q + 1;
    }
    if (count != gains.size())
        return fail ("fewer than nine values in", all);
    out = gains;
    return true;
}

// `gains` as the hook's text, each value spelled so it parses back to the same float.
inline int formatOctaveTrim (const OctaveTrimGains& gains, char* out, size_t size) noexcept
{
    return std::snprintf (out, size, "%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g",
                          static_cast<double> (gains[0]), static_cast<double> (gains[1]),
                          static_cast<double> (gains[2]), static_cast<double> (gains[3]),
                          static_cast<double> (gains[4]), static_cast<double> (gains[5]),
                          static_cast<double> (gains[6]), static_cast<double> (gains[7]),
                          static_cast<double> (gains[8]));
}

// One row per preset with a band limit on the wet (DuskVerbBandLimit.hpp): the low-pass corner and
// order, then the lift under it. Read at every preset swap, like the tables above. Searched on real
// renders with the trim flat and both filters applied offline, which is exact for LTI stages on the
// wet (tools repo classic_lab/phaseC/bandlimit_refine.py); kept only where full_check's honest
// count drops and no sanity gate opens.
inline constexpr std::array<std::pair<std::string_view, BandLimitSetting>, 2> kBandLimitByName {{
    // The reference has a steep cutoff near 8 kHz; pair this limit with qt_air_mult in kPresetOverrides.
    { "79 Vocal Chamber", { 8750.414f, 24, 8375.813f, 3.121f, 1.238f } },
    // The reference ends at 15 kHz (13 dB down at 15-16 kHz, 36 dB at 16-17 kHz, nothing above 18 kHz)
    // where the preset ran flat to Nyquist: 10 dB over it in the 16 kHz third octave of the snare.
    // Order 8 is the lowest that brings that band inside 3 dB; no lift. 13.8 kHz once the early field
    // row carried the hit (its taps are brighter than the tank up there): 14.5 kHz left the band
    // 5 dB over, 13.2 kHz takes it 5 dB under.
    { "Small Drum Room", { 13800.0f, 8, 0.0f, 0.0f, 0.0f } },
}};

inline constexpr BandLimitSetting kNoBandLimit {};

constexpr bool bandLimitTableValid() noexcept
{
    for (auto row = kBandLimitByName.begin(); row != kBandLimitByName.end(); ++row)
    {
        if (row->first.empty() || ! BandLimit::valid (row->second) || row->second == kNoBandLimit)
            return false;
        for (auto earlier = kBandLimitByName.begin(); earlier != row; ++earlier)
            if (earlier->first == row->first)
                return false;
    }
    return true;
}
static_assert (bandLimitTableValid(), "every band-limit row names its preset once, does something, and keeps BandLimit's ranges");

// The band limit preset `name` plays: its row, or none.
inline const BandLimitSetting& bandLimitFor (std::string_view name) noexcept
{
    for (const auto& row : kBandLimitByName)
        if (row.first == name)
            return row.second;
    return kNoBandLimit;
}

// Parses the band-limit hook's text into `out`: five plain numbers, comma-separated, the low-pass
// corner (Hz) and order, then the lift's centre (Hz), gain (dB) and Q; "0,0,0,0,0" is none. Returns
// false, leaving `out` alone, on any other count, a value that is not a plain number, an order that
// is not a whole number, or a setting outside BandLimit::valid; `error`, when given, then says which.
inline bool parseBandLimit (const char* text, BandLimitSetting& out, char* error = nullptr, size_t errorSize = 0) noexcept
{
    const auto fail = [&] (const char* what, std::string_view item) {
        if (error != nullptr && errorSize > 0)
            std::snprintf (error, errorSize, "%s '%.*s'", what, static_cast<int> (item.size()),
                           item.empty() ? "" : item.data());
        return false;
    };
    const std::string_view all = text != nullptr ? detail::trimmed (text, text + std::strlen (text)) : std::string_view {};
    if (all.empty())
        return fail ("empty", {});

    std::array<double, 5> v {};
    size_t count = 0;
    for (const char* p = text;;)
    {
        const char* q = p;
        while (*q != '\0' && *q != ',') ++q;
        const std::string_view item = detail::trimmed (p, q);
        if (count == v.size())
            return fail ("more than five values in", all);
        if (! detail::parseNumber (item, v[count]))
            return fail ("bad value", item);
        ++count;
        if (*q == '\0')
            break;
        p = q + 1;
    }
    if (count != v.size())
        return fail ("fewer than five values in", all);
    if (! (v[1] >= 0.0 && v[1] <= BandLimit::kMaxOrder) || v[1] != std::floor (v[1]))
        return fail ("order not a whole number from 0 to 24 in", all);
    const auto toFloat = [] (double x) { return static_cast<float> (std::clamp (x, -1.0e30, 1.0e30)); };
    const BandLimitSetting s { toFloat (v[0]), static_cast<int> (v[1]), toFloat (v[2]), toFloat (v[3]), toFloat (v[4]) };
    if (! BandLimit::valid (s))
        return fail ("out of range:", all);
    out = s;
    return true;
}

// `s` as the hook's text, each value spelled so it parses back to the same float.
inline int formatBandLimit (const BandLimitSetting& s, char* out, size_t size) noexcept
{
    return std::snprintf (out, size, "%.9g,%d,%.9g,%.9g,%.9g", static_cast<double> (s.cornerHz), s.order,
                          static_cast<double> (s.liftHz), static_cast<double> (s.liftDb), static_cast<double> (s.liftQ));
}

// One row per preset whose shared engine DuskVerb 2 sets differently from the legacy build, applied
// right after the shared engine config at every preset swap; a preset without one (or a session
// without a preset) gets every feature off, which is the legacy build's engine. Read at every swap,
// like the tables above.
//
// pmbDecorrelatedSide: ParallelMultibandTank::setDecorrelatedSide (the band's side from line 1's
// read; L + R unchanged). pmbWidth: the six bands' output width over the shared table's (below 0:
// keep it); with the decorrelated side, width 1 is a decorrelated band and above 1 leans anti-phase.
// octaveDecay: multipliers on the decay of each octave, 63 Hz to 16 kHz, over whichever per-octave
// table the preset's engine plays (Dattorro, AccurateHall, DenseHall), and on the ParallelMultiband
// bands as the octaves they span (geometric mean). 1 leaves the octave as the shared config set it.
// modeSmear: {depth in samples, rate in Hz} of the tank's slow delay wander that smears an isolated
// tail mode (DuskVerbEngine::setDattorroModeSmear: the Dattorro, AccurateHall, ParallelMultiband and
// Shimmer tanks); a negative depth keeps the shared config's. pmbSmearShare: each ParallelMultiband
// band's share of that depth, 0..1 (ParallelMultibandTank::setModeSmearBandShare), so a smear aimed at
// one band's ring leaves the others still. densityJitter: the Dattorro density allpasses' read
// jitter as a fraction of their length (DuskVerbEngine::setDattorroDensityJitter, 0..0.02); a
// jittered allpass reads through cubic Hermite on every pass, which caps the top octave's decay
// (Drum Plate), so 0 frees it; below 0 keeps the shared config's. stereoBands: the side
// gains of DuskVerb 2's own band width stage (Dv2StereoBands.hpp: below 300 Hz, to 5 kHz, above),
// on top of the Width control; a row puts the engine's own (one-pole) width bands at unity. Below
// 0: no stage, the engine's width bands as the shared config set them. pmbDensifier: each
// ParallelMultiband band's in-loop allpass coefficient, 0..0.75 (ParallelMultibandTank::
// setDensifierCoeff: at the tank's 0.55 the allpasses bunch a band's modes and let them ring
// longer); below 0 keeps the tank's. pmbLevel: each band's output level over the shared row's, a
// multiplier in 0.1..4; 1 leaves it.
// sparseERLowpassHz: a 4th-order Butterworth low-pass on the DenseHall composite's sparse ER front
// (DuskVerbEngine::setSparseERTone), the early field that composite plays; 0: off, the field broadband
// as the shared engine has it.
// dualSlope: {ratio, fast-line output gain} for the FDN/AccurateHall tanks (DuskVerbEngine::setFDNDualSlope):
// half the lines decay at RT60 x ratio, a mixture of two rates; ratio 0: off, every line at RT60.
// fdnDelays: the 16 base delays of the 16-line tanks in samples at 44.1 kHz (DuskVerbEngine::
// setFDNBaseDelays: FDN, AccurateHall, the Shimmer hall), each from 64 to kFdnDelayMax and all
// distinct; all 0: the set the shared config installs (its kBaseDelaysByName row or the default).
// fbRotation: {peak angle in radians, rate in Hz} of the tank's time-varying feedback matrix
// (DuskVerbEngine::setFeedbackRotation): the FDN tanks' line pairs (FDN, AccurateHall, the Shimmer
// hall, the multiband tanks) or the Dattorro tank's cross-feed turn by up to the peak angle at about
// the rate, which moves the tail's modes with less pitch drift than a delay wander (the mode
// smear); 0 to pi/2 rad, 0.01 to 10 Hz; angle 0: off.
// sparseERShape: {onset ms, decay ms, late-burst gain, level} of the composite's sparse early field
// (DuskVerbEngine::setSparseFieldOnsetMs, setSparseFieldDecayMs, setSparseFieldBurst2Gain,
// setSparseERGain) over the shared composite row's, which DuskVerb 1 keeps: onset 0 to 60 ms, decay
// 10 to 200 ms, burst gain 0 to 4, level 0 to 2; a value below 0 keeps the shared row's. Level 0
// also switches the Dattorro composite off (DuskVerbEngine::setDattorroEarlyField), which skips the
// sparse field's processing; the output is the same as with it running at no level.
inline constexpr int kFdnDelayMax = 6700;   // FDNReverb::kMaxBaseDelay (private there: setBaseDelays clamps to it)

// tankOnset: {build-up amount, build-up time scale, early duck amount, hold ms, threshold} of the
// DenseHall composite's tank (DuskVerbEngine::setBuildupAmount / setBuildupTimeScale: the allpass
// cascade the tank builds through, 0..1 and 0.5..3; setEarlyTankDuck: the tank held down by the
// amount for the hold after an onset). Each of the first two below 0 keeps the shared row's, and
// so do the last three together.
// denseHallStaticAllpasses: which of the DenseHall tank's allpasses lose their modulation
// (DuskVerbEngine::setDenseHallAllpassModulation). 0: none, the tank as the shared engine has it.
// 1: the ten input allpasses a side. 2: those and each line's three loop allpasses. A modulated
// allpass reads between samples through a cubic that loses treble (1.5 dB at 16 kHz a read at
// 48 kHz); a still one is exact. The delay lines keep their modulation, which is what moves the
// tail's modes.
// sustainLimitMid / sustainLimitLow: the engine's two level-keyed ducks of the wet feed (DuskVerbEngine::
// setSustainLimiterMid / setSustainLimiterLow: band edges, threshold, most cut, attack, fast and
// slow release), over the shared rows'; most cut 0 switches one off. The shared rows were sized
// while the judge matched level on a noise burst that is mostly infrasound (tools repo
// tuner/fleet_audit.py rms), so they cut for a level error that was the judge's.
struct EnginePatch
{
    bool pmbDecorrelatedSide = false;
    std::array<float, 6> pmbWidth { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
    std::array<float, 9> octaveDecay { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    std::array<float, 2> modeSmear { -1.0f, 0.0f };
    std::array<float, 6> pmbSmearShare { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    float densityJitter = -1.0f;
    StereoBands::Gains stereoBands = StereoBands::kOff;
    std::array<float, 6> pmbDensifier { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
    std::array<float, 6> pmbLevel { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    float sparseERLowpassHz = 0.0f;
    std::array<float, 2> dualSlope { 0.0f, 1.0f };
    std::array<int, 16> fdnDelays {};
    std::array<float, 2> fbRotation { 0.0f, 1.0f };
    std::array<float, 4> sparseERShape { -1.0f, -1.0f, -1.0f, -1.0f };
    std::array<float, 5> tankOnset { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f };
    std::array<float, 7> sustainLimitMid { -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<float, 7> sustainLimitLow { -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    int denseHallStaticAllpasses = 0;
};

// Whether {low Hz, high Hz, threshold dB, most cut dB, attack ms, fast release ms, slow release ms}
// is a sustain limiter the patch may play (EnginePatch::sustainLimitMid / sustainLimitLow); a low
// edge below 0 keeps the shared row's.
constexpr bool sustainLimitValid (const std::array<float, 7>& v) noexcept
{
    return v[0] < 0.0f
        || (v[0] >= 20.0f && v[1] > v[0] && v[1] <= 20000.0f && v[2] >= -80.0f && v[2] <= 0.0f && v[3] >= 0.0f && v[3] <= 24.0f
            && v[4] >= 1.0f && v[4] <= 20000.0f && v[5] >= 1.0f && v[5] <= 20000.0f && v[6] >= 1.0f && v[6] <= 60000.0f);
}

// Whether {build-up amount, build-up time scale, early duck amount, hold ms, threshold} is a tank
// onset the patch may play (EnginePatch::tankOnset): the time scale's range is BuildupDiffuser::
// setTimeScale's, and a duck names all three of its values.
constexpr bool tankOnsetValid (const std::array<float, 5>& v) noexcept
{
    const bool duck = v[2] < 0.0f ? v[3] < 0.0f && v[4] < 0.0f
                                  : v[2] <= 1.0f && v[3] >= 1.0f && v[3] <= 1000.0f && v[4] >= 0.0f && v[4] <= 4.0f;
    return (v[0] < 0.0f || v[0] <= 1.0f) && (v[1] < 0.0f || (v[1] >= 0.5f && v[1] <= 3.0f)) && duck;
}

// Whether {onset ms, decay ms, late-burst gain, level} is a sparse early field shape the patch may
// play (EnginePatch::sparseERShape); the decay's floor is SparseEarlyField::setDecayMs's and the
// level's ceiling DuskVerbEngine::setSparseERGain's.
constexpr bool sparseERShapeValid (const std::array<float, 4>& v) noexcept
{
    return (v[0] < 0.0f || v[0] <= 60.0f) && (v[1] < 0.0f || (v[1] >= 10.0f && v[1] <= 200.0f))
        && (v[2] < 0.0f || v[2] <= 4.0f) && (v[3] < 0.0f || v[3] <= 2.0f);
}

// Whether {peak angle, rate} is a feedback rotation the patch may play (EnginePatch::fbRotation).
constexpr bool fbRotationValid (const std::array<float, 2>& r) noexcept
{
    return r[0] >= 0.0f && r[0] <= 1.5707964f && r[1] >= 0.01f && r[1] <= 10.0f;
}

// Whether `d` is a delay row the patch may play: all 0 (none), or every delay in 64..kFdnDelayMax
// and no two alike (two equal lines share their modes).
constexpr bool fdnDelaysValid (const std::array<int, 16>& d) noexcept
{
    bool none = true;
    for (int v : d)
        none = none && v == 0;
    if (none)
        return true;
    for (size_t i = 0; i < d.size(); ++i)
    {
        if (d[i] < 64 || d[i] > kFdnDelayMax)
            return false;
        for (size_t j = 0; j < i; ++j)
            if (d[j] == d[i])
                return false;
    }
    return true;
}

namespace detail
{
// Exactly N comma-separated plain numbers, each with lo <= v <= hi when `closed`, else lo < v < hi
// (locale free, as the other hooks), into `out`; false, leaving `out` alone, on anything else.
template <size_t N>
bool parseNumberList (const char* text, std::array<float, N>& out, double lo, double hi, bool closed) noexcept
{
    if (text == nullptr)
        return false;
    std::array<float, N> m {};
    size_t count = 0;
    for (const char* p = text;;)
    {
        const char* q = p;
        while (*q != '\0' && *q != ',') ++q;
        double v = 0.0;
        if (count == m.size() || ! parseNumber (trimmed (p, q), v)
            || ! (closed ? (v >= lo && v <= hi) : (v > lo && v < hi)))
            return false;
        m[count++] = static_cast<float> (v);
        if (*q == '\0')
            break;
        p = q + 1;
    }
    if (count != m.size())
        return false;
    out = m;
    return true;
}
} // namespace detail

// Parses the octave-decay hook's text into `out`: nine multipliers, comma-separated, 63 Hz first,
// each a plain number between 0.05 and 20 (locale free, as the other hooks). Returns false, leaving
// `out` alone, on anything else.
inline bool parseOctaveDecay (const char* text, std::array<float, 9>& out) noexcept
{
    return detail::parseNumberList (text, out, 0.05, 20.0, false);
}

// Parses the stereo-band hook's text into `out`: the three side gains (below 300 Hz, to 5 kHz,
// above), each 0..StereoBands::kMaxGain. Returns false, leaving `out` alone, on anything else.
inline bool parseStereoBands (const char* text, StereoBands::Gains& out) noexcept
{
    return detail::parseNumberList (text, out, 0.0, StereoBands::kMaxGain, true);
}

// Parses the smear-share hook's text into `out`: the six ParallelMultiband bands' shares, lowest
// band first, each 0..1. Returns false, leaving `out` alone, on anything else.
inline bool parsePmbSmearShare (const char* text, std::array<float, 6>& out) noexcept
{
    return detail::parseNumberList (text, out, 0.0, 1.0, true);
}

// Parses the densifier hook's text into `out`: the six ParallelMultiband bands' in-loop allpass
// coefficients, lowest band first, each -1..0.75 (below 0: the tank's own). Returns false, leaving
// `out` alone, on anything else.
inline bool parsePmbDensifier (const char* text, std::array<float, 6>& out) noexcept
{
    return detail::parseNumberList (text, out, -1.0, 0.75, true);
}

// Parses the band-level hook's text into `out`: the six ParallelMultiband bands' level multipliers,
// lowest band first, each 0.1..4. Returns false, leaving `out` alone, on anything else.
inline bool parsePmbLevel (const char* text, std::array<float, 6>& out) noexcept
{
    return detail::parseNumberList (text, out, 0.1, 4.0, true);
}

inline constexpr std::array<std::pair<std::string_view, EnginePatch>, 13> kEnginePatchByName {{
    // The shared row builds the tank through its longest cascade (time scale 1.7) and ducks it by
    // half for 165 ms after a hit. Together they leave the impulse response 10 to 17 dB under the
    // reference from 60 to 210 ms and bring the tank in at 220 ms, 4 dB over it; the reference
    // holds one level from 50 to 200 ms (tools repo classic_lab/energy_map.py, 2026-11-20). No
    // duck, and the cascade at time scale 0.6, not its shortest (0.5): on the shortest the tank put
    // the snare's 125 and 250 Hz body in at 100 to 200 ms and let it go, 6 dB down 17 and 14 % ahead
    // of the reference ("hits, then the body is lost quicker", Marc 2026-11-20); at 0.6 they are 6
    // and 9 % ahead. 0.55 and 0.65 hold the 500 Hz octave 25 to 30 % late; the cascade's delays move
    // its modes, so the setting is not a smooth knob. 125 Hz decays 10 % longer (T60 within 2 % of
    // the reference's, from 7 % short). 63 Hz decays 5 % longer: at 10 % the low end rang on after
    // the music (40-125 Hz over the body 3.8 dB above the reference's 3.5 to 6 s after it, heard as
    // "boomier"), at none the 40-100 Hz tail of the noise burst sat 3.2 dB under the reference's at
    // 1 to 2 s; DuskVerb 2's modes there are not the reference's, and 5 % keeps both within their
    // gates (2.3 and 2.1 dB; tools repo fleet_audit, 2026-11-21). The mid limiter
    // is off: with level matched above 40 Hz the 1 kHz sine reads 0.8 dB over the reference without
    // it and 2.2 to 3.4 dB under with it. The low limiter cuts 2 dB, not 6: the piano's tail keeps
    // its lows (2.5 dB under the reference, 5.8 dB at 6) and its low growth stays inside its gate
    // (1.2 dB; 1.7 dB with the limiter off).
    // Input allpasses still (denseHallStaticAllpasses 1): 200 to 800 ms into the impulse response
    // the tank then sits 3 dB under the reference at 12.7 kHz and 11 dB at 16 kHz, from 6 and 18,
    // and the 12 to 16 kHz release of the music comes inside its gate without any lift. With the
    // loop allpasses still as well (2) the top end follows the reference to 16 kHz, but the tail's
    // pitch drift drops to 0.10 Hz against the reference's 0.23 and the sheet reads two worse.
    { "Large Chamber", { false, { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f }, { 1.05f, 1.1f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },
                         { -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }, -1.0f, StereoBands::kOff,
                         { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }, 0.0f, { 0.0f, 1.0f }, {}, { 0.0f, 1.0f },
                         { -1.0f, -1.0f, -1.0f, -1.0f }, { 1.0f, 0.6f, 0.0f, 165.0f, 0.35f },
                         { 700.0f, 1400.0f, -13.96f, 0.0f, 120.0f, 80.0f, 4000.0f },
                         { 100.0f, 600.0f, -27.0f, 2.0f, 8000.0f, 80.0f, 12000.0f }, 1 } },
    // Lower densifier coefficients spread the clustered low-mid modes without moving the attack.
    { "Medium Drum Room", { true, { 1.0f, 1.0f, 1.15f, 1.15f, 1.2f, 1.2f },
                            { 1.05023f, 0.983058f, 0.983058f, 0.97726f, 1.0728f, 1.0728f, 0.973603f, 1.0f, 1.0f },
                            { 15.0f, 1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f }, -1.0f, StereoBands::kOff,
                            { -1.0f, 0.0810736f, -1.0f, 0.18248f, -1.0f, -1.0f },
                            { 1.0f, 0.947234f, 1.05594f, 0.986082f, 0.984391f, 1.0f } } },
    // Recalibrated delays avoid strong beating modes without the pitch drift of delay smear.
    // The reference's sparse low-frequency decay cannot match both a fitted T60 and every energy window.
    { "Ambience", { false, { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f },
                    { 1.457f, 0.86f, 1.028f, 1.064f, 1.091f, 0.9447f, 1.036f, 1.006f, 1.337f },
                    { 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }, -1.0f, StereoBands::kOff,
                    { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }, 0.0f, { 0.0f, 1.0f },
                    { 794, 1018, 1046, 1317, 1502, 2109, 2483, 2881, 3273, 4168, 3879, 4790, 4666, 5268, 6188, 6176 } } },
    // Filter the sparse early field rather than lowering its gain, which also carries the attack.
    { "Blade Runner 224", { false, { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.15f },
                            { -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }, -1.0f, StereoBands::kOff,
                            { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }, 8000.0f } },
    { "Tiled Room", { false, { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f }, { 1.0f, 1.15f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f } } },
    // The shared table's 63 and 125 Hz decay (0.41 and 0.39 s, shortened in July against a judge
    // that read the mono sum and matched gain on the whole band) left the realized T60 28 and 23 %
    // under the reference's 0.62 and 0.54 s, and the 40-200 Hz tail 21 to 24 dB under it at 0.5 to
    // 1 s; 500 Hz ran 11 % long. With these the four octaves read within 10 % (2026-11-21).
    { "Vocal Plate", { false, { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f }, { 1.38f, 1.29f, 1.0f, 0.9f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f } } },
    { "Vintage Vocal Plate", { false, { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f }, { 1.15f, 1.0f, 0.85f, 1.0f, 1.0f, 1.15f, 1.0f, 1.0f, 1.0f } } },
    // The reference (Lexicon Medium Live Room 1) opens with thirteen taps a side inside 15 ms, the
    // left ones negative and the right ones positive, and its lows stay in opposite phase for the
    // first 200 ms (correlation -0.8 at 125 Hz, 100 to 200 ms in) and are uncorrelated after. The
    // early field row plays its taps that way and carries them to 140 ms with five pairs of
    // reflections, one of each pair on either side about a millisecond apart, so the pair is in
    // opposite phase below a few hundred Hz. The tank's own lows are uncorrelated; the band width
    // stage widens them only 1.15 times, which with the field reads -0.84 below 300 Hz against
    // the reference's -0.86 and the music's side/mid 2.1 dB under it, and leaves the tail's lows
    // out of opposite phase as the reference's are (2.5 times read the music 1.8 dB under, with
    // the tail at -0.74). The mids are narrowed to 0.85 (correlation +0.07 against +0.12).
    // Both limiters are off. They were sized while the judge read the mono sum, in which this
    // reference's lows cancel: the mid one then cut a 1 kHz sine by 12.9 dB under the reference
    // (0.8 dB over it without), and the low one, charging on sustained input, cut the lows' decay
    // from 0.88 s to 0.60 s at 63 Hz against the reference's 0.93 s.
    // With them off the octave decays want 125 Hz and 500 Hz a fifth longer and 250 Hz a tenth
    // shorter (0.82 / 0.81 / 0.57 s read against 0.99 / 0.72 / 0.69 s).
    { "Live Room", { false, { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f }, { 1.0f, 1.2f, 0.89f, 1.2f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }, { 40.0f, 2.5f }, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }, -1.0f, { 1.15f, 0.85f, 1.0f },
                     { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }, 0.0f, { 0.0f, 1.0f }, {}, { 0.0f, 1.0f },
                     { -1.0f, -1.0f, -1.0f, -1.0f }, { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f },
                     { 700.0f, 1400.0f, -19.08f, 0.0f, 120.0f, 80.0f, 4000.0f },
                     { 40.0f, 140.0f, -35.0f, 0.0f, 2000.0f, 80.0f, 12000.0f } } },
    { "Black Hole", { false, { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }, { 50.0f, 2.0f } } },
    // Cubic-Hermite reads in jittered density allpasses shorten top-octave decay; disable jitter here.
    { "Drum Plate", { false, { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f }, { 1.0f, 1.05f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.8178f, 0.3f }, { -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }, 0.0f } },
    // Uniform band gain avoids the excess anti-phase high-frequency output of the shared width row.
    // No sparse early field: the shared row's (onset 10 ms, decay 40 ms, a burst at 90 ms at 1.5) put
    // 39 % of the impulse's energy at 50-150 ms where the reference has 6 %, so a snare read as a
    // reverb behind the hit next to a reference that reads as the hit itself (clarity C50 8.1 dB
    // under it). This preset's early field row (kEarlyFieldByName) places the hit instead; skipping
    // the sparse field pays for that row's 27 taps.
    { "Small Drum Room", { false, { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }, { -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }, -1.0f, { 0.969163f, 0.969163f, 0.969163f },
                           { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }, 0.0f, { 0.0f, 1.0f }, {}, { 0.0f, 1.0f },
                           { -1.0f, -1.0f, -1.0f, 0.0f } } },
    // Restrict mode smear to 1-3 kHz: whole-tank modulation loses top-octave decay,
    // while including the lower band reduces energy around 250-500 Hz.
    { "Vocal Hall", { false, { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },
                      { 10.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f } } },
    // Feedback rotation reduces isolated modes with less pitch drift than delay modulation.
    // The matching Lo Cut override compensates its low-frequency level change.
    { "Deep Blue Day", { false, { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },
                         { 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }, -1.0f, StereoBands::kOff,
                         { -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f }, 0.0f, { 0.0f, 1.0f },
                         { 1237, 1331, 1558, 1574, 1836, 1920, 2081, 2542, 2980, 3006, 3553, 3984, 4298, 5080, 5828, 6700 },
                         { 1.57f, 0.7f } } },
}};

inline constexpr EnginePatch kNoEnginePatch {};

constexpr bool enginePatchTableValid() noexcept
{
    for (auto row = kEnginePatchByName.begin(); row != kEnginePatchByName.end(); ++row)
    {
        if (row->first.empty())
            return false;
        for (float w : row->second.pmbWidth)
            if (w > 2.0f)
                return false;
        for (float m : row->second.octaveDecay)
            if (! (m > 0.05f && m < 20.0f))
                return false;
        if (row->second.modeSmear[0] > 256.0f || (row->second.modeSmear[0] >= 0.0f
                                                  && ! (row->second.modeSmear[1] > 0.0f && row->second.modeSmear[1] <= 5.0f)))
            return false;
        for (float s : row->second.pmbSmearShare)
            if (! (s >= 0.0f && s <= 1.0f))
                return false;
        if (row->second.densityJitter > 0.02f || ! StereoBands::valid (row->second.stereoBands))
            return false;
        for (float k : row->second.pmbDensifier)
            if (! (k >= -1.0f && k <= 0.75f))
                return false;
        for (float m : row->second.pmbLevel)
            if (! (m >= 0.1f && m <= 4.0f))
                return false;
        if (! (row->second.sparseERLowpassHz == 0.0f
               || (row->second.sparseERLowpassHz >= 1000.0f && row->second.sparseERLowpassHz <= 20000.0f)))
            return false;
        if (! ((row->second.dualSlope[0] == 0.0f || (row->second.dualSlope[0] >= 0.05f && row->second.dualSlope[0] <= 1.0f))
               && row->second.dualSlope[1] >= 0.0f && row->second.dualSlope[1] <= 4.0f))
            return false;
        if (! fdnDelaysValid (row->second.fdnDelays) || ! fbRotationValid (row->second.fbRotation)
            || ! sparseERShapeValid (row->second.sparseERShape) || ! tankOnsetValid (row->second.tankOnset)
            || ! sustainLimitValid (row->second.sustainLimitMid) || ! sustainLimitValid (row->second.sustainLimitLow)
            || row->second.denseHallStaticAllpasses < 0 || row->second.denseHallStaticAllpasses > 2)
            return false;
        for (auto earlier = kEnginePatchByName.begin(); earlier != row; ++earlier)
            if (earlier->first == row->first)
                return false;
    }
    return true;
}
static_assert (enginePatchTableValid(), "every engine-patch row names its preset once and keeps its values in range");

// The engine patch preset `name` plays: its row, or none.
inline const EnginePatch& enginePatchFor (std::string_view name) noexcept
{
    for (const auto& row : kEnginePatchByName)
        if (row.first == name)
            return row.second;
    return kNoEnginePatch;
}

} // namespace duskverb
