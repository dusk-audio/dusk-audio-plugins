// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Exercise the built legacy CLAP's metadata and saved-input boundary.
#include <clap/entry.h>
#include <clap/plugin.h>
#include <clap/factory/plugin-factory.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/thread-check.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace
{
int checks = 0, failures = 0;
void check(bool ok, const char* message)
{++checks; if (!ok) {++failures; std::fprintf(stderr, "FAIL: %s\n", message);}}
const clap_host_params_t hostParams{[](const clap_host_t*, clap_param_rescan_flags) {},
    [](const clap_host_t*, clap_id, clap_param_clear_flags) {}, [](const clap_host_t*) {}};
const clap_host_thread_check_t hostThreads{[](const clap_host_t*) {return true;}, [](const clap_host_t*) {return false;}};
const clap_host_t host{CLAP_VERSION_INIT, nullptr, "ChordClapOutputTest", "Dusk Audio", "https://dusk-audio.github.io", "1",
    [](const clap_host_t*, const char* id)->const void* {
        if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &hostParams;
        if (!std::strcmp(id, CLAP_EXT_THREAD_CHECK)) return &hostThreads;
        return nullptr;
    }, [](const clap_host_t*) {}, [](const clap_host_t*) {}, [](const clap_host_t*) {}};
struct Saved
{
    std::vector<char> bytes;
    std::size_t offset = 0;
    clap_ostream_t output{this, [](const clap_ostream_t* stream, const void* data, uint64_t size)->int64_t {
        auto& state = *static_cast<Saved*>(stream->ctx);
        const auto* begin = static_cast<const char*>(data);
        state.bytes.insert(state.bytes.end(), begin, begin + size);
        return static_cast<int64_t>(size);
    }};
    clap_istream_t input{this, [](const clap_istream_t* stream, void* data, uint64_t size)->int64_t {
        auto& state = *static_cast<Saved*>(stream->ctx);
        const auto count = std::min<uint64_t>(size, state.bytes.size() - state.offset);
        if (count) std::memcpy(data, state.bytes.data() + state.offset, static_cast<std::size_t>(count));
        state.offset += static_cast<std::size_t>(count); return static_cast<int64_t>(count);
    }};
};
struct Changes
{
    std::array<clap_event_param_value_t, 5> values{};
    clap_input_events_t input{this, [](const clap_input_events_t*) {return 5u;},
        [](const clap_input_events_t* events, uint32_t i)->const clap_event_header_t* {
            return &static_cast<Changes*>(events->ctx)->values[i].header;
        }};
};
// Captured shipping IDs/order/defaults. They must survive parameter subclassing.
constexpr std::array<clap_id, 10> ids{{3479456737u, 3479307426u, 2627503072u, 3493426589u, 94418301u,
    3327064708u, 895634461u, 3326574721u, 941828017u, 45729530u}};
constexpr std::array<const char*, 10> names{{"Key Root", "Key Mode", "Suggestion Level", "Show Inversions", "Respect Sustain",
    "Detected Root", "Detected Quality", "Detected Bass", "Detected Inversion", "Detected Chord"}};
constexpr std::array<const char*, 10> keys{{"keyRoot", "keyMode", "suggestionLevel", "showInversions", "respectSustain",
    "detectedRoot", "detectedQuality", "detectedBass", "detectedInversion", "detectedChord"}};
constexpr std::array<double, 5> defaults{{0, 0, 1, 1, 1}};
}

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
#if defined(_WIN32)
    auto module = LoadLibraryA(argv[1]);
    auto* entry = module ? reinterpret_cast<const clap_plugin_entry_t*>(GetProcAddress(module, "clap_entry")) : nullptr;
#else
    auto* module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    auto* entry = module ? static_cast<const clap_plugin_entry_t*>(dlsym(module, "clap_entry")) : nullptr;
#endif
    const bool initialized = entry && entry->init(argv[1]);
    check(initialized, "actual legacy CLAP library initializes");
    if (!initialized) return 1;
    auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    check(factory && factory->get_plugin_count(factory) == 1, "legacy binary exposes one plugin");
    if (!factory) return 1;
    const auto* descriptor = factory->get_plugin_descriptor(factory, 0);
    check(descriptor && !std::strcmp(descriptor->id, "com.dusk-audio.chord-analyzer"), "legacy CLAP identity is unchanged");
    if (!descriptor) return 1;
    const auto* plugin = factory->create_plugin(factory, &host, descriptor->id);
    const bool pluginInitialized = plugin && plugin->init(plugin);
    check(pluginInitialized, "actual legacy processor initializes");
    if (!pluginInitialized) return 1;
    auto* params = static_cast<const clap_plugin_params_t*>(plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    auto* state = static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin, CLAP_EXT_STATE));
    check(params && state, "real parameter and state extensions exist");
    if (!params || !state) return 1;
    check(params->count(plugin) == ids.size(), "exactly five saved inputs and five transient outputs exist");
    bool identifiersMatch = true;
    for (unsigned i = 0; i < ids.size(); ++i)
    {
        clap_param_info_t info{};
        check(params->get_info(plugin, i, &info), "each parameter is enumerable");
        identifiersMatch &= info.id == ids[i];
        check(info.id == ids[i] && !std::strcmp(info.name, names[i]), "shipping IDs, names and order remain stable");
        check(info.min_value == 0 && info.max_value == 1 && info.default_value == (i < 5 ? defaults[i] : 0),
              "legacy normalized domains and defaults remain stable");
        check(bool(info.flags & CLAP_PARAM_IS_READONLY) == (i >= 5), "only the five transient outputs are read-only");
        check(bool(info.flags & CLAP_PARAM_IS_AUTOMATABLE) == (i < 5), "only the five saved inputs are automatable");
    }
    if (!identifiersMatch) {plugin->destroy(plugin); entry->deinit(); return 1;}
    for (unsigned i = 5; i < ids.size(); ++i)
    {
        constexpr std::array<double, 5> probes{{1.0 / 12, 1.0 / 33, 1.0 / 12, 2.0 / 8, 61.0 / 295340}};
        constexpr std::array<const char*, 5> text{{"C", "maj", "C", "1st", "C4"}};
        char label[256]{};
        check(params->value_to_text(plugin, ids[i], probes[i - 5], label, sizeof(label)) && !std::strcmp(label, text[i - 5]),
              "read-only outputs retain named choices and the encoded chord label");
    }
    clap_output_events_t output{nullptr, [](const clap_output_events_t*, const clap_event_header_t*) {return true;}};
    const auto apply = [&](const std::array<double, 5>& values) {
        Changes changes;
        for (unsigned i = 0; i < values.size(); ++i)
        {
            auto& event = changes.values[i];
            event.header = {sizeof(event), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
            event.param_id = ids[i]; event.value = values[i];
            event.note_id = -1; event.port_index = -1; event.channel = -1; event.key = -1;
        }
        params->flush(plugin, &changes.input, &output);
    };
    constexpr std::array<double, 5> saved{{6.0 / 11, 1, 0.5, 0, 0}};
    apply(saved);
    Saved before;
    check(state->save(plugin, &before.output) && !before.bytes.empty(), "actual CLAP saves nondefault inputs");
    const std::string bytes(before.bytes.begin(), before.bytes.end());
    for (unsigned i = 0; i < keys.size(); ++i)
        check((bytes.find(keys[i]) != std::string::npos) == (i < 5), "state contains all saved inputs and no transient detection keys");
    apply(defaults);
    for (unsigned i = 0; i < defaults.size(); ++i)
    {
        double value = -1;
        check(params->get_value(plugin, ids[i], &value) && std::abs(value - saved[i]) > 0.01,
              "all five input values change before the restore proof");
    }
    check(state->load(plugin, &before.input), "actual CLAP restores the saved state");
    for (unsigned i = 0; i < saved.size(); ++i)
    {
        double value = -1;
        check(params->get_value(plugin, ids[i], &value) && std::abs(value - saved[i]) < 1e-6,
              "all five nondefault inputs survive actual host state restore");
    }
    Saved after;
    check(state->save(plugin, &after.output) && before.bytes == after.bytes, "CLAP save/load/save remains byte reproducible");
    plugin->destroy(plugin); entry->deinit();
#if defined(_WIN32)
    FreeLibrary(module);
#else
    dlclose(module);
#endif
    std::printf("Legacy CLAP outputs/state: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
