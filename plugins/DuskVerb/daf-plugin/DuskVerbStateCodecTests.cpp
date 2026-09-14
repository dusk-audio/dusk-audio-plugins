#include "DuskVerbPresetImport.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
using namespace duskverb;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %s:%d\n", #x, __LINE__); ++failures; } } while (false)

static StateValues sample()
{
    StateValues s = makeDefaultState();
    for (int i = 0; i < kNumParams; ++i)
        s.params[static_cast<size_t>(i)] = paramDesc(i).integer
            ? std::round(hostMin(paramDesc(i)) + (hostMax(paramDesc(i)) - hostMin(paramDesc(i))) * 0.37f)
            : hostMin(paramDesc(i)) + (hostMax(paramDesc(i)) - hostMin(paramDesc(i))) * 0.37f;
    s.params[Algorithm] = 3.0f; s.params[BusMode] = 1.0f; s.params[Bypass] = 1.0f;
    s.params[PredelaySync] = 4.0f; s.params[Freeze] = 1.0f;
    s.sixAP.densityBaseline = 0.71f; s.sixAP.bloomCeiling = 0.93f;
    s.sixAP.earlyMix = 0.66f; s.sixAP.outputTrim = 1.42f;
    for (int i = 0; i < 6; ++i) s.sixAP.bloomStagger[i] = 0.5f + i * 0.13f;
    s.presetName = "Factory;=λ"; s.userName = "A;=λ"; s.edited = true;
    return s;
}

static bool same(const StateValues& a, const StateValues& b)
{
    if (a.presetName != b.presetName || a.userName != b.userName || a.edited != b.edited) return false;
    for (int i = 0; i < kNumParams; ++i)
        if (std::memcmp(&a.params[static_cast<size_t>(i)], &b.params[static_cast<size_t>(i)], sizeof(float)) != 0) return false;
    return std::memcmp(&a.sixAP, &b.sixAP, sizeof(a.sixAP)) == 0;
}

int main()
{
    const StateValues original = sample(); StateValues out{};
    CHECK(decodeState(encodeState(original), out) && same(original, out));
    CHECK(makeDefaultState().params[Bypass] == hostDefault(paramDesc(Bypass)));
    StateValues sentinel = sample(); sentinel.userName = "sentinel";
    const std::string good = encodeState(original);
    CHECK(decodeUserPreset(good, out) && same(original, out));
    CHECK(decodeUserPreset("name=Header Name\n" + good, out) && out.userName == "Header Name");
    const std::string legacyLine = std::string(paramDesc(Mix).id) + "=" + std::to_string(paramDesc(Mix).def) + "\n";
    for (const auto& mixed : {legacyLine + good, std::string("@config=Black Hole\n") + good,
                              std::string("name=Header Name\n") + legacyLine + good,
                              good + "\n" + legacyLine, good + "\nname=Late Name"})
    {
        const auto beforeImport = sentinel;
        CHECK(!decodeUserPreset(mixed, sentinel) && same(sentinel, beforeImport));
    }
    std::string bad = good;
    const std::string densityKey = "@sixap_density_baseline=";
    bad.replace(bad.find(densityKey) + densityKey.size(), 8, "7fc00000");
    const auto untouched = sentinel;
    CHECK(!decodeState(bad, sentinel) && same(sentinel, untouched));
    float nonfinite = 0.0f;
    CHECK(!decodeStateFloat("7fc00000", nonfinite));
    std::string v1 = "v=1";
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& d = paramDesc(i);
        float plain = d.min + (d.max - d.min) * 0.37f;
        if (d.integer) plain = std::round(plain);
        v1 += ";"; v1 += d.id; v1 += "=";
        appendStateFloat(v1, plain);
    }
    for (int i = 0; i < 10; ++i) { v1 += ";"; v1 += kSixApKeys[i]; v1 += "="; appendStateFloat(v1, *sixApSlot(original.sixAP, i)); }
    CHECK(decodeState(v1, out));
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& d = paramDesc(i);
        float plain = d.min + (d.max - d.min) * 0.37f;
        if (d.integer) plain = std::round(plain);
        CHECK(out.params[static_cast<size_t>(i)] == plainToHost(d, plain));
    }
    const size_t six = good.find(";@sixap_density_baseline=");
    CHECK(six != std::string::npos && !decodeState(good.substr(0, six) + good.substr(good.find(';', six + 1)), sentinel));
    std::string old = "name=Old Name\n";
    for (int i = 0; i < kNumParams; ++i) if (i != Bypass) old += std::string(paramDesc(i).id) + "=" + std::to_string(paramDesc(i).def) + "\n";
    old += "@config=Black Hole\n";
    CHECK(decodeUserPreset(old, out) && out.userName == "Old Name" && out.presetName == "Black Hole");
    std::string truncated = old; truncated.erase(truncated.find("\n" + std::string(paramDesc(kNumParams - 1).id)));
    CHECK(!decodeUserPreset(truncated, sentinel));
    std::string xml = "<?xml version=\"1.0\"?><DuskVerb stateVersion=\"4\" lastPresetName=\"Black Hole\" presetName=\"A &amp; B\" sixAPDensityBaseline=\"0.71\">";
    for (int i = 0; i < kNumParams; ++i) xml += "<PARAM id=\"" + std::string(paramDesc(i).id) + "\" value=\"" + std::to_string(paramDesc(i).def) + "\"/>";
    xml += "</DuskVerb>";
    CHECK(decodeJucePreset(xml, out) && out.presetName == "Black Hole" && out.userName == "A & B"
          && out.sixAP.densityBaseline == 0.71f);
    // Parameter IDs independently transcribed from the first versioned JUCE
    // layouts (99eba57, 628e356, f95ecfc), not from the importer's omission mask.
    const char* v1Ids[] = {"algorithm", "mix", "bus_mode", "bypass", "predelay",
        "predelay_sync", "decay", "size", "mod_depth", "mod_rate", "bass_mult",
        "mid_mult", "damping", "crossover", "high_crossover", "saturation",
        "diffusion", "er_level", "er_size", "lo_cut", "hi_cut", "width", "freeze",
        "gain_trim", "mono_below"};
    for (int version = 1; version <= 3; ++version)
    {
        std::string historical = "<DuskVerb stateVersion=\"" + std::to_string(version) + "\">";
        std::array<bool, kNumParams> present{};
        const auto add = [&](const char* id) {
            const int index = paramIndexForId(id);
            present[index] = true;
            historical += "<PARAM id=\"" + std::string(id) + "\" value=\""
                + std::to_string(index == Algorithm ? 7.0f : paramDesc(index).def) + "\"/>";
        };
        for (const auto* id : v1Ids) add(id);
        if (version >= 2) add("gate_enabled");
        if (version >= 3) add("bass_choke");
        historical += "</DuskVerb>";
        CHECK(decodeJucePreset(historical, out));
        CHECK(out.params[Algorithm] == (version < 3 ? 1.0f : 7.0f));
        const auto defaults = makeDefaultState();
        for (int i = 0; i < kNumParams; ++i)
            if (!present[i]) CHECK(out.params[i] == defaults.params[i]);
        auto incomplete = historical;
        const auto start = incomplete.find("<PARAM id=\"mix\"");
        incomplete.erase(start, incomplete.find("/>", start) + 2 - start);
        CHECK(!decodeJucePreset(incomplete, sentinel) && same(sentinel, untouched));
        auto modernIncomplete = historical;
        modernIncomplete[modernIncomplete.find("stateVersion=\"") + 14] = '4';
        CHECK(!decodeJucePreset(modernIncomplete, sentinel) && same(sentinel, untouched));
    }
    const char* legacyNames[] = {"dpvHfShelfGainDb", "dpvHfShelfFreqHz", "dpvStructHfDampHz",
        "dpvBoxCutGainDb", "dpvBoxCutFreqHz", "dpvBassShelfGainDb", "dpvBassShelfFreqHz"};
    const int legacyParams[] = {DpvHfShelfDb, DpvHfShelfHz, DpvStructHfDampHz,
        DpvBoxCutDb, DpvBoxCutHz, DpvBassShelfDb, DpvBassShelfHz};
    std::string legacyXml = "<DuskVerb stateVersion=\"2\"";
    for (int i = 0; i < 7; ++i)
        legacyXml += " " + std::string(legacyNames[i]) + "=\"" + std::to_string(paramDesc(legacyParams[i]).min) + "\"";
    legacyXml += ">";
    for (int i = 0; i < kNumParams; ++i)
        if (i != TonalCorrection && (i < DpvHfShelfDb || i > DpvBassShelfHz))
            legacyXml += "<PARAM id=\"" + std::string(paramDesc(i).id) + "\" value=\"" + std::to_string(paramDesc(i).def) + "\"/>";
    legacyXml += "</DuskVerb>";
    CHECK(decodeJucePreset(legacyXml, out));
    for (int i = 0; i < 7; ++i)
        CHECK(out.params[legacyParams[i]] == plainToHost(paramDesc(legacyParams[i]), paramDesc(legacyParams[i]).min));
    std::string invalidLegacy = legacyXml;
    const auto legacyValue = invalidLegacy.find("dpvHfShelfGainDb=\"") + std::strlen("dpvHfShelfGainDb=\"");
    invalidLegacy.replace(legacyValue, invalidLegacy.find('"', legacyValue) - legacyValue, "oops");
    CHECK(!decodeJucePreset(invalidLegacy, sentinel) && same(sentinel, untouched));
    std::string malformedName = xml;
    malformedName.replace(malformedName.find("A &amp; B"), 9, "A &broken; B");
    CHECK(!decodeJucePreset(malformedName, sentinel) && same(sentinel, untouched));
    std::string malformedConfig = xml;
    malformedConfig.replace(malformedConfig.find("sixAPDensityBaseline=\"0.71\""),
                            std::strlen("sixAPDensityBaseline=\"0.71\""),
                            "sixAPDensityBaseline=\"&broken;\"");
    CHECK(!decodeJucePreset(malformedConfig, sentinel) && same(sentinel, untouched));
    CHECK(!decodeJucePreset("<!DOCTYPE x><DuskVerb/>", sentinel));
    std::printf("codec tests: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
