// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Loads states into the built 4K EQ 2 CLAP and checks what it plays against
// the FourKEQDSP core configured directly. The band and filter frequency
// parameters are positions on the console's printed dial, played through the
// core's dial API, as in 1.0.5:
//
//   - a state saved by 1.0.5 (every frequency a dial position under
//     lf_freq..hf_freq, hpf_freq and lpf_freq) plays what the core's dial API
//     plays, and keeps doing so after the plugin saves it again and a fresh
//     instance reloads it;
//   - automation of the six frequency parameters does the same;
//   - the six frequency parameters have 1.0.5's names, ranges and defaults;
//   - every factory program loads the dial positions 1.0.5 loaded (frozen in
//     FourKEQTestReference.hpp) and plays them.
//
// The comparisons are sample for sample: the plugin runs the same core.
//
// Linux and macOS only (dlopen).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <dlfcn.h>

#include "clap/entry.h"
#include "clap/plugin.h"
#include "clap/ext/audio-ports.h"
#include "clap/ext/params.h"
#include "clap/ext/state.h"
#include "clap/factory/plugin-factory.h"

#include "FourKEQDSP.hpp"
#include "FourKEQParams.hpp"
#include "FourKEQTestReference.hpp"

using duskaudio::FourKEQDSP;
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


const void* CLAP_ABI hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void CLAP_ABI hostNoop(const clap_host_t*) {}
const clap_host_t kHost = {
    CLAP_VERSION_INIT, nullptr, "FourKEQClapStateTest", "Dusk Audio", "https://dusk-audio.github.io/", "1.0.0",
    hostGetExtension, hostNoop, hostNoop, hostNoop
};

struct Events
{
    clap_input_events_t in;
    clap_output_events_t out;
    std::vector<clap_event_param_value> queued;

    Events()
    {
        in.ctx = this;
        in.size = [](const clap_input_events_t* l) -> uint32_t
        { return (uint32_t) static_cast<const Events*>(l->ctx)->queued.size(); };
        in.get = [](const clap_input_events_t* l, uint32_t i) -> const clap_event_header_t*
        { return &static_cast<const Events*>(l->ctx)->queued[i].header; };
        out.ctx = this;
        out.try_push = [](const clap_output_events_t*, const clap_event_header_t*) { return true; };
    }

    void queue(clap_id id, double value)
    {
        clap_event_param_value ev = {};
        ev.header.size = sizeof(ev);
        ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.header.type = CLAP_EVENT_PARAM_VALUE;
        ev.param_id = id;
        ev.note_id = -1; ev.port_index = -1; ev.channel = -1; ev.key = -1;
        ev.value = value;
        queued.push_back(ev);
    }
};

struct Library
{
    const clap_plugin_factory_t* factory = nullptr;
    const char* id = nullptr;
};

struct Instance
{
    const clap_plugin_t* plugin = nullptr;
    const clap_plugin_params_t* params = nullptr;
    const clap_plugin_state_t* state = nullptr;
    std::map<std::string, clap_id> bySymbol, byName;
    bool active = false;

    explicit Instance(const Library& lib)
    {
        plugin = lib.factory->create_plugin(lib.factory, &kHost, lib.id);
        if (plugin == nullptr || !plugin->init(plugin))
        {
            std::fprintf(stderr, "FAIL: could not create the plugin\n");
            std::exit(1);
        }
        params = (const clap_plugin_params_t*) plugin->get_extension(plugin, CLAP_EXT_PARAMS);
        state = (const clap_plugin_state_t*) plugin->get_extension(plugin, CLAP_EXT_STATE);
        if (params == nullptr || state == nullptr)
        {
            std::fprintf(stderr, "FAIL: no params or state extension\n");
            std::exit(1);
        }
        for (uint32_t i = 0, n = params->count(plugin); i < n; ++i)
        {
            clap_param_info_t info = {};
            params->get_info(plugin, i, &info);
            bySymbol[info.module] = info.id; // DAF puts the symbol in module
            byName[info.name] = info.id;
        }
    }
    ~Instance()
    {
        if (active) { plugin->stop_processing(plugin); plugin->deactivate(plugin); }
        plugin->destroy(plugin);
    }

    clap_id idOf(const std::string& symbol) const
    {
        const auto it = bySymbol.find(symbol);
        if (it == bySymbol.end())
        {
            std::fprintf(stderr, "FAIL: no parameter with symbol %s\n", symbol.c_str());
            std::exit(1);
        }
        return it->second;
    }

    bool load(const std::string& blob)
    {
        struct Reader { const std::string* data; size_t pos; } reader { &blob, 0 };
        clap_istream_t stream;
        stream.ctx = &reader;
        stream.read = [](const clap_istream_t* s, void* buffer, uint64_t size) -> int64_t
        {
            Reader* r = static_cast<Reader*>(s->ctx);
            const size_t n = std::min<size_t>((size_t)size, r->data->size() - r->pos);
            std::memcpy(buffer, r->data->data() + r->pos, n);
            r->pos += n;
            return (int64_t)n;
        };
        return state->load(plugin, &stream);
    }

    std::string save()
    {
        std::string out;
        clap_ostream_t stream;
        stream.ctx = &out;
        stream.write = [](const clap_ostream_t* s, const void* buffer, uint64_t size) -> int64_t
        {
            static_cast<std::string*>(s->ctx)->append(static_cast<const char*>(buffer), (size_t)size);
            return (int64_t)size;
        };
        if (!state->save(plugin, &stream))
        {
            std::fprintf(stderr, "FAIL: state save\n");
            std::exit(1);
        }
        return out;
    }

    // Stereo in, stereo out, kBlocks blocks; `events` is delivered with the first.
    std::vector<float> render(const std::vector<float>& input, Events* events = nullptr)
    {
        if (!active)
        {
            if (!plugin->activate(plugin, kRate, 1, kBlock) || !plugin->start_processing(plugin))
            {
                std::fprintf(stderr, "FAIL: activate\n");
                std::exit(1);
            }
            active = true;
        }
        Events none;
        std::vector<float> inL(kBlock), inR(kBlock), outL(kBlock), outR(kBlock), result;
        float* inPtrs[2] = { inL.data(), inR.data() };
        float* outPtrs[2] = { outL.data(), outR.data() };
        clap_audio_buffer_t inBuf = {}, outBuf = {};
        inBuf.data32 = inPtrs; inBuf.channel_count = 2;
        outBuf.data32 = outPtrs; outBuf.channel_count = 2;
        for (int b = 0; b < kBlocks; ++b)
        {
            for (uint32_t i = 0; i < kBlock; ++i)
            {
                inL[i] = input[2 * (b * kBlock + i)];
                inR[i] = input[2 * (b * kBlock + i) + 1];
            }
            Events* ev = (b == 0 && events != nullptr) ? events : &none;
            clap_process_t p = {};
            p.steady_time = -1;
            p.frames_count = kBlock;
            p.audio_inputs = &inBuf; p.audio_inputs_count = 1;
            p.audio_outputs = &outBuf; p.audio_outputs_count = 1;
            p.in_events = &ev->in; p.out_events = &ev->out;
            plugin->process(plugin, &p);
            for (uint32_t i = 0; i < kBlock; ++i)
            {
                result.push_back(outL[i]);
                result.push_back(outR[i]);
            }
        }
        return result;
    }
};

// A state as the plugin's CLAP wrapper writes it: program, then every input
// parameter as symbol/value, NUL separated, 0xfe terminated.
std::string blob(const std::vector<std::pair<std::string, std::string>>& params, int program = 0)
{
    std::string s = "__daf_program__";
    s += '\0'; s += std::to_string(program); s += '\0';
    s += "__daf_parameters_begin__"; s += '\0';
    for (const auto& p : params)
    {
        s += p.first; s += '\0';
        s += p.second; s += '\0';
    }
    s += "__daf_parameters_end__"; s += '\0';
    s += '\xfe'; s += '\0';
    return s;
}

std::string num(float v)
{
    char b[64];
    std::snprintf(b, sizeof(b), "%.12g", (double)v);
    return b;
}

std::string stateBlob(const Settings& s)
{
    auto i = [](float v) { return std::to_string((int)std::lround(v)); };
    return blob({
        { "hpf_freq", num(s.hpfFreq) }, { "hpf_enabled", i(s.hpfEnabled) },
        { "lpf_freq", num(s.lpfFreq) }, { "lpf_enabled", i(s.lpfEnabled) },
        { "lf_gain", num(s.lfGain) }, { "lf_freq", num(s.lfFreq) }, { "lf_bell", i(s.lfBell) },
        { "lm_gain", num(s.lmGain) }, { "lm_freq", num(s.lmFreq) }, { "lm_q", num(s.lmQ) },
        { "hm_gain", num(s.hmGain) }, { "hm_freq", num(s.hmFreq) }, { "hm_q", num(s.hmQ) },
        { "hf_gain", num(s.hfGain) }, { "hf_freq", num(s.hfFreq) }, { "hf_bell", i(s.hfBell) },
        { "eq_type", i(s.eqType) }, { "bypass", "0" },
        { "input_gain", num(s.inputGain) }, { "output_gain", num(s.outputGain) },
        { "saturation", "0" }, { "oversampling", i(s.oversampling) }, { "ms_mode", "0" },
        { "spectrum_prepost", "0" }, { "auto_gain", i(s.autoGain) }, { "show_graph", "1" },
    });
}

void testSessionsKeepTheirSound(const Library& lib)
{
    const std::vector<float> in = noise();
    for (const auto& [name, settings] : sessions105())
    {
        const std::vector<float> want = CoreRunner(settings).render(in);

        Instance first(lib);
        CHECK(first.load(stateBlob(settings)), "%s: 1.0.5 state rejected", name);
        const double loaded = maxDiff(first.render(in), want);
        CHECK(loaded <= 1.0e-6, "%s: 1.0.5 state plays %.3g away from the dial API", name, loaded);

        // The plugin saves it again, and a fresh instance reloads that.
        const std::string resaved = first.save();
        Instance second(lib);
        CHECK(second.load(resaved), "%s: re-saved state rejected", name);
        const double reloaded = maxDiff(second.render(in), want);
        CHECK(reloaded <= 1.0e-6, "%s: re-saved state plays %.3g away from the dial API", name, reloaded);
        std::printf("  %-38s loaded %.2g, re-saved and reloaded %.2g\n", name, loaded, reloaded);
    }
}

void testFrequencyAutomation(const Library& lib)
{
    const std::vector<float> in = noise();
    Settings s;
    s.lfGain = 6.f; s.lmGain = -5.f; s.hmGain = 7.f; s.hfGain = 4.f;
    s.lfFreq = 60.f; s.lmFreq = 1450.f; s.hmFreq = 5250.f; s.hfFreq = 3500.f;
    s.hpfEnabled = 1.f; s.lpfEnabled = 1.f;
    s.hpfFreq = 120.f; s.lpfFreq = 8800.f;

    Instance plugin(lib);
    Events lane;
    lane.queue(plugin.idOf("lf_gain"), s.lfGain);
    lane.queue(plugin.idOf("lm_gain"), s.lmGain);
    lane.queue(plugin.idOf("hm_gain"), s.hmGain);
    lane.queue(plugin.idOf("hf_gain"), s.hfGain);
    lane.queue(plugin.idOf("hpf_enabled"), 1.0);
    lane.queue(plugin.idOf("lpf_enabled"), 1.0);
    lane.queue(plugin.idOf("lf_freq"), s.lfFreq);
    lane.queue(plugin.idOf("lm_freq"), s.lmFreq);
    lane.queue(plugin.idOf("hm_freq"), s.hmFreq);
    lane.queue(plugin.idOf("hf_freq"), s.hfFreq);
    lane.queue(plugin.idOf("hpf_freq"), s.hpfFreq);
    lane.queue(plugin.idOf("lpf_freq"), s.lpfFreq);
    const double played = maxDiff(plugin.render(in, &lane), CoreRunner(s).render(in));
    CHECK(played <= 1.0e-6, "automated frequency dials play %.3g away from the dial API", played);

    // Saved after the lane and reloaded into a fresh instance.
    const std::string saved = plugin.save();
    Instance reloaded(lib);
    CHECK(reloaded.load(saved), "the automated state was rejected");
    const double again = maxDiff(reloaded.render(in), CoreRunner(s).render(in));
    CHECK(again <= 1.0e-6, "the automated state reloads %.3g away from the dial API", again);
    std::printf("  automated dials %.2g from the dial API; saved and reloaded %.2g\n", played, again);
}

void testFrequencyParametersAreThe105Dials(const Library& lib)
{
    // 1.0.5's six frequency parameters: symbol, host name, range, default.
    struct Want { const char* symbol; const char* name; double min, max, def; };
    const Want want[6] = {
        { "hpf_freq", "HPF Frequency", 16.0, 350.0, 16.0 },
        { "lpf_freq", "LPF Frequency", 3000.0, 15201.0, 15201.0 },
        { "lf_freq", "LF Frequency", 30.0, 450.0, 200.0 },
        { "lm_freq", "LM Frequency", 200.0, 2500.0, 1000.0 },
        { "hm_freq", "HM Frequency", 600.0, 7000.0, 3000.0 },
        { "hf_freq", "HF Frequency", 1500.0, 16000.0, 8000.0 },
    };
    Instance plugin(lib);
    CHECK(plugin.params->count(plugin.plugin) == 28u, "the CLAP has %u parameters, 1.0.5 had 28",
          plugin.params->count(plugin.plugin));
    for (const Want& w : want)
    {
        clap_param_info_t info = {};
        bool found = false;
        for (uint32_t i = 0, n = plugin.params->count(plugin.plugin); i < n && !found; ++i)
            found = plugin.params->get_info(plugin.plugin, i, &info) && info.id == plugin.idOf(w.symbol);
        CHECK(found && std::string(info.name) == w.name && info.min_value == w.min && info.max_value == w.max
                  && info.default_value == w.def,
              "%s is \"%s\" %.6g..%.6g default %.6g, 1.0.5's is \"%s\" %.6g..%.6g default %.6g", w.symbol,
              info.name, info.min_value, info.max_value, info.default_value, w.name, w.min, w.max, w.def);
    }
    std::printf("  hpf_freq, lpf_freq, lf_freq, lm_freq, hm_freq, hf_freq: 1.0.5's names, ranges and defaults\n");
}

void testFactoryProgramsLoadThe105Dials(const Library& lib)
{
    CHECK(kNumFactoryPresets == 14, "%d factory programs, 1.0.5 had 14", kNumFactoryPresets);
    if (kNumFactoryPresets != 14)
        return;
    const std::vector<float> in = noise();
    for (int i = 0; i < kNumFactoryPresets; ++i)
    {
        const FourKEQPreset& p = kFactoryPresets[i];
        const Settings s = presetSettings(i);
        const std::vector<float> want = CoreRunner(s).render(in);

        Instance plugin(lib);
        std::string program = "__daf_program__";
        program += '\0'; program += std::to_string(i); program += '\0'; program += '\xfe'; program += '\0';
        CHECK(plugin.load(program), "program %d rejected", i);
        const double diff = maxDiff(plugin.render(in), want);
        CHECK(diff <= 1.0e-6, "program \"%s\" plays %.3g away from its dial positions", p.name, diff);
        const std::pair<const char*, float> dials[] = {
            { "lf_freq", s.lfFreq }, { "lm_freq", s.lmFreq }, { "hm_freq", s.hmFreq }, { "hf_freq", s.hfFreq },
            { "hpf_freq", s.hpfFreq }, { "lpf_freq", s.lpfFreq } };
        for (const auto& [sym, dial] : dials)
        {
            double v = 0.0;
            plugin.params->get_value(plugin.plugin, plugin.idOf(sym), &v);
            CHECK(v == (double)dial, "program \"%s\" %s reads %.9g, its dial is %.9g", p.name, sym, v, (double)dial);
        }
        // The dials 1.0.5 loaded, frozen from its build.
        for (int c = 0; c < 6; ++c)
        {
            const char* const sym = kFourKParams[kPresetDialParams[c]].key;
            double v = 0.0;
            plugin.params->get_value(plugin.plugin, plugin.idOf(sym), &v);
            CHECK(isPresetDial105(i, c, (float)v), "program \"%s\" %s reads %.9g, 1.0.5 loaded %.9g", p.name, sym, v,
                  (double)kPresetDials105[i].dial[c]);
        }
        std::printf("  %-24s %.2g from LF %.4g / LM %.4g / HM %.4g / HF %.4g / HPF %.4g / LPF %.4g\n",
                    p.name, diff, (double)s.lfFreq, (double)s.lmFreq, (double)s.hmFreq, (double)s.hfFreq,
                    (double)s.hpfFreq, (double)s.lpfFreq);
    }
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: %s <4k-eq-2.clap>\n", argv[0]);
        return 2;
    }
    void* const handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr)
    {
        std::fprintf(stderr, "FAIL: dlopen(%s): %s\n", argv[1], dlerror());
        return 1;
    }
    const auto* entry = (const clap_plugin_entry_t*) dlsym(handle, "clap_entry");
    if (entry == nullptr || !entry->init(argv[1]))
    {
        std::fprintf(stderr, "FAIL: clap_entry\n");
        return 1;
    }
    Library lib;
    lib.factory = (const clap_plugin_factory_t*) entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    if (lib.factory == nullptr || lib.factory->get_plugin_count(lib.factory) == 0)
    {
        std::fprintf(stderr, "FAIL: no plugin factory\n");
        return 1;
    }
    lib.id = lib.factory->get_plugin_descriptor(lib.factory, 0)->id;

    std::printf("[1] states saved by 1.0.5 keep their sound\n");
    testSessionsKeepTheirSound(lib);
    std::printf("[2] automation of the frequency dials\n");
    testFrequencyAutomation(lib);
    std::printf("[3] the frequency parameters are 1.0.5's\n");
    testFrequencyParametersAreThe105Dials(lib);
    std::printf("[4] factory programs load 1.0.5's dial positions\n");
    testFactoryProgramsLoadThe105Dials(lib);

    entry->deinit();
    std::printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
