// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
//
// CLAP factory presets, end to end, through a plugin's shipped .clap.
//
// DAF publishes a plugin's programs to CLAP hosts through the preset-discovery
// factory and loads them through the preset-load extension. clap-validator's own
// preset-discovery-crawl and preset-discovery-load tests deadlock on every plugin
// that does this (its metadata receiver locks a mutex it already holds on the
// second preset), so .github/scripts/daf_clap_validate.py skips those two and this
// harness covers the same ground for every DAF plugin with programs:
//
//   * the factory, its one provider and that provider's single PLUGIN location;
//   * at least one preset, each named, each with a unique load key and exactly
//     this plugin's id (CLAP has no program count to check the total against);
//   * every preset loads through preset-load, the host is told which one loaded,
//     parameter values are rescanned, and the plugin keeps processing finite audio;
//   * a load key that is not one of ours is refused through on_error;
//   * the presets are not all the same sound (some parameter differs between them).
//
// Linux and macOS only (dlopen plus the bundle layouts below).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include <dlfcn.h>

// DAF vendors a trimmed CLAP SDK with no umbrella clap.h, so the pieces this
// harness needs are included individually.
#include "clap/entry.h"
#include "clap/plugin.h"
#include "clap/ext/audio-ports.h"
#include "clap/ext/params.h"
#include "clap/ext/preset-load.h"
#include "clap/factory/plugin-factory.h"
#include "clap/factory/preset-discovery.h"

static int gFailures = 0;

static void check(const bool ok, const char* const what)
{
    if (! ok)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++gFailures;
    }
}

// --------------------------------------------------------------------------------------------------------------------
// host

static int gRescans = 0;
static int gLoaded = 0;
static int gLoadErrors = 0;
static std::string gLastLoadedKey;

static void CLAP_ABI hostRescan(const clap_host_t*, clap_param_rescan_flags flags)
{
    if (flags & CLAP_PARAM_RESCAN_VALUES)
        ++gRescans;
}
static void CLAP_ABI hostClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
static void CLAP_ABI hostRequestFlush(const clap_host_t*) {}
static const clap_host_params_t gHostParams = { hostRescan, hostClear, hostRequestFlush };

static void CLAP_ABI hostPresetError(const clap_host_t*, uint32_t, const char*, const char*, int32_t, const char*)
{
    ++gLoadErrors;
}
static void CLAP_ABI hostPresetLoaded(const clap_host_t*, uint32_t, const char*, const char* const key)
{
    ++gLoaded;
    gLastLoadedKey = key != nullptr ? key : "";
}
static const clap_host_preset_load_t gHostPresetLoad = { hostPresetError, hostPresetLoaded };

static const void* CLAP_ABI hostGetExtension(const clap_host_t*, const char* const id)
{
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &gHostParams;
    if (std::strcmp(id, CLAP_EXT_PRESET_LOAD) == 0) return &gHostPresetLoad;
    return nullptr;
}
static void CLAP_ABI hostRequest(const clap_host_t*) {}

static const clap_host_t gHost = {
    CLAP_VERSION_INIT, nullptr,
    "DafClapPresetsTest", "Dusk Audio", "https://dusk-audio.github.io/", "1.0.0",
    hostGetExtension, hostRequest, hostRequest, hostRequest
};

// --------------------------------------------------------------------------------------------------------------------
// indexer

// Copied out of each callback: the strings a provider hands over are only promised
// for the duration of the call, so nothing here keeps one of its pointers.
struct DeclaredLocation {
    uint32_t kind;
    bool hasPath;
};
struct Preset {
    std::string name, key;
    std::vector<std::string> pluginIds;   // as "abi:id", from add_plugin_id
};
struct Crawl {
    std::vector<DeclaredLocation> locations;
    std::vector<Preset> presets;
};

static bool CLAP_ABI indexerFiletype(const clap_preset_discovery_indexer_t*, const clap_preset_discovery_filetype_t*)
{
    return true;
}
static bool CLAP_ABI indexerLocation(const clap_preset_discovery_indexer_t* const indexer,
                                     const clap_preset_discovery_location_t* const location)
{
    static_cast<Crawl*>(indexer->indexer_data)->locations.push_back({ location->kind, location->location != nullptr });
    return true;
}
static bool CLAP_ABI indexerSoundpack(const clap_preset_discovery_indexer_t*, const clap_preset_discovery_soundpack_t*)
{
    return true;
}
static const void* CLAP_ABI indexerExtension(const clap_preset_discovery_indexer_t*, const char*) { return nullptr; }

static Crawl& crawlOf(const clap_preset_discovery_metadata_receiver_t* const r)
{
    return *static_cast<Crawl*>(r->receiver_data);
}
static void CLAP_ABI rxError(const clap_preset_discovery_metadata_receiver_t*, int32_t, const char* const message)
{
    std::fprintf(stderr, "FAIL: provider reported an error: %s\n", message != nullptr ? message : "");
    ++gFailures;
}
static bool CLAP_ABI rxBegin(const clap_preset_discovery_metadata_receiver_t* const r, const char* const name,
                             const char* const key)
{
    crawlOf(r).presets.push_back({ name != nullptr ? name : "", key != nullptr ? key : "", {} });
    return true;
}
// Metadata belongs to the preset begun last; the CLAP header requires begin_preset first.
static void CLAP_ABI rxPluginId(const clap_preset_discovery_metadata_receiver_t* const r,
                                const clap_universal_plugin_id_t* const id)
{
    Crawl& crawl = crawlOf(r);
    if (crawl.presets.empty() || id == nullptr || id->abi == nullptr || id->id == nullptr)
    {
        std::fprintf(stderr, "FAIL: add_plugin_id with a null id, or before any begin_preset\n");
        ++gFailures;
        return;
    }
    crawl.presets.back().pluginIds.push_back(std::string(id->abi) + ":" + id->id);
}
static void CLAP_ABI rxSoundpack(const clap_preset_discovery_metadata_receiver_t*, const char*) {}
static void CLAP_ABI rxFlags(const clap_preset_discovery_metadata_receiver_t*, uint32_t) {}
static void CLAP_ABI rxCreator(const clap_preset_discovery_metadata_receiver_t*, const char*) {}
static void CLAP_ABI rxDescription(const clap_preset_discovery_metadata_receiver_t*, const char*) {}
static void CLAP_ABI rxTimestamps(const clap_preset_discovery_metadata_receiver_t*, clap_timestamp, clap_timestamp) {}
static void CLAP_ABI rxFeature(const clap_preset_discovery_metadata_receiver_t*, const char*) {}
static void CLAP_ABI rxExtra(const clap_preset_discovery_metadata_receiver_t*, const char*, const char*) {}

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

static constexpr uint32_t kFrames = 256;

// One buffer per port the plugin declares, each with that port's channel count:
// handing a plugin fewer ports than it declares is how a harness reads past the
// end of audio_inputs (a sidechain), and a synth declares no input at all.
struct Buses {
    std::vector<std::vector<float>> channels;
    std::vector<std::vector<float*>> pointers;
    std::vector<clap_audio_buffer_t> buffers;

    Buses(const clap_plugin_t* const plugin, const clap_plugin_audio_ports_t* const ports, const bool isInput)
    {
        const uint32_t count = ports->count(plugin, isInput);
        std::vector<uint32_t> widths(count, 0);
        for (uint32_t i = 0; i < count; ++i)
        {
            clap_audio_port_info_t info{};
            if (ports->get(plugin, i, isInput, &info))
                widths[i] = info.channel_count;
            for (uint32_t c = 0; c < widths[i]; ++c)
                channels.emplace_back(kFrames, 0.0f);
        }
        pointers.resize(count);
        buffers.resize(count);
        size_t next = 0;
        for (uint32_t i = 0; i < count; ++i)
        {
            for (uint32_t c = 0; c < widths[i]; ++c)
                pointers[i].push_back(channels[next++].data());
            buffers[i] = {};
            buffers[i].data32 = pointers[i].data();
            buffers[i].channel_count = widths[i];
        }
    }
};

static bool processBlock(const clap_plugin_t* const plugin, Buses& in, Buses& out, const bool impulse, int64_t& steady)
{
    for (auto& c : in.channels)
    {
        std::fill(c.begin(), c.end(), 0.0f);
        if (impulse)
            c[0] = 0.25f;
    }
    for (auto& c : out.channels)
        std::fill(c.begin(), c.end(), 0.0f);
    struct NoIn { static uint32_t size(const clap_input_events_t*) { return 0; }
                  static const clap_event_header_t* get(const clap_input_events_t*, uint32_t) { return nullptr; } };
    struct NoOut { static bool push(const clap_output_events_t*, const clap_event_header_t*) { return true; } };
    const clap_input_events_t inEvents = { nullptr, NoIn::size, NoIn::get };
    const clap_output_events_t outEvents = { nullptr, NoOut::push };
    clap_process_t process{};
    process.steady_time = steady;
    process.frames_count = kFrames;
    process.audio_inputs = in.buffers.empty() ? nullptr : in.buffers.data();
    process.audio_inputs_count = static_cast<uint32_t>(in.buffers.size());
    process.audio_outputs = out.buffers.empty() ? nullptr : out.buffers.data();
    process.audio_outputs_count = static_cast<uint32_t>(out.buffers.size());
    process.in_events = &inEvents;
    process.out_events = &outEvents;
    steady += kFrames;
    if (plugin->process(plugin, &process) == CLAP_PROCESS_ERROR)
        return false;
    for (const auto& c : out.channels)
        for (const float v : c)
            if (! std::isfinite(v))
                return false;
    return true;
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: %s <plugin.clap>\n", argv[0]);
        return 2;
    }
    const std::string path = binaryInsideBundle(argv[1]);
    void* const lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (lib == nullptr)
    {
        std::fprintf(stderr, "FAIL: dlopen(%s): %s\n", path.c_str(), dlerror());
        return 1;
    }
    const auto* const entry = static_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry"));
    if (entry == nullptr || ! entry->init(argv[1]))
    {
        std::fprintf(stderr, "FAIL: no usable clap_entry in %s\n", path.c_str());
        return 1;
    }
    const auto* const factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    const clap_plugin_descriptor_t* const desc =
        factory != nullptr && factory->get_plugin_count(factory) != 0 ? factory->get_plugin_descriptor(factory, 0) : nullptr;
    if (desc == nullptr)
    {
        std::fprintf(stderr, "FAIL: no plugin factory / descriptor\n");
        return 1;
    }

    // ---- crawl ----
    const auto* const discovery =
        static_cast<const clap_preset_discovery_factory_t*>(entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID));
    if (discovery == nullptr)
    {
        std::fprintf(stderr, "FAIL: a plugin with programs must publish the preset discovery factory\n");
        return 1;
    }
    check(discovery->count(discovery) == 1, "one preset provider");
    const clap_preset_discovery_provider_descriptor_t* const pdesc = discovery->get_descriptor(discovery, 0);
    if (pdesc == nullptr)
    {
        std::fprintf(stderr, "FAIL: no preset provider descriptor\n");
        return 1;
    }
    Crawl crawl;
    const clap_preset_discovery_indexer_t indexer = {
        CLAP_VERSION_INIT, "DafClapPresetsTest", "Dusk Audio", "", "1.0.0", &crawl,
        indexerFiletype, indexerLocation, indexerSoundpack, indexerExtension
    };
    const clap_preset_discovery_provider_t* const provider = discovery->create(discovery, &indexer, pdesc->id);
    check(provider != nullptr && provider->init(provider), "the preset provider starts");
    if (provider == nullptr)
        return 1;
    check(crawl.locations.size() == 1, "the provider declares one location");
    const clap_preset_discovery_metadata_receiver_t receiver = {
        &crawl, rxError, rxBegin, rxPluginId, rxSoundpack, rxFlags, rxCreator, rxDescription, rxTimestamps,
        rxFeature, rxExtra
    };
    for (const auto& location : crawl.locations)
    {
        // A file location is not crawled: the path it named was only valid during declare_location.
        const bool insidePlugin = location.kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN && ! location.hasPath;
        check(insidePlugin, "factory presets live inside the plugin (kind PLUGIN, null location)");
        if (insidePlugin)
            check(provider->get_metadata(provider, location.kind, nullptr, &receiver), "metadata for the location");
    }
    provider->destroy(provider);

    check(! crawl.presets.empty(), "at least one factory preset is published");
    const std::string ownId = std::string("clap:") + desc->id;
    std::set<std::string> seenKeys;
    for (const auto& preset : crawl.presets)
    {
        const char* const problem =
            preset.name.empty() ? "has no name"
            : preset.key.empty() || ! seenKeys.insert(preset.key).second ? "has an empty or duplicate load key"
            : preset.pluginIds.size() != 1 || preset.pluginIds.front() != ownId ? "does not name exactly this plugin's CLAP id"
            : nullptr;
        if (problem != nullptr)
        {
            std::fprintf(stderr, "FAIL: preset '%s' (key '%s') %s\n", preset.name.c_str(), preset.key.c_str(), problem);
            ++gFailures;
        }
    }

    // ---- load ----
    const clap_plugin_t* const plugin = factory->create_plugin(factory, &gHost, desc->id);
    if (plugin == nullptr || ! plugin->init(plugin))
    {
        std::fprintf(stderr, "FAIL: create_plugin/init\n");
        return 1;
    }
    const auto* const params = static_cast<const clap_plugin_params_t*>(plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    const auto* const presetLoad =
        static_cast<const clap_plugin_preset_load_t*>(plugin->get_extension(plugin, CLAP_EXT_PRESET_LOAD));
    const auto* const ports = static_cast<const clap_plugin_audio_ports_t*>(plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    if (params == nullptr || presetLoad == nullptr || ports == nullptr)
    {
        std::fprintf(stderr, "FAIL: the plugin must offer the params, preset-load and audio-ports extensions\n");
        return 1;
    }
    Buses in(plugin, ports, true), out(plugin, ports, false);
    if (! plugin->activate(plugin, 48000.0, 1, kFrames) || ! plugin->start_processing(plugin))
    {
        std::fprintf(stderr, "FAIL: activate/start_processing\n");
        return 1;
    }

    std::vector<std::vector<double>> snapshots;
    int64_t steady = 0;
    for (const auto& preset : crawl.presets)
    {
        const int loadedBefore = gLoaded, rescansBefore = gRescans;
        const bool ok = presetLoad->from_location(plugin, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, preset.key.c_str());
        const char* const problem =
            ! ok ? "from_location refused it"
            : gLoaded != loadedBefore + 1 ? "the host's loaded() was not called exactly once"
            : gLastLoadedKey != preset.key ? "loaded() named another load key"
            : gRescans <= rescansBefore ? "no parameter value rescan was requested"
            : nullptr;
        if (problem != nullptr)
        {
            std::fprintf(stderr, "FAIL: preset '%s' (key '%s'): %s\n", preset.name.c_str(), preset.key.c_str(), problem);
            ++gFailures;
        }
        std::vector<double> values;
        for (uint32_t p = 0; p < params->count(plugin); ++p)
        {
            clap_param_info_t info{};
            double v = 0.0;
            if (params->get_info(plugin, p, &info) && ! (info.flags & CLAP_PARAM_IS_READONLY)
                && params->get_value(plugin, info.id, &v))
                values.push_back(v);
        }
        snapshots.push_back(values);
        if (! processBlock(plugin, in, out, true, steady) || ! processBlock(plugin, in, out, false, steady))
        {
            std::fprintf(stderr, "FAIL: preset '%s' does not process finite audio\n", preset.name.c_str());
            ++gFailures;
        }
    }
    bool differ = snapshots.size() < 2;
    for (size_t i = 1; i < snapshots.size() && ! differ; ++i)
        differ = snapshots[i] != snapshots[0];
    check(differ, "the factory presets are not all the same parameter set");

    const int errorsBefore = gLoadErrors;
    check(! presetLoad->from_location(plugin, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, "999999"),
          "an unknown load key is refused");
    check(gLoadErrors == errorsBefore + 1, "a refused load is reported through on_error");

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);
    entry->deinit();

    if (gFailures != 0)
    {
        std::fprintf(stderr, "%d CLAP preset check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("PASS: %zu factory presets crawled and loaded through CLAP\n", crawl.presets.size());
    return 0;
}
