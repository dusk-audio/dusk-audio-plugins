// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Exercise the built successors' complete saved-input boundary through CLAP.
#include <clap/entry.h>
#include <clap/plugin.h>
#include <clap/factory/plugin-factory.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/thread-check.h>
#include "ChordAnalyzerState.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace
{
int checks = 0, failures = 0;
unsigned rescans = 0;
void check(bool ok, const char* message)
{++checks; if (!ok) {++failures; std::fprintf(stderr, "FAIL: %s\n", message);}}
const clap_host_params_t hostParams{[](const clap_host_t*, clap_param_rescan_flags) {++rescans;},
    [](const clap_host_t*, clap_id, clap_param_clear_flags) {}, [](const clap_host_t*) {}};
const clap_host_thread_check_t hostThreads{[](const clap_host_t*) {return true;}, [](const clap_host_t*) {return false;}};
const clap_host_t host{CLAP_VERSION_INIT, nullptr, "ChordStateHostTest", "Dusk Audio", "https://dusk-audio.github.io", "1",
    [](const clap_host_t*, const char* id)->const void* {
        if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &hostParams;
        if (!std::strcmp(id, CLAP_EXT_THREAD_CHECK)) return &hostThreads;
        return nullptr;
    }, [](const clap_host_t*) {}, [](const clap_host_t*) {}, [](const clap_host_t*) {}};
struct Saved
{
    std::vector<char> bytes;
    std::size_t offset = 0;
    uint64_t chunk = 3;
    Saved() = default;
    Saved(const Saved&) = delete;
    Saved(Saved&& other) noexcept : bytes(std::move(other.bytes)), offset(other.offset), chunk(other.chunk) {}
    clap_ostream_t output{this, [](const clap_ostream_t* stream, const void* data, uint64_t size)->int64_t {
        auto& state = *static_cast<Saved*>(stream->ctx);
        const auto* begin = static_cast<const char*>(data);
        state.bytes.insert(state.bytes.end(), begin, begin + std::min(size, state.chunk));
        return static_cast<int64_t>(std::min(size, state.chunk));
    }};
    clap_istream_t input{this, [](const clap_istream_t* stream, void* data, uint64_t size)->int64_t {
        auto& state = *static_cast<Saved*>(stream->ctx);
        const auto count = std::min({size, state.chunk, uint64_t(state.bytes.size() - state.offset)});
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

using namespace duskaudio::chord;
void token(Saved& stream, const std::string& text)
{ stream.bytes.insert(stream.bytes.end(), text.begin(), text.end()); stream.bytes.push_back(0); }
Saved payload(const std::string& text, unsigned copies = 1)
{
    Saved stream;
    token(stream, "__daf_program__"); token(stream, "999");
    token(stream, "__daf_state_begin__");
    for (unsigned i=0; i<copies; ++i) { token(stream, "parameters"); token(stream, text); }
    token(stream, "__daf_state_end__");
    // A complete snapshot is authoritative over these redundant parameters.
    token(stream, "__daf_parameters_begin__");
    token(stream, "keyRoot"); token(stream, "9");
    token(stream, "keyMode"); token(stream, "garbage");
    token(stream, "__daf_parameters_end__");
    stream.bytes.push_back(char(0xfe)); stream.bytes.push_back(0);
    return stream;
}
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
    check(initialized, "actual successor CLAP library initializes");
    if (!initialized) return 1;
    auto* factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!factory) return 1;
    const auto* descriptor = factory->get_plugin_descriptor(factory, 0);
    if (!descriptor) return 1;
    const auto* plugin = factory->create_plugin(factory, &host, descriptor->id);
    if (!plugin || !plugin->init(plugin)) return 1;
    auto* params = static_cast<const clap_plugin_params_t*>(plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    auto* state = static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin, CLAP_EXT_STATE));
    check(params && state && params->count(plugin) == PARAM_COUNT, "actual parameter and state interfaces retain the complete ABI");
    if (!params || !state) return 1;
    auto values = [&] {
        InputValues result{};
        for (unsigned i=0; i<inputParameterCount; ++i) {
            double value=-1;
            check(params->get_value(plugin, i, &value), "each saved input remains readable");
            result[i]=static_cast<float>(value);
        }
        return result;
    };
    const InputValues defaults{{0,0,2,1,1}}, saved{{6,1,1,0,0}};
    check(values()==defaults, "fresh instance starts with the correct input defaults");
    auto accepted=payload(encodeInputState(saved));
    unsigned beforeRescans=rescans;
    check(state->load(plugin,&accepted.input) && values()==saved && rescans>beforeRescans,
          "chunked complete recall restores all five inputs and notifies the host");
    Saved before;
    check(state->save(plugin,&before.output) && !before.bytes.empty(), "short writes save a populated state");
    const std::string bytes(before.bytes.begin(),before.bytes.end());
    check(bytes.find(encodeInputState(saved))!=std::string::npos, "host state contains the canonical complete snapshot");
    for (unsigned i=inputParameterCount; i<PARAM_COUNT; ++i)
        check(bytes.find(parameterSpecs[i].symbol)==std::string::npos, "host saves exclude every transient detection key");
    auto rejected = [&](Saved invalid) {
        const auto initial=values();
        const auto oldRescans=rescans;
        const bool loaded=state->load(plugin,&invalid.input);
        check(!loaded && values()==initial && rescans==oldRescans,
              "invalid host state changes neither inputs nor host value mirrors");
    };
    rejected(payload("",0)); rejected(payload(encodeInputState(saved),2));
    rejected(payload("ChordAnalyzer2:2\n"));
    for (unsigned i=0; i<inputParameterCount; ++i) {
        const auto text=encodeInputState(defaults), key=std::string(parameterSpecs[i].symbol)+"=";
        const auto begin=text.find(key), end=text.find('\n',begin);
        rejected(payload(text.substr(0,begin)+text.substr(end+1)));
        rejected(payload(text+text.substr(begin,end+1-begin)));
        for (const char* value : {"nan", "0.5", "garbage", "-1", "999", "1junk"})
            rejected(payload(text.substr(0,begin)+key+value+text.substr(end)));
    }
    auto truncated=payload(encodeInputState(defaults)); truncated.bytes.pop_back(); truncated.bytes.pop_back(); rejected(std::move(truncated));
    auto trailing=payload(encodeInputState(defaults)); trailing.bytes.push_back('x'); rejected(std::move(trailing));
    auto empty=Saved{}; rejected(std::move(empty));
    auto defaultsState=payload(encodeInputState(defaults));
    check(state->load(plugin,&defaultsState.input) && values()==defaults, "a valid recall still applies after all rejected states");
    before.offset=0;
    check(state->load(plugin,&before.input) && values()==saved, "actual saved bytes restore all nondefault controls");
    Saved after;
    check(state->save(plugin,&after.output) && before.bytes==after.bytes, "save/load/save remains byte reproducible");
    // All individual inputs must still be writable after a snapshot restore.
    clap_output_events_t output{nullptr, [](const clap_output_events_t*, const clap_event_header_t*) {return true;}};
    Changes changes;
    for (unsigned i=0; i<inputParameterCount; ++i) {
        auto& event=changes.values[i];
        event.header={sizeof(event),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_PARAM_VALUE,0};
        event.param_id=i; event.value=defaults[i]; event.note_id=event.port_index=event.channel=event.key=-1;
    }
    params->flush(plugin,&changes.input,&output);
    check(values()==defaults, "host automation still edits all five controls after complete recalls");
    plugin->destroy(plugin); entry->deinit();
#if defined(_WIN32)
    FreeLibrary(module);
#else
    dlclose(module);
#endif
    std::printf("Successor CLAP state: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
