// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Loads states into the built 4K EQ 2 CLAP and checks what it plays against
// the FourKEQDSP core configured directly (dusk-audio-plugins#288):
//
//   - a state saved before #288 (band frequencies stored as dial positions
//     under lf_freq..hf_freq) plays what the core's dial API plays, and keeps
//     doing so after the plugin saves it again and a fresh instance reloads it;
//   - automation replayed into a legacy dial parameter takes the band over;
//   - the band frequency parameters are Hz: HM Frequency at 7000 plays the
//     core's Hz API at 7 kHz and peaks there;
//   - every factory program plays its stated band frequencies through the Hz
//     API.
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

std::string legacyBlob(const LegacySettings& s)
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

void testLegacySessionsKeepTheirSound(const Library& lib)
{
    const std::vector<float> in = noise();
    for (const auto& [name, settings] : legacySessions())
    {
        const std::vector<float> want = CoreRunner(settings, dialsOf(settings)).render(in);

        Instance first(lib);
        CHECK(first.load(legacyBlob(settings)), "%s: legacy state rejected", name);
        const double loaded = maxDiff(first.render(in), want);
        CHECK(loaded <= 1.0e-6, "%s: legacy state plays %.3g away from the dial API", name, loaded);

        // The plugin saves it again, and a fresh instance reloads that.
        const std::string resaved = first.save();
        Instance second(lib);
        CHECK(second.load(resaved), "%s: re-saved state rejected", name);
        const double reloaded = maxDiff(second.render(in), want);
        CHECK(reloaded <= 1.0e-6, "%s: re-saved legacy state plays %.3g away from the dial API", name, reloaded);
        std::printf("  %-38s loaded %.2g, re-saved and reloaded %.2g\n", name, loaded, reloaded);
    }
}

void testLegacyAutomationTakesTheBandOver(const Library& lib)
{
    const std::vector<float> in = noise();
    LegacySettings s;
    s.lfGain = 6.f; s.lmGain = -5.f; s.hmGain = 7.f; s.hfGain = 4.f;
    s.lfFreq = 60.f; s.lmFreq = 1450.f; s.hmFreq = 5250.f; s.hfFreq = 3500.f;
    const CoreBands hzState { 300.f, 500.f, 2000.f, 12000.f, true };

    Instance plugin(lib);
    const bool hasHzParams = plugin.bySymbol.count("lf_hz") != 0;
    CHECK(hasHzParams, "no Hz parameter for LF");
    if (!hasHzParams)
        return;
    Events setup;
    setup.queue(plugin.idOf("lf_gain"), s.lfGain);
    setup.queue(plugin.idOf("lm_gain"), s.lmGain);
    setup.queue(plugin.idOf("hm_gain"), s.hmGain);
    setup.queue(plugin.idOf("hf_gain"), s.hfGain);
    setup.queue(plugin.idOf("lf_hz"), hzState.lf);
    setup.queue(plugin.idOf("lm_hz"), hzState.lm);
    setup.queue(plugin.idOf("hm_hz"), hzState.hm);
    setup.queue(plugin.idOf("hf_hz"), hzState.hf);
    CoreRunner core(s, hzState);
    const double before = maxDiff(plugin.render(in, &setup), core.render(in));
    CHECK(before <= 1.0e-6, "Hz state plays %.3g away from the Hz API", before);

    // A lane recorded against the shipped parameter replays its dial values.
    Events lane;
    lane.queue(plugin.idOf("lf_freq"), s.lfFreq);
    lane.queue(plugin.idOf("lm_freq"), s.lmFreq);
    lane.queue(plugin.idOf("hm_freq"), s.hmFreq);
    lane.queue(plugin.idOf("hf_freq"), s.hfFreq);
    core.apply(s, dialsOf(s));
    const double during = maxDiff(plugin.render(in, &lane), core.render(in));
    CHECK(during <= 1.0e-6, "legacy-dial automation plays %.3g away from the dial API", during);
    std::printf("  Hz state %.2g from the Hz API; legacy lane over it %.2g from the dial API\n", before, during);

    // A Hz write takes the band back.
    Events back;
    back.queue(plugin.idOf("hm_hz"), 7000.f);
    core.apply(s, { s.lfFreq, s.lmFreq, s.hmFreq, s.hfFreq, false });
    const std::vector<float> got = plugin.render(in, &back);
    const std::vector<float> dialOnly = core.render(in);
    CHECK(maxDiff(got, dialOnly) > 1.0e-3, "an HM Hz write after the legacy lane did not move the band");
}

// Level of a sine through the plugin, relative to its input.
double gainAt(Instance& plugin, double hz)
{
    std::vector<float> in(2 * kBlock * kBlocks);
    for (size_t i = 0; i < in.size() / 2; ++i)
        in[2 * i] = in[2 * i + 1] = 0.05f * (float)std::sin(2.0 * M_PI * hz * (double)i / kRate);
    const std::vector<float> out = plugin.render(in);
    // Skip the first half (settling, oversampler latency), then correlate.
    const size_t n = in.size() / 2, start = n / 2;
    double re = 0.0, im = 0.0;
    for (size_t i = start; i < n; ++i)
    {
        const double w = 2.0 * M_PI * hz * (double)i / kRate;
        re += out[2 * i] * std::cos(w);
        im += out[2 * i] * std::sin(w);
    }
    return 2.0 * std::sqrt(re * re + im * im) / (double)(n - start) / 0.05;
}

void testHmReachesSevenKilohertz(const Library& lib)
{
    const std::vector<float> in = noise();
    for (int black = 0; black < 2; ++black)
    {
        Instance plugin(lib);
        const auto named = plugin.byName.find("HM Frequency");
        CHECK(named != plugin.byName.end(), "no parameter named HM Frequency");
        if (named == plugin.byName.end())
            return;
        clap_param_info_t info = {};
        for (uint32_t i = 0, n = plugin.params->count(plugin.plugin); i < n; ++i)
            if (plugin.params->get_info(plugin.plugin, i, &info) && info.id == named->second)
                break;
        CHECK(info.max_value == 7000.0, "HM Frequency tops out at %.0f", info.max_value);

        Events ev;
        ev.queue(plugin.idOf("eq_type"), black);
        ev.queue(plugin.idOf("hm_gain"), FourKEQDSP::kEqReferenceGainDb);
        ev.queue(named->second, info.max_value);
        LegacySettings s;
        s.eqType = (float)black;
        s.hmGain = FourKEQDSP::kEqReferenceGainDb;
        CoreRunner core(s, { 200.f, 1000.f, 7000.f, 8000.f, true });
        const double vsHz = maxDiff(plugin.render(in, &ev), core.render(in));
        CHECK(vsHz <= 1.0e-6, "%s HM Frequency at its top plays %.3g away from the Hz API at 7 kHz",
              black ? "Black" : "Brown", vsHz);

        // Measured: the HM peak, by golden-section search on the level.
        double lo = 5000.0, hi = 9500.0;
        const double phi = 0.5 * (std::sqrt(5.0) - 1.0);
        double x1 = hi - phi * (hi - lo), x2 = lo + phi * (hi - lo);
        double g1 = gainAt(plugin, x1), g2 = gainAt(plugin, x2);
        for (int i = 0; i < 22; ++i)
        {
            if (g1 > g2) { hi = x2; x2 = x1; g2 = g1; x1 = hi - phi * (hi - lo); g1 = gainAt(plugin, x1); }
            else         { lo = x1; x1 = x2; g1 = g2; x2 = lo + phi * (hi - lo); g2 = gainAt(plugin, x2); }
        }
        const double peak = 0.5 * (lo + hi);
        CHECK(std::abs(peak / 7000.0 - 1.0) < 0.01, "%s HM at the top of its knob peaks at %.0f Hz",
              black ? "Black" : "Brown", peak);
        std::printf("  %s HM Frequency %.0f: %.2g from the Hz API, measured peak %.0f Hz\n",
                    black ? "Black" : "Brown", info.max_value, vsHz, peak);
    }
}

void testFactoryProgramsPlayTheirStatedHz(const Library& lib)
{
    const std::vector<float> in = noise();
    for (int i = 0; i < kNumFactoryPresets; ++i)
    {
        const FourKEQPreset& p = kFactoryPresets[i];
        const bool black = p.eqType > 0.5f;
        LegacySettings s;
        s.eqType = p.eqType;
        s.lfGain = p.lfGain; s.lfBell = p.lfBell;
        s.lmGain = p.lmGain; s.lmQ = p.lmQ;
        s.hmGain = p.hmGain; s.hmQ = p.hmQ;
        s.hfGain = p.hfGain; s.hfBell = p.hfBell;
        s.hpfFreq = FourKEQDSP::controlForCalibratedFilterFrequency(p.hpfFreq, true, black);
        s.lpfFreq = FourKEQDSP::controlForCalibratedFilterFrequency(p.lpfFreq, false, black);
        s.hpfEnabled = p.hpfFreq > 16.5f ? 1.f : 0.f;
        s.lpfEnabled = p.lpfFreq < 15200.5f ? 1.f : 0.f;
        s.inputGain = p.inputGain; s.outputGain = p.outputGain;
        const std::vector<float> want = CoreRunner(s, { p.lfFreq, p.lmFreq, p.hmFreq, p.hfFreq, true }).render(in);

        Instance plugin(lib);
        std::string program = "__daf_program__";
        program += '\0'; program += std::to_string(i); program += '\0'; program += '\xfe'; program += '\0';
        CHECK(plugin.load(program), "program %d rejected", i);
        const double diff = maxDiff(plugin.render(in), want);
        CHECK(diff <= 1.0e-6, "program \"%s\" plays %.3g away from its stated Hz", p.name, diff);
        for (const char* sym : { "lf_hz", "lm_hz", "hm_hz", "hf_hz" })
        {
            double v = 0.0;
            plugin.params->get_value(plugin.plugin, plugin.idOf(sym), &v);
            const float stated = sym[0] == 'l' ? (sym[1] == 'f' ? p.lfFreq : p.lmFreq)
                                               : (sym[1] == 'm' ? p.hmFreq : p.hfFreq);
            CHECK(std::abs(v - stated) < 1.0e-3, "program \"%s\" %s reads %.1f, states %.1f", p.name, sym, v, stated);
        }
        std::printf("  %-24s %.2g from LF %.0f / LM %.0f / HM %.0f / HF %.0f Hz\n",
                    p.name, diff, p.lfFreq, p.lmFreq, p.hmFreq, p.hfFreq);
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

    std::printf("[1] pre-#288 states keep their sound\n");
    testLegacySessionsKeepTheirSound(lib);
    std::printf("[2] automation of a legacy dial parameter\n");
    testLegacyAutomationTakesTheBandOver(lib);
    std::printf("[3] HM Frequency reaches 7 kHz\n");
    testHmReachesSevenKilohertz(lib);
    std::printf("[4] factory programs play their stated Hz\n");
    testFactoryProgramsPlayTheirStatedHz(lib);

    entry->deinit();
    std::printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
