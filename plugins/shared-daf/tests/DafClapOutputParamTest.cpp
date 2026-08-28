// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// CLAP counterpart of DafVst3OutputParamTest, guarding dusk-audio/plugins#231
// and the CLAP half of #233.
//
// DAF's CLAP wrapper used to push a CLAP_EVENT_PARAM_VALUE, flagged
// CLAP_EVENT_IS_LIVE and carrying no CLAP_EVENT_DONT_RECORD, for every output
// parameter whose value had moved since the last block -- one per meter per
// block. CLAP_EVENT_IS_LIVE means "a user turning a physical knob", so the host
// was told hundreds of times a second that the user was riding the VU meter.
// dusk-audio/DAF#18 stopped that; this keeps it stopped.
//
// Same two-sided contract as the VST3 harness: no output parameter may reach
// the host's out_events, and the meters must still move -- read back through
// clap_plugin_params.get_value(), which clap/ext/params.h lets a host call at
// any time and is how a host is expected to display them now.
//
// Linux and macOS only (dlopen plus the bundle layouts below).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include <dlfcn.h>

// DAF vendors a trimmed CLAP SDK with no umbrella clap.h, so the pieces this
// harness needs are included individually.
#include "clap/entry.h"
#include "clap/plugin.h"
#include "clap/plugin-features.h"
#include "clap/ext/audio-ports.h"
#include "clap/ext/params.h"
#include "clap/factory/plugin-factory.h"

// --------------------------------------------------------------------------------------------------------------------
// minimal host

static const clap_host_t* gHost = nullptr;

static const void* CLAP_ABI hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
static void CLAP_ABI hostRequestRestart(const clap_host_t*) {}
static void CLAP_ABI hostRequestProcess(const clap_host_t*) {}
static void CLAP_ABI hostRequestCallback(const clap_host_t*) {}

static clap_host_t gHostImpl = {
    CLAP_VERSION_INIT, nullptr,
    "DafClapOutputParamTest", "Dusk Audio", "https://dusk-audio.github.io/", "1.0.0",
    hostGetExtension, hostRequestRestart, hostRequestProcess, hostRequestCallback
};

// ---- out_events collector -------------------------------------------------------------------------------------------

struct CollectedEvent {
    uint16_t type;
    uint32_t flags;
    clap_id paramId;
    double value;
};

struct OutEvents {
    clap_output_events_t iface;
    std::vector<CollectedEvent> events;

    OutEvents()
    {
        iface.ctx = this;
        iface.try_push = tryPush;
    }

    static bool CLAP_ABI tryPush(const clap_output_events_t* const list, const clap_event_header_t* const event)
    {
        OutEvents* const self = static_cast<OutEvents*>(list->ctx);
        CollectedEvent c = { event->type, event->flags, CLAP_INVALID_ID, 0.0 };
        if (event->type == CLAP_EVENT_PARAM_VALUE)
        {
            const clap_event_param_value_t* const pv = (const clap_event_param_value_t*) event;
            c.paramId = pv->param_id;
            c.value = pv->value;
        }
        else if (event->type == CLAP_EVENT_PARAM_GESTURE_BEGIN || event->type == CLAP_EVENT_PARAM_GESTURE_END)
        {
            const clap_event_param_gesture_t* const g = (const clap_event_param_gesture_t*) event;
            c.paramId = g->param_id;
        }
        self->events.push_back(c);
        return true;
    }
};

struct InEvents {
    clap_input_events_t iface;

    InEvents()
    {
        iface.ctx = this;
        iface.size = size;
        iface.get = get;
    }

    static uint32_t CLAP_ABI size(const clap_input_events_t*) { return 0; }
    static const clap_event_header_t* CLAP_ABI get(const clap_input_events_t*, uint32_t) { return nullptr; }
};

// --------------------------------------------------------------------------------------------------------------------

static std::string binaryInsideBundle(const std::string& path)
{
   #ifdef __APPLE__
    // A macOS .clap is a bundle; CMake hands over the binary itself.
    if (path.size() > 5 && path.compare(path.size() - 5, 5, ".clap") == 0)
    {
        std::string name(path);
        const size_t slash = name.find_last_of('/');
        if (slash != std::string::npos)
            name = name.substr(slash + 1);
        name = name.substr(0, name.size() - 5);
        const std::string inner = path + "/Contents/MacOS/" + name;
        if (FILE* const f = std::fopen(inner.c_str(), "rb")) { std::fclose(f); return inner; }
    }
   #endif
    return path;
}

static bool parsePositiveInt(const char* const text, int& out)
{
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || value < 1 || value > 1000000)
        return false;
    out = static_cast<int>(value);
    return true;
}

int main(const int argc, const char* const* const argv)
{
    bool silent = false;
    std::vector<const char*> positional;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--silent") == 0)
            silent = true;
        else
            positional.push_back(argv[i]);
    }

    if (positional.empty() || positional.size() > 3)
    {
        std::fprintf(stderr, "usage: %s <plugin.clap> [blocks] [frames] [--silent]\n", argv[0]);
        return 2;
    }

    const std::string path(binaryInsideBundle(positional[0]));
    int numBlocks = 100, numFrames = 256;
    if (positional.size() > 1 && ! parsePositiveInt(positional[1], numBlocks))
    {
        std::fprintf(stderr, "FAIL: block count %s is not a positive integer\n", positional[1]);
        return 2;
    }
    if (positional.size() > 2 && ! parsePositiveInt(positional[2], numFrames))
    {
        std::fprintf(stderr, "FAIL: frame count %s is not a positive integer\n", positional[2]);
        return 2;
    }

    // --silent characterises what a plugin does on an idle track. It is a
    // diagnostic mode, not the regression: the "meters still move" assertion
    // cannot hold for a plugin whose meters sit still on silence, so a silent
    // run is expected to report a failure, and the registered ctest always runs
    // with noise.

    void* const lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (lib == nullptr)
    {
        std::fprintf(stderr, "FAIL: dlopen(%s): %s\n", path.c_str(), dlerror());
        return 1;
    }

    const clap_plugin_entry_t* const entry = (const clap_plugin_entry_t*) dlsym(lib, "clap_entry");
    if (entry == nullptr)
    {
        std::fprintf(stderr, "FAIL: no clap_entry in %s\n", path.c_str());
        return 1;
    }
    if (! entry->init(path.c_str()))
    {
        std::fprintf(stderr, "FAIL: clap_entry->init\n");
        return 1;
    }

    const clap_plugin_factory_t* const factory =
        (const clap_plugin_factory_t*) entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    if (factory == nullptr || factory->get_plugin_count(factory) == 0)
    {
        std::fprintf(stderr, "FAIL: no plugin factory\n");
        return 1;
    }

    const clap_plugin_descriptor_t* const desc = factory->get_plugin_descriptor(factory, 0);
    if (desc == nullptr || desc->id == nullptr)
    {
        std::fprintf(stderr, "FAIL: factory returned no descriptor for plugin 0\n");
        return 1;
    }

    gHost = &gHostImpl;
    const clap_plugin_t* const plugin = factory->create_plugin(factory, gHost, desc->id);
    if (plugin == nullptr || ! plugin->init(plugin))
    {
        std::fprintf(stderr, "FAIL: could not create/init the plugin\n");
        return 1;
    }

    const clap_plugin_params_t* const paramsExt =
        (const clap_plugin_params_t*) plugin->get_extension(plugin, CLAP_EXT_PARAMS);
    if (paramsExt == nullptr)
    {
        std::fprintf(stderr, "FAIL: plugin has no params extension\n");
        return 1;
    }

    struct P { clap_id id; std::string name; std::string module; uint32_t flags; };
    std::vector<P> params;
    const uint32_t paramCount = paramsExt->count(plugin);
    for (uint32_t i = 0; i < paramCount; ++i)
    {
        clap_param_info_t info = {};
        if (! paramsExt->get_info(plugin, i, &info))
        {
            std::fprintf(stderr, "FAIL: get_info(%u); cannot classify every parameter\n", i);
            return 1;
        }
        params.push_back({ info.id, info.name, info.module, info.flags });
    }

    // DAF stamps CLAP_PARAM_IS_READONLY on output parameters and on the Reset
    // designation, which is a trigger and is deliberately still reported, so
    // READONLY alone would call a Reset a meter and fail a correct plugin the
    // day one is added. Unlike VST3 the wrapper sets no hidden flag to separate
    // them (getParameterInfo gives Reset exactly STEPPED|READONLY), but it does
    // stamp the module "daf_reset", which is the designation's own marker and
    // cannot collide with an ordinary parameter's module. Matching on that
    // rather than on flags keeps the rule exact for integer and boolean output
    // parameters, which carry STEPPED too.
    auto isOutput = [](const P& p) {
        return (p.flags & CLAP_PARAM_IS_READONLY) != 0 && p.module != "daf_reset";
    };

    std::vector<size_t> meters;
    for (size_t i = 0; i < params.size(); ++i)
        if (isOutput(params[i]))
            meters.push_back(i);

    std::printf("plugin      : %s\n", positional[0]);
    std::printf("parameters  : %u (%zu output/read-only)\n", paramCount, meters.size());
    for (const size_t i : meters)
        std::printf("  output    : id=%u \"%s\" flags=0x%x\n", params[i].id, params[i].name.c_str(), params[i].flags);

    if (meters.empty())
    {
        std::fprintf(stderr, "FAIL: plugin exposes no output parameters; this test would pass vacuously\n");
        return 1;
    }

    if (! plugin->activate(plugin, 48000.0, 1, static_cast<uint32_t>(numFrames)))
    {
        std::fprintf(stderr, "FAIL: activate\n");
        return 1;
    }
    if (! plugin->start_processing(plugin))
    {
        std::fprintf(stderr, "FAIL: start_processing\n");
        return 1;
    }

    // Buffers follow the ports the plugin actually declares, so a plugin with a
    // sidechain gets every port it will index into. Only the main input carries
    // signal; handing a plugin fewer ports than it declares is how a harness
    // reads past the end of audio_inputs.
    const clap_plugin_audio_ports_t* const portsExt =
        (const clap_plugin_audio_ports_t*) plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS);
    if (portsExt == nullptr)
    {
        std::fprintf(stderr, "FAIL: plugin has no audio-ports extension\n");
        return 1;
    }

    const uint32_t numInPorts = portsExt->count(plugin, true);
    const uint32_t numOutPorts = portsExt->count(plugin, false);
    if (numOutPorts == 0)
    {
        std::fprintf(stderr, "FAIL: plugin declares no audio output port\n");
        return 1;
    }

    std::vector<uint32_t> inCh(numInPorts), outCh(numOutPorts);
    for (uint32_t i = 0; i < numInPorts; ++i)
    {
        clap_audio_port_info_t info = {};
        if (! portsExt->get(plugin, i, true, &info)) { std::fprintf(stderr, "FAIL: audio input port %u\n", i); return 1; }
        inCh[i] = info.channel_count;
    }
    for (uint32_t i = 0; i < numOutPorts; ++i)
    {
        clap_audio_port_info_t info = {};
        if (! portsExt->get(plugin, i, false, &info)) { std::fprintf(stderr, "FAIL: audio output port %u\n", i); return 1; }
        outCh[i] = info.channel_count;
    }

    std::vector<std::vector<float>> inStore, outStore;
    std::vector<std::vector<float*>> inPtrs(numInPorts), outPtrs(numOutPorts);
    std::vector<clap_audio_buffer_t> inBuses(numInPorts), outBuses(numOutPorts);

    for (uint32_t b = 0; b < numInPorts; ++b)
        for (uint32_t c = 0; c < inCh[b]; ++c)
            inStore.emplace_back(numFrames, 0.0f);
    for (uint32_t b = 0; b < numOutPorts; ++b)
        for (uint32_t c = 0; c < outCh[b]; ++c)
            outStore.emplace_back(numFrames, 0.0f);

    {
        size_t next = 0;
        for (uint32_t b = 0; b < numInPorts; ++b)
        {
            for (uint32_t c = 0; c < inCh[b]; ++c)
                inPtrs[b].push_back(inStore[next++].data());
            inBuses[b].data32 = inPtrs[b].data();
            inBuses[b].channel_count = inCh[b];
        }
        next = 0;
        for (uint32_t b = 0; b < numOutPorts; ++b)
        {
            for (uint32_t c = 0; c < outCh[b]; ++c)
                outPtrs[b].push_back(outStore[next++].data());
            outBuses[b].data32 = outPtrs[b].data();
            outBuses[b].channel_count = outCh[b];
        }
    }

    InEvents inEvents;
    OutEvents outEvents;

    std::vector<double> meterMin(meters.size()), meterMax(meters.size()), meterFirst(meters.size());
    for (size_t k = 0; k < meters.size(); ++k)
    {
        double v = 0.0;
        paramsExt->get_value(plugin, params[meters[k]].id, &v);
        meterFirst[k] = meterMin[k] = meterMax[k] = v;
    }

    std::mt19937 rng(20260828u);
    std::uniform_real_distribution<float> noise(-0.5f, 0.5f);

    for (int block = 0; block < numBlocks; ++block)
    {
        if (numInPorts > 0)
        {
            for (uint32_t c = 0; c < inCh[0]; ++c)
            {
                float* const dst = inPtrs[0][c];
                for (int i = 0; i < numFrames; ++i)
                    dst[i] = silent ? 0.0f : noise(rng);
            }
        }

        clap_process_t process = {};
        process.frames_count = static_cast<uint32_t>(numFrames);
        process.audio_inputs = numInPorts > 0 ? inBuses.data() : nullptr;
        process.audio_inputs_count = numInPorts;
        process.audio_outputs = outBuses.data();
        process.audio_outputs_count = numOutPorts;
        process.in_events = &inEvents.iface;
        process.out_events = &outEvents.iface;
        process.steady_time = static_cast<int64_t>(block) * numFrames;

        const clap_process_status status = plugin->process(plugin, &process);
        if (status == CLAP_PROCESS_ERROR)
        {
            std::fprintf(stderr, "FAIL: process() error on block %d\n", block);
            return 1;
        }

        for (size_t k = 0; k < meters.size(); ++k)
        {
            double v = 0.0;
            paramsExt->get_value(plugin, params[meters[k]].id, &v);
            if (v < meterMin[k]) meterMin[k] = v;
            if (v > meterMax[k]) meterMax[k] = v;
        }
    }

    // The second entry point into flushParameters(). It cannot carry meter values
    // at this point -- the process() above already cached them and d_isEqual
    // swallows an unchanged value -- so this is a smoke check that flush() emits
    // nothing of its own, not a second regression gate. The gate is the block
    // loop above.
    paramsExt->flush(plugin, &inEvents.iface, &outEvents.iface);

    plugin->stop_processing(plugin);

    size_t outputParamEvents = 0, otherEvents = 0, unknownEvents = 0;
    for (const CollectedEvent& e : outEvents.events)
    {
        int idx = -1;
        for (size_t i = 0; i < params.size(); ++i)
            if (params[i].id == e.paramId) { idx = static_cast<int>(i); break; }

        if (e.paramId == CLAP_INVALID_ID)
            ++otherEvents;
        else if (idx < 0)
            ++unknownEvents;
        else if (isOutput(params[idx]))
            ++outputParamEvents;
        else
            ++otherEvents;
    }

    std::printf("\nblocks      : %d x %d frames @ 48 kHz, %s\n",
                numBlocks, numFrames, silent ? "digital silence" : "full-scale noise");
    std::printf("total out_events                      : %zu\n", outEvents.events.size());
    std::printf("output-param events reaching the host : %zu\n", outputParamEvents);
    std::printf("other events reaching the host        : %zu\n", otherEvents);
    std::printf("events for ids the plugin never listed : %zu\n", unknownEvents);

    bool meterMoved = false;
    std::printf("\nmeter read-back through clap_plugin_params.get_value (first, min, max):\n");
    for (size_t k = 0; k < meters.size(); ++k)
    {
        const P& p(params[meters[k]]);
        std::printf("  id=%u \"%s\" first=%.6f min=%.6f max=%.6f\n",
                    p.id, p.name.c_str(), meterFirst[k], meterMin[k], meterMax[k]);
        if (meterMax[k] - meterMin[k] > 1.0e-4)
            meterMoved = true;
    }

    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();

    std::fflush(stdout);
    int failures = 0;

    if (! meterMoved)
    {
        std::fprintf(stderr, "\nFAIL: no output parameter moved over %d blocks of %s;\n"
                             "      the meters are not tracking the audio.\n",
                     numBlocks, silent ? "silence" : "noise");
        ++failures;
    }
    else
    {
        std::printf("\nPASS: meters track the audio (at least one output parameter moved).\n");
    }

    if (outputParamEvents != 0)
    {
        std::fprintf(stderr, "FAIL: %zu output-parameter events were pushed to the host's out_events.\n"
                             "      CLAP gives a host no way to tell these from a user's own edits;\n"
                             "      Bitwig takes the meter over as last-touched and re-marks the\n"
                             "      project modified (dusk-audio/plugins#231). Expected 0.\n",
                     outputParamEvents);
        // The header flags are the reason these are so damaging: CLAP_EVENT_IS_LIVE
        // (1 << 0) means "a user turning a physical knob", and without
        // CLAP_EVENT_DONT_RECORD (1 << 1) the host is invited to record them.
        for (const CollectedEvent& e : outEvents.events)
        {
            int idx = -1;
            for (size_t i = 0; i < params.size(); ++i)
                if (params[i].id == e.paramId) { idx = static_cast<int>(i); break; }
            if (idx >= 0 && isOutput(params[idx]))
            {
                std::fprintf(stderr, "      first offender: type=%u flags=0x%x id=%u \"%s\" value=%.6f\n",
                             e.type, e.flags, e.paramId, params[idx].name.c_str(), e.value);
                break;
            }
        }
        ++failures;
    }
    else
    {
        std::printf("PASS: no output-parameter events reached the host.\n");
    }

    if (unknownEvents != 0)
    {
        std::fprintf(stderr, "FAIL: %zu events carried parameter ids the plugin never listed,\n"
                             "      so they could not be classified.\n", unknownEvents);
        ++failures;
    }

    std::printf("\n%s\n", failures == 0 ? "RESULT: PASS" : "RESULT: FAIL");
    return failures == 0 ? 0 : 1;
}
