#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "DuskVerbTestLibrary.hpp"
#include <set>
#include <utility>
#include <vector>

#include "clap/entry.h"
#include "clap/ext/audio-ports.h"
#include "clap/ext/audio-ports-config.h"
#include "clap/factory/plugin-factory.h"

namespace {

int failures = 0;
int checks = 0;

void check(const bool condition, const char* const message)
{
    ++checks;
    if (! condition)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}

static const void* CLAP_ABI hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
static void CLAP_ABI hostRequest(const clap_host_t*) {}

static const clap_host_t host = {
    CLAP_VERSION_INIT, nullptr,
    "DAF CLAP layout regression", "Dusk Audio", "", "1.0",
    hostGetExtension, hostRequest, hostRequest, hostRequest
};

struct EmptyInputEvents {
    clap_input_events_t iface{};
    EmptyInputEvents()
    {
        iface.ctx = this;
        iface.size = [](const clap_input_events_t*) { return uint32_t(0); };
        iface.get = [](const clap_input_events_t*, uint32_t) -> const clap_event_header_t* {
            return nullptr;
        };
    }
};

struct EmptyOutputEvents {
    clap_output_events_t iface{};
    EmptyOutputEvents()
    {
        iface.ctx = this;
        iface.try_push = [](const clap_output_events_t*, const clap_event_header_t*) { return true; };
    }
};

struct Library {
    void* handle = nullptr;
    const clap_plugin_entry_t* entry = nullptr;
    const clap_plugin_factory_t* factory = nullptr;

    explicit Library(const char* const path)
    {
        handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (handle == nullptr)
            return;
        entry = static_cast<const clap_plugin_entry_t*>(dlsym(handle, "clap_entry"));
        if (entry == nullptr || ! entry->init(path))
        {
            entry = nullptr;
            return;
        }
        factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    }

    ~Library()
    {
        if (entry != nullptr)
            entry->deinit();
        if (handle != nullptr)
            dlclose(handle);
    }
};

struct Instance {
    const clap_plugin_t* plugin = nullptr;
    const clap_plugin_audio_ports_t* ports = nullptr;
    const clap_plugin_audio_ports_config_t* configs = nullptr;
    bool active = false;
    bool processing = false;

    explicit Instance(const clap_plugin_factory_t* const factory)
    {
        const clap_plugin_descriptor_t* descriptor = nullptr;
        if (factory != nullptr && factory->get_plugin_count(factory) != 0)
            descriptor = factory->get_plugin_descriptor(factory, 0);
        if (descriptor == nullptr)
            return;
        plugin = factory->create_plugin(factory, &host, descriptor->id);
        if (plugin == nullptr || ! plugin->init(plugin))
        {
            if (plugin != nullptr)
                plugin->destroy(plugin);
            plugin = nullptr;
            return;
        }
        ports = static_cast<const clap_plugin_audio_ports_t*>(
            plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
        configs = static_cast<const clap_plugin_audio_ports_config_t*>(
            plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS_CONFIG));
    }

    ~Instance()
    {
        deactivate();
        if (plugin != nullptr)
            plugin->destroy(plugin);
    }

    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;

    bool activate()
    {
        active = plugin != nullptr && plugin->activate(plugin, 48000.0, 1, 512);
        processing = active && plugin->start_processing(plugin);
        return processing;
    }

    void deactivate()
    {
        if (processing)
        {
            plugin->stop_processing(plugin);
            processing = false;
        }
        if (active)
        {
            plugin->deactivate(plugin);
            active = false;
        }
    }

    bool portInfo(const bool input, clap_audio_port_info_t& info) const
    {
        return ports != nullptr && ports->count(plugin, input) == 1
            && ports->get(plugin, 0, input, &info);
    }
};

struct Layout {
    clap_id id = CLAP_INVALID_ID;
    uint32_t inputs = 0;
    uint32_t outputs = 0;
};

std::vector<Layout> readLayouts(const Instance& instance)
{
    std::vector<Layout> result;
    if (instance.configs == nullptr)
        return result;
    const uint32_t count = instance.configs->count(instance.plugin);
    for (uint32_t i=0; i<count; ++i)
    {
        clap_audio_ports_config_t config{};
        check(instance.configs->get(instance.plugin, i, &config), "configuration metadata is queryable");
        if (config.id == CLAP_INVALID_ID)
            continue;
        check(config.input_port_count == 1 && config.output_port_count == 1,
              "configuration has one input and one output bus");
        check(config.has_main_input && config.has_main_output,
              "configuration describes both main buses");
        const char* const expectedInputType = config.main_input_channel_count == 1 ? CLAP_PORT_MONO : CLAP_PORT_STEREO;
        const char* const expectedOutputType = config.main_output_channel_count == 1 ? CLAP_PORT_MONO : CLAP_PORT_STEREO;
        check(config.main_input_port_type != nullptr
              && std::strcmp(config.main_input_port_type, expectedInputType) == 0,
              "configuration input mono/stereo type matches its channel count");
        check(config.main_output_port_type != nullptr
              && std::strcmp(config.main_output_port_type, expectedOutputType) == 0,
              "configuration output mono/stereo type matches its channel count");
        result.push_back({config.id, config.main_input_channel_count, config.main_output_channel_count});
    }
    clap_audio_ports_config_t extra{};
    check(! instance.configs->get(instance.plugin, count, &extra), "out-of-range configuration index is rejected");
    return result;
}

const Layout* findLayout(const std::vector<Layout>& layouts, const uint32_t inputs, const uint32_t outputs)
{
    for (const Layout& layout : layouts)
        if (layout.inputs == inputs && layout.outputs == outputs)
            return &layout;
    return nullptr;
}

bool metadataMatches(const Instance& instance, const uint32_t inputs, const uint32_t outputs)
{
    clap_audio_port_info_t input{}, output{};
    if (! instance.portInfo(true, input) || ! instance.portInfo(false, output))
        return false;
    const bool inputType = input.port_type != nullptr
                        && std::strcmp(input.port_type, inputs == 1 ? CLAP_PORT_MONO : CLAP_PORT_STEREO) == 0;
    const bool outputType = output.port_type != nullptr
                         && std::strcmp(output.port_type, outputs == 1 ? CLAP_PORT_MONO : CLAP_PORT_STEREO) == 0;
    const bool pair = inputs == outputs
                    ? input.in_place_pair == output.id && output.in_place_pair == input.id
                    : input.in_place_pair == CLAP_INVALID_ID && output.in_place_pair == CLAP_INVALID_ID;
    return input.channel_count == inputs && output.channel_count == outputs
        && inputType && outputType && pair;
}

struct Render {
    std::vector<float> left;
    std::vector<float> right;
    bool valid = true;
    bool nonzero = false;
};

Render processSelected(Instance& instance, const uint32_t inputs, const uint32_t outputs)
{
    Render result;
    if (! instance.activate())
    {
        result.valid = false;
        return result;
    }

    EmptyInputEvents inputEvents;
    EmptyOutputEvents outputEvents;
    int64_t steadyTime = 0;
    const std::array<uint32_t, 7> frameCounts = {0, 1, 17, 64, 257, 128, 33};

    for (uint32_t block=0; block<64; ++block)
    {
        const uint32_t frames = frameCounts[block % frameCounts.size()];
        std::vector<float> mono(frames);
        for (uint32_t i=0; i<frames; ++i)
            mono[i] = static_cast<float>((int((steadyTime + i) % 31) - 15) * 0.01);
        std::vector<float> duplicate(mono);
        std::vector<float> left(frames, -900.0f), right(frames, -900.0f);
        float* inputChannels[] = {mono.data(), duplicate.data()};
        float* outputChannels[] = {left.data(), right.data()};
        clap_audio_buffer_t input{}, output{};
        input.data32 = inputChannels;
        input.channel_count = inputs;
        output.data32 = outputChannels;
        output.channel_count = outputs;
        clap_process_t process{};
        process.steady_time = steadyTime;
        process.frames_count = frames;
        process.audio_inputs = &input;
        process.audio_outputs = &output;
        process.audio_inputs_count = 1;
        process.audio_outputs_count = 1;
        process.in_events = &inputEvents.iface;
        process.out_events = &outputEvents.iface;
        if (instance.plugin->process(instance.plugin, &process) == CLAP_PROCESS_ERROR)
            result.valid = false;
        steadyTime += frames;

        for (float sample : left)
        {
            result.valid = result.valid && std::isfinite(sample);
            result.nonzero = result.nonzero || std::abs(sample) > 1.0e-8f;
        }
        if (outputs == 2)
        {
            for (float sample : right)
            {
                result.valid = result.valid && std::isfinite(sample);
                result.nonzero = result.nonzero || std::abs(sample) > 1.0e-8f;
            }
        }
        result.left.insert(result.left.end(), left.begin(), left.end());
        if (outputs == 2)
            result.right.insert(result.right.end(), right.begin(), right.end());
    }
    return result;
}

Render renderLayout(const clap_plugin_factory_t* const factory, const Layout& layout)
{
    Instance instance(factory);
    Render result;
    if (instance.plugin == nullptr || instance.configs == nullptr
        || ! instance.configs->select(instance.plugin, layout.id)
        || ! metadataMatches(instance, layout.inputs, layout.outputs))
    {
        result.valid = false;
        return result;
    }
    return processSelected(instance, layout.inputs, layout.outputs);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: %s /path/to/plugin.clap\n", argv[0]);
        return 2;
    }

    Library library(argv[1]);
    check(library.factory != nullptr, "CLAP library and plugin factory load");
    Instance probe(library.factory);
    check(probe.plugin != nullptr, "CLAP plugin initializes");
    check(probe.ports != nullptr, "clap.audio-ports is exposed");
    check(probe.configs != nullptr, "clap.audio-ports-config is exposed");
    if (probe.plugin == nullptr || probe.ports == nullptr || probe.configs == nullptr)
    {
        std::fprintf(stderr, "%d/%d checks failed\n", failures, checks);
        return 1;
    }

    check(metadataMatches(probe, 2, 2), "default live port metadata remains stereo 2/2");
    const std::vector<Layout> layouts = readLayouts(probe);
    check(layouts.size() == 3, "exactly default 2/2 plus declared 1/1 and 1/2 configurations are exposed");
    std::set<clap_id> ids;
    for (const Layout& layout : layouts)
        ids.insert(layout.id);
    check(ids.size() == layouts.size(), "configuration IDs are unique");
    Instance secondProbe(library.factory);
    const std::vector<Layout> secondLayouts = readLayouts(secondProbe);
    bool stableIds = secondLayouts.size() == layouts.size();
    for (const Layout& layout : layouts)
    {
        const Layout* const again = findLayout(secondLayouts, layout.inputs, layout.outputs);
        stableIds = stableIds && again != nullptr && again->id == layout.id;
    }
    check(stableIds, "configuration IDs are stable across instances");

    const Layout* const mono = findLayout(layouts, 1, 1);
    const Layout* const monoToStereo = findLayout(layouts, 1, 2);
    const Layout* const stereo = findLayout(layouts, 2, 2);
    check(mono != nullptr && monoToStereo != nullptr && stereo != nullptr,
          "2/2, 1/1 and 1/2 configurations are all present");
    if (mono == nullptr || monoToStereo == nullptr || stereo == nullptr)
        return 1;

    check(! probe.configs->select(probe.plugin, CLAP_INVALID_ID),
          "invalid selection is rejected while deactivated");
    check(metadataMatches(probe, 2, 2), "invalid deactivated selection leaves layout untouched");
    check(probe.configs->select(probe.plugin, monoToStereo->id), "1/2 selects while deactivated");
    check(metadataMatches(probe, 1, 2), "selected 1/2 live metadata is mono-in/stereo-out with no in-place pair");
    check(probe.activate(), "selected 1/2 instance activates");
    check(! probe.configs->select(probe.plugin, stereo->id), "valid layout selection is rejected while active");
    check(! probe.configs->select(probe.plugin, CLAP_INVALID_ID), "invalid layout selection is rejected while active");
    check(metadataMatches(probe, 1, 2), "active selection attempts leave layout untouched");
    probe.deactivate();
    check(probe.configs->select(probe.plugin, stereo->id), "layout can be reselected after deactivation");
    check(metadataMatches(probe, 2, 2), "reselected 2/2 metadata is restored");
    Render reselected = processSelected(probe, 2, 2);
    check(reselected.valid && reselected.nonzero, "reselected layout processes zero/variable frames to finite nonzero audio");
    probe.deactivate();
    check(probe.activate(), "same selected layout supports repeated activation");
    probe.deactivate();

    const Render oneOne = renderLayout(library.factory, *mono);
    const Render oneTwo = renderLayout(library.factory, *monoToStereo);
    const Render twoTwo = renderLayout(library.factory, *stereo);
    check(oneOne.valid && oneOne.nonzero, "real selected 1/1 processing is finite and nonzero");
    check(oneTwo.valid && oneTwo.nonzero, "real selected 1/2 processing is finite and nonzero");
    check(twoTwo.valid && twoTwo.nonzero, "real selected 2/2 processing is finite and nonzero");
    check(oneTwo.left == twoTwo.left && oneTwo.right == twoTwo.right,
          "selected 1/2 equals selected 2/2 when the stereo input duplicates the mono signal");

    if (failures == 0)
        std::printf("PASS: %d CLAP selectable-layout checks\n", checks);
    else
        std::fprintf(stderr, "FAIL: %d/%d CLAP selectable-layout checks failed\n", failures, checks);
    return failures == 0 ? 0 : 1;
}
