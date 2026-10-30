// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Restores Multi-Comp 2 sessions through the built VST3, CLAP and AU the way each host format restores one: the
// wrapper's own saved form, which carries the "parameters" state and, after it, the wrapper's parameter list (and a
// program), into a fresh instance.
//
// The case that matters is a session saved before FET BLUE: in mode 4, then a generic Studio FET, with FET controls
// away from their defaults. Both released alphas wrote state version 6. Version 7 (Studio VCA's Input, Auto Release
// and meter) never shipped; version 8 added FET BLUE. decodeState() reopens an older state saved in mode 4 in FET
// (mode 1), which reads every fet_* control it set, while the parameter list saved after it still says mode 4. The
// state is the plugin's complete parameter snapshot, so it decides, in every format:
//   [1] a version-6 (alpha) or version-7 session in mode 4 reads mode 1 (FET) and the FET Ratio, Curve and Transient
//       it saved;
//   [2] a version-8 session in mode 4 (FET BLUE) still reads mode 4;
//   [3] a version-7 session in another mode (VCA) keeps it.
// Each session is made as the build that wrote it made it: the controls set through the host API, the wrapper's own
// saved form taken, and only its state string rewritten to the older version: "v=7" without the three fet_blue_* keys
// version 8 added, "v=6" without those and the three Studio VCA keys version 7 added. The parameter list stays as the
// wrapper wrote it. Every reading comes through the host API, and the restored instance's state, saved again, must
// decode to the same mode.
//
// Two more restores:
//   [4] Program recall. Over a snapshot the wrappers do not replay the saved program: they ask the plugin which
//       factory program its controls read (getCurrentProgram()). A session saved right after loading factory program N
//       through the host API reopens with the host reading program N, number and name (VST3: the program parameter;
//       AU: kAudioUnitProperty_PresentPreset). One saved after Mix moved off the program's 100 % reopens on no factory
//       program: the host reads what it read before the restore (AU -1; the VST3 program parameter has no such value
//       and keeps the one it had). CLAP offers a host no program, so it is skipped there.
//   [5] A newer build's session: its state claims version kStateVersion + 1 (otherwise valid) and its parameter list
//       carries non-default values. This build cannot decode that state, and a state it cannot decode is refused
//       whole (a half-applied session is worse than a refused one): the restore fails and the instance stays as it
//       was, every control and its saved state as before the attempt.
//
//   MultiCompSessionRestoreTest --vst3 <binary inside multi-comp-2.vst3>
//   MultiCompSessionRestoreTest --clap <binary inside multi-comp-2.clap>
//   MultiCompSessionRestoreTest --au <binary inside multi-comp-2.component>   (macOS)
//
// Linux and macOS only (dlopen); CMake registers what the platform can load.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <dlfcn.h>

#include "clap/entry.h"
#include "clap/ext/params.h"
#include "clap/ext/state.h"
#include "clap/factory/plugin-factory.h"
#include "clap/plugin.h"
#include "travesty/audio_processor.h"
#include "travesty/base.h"
#include "travesty/bstream.h"
#include "travesty/component.h"
#include "travesty/edit_controller.h"
#include "travesty/factory.h"
#include "travesty/host.h"

#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

#include "MultiCompParams.hpp"
#include "MultiCompProgramPresets.hpp"

namespace
{
using multicompp::ParamId;

int gFailures = 0;

void check(bool passed, const char* format, ...)
{
    char message[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    // Flushed line by line: the failures go to stderr, and the two streams must stay in order under ctest.
    std::FILE* const out = passed ? stdout : stderr;
    std::fprintf(out, "%s: %s\n", passed ? "PASS" : "FAIL", message);
    std::fflush(out);
    if (!passed) ++gFailures;
}

// One slot per DAF parameter index; only the controls' (multicompp::isControlParameter) are used.
constexpr size_t kParamSlots = static_cast<size_t>(multicompp::kTotalParamCount);

constexpr int countControls()
{
    int count = 0;
    for (int i = 0; i < multicompp::kTotalParamCount; i = multicompp::nextControlParameter(i)) ++count;
    return count;
}
constexpr int kControlCount = countControls();

constexpr int indexOf(ParamId id) { return static_cast<int>(id); }

// The controls each session sets and reads back: Mode, then FET Ratio All, Curve Measured and Transient 25 % (25
// is exact through every format's normalised and text forms).
constexpr std::array<ParamId, 4> kReadBack{{
    ParamId::Mode, ParamId::FetRatio, ParamId::FetCurve, ParamId::FetTransient}};
constexpr float kFetRatio = 4.0f, kFetCurve = 1.0f, kFetTransient = 25.0f;

struct Case
{
    const char* name;
    float mode;            // the mode the session was saved in
    int version;           // the state version it was saved with: 8 as this build writes it, 6 or 7 rewritten
    float expectedMode;    // what the restored instance must read
};
constexpr std::array<Case, 4> kCases{{
    {"version-6 session in mode 4 (Studio FET then, an alpha)", 4.0f, 6, 1.0f},
    {"version-7 session in mode 4 (Studio FET then)", 4.0f, 7, 1.0f},
    {"version-8 session in mode 4 (FET BLUE)", 4.0f, 8, 4.0f},
    {"version-7 session in mode 2 (VCA)", 2.0f, 7, 2.0f},
}};

// A control's host name (the VST3 title, the CLAP name), by DAF parameter index.
std::string hostNameOf(int index)
{
    return multicompp::resolveParameter(index,
        [](const multicompp::Param& d) { return std::string(d.name); },
        [](const multicompp::BandParam& d, int band) { return multicompp::bandHostName(d, band); });
}

// The control a host name belongs to (its DAF parameter index), or -1.
int controlNamed(const std::string& name)
{
    for (int i = 0; i < multicompp::kTotalParamCount; i = multicompp::nextControlParameter(i))
        if (hostNameOf(i) == name) return i;
    return -1;
}

// The host id of every control, matched by host name: complete when each control was found once (a name two controls
// shared would leave one of them unfound).
template <class Id>
struct ControlIds
{
    std::array<Id, kParamSlots> ids{};
    std::array<bool, kParamSlots> found{};
    int count = 0;

    void add(const std::string& name, Id id)
    {
        const int index = controlNamed(name);
        if (index < 0 || found[static_cast<size_t>(index)]) return;
        found[static_cast<size_t>(index)] = true;
        ids[static_cast<size_t>(index)] = id;
        ++count;
    }
    bool complete() const { return count == kControlCount; }
    Id operator[](int index) const { return ids[static_cast<size_t>(index)]; }
};

// The keys a state of `version` lacks: version 8 added FET BLUE's three (fet_blue_*), version 7 Studio VCA's Input,
// Auto Release and meter.
bool addedAfter(int version, const std::string& key)
{
    const bool fetBlue = key.compare(0, 9, "fet_blue_") == 0;
    const bool studioVca = key == "studio_vca_input" || key == "studio_vca_auto_release" || key == "studio_vca_meter";
    return (version < 8 && fetBlue) || (version < 7 && studioVca);
}

// The "parameters" state as a build that wrote `version` wrote it (6: both alphas; 7: never shipped): "v=<version>",
// without the keys it lacks.
bool toVersion(const std::string& state, int version, std::string& out)
{
    if (state.compare(0, 4, "v=8;") != 0 || version < 6 || version > 7) return false;
    out = "v=" + std::to_string(version);
    int dropped = 0;
    for (size_t begin = 4; begin <= state.size();)
    {
        const size_t end = std::min(state.find(';', begin), state.size());
        const std::string token = state.substr(begin, end - begin);
        if (addedAfter(version, token.substr(0, token.find('=')))) ++dropped;
        else out += ";" + token;
        begin = end + 1;
    }
    return dropped == (version == 7 ? 3 : 6);
}

// The mode a "parameters" state decodes to, -1 when it does not decode.
float decodedMode(const std::string& state)
{
    multicompp::StateValues values{};
    return multicompp::decodeState(state, values) ? values[static_cast<size_t>(ParamId::Mode)] : -1.0f;
}

// ---- the VST3 and CLAP saved form ----------------------------------------------------------------------------------
// NUL-separated tokens: "__daf_program__" and its number, then "__daf_state_begin__" key value ... "__daf_state_end__",
// then "__daf_parameters_begin__" symbol value ... "__daf_parameters_end__", then the 0xfe terminator.

std::vector<std::string> tokensOf(const std::string& bytes)
{
    std::vector<std::string> tokens;
    size_t begin = 0;
    for (size_t end; (end = bytes.find('\0', begin)) != std::string::npos; begin = end + 1)
        tokens.push_back(bytes.substr(begin, end - begin));
    tokens.push_back(bytes.substr(begin));
    return tokens;
}

std::string bytesOf(const std::vector<std::string>& tokens)
{
    std::string bytes;
    for (size_t i = 0; i < tokens.size(); ++i)
    {
        if (i != 0) bytes.push_back('\0');
        bytes += tokens[i];
    }
    return bytes;
}

// The index of `key`'s value inside the section that `begin` opens, or 0 when it has none.
size_t valueIndex(const std::vector<std::string>& tokens, const char* begin, const char* end, const char* key)
{
    const auto open = std::find(tokens.begin(), tokens.end(), begin);
    for (size_t i = static_cast<size_t>(open - tokens.begin()) + 1; open != tokens.end() && i + 1 < tokens.size()
         && tokens[i] != end; i += 2)
        if (tokens[i] == key) return i + 1;
    return 0;
}

struct TokenForm
{
    std::string bytes;

    std::string state() const
    {
        const auto tokens = tokensOf(bytes);
        const size_t i = valueIndex(tokens, "__daf_state_begin__", "__daf_state_end__", "parameters");
        return i != 0 ? tokens[i] : std::string();
    }
    float listedMode() const
    {
        const auto tokens = tokensOf(bytes);
        const size_t i = valueIndex(tokens, "__daf_parameters_begin__", "__daf_parameters_end__", "mode");
        return i != 0 ? static_cast<float>(std::atof(tokens[i].c_str())) : -1.0f;
    }
    TokenForm withState(const std::string& state) const
    {
        auto tokens = tokensOf(bytes);
        const size_t i = valueIndex(tokens, "__daf_state_begin__", "__daf_state_end__", "parameters");
        if (i != 0) tokens[i] = state;
        return {bytesOf(tokens)};
    }
};

// ---- VST3 ----------------------------------------------------------------------------------------------------------

template <class T>
T* selfOf(void* self) { return *static_cast<T**>(self); }
uint32_t V3_API refStub(void*) { return 1; }
uint32_t V3_API unrefStub(void*) { return 1; }

std::string asciiOf(const int16_t* text)
{
    std::string out;
    for (int c = 0; c < 128 && text[c] != 0; ++c) out += static_cast<char>(text[c] & 0x7f);
    return out;
}

struct Vst3Stream : v3_bstream_cpp
{
    std::string data;
    size_t position = 0;
    Vst3Stream* const selfptr = this;
    v3_bstream** handle() { return (v3_bstream**) &selfptr; }

    explicit Vst3Stream(std::string bytes = {}) : data(std::move(bytes))
    {
        query_interface = queryInterface;
        ref = refStub;
        unref = unrefStub;
        stream.read = read;
        stream.write = write;
        stream.seek = seek;
        stream.tell = tell;
    }
    Vst3Stream(const Vst3Stream&) = delete;
    Vst3Stream& operator=(const Vst3Stream&) = delete;

    static v3_result V3_API queryInterface(void* self, const v3_tuid iid, void** obj)
    {
        if (v3_tuid_match(iid, v3_funknown_iid) || v3_tuid_match(iid, v3_bstream_iid)) { *obj = self; return V3_OK; }
        *obj = nullptr;
        return V3_NO_INTERFACE;
    }
    static v3_result V3_API read(void* self, void* buffer, int32_t size, int32_t* done)
    {
        Vst3Stream* const s = selfOf<Vst3Stream>(self);
        const size_t n = std::min(static_cast<size_t>(std::max(size, 0)), s->data.size() - s->position);
        std::memcpy(buffer, s->data.data() + s->position, n);
        s->position += n;
        if (done != nullptr) *done = static_cast<int32_t>(n);
        return V3_OK;
    }
    static v3_result V3_API write(void* self, void* buffer, int32_t size, int32_t* done)
    {
        Vst3Stream* const s = selfOf<Vst3Stream>(self);
        s->data.append(static_cast<const char*>(buffer), static_cast<size_t>(std::max(size, 0)));
        if (done != nullptr) *done = size;
        return V3_OK;
    }
    static v3_result V3_API seek(void* self, int64_t pos, int32_t mode, int64_t* result)
    {
        Vst3Stream* const s = selfOf<Vst3Stream>(self);
        const int64_t base = mode == V3_SEEK_CUR ? static_cast<int64_t>(s->position)
                           : mode == V3_SEEK_END ? static_cast<int64_t>(s->data.size()) : 0;
        s->position = static_cast<size_t>(std::clamp<int64_t>(base + pos, 0, static_cast<int64_t>(s->data.size())));
        if (result != nullptr) *result = static_cast<int64_t>(s->position);
        return V3_OK;
    }
    static v3_result V3_API tell(void* self, int64_t* pos)
    {
        *pos = static_cast<int64_t>(selfOf<Vst3Stream>(self)->position);
        return V3_OK;
    }
};

// One parameter change, delivered in a zero-frame process() as a host flushes its edits with the transport stopped.
struct Vst3ParamChange : v3_param_value_queue_cpp
{
    v3_param_id id = 0;
    double value = 0.0;
    Vst3ParamChange* const selfptr = this;
    v3_param_value_queue** handle() { return (v3_param_value_queue**) &selfptr; }

    Vst3ParamChange()
    {
        query_interface = queryInterface;
        ref = refStub;
        unref = unrefStub;
        queue.get_param_id = [](void* self) { return selfOf<Vst3ParamChange>(self)->id; };
        queue.get_point_count = [](void*) -> int32_t { return 1; };
        queue.get_point = [](void* self, int32_t index, int32_t* offset, double* value) -> v3_result {
            if (index != 0) return V3_INVALID_ARG;
            *offset = 0;
            *value = selfOf<Vst3ParamChange>(self)->value;
            return V3_OK;
        };
        queue.add_point = [](void*, int32_t, double, int32_t*) -> v3_result { return V3_NOT_IMPLEMENTED; };
    }
    Vst3ParamChange(const Vst3ParamChange&) = delete;
    Vst3ParamChange& operator=(const Vst3ParamChange&) = delete;

    static v3_result V3_API queryInterface(void* self, const v3_tuid iid, void** obj)
    {
        if (v3_tuid_match(iid, v3_funknown_iid) || v3_tuid_match(iid, v3_param_value_queue_iid)) { *obj = self; return V3_OK; }
        *obj = nullptr;
        return V3_NO_INTERFACE;
    }
};

struct Vst3ParamChanges : v3_param_changes_cpp
{
    Vst3ParamChange* change = nullptr;   // one change, or none
    Vst3ParamChanges* const selfptr = this;
    v3_param_changes** handle() { return (v3_param_changes**) &selfptr; }

    Vst3ParamChanges()
    {
        query_interface = queryInterface;
        ref = refStub;
        unref = unrefStub;
        changes.get_param_count = [](void* self) -> int32_t { return selfOf<Vst3ParamChanges>(self)->change ? 1 : 0; };
        changes.get_param_data = [](void* self, int32_t index) -> v3_param_value_queue** {
            Vst3ParamChanges* const c = selfOf<Vst3ParamChanges>(self);
            return index == 0 && c->change != nullptr ? c->change->handle() : nullptr;
        };
        changes.add_param_data = [](void*, const v3_param_id*, int32_t*) -> v3_param_value_queue** { return nullptr; };
    }
    Vst3ParamChanges(const Vst3ParamChanges&) = delete;
    Vst3ParamChanges& operator=(const Vst3ParamChanges&) = delete;

    static v3_result V3_API queryInterface(void* self, const v3_tuid iid, void** obj)
    {
        if (v3_tuid_match(iid, v3_funknown_iid) || v3_tuid_match(iid, v3_param_changes_iid)) { *obj = self; return V3_OK; }
        *obj = nullptr;
        return V3_NO_INTERFACE;
    }
};

struct Vst3HostApplication : v3_host_application_cpp
{
    Vst3HostApplication* const selfptr = this;
    v3_funknown** handle() { return (v3_funknown**) &selfptr; }

    Vst3HostApplication()
    {
        query_interface = queryInterface;
        ref = refStub;
        unref = unrefStub;
        app.get_name = [](void*, v3_str_128 name) -> v3_result {
            const char* const text = "MultiCompSessionRestoreTest";
            int i = 0;
            for (; text[i] != '\0'; ++i) name[i] = static_cast<int16_t>(text[i]);
            name[i] = 0;
            return V3_OK;
        };
        app.create_instance = [](void*, v3_tuid, v3_tuid, void** obj) -> v3_result { *obj = nullptr; return V3_NOT_IMPLEMENTED; };
    }

    static v3_result V3_API queryInterface(void* self, const v3_tuid iid, void** obj)
    {
        if (v3_tuid_match(iid, v3_funknown_iid) || v3_tuid_match(iid, v3_host_application_iid)) { *obj = self; return V3_OK; }
        *obj = nullptr;
        return V3_NO_INTERFACE;
    }
};

class Vst3Format
{
public:
    using Form = TokenForm;
    static constexpr const char* kName = "VST3";
    static constexpr bool kHasPrograms = true;
    static constexpr bool kReadsNoProgram = false;   // the program parameter always holds a program

    explicit Vst3Format(const char* binary)
    {
        library = dlopen(binary, RTLD_NOW | RTLD_LOCAL);
        if (library == nullptr) { std::fprintf(stderr, "FAIL: dlopen(%s): %s\n", binary, dlerror()); return; }
       #ifdef __APPLE__
        moduleEntry = (bool (*)(void*)) dlsym(library, "bundleEntry");
        moduleExit = (bool (*)()) dlsym(library, "bundleExit");
       #else
        moduleEntry = (bool (*)(void*)) dlsym(library, "ModuleEntry");
        moduleExit = (bool (*)()) dlsym(library, "ModuleExit");
       #endif
        const auto getFactory = (v3_plugin_factory** (*)()) dlsym(library, "GetPluginFactory");
        if (getFactory == nullptr || (moduleEntry != nullptr && !moduleEntry(library))) return;
        factory = getFactory();
        if (factory == nullptr) return;
        if (v3_cpp_obj_query_interface(factory, v3_plugin_factory_3_iid, &factory3) == V3_OK && factory3 != nullptr)
            v3_cpp_obj(factory3)->set_host_context(factory3, host.handle());
        for (int32_t i = 0, count = v3_cpp_obj(factory)->num_classes(factory); i < count; ++i)
        {
            v3_class_info info = {};
            if (v3_cpp_obj(factory)->get_class_info(factory, i, &info) == V3_OK
                && std::strcmp(info.category, "Audio Module Class") == 0)
            {
                std::memcpy(componentClass, info.class_id, sizeof(v3_tuid));
                loaded = true;
                break;
            }
        }
    }
    ~Vst3Format()
    {
        if (factory3 != nullptr) v3_cpp_obj_unref(factory3);
        if (moduleExit != nullptr) moduleExit();
    }
    Vst3Format(const Vst3Format&) = delete;
    Vst3Format& operator=(const Vst3Format&) = delete;

    bool ok() const { return loaded; }

    struct Instance
    {
        v3_component** component = nullptr;
        v3_edit_controller** controller = nullptr;
        ControlIds<v3_param_id> ids;
        v3_param_id programId = 0;   // the parameter flagged V3_PARAM_PROGRAM_CHANGE
        bool hasProgram = false;
    };

    bool create(Instance& instance)
    {
        if (v3_cpp_obj(factory)->create_instance(factory, componentClass, v3_component_iid, (void**) &instance.component) != V3_OK
            || instance.component == nullptr || v3_cpp_obj_initialize(instance.component, host.handle()) != V3_OK)
            return false;
        // Single-component DAF builds answer for the edit controller too (DAF_PLUGIN_WANT_DIRECT_ACCESS).
        if (v3_cpp_obj_query_interface(instance.component, v3_edit_controller_iid, &instance.controller) != V3_OK
            || instance.controller == nullptr)
            return false;
        for (int32_t i = 0, count = v3_cpp_obj(instance.controller)->get_parameter_count(instance.controller); i < count; ++i)
        {
            v3_param_info info = {};
            if (v3_cpp_obj(instance.controller)->get_parameter_info(instance.controller, i, &info) != V3_OK) return false;
            if ((info.flags & V3_PARAM_PROGRAM_CHANGE) != 0)
            {
                instance.programId = info.param_id;
                instance.hasProgram = true;
            }
            else
                instance.ids.add(asciiOf(info.title), info.param_id);
        }
        return instance.ids.complete();
    }

    void destroy(Instance& instance)
    {
        if (instance.controller != nullptr) v3_cpp_obj_unref(instance.controller);
        if (instance.component != nullptr)
        {
            v3_cpp_obj_terminate(instance.component);
            v3_cpp_obj_unref(instance.component);
        }
        instance = {};
    }

    bool set(Instance& instance, int index, float plain) { return send(instance, instance.ids[index], plain); }

    float get(Instance& instance, int index)
    {
        const v3_param_id id = instance.ids[index];
        const double normalised = v3_cpp_obj(instance.controller)->get_parameter_normalised(instance.controller, id);
        return static_cast<float>(v3_cpp_obj(instance.controller)->normalised_parameter_to_plain(instance.controller, id, normalised));
    }

    // A host's program change: the program parameter, delivered as any other edit.
    bool loadProgram(Instance& instance, int program)
    {
        return instance.hasProgram && send(instance, instance.programId, program);
    }

    // The program parameter's value, and its text: the program's name.
    bool program(Instance& instance, int& number, std::string& name)
    {
        if (!instance.hasProgram) return false;
        const v3_param_id id = instance.programId;
        const double normalised = v3_cpp_obj(instance.controller)->get_parameter_normalised(instance.controller, id);
        number = static_cast<int>(std::lround(
            v3_cpp_obj(instance.controller)->normalised_parameter_to_plain(instance.controller, id, normalised)));
        v3_str_128 text = {};
        if (v3_cpp_obj(instance.controller)->get_parameter_string_for_value(instance.controller, id, normalised, text) != V3_OK)
            return false;
        name = asciiOf(text);
        return true;
    }

    bool save(Instance& instance, Form& form)
    {
        Vst3Stream stream;
        if (v3_cpp_obj(instance.component)->get_state(instance.component, stream.handle()) != V3_OK) return false;
        form.bytes = stream.data;
        return true;
    }

    // As a host loads a project: the component's state, then the same bytes to the controller.
    bool restore(Instance& instance, const Form& form)
    {
        Vst3Stream stream(form.bytes);
        if (v3_cpp_obj(instance.component)->set_state(instance.component, stream.handle()) != V3_OK) return false;
        Vst3Stream controllerStream(form.bytes);
        return v3_cpp_obj(instance.controller)->set_component_state(instance.controller, controllerStream.handle()) == V3_OK;
    }

private:
    // One edit, in plain units, delivered in a zero-frame process().
    bool send(Instance& instance, v3_param_id id, double plain)
    {
        v3_audio_processor** processor = nullptr;
        if (v3_cpp_obj_query_interface(instance.component, v3_audio_processor_iid, &processor) != V3_OK || processor == nullptr)
            return false;
        v3_process_setup setup = {V3_REALTIME, V3_SAMPLE_32, 256, 48000.0};
        Vst3ParamChange change;
        change.id = id;
        change.value = v3_cpp_obj(instance.controller)->plain_parameter_to_normalised(instance.controller, id, plain);
        Vst3ParamChanges changes;
        changes.change = &change;
        v3_process_data data = {};
        data.process_mode = V3_REALTIME;
        data.symbolic_sample_size = V3_SAMPLE_32;
        data.nframes = 0;
        data.input_params = changes.handle();
        const bool done = v3_cpp_obj(processor)->setup_processing(processor, &setup) == V3_OK
            && v3_cpp_obj(instance.component)->set_active(instance.component, true) == V3_OK
            && v3_cpp_obj(processor)->set_processing(processor, true) == V3_OK
            && v3_cpp_obj(processor)->process(processor, &data) == V3_OK;
        v3_cpp_obj(processor)->set_processing(processor, false);
        v3_cpp_obj(instance.component)->set_active(instance.component, false);
        v3_cpp_obj_unref(processor);
        return done;
    }

    void* library = nullptr;
    bool (*moduleEntry)(void*) = nullptr;
    bool (*moduleExit)() = nullptr;
    v3_plugin_factory** factory = nullptr;
    v3_plugin_factory_3** factory3 = nullptr;
    v3_tuid componentClass = {};
    Vst3HostApplication host;
    bool loaded = false;
};

// ---- CLAP ----------------------------------------------------------------------------------------------------------

const void* CLAP_ABI clapHostExtension(const clap_host_t*, const char*) { return nullptr; }
void CLAP_ABI clapHostRequest(const clap_host_t*) {}
const clap_host_t kClapHost = {
    CLAP_VERSION_INIT, nullptr, "MultiCompSessionRestoreTest", "Dusk Audio", "https://dusk-audio.github.io/", "1.0.0",
    clapHostExtension, clapHostRequest, clapHostRequest, clapHostRequest};

struct ClapEvents
{
    clap_input_events_t in;
    clap_output_events_t out;
    clap_event_param_value_t event = {};

    ClapEvents(clap_id id, double value)
    {
        event.header = {sizeof(event), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
        event.param_id = id;
        event.cookie = nullptr;
        event.note_id = event.port_index = event.channel = event.key = -1;
        event.value = value;
        in.ctx = this;
        in.size = [](const clap_input_events_t*) -> uint32_t { return 1; };
        in.get = [](const clap_input_events_t* list, uint32_t index) -> const clap_event_header_t* {
            return index == 0 ? &static_cast<const ClapEvents*>(list->ctx)->event.header : nullptr;
        };
        out.ctx = this;
        out.try_push = [](const clap_output_events_t*, const clap_event_header_t*) { return true; };
    }
};

class ClapFormat
{
public:
    using Form = TokenForm;
    static constexpr const char* kName = "CLAP";
    static constexpr bool kHasPrograms = false;   // no program or preset extension: a host reads no program

    explicit ClapFormat(const char* binary)
    {
        library = dlopen(binary, RTLD_NOW | RTLD_LOCAL);
        if (library == nullptr) { std::fprintf(stderr, "FAIL: dlopen(%s): %s\n", binary, dlerror()); return; }
        entry = (const clap_plugin_entry_t*) dlsym(library, "clap_entry");
        if (entry == nullptr || !entry->init(binary)) { entry = nullptr; return; }
        factory = (const clap_plugin_factory_t*) entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
        descriptor = factory != nullptr && factory->get_plugin_count(factory) > 0
            ? factory->get_plugin_descriptor(factory, 0) : nullptr;
    }
    ~ClapFormat()
    {
        if (entry != nullptr) entry->deinit();
    }
    ClapFormat(const ClapFormat&) = delete;
    ClapFormat& operator=(const ClapFormat&) = delete;

    bool ok() const { return descriptor != nullptr; }

    struct Instance
    {
        const clap_plugin_t* plugin = nullptr;
        const clap_plugin_params_t* params = nullptr;
        const clap_plugin_state_t* state = nullptr;
        ControlIds<clap_id> ids;
    };

    bool create(Instance& instance)
    {
        instance.plugin = factory->create_plugin(factory, &kClapHost, descriptor->id);
        if (instance.plugin == nullptr || !instance.plugin->init(instance.plugin)) return false;
        instance.params = (const clap_plugin_params_t*) instance.plugin->get_extension(instance.plugin, CLAP_EXT_PARAMS);
        instance.state = (const clap_plugin_state_t*) instance.plugin->get_extension(instance.plugin, CLAP_EXT_STATE);
        if (instance.params == nullptr || instance.state == nullptr) return false;
        for (uint32_t i = 0, count = instance.params->count(instance.plugin); i < count; ++i)
        {
            clap_param_info_t info = {};
            if (!instance.params->get_info(instance.plugin, i, &info)) return false;
            instance.ids.add(info.name, info.id);
        }
        return instance.ids.complete();
    }

    void destroy(Instance& instance)
    {
        if (instance.plugin != nullptr) instance.plugin->destroy(instance.plugin);
        instance = {};
    }

    // clap_plugin_params::flush, as a host delivers an edit to an inactive plugin.
    bool set(Instance& instance, int index, float plain)
    {
        ClapEvents events(instance.ids[index], plain);
        instance.params->flush(instance.plugin, &events.in, &events.out);
        return true;
    }

    float get(Instance& instance, int index)
    {
        double value = -1.0;
        return instance.params->get_value(instance.plugin, instance.ids[index], &value) ? static_cast<float>(value) : -1.0f;
    }

    bool save(Instance& instance, Form& form)
    {
        clap_ostream_t stream = {&form.bytes, [](const clap_ostream_t* s, const void* buffer, uint64_t size) -> int64_t {
            static_cast<std::string*>(s->ctx)->append(static_cast<const char*>(buffer), static_cast<size_t>(size));
            return static_cast<int64_t>(size);
        }};
        form.bytes.clear();
        return instance.state->save(instance.plugin, &stream);
    }

    bool restore(Instance& instance, const Form& form)
    {
        struct Reader { const std::string* bytes; size_t position; } reader{&form.bytes, 0};
        clap_istream_t stream = {&reader, [](const clap_istream_t* s, void* buffer, uint64_t size) -> int64_t {
            Reader* const r = static_cast<Reader*>(s->ctx);
            const size_t n = std::min(static_cast<size_t>(size), r->bytes->size() - r->position);
            std::memcpy(buffer, r->bytes->data() + r->position, n);
            r->position += n;
            return static_cast<int64_t>(n);
        }};
        return instance.state->load(instance.plugin, &stream);
    }

private:
    void* library = nullptr;
    const clap_plugin_entry_t* entry = nullptr;
    const clap_plugin_factory_t* factory = nullptr;
    const clap_plugin_descriptor_t* descriptor = nullptr;
};

// ---- AU ------------------------------------------------------------------------------------------------------------

#ifdef __APPLE__
constexpr OSType fourcc(char a, char b, char c, char d)
{
    return (static_cast<OSType>(a) << 24u) | (static_cast<OSType>(b) << 16u)
         | (static_cast<OSType>(c) << 8u) | static_cast<OSType>(d);
}

std::string stringOf(CFStringRef text)
{
    if (text == nullptr || CFGetTypeID(text) != CFStringGetTypeID()) return {};
    const CFIndex capacity = CFStringGetMaximumSizeForEncoding(CFStringGetLength(text), kCFStringEncodingUTF8) + 1;
    std::string out(static_cast<size_t>(capacity), '\0');
    if (!CFStringGetCString(text, out.data(), capacity, kCFStringEncodingUTF8)) return {};
    out.resize(std::strlen(out.c_str()));
    return out;
}

// kAudioUnitProperty_ClassInfo: the wrapper's "data" dictionary holds "program", "states" (one-key dictionaries, the
// "parameters" state among them) and "params" (one-key dictionaries, symbol to value).
struct ClassInfoForm
{
    CFPropertyListRef info = nullptr;

    ClassInfoForm() = default;
    ClassInfoForm(const ClassInfoForm& other) : info(other.info) { if (info != nullptr) CFRetain(info); }
    ClassInfoForm& operator=(const ClassInfoForm& other)
    {
        if (other.info != nullptr) CFRetain(other.info);
        if (info != nullptr) CFRelease(info);
        info = other.info;
        return *this;
    }
    ~ClassInfoForm() { if (info != nullptr) CFRelease(info); }

    // The value of the one-key dictionary in `list` ("states" or "params") whose key is `key`.
    CFTypeRef entry(CFStringRef list, CFStringRef key) const
    {
        if (info == nullptr || CFGetTypeID(info) != CFDictionaryGetTypeID()) return nullptr;
        const auto data = (CFDictionaryRef) CFDictionaryGetValue((CFDictionaryRef) info, CFSTR(kAUPresetDataKey));
        if (data == nullptr || CFGetTypeID(data) != CFDictionaryGetTypeID()) return nullptr;
        const auto array = (CFArrayRef) CFDictionaryGetValue(data, list);
        if (array == nullptr || CFGetTypeID(array) != CFArrayGetTypeID()) return nullptr;
        for (CFIndex i = 0; i < CFArrayGetCount(array); ++i)
        {
            const auto item = (CFDictionaryRef) CFArrayGetValueAtIndex(array, i);
            if (item != nullptr && CFGetTypeID(item) == CFDictionaryGetTypeID())
                if (const CFTypeRef value = CFDictionaryGetValue(item, key)) return value;
        }
        return nullptr;
    }
    std::string state() const { return stringOf((CFStringRef) entry(CFSTR("states"), CFSTR("parameters"))); }
    float listedMode() const
    {
        const auto number = (CFNumberRef) entry(CFSTR("params"), CFSTR("mode"));
        float value = -1.0f;
        if (number == nullptr || CFGetTypeID(number) != CFNumberGetTypeID()
            || !CFNumberGetValue(number, kCFNumberFloat32Type, &value)) return -1.0f;
        return value;
    }
    ClassInfoForm withState(const std::string& state) const
    {
        ClassInfoForm copy;
        copy.info = CFPropertyListCreateDeepCopy(nullptr, info, kCFPropertyListMutableContainersAndLeaves);
        const auto data = (CFMutableDictionaryRef) CFDictionaryGetValue((CFDictionaryRef) copy.info, CFSTR(kAUPresetDataKey));
        const auto array = data != nullptr ? (CFArrayRef) CFDictionaryGetValue(data, CFSTR("states")) : nullptr;
        for (CFIndex i = 0; array != nullptr && i < CFArrayGetCount(array); ++i)
        {
            const auto item = (CFMutableDictionaryRef) CFArrayGetValueAtIndex(array, i);
            if (item == nullptr || !CFDictionaryContainsKey(item, CFSTR("parameters"))) continue;
            const CFStringRef value = CFStringCreateWithCString(nullptr, state.c_str(), kCFStringEncodingUTF8);
            CFDictionarySetValue(item, CFSTR("parameters"), value);
            CFRelease(value);
        }
        return copy;
    }
};

class AUFormat
{
public:
    using Form = ClassInfoForm;
    static constexpr const char* kName = "AU";
    static constexpr bool kHasPrograms = true;
    static constexpr bool kReadsNoProgram = true;   // a negative preset number: a user preset, no factory program

    explicit AUFormat(const char* binary)
    {
        library = dlopen(binary, RTLD_NOW | RTLD_LOCAL);
        if (library == nullptr) { std::fprintf(stderr, "FAIL: dlopen(%s): %s\n", binary, dlerror()); return; }
        factory = (void* (*)(const AudioComponentDescription*)) dlsym(library, "PluginAUFactory");
    }
    AUFormat(const AUFormat&) = delete;
    AUFormat& operator=(const AUFormat&) = delete;

    bool ok() const { return factory != nullptr; }

    struct Instance { AudioComponentPlugInInterface* unit = nullptr; };

    bool create(Instance& instance)
    {
        const AudioComponentDescription description = {
            kAudioUnitType_Effect, fourcc('D', 's', 'M', 'c'), fourcc('D', 'u', 's', 'k'), 0, 0};
        instance.unit = static_cast<AudioComponentPlugInInterface*>(factory(&description));
        return instance.unit != nullptr
            && instance.unit->Open(instance.unit, reinterpret_cast<AudioUnit>(instance.unit)) == noErr;
    }

    void destroy(Instance& instance)
    {
        if (instance.unit != nullptr) instance.unit->Close(instance.unit);
        instance = {};
    }

    // An AU parameter's id is the DAF parameter index.
    bool set(Instance& instance, int index, float plain)
    {
        using SetParameter = OSStatus (*)(void*, AudioUnitParameterID, AudioUnitScope, AudioUnitElement,
                                          AudioUnitParameterValue, UInt32);
        const auto setParameter = reinterpret_cast<SetParameter>(instance.unit->Lookup(kAudioUnitSetParameterSelect));
        return setParameter != nullptr && setParameter(instance.unit, static_cast<AudioUnitParameterID>(index),
                                                       kAudioUnitScope_Global, 0, plain, 0) == noErr;
    }

    float get(Instance& instance, int index)
    {
        using GetParameter = OSStatus (*)(void*, AudioUnitParameterID, AudioUnitScope, AudioUnitElement,
                                          AudioUnitParameterValue*);
        const auto getParameter = reinterpret_cast<GetParameter>(instance.unit->Lookup(kAudioUnitGetParameterSelect));
        AudioUnitParameterValue value = -1.0f;
        return getParameter != nullptr && getParameter(instance.unit, static_cast<AudioUnitParameterID>(index),
                                                       kAudioUnitScope_Global, 0, &value) == noErr ? value : -1.0f;
    }

    // A host's program change: kAudioUnitProperty_PresentPreset set to the factory preset.
    bool loadProgram(Instance& instance, int program)
    {
        if (program < 0 || static_cast<size_t>(program) >= multicompp::kFactoryPresets.size()) return false;
        const CFStringRef name = CFStringCreateWithCString(
            nullptr, multicompp::kFactoryPresets[static_cast<size_t>(program)].name, kCFStringEncodingUTF8);
        const AUPreset preset = {static_cast<SInt32>(program), name};
        const bool done = setProperty(instance, kAudioUnitProperty_PresentPreset, &preset, sizeof(preset)) == noErr;
        if (name != nullptr) CFRelease(name);
        return done;
    }

    // kAudioUnitProperty_PresentPreset: a factory preset's number and name, or a user preset's (a negative number).
    bool program(Instance& instance, int& number, std::string& name)
    {
        AUPreset preset = {};
        UInt32 size = sizeof(preset);
        if (getProperty(instance, kAudioUnitProperty_PresentPreset, &preset, &size) != noErr) return false;
        number = static_cast<int>(preset.presetNumber);
        name = stringOf(preset.presetName);
        if (preset.presetName != nullptr) CFRelease(preset.presetName);   // the property hands over a reference
        return true;
    }

    bool save(Instance& instance, Form& form)
    {
        CFPropertyListRef info = nullptr;
        UInt32 size = sizeof(info);
        if (getProperty(instance, kAudioUnitProperty_ClassInfo, &info, &size) != noErr) return false;
        form = {};
        form.info = info;   // the property hands over a reference
        return info != nullptr;
    }

    bool restore(Instance& instance, const Form& form)
    {
        return setProperty(instance, kAudioUnitProperty_ClassInfo, &form.info, sizeof(form.info)) == noErr;
    }

private:
    static OSStatus getProperty(Instance& instance, AudioUnitPropertyID property, void* data, UInt32* size)
    {
        using GetProperty = OSStatus (*)(void*, AudioUnitPropertyID, AudioUnitScope, AudioUnitElement, void*, UInt32*);
        const auto function = reinterpret_cast<GetProperty>(instance.unit->Lookup(kAudioUnitGetPropertySelect));
        return function != nullptr ? function(instance.unit, property, kAudioUnitScope_Global, 0, data, size)
                                   : static_cast<OSStatus>(kAudioUnitErr_InvalidProperty);
    }

    static OSStatus setProperty(Instance& instance, AudioUnitPropertyID property, const void* data, UInt32 size)
    {
        using SetProperty = OSStatus (*)(void*, AudioUnitPropertyID, AudioUnitScope, AudioUnitElement, const void*, UInt32);
        const auto function = reinterpret_cast<SetProperty>(instance.unit->Lookup(kAudioUnitSetPropertySelect));
        return function != nullptr ? function(instance.unit, property, kAudioUnitScope_Global, 0, data, size)
                                   : static_cast<OSStatus>(kAudioUnitErr_InvalidProperty);
    }

    void* library = nullptr;
    void* (*factory)(const AudioComponentDescription*) = nullptr;
};
#endif

// ---- [1]-[3] sessions saved in mode 4, and in VCA ------------------------------------------------------------------

template <class Format>
void runSessions(Format& format)
{
    for (const Case& c : kCases)
    {
        // The session: an instance set through the host API and saved by the wrapper, its state then rewritten.
        const std::array<float, kReadBack.size()> set{{c.mode, kFetRatio, kFetCurve, kFetTransient}};
        std::array<float, kReadBack.size()> saved{};
        typename Format::Instance source;
        typename Format::Form form;
        bool made = format.create(source);
        for (size_t k = 0; made && k < kReadBack.size(); ++k) made = format.set(source, indexOf(kReadBack[k]), set[k]);
        for (size_t k = 0; made && k < kReadBack.size(); ++k) saved[k] = format.get(source, indexOf(kReadBack[k]));
        made = made && saved == set && format.save(source, form);
        format.destroy(source);
        std::string state = made ? form.state() : std::string();
        if (made && c.version != multicompp::kStateVersion)
        {
            std::string older;
            made = toVersion(state, c.version, older);
            if (made)
            {
                state = older;
                form = form.withState(state);
            }
        }
        const float stateMode = made ? decodedMode(state) : -1.0f, listedMode = made ? form.listedMode() : -1.0f;
        check(made && form.state() == state && stateMode == c.expectedMode && listedMode == c.mode,
              "%s %s: saved with mode %g, FET Ratio %g, Curve %g, Transient %g; its state decodes to mode %g, "
              "its parameter list says mode %g",
              Format::kName, c.name, saved[0], saved[1], saved[2], saved[3], stateMode, listedMode);
        if (!made) continue;

        // Restored into a fresh instance, read through the host API, and saved again.
        typename Format::Instance restored;
        const bool loaded = format.create(restored) && format.restore(restored, form);
        std::array<float, kReadBack.size()> read{};
        float resavedMode = -1.0f;
        if (loaded)
        {
            for (size_t k = 0; k < kReadBack.size(); ++k) read[k] = format.get(restored, indexOf(kReadBack[k]));
            typename Format::Form resaved;
            if (format.save(restored, resaved)) resavedMode = decodedMode(resaved.state());
        }
        format.destroy(restored);
        check(loaded, "%s %s: a fresh instance accepts the host's restore", Format::kName, c.name);
        if (!loaded) continue;
        check(read[0] == c.expectedMode && resavedMode == c.expectedMode,
              "%s %s: restored, Mode reads %g and its state saves mode %g (expected %g)",
              Format::kName, c.name, read[0], resavedMode, c.expectedMode);
        check(read[1] == saved[1] && read[2] == saved[2] && read[3] == saved[3],
              "%s %s: restored, FET Ratio %g, Curve %g, Transient %g read as saved (%g, %g, %g)",
              Format::kName, c.name, read[1], read[2], read[3], saved[1], saved[2], saved[3]);
    }
}

// ---- [4] program recall --------------------------------------------------------------------------------------------

constexpr float kEditedMix = 50.0f;   // every factory program sets Mix to 100 %

template <class Format>
void runPrograms(Format& format)
{
    for (size_t n = 0; n < multicompp::kFactoryPresets.size(); ++n)
    {
        const int number = static_cast<int>(n);
        const std::string name = multicompp::kFactoryPresets[n].name;
        for (const bool edited : {false, true})
        {
            // The session: program n loaded through the host API, which then reads it, and saved (edited: after Mix
            // moved off the program's 100 %).
            typename Format::Instance source;
            typename Format::Form form;
            int loadedNumber = -2;
            std::string loadedName;
            const bool made = format.create(source) && format.loadProgram(source, number)
                && format.program(source, loadedNumber, loadedName) && loadedNumber == number && loadedName == name
                && (!edited || format.set(source, indexOf(ParamId::Mix), kEditedMix))
                && format.save(source, form);
            format.destroy(source);

            // Restored into a fresh instance, whose program the host reads before and after.
            typename Format::Instance restored;
            int before = -2, after = -2;
            std::string beforeName, afterName;
            const bool loaded = made && format.create(restored) && format.program(restored, before, beforeName)
                && format.restore(restored, form) && format.program(restored, after, afterName);
            format.destroy(restored);
            const char* const failedStep = !made ? " [the session was not made]"
                                         : !loaded ? " [the restore or a reading failed]" : "";
            if (!edited)
                check(loaded && after == number && afterName == name,
                      "%s program %d \"%s\": loaded, the host read %d \"%s\"; saved and restored, it reads %d \"%s\"%s",
                      Format::kName, number, name.c_str(), loadedNumber, loadedName.c_str(), after, afterName.c_str(),
                      failedStep);
            else
                check(loaded && after == before && afterName == beforeName && (!Format::kReadsNoProgram || after < 0),
                      "%s program %d \"%s\", then Mix %g %%: restored, the host reads %d \"%s\", as before the restore "
                      "(%d \"%s\"): no factory program%s",
                      Format::kName, number, name.c_str(), kEditedMix, after, afterName.c_str(), before,
                      beforeName.c_str(), failedStep);
        }
    }
}

// ---- [5] a newer build's session -----------------------------------------------------------------------------------

template <class Format>
void runNewerSession(Format& format)
{
    const int newerVersion = multicompp::kStateVersion + 1;
    const std::string written = "v=" + std::to_string(multicompp::kStateVersion) + ";";
    const std::string claimed = "v=" + std::to_string(newerVersion) + ";";

    // The session: FET with the FET controls the cases set, saved by the wrapper, its state then claiming the next
    // version with its values unchanged.
    const std::array<float, kReadBack.size()> sessionValues{{1.0f, kFetRatio, kFetCurve, kFetTransient}};
    typename Format::Instance source;
    typename Format::Form form;
    bool made = format.create(source);
    for (size_t k = 0; made && k < kReadBack.size(); ++k)
        made = format.set(source, indexOf(kReadBack[k]), sessionValues[k]);
    made = made && format.save(source, form);
    format.destroy(source);
    const std::string state = made ? form.state() : std::string();
    made = made && state.compare(0, written.size(), written) == 0;
    if (made) form = form.withState(claimed + state.substr(written.size()));
    const float stateMode = made ? decodedMode(form.state()) : 0.0f, listedMode = made ? form.listedMode() : -1.0f;
    check(made && form.state().compare(0, claimed.size(), claimed) == 0 && stateMode == -1.0f && listedMode == 1.0f,
          "%s session from a newer build: its state claims version %d (this build decodes it to mode %g, -1: it "
          "cannot), its parameter list says mode %g%s", Format::kName, newerVersion, stateMode, listedMode,
          made ? "" : " [the session was not made]");
    if (!made) return;

    // Loaded into an instance with edits of its own (VCA, FET Ratio 12:1, Curve Modern, Transient 50 %), which is read
    // and saved before and after the attempt.
    const std::array<float, kReadBack.size()> ownValues{{2.0f, 2.0f, 0.0f, 50.0f}};
    typename Format::Instance target;
    bool ready = format.create(target);
    for (size_t k = 0; ready && k < kReadBack.size(); ++k)
        ready = format.set(target, indexOf(kReadBack[k]), ownValues[k]);
    std::array<float, kParamSlots> before{}, after{};
    for (int i = 0; ready && i < multicompp::kTotalParamCount; i = multicompp::nextControlParameter(i))
        before[static_cast<size_t>(i)] = format.get(target, i);
    for (size_t k = 0; ready && k < kReadBack.size(); ++k)
        ready = before[static_cast<size_t>(indexOf(kReadBack[k]))] == ownValues[k];
    typename Format::Form savedBefore, savedAfter;
    ready = ready && format.save(target, savedBefore);
    const bool accepted = ready && format.restore(target, form);
    for (int i = 0; ready && i < multicompp::kTotalParamCount; i = multicompp::nextControlParameter(i))
        after[static_cast<size_t>(i)] = format.get(target, i);
    const bool savedAgain = ready && format.save(target, savedAfter);
    format.destroy(target);
    check(ready && !accepted, "%s session from a newer build (state version %d): an instance in VCA refuses the "
          "host's restore%s", Format::kName, newerVersion, ready ? "" : " [the instance was not set up]");
    if (!ready) return;

    int differs = -1;
    for (int i = 0; differs < 0 && i < multicompp::kTotalParamCount; i = multicompp::nextControlParameter(i))
        if (after[static_cast<size_t>(i)] != before[static_cast<size_t>(i)]) differs = i;
    const size_t mode = static_cast<size_t>(indexOf(ParamId::Mode));
    char difference[160] = "";
    if (differs >= 0)
        std::snprintf(difference, sizeof(difference), ", but %s reads %g, %g before", hostNameOf(differs).c_str(),
                      after[static_cast<size_t>(differs)], before[static_cast<size_t>(differs)]);
    check(differs < 0, "%s session from a newer build: refused, every control reads as before the attempt (Mode %g; "
          "the session says %g)%s", Format::kName, after[mode], sessionValues[0], difference);
    const std::string stateBefore = savedBefore.state(), stateAfter = savedAfter.state();
    check(savedAgain && !stateBefore.empty() && stateAfter == stateBefore && decodedMode(stateAfter) == ownValues[0],
          "%s session from a newer build: refused, its state saves as before the attempt (mode %g, %s)",
          Format::kName, decodedMode(stateAfter), stateAfter == stateBefore ? "the same string" : "a different string");
}

// ---- the cases -----------------------------------------------------------------------------------------------------

template <class Format>
void run(Format& format)
{
    runSessions(format);
    if constexpr (Format::kHasPrograms)
        runPrograms(format);
    else
    {
        std::printf("SKIP: %s program recall: the wrapper offers a host no program\n", Format::kName);
        std::fflush(stdout);
    }
    runNewerSession(format);
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::fprintf(stderr, "usage: %s --vst3|--clap|--au <plugin binary>\n", argv[0]);
        return 2;
    }
    const std::string flag = argv[1];
    if (flag == "--vst3")
    {
        Vst3Format format(argv[2]);
        if (!format.ok()) { std::fprintf(stderr, "FAIL: %s is not a loadable VST3\n", argv[2]); return 1; }
        run(format);
    }
    else if (flag == "--clap")
    {
        ClapFormat format(argv[2]);
        if (!format.ok()) { std::fprintf(stderr, "FAIL: %s is not a loadable CLAP\n", argv[2]); return 1; }
        run(format);
    }
#ifdef __APPLE__
    else if (flag == "--au")
    {
        AUFormat format(argv[2]);
        if (!format.ok()) { std::fprintf(stderr, "FAIL: %s exports no PluginAUFactory\n", argv[2]); return 1; }
        run(format);
    }
#endif
    else
    {
        std::fprintf(stderr, "unknown format %s\n", argv[1]);
        return 2;
    }
    std::printf("%s session restore failures: %d\n", flag.c_str() + 2, gFailures);
    return gFailures == 0 ? 0 : 1;
}
