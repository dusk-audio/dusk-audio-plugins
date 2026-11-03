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
    // A JUCE user preset file, as UserPresetManager::saveUserPreset writes it:
    // parameters.copyState() plus its own root attributes. A fresh JUCE instance
    // never stamps stateVersion on parameters.state, and JUCE's loadUserPreset
    // replaces the state without migrating, so every index here is already in
    // the current (post-f95ecfc) engine order and must import unchanged.
    const auto userPresetFile = [](const char* rootAttrs, int algorithm) {
        std::string file = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\n<DuskVerb";
        file += rootAttrs;
        file += " presetName=\"My Hall\" savedAt=\"2026-09-01T10:00:00.000+02:00\" pluginName=\"DuskVerb\">\n";
        for (int i = 0; i < kNumParams; ++i)
            file += "  <PARAM id=\"" + std::string(paramDesc(i).id) + "\" value=\""
                + (i == Algorithm ? std::to_string(algorithm) + ".0" : std::to_string(paramDesc(i).def)) + "\"/>\n";
        return file + "</DuskVerb>\n";
    };
    for (int algorithm = 0; algorithm < paramDesc(Algorithm).enumCount; ++algorithm)
    {
        CHECK(decodeJucePreset(userPresetFile("", algorithm), out)
              && out.params[Algorithm] == static_cast<float>(algorithm) && out.userName == "My Hall");
        // Saved after JUCE restored an old v2 session: setStateInformation had
        // already migrated the index and left stateVersion=2 in parameters.state.
        // This fixture writes bass_choke with a value; JUCE writes a parameter
        // the restored tree did not contain as a bare PARAM, which the
        // restoredV2File cases below cover.
        CHECK(decodeJucePreset(userPresetFile(" stateVersion=\"2\"", algorithm), out)
              && out.params[Algorithm] == static_cast<float>(algorithm));
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
    // Anything an importer accepts must survive the state codec unchanged: the
    // editor mirrors what it recalls, so a state the codec refuses would show a
    // sound and a name the plugin never took.
    const auto codecKeeps = [](const StateValues& s) {
        StateValues back = makeDefaultState();
        return decodeState(encodeState(s), back) && same(s, back);
    };
    CHECK(decodeJucePreset(userPresetFile("", 3), out) && codecKeeps(out));
    CHECK(decodeUserPreset("name=Header Name\n" + good, out) && codecKeeps(out));

    // The file JUCE really writes after restoring an old v2 session: a parameter
    // the restored tree did not contain is a bare <PARAM id=.../> (JUCE leaves it
    // at its default and never flushes a value), and JUCE loads it as that
    // default. setStateInformation already migrated the engine index, so it
    // imports unchanged.
    const auto inV2Tree = [](int i) {
        switch (i)
        {
        case Algorithm: case Decay: case Predelay: case Size: case Damping: case BassMult:
        case Crossover: case Diffusion: case ModDepth: case ModRate: case ErLevel: case ErSize:
        case Mix: case LoCut: case HiCut: case Width: case Freeze: case PredelaySync: case BusMode:
        case Bypass: case MidMult: case HighCrossover: case Saturation: case GainTrim: case MonoBelow:
        case GateEnabled:
            return true;
        default:
            return false;
        }
    };
    const auto restoredV2File = [&](int algorithm) {
        std::string file = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n\n<DuskVerb stateVersion=\"2\""
                           " presetName=\"Old Session\" pluginName=\"DuskVerb\">\n";
        for (int i = 0; i < kNumParams; ++i)
        {
            file += "  <PARAM id=\"" + std::string(paramDesc(i).id) + "\"";
            if (inV2Tree(i))
                file += " value=\"" + (i == Algorithm ? std::to_string(algorithm) + ".0"
                                                      : std::to_string(paramDesc(i).def)) + "\"";
            file += "/>\n";
        }
        return file + "</DuskVerb>\n";
    };
    const StateValues defaults = makeDefaultState();
    for (int algorithm = 0; algorithm < paramDesc(Algorithm).enumCount; ++algorithm)
    {
        CHECK(decodeJucePreset(restoredV2File(algorithm), out)
              && out.params[Algorithm] == static_cast<float>(algorithm)
              && out.params[BassChoke] == defaults.params[BassChoke]
              && out.params[TonalCorrection] == defaults.params[TonalCorrection]
              && out.userName == "Old Session" && codecKeeps(out));
        // bass_choke present only as a bare element still marks the new engine
        // order: reading presence from the value would migrate an index JUCE has
        // already migrated.
        std::string bareChoke = userPresetFile("", algorithm);
        const size_t at = bareChoke.find("<PARAM id=\"bass_choke\" value=\"");
        bareChoke.replace(at, bareChoke.find("/>", at) - at, "<PARAM id=\"bass_choke\"");
        CHECK(decodeJucePreset(bareChoke, out) && out.params[Algorithm] == static_cast<float>(algorithm));
    }
    // JUCE stamps the current stateVersion on save but never flushes a value
    // into a bare PARAM, so reopening a restored v2 session and saving it gives
    // stateVersion 4 with bare PARAMs. Version 4 requires all 92 parameters, so
    // a bare one must count as present.
    for (int algorithm : {0, 3, 10})
    {
        std::string v4 = restoredV2File(algorithm);
        v4.replace(v4.find("stateVersion=\"2\""), std::strlen("stateVersion=\"2\""), "stateVersion=\"4\"");
        CHECK(decodeJucePreset(v4, out) && out.params[Algorithm] == static_cast<float>(algorithm)
              && out.params[TonalCorrection] == defaults.params[TonalCorrection] && codecKeeps(out));
    }
    {
        // A bare algorithm is its default, which is already in the new engine
        // order: only a valued index is migrated, even without bass_choke.
        std::string bareAlgorithm = userPresetFile("", 3);
        size_t at = bareAlgorithm.find("<PARAM id=\"bass_choke\"");
        bareAlgorithm.erase(at, bareAlgorithm.find('\n', at) + 1 - at);
        at = bareAlgorithm.find("<PARAM id=\"algorithm\" value=\"");
        bareAlgorithm.replace(at, bareAlgorithm.find("/>", at) - at, "<PARAM id=\"algorithm\"");
        CHECK(decodeJucePreset(bareAlgorithm, out) && out.params[Algorithm] == defaults.params[Algorithm]);
    }
    {
        // Still refused: a parameter twice (bare and valued), an unknown bare id,
        // an empty value (not the same as no value), and a PARAM whose value
        // attribute is misspelled: bare means exactly <PARAM id="..."/>.
        std::string twice = userPresetFile("", 3);
        twice.insert(twice.find("</DuskVerb>"), "  <PARAM id=\"mix\"/>\n");
        CHECK(!decodeJucePreset(twice, sentinel) && same(sentinel, untouched));
        std::string unknown = userPresetFile("", 3);
        unknown.insert(unknown.find("</DuskVerb>"), "  <PARAM id=\"no_such_parameter\"/>\n");
        CHECK(!decodeJucePreset(unknown, sentinel) && same(sentinel, untouched));
        std::string empty = userPresetFile("", 3);
        const size_t at = empty.find("<PARAM id=\"mix\" value=\"");
        empty.replace(at, empty.find("/>", at) - at, "<PARAM id=\"mix\" value=\"\"");
        CHECK(!decodeJucePreset(empty, sentinel) && same(sentinel, untouched));
        for (const char* misspelled : {"valeu", "Value"})
        {
            std::string typo = userPresetFile("", 3);
            const size_t m = typo.find("<PARAM id=\"mix\" value=\"");
            typo.replace(m + std::strlen("<PARAM id=\"mix\" "), 5, misspelled);
            CHECK(!decodeJucePreset(typo, sentinel) && same(sentinel, untouched));
        }
    }

    // A user name is printable text. The state codec refuses a control byte or
    // DEL, so every importer refuses one too.
    for (const char* bad : {"My\x01Hall", "My\tHall", "My\x7fHall"})
    {
        std::string juce = userPresetFile("", 3);
        juce.replace(juce.find("presetName=\"My Hall\""), std::strlen("presetName=\"My Hall\""),
                     std::string("presetName=\"") + bad + "\"");
        CHECK(!decodeJucePreset(juce, sentinel) && same(sentinel, untouched));
        CHECK(!decodeUserPreset(std::string("name=") + bad + "\n" + good, sentinel) && same(sentinel, untouched));
    }
    {
        // The older key=value .dvpreset layout carries its name the same way.
        std::string legacy = "name=Bad\x02Name\n";
        for (int i = 0; i < kNumParams; ++i)
            if (i != Bypass)
                legacy += std::string(paramDesc(i).id) + "=" + std::to_string(paramDesc(i).def) + "\n";
        CHECK(!decodeUserPreset(legacy, sentinel) && same(sentinel, untouched));
        legacy.replace(0, legacy.find('\n'), "name=Good Name");
        CHECK(decodeUserPreset(legacy, out) && out.userName == "Good Name" && codecKeeps(out));
    }
    std::printf("codec tests: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
