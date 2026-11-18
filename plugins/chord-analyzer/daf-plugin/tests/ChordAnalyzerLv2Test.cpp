// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Load the built DAF LV2 binary through its public ABI, including atom ports.
#include "lv2/lv2.h"
#include "lv2/urid.h"
#include "lv2/options.h"
#include "lv2/buf-size.h"
#include "lv2/atom-util.h"
#include "lv2/state.h"
#include "lv2/worker.h"
#include "ChordAnalyzerState.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#if defined(_WIN32)
# include <windows.h>
#else
# include <dlfcn.h>
#endif

namespace
{
int checks = 0, failures = 0;
void check(bool ok, const char* message)
{
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
struct Mapper
{
    std::unordered_map<std::string, LV2_URID> urids;
    static LV2_URID map(void* handle, const char* uri)
    {
        auto& table = static_cast<Mapper*>(handle)->urids;
        const auto result = table.emplace(uri, static_cast<LV2_URID>(table.size() + 1));
        return result.first->second;
    }
    LV2_URID operator()(const char* uri) { return map(this, uri); }
};
struct Buffer { LV2_Atom_Sequence seq{}; std::array<std::uint64_t, 1024> storage{}; };
struct Packet { LV2_Atom_Event event{}; std::array<std::uint8_t, 8> bytes{}; };
struct SavedState
{
    struct Property { std::vector<char> bytes; uint32_t type = 0, flags = 0; };
    std::unordered_map<uint32_t, Property> properties;
    static LV2_State_Status store(void* handle, uint32_t key, const void* bytes, size_t size, uint32_t type, uint32_t flags)
    {
        auto& property = static_cast<SavedState*>(handle)->properties[key];
        const auto* begin = static_cast<const char*>(bytes);
        property.bytes.assign(begin, begin + size); property.type = type; property.flags = flags;
        return LV2_STATE_SUCCESS;
    }
    static const void* retrieve(void* handle, uint32_t key, size_t* size, uint32_t* type, uint32_t* flags)
    {
        auto& properties = static_cast<SavedState*>(handle)->properties;
        const auto it = properties.find(key);
        if (it == properties.end()) return nullptr;
        *size = it->second.bytes.size(); *type = it->second.type; *flags = it->second.flags;
        return it->second.bytes.data();
    }
};
}

int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    const bool midiOnly = std::strcmp(argv[2], "1") == 0;
   #if defined(_WIN32)
    auto library = LoadLibraryA(argv[1]);
    auto descriptor = library ? reinterpret_cast<LV2_Descriptor_Function>(GetProcAddress(library, "lv2_descriptor")) : nullptr;
   #else
    auto library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    auto descriptor = library ? reinterpret_cast<LV2_Descriptor_Function>(dlsym(library, "lv2_descriptor")) : nullptr;
   #endif
    check(descriptor != nullptr, "built LV2 binary loads and exports its descriptor");
    if (!descriptor) return 1;
    const auto* api = descriptor(0);
    check(api && descriptor(1) == nullptr, "exactly one successor per LV2 binary");
    if (!api) return 1;
    check(std::strcmp(api->URI, midiOnly ? "https://dusk-audio.github.io/plugins/chord-analyzer-midi-2"
                                      : "https://dusk-audio.github.io/plugins/chord-analyzer-2") == 0,
          "successor URI is distinct from shipping legacy LV2");
    Mapper mapper;
    LV2_URID_Map map{&mapper, Mapper::map};
    const std::int32_t blockSize = 512;
    LV2_Options_Option options[] = {
        {LV2_OPTIONS_INSTANCE, 0, mapper(LV2_BUF_SIZE__nominalBlockLength), sizeof(blockSize), mapper(LV2_ATOM__Int), &blockSize},
        {LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, nullptr}
    };
    const LV2_Feature mapFeature{LV2_URID__map, &map};
    const LV2_Feature optionFeature{LV2_OPTIONS__options, options};
    unsigned workerRequests = 0;
    LV2_Worker_Schedule scheduler{&workerRequests, [](void* context, uint32_t, const void*) {
        ++*static_cast<unsigned*>(context); return LV2_WORKER_ERR_UNKNOWN;
    }};
    const LV2_Feature workerFeature{LV2_WORKER__schedule, &scheduler};
    const LV2_Feature* features[] = {&mapFeature, &optionFeature, &workerFeature, nullptr};
    const auto bundle = std::filesystem::path(argv[1]).parent_path().generic_string() + "/";
    auto instance = api->instantiate(api, 48000, bundle.c_str(), features);
    check(instance != nullptr, "DAF LV2 instantiates with URID and block options");
    if (!instance) return 1;
    std::array<float, 512> in, left, right;
    in.fill(0.75f); left.fill(0.25f); right.fill(-0.5f);
    unsigned port = 0;
    if (!midiOnly)
    {
        api->connect_port(instance, port++, in.data()); api->connect_port(instance, port++, in.data());
        api->connect_port(instance, port++, left.data()); api->connect_port(instance, port++, right.data());
    }
    Buffer input, output;
    api->connect_port(instance, port++, &input.seq); api->connect_port(instance, port++, &output.seq);
    std::array<float, 10> controls {{0, 0, 2, 1, 1, 0, 0, 0, 0, 0}};
    for (auto& control : controls) api->connect_port(instance, port++, &control);
    api->activate(instance);
    const auto begin = [&] {
        input.seq.atom = {sizeof(LV2_Atom_Sequence_Body), mapper(LV2_ATOM__Sequence)};
        input.seq.body = {};
        output.seq.atom = {sizeof(Buffer) - sizeof(LV2_Atom), mapper(LV2_ATOM__Sequence)};
        output.seq.body = {};
    };
    begin();
    for (int i = 0; i < 3; ++i)
    {
        Packet packet;
        packet.event.time.frames = 3 + i * 7;
        packet.event.body = {3, mapper("http://lv2plug.in/ns/ext/midi#MidiEvent")};
        packet.bytes[0] = 0x90; packet.bytes[1] = i == 0 ? 60 : i == 1 ? 64 : 67; packet.bytes[2] = 100;
        check(lv2_atom_sequence_append_event(&input.seq, sizeof(Buffer) - sizeof(LV2_Atom), &packet.event) != nullptr,
              "fixture appends real MIDI atoms");
    }
    api->run(instance, 512);
    check(input.seq.atom.size == output.seq.atom.size &&
          std::memcmp(&input.seq.body, &output.seq.body, input.seq.atom.size) == 0,
          "DAF LV2 forwards all MIDI atoms with exact frame offsets");
    if (!midiOnly)
        check(std::all_of(left.begin(), left.end(), [](float x) {return x == 0;}) &&
              std::all_of(right.begin(), right.end(), [](float x) {return x == 0;}), "DAF LV2 instrument produces silence");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    begin();
    do
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        output.seq.atom.size = sizeof(Buffer) - sizeof(LV2_Atom);
        api->run(instance, 512);
    } while (controls[5] != 1 && std::chrono::steady_clock::now() < deadline);
    check(controls[5] == 1 && controls[6] == 1 && controls[7] == 1 && controls[8] == 1 && controls[9] > 0,
          "all detection outputs reach real LV2 control ports without an editor");
    const auto* state = static_cast<const LV2_State_Interface*>(api->extension_data(LV2_STATE__interface));
    check(state != nullptr, "LV2 exposes the complete input snapshot state interface");
    if (!state) {api->deactivate(instance); api->cleanup(instance); return 1;}
    const auto key = mapper((std::string(api->URI) + "#parameters").c_str());
    auto save = [&] {
        SavedState result;
        check(state->save(instance, SavedState::store, &result, 0, features) == LV2_STATE_SUCCESS, "LV2 host saves current state");
        return result;
    };
    auto text = [&](const SavedState& snapshot) {
        const auto it = snapshot.properties.find(key);
        if (it == snapshot.properties.end()) return std::string();
        const auto& bytes = it->second.bytes;
        return std::string(bytes.begin(), bytes.end() - (!bytes.empty() && bytes.back() == 0 ? 1 : 0));
    };
    const auto before = save();
    const auto original = text(before);
    check(before.properties.size() == 1 && original == duskaudio::chord::encodeInputState({{0,0,2,1,1}}),
          "populated detection outputs stay outside the only saved five-input snapshot");
    auto reject = [&](SavedState invalid) {
        check(state->restore(instance, SavedState::retrieve, &invalid, 0, features) != LV2_STATE_SUCCESS
              && text(save()) == original, "invalid LV2 state leaves all live controls unchanged");
    };
    reject({});
    auto invalid = before;
    invalid.properties[key].type = mapper(LV2_ATOM__Float); reject(invalid);
    invalid = before;
    invalid.properties[key].bytes.assign({'n','a','n',0}); reject(invalid);
    const auto savedText = duskaudio::chord::encodeInputState({{6,1,1,0,0}});
    auto accepted = before;
    accepted.properties[key].bytes.assign(savedText.begin(), savedText.end()); accepted.properties[key].bytes.push_back(0);
    check(state->restore(instance, SavedState::retrieve, &accepted, 0, features) == LV2_STATE_SUCCESS
          && text(save()) == savedText, "valid LV2 snapshot applies all five nondefault inputs");
    begin(); api->run(instance, 512);
    check(text(save()) == savedText, "unchanged stale host buffers cannot overwrite a complete recall");
    controls[0] = 4;
    begin(); api->run(instance, 512);
    check(text(save()) == duskaudio::chord::encodeInputState({{4,1,1,0,0}}),
          "new host automation still changes an individual input after recall");
    check(workerRequests == 0, "MIDI processing and direct snapshot restores need no worker jobs");
    api->deactivate(instance); api->activate(instance);
    begin(); api->run(instance, 512);
    check(std::all_of(controls.begin() + 5, controls.end(), [](float x) {return x == 0;}),
          "LV2 reactivation clears note and output state");
    api->deactivate(instance); api->cleanup(instance);
   #if defined(_WIN32)
    FreeLibrary(library);
   #else
    dlclose(library);
   #endif
    std::printf("DAF Chord Analyzer LV2 %s: %d checks, %d failures\n", midiOnly ? "MIDI" : "instrument", checks, failures);
    return failures == 0 ? 0 : 1;
}
