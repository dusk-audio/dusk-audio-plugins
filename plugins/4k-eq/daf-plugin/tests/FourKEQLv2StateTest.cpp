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
//   - a session saved with the Hz ports plays the core's Hz API, filters too.

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

    lilv_node_free(pluginUri);
    lilv_node_free(bundleUri);
    lilv_world_free(world);
    std::printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
