// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Hosts the built Multi-Comp 2 LV2 through lilv the way an LV2 host does, and
// holds it to the core DSP driven the way MultiCompPlugin drives it. VST3, CLAP
// and pluginval never reach this path: DAF's LV2 wrapper starts every control
// port at its TTL default and passes on only the ports that changed since the
// last run, Bypass is the inverted lv2:enabled port, the sidechain is a second
// pair of audio ports, latency and gain reduction are output ports, presets are
// exported to presets.ttl, and sessions go through the LV2 state interface.
//
//   [1] every mode at the TTL defaults plays the core at the plugin's defaults,
//       and the latency and gain-reduction ports read what the core reports;
//   [2] every mode with every control port set, then half of them moved
//       between blocks as automation, plays the core given the same values at
//       the same block boundaries;
//   [3] Bypass through lv2:enabled, and the external sidechain on its own ports;
//   [4] every exported factory preset, applied as Ardour applies one, plays the
//       program it was exported from, in a fresh instance and after other
//       settings;
//   [5] a program selected through the programs extension plays, and keeps its
//       sound once its ports are saved and reloaded;
//   [6] LV2 state saved from one instance and restored into another, fresh or
//       already set otherwise, plays the saved settings.
//
// The comparison is bit for bit: the plugin and this test compile the same DSP
// with the same flags (no FMA contraction, see CMakeLists.txt), and the TTL
// writes every value with enough digits to parse back to the same float.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

#include <lilv/lilv.h>
#include <lv2/atom/atom.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/options/options.h>
#include <lv2/parameters/parameters.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>

#include "DuskDenormals.hpp"
#include "MultiCompDSP.hpp"
#include "MultiCompParams.hpp"
#include "MultiCompProgramPresets.hpp"

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

constexpr const char* kUri = "https://dusk-audio.github.io/plugins/multi-comp-2";
constexpr double kRate = 48000.0;
constexpr uint32_t kMaxBlock = 512;
// Hosts do not always run full blocks; DAF must pass on port changes the same
// way whatever the split.
constexpr std::array<uint32_t, 10> kBlockSizes{{512, 64, 333, 512, 1, 200, 512, 127, 512, 256}};
constexpr int kBlocks = 24;

constexpr int kModeIndex = static_cast<int>(multicompp::ParamId::Mode);
constexpr int kBypassIndex = static_cast<int>(multicompp::ParamId::Bypass);
constexpr int kExternalSidechainIndex = static_cast<int>(multicompp::ParamId::ExternalSidechain);

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
LV2_URID_Map gMap { nullptr, mapUri };

// Host values for every control parameter, by parameter index.
using Settings = std::map<int, float>;

std::string symbolOf(int index)
{
    if (index == kBypassIndex)
        return "lv2_enabled";
    return multicompp::resolveParameter(index,
        [](const multicompp::Param& d) { return std::string(d.id); },
        [](const multicompp::BandParam& d, int band) { return std::string(d.id) + "_" + std::to_string(band); });
}

float defaultOf(int index)
{
    return multicompp::resolveParameter(index,
        [](const multicompp::Param& d) { return multicompp::hostDefault(d); },
        [](const multicompp::BandParam& d, int) { return multicompp::hostDefault(d); });
}

Settings defaults()
{
    Settings s;
    for (int i = 0; i < multicompp::kTotalParamCount; i = multicompp::nextControlParameter(i))
        s[i] = defaultOf(i);
    return s;
}

// A host value for every parameter, in range and on the integer grid, Bypass off.
Settings randomSettings(uint32_t seed, int mode)
{
    std::mt19937 rng(seed);
    Settings s;
    for (int i = 0; i < multicompp::kTotalParamCount; i = multicompp::nextControlParameter(i))
        s[i] = multicompp::resolveParameter(i,
            [&](const auto& d) {
                const float lo = multicompp::hostMin(d), hi = multicompp::hostMax(d);
                const float v = std::uniform_real_distribution<float>(lo, hi)(rng);
                return d.integer ? std::round(v) : v;
            },
            [&](const auto& d, int) {
                const float lo = multicompp::hostMin(d), hi = multicompp::hostMax(d);
                const float v = std::uniform_real_distribution<float>(lo, hi)(rng);
                return d.integer ? std::round(v) : v;
            });
    s[kModeIndex] = static_cast<float>(mode);
    s[kBypassIndex] = 0.0f;
    return s;
}

// Factory preset i as the plugin's own program defines it, on top of the defaults.
Settings presetSettings(uint32_t index, Settings base = defaults())
{
    multicompp::applyFactoryPresetToHostParameters(index,
        [&](int parameterIndex, float hostValue) { base[parameterIndex] = hostValue; });
    return base;
}

struct Signal { std::vector<float> l, r, scL, scR; };

// Bursts a compressor has to move on, and a sidechain that is loud where the
// main input is quiet, so the external sidechain audibly takes over.
Signal makeSignal()
{
    const int frames = static_cast<int>(kMaxBlock) * kBlocks;
    Signal s;
    s.l.resize(frames); s.r.resize(frames); s.scL.resize(frames); s.scR.resize(frames);
    std::mt19937 rng(7);
    std::normal_distribution<float> noise(0.0f, 1.0f);
    for (int i = 0; i < frames; ++i)
    {
        const double t = i / kRate;
        const float burst = std::fmod(t, 0.08) < 0.04 ? 0.7f : 0.08f;
        const float music = static_cast<float>(0.6 * std::sin(2.0 * M_PI * 110.0 * t)
            + 0.25 * std::sin(2.0 * M_PI * 1730.0 * t));
        s.l[i] = burst * (music + 0.05f * noise(rng));
        s.r[i] = burst * (0.8f * music + 0.05f * noise(rng));
        const float scBurst = std::fmod(t + 0.04, 0.08) < 0.04 ? 0.9f : 0.0f;
        s.scL[i] = scBurst * static_cast<float>(std::sin(2.0 * M_PI * 60.0 * t));
        s.scR[i] = s.scL[i];
    }
    return s;
}

struct Render
{
    std::vector<float> l, r;
    float latency = 0.0f, gainReduction = 0.0f;
};

double maxDiff(const Render& a, const Render& b)
{
    double worst = 0.0;
    for (size_t i = 0; i < a.l.size() && i < b.l.size(); ++i)
    {
        if (!std::isfinite(a.l[i]) || !std::isfinite(a.r[i]))
            return INFINITY;
        worst = std::max({ worst, std::fabs((double)a.l[i] - b.l[i]), std::fabs((double)a.r[i] - b.r[i]) });
    }
    return a.l.size() == b.l.size() ? worst : INFINITY;
}

// The core, driven as MultiCompPlugin drives it: constructed at the defaults,
// prepared and handed every value on activate(), then given each new host value
// through snapHostValue/hostToPlain, and run with the external sidechain only
// while that parameter is on.
//
// A host that restores a preset or a session into an instance it has not yet
// activated reaches setState, and so the core, before prepare(). prepare()
// snaps the mix and makeup ramps to the values it finds, where values arriving
// after it ramp in from the defaults, so the first blocks differ (by up to a
// few dB for ~30 ms, longer through Bus's slow release). `beforeActivation`
// replays that order.
class CoreRunner
{
public:
    explicit CoreRunner(const Settings& beforeActivation = {})
    {
        values = defaults();
        for (const auto& [index, value] : beforeActivation)
        {
            values[index] = value;
            push(index, value);
        }
        dsp.prepare(kRate, static_cast<int>(kMaxBlock));
        for (const auto& [index, value] : values)
            push(index, value);
    }

    void apply(const Settings& settings)
    {
        for (const auto& [index, value] : settings)
            if (values[index] != value)
            {
                values[index] = value;
                push(index, value);
            }
    }

    // `from` and `blocks` select the slice of the block schedule, so a render
    // can be split around a change as the LV2 one is.
    Render render(const Signal& in, int from = 0, int blocks = kBlocks)
    {
        Render out;
        size_t offset = offsetOf(from);
        for (int b = from; b < from + blocks; ++b)
        {
            const uint32_t n = kBlockSizes[static_cast<size_t>(b) % kBlockSizes.size()];
            std::array<float, kMaxBlock> outL{}, outR{};
            const float* audio[2] = { in.l.data() + offset, in.r.data() + offset };
            const float* sidechain[2] = { in.scL.data() + offset, in.scR.data() + offset };
            float* outputs[2] = { outL.data(), outR.data() };
            {
                duskaudio::ScopedFlushDenormals guard;
                if (values[kExternalSidechainIndex] > 0.5f)
                    dsp.processBlockExternal(audio, sidechain, outputs, 2, static_cast<int>(n));
                else
                    dsp.processBlock(audio, outputs, 2, static_cast<int>(n));
            }
            out.l.insert(out.l.end(), outL.begin(), outL.begin() + n);
            out.r.insert(out.r.end(), outR.begin(), outR.begin() + n);
            offset += n;
        }
        out.latency = static_cast<float>(std::max(0, dsp.getLatencySamples()));
        out.gainReduction = dsp.getGainReduction();
        return out;
    }

    static size_t offsetOf(int block)
    {
        size_t offset = 0;
        for (int b = 0; b < block; ++b)
            offset += kBlockSizes[static_cast<size_t>(b) % kBlockSizes.size()];
        return offset;
    }

private:
    void push(int index, float hostValue)
    {
        multicompp::resolveParameter(index,
            [&](const multicompp::Param& d) {
                dsp.setParameter(d.core, multicompp::hostToPlain(d, multicompp::snapHostValue(d, hostValue)));
            },
            [&](const multicompp::BandParam& d, int band) {
                dsp.setMultibandParameter(band, d.core,
                                          multicompp::hostToPlain(d, multicompp::snapHostValue(d, hostValue)));
            });
    }

    duskaudio::MultiCompDSP dsp;
    Settings values;
};

class Lv2Instance
{
public:
    Lv2Instance(LilvWorld* world, const LilvPlugin* plugin)
        : plugin(plugin)
    {
        const uint32_t n = lilv_plugin_get_num_ports(plugin);
        controls.resize(n, 0.0f);
        std::vector<float> portDefaults(n, 0.0f);
        lilv_plugin_get_port_ranges_float(plugin, nullptr, nullptr, portDefaults.data());
        LilvNode* audio = lilv_new_uri(world, LILV_URI_AUDIO_PORT);
        LilvNode* atom = lilv_new_uri(world, LV2_ATOM__AtomPort);
        LilvNode* control = lilv_new_uri(world, LILV_URI_CONTROL_PORT);
        LilvNode* input = lilv_new_uri(world, LILV_URI_INPUT_PORT);
        for (uint32_t i = 0; i < n; ++i)
        {
            const LilvPort* port = lilv_plugin_get_port_by_index(plugin, i);
            const std::string symbol = lilv_node_as_string(lilv_port_get_symbol(plugin, port));
            const bool isInput = lilv_port_is_a(plugin, port, input);
            if (lilv_port_is_a(plugin, port, audio))
                (isInput ? audioIn : audioOut).push_back(i);
            else if (lilv_port_is_a(plugin, port, atom))
                (isInput ? atomIn : atomOut) = i;
            else if (lilv_port_is_a(plugin, port, control))
            {
                controls[i] = std::isnan(portDefaults[i]) ? 0.0f : portDefaults[i];
                bySymbol[symbol] = i;
            }
        }
        lilv_node_free(audio);
        lilv_node_free(atom);
        lilv_node_free(control);
        lilv_node_free(input);
        if (audioIn.size() != 4 || audioOut.size() != 2 || atomIn == UINT32_MAX || atomOut == UINT32_MAX)
        {
            std::fprintf(stderr, "FAIL: unexpected port layout (%zu audio in, %zu out)\n", audioIn.size(), audioOut.size());
            std::exit(1);
        }

        mapFeature = { LV2_URID__map, &gMap };
        options[0] = { LV2_OPTIONS_INSTANCE, 0, mapUri(nullptr, LV2_BUF_SIZE__maxBlockLength),
                       sizeof(int32_t), mapUri(nullptr, LV2_ATOM__Int), &maxBlockValue };
        options[1] = { LV2_OPTIONS_INSTANCE, 0, mapUri(nullptr, LV2_BUF_SIZE__nominalBlockLength),
                       sizeof(int32_t), mapUri(nullptr, LV2_ATOM__Int), &maxBlockValue };
        options[2] = { LV2_OPTIONS_INSTANCE, 0, mapUri(nullptr, LV2_PARAMETERS__sampleRate),
                       sizeof(float), mapUri(nullptr, LV2_ATOM__Float), &rateValue };
        options[3] = { LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, nullptr };
        optionsFeature = { LV2_OPTIONS__options, options };
        boundedFeature = { LV2_BUF_SIZE__boundedBlockLength, nullptr };
        schedule = { this, &Lv2Instance::scheduleWork };
        scheduleFeature = { LV2_WORKER__schedule, &schedule };
        features = { &mapFeature, &optionsFeature, &boundedFeature, &scheduleFeature, nullptr };

        instance = lilv_plugin_instantiate(plugin, kRate, features.data());
        if (instance == nullptr)
        {
            std::fprintf(stderr, "FAIL: could not instantiate %s\n", kUri);
            std::exit(1);
        }
        worker = (const LV2_Worker_Interface*)lilv_instance_get_extension_data(instance, LV2_WORKER__interface);
        for (auto& buffer : audioBuffers)
            buffer.assign(kMaxBlock, 0.0f);
        for (const auto& [symbol, index] : bySymbol)
            lilv_instance_connect_port(instance, index, &controls[index]);
        for (size_t i = 0; i < 4; ++i)
            lilv_instance_connect_port(instance, audioIn[i], audioBuffers[i].data());
        lilv_instance_connect_port(instance, audioOut[0], audioBuffers[4].data());
        lilv_instance_connect_port(instance, audioOut[1], audioBuffers[5].data());
        lilv_instance_connect_port(instance, atomIn, eventsIn.data());
        lilv_instance_connect_port(instance, atomOut, eventsOut.data());
    }
    ~Lv2Instance()
    {
        if (active)
            lilv_instance_deactivate(instance);
        lilv_instance_free(instance);
    }
    Lv2Instance(const Lv2Instance&) = delete;
    Lv2Instance& operator=(const Lv2Instance&) = delete;

    LilvInstance* handle() const { return instance; }
    const LV2_Feature* const* featureList() const { return features.data(); }
    float get(const std::string& symbol) const { return controls[bySymbol.at(symbol)]; }

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

    // As a host writes a session's values: Bypass arrives inverted on lv2:enabled.
    void apply(const Settings& settings)
    {
        for (const auto& [index, value] : settings)
            set(symbolOf(index), index == kBypassIndex ? 1.0f - value : value);
    }

    // The input control ports, as a host saves them.
    std::map<std::string, float> inputPorts() const
    {
        std::map<std::string, float> out;
        for (int i = 0; i < multicompp::kTotalParamCount; i = multicompp::nextControlParameter(i))
            out[symbolOf(i)] = get(symbolOf(i));
        return out;
    }

    const void* portValue(const char* symbol, uint32_t* size, uint32_t* type) const
    {
        const auto it = bySymbol.find(symbol);
        if (it == bySymbol.end())
            return nullptr;
        *size = sizeof(float);
        *type = mapUri(nullptr, LV2_ATOM__Float);
        return &controls[it->second];
    }

    void selectProgram(uint32_t index)
    {
        const auto* programs = (const ProgramsInterface*)lilv_instance_get_extension_data(instance, kProgramsInterface);
        if (programs == nullptr || programs->select_program == nullptr)
        {
            std::fprintf(stderr, "FAIL: no programs interface\n");
            std::exit(1);
        }
        programs->select_program(lilv_instance_get_handle(instance), index / 128, index % 128);
    }

    Render render(const Signal& in, int from = 0, int blocks = kBlocks)
    {
        if (!active)
        {
            lilv_instance_activate(instance);
            active = true;
        }
        Render out;
        size_t offset = CoreRunner::offsetOf(from);
        for (int b = from; b < from + blocks; ++b)
        {
            const uint32_t n = kBlockSizes[static_cast<size_t>(b) % kBlockSizes.size()];
            std::memcpy(audioBuffers[0].data(), in.l.data() + offset, n * sizeof(float));
            std::memcpy(audioBuffers[1].data(), in.r.data() + offset, n * sizeof(float));
            std::memcpy(audioBuffers[2].data(), in.scL.data() + offset, n * sizeof(float));
            std::memcpy(audioBuffers[3].data(), in.scR.data() + offset, n * sizeof(float));
            prepareEvents();
            lilv_instance_run(instance, n);
            runWorker();
            out.l.insert(out.l.end(), audioBuffers[4].begin(), audioBuffers[4].begin() + n);
            out.r.insert(out.r.end(), audioBuffers[5].begin(), audioBuffers[5].begin() + n);
            offset += n;
        }
        out.latency = get("lv2_latency");
        out.gainReduction = get("gr_meter");
        return out;
    }

private:
    // An empty input sequence, and an output sequence with room to write in.
    void prepareEvents()
    {
        auto* in = reinterpret_cast<LV2_Atom_Sequence*>(eventsIn.data());
        in->atom.type = mapUri(nullptr, LV2_ATOM__Sequence);
        in->atom.size = sizeof(LV2_Atom_Sequence_Body);
        in->body.unit = 0;
        in->body.pad = 0;
        auto* out = reinterpret_cast<LV2_Atom_Sequence*>(eventsOut.data());
        out->atom.type = mapUri(nullptr, LV2_ATOM__Chunk);
        out->atom.size = static_cast<uint32_t>(eventsOut.size() * sizeof(uint64_t) - sizeof(LV2_Atom));
    }

    // Ardour runs the worker on its own thread; here it runs after each block.
    static LV2_Worker_Status scheduleWork(LV2_Worker_Schedule_Handle handle, uint32_t size, const void* data)
    {
        auto* self = static_cast<Lv2Instance*>(handle);
        const auto* bytes = static_cast<const uint8_t*>(data);
        self->pendingWork.emplace_back(bytes, bytes + size);
        return LV2_WORKER_SUCCESS;
    }
    static LV2_Worker_Status respond(LV2_Worker_Respond_Handle handle, uint32_t size, const void* data)
    {
        auto* self = static_cast<Lv2Instance*>(handle);
        const auto* bytes = static_cast<const uint8_t*>(data);
        self->responses.emplace_back(bytes, bytes + size);
        return LV2_WORKER_SUCCESS;
    }
    void runWorker()
    {
        if (worker == nullptr)
            return;
        LV2_Handle plugin = lilv_instance_get_handle(instance);
        while (!pendingWork.empty())
        {
            const std::vector<uint8_t> work = std::move(pendingWork.front());
            pendingWork.erase(pendingWork.begin());
            if (worker->work != nullptr)
                worker->work(plugin, respond, this, static_cast<uint32_t>(work.size()), work.data());
        }
        for (const auto& response : responses)
            if (worker->work_response != nullptr)
                worker->work_response(plugin, static_cast<uint32_t>(response.size()), response.data());
        responses.clear();
        if (worker->end_run != nullptr)
            worker->end_run(plugin);
    }

    const LilvPlugin* plugin;
    LilvInstance* instance = nullptr;
    const LV2_Worker_Interface* worker = nullptr;
    bool active = false;
    std::vector<float> controls;
    std::array<std::vector<float>, 6> audioBuffers;
    std::array<uint64_t, 1024> eventsIn{}, eventsOut{};
    std::vector<uint32_t> audioIn, audioOut;
    uint32_t atomIn = UINT32_MAX, atomOut = UINT32_MAX;
    std::map<std::string, uint32_t> bySymbol;
    std::vector<std::vector<uint8_t>> pendingWork, responses;
    LV2_Feature mapFeature {}, optionsFeature {}, boundedFeature {}, scheduleFeature {};
    LV2_Worker_Schedule schedule {};
    std::array<const LV2_Feature*, 5> features {};
    LV2_Options_Option options[4] {};
    int32_t maxBlockValue = static_cast<int32_t>(kMaxBlock);
    float rateValue = static_cast<float>(kRate);
};

float portFloat(const char* symbol, const void* value, uint32_t type)
{
    if (type == mapUri(nullptr, LV2_ATOM__Float)) return *static_cast<const float*>(value);
    if (type == mapUri(nullptr, LV2_ATOM__Double)) return (float)*static_cast<const double*>(value);
    if (type == mapUri(nullptr, LV2_ATOM__Int) || type == mapUri(nullptr, LV2_ATOM__Bool))
        return (float)*static_cast<const int32_t*>(value);
    if (type == mapUri(nullptr, LV2_ATOM__Long)) return (float)*static_cast<const int64_t*>(value);
    std::fprintf(stderr, "FAIL: port %s has an unknown value type\n", symbol);
    std::exit(1);
}

void setPortValue(const char* symbol, void* user, const void* value, uint32_t, uint32_t type)
{
    static_cast<Lv2Instance*>(user)->set(symbol, portFloat(symbol, value, type));
}

void collectPortValue(const char* symbol, void* user, const void* value, uint32_t, uint32_t type)
{
    (*static_cast<std::map<std::string, float>*>(user))[symbol] = portFloat(symbol, value, type);
}

const void* getPortValue(const char* symbol, void* user, uint32_t* size, uint32_t* type)
{
    return static_cast<const Lv2Instance*>(user)->portValue(symbol, size, type);
}

const char* modeName(int mode)
{
    return multicompp::kModes[mode];
}

void checkRender(const char* what, const Render& lv2, const Render& core)
{
    const double diff = maxDiff(lv2, core);
    CHECK(diff == 0.0, "%s: LV2 plays %.3g away from the core", what, diff);
    CHECK(lv2.latency == core.latency, "%s: latency port %g, core %g", what, lv2.latency, core.latency);
    CHECK(lv2.gainReduction == core.gainReduction, "%s: GR port %g, core %g", what, lv2.gainReduction, core.gainReduction);
}

void testDefaults(LilvWorld* world, const LilvPlugin* plugin, const Signal& in)
{
    // The TTL defaults are the plugin's own: an instance nobody touched plays
    // what the VST3 and CLAP play.
    {
        Lv2Instance lv2(world, plugin);
        int mismatched = 0;
        for (const auto& [index, value] : defaults())
        {
            const float port = lv2.get(symbolOf(index));
            const float expected = index == kBypassIndex ? 1.0f - value : value;
            if (port != expected)
            {
                ++mismatched;
                std::fprintf(stderr, "  %s: TTL default %.9g, plugin default %.9g\n", symbolOf(index).c_str(), port, expected);
            }
        }
        CHECK(mismatched == 0, "%d control ports have a TTL default other than the plugin's", mismatched);
    }
    for (int mode = 0; mode < 8; ++mode)
    {
        Lv2Instance lv2(world, plugin);
        Settings s = defaults();
        s[kModeIndex] = static_cast<float>(mode);
        lv2.apply(s);
        CoreRunner core;
        core.apply(s);
        const Render a = lv2.render(in), b = core.render(in);
        char what[64];
        std::snprintf(what, sizeof(what), "%s at defaults", modeName(mode));
        checkRender(what, a, b);
        std::printf("  %-28s %.2g, latency %g, GR %.2f dB\n", what, maxDiff(a, b), a.latency, a.gainReduction);
    }
}

void testEveryPortAndAutomation(LilvWorld* world, const LilvPlugin* plugin, const Signal& in)
{
    for (int mode = 0; mode < 8; ++mode)
    {
        const Settings first = randomSettings(100u + static_cast<uint32_t>(mode), mode);
        // Half the parameters move at a third and again at two thirds of the
        // render, as automation: DAF passes on only the ports that changed.
        Settings second = first, third;
        const Settings moved = randomSettings(200u + static_cast<uint32_t>(mode), mode);
        const Settings movedAgain = randomSettings(300u + static_cast<uint32_t>(mode), mode);
        int k = 0;
        for (const auto& [index, value] : moved)
            if (index != kModeIndex && index != kBypassIndex && (k++ % 2) == 0)
                second[index] = value;
        third = second;
        k = 0;
        for (const auto& [index, value] : movedAgain)
            if (index != kModeIndex && index != kBypassIndex && (k++ % 2) == 1)
                third[index] = value;

        Lv2Instance lv2(world, plugin);
        CoreRunner core;
        Render a, b;
        const int segment = kBlocks / 3;
        const Settings* stages[3] = { &first, &second, &third };
        for (int stage = 0; stage < 3; ++stage)
        {
            lv2.apply(*stages[stage]);
            core.apply(*stages[stage]);
            const Render ra = lv2.render(in, stage * segment, segment), rb = core.render(in, stage * segment, segment);
            a.l.insert(a.l.end(), ra.l.begin(), ra.l.end()); a.r.insert(a.r.end(), ra.r.begin(), ra.r.end());
            b.l.insert(b.l.end(), rb.l.begin(), rb.l.end()); b.r.insert(b.r.end(), rb.r.begin(), rb.r.end());
            a.latency = ra.latency; a.gainReduction = ra.gainReduction;
            b.latency = rb.latency; b.gainReduction = rb.gainReduction;
        }
        char what[64];
        std::snprintf(what, sizeof(what), "%s, every port, automated", modeName(mode));
        checkRender(what, a, b);
        std::printf("  %-34s %.2g, latency %g\n", what, maxDiff(a, b), a.latency);
    }
}

void testBypassAndSidechain(LilvWorld* world, const LilvPlugin* plugin, const Signal& in)
{
    // VCA, compressing hard, so the detector source shows in the output.
    Settings s = defaults();
    s[kModeIndex] = 2.0f;
    s[static_cast<int>(multicompp::ParamId::VcaThreshold)] = -40.0f;

    Settings bypassed = s;
    bypassed[kBypassIndex] = 1.0f;
    {
        Lv2Instance lv2(world, plugin);
        lv2.apply(bypassed);
        CoreRunner core;
        core.apply(bypassed);
        checkRender("Bypass through lv2:enabled", lv2.render(in), core.render(in));
    }

    Render internal, external;
    for (const bool useSidechain : { false, true })
    {
        Settings t = s;
        t[kExternalSidechainIndex] = useSidechain ? 1.0f : 0.0f;
        Lv2Instance lv2(world, plugin);
        lv2.apply(t);
        CoreRunner core;
        core.apply(t);
        const Render a = lv2.render(in), b = core.render(in);
        checkRender(useSidechain ? "external sidechain on" : "external sidechain off", a, b);
        (useSidechain ? external : internal) = a;
    }
    const double moved = maxDiff(internal, external);
    CHECK(moved > 1.0e-2, "the sidechain ports change nothing (%.3g)", moved);
    std::printf("  bypass and sidechain: external sidechain moves the output by %.3g\n", moved);
}

LilvState* exportedPreset(LilvWorld* world, uint32_t index)
{
    char uri[128];
    std::snprintf(uri, sizeof(uri), "%s#preset%03u", kUri, index + 1);
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

void testExportedPresets(LilvWorld* world, const LilvPlugin* plugin, const Signal& in)
{
    const int half = kBlocks / 2;
    for (uint32_t i = 0; i < multicompp::kFactoryPresets.size(); ++i)
    {
        const char* name = multicompp::kFactoryPresets[i].name;
        LilvState* state = exportedPreset(world, i);
        const Settings expected = presetSettings(i);

        // Every port value it carries is the program's. A host shows these,
        // and a later mode switch plays the ones the renders below, in the
        // preset's own mode, cannot hear.
        {
            std::map<std::string, float> ports;
            lilv_state_emit_port_values(state, collectPortValue, &ports);
            int wrong = 0;
            for (const auto& [index, value] : expected)
            {
                const auto it = ports.find(symbolOf(index));
                const float stated = index == kBypassIndex ? 1.0f - value : value;
                if (it == ports.end() || it->second != stated)
                {
                    ++wrong;
                    std::fprintf(stderr, "  preset \"%s\" %s: port %s, program %.9g\n", name, symbolOf(index).c_str(),
                                 it == ports.end() ? "missing" : std::to_string(it->second).c_str(), stated);
                }
            }
            CHECK(wrong == 0 && ports.size() == expected.size(),
                  "preset \"%s\": %d of %zu port values differ from the program (%zu ports exported)",
                  name, wrong, expected.size(), ports.size());
        }

        double fresh = 0.0, after = 0.0;
        {
            // As Ardour applies an LV2 preset: lilv restores its state and
            // writes each of its port values.
            Lv2Instance lv2(world, plugin);
            lilv_state_restore(state, lv2.handle(), setPortValue, &lv2, 0, lv2.featureList());
            CoreRunner core(expected);
            const Render a = lv2.render(in), b = core.render(in);
            char what[96];
            std::snprintf(what, sizeof(what), "preset \"%s\", fresh", name);
            checkRender(what, a, b);
            fresh = maxDiff(a, b);
        }
        {
            const Settings before = randomSettings(400u + i, static_cast<int>(i % 8));
            Lv2Instance lv2(world, plugin);
            CoreRunner core;
            lv2.apply(before);
            core.apply(before);
            const double priorDiff = maxDiff(lv2.render(in, 0, half), core.render(in, 0, half));
            lilv_state_restore(state, lv2.handle(), setPortValue, &lv2, 0, lv2.featureList());
            core.apply(expected);
            const Render a = lv2.render(in, half, half), b = core.render(in, half, half);
            char what[96];
            std::snprintf(what, sizeof(what), "preset \"%s\" after other settings", name);
            CHECK(priorDiff == 0.0, "%s: the settings before it already played %.3g away", what, priorDiff);
            checkRender(what, a, b);
            after = maxDiff(a, b);
        }
        std::printf("  %-26s fresh %.2g, after other settings %.2g\n", name, fresh, after);
        lilv_state_free(state);
    }
}

void testSelectedPrograms(LilvWorld* world, const LilvPlugin* plugin, const Signal& in)
{
    const int half = kBlocks / 2;
    for (uint32_t i = 0; i < multicompp::kFactoryPresets.size(); ++i)
    {
        const char* name = multicompp::kFactoryPresets[i].name;
        // A program sets only what it owns; everything else stays as it was.
        const Settings before = randomSettings(500u + i, static_cast<int>((i + 3) % 8));
        const Settings program = presetSettings(i, before);
        Lv2Instance lv2(world, plugin);
        CoreRunner core;
        lv2.apply(before);
        core.apply(before);
        lv2.render(in, 0, half);
        core.render(in, 0, half);
        lv2.selectProgram(i);
        core.apply(program);
        const Render a = lv2.render(in, half, half), b = core.render(in, half, half);
        char what[96];
        std::snprintf(what, sizeof(what), "program \"%s\" selected", name);
        checkRender(what, a, b);

        // Saved as a host saves ports, reloaded into a new instance.
        Lv2Instance reloaded(world, plugin);
        for (const auto& [symbol, value] : lv2.inputPorts())
            reloaded.set(symbol, value);
        CoreRunner fresh;
        fresh.apply(program);
        const Render c = reloaded.render(in), d = fresh.render(in);
        std::snprintf(what, sizeof(what), "program \"%s\" saved and reloaded", name);
        checkRender(what, c, d);
        std::printf("  %-26s selected %.2g, reloaded %.2g\n", name, maxDiff(a, b), maxDiff(c, d));
    }
}

void testStateRoundTrip(LilvWorld* world, const LilvPlugin* plugin, const Signal& in)
{
    const int half = kBlocks / 2;
    for (int mode = 0; mode < 8; ++mode)
    {
        const Settings saved = randomSettings(600u + static_cast<uint32_t>(mode), mode);
        Lv2Instance source(world, plugin);
        source.apply(saved);
        source.render(in, 0, 2);
        LilvState* state = lilv_state_new_from_instance(plugin, source.handle(), &gMap,
            nullptr, nullptr, nullptr, nullptr, getPortValue, &source, 0, source.featureList());
        CHECK(state != nullptr, "%s: no LV2 state saved", modeName(mode));
        if (state == nullptr)
            continue;

        double fresh = 0.0, after = 0.0;
        {
            Lv2Instance restored(world, plugin);
            lilv_state_restore(state, restored.handle(), setPortValue, &restored, 0, restored.featureList());
            CoreRunner core(saved);
            const Render a = restored.render(in), b = core.render(in);
            char what[64];
            std::snprintf(what, sizeof(what), "%s state, fresh instance", modeName(mode));
            checkRender(what, a, b);
            fresh = maxDiff(a, b);
        }
        {
            const Settings other = randomSettings(700u + static_cast<uint32_t>(mode), (mode + 4) % 8);
            Lv2Instance restored(world, plugin);
            CoreRunner core;
            restored.apply(other);
            core.apply(other);
            restored.render(in, 0, half);
            core.render(in, 0, half);
            lilv_state_restore(state, restored.handle(), setPortValue, &restored, 0, restored.featureList());
            core.apply(saved);
            const Render a = restored.render(in, half, half), b = core.render(in, half, half);
            char what[64];
            std::snprintf(what, sizeof(what), "%s state, over other settings", modeName(mode));
            checkRender(what, a, b);
            after = maxDiff(a, b);
        }
        std::printf("  %-12s restored fresh %.2g, over other settings %.2g\n", modeName(mode), fresh, after);
        lilv_state_free(state);
    }
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: %s <multi-comp-2.lv2 bundle directory>\n", argv[0]);
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

    const Signal in = makeSignal();
    std::printf("[1] every mode at the TTL defaults\n");
    testDefaults(world, plugin, in);
    std::printf("[2] every mode, every port, automated between blocks\n");
    testEveryPortAndAutomation(world, plugin, in);
    std::printf("[3] Bypass through lv2:enabled, external sidechain ports\n");
    testBypassAndSidechain(world, plugin, in);
    std::printf("[4] exported factory presets, applied as Ardour applies them\n");
    testExportedPresets(world, plugin, in);
    std::printf("[5] programs selected, saved and reloaded\n");
    testSelectedPrograms(world, plugin, in);
    std::printf("[6] LV2 state saved and restored\n");
    testStateRoundTrip(world, plugin, in);

    lilv_node_free(pluginUri);
    lilv_node_free(bundleUri);
    lilv_world_free(world);
    std::printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
