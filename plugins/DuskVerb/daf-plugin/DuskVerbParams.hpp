// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// DuskVerbParams.hpp — the DAF-facing view of the parameter set, plus the full
// state codec and the factory-program apply path.
//
// The parameter TABLE itself (ids, names, ranges, defaults, tapers) lives in
// ../core/DuskVerbParamTable.hpp so the core tests can check it without pulling
// in DAF. This header adds the parts that only the plugin shell needs.

#pragma once

#include "../core/DuskVerbParamTable.hpp"
#include "../core/DuskVerbDSP.hpp"
#include "../src/FactoryPresets.h"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace duskverb
{

// DuskVerb 2 exposes NO output-only meter parameters. The four level meters are
// read through the same-process bridge in DuskVerbAccess.hpp instead.
//
// Why, since the other Dusk DAF plugins do declare them: clap-validator's
// param-set-wrong-namespace test diffs EVERY parameter the plugin reports,
// readonly meters included, after processing one buffer of non-silent audio. A
// peak meter that honestly follows the input therefore always "changes", and the
// test reports it as the plugin ignoring an event namespace. Measured here:
// pinning all four meters to a constant makes the test pass, pinning only the
// input pair or only the output pair still fails, so it is the meters and not
// the event handling (the DAF fork does check space_id on both the process and
// flush paths -- project memory clap-validator-versions-and-meter-false-fails).
// CI runs that suite with SUITE_STRICT=1 and its script says, in as many words,
// never to add a per-test allowlist and to fix the plugin instead.
//
// Nothing is lost in the builds we ship: the plugin is MONOLITHIC, so the UI
// always links with the DSP and always has direct access. The one thing the
// parameters would still buy is meters in a host's own generic UI, which is not
// worth failing a release gate for.
inline constexpr int kTotalParamCount = kNumParams;

// ── State ───────────────────────────────────────────────────────────────────
//
// Format: "v=2" followed by ';'-separated "<key>=<value>" tokens.
//   * parameter keys are the APVTS parameter ids; values are the IEEE-754 bit
//     pattern in hex. Hex bits rather than a decimal float because reading a
//     decimal back needs the floating-point std::from_chars, which libc++ marks
//     "introduced in macOS 13.3" — above our deployment target. Exact and
//     locale-independent either way. (Same reasoning as Multi-Comp 2.)
//   * "@sixap_*" keys carry the per-preset SixAPTank brightness/density state,
//     which the JUCE build stored as custom properties on the state ValueTree
//     for the same reason: it travels with the session but is not an automation
//     target.
//   * "@preset" carries the factory preset NAME. The name-keyed engine config
//     (PostTankEQ bands, modulation topology, FDN base delays, post-band trims)
//     lives outside the parameter set and is reconstructed by name, exactly as
//     the JUCE build's "lastPresetName" property did. Absent or unknown means
//     "no identity": the engine config is reset to defaults on load.
//
// NOT carried, deliberately: the JUCE build also wrote duplicate "dpv*" custom
// properties. Those exist purely so a pre-2026-05-25 JUCE binary could read a
// newer JUCE session; the 7 dpv parameters are in the parameter set and round
// trip through it. There is no older DuskVerb 2 binary to be compatible with.
inline constexpr int kStateVersion = 2;

inline bool finiteBits(std::uint32_t bits) noexcept
{
    return (bits & 0x7f800000u) != 0x7f800000u;
}

inline bool parseHexBits(std::string_view text, std::uint32_t& bits) noexcept
{
    if (text.empty() || text.size() > 8) return false;
    bits = 0;
    for (char c : text)
    {
        const unsigned digit = c >= '0' && c <= '9' ? unsigned(c - '0')
                            : c >= 'a' && c <= 'f' ? unsigned(c - 'a' + 10)
                            : c >= 'A' && c <= 'F' ? unsigned(c - 'A' + 10) : 16u;
        if (digit > 15u) return false;
        bits = (bits << 4) | digit;
    }
    return true;
}

inline bool decodeStateFloat(std::string_view text, float& out) noexcept
{
    std::uint32_t bits = 0;
    if (!parseHexBits(text, bits) || !finiteBits(bits)) return false;
    std::memcpy(&out, &bits, sizeof(out));
    return true;
}

inline void appendStateFloat(std::string& out, float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    char number[16];
    const auto result = std::to_chars(number, number + sizeof(number), bits, 16);
    if (result.ec == std::errc()) out.append(number, result.ptr);
}

struct StateValues
{
    std::array<float, static_cast<size_t>(kNumParams)> params{};
    DuskVerbDSP::SixAPBrightnessState sixAP{};
    std::string presetName;          // factory identity; empty = no identity
    std::string userName;            // optional arbitrary UTF-8 display name
    bool edited = false;
};

inline StateValues makeDefaultState();

inline bool sixApValueInRange(int i, float v) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    if (!finiteBits(bits)) return false;
    (void) i;
    return true;
}

inline bool plainValueInRange(const ParamDesc& d, float v) noexcept
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return finiteBits(bits) && v >= d.min && v <= d.max
        && (!d.integer || std::trunc(v) == v);
}

inline void appendHexText(std::string& out, std::string_view text)
{
    static constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : text) { out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); }
}

inline bool decodeHexText(std::string_view text, std::string& out)
{
    if (text.size() < 4 || text.substr(0, 4) != "hex:") return false;
    const auto hex = text.substr(4);
    if ((hex.size() & 1u) != 0) return false;
    std::string value;
    value.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2)
    {
        std::uint32_t b = 0;
        if (!parseHexBits(hex.substr(i, 2), b)) return false;
        value.push_back(static_cast<char>(b));
    }
    out = std::move(value);
    return true;
}

inline bool safePresetText(std::string_view text) noexcept
{
    return !text.empty() && text.find_first_of(";=\r\n") == std::string_view::npos;
}

// The 10 SixAP keys, in the order they are written.
inline constexpr const char* const kSixApKeys[10] = {
    "@sixap_density_baseline", "@sixap_bloom_ceiling", "@sixap_early_mix", "@sixap_output_trim",
    "@sixap_bloom_stagger0", "@sixap_bloom_stagger1", "@sixap_bloom_stagger2",
    "@sixap_bloom_stagger3", "@sixap_bloom_stagger4", "@sixap_bloom_stagger5"
};

inline float* sixApSlot(DuskVerbDSP::SixAPBrightnessState& s, int i) noexcept
{
    switch (i)
    {
        case 0: return &s.densityBaseline;
        case 1: return &s.bloomCeiling;
        case 2: return &s.earlyMix;
        case 3: return &s.outputTrim;
        default: return &s.bloomStagger[i - 4];
    }
}

inline const float* sixApSlot(const DuskVerbDSP::SixAPBrightnessState& s, int i) noexcept
{
    return sixApSlot(const_cast<DuskVerbDSP::SixAPBrightnessState&>(s), i);
}

inline std::string encodeState(const StateValues& v)
{
    std::string state = "v=" + std::to_string(kStateVersion);
    state.reserve(48 + static_cast<size_t>(kNumParams) * 28);
    for (int i = 0; i < kNumParams; ++i)
    {
        state.push_back(';');
        state += paramDesc(i).id;
        state.push_back('=');
        appendStateFloat(state, v.params[static_cast<size_t>(i)]);
    }
    DuskVerbDSP::SixAPBrightnessState sixAP = v.sixAP;
    for (int i = 0; i < 10; ++i)
    {
        state.push_back(';');
        state += kSixApKeys[i];
        state.push_back('=');
        appendStateFloat(state, *sixApSlot(sixAP, i));
    }
    if (!v.presetName.empty())
    {
        state.push_back(';');
        state += "@preset=";
        if (safePresetText(v.presetName)) state += v.presetName;
        else { state += "hex:"; appendHexText(state, v.presetName); }
    }
    if (!v.userName.empty()) { state += ";@user=hex:"; appendHexText(state, v.userName); }
    if (v.edited) state += ";@edited=1";
    return state;
}

// Complete-set transactional decoder: `out` is untouched unless every
// parameter key appears exactly once with a finite, in-range value. A
// half-applied state is worse than a rejected one.
inline bool decodeState(std::string_view state, StateValues& out) noexcept
{
    if (state.empty() || state.back() == ';') return false;
    const size_t versionEnd = state.find(';');
    if (versionEnd == std::string_view::npos) return false;
    int version = 0;
    {
        const std::string_view token = state.substr(0, versionEnd);
        if (token.size() < 3 || token.substr(0, 2) != "v=") return false;
        const std::string_view digits = token.substr(2);
        const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), version);
        if (parsed.ec != std::errc() || parsed.ptr != digits.data() + digits.size()) return false;
        if (version != 1 && version != kStateVersion) return false;
    }

    StateValues decoded = makeDefaultState();
    bool presetSeen = false, userSeen = false, editedSeen = false;
    std::array<bool, static_cast<size_t>(kNumParams)> seen{};
    std::array<bool, 10> sixApSeen{};
    size_t begin = versionEnd + 1;
    while (begin < state.size())
    {
        const size_t end = state.find(';', begin);
        const std::string_view token = state.substr(
            begin, end == std::string_view::npos ? state.size() - begin : end - begin);
        const size_t equal = token.find('=');
        if (equal == std::string_view::npos || equal == 0 || equal + 1 == token.size())
            return false;
        const std::string_view key   = token.substr(0, equal);
        const std::string_view value = token.substr(equal + 1);

        if (key == "@preset")
        {
            if (presetSeen) return false;
            presetSeen = true;
            if (value.substr(0, 4) == "hex:") { if (!decodeHexText(value, decoded.presetName)) return false; }
            else if (safePresetText(value)) decoded.presetName.assign(value);
            else return false;
        }
        else if (key == "@user")
        {
            if (userSeen || !decodeHexText(value, decoded.userName)) return false;
            userSeen = true;
            for (unsigned char c : decoded.userName)
                if (c < 0x20 || c == 0x7f) return false;
        }
        else if (key == "@edited")
        {
            if (editedSeen || value != "1") return false;
            editedSeen = true;
            decoded.edited = true;
        }
        else if (!key.empty() && key[0] == '@')
        {
            bool matched = false;
            for (int i = 0; i < 10; ++i)
            {
                if (key != kSixApKeys[i]) continue;
                if (sixApSeen[static_cast<size_t>(i)]) return false;
                float f = 0.0f;
                if (!decodeStateFloat(value, f) || !sixApValueInRange(i, f)) return false;
                *sixApSlot(decoded.sixAP, i) = f;
                sixApSeen[static_cast<size_t>(i)] = true;
                matched = true;
                break;
            }
            if (!matched) return false;
        }
        else
        {
            const int index = paramIndexForId(key);
            if (index < 0 || seen[static_cast<size_t>(index)]) return false;
            // Reject a second '=' only inside a parameter token; a preset name
            // never contains one (checked on encode by construction: the
            // factory names are plain words and digits).
            if (value.find('=') != std::string_view::npos) return false;
            float f = 0.0f;
            if (!decodeStateFloat(value, f)) return false;
            if (version == 1)
            {
                if (!plainValueInRange(paramDesc(index), f)) return false;
                f = plainToHost(paramDesc(index), f);
            }
            if (!hostValueInRange(paramDesc(index), f)) return false;
            decoded.params[static_cast<size_t>(index)] = f;
            seen[static_cast<size_t>(index)] = true;
        }

        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    for (bool present : seen)
        if (!present) return false;
    // The encoder always writes all 10 SixAP keys; a state missing any of them
    // is not one this build wrote, so it is rejected rather than half-applied.
    for (bool present : sixApSeen)
        if (!present) return false;
    out = std::move(decoded);
    return true;
}

inline StateValues makeDefaultState()
{
    StateValues state{};
    for (int i = 0; i < kNumParams; ++i)
        state.params[static_cast<size_t>(i)] = hostDefault(paramDesc(i));
    return state;
}

// ── Factory programs ────────────────────────────────────────────────────────
inline int factoryPresetCount() { return static_cast<int>(getFactoryPresets().size()); }

inline const FactoryPreset* factoryPresetByName(std::string_view name)
{
    for (const auto& preset : getFactoryPresets())
        if (name == preset.name) return &preset;
    return nullptr;
}

// Emits (parameter index, HOST value) for every parameter the preset sets, in
// the JUCE build's order. `apply` is called as apply(int index, float hostValue).
template <typename ApplyFn>
inline void applyFactoryPresetToHostParameters(const FactoryPreset& preset, ApplyFn&& apply)
{
    preset.collectParameters([&apply](const char* id, float plainValue)
    {
        const int index = paramIndexForId(id);
        if (index < 0) return;
        apply(index, plainToHost(paramDesc(index), plainValue));
    });
}

} // namespace duskverb
