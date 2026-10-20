// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Exercise complete input recall through each built VST3's actual COM ABI.
#include "ChordAnalyzerState.hpp"
#include "travesty/audio_processor.h"
#include "travesty/component.h"
#include "travesty/edit_controller.h"
#include "travesty/factory.h"
#include "travesty/host.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>
#ifdef _WIN32
# define NOMINMAX
# include <windows.h>
#else
# include <dlfcn.h>
#endif

namespace {
using namespace duskaudio::chord;
int checks = 0, failures = 0;
void check(bool ok, const char* message)
{ ++checks; if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); } }
template<class T> T& object(void* self) { return **static_cast<T**>(self); }
uint32_t V3_API ref(void*) { return 1; }
// These host objects live on the stack and are pinned for the entire test.
v3_result query(void* self, const v3_tuid iid, void** out, const v3_tuid own)
{
    const bool match = v3_tuid_match(iid, own) || v3_tuid_match(iid, v3_funknown_iid);
    *out = match ? self : nullptr;
    return match ? V3_OK : V3_NO_INTERFACE;
}
struct Stream : v3_bstream_cpp {
    Stream* selfptr = this;
    std::vector<char> bytes;
    std::size_t position = 0;
    int32_t chunk = 3;
    bool failRead = false, failWrite = false, stallWrite = false;
    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;
    explicit Stream(std::vector<char> data = {}) : bytes(std::move(data))
    {
        query_interface = queryInterface;
        this->ref = this->unref = ::ref;
        stream.read = readBytes;
        stream.write = writeBytes;
        stream.seek = seek;
        stream.tell = tell;
    }
    static v3_result V3_API queryInterface(void* s, const v3_tuid iid, void** out)
    {
        return query(s, iid, out, v3_bstream_iid);
    }
    static v3_result V3_API readBytes(void* s, void* data, int32_t size, int32_t* read)
    {
        auto& x = object<Stream>(s); *read = 0;
        if (x.failRead) return V3_INTERNAL_ERR;
        const auto count = std::min({std::size_t(size), std::size_t(x.chunk), x.bytes.size() - x.position});
        if (count) std::memcpy(data, x.bytes.data() + x.position, count);
        x.position += count; *read = static_cast<int32_t>(count); return V3_OK;
    }
    static v3_result V3_API writeBytes(void* s, void* data, int32_t size, int32_t* written)
    {
        auto& x = object<Stream>(s); *written = 0;
        if (x.failWrite) return V3_INTERNAL_ERR;
        if (x.stallWrite) return V3_OK;
        const auto count = std::size_t(std::min(size, x.chunk));
        if (x.position + count > x.bytes.size()) x.bytes.resize(x.position + count);
        std::memcpy(x.bytes.data() + x.position, data, count);
        x.position += count; *written = static_cast<int32_t>(count); return V3_OK;
    }
    static v3_result V3_API seek(void* s, int64_t offset, int32_t mode, int64_t* result)
    {
        auto& x = object<Stream>(s);
        if (mode < V3_SEEK_SET || mode > V3_SEEK_END) return V3_INVALID_ARG;
        const auto base = mode == V3_SEEK_SET ? 0 : mode == V3_SEEK_CUR ? x.position : x.bytes.size();
        const int64_t next = static_cast<int64_t>(base) + offset;
        if (next < 0 || uint64_t(next) > x.bytes.size()) return V3_INVALID_ARG;
        x.position = static_cast<std::size_t>(next); if (result) *result = next; return V3_OK;
    }
    static v3_result V3_API tell(void* s, int64_t* result)
    {
        *result = static_cast<int64_t>(object<Stream>(s).position); return V3_OK;
    }
    v3_bstream** handle() { return reinterpret_cast<v3_bstream**>(&selfptr); }
};
struct Host : v3_host_application_cpp {
    Host* selfptr = this;
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;
    Host()
    {
        query_interface = queryInterface;
        this->ref = this->unref = ::ref;
        app.get_name = getName;
        app.create_instance = createInstance;
    }
    static v3_result V3_API queryInterface(void* s, const v3_tuid iid, void** out)
    {
        return query(s, iid, out, v3_host_application_iid);
    }
    static v3_result V3_API getName(void*, v3_str_128 name)
    {
        const char text[] = "ChordVst3StateTest";
        for (std::size_t i = 0; i < sizeof(text); ++i) name[i] = text[i];
        return V3_OK;
    }
    static v3_result V3_API createInstance(void*, v3_tuid, v3_tuid, void** out)
    {
        *out = nullptr; return V3_NOT_IMPLEMENTED;
    }
    v3_funknown** handle() { return reinterpret_cast<v3_funknown**>(&selfptr); }
};
struct Handler : v3_component_handler_cpp {
    Handler* selfptr = this;
    unsigned edits = 0;
    std::vector<int32_t> restarts;
    Handler(const Handler&) = delete;
    Handler& operator=(const Handler&) = delete;
    Handler()
    {
        query_interface = queryInterface;
        this->ref = this->unref = ::ref;
        comp.begin_edit = comp.end_edit = edit;
        comp.perform_edit = performEdit;
        comp.restart_component = restart;
    }
    static v3_result V3_API queryInterface(void* s, const v3_tuid iid, void** out)
    {
        return query(s, iid, out, v3_component_handler_iid);
    }
    static v3_result V3_API edit(void* s, v3_param_id)
    {
        ++object<Handler>(s).edits; return V3_OK;
    }
    static v3_result V3_API performEdit(void* s, v3_param_id, double)
    {
        ++object<Handler>(s).edits; return V3_OK;
    }
    static v3_result V3_API restart(void* s, int32_t flags)
    {
        object<Handler>(s).restarts.push_back(flags); return V3_OK;
    }
    v3_component_handler** handle() { return reinterpret_cast<v3_component_handler**>(&selfptr); }
};
struct Queue : v3_param_value_queue_cpp {
    Queue* selfptr = this;
    v3_param_id id = 0;
    double value = 0;
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;
    Queue()
    {
        query_interface = queryInterface;
        this->ref = this->unref = ::ref;
        queue.get_param_id = getId;
        queue.get_point_count = pointCount;
        queue.get_point = getPoint;
        queue.add_point = addPoint;
    }
    static v3_result V3_API queryInterface(void* s, const v3_tuid iid, void** out)
    {
        return query(s, iid, out, v3_param_value_queue_iid);
    }
    static v3_param_id V3_API getId(void* s) { return object<Queue>(s).id; }
    static int32_t V3_API pointCount(void*) { return 1; }
    static v3_result V3_API getPoint(void* s, int32_t index, int32_t* offset, double* value)
    {
        if (index != 0) return V3_INVALID_ARG;
        *offset = 0; *value = object<Queue>(s).value; return V3_OK;
    }
    static v3_result V3_API addPoint(void*, int32_t, double, int32_t*) { return V3_NOT_IMPLEMENTED; }
    v3_param_value_queue** handle() { return reinterpret_cast<v3_param_value_queue**>(&selfptr); }
};
struct Changes : v3_param_changes_cpp {
    Changes* selfptr = this;
    std::array<Queue, inputParameterCount> queues;
    Changes(const Changes&) = delete;
    Changes& operator=(const Changes&) = delete;
    Changes()
    {
        query_interface = queryInterface;
        this->ref = this->unref = ::ref;
        changes.get_param_count = paramCount;
        changes.get_param_data = getParam;
        changes.add_param_data = addParam;
    }
    static v3_result V3_API queryInterface(void* s, const v3_tuid iid, void** out)
    {
        return query(s, iid, out, v3_param_changes_iid);
    }
    static int32_t V3_API paramCount(void*) { return inputParameterCount; }
    static v3_param_value_queue** V3_API getParam(void* s, int32_t i)
    {
        return i >= 0 && unsigned(i) < inputParameterCount ? object<Changes>(s).queues[i].handle() : nullptr;
    }
    static v3_param_value_queue** V3_API addParam(void*, const v3_param_id*, int32_t*) { return nullptr; }
    v3_param_changes** handle() { return reinterpret_cast<v3_param_changes**>(&selfptr); }
};
void token(std::vector<char>& bytes, const std::string& text)
{ bytes.insert(bytes.end(), text.begin(), text.end()); bytes.push_back(0); }
std::vector<char> payload(const std::string& text, unsigned copies = 1)
{
    std::vector<char> bytes;
    token(bytes, "__daf_program__"); token(bytes, "999");
    token(bytes, "__daf_state_begin__");
    for (unsigned i = 0; i < copies; ++i) { token(bytes, "parameters"); token(bytes, text); }
    token(bytes, "__daf_state_end__");
    token(bytes, "__daf_parameters_begin__");
    token(bytes, "keyRoot"); token(bytes, "9");
    token(bytes, "keyMode"); token(bytes, "garbage");
    token(bytes, "__daf_parameters_end__"); bytes.push_back(char(0xfe)); bytes.push_back(0);
    return bytes;
}
std::string ascii(const v3_str_128 text)
{
    std::string out;
    for (int i = 0; i < 128 && text[i]; ++i) out += static_cast<char>(text[i]);
    return out;
}
}

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
#ifdef _WIN32
    const auto library = LoadLibraryA(argv[1]);
    const auto symbol = [library](const char* name) { return GetProcAddress(library, name); };
    const auto entry = reinterpret_cast<bool(*)()>(symbol("InitDll"));
    const auto leave = reinterpret_cast<bool(*)()>(symbol("ExitDll"));
#else
    void* const library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    const auto symbol = [library](const char* name) { return library ? dlsym(library, name) : nullptr; };
# ifdef __APPLE__
    const auto entry = reinterpret_cast<bool(*)(void*)>(symbol("bundleEntry"));
    const auto leave = reinterpret_cast<bool(*)()>(symbol("bundleExit"));
# else
    const auto entry = reinterpret_cast<bool(*)(void*)>(symbol("ModuleEntry"));
    const auto leave = reinterpret_cast<bool(*)()>(symbol("ModuleExit"));
# endif
#endif
    const auto getFactory = reinterpret_cast<const void*(*)()>(symbol("GetPluginFactory"));
    check(library && entry && leave && getFactory, "native VST3 library and exports");
    if (!library || !entry || !leave || !getFactory) return 1;
#ifdef _WIN32
    if (!entry()) return 1;
#else
    if (!entry(library)) return 1;
#endif
    auto** factory = static_cast<v3_plugin_factory**>(const_cast<void*>(getFactory()));
    check(factory != nullptr, "factory exists"); if (!factory) return 1;
    Host host;
    v3_plugin_factory_3** factory3 = nullptr;
    if (v3_cpp_obj_query_interface(factory, v3_plugin_factory_3_iid, &factory3) == V3_OK && factory3)
        check(v3_cpp_obj(factory3)->set_host_context(factory3, host.handle()) == V3_OK, "factory host context");
    v3_class_info info{};
    bool found = false;
    for (int i = 0; i < v3_cpp_obj(factory)->num_classes(factory); ++i)
        if (v3_cpp_obj(factory)->get_class_info(factory, i, &info) == V3_OK && !std::strcmp(info.category, "Audio Module Class"))
        { found = true; break; }
    check(found, "audio component class"); if (!found) return 1;
    v3_component** component = nullptr;
    check(v3_cpp_obj(factory)->create_instance(factory, info.class_id, v3_component_iid, reinterpret_cast<void**>(&component)) == V3_OK
        && component, "create actual component"); if (!component) return 1;
    check(v3_cpp_obj_initialize(component, host.handle()) == V3_OK, "initialize component");
    v3_edit_controller** controller = nullptr;
    v3_audio_processor** processor = nullptr;
    check(v3_cpp_obj_query_interface(component, v3_edit_controller_iid, &controller) == V3_OK && controller, "monolithic controller");
    check(v3_cpp_obj_query_interface(component, v3_audio_processor_iid, &processor) == V3_OK && processor, "audio processor");
    if (!controller || !processor) return 1;
    Handler handler;
    check(v3_cpp_obj(controller)->set_component_handler(controller, handler.handle()) == V3_OK, "host handler");
    std::array<v3_param_id, PARAM_COUNT> ids{};
    std::array<unsigned, PARAM_COUNT> seen{};
    unsigned visible = 0;
    for (int i = 0; i < v3_cpp_obj(controller)->get_parameter_count(controller); ++i)
    {
        v3_param_info parameter{};
        check(v3_cpp_obj(controller)->get_parameter_info(controller, i, &parameter) == V3_OK, "all native parameter metadata readable");
        check(!(parameter.flags & V3_PARAM_PROGRAM_CHANGE), "no synthetic factory-program control");
        if (parameter.flags & V3_PARAM_IS_HIDDEN) continue;
        ++visible;
        for (unsigned j = 0; j < PARAM_COUNT; ++j)
            if (ascii(parameter.title) == parameterSpecs[j].name)
            {
                ++seen[j]; ids[j] = parameter.param_id;
                check(bool(parameter.flags & V3_PARAM_READ_ONLY) == parameterSpecs[j].output, "only detection outputs are read-only");
                check(bool(parameter.flags & V3_PARAM_CAN_AUTOMATE) != parameterSpecs[j].output, "only five inputs are automatable");
                check(parameter.step_count == (j == PARAM_DETECTED_CHORD ? 0 : parameterSpecs[j].maximum),
                    "retained discrete domains and continuous encoded chord label");
            }
    }
    check(visible == PARAM_COUNT, "exactly ten visible controls");
    for (unsigned count : seen) check(count == 1, "every input and output enumerated exactly once");
    if (visible != PARAM_COUNT || std::any_of(seen.begin(), seen.end(), [](unsigned n) { return n != 1; })) return 1;
    const auto values = [&] {
        InputValues result{};
        for (unsigned i = 0; i < inputParameterCount; ++i)
            result[i] = static_cast<float>(v3_cpp_obj(controller)->normalised_parameter_to_plain(controller, ids[i],
                v3_cpp_obj(controller)->get_parameter_normalised(controller, ids[i])));
        return result;
    };
    const auto save = [&] {
        Stream output;
        check(v3_cpp_obj(component)->get_state(component, output.handle()) == V3_OK, "save succeeds with three-byte writes");
        return output.bytes;
    };
    const auto load = [&](const std::vector<char>& bytes) {
        Stream input(bytes);
        return v3_cpp_obj(component)->set_state(component, input.handle());
    };
    const InputValues defaults{{0,0,2,1,1}}, saved{{6,1,1,0,0}};
    check(values() == defaults, "native input defaults");
    const auto oldRestarts = handler.restarts.size();
    check(load(payload(encodeInputState(saved))) == V3_OK && values() == saved, "complete snapshot restores all inputs despite redundant bad values");
    check(handler.restarts.size() == oldRestarts + 1 && handler.restarts.back() == V3_RESTART_PARAM_VALUES_CHANGED,
        "one host value refresh after recall");
    const auto before = save();
    check(!before.empty(), "saved bytes populated");
    const std::string bytes(before.begin(), before.end());
    check(bytes.find(encodeInputState(saved)) != std::string::npos, "canonical saved snapshot");
    check(bytes.find("__daf_program__") == std::string::npos, "no invented program state");
    for (unsigned i = inputParameterCount; i < PARAM_COUNT; ++i)
        check(bytes.find(parameterSpecs[i].symbol) == std::string::npos, "all transient detection keys excluded");
    const auto rejected = [&](const std::vector<char>& invalid) {
        const auto previousRestarts = handler.restarts.size();
        check(load(invalid) != V3_OK, "malformed or incomplete native state rejected");
        check(values() == saved && handler.restarts.size() == previousRestarts, "rejection preserves every input and host mirror");
        check(save() == before, "rejection leaves actual saved state unchanged");
    };
    rejected({}); rejected(payload("", 0)); rejected(payload(encodeInputState(defaults), 2));
    rejected(payload("ChordAnalyzer2:2\n"));
    for (unsigned i = 0; i < inputParameterCount; ++i)
    {
        const auto text = encodeInputState(defaults), key = std::string(parameterSpecs[i].symbol) + "=";
        const auto begin = text.find(key), end = text.find('\n', begin);
        rejected(payload(text.substr(0, begin) + text.substr(end + 1)));
        rejected(payload(text + text.substr(begin, end + 1 - begin)));
        for (const char* value : {"nan", "0.5", "garbage", "-1", "999", "1junk"})
            rejected(payload(text.substr(0, begin) + key + value + text.substr(end)));
    }
    const auto valid = payload(encodeInputState(defaults));
    // The VST3 parser's terminator is 0xfe itself. Its writer also emits a NUL,
    // but that padding is optional here (unlike CLAP's whole-stream contract).
    for (std::size_t size = 0; size + 1 < valid.size(); ++size)
        rejected(std::vector<char>(valid.begin(), valid.begin() + size));
    Stream failedRead(valid); failedRead.failRead = true;
    check(v3_cpp_obj(component)->set_state(component, failedRead.handle()) != V3_OK && values() == saved, "failed stream read preserves inputs");
    Stream failedWrite; failedWrite.failWrite = true;
    check(v3_cpp_obj(component)->get_state(component, failedWrite.handle()) != V3_OK, "failed stream write reported");
    Stream stalledWrite; stalledWrite.stallWrite = true;
    check(v3_cpp_obj(component)->get_state(component, stalledWrite.handle()) != V3_OK, "non-progressing write rejected");
    check(load(std::vector<char>(valid.begin(), valid.end() - 1)) == V3_OK && values() == defaults,
        "complete VST3 state accepts its terminator without optional NUL padding");
    check(load(valid) == V3_OK && values() == defaults, "valid recall succeeds after rejection cases");
    check(load(before) == V3_OK && values() == saved && save() == before, "save/load/save is byte reproducible");

    v3_process_setup setup{V3_REALTIME, V3_SAMPLE_32, 64, 48000.0};
    check(v3_cpp_obj(processor)->setup_processing(processor, &setup) == V3_OK, "prepare parameter flush");
    check(v3_cpp_obj(component)->set_active(component, true) == V3_OK, "activate");
    check(v3_cpp_obj(processor)->set_processing(processor, true) == V3_OK, "start processing");
    Changes changes;
    for (unsigned i = 0; i < inputParameterCount; ++i)
    {
        changes.queues[i].id = ids[i];
        changes.queues[i].value = v3_cpp_obj(controller)->plain_parameter_to_normalised(controller, ids[i], defaults[i]);
    }
    v3_process_data data{};
    data.process_mode = V3_REALTIME; data.symbolic_sample_size = V3_SAMPLE_32; data.input_params = changes.handle();
    const auto beforeProcessRestarts = handler.restarts.size();
    check(v3_cpp_obj(processor)->process(processor, &data) == V3_OK, "zero-frame host automation flush");
    check(values() == defaults, "all five inputs remain automatable after snapshot restores");
    const auto automated = save();
    check(std::string(automated.begin(), automated.end()).find(encodeInputState(defaults)) != std::string::npos,
        "host automation updates the actual saved snapshot");
    check(handler.restarts.size() == beforeProcessRestarts && handler.edits == 0, "recall and flush create no undo edits or processing-thread restarts");
    check(v3_cpp_obj(processor)->set_processing(processor, false) == V3_OK, "stop processing");
    check(v3_cpp_obj(component)->set_active(component, false) == V3_OK, "deactivate");
    check(v3_cpp_obj(controller)->set_component_handler(controller, nullptr) == V3_OK, "disconnect host");
    v3_cpp_obj_unref(processor); v3_cpp_obj_unref(controller);
    check(v3_cpp_obj_terminate(component) == V3_OK, "terminate component"); v3_cpp_obj_unref(component);
    if (factory3) v3_cpp_obj_unref(factory3);
    v3_cpp_obj_unref(factory); check(leave(), "module shutdown");
#ifdef _WIN32
    FreeLibrary(library);
#else
    dlclose(library);
#endif
    std::printf("Successor VST3 state: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
