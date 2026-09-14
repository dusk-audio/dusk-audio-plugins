// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Standalone, real-binary CLAP host regression test for DuskVerb 2.

#include "../core/DuskVerbParamTable.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "DuskVerbTestLibrary.hpp"

#include "clap/entry.h"
#include "clap/events.h"
#include "clap/ext/audio-ports.h"
#include "clap/ext/params.h"
#include "clap/ext/state.h"
#include "clap/factory/plugin-factory.h"
#include "clap/plugin.h"
#include "clap/process.h"

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* message)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}

void checkNear(double actual, double expected, double tolerance, const char* message)
{
    ++checks;
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s (expected %.12g, observed %.12g, tolerance %.3g)\n",
                     message, expected, actual, tolerance);
    }
}

static void CLAP_ABI hostParamsRescan(const clap_host_t*, clap_param_rescan_flags) {}
static void CLAP_ABI hostParamsClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
static void CLAP_ABI hostParamsRequestFlush(const clap_host_t*) {}
static const clap_host_params_t hostParams = {
    hostParamsRescan, hostParamsClear, hostParamsRequestFlush
};

static const void* CLAP_ABI hostGetExtension(const clap_host_t*, const char* id)
{
    return id != nullptr && std::strcmp(id, CLAP_EXT_PARAMS) == 0 ? &hostParams : nullptr;
}
static void CLAP_ABI hostRequestRestart(const clap_host_t*) {}
static void CLAP_ABI hostRequestProcess(const clap_host_t*) {}
static void CLAP_ABI hostRequestCallback(const clap_host_t*) {}

static clap_host_t host = {
    CLAP_VERSION_INIT, nullptr,
    "DuskVerbClapTests", "Dusk Audio", "https://dusk-audio.github.io/", "1.0.0",
    hostGetExtension, hostRequestRestart, hostRequestProcess, hostRequestCallback
};

struct EmptyOutEvents {
    clap_output_events_t iface{};
    EmptyOutEvents()
    {
        iface.ctx = this;
        iface.try_push = [](const clap_output_events_t*, const clap_event_header_t*) { return true; };
    }
};

struct ParamEvents {
    clap_input_events_t iface{};
    std::vector<clap_event_param_value_t> events;

    ParamEvents()
    {
        iface.ctx = this;
        iface.size = [](const clap_input_events_t* list) {
            return static_cast<uint32_t>(static_cast<const ParamEvents*>(list->ctx)->events.size());
        };
        iface.get = [](const clap_input_events_t* list, uint32_t index) -> const clap_event_header_t* {
            const auto& e = static_cast<const ParamEvents*>(list->ctx)->events;
            return index < e.size() ? &e[index].header : nullptr;
        };
    }

    void add(clap_id id, double value, uint32_t time = 0)
    {
        clap_event_param_value_t event{};
        event.header.size = sizeof(event);
        event.header.time = time;
        event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        event.header.type = CLAP_EVENT_PARAM_VALUE;
        event.param_id = id;
        event.note_id = -1;
        event.port_index = -1;
        event.channel = -1;
        event.key = -1;
        event.value = value;
        events.push_back(event);
    }
};

struct ByteWriter {
    clap_ostream_t iface{};
    std::vector<uint8_t> bytes;
    ByteWriter()
    {
        iface.ctx = this;
        iface.write = [](const clap_ostream_t* stream, const void* buffer, uint64_t size) -> int64_t {
            auto* self = static_cast<ByteWriter*>(stream->ctx);
            const auto* begin = static_cast<const uint8_t*>(buffer);
            self->bytes.insert(self->bytes.end(), begin, begin + size);
            return static_cast<int64_t>(size);
        };
    }
};

struct ByteReader {
    clap_istream_t iface{};
    const std::vector<uint8_t>& bytes;
    size_t offset = 0;
    size_t maxChunk;
    explicit ByteReader(const std::vector<uint8_t>& data, size_t chunk = 511) : bytes(data), maxChunk(chunk)
    {
        iface.ctx = this;
        iface.read = [](const clap_istream_t* stream, void* buffer, uint64_t size) -> int64_t {
            auto* self = static_cast<ByteReader*>(stream->ctx);
            const size_t count = std::min<size_t>(std::min<size_t>(size, self->maxChunk), self->bytes.size() - self->offset);
            if (count != 0)
                std::memcpy(buffer, self->bytes.data() + self->offset, count);
            self->offset += count;
            return static_cast<int64_t>(count);
        };
    }
};

struct Library {
    void* handle = nullptr;
    const clap_plugin_entry_t* entry = nullptr;
    const clap_plugin_factory_t* factory = nullptr;

    explicit Library(const char* path)
    {
        handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (handle == nullptr) return;
        entry = static_cast<const clap_plugin_entry_t*>(dlsym(handle, "clap_entry"));
        if (entry == nullptr || !entry->init(path)) { entry = nullptr; return; }
        factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    }
    ~Library()
    {
        if (entry != nullptr) entry->deinit();
        if (handle != nullptr) dlclose(handle);
    }
};

struct Instance {
    const clap_plugin_t* plugin = nullptr;
    const clap_plugin_params_t* params = nullptr;
    const clap_plugin_state_t* state = nullptr;
    const clap_plugin_audio_ports_t* ports = nullptr;
    bool active = false;
    bool processing = false;

    Instance() = default;
    explicit Instance(const clap_plugin_factory_t* factory)
    {
        const auto* desc = factory != nullptr && factory->get_plugin_count(factory) != 0
                         ? factory->get_plugin_descriptor(factory, 0) : nullptr;
        plugin = desc != nullptr ? factory->create_plugin(factory, &host, desc->id) : nullptr;
        if (plugin == nullptr) return;
        if (!plugin->init(plugin))
        {
            plugin->destroy(plugin);
            plugin = nullptr;
            return;
        }
        params = static_cast<const clap_plugin_params_t*>(plugin->get_extension(plugin, CLAP_EXT_PARAMS));
        state = static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin, CLAP_EXT_STATE));
        ports = static_cast<const clap_plugin_audio_ports_t*>(plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    }
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;
    ~Instance()
    {
        if (plugin == nullptr) return;
        if (processing) plugin->stop_processing(plugin);
        if (active) plugin->deactivate(plugin);
        plugin->destroy(plugin);
    }

    bool activate(double rate = 48000.0, uint32_t maximum = 512)
    {
        active = plugin != nullptr && plugin->activate(plugin, rate, 1, maximum);
        processing = active && plugin->start_processing(plugin);
        return processing;
    }
    void deactivate()
    {
        if (processing) { plugin->stop_processing(plugin); processing = false; }
        if (active) { plugin->deactivate(plugin); active = false; }
    }
    void set(clap_id id, double value)
    {
        ParamEvents in;
        EmptyOutEvents out;
        in.add(id, value);
        params->flush(plugin, &in.iface, &out.iface);
    }
    double get(clap_id id) const
    {
        double value = std::numeric_limits<double>::quiet_NaN();
        params->get_value(plugin, id, &value);
        return value;
    }
    std::vector<uint8_t> save() const
    {
        ByteWriter writer;
        if (!state->save(plugin, &writer.iface)) writer.bytes.clear();
        return writer.bytes;
    }
    bool load(const std::vector<uint8_t>& bytes, size_t chunk = 511)
    {
        ByteReader reader(bytes, chunk);
        return state->load(plugin, &reader.iface);
    }
};

struct ParamMeta {
    clap_param_info_t info{};
    std::string symbol;
};

std::vector<ParamMeta> readParams(const Instance& instance)
{
    std::vector<ParamMeta> result;
    if (instance.params == nullptr) return result;
    const uint32_t count = instance.params->count(instance.plugin);
    result.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        ParamMeta p;
        if (!instance.params->get_info(instance.plugin, i, &p.info)) continue;
        const char* slash = std::strrchr(p.info.module, '/');
        p.symbol = slash != nullptr ? slash + 1 : p.info.module;
        result.push_back(p);
    }
    return result;
}

void parameterContract(Instance& instance, const std::vector<ParamMeta>& metadata)
{
    check(metadata.size() == 92, "CLAP exposes exactly 92 parameters");
    std::set<clap_id> ids;
    for (size_t i = 0; i < metadata.size(); ++i)
    {
        ids.insert(metadata[i].info.id);
        if (i < static_cast<size_t>(duskverb::kNumParams))
        {
            check(metadata[i].info.id == i, "CLAP parameter ID equals stable table index");
            const bool designatedBypass = i == static_cast<size_t>(duskverb::Bypass)
                                       && metadata[i].symbol == "daf_bypass";
            if (metadata[i].symbol != duskverb::paramDesc(static_cast<int>(i)).id && !designatedBypass)
            {
                ++checks;
                ++failures;
                std::fprintf(stderr,
                    "FAIL: CLAP parameter %zu order/symbol mismatch (expected '%s', observed module '%s')\n",
                    i, duskverb::paramDesc(static_cast<int>(i)).id, metadata[i].info.module);
            }
            else ++checks;
            check(std::strcmp(metadata[i].info.name,
                              duskverb::paramDesc(static_cast<int>(i)).name) == 0,
                  "CLAP parameter order/name matches DuskVerb table");
            const auto& desc = duskverb::paramDesc(static_cast<int>(i));
            if (duskverb::hasSkew(desc))
            {
                checkNear(metadata[i].info.min_value, 0.0, 0.0, "nonlinear CLAP parameter minimum is normalized");
                checkNear(metadata[i].info.max_value, 1.0, 0.0, "nonlinear CLAP parameter maximum is normalized");
            }
        }
    }
    check(ids.size() == metadata.size(), "CLAP parameter IDs are unique");

    char text[128]{};
    check(instance.params->value_to_text(instance.plugin, duskverb::Decay, 0.5, text, sizeof(text)),
          "decay 0.5 has CLAP text");
    double displayedDecay = 0.0;
    std::sscanf(text, "%lf", &displayedDecay);
    checkNear(displayedDecay, 5.47, 0.02, "decay 0.5 displays about 5.47 seconds");

    double parsed = -99.0;
    check(instance.params->text_to_value(instance.plugin, duskverb::Mix, "50%", &parsed),
          "CLAP parses Mix 50%");
    checkNear(parsed, 0.5, 1e-6, "Mix 50% maps to host value 0.5");

    instance.set(duskverb::Algorithm, 7.0);
    parsed = -99.0;
    check(instance.params->text_to_value(instance.plugin, duskverb::ModDepth, "12 st", &parsed),
          "CLAP parses Shimmer pitch 12 st");
    checkNear(parsed, 0.5, 1e-6, "Shimmer 12 st maps to depth 0.5");

    instance.set(duskverb::Algorithm, 6.0);
    parsed = -99.0;
    check(instance.params->text_to_value(instance.plugin, duskverb::ModDepth, "25 ms", &parsed),
          "CLAP parses Gated attack 25 ms");
    checkNear(parsed, 0.49, 1e-6, "Gated attack 25 ms maps to snapped depth 0.49");

    instance.set(duskverb::Algorithm, 0.0);
    for (size_t i = 0; i < metadata.size() && i < static_cast<size_t>(duskverb::kNumParams); ++i)
    {
        const auto& desc = duskverb::paramDesc(static_cast<int>(i));
        if (desc.enumLabels != nullptr) continue;
        const std::array<double, 3> values = {
            metadata[i].info.min_value,
            (metadata[i].info.min_value + metadata[i].info.max_value) * 0.5,
            metadata[i].info.max_value
        };
        for (double value : values)
        {
            std::memset(text, 0, sizeof(text));
            const bool formatted = instance.params->value_to_text(
                instance.plugin, metadata[i].info.id, value, text, sizeof(text));
            if (!formatted)
            {
                ++checks; ++failures;
                std::fprintf(stderr, "FAIL: custom text %s formats %.9g\n", desc.id, value);
                continue;
            }
            ++checks;
            double roundTrip = -12345.0;
            const bool parsedText = instance.params->text_to_value(
                instance.plugin, metadata[i].info.id, text, &roundTrip);
            if (!parsedText)
            {
                ++checks; ++failures;
                std::fprintf(stderr, "FAIL: custom text %s cannot parse its own '%s' for %.9g\n",
                             desc.id, text, value);
            }
            else
            {
                ++checks;
                if (!std::isfinite(roundTrip) || roundTrip < metadata[i].info.min_value - 1e-6
                    || roundTrip > metadata[i].info.max_value + 1e-6)
                {
                    ++checks; ++failures;
                    std::fprintf(stderr,
                        "FAIL: custom text %s roundtrip '%s' produced out-of-range %.9g\n",
                        desc.id, text, roundTrip);
                }
                else ++checks;
            }
        }
        for (const char* bad : { "junk", "NaN", "Inf", "-Inf" })
        {
            double sentinel = 987654.25;
            const bool accepted = instance.params->text_to_value(
                instance.plugin, metadata[i].info.id, bad, &sentinel);
            if (accepted || sentinel != 987654.25)
            {
                checks += 2; failures += 2;
                std::fprintf(stderr,
                    "FAIL: custom text %s accepted '%s'=%d or overwrote sentinel with %.9g\n",
                    desc.id, bad, accepted ? 1 : 0, sentinel);
            }
            else checks += 2;
        }
    }

    for (size_t i = 0; i < metadata.size() && i < static_cast<size_t>(duskverb::kNumParams); ++i)
    {
        const auto& desc = duskverb::paramDesc(static_cast<int>(i));
        for (const double invalid : {std::numeric_limits<double>::quiet_NaN(),
                                     std::numeric_limits<double>::infinity(),
                                    -std::numeric_limits<double>::infinity()})
        {
            instance.set(metadata[i].info.id, invalid);
            checkNear(instance.get(metadata[i].info.id), duskverb::hostDefault(desc), 2e-6,
                      "invalid host numbers fall back to the parameter default");
        }
        if (!desc.integer && desc.interval <= 0.0f) continue;
        const double sent = metadata[i].info.min_value
                          + (metadata[i].info.max_value - metadata[i].info.min_value) * 0.4137;
        instance.set(metadata[i].info.id, sent);
        const double expected = duskverb::snapHostValue(desc, static_cast<float>(sent));
        checkNear(instance.get(metadata[i].info.id), expected, 2e-6,
                  "integer/interval host setter snaps on CLAP readback");
    }
}

bool locateCustomPayload(const std::vector<uint8_t>& state, size_t& begin, size_t& end)
{
    static constexpr char key[] = "parameters";
    for (size_t i = 0; i + sizeof(key) < state.size(); ++i)
    {
        if (std::memcmp(state.data() + i, key, sizeof(key)) != 0) continue;
        begin = i + sizeof(key);
        end = begin;
        while (end < state.size() && state[end] != 0) ++end;
        return end < state.size();
    }
    return false;
}

std::string customPayload(const std::vector<uint8_t>& state)
{
    size_t begin = 0, end = 0;
    return locateCustomPayload(state, begin, end)
         ? std::string(reinterpret_cast<const char*>(state.data() + begin), end - begin) : std::string();
}

std::vector<uint8_t> replaceCustomPayload(const std::vector<uint8_t>& state, const std::string& payload)
{
    size_t begin = 0, end = 0;
    if (!locateCustomPayload(state, begin, end)) return {};
    std::vector<uint8_t> result;
    result.reserve(state.size() + payload.size());
    result.insert(result.end(), state.begin(), state.begin() + static_cast<std::ptrdiff_t>(begin));
    result.insert(result.end(), payload.begin(), payload.end());
    result.insert(result.end(), state.begin() + static_cast<std::ptrdiff_t>(end), state.end());
    return result;
}

bool replaceToken(std::string& payload, std::string_view key, std::string_view value)
{
    const std::string marker = ";" + std::string(key) + "=";
    const size_t begin = payload.find(marker);
    if (begin == std::string::npos) return false;
    const size_t valueBegin = begin + marker.size();
    const size_t end = payload.find(';', valueBegin);
    payload.replace(valueBegin, end == std::string::npos ? payload.size() - valueBegin : end - valueBegin, value);
    return true;
}

std::pair<std::string, std::string> firstDifferentToken(const std::string& before,
                                                        const std::string& after)
{
    size_t a = 0, b = 0;
    while (a <= before.size() || b <= after.size())
    {
        const size_t ae = before.find(';', a), be = after.find(';', b);
        const std::string at = a > before.size() ? std::string()
            : before.substr(a, ae == std::string::npos ? std::string::npos : ae - a);
        const std::string bt = b > after.size() ? std::string()
            : after.substr(b, be == std::string::npos ? std::string::npos : be - b);
        if (at != bt) return {at, bt};
        if (ae == std::string::npos && be == std::string::npos) break;
        a = ae == std::string::npos ? before.size() + 1 : ae + 1;
        b = be == std::string::npos ? after.size() + 1 : be + 1;
    }
    return {"<none>", "<none>"};
}

void stateContract(Instance& instance)
{
    check(instance.state != nullptr, "plugin exposes clap.state");
    if (instance.state == nullptr) return;

    instance.set(duskverb::Mix, 0.37);
    instance.set(duskverb::Algorithm, 7.0);
    const auto original = instance.save();
    check(!original.empty(), "CLAP state saves bytes");
    std::string payload = customPayload(original);
    check(payload.rfind("v=2;", 0) == 0, "actual DAF state container contains DuskVerb v2 payload");
    check(replaceToken(payload, "@sixap_density_baseline", "3e4ccccd"), "state has SixAP density key");
    check(replaceToken(payload, "@sixap_output_trim", "3f4ccccd"), "state has SixAP trim key");
    const auto custom = replaceCustomPayload(original, payload);
    check(!custom.empty() && instance.load(custom), "actual CLAP stream loads complete custom v2 state");
    const std::string loadedPayload = customPayload(instance.save());
    check(loadedPayload.find(";@sixap_density_baseline=3e4ccccd") != std::string::npos,
          "SixAP density 0.2 roundtrips through hosted CLAP state");
    check(loadedPayload.find(";@sixap_output_trim=3f4ccccd") != std::string::npos,
          "SixAP trim 0.8 roundtrips through hosted CLAP state");
    checkNear(instance.get(duskverb::Mix), 0.37, 1e-6, "parameter survives hosted v2 state roundtrip");
    checkNear(instance.get(duskverb::Algorithm), 7.0, 0.0, "integer parameter survives hosted v2 state roundtrip");

    const auto postValid = instance.save();
    check(instance.load(postValid), "clean hosted CLAP state reload succeeds");
    const auto baseline = instance.save();
    check(baseline == postValid, "complete hosted CLAP state is stable across save/load/save");
    auto trailing = baseline;
    trailing.push_back(0x7f);
    check(!instance.load(trailing), "trailing bytes after CLAP state terminator are rejected");
    check(instance.save() == baseline, "trailing garbage rejection preserves the complete sound");
    for (size_t chunk : {size_t(1), size_t(2), size_t(7), size_t(511)})
    {
        check(instance.load(baseline, chunk), "valid CLAP state supports short stream reads");
        check(!instance.load(trailing, chunk), "trailing garbage is rejected across stream chunk boundaries");
        check(instance.save() == baseline, "short-read validation preserves complete state");
    }
    const std::string valid = customPayload(baseline);
    std::vector<std::pair<const char*, std::string>> bad;
    bad.emplace_back("unknown key", valid + ";@unknown=1");
    {
        std::string missing = valid;
        const size_t b = missing.find(";mix=");
        const size_t e = b == std::string::npos ? b : missing.find(';', b + 1);
        if (b != std::string::npos) missing.erase(b, e == std::string::npos ? std::string::npos : e - b);
        bad.emplace_back("missing key", std::move(missing));
    }
    {
        std::string duplicate = valid;
        const size_t b = duplicate.find(";mix=");
        const size_t e = b == std::string::npos ? b : duplicate.find(';', b + 1);
        if (b != std::string::npos) duplicate += duplicate.substr(b, e - b);
        bad.emplace_back("duplicate key", std::move(duplicate));
    }
    {
        std::string nonfinite = valid;
        replaceToken(nonfinite, "mix", "7f800000");
        bad.emplace_back("nonfinite value", std::move(nonfinite));
    }
    bad.emplace_back("truncated payload", valid.substr(0, valid.size() - 1));

    for (const auto& mutation : bad)
    {
        check(instance.load(baseline), "restore clean baseline before malformed-state probe");
        const auto before = instance.save();
        const auto bytes = replaceCustomPayload(baseline, mutation.second);
        check(!bytes.empty(), "construct malformed payload inside actual CLAP wrapper");
        // Either return value is permitted here: a wrapper may reject the whole
        // stream or accept its structure while the plugin rejects the inner
        // value. The regression contract is that no hosted state changes.
        (void)instance.load(bytes);
        const auto after = instance.save();
        if (after != before)
        {
            ++failures;
            ++checks;
            const std::string beforePayload = customPayload(before);
            const std::string afterPayload = customPayload(after);
            const bool editedOnly = afterPayload == beforePayload + ";@edited=1";
            const auto difference = firstDifferentToken(beforePayload, afterPayload);
            std::fprintf(stderr,
                "FAIL: malformed %s changed full hosted state (before %zu bytes, after %zu; %s; "
                "first token expected '%s', observed '%s')\n",
                mutation.first, before.size(), after.size(),
                editedOnly ? "custom payload gained @edited=1" : "serialized state differs beyond @edited",
                difference.first.c_str(), difference.second.c_str());
        }
        else
        {
            ++checks;
        }
    }

    std::vector<uint8_t> wrapperTruncated = baseline;
    wrapperTruncated.resize(wrapperTruncated.size() - 2);
    check(!instance.load(wrapperTruncated), "truncated actual CLAP wrapper is rejected");
    check(instance.save() == baseline, "truncated actual CLAP wrapper leaves full state unchanged");
}

struct RenderResult {
    std::vector<float> left;
    std::vector<float> right;
    bool valid = true;
};

RenderResult render(const clap_plugin_factory_t* factory, const clap_event_transport_t* transport,
                    bool bypass = false, bool exerciseFrames = false, bool reprepare = false)
{
    Instance instance(factory);
    RenderResult result;
    if (instance.plugin == nullptr || instance.params == nullptr)
    {
        result.valid = false;
        return result;
    }
    instance.set(duskverb::Mix, bypass ? 0.5 : 1.0);
    instance.set(duskverb::PredelaySync, 3.0); // 1/8 note
    instance.set(duskverb::Bypass, bypass ? 1.0 : 0.0);
    if (!instance.activate(48000.0, 512)) { result.valid = false; return result; }

    EmptyOutEvents outEvents;
    ParamEvents inEvents;
    int64_t steady = 0;
    auto block = [&](uint32_t frames, bool impulse, bool compareInput) {
        std::vector<float> inL(frames, 0.0f), inR(frames, 0.0f);
        std::vector<float> outL(frames, -777.0f), outR(frames, -777.0f);
        if (impulse && frames != 0) inL[0] = inR[0] = 0.25f;
        if (compareInput)
            for (uint32_t i = 0; i < frames; ++i) inL[i] = inR[i] = float((int(i % 19) - 9) * 0.013);
        float* inChannels[] = { inL.data(), inR.data() };
        float* outChannels[] = { outL.data(), outR.data() };
        clap_audio_buffer_t input{};
        input.data32 = inChannels;
        input.channel_count = 2;
        clap_audio_buffer_t output{};
        output.data32 = outChannels;
        output.channel_count = 2;
        clap_process_t process{};
        process.steady_time = steady;
        process.frames_count = frames;
        process.transport = transport;
        process.audio_inputs = &input;
        process.audio_outputs = &output;
        process.audio_inputs_count = 1;
        process.audio_outputs_count = 1;
        process.in_events = &inEvents.iface;
        process.out_events = &outEvents.iface;
        const auto status = instance.plugin->process(instance.plugin, &process);
        if (status == CLAP_PROCESS_ERROR) result.valid = false;
        steady += frames;
        for (uint32_t i = 0; i < frames; ++i)
        {
            if (!std::isfinite(outL[i]) || !std::isfinite(outR[i])) result.valid = false;
            if (compareInput && (outL[i] != inL[i] || outR[i] != inR[i])) result.valid = false;
        }
        result.left.insert(result.left.end(), outL.begin(), outL.end());
        result.right.insert(result.right.end(), outR.begin(), outR.end());
    };

    if (exerciseFrames) { block(0, false, false); block(1, true, false); block(17, false, false); }
    if (bypass)
    {
        for (int i = 0; i < 48; ++i) block(128, false, false);
        result.left.clear(); result.right.clear();
        block(257, false, true);
    }
    else
    {
        bool first = true;
        for (int i = 0; i < 420; ++i)
        {
            const uint32_t frames = exerciseFrames ? std::array<uint32_t, 4>{64, 127, 256, 511}[i % 4] : 128;
            block(frames, first, false);
            first = false;
        }
    }
    if (reprepare)
    {
        instance.deactivate();
        check(instance.activate(44100.0, 257), "CLAP instance reactivates with new rate/block maximum");
        result.left.clear(); result.right.clear();
        block(257, true, false);
        for (int i = 0; i < 80; ++i) block(257, false, false);
    }
    return result;
}

double rms(const RenderResult& r)
{
    long double sum = 0.0;
    for (float v : r.left) sum += static_cast<long double>(v) * v;
    for (float v : r.right) sum += static_cast<long double>(v) * v;
    const size_t count = r.left.size() + r.right.size();
    return count == 0 ? 0.0 : std::sqrt(static_cast<double>(sum / count));
}

double maxDifference(const RenderResult& a, const RenderResult& b)
{
    if (a.left.size() != b.left.size() || a.right.size() != b.right.size())
        return std::numeric_limits<double>::infinity();
    double maximum = 0.0;
    for (size_t i = 0; i < a.left.size(); ++i) maximum = std::max(maximum, std::abs(double(a.left[i] - b.left[i])));
    for (size_t i = 0; i < a.right.size(); ++i) maximum = std::max(maximum, std::abs(double(a.right[i] - b.right[i])));
    return maximum;
}

void audioContract(const clap_plugin_factory_t* factory, const Instance& probe)
{
    check(probe.ports != nullptr, "plugin exposes clap.audio-ports");
    if (probe.ports != nullptr)
    {
        clap_audio_port_info_t input{}, output{};
        check(probe.ports->count(probe.plugin, true) == 1 && probe.ports->count(probe.plugin, false) == 1,
              "CLAP has one main input and output bus");
        check(probe.ports->get(probe.plugin, 0, true, &input)
              && probe.ports->get(probe.plugin, 0, false, &output), "CLAP main audio ports are queryable");
        check(input.channel_count == 2 && output.channel_count == 2, "default CLAP port configuration is 2/2");
    }

    // Neither the consolidated CLAP headers nor the current DAF wrapper define an
    // audio-ports-config API. Query the published extension ID anyway so absence is
    // an explicit hosted regression result, never simulated by changing buffer counts.
    const void* configs = probe.plugin->get_extension(probe.plugin, "clap.audio-ports-config");
    check(configs != nullptr, "CLAP audio-ports-config extension exists for real 1/1, 1/2 and 2/2 selection");

    const RenderResult varied = render(factory, nullptr, false, true, false);
    check(varied.valid, "zero/one/variable-frame real CLAP processing stays finite and succeeds");
    check(rms(varied) > 1e-7, "variable-frame render produces nonzero audio");

    const RenderResult bypass = render(factory, nullptr, true, false, false);
    check(bypass.valid, "bypass is sample-exact after settling");
    check(rms(bypass) > 1e-7, "bypass assertion used nonzero audio");

    const RenderResult reprepared = render(factory, nullptr, false, false, true);
    check(reprepared.valid, "reprepared CLAP render stays finite and succeeds");
    check(rms(reprepared) > 1e-7, "reprepared render produces nonzero audio");

    clap_event_transport_t tempo{};
    tempo.header.size = sizeof(tempo);
    tempo.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    tempo.header.type = CLAP_EVENT_TRANSPORT;
    tempo.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_IS_PLAYING;
    tempo.tempo = 120.0;

    clap_event_transport_t full = tempo;
    full.flags |= CLAP_TRANSPORT_HAS_BEATS_TIMELINE | CLAP_TRANSPORT_HAS_TIME_SIGNATURE;
    full.song_pos_beats = 0;
    full.tsig_num = 4;
    full.tsig_denom = 4;

    const RenderResult tempoOnly = render(factory, &tempo);
    const RenderResult fullBbt = render(factory, &full);
    const RenderResult absent = render(factory, nullptr);
    check(tempoOnly.valid && fullBbt.valid && absent.valid, "transport comparison renders are valid and finite");
    check(rms(tempoOnly) > 1e-7 && rms(fullBbt) > 1e-7 && rms(absent) > 1e-7,
          "transport comparisons use nonzero rendered audio");
    checkNear(maxDifference(tempoOnly, fullBbt), 0.0, 0.0,
              "tempo-only and full-BBT at the same BPM render identically");
    check(maxDifference(absent, tempoOnly) > 1e-7,
          "absent-tempo fallback differs from explicit 120 BPM with synced predelay");

    Instance processEvent(factory);
    check(processEvent.activate(48000.0, 64), "instance activates for process-event parameter probe");
    if (processEvent.processing)
    {
        ParamEvents inEvents;
        inEvents.add(duskverb::Mix, 0.23, 0);
        EmptyOutEvents outEvents;
        std::array<float, 1> inL{}, inR{}, outL{}, outR{};
        float* inChannels[] = { inL.data(), inR.data() };
        float* outChannels[] = { outL.data(), outR.data() };
        clap_audio_buffer_t input{}, output{};
        input.data32 = inChannels; input.channel_count = 2;
        output.data32 = outChannels; output.channel_count = 2;
        clap_process_t process{};
        process.steady_time = 0; process.frames_count = 1;
        process.audio_inputs = &input; process.audio_outputs = &output;
        process.audio_inputs_count = 1; process.audio_outputs_count = 1;
        process.in_events = &inEvents.iface; process.out_events = &outEvents.iface;
        check(processEvent.plugin->process(processEvent.plugin, &process) != CLAP_PROCESS_ERROR,
              "parameter event is accepted through clap_plugin.process");
        checkNear(processEvent.get(duskverb::Mix), 0.23, 1e-6,
                  "process-event parameter update is visible on hosted readback");
    }
}

} // namespace

int main(int argc, char** argv)
{
    check(firstDifferentToken("a", "a;;b") == std::make_pair(std::string(), std::string("b")),
          "token diagnostics handle exhausted before payload");
    check(firstDifferentToken("a;;b", "a") == std::make_pair(std::string("b"), std::string()),
          "token diagnostics handle exhausted after payload");
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: %s <duskverb-2.clap>\n", argv[0]);
        return 2;
    }

    Library library(argv[1]);
    if (library.handle == nullptr)
    {
        std::fprintf(stderr, "FAIL: dlopen(%s): %s\n", argv[1], dlerror());
        return 1;
    }
    if (library.entry == nullptr || library.factory == nullptr)
    {
        std::fprintf(stderr, "FAIL: no usable clap_entry/plugin factory in %s\n", argv[1]);
        return 1;
    }

    Instance probe(library.factory);
    if (probe.plugin == nullptr || probe.params == nullptr || probe.state == nullptr)
    {
        std::fprintf(stderr, "FAIL: candidate does not create/init with params and state extensions\n");
        return 1;
    }

    const auto metadata = readParams(probe);
    parameterContract(probe, metadata);
    stateContract(probe);
    audioContract(library.factory, probe);

    if (failures != 0)
    {
        std::fprintf(stderr, "%d/%d hosted CLAP checks failed\n", failures, checks);
        return 1;
    }
    std::printf("PASS: %d hosted CLAP checks\n", checks);
    return 0;
}
