// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Restores sessions into the built 4K EQ 2 LV2 the way an LV2 host does: every
// control port starts at its declared default, the session's values are
// written by symbol, and the plugin reads the ports when it runs. DAF's LV2
// wrapper passes on only the ports that differ from the value it last saw, so
// this path cannot be covered by the CLAP test (dusk-audio-plugins#288):
//
//   - a pre-#288 session keeps its sound, including one whose frequency dials
//     sat at 1.0.5's defaults, filters switched in or not;
//   - a session saved with the Hz ports plays the core's Hz API, filters too;
//   - every exported factory preset, applied through lilv the way Ardour
//     applies one, plays its stated Hz in a fresh instance and in one that
//     restored a pre-#288 session, and sets no legacy dial;
//   - a program selected through the programs extension, saved and reloaded,
//     keeps its Hz;
//   - the editor's selector write takes a band off its legacy dial even where
//     the Hz port already holds the value.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <lilv/lilv.h>
#include <lv2/atom/atom.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/options/options.h>
#include <lv2/parameters/parameters.h>
#include <lv2/urid/urid.h>

#include "FourKEQBandFrequency.hpp"
#include "FourKEQDSP.hpp"
#include "FourKEQParams.hpp"
#include "FourKEQTestReference.hpp"

using namespace fourk_test;

namespace
{
int gChecks = 0, gFailures = 0;

#define CHECK(cond, ...)                                                              \
    do                                                                                \
    {                                                                                 \
        ++gChecks;                                                                    \
        if (!(cond))                                                                  \
        {                                                                             \
            ++gFailures;                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);    \
            std::fprintf(stderr, __VA_ARGS__);                                        \
            std::fputc('\n', stderr);                                                 \
        }                                                                             \
    } while (false)

constexpr const char* kUri = "https://dusk-audio.github.io/plugins/4k-eq-2";

// The programs extension as DAF's lv2_programs.h declares it.
constexpr const char* kProgramsInterface = "http://kxstudio.sf.net/ns/lv2ext/programs#Interface";
struct ProgramDescriptor { uint32_t bank, program; const char* name; };
struct ProgramsInterface
{
    const ProgramDescriptor* (*get_program)(LV2_Handle, uint32_t);
    void (*select_program)(LV2_Handle, uint32_t bank, uint32_t program);
};

std::vector<std::string> gUris;
LV2_URID mapUri(LV2_URID_Map_Handle, const char* uri)
{
    for (size_t i = 0; i < gUris.size(); ++i)
        if (gUris[i] == uri)
            return (LV2_URID)(i + 1);
    gUris.emplace_back(uri);
    return (LV2_URID)gUris.size();
}

class Lv2Instance
{
public:
    Lv2Instance(LilvWorld* world, const LilvPlugin* plugin)
        : plugin(plugin)
    {
        const uint32_t n = lilv_plugin_get_num_ports(plugin);
        controls.resize(n, 0.0f);
        std::vector<float> defaults(n, 0.0f);
        lilv_plugin_get_port_ranges_float(plugin, nullptr, nullptr, defaults.data());
        LilvNode* audio = lilv_new_uri(world, LILV_URI_AUDIO_PORT);
        LilvNode* input = lilv_new_uri(world, LILV_URI_INPUT_PORT);
        for (uint32_t i = 0; i < n; ++i)
        {
            const LilvPort* port = lilv_plugin_get_port_by_index(plugin, i);
            const std::string symbol = lilv_node_as_string(lilv_port_get_symbol(plugin, port));
            if (lilv_port_is_a(plugin, port, audio))
                (lilv_port_is_a(plugin, port, input) ? audioIn : audioOut).push_back(i);
            else
            {
                controls[i] = std::isnan(defaults[i]) ? 0.0f : defaults[i];
                bySymbol[symbol] = i;
            }
        }
        lilv_node_free(audio);
        lilv_node_free(input);

        map.handle = nullptr;
        map.map = mapUri;
        mapFeature = { LV2_URID__map, &map };
        options[0] = { LV2_OPTIONS_INSTANCE, 0, mapUri(nullptr, LV2_BUF_SIZE__maxBlockLength),
                       sizeof(int32_t), mapUri(nullptr, LV2_ATOM__Int), &maxBlockValue };
        options[1] = { LV2_OPTIONS_INSTANCE, 0, mapUri(nullptr, LV2_BUF_SIZE__nominalBlockLength),
                       sizeof(int32_t), mapUri(nullptr, LV2_ATOM__Int), &maxBlockValue };
        options[2] = { LV2_OPTIONS_INSTANCE, 0, mapUri(nullptr, LV2_PARAMETERS__sampleRate),
                       sizeof(float), mapUri(nullptr, LV2_ATOM__Float), &rateValue };
        options[3] = { LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, nullptr };
        maxBlockValue = (int32_t)kBlock;
        rateValue = (float)kRate;
        optionsFeature = { LV2_OPTIONS__options, options };
        boundedFeature = { LV2_BUF_SIZE__boundedBlockLength, nullptr };
        const LV2_Feature* features[] = { &mapFeature, &optionsFeature, &boundedFeature, nullptr };

        instance = lilv_plugin_instantiate(plugin, kRate, features);
        if (instance == nullptr)
        {
            std::fprintf(stderr, "FAIL: could not instantiate %s\n", kUri);
            std::exit(1);
        }
        inL.resize(kBlock); inR.resize(kBlock); outL.resize(kBlock); outR.resize(kBlock);
        for (const auto& control : bySymbol)
            lilv_instance_connect_port(instance, control.second, &controls[control.second]);
        lilv_instance_connect_port(instance, audioIn.at(0), inL.data());
        lilv_instance_connect_port(instance, audioIn.at(1), inR.data());
        lilv_instance_connect_port(instance, audioOut.at(0), outL.data());
        lilv_instance_connect_port(instance, audioOut.at(1), outR.data());
    }
    ~Lv2Instance()
    {
        if (active)
            lilv_instance_deactivate(instance);
        lilv_instance_free(instance);
    }

    float get(const std::string& symbol) const { return controls[bySymbol.at(symbol)]; }

    // Every control port's value, as a host saves them.
    std::map<std::string, float> ports() const
    {
        std::map<std::string, float> out;
        for (const auto& [symbol, index] : bySymbol)
            out[symbol] = controls[index];
        return out;
    }

    void selectProgram(uint32_t index)
    {
        const auto* programs = (const ProgramsInterface*) lilv_instance_get_extension_data(instance, kProgramsInterface);
        if (programs == nullptr || programs->select_program == nullptr)
        {
            std::fprintf(stderr, "FAIL: no programs interface\n");
            std::exit(1);
        }
        programs->select_program(lilv_instance_get_handle(instance), index / 128, index % 128);
    }

    LilvInstance* handle() const { return instance; }

    void set(const std::string& symbol, float value)
    {
        const auto it = bySymbol.find(symbol);
        if (it == bySymbol.end())
        {
            std::fprintf(stderr, "FAIL: no control port %s\n", symbol.c_str());
            std::exit(1);
        }
        controls[it->second] = value;
    }

    std::vector<float> render(const std::vector<float>& input)
    {
        if (!active)
        {
            lilv_instance_activate(instance);
            active = true;
        }
        std::vector<float> result;
        for (int b = 0; b < kBlocks; ++b)
        {
            for (uint32_t i = 0; i < kBlock; ++i)
            {
                inL[i] = input[2 * (b * kBlock + i)];
                inR[i] = input[2 * (b * kBlock + i) + 1];
            }
            lilv_instance_run(instance, kBlock);
            for (uint32_t i = 0; i < kBlock; ++i)
            {
                result.push_back(outL[i]);
                result.push_back(outR[i]);
            }
        }
        return result;
    }

private:
    const LilvPlugin* plugin;
    LilvInstance* instance = nullptr;
    bool active = false;
    std::vector<float> controls, inL, inR, outL, outR;
    std::vector<uint32_t> audioIn, audioOut;
    std::map<std::string, uint32_t> bySymbol;
    LV2_URID_Map map {};
    LV2_Feature mapFeature {}, optionsFeature {}, boundedFeature {};
    LV2_Options_Option options[4] {};
    int32_t maxBlockValue = 0;
    float rateValue = 0.0f;
};

// A pre-#288 session as an LV2 host holds it: one value per 1.0.5 port symbol.
void restoreLegacy(Lv2Instance& lv2, const LegacySettings& s)
{
    lv2.set("hpf_freq", s.hpfFreq); lv2.set("hpf_enabled", s.hpfEnabled);
    lv2.set("lpf_freq", s.lpfFreq); lv2.set("lpf_enabled", s.lpfEnabled);
    lv2.set("lf_gain", s.lfGain); lv2.set("lf_freq", s.lfFreq); lv2.set("lf_bell", s.lfBell);
    lv2.set("lm_gain", s.lmGain); lv2.set("lm_freq", s.lmFreq); lv2.set("lm_q", s.lmQ);
    lv2.set("hm_gain", s.hmGain); lv2.set("hm_freq", s.hmFreq); lv2.set("hm_q", s.hmQ);
    lv2.set("hf_gain", s.hfGain); lv2.set("hf_freq", s.hfFreq); lv2.set("hf_bell", s.hfBell);
    lv2.set("eq_type", s.eqType);
    lv2.set("input_gain", s.inputGain); lv2.set("output_gain", s.outputGain);
    lv2.set("oversampling", s.oversampling); lv2.set("auto_gain", s.autoGain);
}

float legacyDialPort(const LegacySettings& s, const std::string& symbol)
{
    const std::map<std::string, float> dials {
        { "hpf_freq", s.hpfFreq }, { "lpf_freq", s.lpfFreq }, { "lf_freq", s.lfFreq },
        { "lm_freq", s.lmFreq }, { "hm_freq", s.hmFreq }, { "hf_freq", s.hfFreq } };
    return dials.at(symbol);
}

LV2_URID_Map gMap { nullptr, mapUri };

void setPortValue(const char* symbol, void* user, const void* value, uint32_t, uint32_t type)
{
    float v = 0.0f;
    if (type == mapUri(nullptr, LV2_ATOM__Float)) v = *static_cast<const float*>(value);
    else if (type == mapUri(nullptr, LV2_ATOM__Double)) v = (float)*static_cast<const double*>(value);
    else if (type == mapUri(nullptr, LV2_ATOM__Int) || type == mapUri(nullptr, LV2_ATOM__Bool))
        v = (float)*static_cast<const int32_t*>(value);
    else if (type == mapUri(nullptr, LV2_ATOM__Long)) v = (float)*static_cast<const int64_t*>(value);
    else
    {
        std::fprintf(stderr, "FAIL: preset port %s has an unknown value type\n", symbol);
        std::exit(1);
    }
    static_cast<Lv2Instance*>(user)->set(symbol, v);
}

void collectSymbol(const char* symbol, void* user, const void*, uint32_t, uint32_t)
{
    static_cast<std::vector<std::string>*>(user)->push_back(symbol);
}

// Factory preset i as exported to presets.ttl.
LilvState* exportedPreset(LilvWorld* world, int index)
{
    char uri[128];
    std::snprintf(uri, sizeof(uri), "%s#preset%03d", kUri, index + 1);
    LilvNode* node = lilv_new_uri(world, uri);
    lilv_world_load_resource(world, node);
    LilvState* state = lilv_state_new_from_world(world, &gMap, node);
    lilv_node_free(node);
    if (state == nullptr)
    {
        std::fprintf(stderr, "FAIL: no exported preset %s\n", uri);
        std::exit(1);
    }
    return state;
}

// As Ardour applies an LV2 preset: lilv writes each of its port values.
void applyPreset(Lv2Instance& lv2, const LilvState* state)
{
    lilv_state_restore(state, lv2.handle(), setPortValue, &lv2, 0, nullptr);
}

// What factory preset i states, as settings for the core.
LegacySettings presetSettings(int index)
{
    const FourKEQPreset& p = kFactoryPresets[index];
    LegacySettings s;
    s.eqType = p.eqType;
    s.lfGain = p.lfGain; s.lfBell = p.lfBell;
    s.lmGain = p.lmGain; s.lmQ = p.lmQ;
    s.hmGain = p.hmGain; s.hmQ = p.hmQ;
    s.hfGain = p.hfGain; s.hfBell = p.hfBell;
    s.hpfEnabled = p.hpfFreq > 16.5f ? 1.f : 0.f;
    s.lpfEnabled = p.lpfFreq < 15200.5f ? 1.f : 0.f;
    s.inputGain = p.inputGain; s.outputGain = p.outputGain;
    return s;
}

CoreBands presetBands(int index)
{
    const FourKEQPreset& p = kFactoryPresets[index];
    return { p.lfFreq, p.lmFreq, p.hmFreq, p.hfFreq, true };
}

CoreFilters presetFilters(int index)
{
    const FourKEQPreset& p = kFactoryPresets[index];
    return { p.hpfFreq, p.lpfFreq, true };
}

void testExportedPresets(LilvWorld* world, const LilvPlugin* plugin, const std::vector<float>& in)
{
    const char* const legacyDials[] = { "hpf_freq", "lpf_freq", "lf_freq", "lm_freq", "hm_freq", "hf_freq" };
    for (int i = 0; i < kNumFactoryPresets; ++i)
    {
        const char* const name = kFactoryPresets[i].name;
        LilvState* state = exportedPreset(world, i);
        std::vector<std::string> symbols;
        lilv_state_emit_port_values(state, collectSymbol, &symbols);
        for (const char* dial : legacyDials)
            for (const std::string& symbol : symbols)
                CHECK(symbol != dial, "preset \"%s\" sets the legacy dial %s", name, dial);

        {
            Lv2Instance lv2(world, plugin);
            applyPreset(lv2, state);
            const double diff = maxDiff(lv2.render(in), CoreRunner(presetSettings(i), presetBands(i), presetFilters(i)).render(in));
            CHECK(diff <= 1.0e-6, "preset \"%s\" in a fresh instance plays %.3g from its stated Hz", name, diff);
            std::printf("  %-24s fresh %.2g", name, diff);
        }

        // Restored from a pre-#288 session, run, then the preset applied: the
        // bands and filters that were on their dials come back to Hz, even
        // where the Hz port already held the preset's value.
        double worst = 0.0;
        for (const auto& [session, settings] : legacySessions())
        {
            Lv2Instance lv2(world, plugin);
            restoreLegacy(lv2, settings);
            CoreRunner core(settings, dialsOf(settings));
            const double before = maxDiff(lv2.render(in), core.render(in));
            applyPreset(lv2, state);
            core.apply(presetSettings(i), presetBands(i), presetFilters(i));
            const double diff = maxDiff(lv2.render(in), core.render(in));
            CHECK(before <= 1.0e-6 && diff <= 1.0e-6,
                  "preset \"%s\" after \"%s\" plays %.3g from its stated Hz (%.3g before it)", name, session, diff, before);
            for (const char* dial : legacyDials)
                CHECK(lv2.get(dial) == legacyDialPort(settings, dial), "preset \"%s\" moved %s", name, dial);
            worst = std::max(worst, diff);
        }
        std::printf(", after each pre-#288 session %.2g\n", worst);
        lilv_state_free(state);
    }
}

void testSelectedProgramsSaveTheirHz(LilvWorld* world, const LilvPlugin* plugin, const std::vector<float>& in)
{
    for (int i = 0; i < kNumFactoryPresets; ++i)
    {
        const char* const name = kFactoryPresets[i].name;
        double worst = 0.0;
        for (const auto& [session, settings] : legacySessions())
        {
            Lv2Instance lv2(world, plugin);
            restoreLegacy(lv2, settings);
            CoreRunner core(settings, dialsOf(settings));
            lv2.render(in);
            core.render(in);
            // A program leaves oversampling, a machine choice, where it was.
            LegacySettings program = presetSettings(i);
            program.oversampling = settings.oversampling;
            lv2.selectProgram((uint32_t)i);
            core.apply(program, presetBands(i), presetFilters(i));
            const double played = maxDiff(lv2.render(in), core.render(in));

            Lv2Instance reloaded(world, plugin);
            for (const auto& [symbol, value] : lv2.ports())
                reloaded.set(symbol, value);
            const double diff = maxDiff(reloaded.render(in),
                                        CoreRunner(program, presetBands(i), presetFilters(i)).render(in));
            CHECK(played <= 1.0e-6 && diff <= 1.0e-6,
                  "program \"%s\" after \"%s\" plays %.3g from its stated Hz, %.3g once saved and reloaded",
                  name, session, played, diff);
            worst = std::max({ worst, played, diff });
        }
        std::printf("  %-24s %.2g\n", name, worst);
    }
}

// The editor takes a band off its legacy dial with an Hz write and a stated
// selector (FourKEQUI.cpp setFrequency). Pre-#288 dials at 1.0.5's defaults
// leave every Hz port at its default, where a reset writes the same value, so
// only the selector reaches the plugin. Legacy dial automation then puts the
// bands back on their dials, and a second reset has to state the same bits
// again, which the other flag carries past the unchanged port.
void testEditorTakesBandsOffTheirDials(LilvWorld* world, const LilvPlugin* plugin, const std::vector<float>& in)
{
    const LegacySettings s = legacySessions()[4].second;
    const CoreBands defaults { kFourKParams[kLfHz].def, kFourKParams[kLmHz].def,
                               kFourKParams[kHmHz].def, kFourKParams[kHfHz].def, true };
    Lv2Instance lv2(world, plugin);
    restoreLegacy(lv2, s);
    CoreRunner core(s, dialsOf(s));
    lv2.render(in);
    core.render(in);

    const auto reset = [&] {
        for (const uint32_t id : { (uint32_t)kLfHz, (uint32_t)kLmHz, (uint32_t)kHmHz, (uint32_t)kHfHz })
            lv2.set(kFourKParams[id].key, kFourKParams[id].def);
        lv2.set("legacy_dial_bands", fkStatedSelector(0u, lv2.get("legacy_dial_bands"), kLegacyDialBandsMax));
    };
    reset();
    core.apply(s, defaults);
    const double first = maxDiff(lv2.render(in), core.render(in));
    CHECK(first <= 1.0e-6, "an editor reset left a band on its legacy dial (%.3g)", first);

    LegacySettings automated = s;
    automated.lfFreq = 90.f; automated.lmFreq = 700.f; automated.hmFreq = 2500.f; automated.hfFreq = 9000.f;
    restoreLegacy(lv2, automated);
    core.apply(automated, dialsOf(automated));
    const double onDials = maxDiff(lv2.render(in), core.render(in));
    CHECK(onDials <= 1.0e-6, "legacy dial automation did not take the bands (%.3g)", onDials);

    const float stated = lv2.get("legacy_dial_bands");
    reset();
    CHECK(lv2.get("legacy_dial_bands") != stated, "a second reset repeated the selector value %g", stated);
    core.apply(automated, defaults);
    const double second = maxDiff(lv2.render(in), core.render(in));
    CHECK(second <= 1.0e-6, "a second editor reset left a band on its legacy dial (%.3g)", second);
    std::printf("  reset %.2g, legacy dial automation %.2g, reset again %.2g\n", first, onDials, second);
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: %s <4k-eq-2.lv2 bundle directory>\n", argv[0]);
        return 2;
    }
    LilvWorld* world = lilv_world_new();
    std::string bundle = argv[1];
    if (bundle.back() != '/')
        bundle += '/';
    LilvNode* bundleUri = lilv_new_file_uri(world, nullptr, bundle.c_str());
    lilv_world_load_bundle(world, bundleUri);
    LilvNode* pluginUri = lilv_new_uri(world, kUri);
    const LilvPlugin* plugin = lilv_plugins_get_by_uri(lilv_world_get_all_plugins(world), pluginUri);
    if (plugin == nullptr)
    {
        std::fprintf(stderr, "FAIL: %s not found in %s\n", kUri, argv[1]);
        return 1;
    }

    const std::vector<float> in = noise();
    std::printf("[1] pre-#288 LV2 sessions keep their sound\n");
    for (const auto& [name, settings] : legacySessions())
    {
        Lv2Instance lv2(world, plugin);
        restoreLegacy(lv2, settings);
        const double diff = maxDiff(lv2.render(in), CoreRunner(settings, dialsOf(settings)).render(in));
        CHECK(diff <= 1.0e-6, "%s: plays %.3g away from the dial API", name, diff);
        std::printf("  %-38s %.2g\n", name, diff);
    }

    std::printf("[2] an LV2 session saved with the Hz ports\n");
    {
        LegacySettings s;
        s.eqType = 1.f; s.lfGain = 6.f; s.lmGain = -4.f; s.hmGain = 7.5f; s.hfGain = 5.f;
        s.hpfEnabled = 1.f; s.lpfEnabled = 1.f;
        const CoreBands hz { 450.f, 200.f, 7000.f, 1500.f, true };
        const CoreFilters filters { 350.f, 3000.f, true };
        // Saved by this build: the legacy dial ports were never moved, so
        // they stay at their declared defaults.
        Lv2Instance lv2(world, plugin);
        lv2.set("eq_type", s.eqType);
        lv2.set("lf_gain", s.lfGain); lv2.set("lm_gain", s.lmGain);
        lv2.set("hm_gain", s.hmGain); lv2.set("hf_gain", s.hfGain);
        lv2.set("lf_hz", hz.lf); lv2.set("lm_hz", hz.lm); lv2.set("hm_hz", hz.hm); lv2.set("hf_hz", hz.hf);
        lv2.set("hpf_enabled", 1.f); lv2.set("lpf_enabled", 1.f);
        lv2.set("hpf_hz", filters.hpf); lv2.set("lpf_hz", filters.lpf);
        const double diff = maxDiff(lv2.render(in), CoreRunner(s, hz, filters).render(in));
        CHECK(diff <= 1.0e-6, "Hz session plays %.3g away from the Hz API", diff);
        std::printf("  Black, LF 450 / LM 200 / HM 7000 / HF 1500 / HPF 350 / LPF 3000 Hz: %.2g\n", diff);
    }

    std::printf("[3] exported factory presets, applied as Ardour applies them\n");
    testExportedPresets(world, plugin, in);
    std::printf("[4] programs selected, saved and reloaded\n");
    testSelectedProgramsSaveTheirHz(world, plugin, in);
    std::printf("[5] the editor takes bands off their legacy dials\n");
    testEditorTakesBandsOffTheirDials(world, plugin, in);

    lilv_node_free(pluginUri);
    lilv_node_free(bundleUri);
    lilv_world_free(world);
    std::printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
