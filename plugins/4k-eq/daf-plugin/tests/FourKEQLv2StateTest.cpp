// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Restores sessions into the built 4K EQ 2 LV2 the way an LV2 host does: every
// control port starts at its declared default, the session's values are
// written by symbol, and the plugin reads the ports when it runs. DAF's LV2
// wrapper passes on only the ports that differ from the value it last saw, so
// this path cannot be covered by the CLAP test. The band and filter frequency
// ports are positions on the console's printed dial, as in 1.0.5:
//
//   - a session saved by 1.0.5 keeps its sound, including one whose frequency
//     dials sat at their defaults, filters switched in or not;
//   - every exported factory preset sets each frequency dial port to the
//     position 1.0.5 loaded (frozen in FourKEQTestReference.hpp) and, applied
//     through lilv the way Ardour applies one, plays it in a fresh instance
//     and in one that restored a 1.0.5 session;
//   - a program selected through the programs extension, saved and reloaded,
//     keeps its dial positions.

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

// A 1.0.5 session as an LV2 host holds it: one value per 1.0.5 port symbol.
void restore105(Lv2Instance& lv2, const Settings& s)
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

void testExportedPresets(LilvWorld* world, const LilvPlugin* plugin, const std::vector<float>& in)
{
    CHECK(kNumFactoryPresets == 14, "%d factory presets, 1.0.5 had 14", kNumFactoryPresets);
    if (kNumFactoryPresets != 14)
        return;
    for (int i = 0; i < kNumFactoryPresets; ++i)
    {
        const char* const name = kFactoryPresets[i].name;
        const Settings want = presetSettings(i);
        LilvState* state = exportedPreset(world, i);
        {
            Lv2Instance ports(world, plugin);
            applyPreset(ports, state);
            const std::pair<const char*, float> dials[] = {
                { "lf_freq", want.lfFreq }, { "lm_freq", want.lmFreq }, { "hm_freq", want.hmFreq },
                { "hf_freq", want.hfFreq }, { "hpf_freq", want.hpfFreq }, { "lpf_freq", want.lpfFreq } };
            for (const auto& [symbol, dial] : dials)
                CHECK(ports.get(symbol) == dial, "preset \"%s\" sets %s to %.9g, its dial is %.9g", name, symbol,
                      (double)ports.get(symbol), (double)dial);
            // The dials 1.0.5 loaded, frozen from its build.
            for (int c = 0; c < 6; ++c)
            {
                const char* const symbol = kFourKParams[kPresetDialParams[c]].key;
                CHECK(isPresetDial105(i, c, ports.get(symbol)), "preset \"%s\" sets %s to %.9g, 1.0.5 loaded %.9g",
                      name, symbol, (double)ports.get(symbol), (double)kPresetDials105[i].dial[c]);
            }
        }

        {
            Lv2Instance lv2(world, plugin);
            applyPreset(lv2, state);
            const double diff = maxDiff(lv2.render(in), CoreRunner(want).render(in));
            CHECK(diff <= 1.0e-6, "preset \"%s\" in a fresh instance plays %.3g from its dial positions", name, diff);
            std::printf("  %-24s fresh %.2g", name, diff);
        }

        // Restored from a 1.0.5 session, run, then the preset applied.
        double worst = 0.0;
        for (const auto& [session, settings] : sessions105())
        {
            Lv2Instance lv2(world, plugin);
            restore105(lv2, settings);
            CoreRunner core(settings);
            const double before = maxDiff(lv2.render(in), core.render(in));
            applyPreset(lv2, state);
            core.apply(want);
            const double diff = maxDiff(lv2.render(in), core.render(in));
            CHECK(before <= 1.0e-6 && diff <= 1.0e-6,
                  "preset \"%s\" after \"%s\" plays %.3g from its dial positions (%.3g before it)", name, session, diff,
                  before);
            worst = std::max(worst, diff);
        }
        std::printf(", after each 1.0.5 session %.2g\n", worst);
        lilv_state_free(state);
    }
}

void testSelectedProgramsSaveTheirDials(LilvWorld* world, const LilvPlugin* plugin, const std::vector<float>& in)
{
    for (int i = 0; i < kNumFactoryPresets; ++i)
    {
        const char* const name = kFactoryPresets[i].name;
        double worst = 0.0;
        for (const auto& [session, settings] : sessions105())
        {
            Lv2Instance lv2(world, plugin);
            restore105(lv2, settings);
            CoreRunner core(settings);
            lv2.render(in);
            core.render(in);
            // A program leaves oversampling, a machine choice, where it was.
            const Settings program = presetSettings(i, settings.oversampling);
            lv2.selectProgram((uint32_t)i);
            core.apply(program);
            const double played = maxDiff(lv2.render(in), core.render(in));

            Lv2Instance reloaded(world, plugin);
            for (const auto& [symbol, value] : lv2.ports())
                reloaded.set(symbol, value);
            const double diff = maxDiff(reloaded.render(in), CoreRunner(program).render(in));
            CHECK(played <= 1.0e-6 && diff <= 1.0e-6,
                  "program \"%s\" after \"%s\" plays %.3g from its dial positions, %.3g once saved and reloaded",
                  name, session, played, diff);
            worst = std::max({ worst, played, diff });
        }
        std::printf("  %-24s %.2g\n", name, worst);
    }
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
    std::printf("[1] LV2 sessions saved by 1.0.5 keep their sound\n");
    for (const auto& [name, settings] : sessions105())
    {
        Lv2Instance lv2(world, plugin);
        restore105(lv2, settings);
        const double diff = maxDiff(lv2.render(in), CoreRunner(settings).render(in));
        CHECK(diff <= 1.0e-6, "%s: plays %.3g away from the dial API", name, diff);
        std::printf("  %-38s %.2g\n", name, diff);
    }

    std::printf("[2] exported factory presets, applied as Ardour applies them\n");
    testExportedPresets(world, plugin, in);
    std::printf("[3] programs selected, saved and reloaded\n");
    testSelectedProgramsSaveTheirDials(world, plugin, in);

    lilv_node_free(pluginUri);
    lilv_node_free(bundleUri);
    lilv_world_free(world);
    std::printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
