#pragma once

#include "DuskVerbParams.hpp"
#include "../src/dsp/AlgorithmConfig.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <locale>
#include <sstream>
#include <array>

namespace duskverb
{

inline const FactoryPreset* factoryPresetByName(std::string_view name);

inline bool parseClassicFloat(std::string_view text, float& out)
{
    std::istringstream in{std::string(text)};
    in.imbue(std::locale::classic());
    double value = 0.0;
    char extra = 0;
    in >> value;
    if (in.fail() || (in >> extra)) return false;
    out = static_cast<float>(value);
    std::uint32_t bits = 0;
    std::memcpy(&bits, &out, sizeof(bits));
    return finiteBits(bits);
}

struct XmlAttr { std::string_view name, value; };

inline bool parseXmlAttrs(std::string_view text, std::array<XmlAttr, 32>& attrs, size_t& count)
{
    count = 0; size_t p = 0;
    while (p < text.size())
    {
        while (p < text.size() && std::isspace(static_cast<unsigned char>(text[p]))) ++p;
        if (p == text.size()) return true;
        const size_t start = p;
        while (p < text.size() && (std::isalnum(static_cast<unsigned char>(text[p])) || text[p] == '_' || text[p] == '-')) ++p;
        const size_t nameEnd = p;
        if (p == start || p >= text.size() || text[p++] != '=') return false;
        if (p >= text.size() || (text[p] != '"' && text[p] != '\'')) return false;
        const char quote = text[p++]; const size_t valueStart = p;
        while (p < text.size() && text[p] != quote)
        {
            if (text[p] == '<') return false;
            ++p;
        }
        if (p == text.size() || count == attrs.size()) return false;
        for (size_t i = 0; i < count; ++i) if (attrs[i].name == text.substr(start, nameEnd - start)) return false;
        attrs[count++] = { text.substr(start, nameEnd - start), text.substr(valueStart, p - valueStart) };
        ++p;
    }
    return true;
}

inline bool decodeUserPreset(std::string_view text, StateValues& out)
{
    StateValues decoded = makeDefaultState();
    std::string headerName;
    std::string v2;
    bool headerSeen = false;
    bool legacySeen = false;
    std::array<bool, static_cast<size_t>(kNumParams)> seen{};
    bool hasConfig = false;
    std::string config;
    size_t pos = 0;
    while (pos <= text.size())
    {
        const size_t nl = text.find('\n', pos);
        std::string_view line = text.substr(pos, nl == std::string_view::npos ? text.size() - pos : nl - pos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const size_t first = line.find_first_not_of(" \t");
        if (first != std::string_view::npos)
        {
            line.remove_prefix(first);
            if (line.substr(0, 4) == "v=2;")
            {
                if (!v2.empty() || legacySeen) return false;
                v2.assign(line);
            }
            else
            {
                const size_t eq = line.find('=');
                if (eq == std::string_view::npos || eq == 0) return false;
                const auto key = line.substr(0, eq), value = line.substr(eq + 1);
                if (!v2.empty()) return false;
                if (key == "name")
                {
                    if (headerSeen) return false;
                    headerSeen = true;
                    headerName.assign(value);
                }
                else if (key == "@config")
                {
                    legacySeen = true;
                    if (hasConfig) return false;
                    hasConfig = true; config.assign(value);
                }
                else
                {
                    legacySeen = true;
                    const int index = paramIndexForId(key);
                    if (index < 0 || index == Bypass || seen[static_cast<size_t>(index)]) return false;
                    float valueFloat = 0.0f;
                    if (!parseClassicFloat(value, valueFloat)
                        || !plainValueInRange(paramDesc(index), valueFloat)) return false;
                    decoded.params[static_cast<size_t>(index)] = plainToHost(paramDesc(index), valueFloat);
                    seen[static_cast<size_t>(index)] = true;
                }
            }
        }
        if (nl == std::string_view::npos) break;
        pos = nl + 1;
    }

    if (!v2.empty())
    {
        if (!decodeState(v2, decoded)) return false;
        if (headerSeen) decoded.userName = headerName;
        out = std::move(decoded);
        return true;
    }
    for (int i = 0; i < kNumParams; ++i)
        if (i != Bypass && !seen[static_cast<size_t>(i)]) return false;
    if (headerSeen) decoded.userName = headerName;

    if (hasConfig)
    {
        if (const FactoryPreset* preset = factoryPresetByName(config))
        {
            decoded.presetName = preset->name;
            decoded.sixAP.densityBaseline = preset->sixAPDensityBaseline;
            decoded.sixAP.bloomCeiling = preset->sixAPBloomCeiling;
            decoded.sixAP.earlyMix = preset->sixAPEarlyMix;
            decoded.sixAP.outputTrim = preset->sixAPOutputTrim;
            for (int i = 0; i < 6; ++i) decoded.sixAP.bloomStagger[i] = preset->sixAPBloomStagger[i];
        }
    }
    out = std::move(decoded);
    return true;
}

// Restricted JUCE ValueTree XML import for the DuskVerb root and versions 1..4.
// Version <4 may omit tonal_correction (its safe default is false). Predefined
// XML entities and comments are supported; DTDs and custom entities are rejected.
inline bool decodeJucePreset(std::string_view xml, StateValues& out)
{
    if (xml.find("<!DOCTYPE") != std::string_view::npos || xml.find("<!ENTITY") != std::string_view::npos) return false;
    const size_t root = xml.find("<DuskVerb");
    if (root == std::string_view::npos) return false;
    for (size_t q = 0; q < root; )
    {
        if (std::isspace(static_cast<unsigned char>(xml[q]))) { ++q; continue; }
        if (xml.substr(q, 5) == "<?xml") { const size_t z = xml.find("?>", q + 5); if (z == std::string_view::npos || z >= root) return false; q = z + 2; continue; }
        if (xml.substr(q, 4) == "<!--") { const size_t z = xml.find("-->", q + 4); if (z == std::string_view::npos || z >= root) return false; q = z + 3; continue; }
        return false;
    }
    const size_t rootEnd = xml.find('>', root);
    if (rootEnd == std::string_view::npos) return false;
    const auto tag = xml.substr(root + 1, xml.find_first_of(" \t\r\n/>", root + 1) - root - 1);
    if (tag != "DuskVerb") return false;
    StateValues decoded = makeDefaultState();
    std::array<bool, static_cast<size_t>(kNumParams)> seen{};
    int version = 1;
    const auto attrs = xml.substr(root + tag.size() + 1, rootEnd - (root + tag.size() + 1));
    std::array<XmlAttr, 32> rootAttrs{}; size_t rootAttrCount = 0;
    if (!parseXmlAttrs(attrs, rootAttrs, rootAttrCount)) return false;
    auto unescape = [](std::string_view s, std::string& dst) {
        dst.clear();
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] != '&') { dst.push_back(s[i]); continue; }
            const size_t e = s.find(';', i); if (e == std::string_view::npos) return false;
            const auto ent = s.substr(i, e - i + 1);
            if (ent == "&amp;") dst.push_back('&'); else if (ent == "&quot;") dst.push_back('"');
            else if (ent == "&apos;") dst.push_back('\''); else if (ent == "&lt;") dst.push_back('<');
            else if (ent == "&gt;") dst.push_back('>'); else return false; i = e;
        } return true;
    };
    auto rootAttr = [&](std::string_view name, std::string& value) {
        for (size_t i = 0; i < rootAttrCount; ++i)
            if (rootAttrs[i].name == name) return unescape(rootAttrs[i].value, value);
        return false;
    };
    std::string textValue;
    // An invalid attribute is not the same as an absent optional property.
    for (size_t i = 0; i < rootAttrCount; ++i)
        if (!unescape(rootAttrs[i].value, textValue)) return false;
    if (rootAttr("stateVersion", textValue))
    {
        const auto parsed = std::from_chars(textValue.data(), textValue.data() + textValue.size(), version);
        if (parsed.ec != std::errc() || parsed.ptr != textValue.data() + textValue.size()
            || version < 1 || version > 4) return false;
    }
    if (rootAttr("presetName", textValue)) decoded.userName = textValue;
    if (rootAttr("lastPresetName", textValue)) decoded.presetName = textValue;
    static constexpr const char* const sixNames[10] = {
        "sixAPDensityBaseline", "sixAPBloomCeiling", "sixAPEarlyMix", "sixAPOutputTrim",
        "sixAPBloomStagger0", "sixAPBloomStagger1", "sixAPBloomStagger2", "sixAPBloomStagger3",
        "sixAPBloomStagger4", "sixAPBloomStagger5" };
    for (int i = 0; i < 10; ++i)
        if (rootAttr(sixNames[i], textValue))
        {
            float f = 0.0f;
            if (!parseClassicFloat(textValue, f) || !sixApValueInRange(i, f)) return false;
            *sixApSlot(decoded.sixAP, i) = f;
        }
    size_t p = rootEnd + 1;
    while (p < xml.size())
    {
        while (p < xml.size() && std::isspace(static_cast<unsigned char>(xml[p]))) ++p;
        if (p >= xml.size() || xml.substr(p, 2) == "</") break;
        if (xml.substr(p, 4) == "<!--") { const size_t z = xml.find("-->", p + 4); if (z == std::string_view::npos) return false; p = z + 3; continue; }
        if (xml[p] != '<') return false;
        const size_t e = xml.find('>', p); if (e == std::string_view::npos) return false;
        const auto child = xml.substr(p + 1, e - p - 1);
        const size_t childNameEnd = child.find_first_of(" \t\r\n/>");
        if (childNameEnd == std::string_view::npos || child.substr(0, childNameEnd) != "PARAM") return false;
        if (childNameEnd != 5 || child.back() != '/') return false;
        std::array<XmlAttr, 32> childAttrs{}; size_t childAttrCount = 0;
        if (!parseXmlAttrs(child.substr(childNameEnd, child.size() - childNameEnd - 1), childAttrs, childAttrCount)) return false;
        auto get = [&](std::string_view name, std::string& value) {
            for (size_t i = 0; i < childAttrCount; ++i)
                if (childAttrs[i].name == name) return unescape(childAttrs[i].value, value);
            return false;
        };
        std::string id, value; if (!get("id", id) || !get("value", value)) return false;
        const int index = paramIndexForId(id); if (index < 0 || seen[static_cast<size_t>(index)]) return false;
        float f = 0.0f; if (!parseClassicFloat(value, f)) return false;
        if (!plainValueInRange(paramDesc(index), f)) return false;
        f = plainToHost(paramDesc(index), f);
        if (version < 3 && index == Algorithm)
            f = static_cast<float>(migrateLegacyAlgorithmIndex(static_cast<int>(f)));
        if (!hostValueInRange(paramDesc(index), f)) return false;
        decoded.params[static_cast<size_t>(index)] = f;
        seen[static_cast<size_t>(index)] = true; p = e + 1;
    }
    while (p < xml.size() && std::isspace(static_cast<unsigned char>(xml[p]))) ++p;
    if (xml.substr(p, 11) != "</DuskVerb>") return false;
    p += 11;
    while (p < xml.size())
    {
        while (p < xml.size() && std::isspace(static_cast<unsigned char>(xml[p]))) ++p;
        if (xml.substr(p, 4) != "<!--") break;
        const size_t z = xml.find("-->", p + 4);
        if (z == std::string_view::npos) return false;
        p = z + 3;
    }
    if (p != xml.size()) return false;
    // Pre-APVTS DPV sessions stored these seven values as root properties.
    // JUCE gives an explicit legacy property precedence over its PARAM child.
    static constexpr const char* legacyNames[] = {
        "dpvHfShelfGainDb", "dpvHfShelfFreqHz", "dpvStructHfDampHz",
        "dpvBoxCutGainDb", "dpvBoxCutFreqHz", "dpvBassShelfGainDb", "dpvBassShelfFreqHz" };
    static constexpr int legacyParams[] = {
        DpvHfShelfDb, DpvHfShelfHz, DpvStructHfDampHz,
        DpvBoxCutDb, DpvBoxCutHz, DpvBassShelfDb, DpvBassShelfHz };
    for (size_t i = 0; i < std::size(legacyParams); ++i)
        if (rootAttr(legacyNames[i], textValue))
        {
            const int index = legacyParams[i];
            float plain;
            if (!parseClassicFloat(textValue, plain) || !plainValueInRange(paramDesc(index), plain)) return false;
            decoded.params[index] = plainToHost(paramDesc(index), plain);
            seen[index] = true;
        }
    for (int i = 0; i < kNumParams; ++i)
        if (!seen[static_cast<size_t>(i)] && !(version < 4 && i == TonalCorrection)) return false;
    out = std::move(decoded); return true;
}

} // namespace duskverb
