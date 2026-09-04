// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Regression test for dusk-audio/plugins#233.
//
// DAF simulates VST3 output parameters by pushing every changed one into the
// host's outputParameterChanges list from process(). A meter moves on nearly
// every block, so a plugin with two meters hands the host two parameter edits
// per block for as long as audio flows. Bitwig turns each of those into an
// undoable plug-in change; the undo history churns fast enough that Undo and
// Redo stop being offered at all, and the project never stays saved.
//
// This is a minimal VST3 host. It instantiates the plugin, runs blocks of
// noise through it, and records everything the plugin writes to
// outputParameterChanges. The contract it enforces:
//
//   1. No parameter carrying V3_PARAM_READ_ONLY without V3_PARAM_IS_HIDDEN
//      (DAF's mapping of kParameterIsOutput) may appear in
//      outputParameterChanges, however much its value moves.
//   2. No output parameter may reach the host through IComponentHandler either
//      -- begin_edit/perform_edit/end_edit is the channel a host turns into
//      undo entries most directly of all, and restart_component must not be
//      called from the audio callback. Guarding only outputParameterChanges
//      would leave a regression free to move to the louder channel.
//   3. The meters must still track the audio: at least one has to move at some
//      point during the run, sampled every block through
//      v3_edit_controller::get_parameter_normalised. Without this a "fix" that
//      simply stopped updating the meters would satisfy 1 and 2, and the
//      plugin's own embedded UI reads those same cached values.
//
// Assertion 3 is what keeps 1 and 2 honest, so all three must hold on the same
// run. It tests for *movement*, not for non-zero: a gain-reduction meter rests
// at the top of its range, so "non-zero" would be true of a frozen one. It
// samples per block rather than at the endpoints, so a meter that moves and
// decays back to rest still counts as alive.
//
// Both of DAF's calls into the code under test are exercised: every iteration
// sends one zero-frame block (updateParametersFromProcessing on the empty-block
// early return) and one full block (the normal path). Hosts send zero-frame
// blocks routinely on transport edges.
//
// Linux and macOS only (dlopen plus the two bundle layouts below); the
// behaviour under test is platform-independent, and the CMake helper that
// registers this skips the platforms it cannot load a bundle on.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include <dlfcn.h>

#include "travesty/audio_processor.h"
#include "travesty/base.h"
#include "travesty/component.h"
#include "travesty/edit_controller.h"
#include "travesty/factory.h"
#include "travesty/host.h"

// --------------------------------------------------------------------------------------------------------------------
// host-side COM objects
//
// travesty's ABI is COM's: a handle is a pointer to a pointer to a block of
// function pointers, and that same handle is passed back as `self`. Mirroring
// how the wrapper builds its own objects keeps the two ends in agreement.

template<class T>
static T* selfOf(void* const self)
{
    return *static_cast<T**>(self);
}

static uint32_t V3_API refStub(void*) { return 1; }
static uint32_t V3_API unrefStub(void*) { return 1; }

// ---- v3_param_value_queue -------------------------------------------------------------------------------------------

struct HostParamQueue : v3_param_value_queue_cpp {
    v3_param_id paramId = 0;
    std::vector<std::pair<int32_t, double>> points;

    // The handle handed to the plugin is the address of this member, so a
    // queue must never be copied, moved or relocated once the plugin holds
    // one. Deleting the copy and move operations makes the compiler enforce
    // that instead of leaving it to a reserve() nobody remembers to keep.
    HostParamQueue* const selfptr = this;
    v3_param_value_queue** handle() { return (v3_param_value_queue**) &selfptr; }

    HostParamQueue(const HostParamQueue&) = delete;
    HostParamQueue& operator=(const HostParamQueue&) = delete;
    HostParamQueue(HostParamQueue&&) = delete;
    HostParamQueue& operator=(HostParamQueue&&) = delete;

    HostParamQueue()
    {
        query_interface = queryInterface;
        ref = refStub;
        unref = unrefStub;
        queue.get_param_id = getParamId;
        queue.get_point_count = getPointCount;
        queue.get_point = getPoint;
        queue.add_point = addPoint;
    }

    static v3_result V3_API queryInterface(void* const self, const v3_tuid iid, void** const obj)
    {
        if (v3_tuid_match(iid, v3_funknown_iid) || v3_tuid_match(iid, v3_param_value_queue_iid))
        {
            *obj = self;
            return V3_OK;
        }
        *obj = nullptr;
        return V3_NO_INTERFACE;
    }

    static v3_param_id V3_API getParamId(void* const self)
    {
        return selfOf<HostParamQueue>(self)->paramId;
    }

    static int32_t V3_API getPointCount(void* const self)
    {
        return static_cast<int32_t>(selfOf<HostParamQueue>(self)->points.size());
    }

    static v3_result V3_API getPoint(void* const self, const int32_t idx, int32_t* const offset, double* const value)
    {
        HostParamQueue* const q = selfOf<HostParamQueue>(self);
        if (idx < 0 || static_cast<size_t>(idx) >= q->points.size())
            return V3_INVALID_ARG;
        *offset = q->points[idx].first;
        *value = q->points[idx].second;
        return V3_OK;
    }

    static v3_result V3_API addPoint(void* const self, const int32_t offset, const double value, int32_t* const idx)
    {
        HostParamQueue* const q = selfOf<HostParamQueue>(self);
        q->points.emplace_back(offset, value);
        *idx = static_cast<int32_t>(q->points.size()) - 1;
        return V3_OK;
    }
};

// ---- v3_param_changes -----------------------------------------------------------------------------------------------

struct HostParamChanges : v3_param_changes_cpp {
    // HostParamQueue is pinned (see above), so the queues are owned through
    // pointers and reused across blocks rather than reallocated. Reusing them
    // also means a run is never limited by how many distinct parameters the
    // plugin decides to report.
    std::vector<HostParamQueue*> queues;
    size_t used = 0;

    HostParamChanges* selfptr = this;
    v3_param_changes** handle() { return (v3_param_changes**) &selfptr; }

    HostParamChanges()
    {
        query_interface = queryInterface;
        ref = refStub;
        unref = unrefStub;
        changes.get_param_count = getParamCount;
        changes.get_param_data = getParamData;
        changes.add_param_data = addParamData;
    }

    ~HostParamChanges()
    {
        for (HostParamQueue* q : queues)
            delete q;
    }

    HostParamChanges(const HostParamChanges&) = delete;
    HostParamChanges& operator=(const HostParamChanges&) = delete;

    void reset()
    {
        for (size_t i = 0; i < used; ++i)
            queues[i]->points.clear();
        used = 0;
    }

    static v3_result V3_API queryInterface(void* const self, const v3_tuid iid, void** const obj)
    {
        if (v3_tuid_match(iid, v3_funknown_iid) || v3_tuid_match(iid, v3_param_changes_iid))
        {
            *obj = self;
            return V3_OK;
        }
        *obj = nullptr;
        return V3_NO_INTERFACE;
    }

    static int32_t V3_API getParamCount(void* const self)
    {
        return static_cast<int32_t>(selfOf<HostParamChanges>(self)->used);
    }

    static v3_param_value_queue** V3_API getParamData(void* const self, const int32_t idx)
    {
        HostParamChanges* const c = selfOf<HostParamChanges>(self);
        if (idx < 0 || static_cast<size_t>(idx) >= c->used)
            return nullptr;
        return c->queues[idx]->handle();
    }

    static v3_param_value_queue** V3_API addParamData(void* const self, const v3_param_id* const id, int32_t* const idx)
    {
        HostParamChanges* const c = selfOf<HostParamChanges>(self);

        for (size_t i = 0; i < c->used; ++i)
        {
            if (c->queues[i]->paramId == *id)
            {
                *idx = static_cast<int32_t>(i);
                return c->queues[i]->handle();
            }
        }

        if (c->used == c->queues.size())
            c->queues.push_back(new HostParamQueue);

        HostParamQueue* const q = c->queues[c->used];
        q->paramId = *id;
        q->points.clear();
        *idx = static_cast<int32_t>(c->used);
        ++c->used;
        return q->handle();
    }
};

// ---- v3_component_handler -------------------------------------------------------------------------------------------
//
// The channel a host turns into undo entries most directly. DAF only drives it
// from the UI message path today, so everything recorded here is expected to be
// zero during processing; that is the point -- a regression that moved meter
// reporting onto this channel would be invisible to a harness that never
// installed a handler.

struct HostComponentHandler : v3_component_handler_cpp {
    std::vector<v3_param_id> beginEdits, performEdits, endEdits;
    std::vector<int32_t> restarts;
    bool processing = false;          // set while the block loop runs
    size_t editsDuringProcessing = 0;
    size_t restartsDuringProcessing = 0;

    HostComponentHandler* selfptr = this;
    v3_component_handler** handle() { return (v3_component_handler**) &selfptr; }

    HostComponentHandler()
    {
        query_interface = queryInterface;
        ref = refStub;
        unref = unrefStub;
        comp.begin_edit = beginEdit;
        comp.perform_edit = performEdit;
        comp.end_edit = endEdit;
        comp.restart_component = restartComponent;
    }

    HostComponentHandler(const HostComponentHandler&) = delete;
    HostComponentHandler& operator=(const HostComponentHandler&) = delete;

    static v3_result V3_API queryInterface(void* const self, const v3_tuid iid, void** const obj)
    {
        if (v3_tuid_match(iid, v3_funknown_iid) || v3_tuid_match(iid, v3_component_handler_iid))
        {
            *obj = self;
            return V3_OK;
        }
        *obj = nullptr;
        return V3_NO_INTERFACE;
    }

    static v3_result V3_API beginEdit(void* const self, const v3_param_id id)
    {
        HostComponentHandler* const h = selfOf<HostComponentHandler>(self);
        h->beginEdits.push_back(id);
        if (h->processing) ++h->editsDuringProcessing;
        return V3_OK;
    }

    static v3_result V3_API performEdit(void* const self, const v3_param_id id, double)
    {
        HostComponentHandler* const h = selfOf<HostComponentHandler>(self);
        h->performEdits.push_back(id);
        if (h->processing) ++h->editsDuringProcessing;
        return V3_OK;
    }

    static v3_result V3_API endEdit(void* const self, const v3_param_id id)
    {
        HostComponentHandler* const h = selfOf<HostComponentHandler>(self);
        h->endEdits.push_back(id);
        if (h->processing) ++h->editsDuringProcessing;
        return V3_OK;
    }

    static v3_result V3_API restartComponent(void* const self, const int32_t flags)
    {
        HostComponentHandler* const h = selfOf<HostComponentHandler>(self);
        h->restarts.push_back(flags);
        if (h->processing) ++h->restartsDuringProcessing;
        return V3_OK;
    }
};

// ---- v3_host_application --------------------------------------------------------------------------------------------

struct HostApplication : v3_host_application_cpp {
    HostApplication* selfptr = this;
    v3_funknown** handle() { return (v3_funknown**) &selfptr; }

    HostApplication()
    {
        query_interface = queryInterface;
        ref = refStub;
        unref = unrefStub;
        app.get_name = getName;
        app.create_instance = createInstance;
    }

    static v3_result V3_API queryInterface(void* const self, const v3_tuid iid, void** const obj)
    {
        if (v3_tuid_match(iid, v3_funknown_iid) || v3_tuid_match(iid, v3_host_application_iid))
        {
            *obj = self;
            return V3_OK;
        }
        *obj = nullptr;
        return V3_NO_INTERFACE;
    }

    static v3_result V3_API getName(void*, v3_str_128 name)
    {
        static const char* const kName = "DafVst3OutputParamTest";
        int i = 0;
        for (; kName[i] != '\0'; ++i)
            name[i] = static_cast<int16_t>(kName[i]);
        name[i] = 0;
        return V3_OK;
    }

    // The wrapper only asks the host to create message and attribute-list
    // objects, which this test never exercises: it never opens the UI, and the
    // meter path under test runs entirely inside process().
    static v3_result V3_API createInstance(void*, v3_tuid, v3_tuid, void** const obj)
    {
        *obj = nullptr;
        return V3_NOT_IMPLEMENTED;
    }
};

// --------------------------------------------------------------------------------------------------------------------

static std::string utf16ToAscii(const v3_str_128 s)
{
    std::string out;
    for (int i = 0; i < 128 && s[i] != 0; ++i)
        out += static_cast<char>(s[i] & 0x7f);
    return out;
}

// CMake passes $<TARGET_FILE:...>, i.e. the loadable binary inside the bundle,
// so no architecture table is needed here and the harness works wherever DAF
// builds. A bundle directory is still accepted for running it by hand, on the
// two layouts this file can be built for; ModuleEntry finds its own bundle
// through dladdr either way, so nothing downstream depends on which was given.
static std::string binaryInsideBundle(const std::string& path)
{
    if (path.size() <= 5 || path.compare(path.size() - 5, 5, ".vst3") != 0)
        return path;

    std::string name(path);
    const size_t slash = name.find_last_of('/');
    if (slash != std::string::npos)
        name = name.substr(slash + 1);
    name = name.substr(0, name.size() - 5);

   #ifdef __APPLE__
    return path + "/Contents/MacOS/" + name;
   #else
    #if defined(__aarch64__)
     const char* const arch = "aarch64-linux";
    #else
     const char* const arch = "x86_64-linux";
    #endif
    return path + "/Contents/" + arch + "/" + name + ".so";
   #endif
}

struct ParamInfo {
    v3_param_id id;
    std::string title;
    int32_t flags;
};

// A DAF output parameter (kParameterIsOutput) maps to exactly
// V3_PARAM_READ_ONLY. Everything else the wrapper marks read-only it also
// marks hidden: the internal Latency/BufferSize/SampleRate mirrors, and the
// Reset designation, which is a trigger and is deliberately still reported.
// "Read-only but not hidden" therefore selects the meters and only the meters,
// without matching on parameter names.
static bool isOutputParameter(const ParamInfo& p)
{
    return (p.flags & V3_PARAM_READ_ONLY) != 0
        && (p.flags & V3_PARAM_IS_HIDDEN) == 0;
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
    // Flags first: leaving --silent to be swallowed by the positional parser
    // turned a typo into "the meters are not tracking the audio", blaming the
    // plugin for a command-line mistake.
    bool silent = false;
    std::vector<const char*> positional;
    // --set "Parameter Name=0.8": a control to move before the run. Named
    // rather than numbered because the same parameter has different ids in each
    // format. A compressor at its default threshold does not compress, so its
    // gain-reduction meters correctly report nothing, which a noise-only run
    // cannot tell apart from meters that do not work.
    struct ArmRequest { std::string name; double value; };
    std::vector<ArmRequest> armRequests;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strncmp(argv[i], "--set", 5) == 0)
        {
            const char* spec = argv[i][5] == '=' ? argv[i] + 6
                             : (i + 1 < argc ? argv[++i] : nullptr);
            const char* eq = spec != nullptr ? std::strrchr(spec, '=') : nullptr;
            if (eq == nullptr || eq == spec)
            {
                std::fprintf(stderr, "FAIL: --set wants NAME=VALUE (normalised 0..1)\n");
                return 1;
            }
            char* end = nullptr;
            const double value = std::strtod(eq + 1, &end);
            // An empty suffix converts nothing and still returns 0.0, which
            // would arm the parameter to zero and then blame the meters; nan
            // and inf both parse cleanly and neither is a normalised value.
            if (end == eq + 1 || end == nullptr || *end != '\0' || ! std::isfinite(value))
            {
                std::fprintf(stderr, "FAIL: --set value for \"%s\" is not a finite number\n", spec);
                return 1;
            }
            armRequests.push_back({ std::string(spec, static_cast<size_t>(eq - spec)), value });
        }
        else if (std::strcmp(argv[i], "--silent") == 0)
            silent = true;
        else
            positional.push_back(argv[i]);
    }

    if (positional.empty() || positional.size() > 3)
    {
        std::fprintf(stderr, "usage: %s <plugin.vst3> [blocks] [frames] [--silent] [--set NAME=VALUE]\n", argv[0]);
        return 2;
    }

    const std::string bundle(positional[0]);
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

    // --silent characterises what a plugin does on an idle track: some of these
    // emulate tape hiss or a console noise floor, and if that reached the
    // meters the symptom would appear with no audio playing at all. It is a
    // diagnostic mode, not the regression -- assertion 2 needs moving meters,
    // so a silent run is expected to fail for a plugin whose meters do sit
    // still, and the registered ctest always runs with noise.

    const std::string binary(binaryInsideBundle(bundle));

    void* const lib = dlopen(binary.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (lib == nullptr)
    {
        std::fprintf(stderr, "FAIL: dlopen(%s): %s\n", binary.c_str(), dlerror());
        return 1;
    }

   #ifdef __APPLE__
    bool (*moduleEntry)(void*) = (bool (*)(void*)) dlsym(lib, "bundleEntry");
    bool (*moduleExit)(void) = (bool (*)(void)) dlsym(lib, "bundleExit");
   #else
    bool (*moduleEntry)(void*) = (bool (*)(void*)) dlsym(lib, "ModuleEntry");
    bool (*moduleExit)(void) = (bool (*)(void)) dlsym(lib, "ModuleExit");
   #endif
    const void* (*getFactory)(void) = (const void* (*)(void)) dlsym(lib, "GetPluginFactory");

    if (getFactory == nullptr)
    {
        std::fprintf(stderr, "FAIL: no GetPluginFactory in %s\n", binary.c_str());
        return 1;
    }
    if (moduleEntry != nullptr && ! moduleEntry(lib))
    {
        std::fprintf(stderr, "FAIL: ModuleEntry refused to initialise the bundle\n");
        return 1;
    }

    v3_plugin_factory** const factory = (v3_plugin_factory**) getFactory();
    if (factory == nullptr)
    {
        std::fprintf(stderr, "FAIL: GetPluginFactory returned null\n");
        return 1;
    }

    HostApplication host;

    // A factory_3 also wants the host context; hand it over when it takes one.
    v3_plugin_factory_3** factory3 = nullptr;
    if (v3_cpp_obj_query_interface(factory, v3_plugin_factory_3_iid, &factory3) == V3_OK && factory3 != nullptr)
        v3_cpp_obj(factory3)->set_host_context(factory3, host.handle());

    v3_tuid componentCid = {};
    bool haveComponentCid = false;

    const int32_t numClasses = v3_cpp_obj(factory)->num_classes(factory);
    for (int32_t i = 0; i < numClasses; ++i)
    {
        v3_class_info info = {};
        if (v3_cpp_obj(factory)->get_class_info(factory, i, &info) != V3_OK)
            continue;
        if (std::strcmp(info.category, "Audio Module Class") == 0)
        {
            std::memcpy(componentCid, info.class_id, sizeof(v3_tuid));
            haveComponentCid = true;
            break;
        }
    }

    if (! haveComponentCid)
    {
        std::fprintf(stderr, "FAIL: no Audio Module Class in factory\n");
        return 1;
    }

    v3_component** component = nullptr;
    if (v3_cpp_obj(factory)->create_instance(factory, componentCid, v3_component_iid, (void**) &component) != V3_OK
        || component == nullptr)
    {
        std::fprintf(stderr, "FAIL: could not create component\n");
        return 1;
    }

    if (v3_cpp_obj_initialize(component, host.handle()) != V3_OK)
    {
        std::fprintf(stderr, "FAIL: component initialize\n");
        return 1;
    }

    // Single-component DAF plugins answer for the controller too, which is what
    // every plugin in this repository builds. A DAF_VST3_USES_SEPARATE_CONTROLLER
    // build is refused rather than measured: there the controller keeps its own
    // parameter cache fed only by the host relaying outputParameterChanges back,
    // and suppressing those pushes is exactly the fix under test -- so
    // get_parameter_normalised would read the same value before and after and
    // the harness would fail a correct plugin.
    v3_edit_controller** controller = nullptr;
    if (v3_cpp_obj_query_interface(component, v3_edit_controller_iid, &controller) != V3_OK || controller == nullptr)
    {
        std::fprintf(stderr,
                     "FAIL: the component does not implement v3_edit_controller.\n"
                     "      This harness reads the meters back through the component's own cache and\n"
                     "      cannot measure a separate-controller build; teach it the relay path first.\n");
        return 1;
    }

    HostComponentHandler handler;
    if (v3_cpp_obj(controller)->set_component_handler(controller, handler.handle()) != V3_OK)
    {
        std::fprintf(stderr, "FAIL: set_component_handler\n");
        return 1;
    }

    std::vector<ParamInfo> params;
    const int32_t paramCount = v3_cpp_obj(controller)->get_parameter_count(controller);
    for (int32_t i = 0; i < paramCount; ++i)
    {
        v3_param_info info = {};
        if (v3_cpp_obj(controller)->get_parameter_info(controller, i, &info) != V3_OK)
        {
            // Not skippable: an unreadable parameter would leave its id
            // unknown below, and unknown ids are what would let meter events
            // slip through the classifier unnoticed.
            std::fprintf(stderr, "FAIL: get_parameter_info(%d) failed; cannot classify every parameter\n", i);
            return 1;
        }
        params.push_back({ info.param_id, utf16ToAscii(info.title), info.flags });
    }

    auto indexOf = [&params](const v3_param_id id) -> int {
        for (size_t i = 0; i < params.size(); ++i)
            if (params[i].id == id)
                return static_cast<int>(i);
        return -1;
    };

    std::vector<std::pair<v3_param_id, double>> armed;
    for (const ArmRequest& req : armRequests)
    {
        const ParamInfo* match = nullptr;
        for (const ParamInfo& p : params)
            if (p.title == req.name) { match = &p; break; }

        if (match == nullptr)
        {
            std::fprintf(stderr, "FAIL: --set names \"%s\", which this plugin has no parameter called\n",
                         req.name.c_str());
            return 1;
        }
        // Clamped to the documented normalised range before it reaches
        // add_point, matching the CLAP side: a VST3 parameter value outside
        // 0..1 is not a value the plugin has any defined answer for.
        const double clamped = req.value < 0.0 ? 0.0 : (req.value > 1.0 ? 1.0 : req.value);
        armed.emplace_back(match->id, clamped);
    }

    std::vector<int> outputParams;
    for (size_t i = 0; i < params.size(); ++i)
        if (isOutputParameter(params[i]))
            outputParams.push_back(static_cast<int>(i));

    std::printf("plugin      : %s\n", bundle.c_str());
    std::printf("parameters  : %d (%zu output/read-only)\n", paramCount, outputParams.size());
    for (const int i : outputParams)
        std::printf("  output    : id=%u \"%s\" flags=0x%x\n", params[i].id, params[i].title.c_str(), params[i].flags);

    if (outputParams.empty())
    {
        std::fprintf(stderr, "FAIL: plugin exposes no output parameters; this test would pass vacuously\n");
        return 1;
    }

    v3_audio_processor** processor = nullptr;
    if (v3_cpp_obj_query_interface(component, v3_audio_processor_iid, &processor) != V3_OK || processor == nullptr)
    {
        std::fprintf(stderr, "FAIL: no audio processor\n");
        return 1;
    }

    // Buses: give every declared bus the arrangement matching the channel
    // count it reports, so a plugin with a sidechain (multi-comp) is set up the
    // way a host sets it up rather than being handed a short array. Only the
    // main buses are activated, which is what a host does for a plain insert.
    const int32_t numInBuses = v3_cpp_obj(component)->get_bus_count(component, V3_AUDIO, V3_INPUT);
    const int32_t numOutBuses = v3_cpp_obj(component)->get_bus_count(component, V3_AUDIO, V3_OUTPUT);

    // DAF matches an arrangement to a bus by channel count, so any mask with the
    // right population count is accepted. Building one from the low speaker bits
    // keeps a 4- or 6-channel bus working instead of being forced to stereo and
    // refused. Mono is spelled with the dedicated V3_SPEAKER_M so a 1-channel
    // bus is not described as "left only".
    auto arrangementFor = [](const int32_t channels) -> v3_speaker_arrangement {
        if (channels <= 0)
            return 0;
        if (channels == 1)
            return V3_SPEAKER_M;
        v3_speaker_arrangement arr = 0;
        for (int32_t i = 0; i < channels && i < 64; ++i)
            arr |= (static_cast<v3_speaker_arrangement>(1) << i);
        return arr;
    };

    std::vector<v3_speaker_arrangement> inArr, outArr;
    std::vector<int32_t> inChannels, outChannels;

    for (int32_t i = 0; i < numInBuses; ++i)
    {
        v3_bus_info info = {};
        if (v3_cpp_obj(component)->get_bus_info(component, V3_AUDIO, V3_INPUT, i, &info) != V3_OK)
        {
            std::fprintf(stderr, "FAIL: get_bus_info(input %d)\n", i);
            return 1;
        }
        inChannels.push_back(info.channel_count);
        inArr.push_back(arrangementFor(info.channel_count));
    }
    for (int32_t i = 0; i < numOutBuses; ++i)
    {
        v3_bus_info info = {};
        if (v3_cpp_obj(component)->get_bus_info(component, V3_AUDIO, V3_OUTPUT, i, &info) != V3_OK)
        {
            std::fprintf(stderr, "FAIL: get_bus_info(output %d)\n", i);
            return 1;
        }
        outChannels.push_back(info.channel_count);
        outArr.push_back(arrangementFor(info.channel_count));
    }

    // A refused arrangement leaves ports disabled, and the run would then
    // measure a configuration nobody asked for, so this is checked rather than
    // discarded.
    const v3_result arrRes = v3_cpp_obj(processor)->set_bus_arrangements(processor,
                                                                        inArr.empty() ? nullptr : inArr.data(),
                                                                        numInBuses,
                                                                        outArr.empty() ? nullptr : outArr.data(),
                                                                        numOutBuses);
    if (arrRes != V3_OK)
    {
        std::fprintf(stderr, "FAIL: set_bus_arrangements refused the plugin's own reported channel counts (%d)\n",
                     arrRes);
        return 1;
    }

    // Checked, not discarded: a refused main-bus activation makes DAF substitute
    // a silent dummy buffer, and the meters then legitimately sit still -- which
    // would surface as "the meters are not tracking the audio", blaming the
    // plugin for a host-side setup failure.
    for (int32_t i = 0; i < numInBuses; ++i)
    {
        if (v3_cpp_obj(component)->activate_bus(component, V3_AUDIO, V3_INPUT, i, i == 0) != V3_OK && i == 0)
        {
            std::fprintf(stderr, "FAIL: could not activate the main input bus\n");
            return 1;
        }
    }
    for (int32_t i = 0; i < numOutBuses; ++i)
    {
        if (v3_cpp_obj(component)->activate_bus(component, V3_AUDIO, V3_OUTPUT, i, i == 0) != V3_OK && i == 0)
        {
            std::fprintf(stderr, "FAIL: could not activate the main output bus\n");
            return 1;
        }
    }

    v3_process_setup setup = { V3_REALTIME, V3_SAMPLE_32, numFrames, 48000.0 };
    if (v3_cpp_obj(processor)->setup_processing(processor, &setup) != V3_OK)
    {
        std::fprintf(stderr, "FAIL: setup_processing\n");
        return 1;
    }

    if (v3_cpp_obj(component)->set_active(component, true) != V3_OK)
    {
        std::fprintf(stderr, "FAIL: set_active\n");
        return 1;
    }
    if (v3_cpp_obj(processor)->set_processing(processor, true) != V3_OK)
    {
        std::fprintf(stderr, "FAIL: set_processing(true)\n");
        return 1;
    }

    // Channel storage: one contiguous vector per channel, plus the per-bus
    // pointer arrays VST3 wants. Only the main input bus carries signal.
    std::vector<std::vector<float>> inStore, outStore;
    std::vector<std::vector<float*>> inPtrs(numInBuses), outPtrs(numOutBuses);
    std::vector<v3_audio_bus_buffers> inBuses(numInBuses), outBuses(numOutBuses);

    for (int32_t b = 0; b < numInBuses; ++b)
        for (int32_t c = 0; c < inChannels[b]; ++c)
            inStore.emplace_back(numFrames, 0.0f);
    for (int32_t b = 0; b < numOutBuses; ++b)
        for (int32_t c = 0; c < outChannels[b]; ++c)
            outStore.emplace_back(numFrames, 0.0f);

    {
        size_t next = 0;
        for (int32_t b = 0; b < numInBuses; ++b)
        {
            for (int32_t c = 0; c < inChannels[b]; ++c)
                inPtrs[b].push_back(inStore[next++].data());
            inBuses[b].num_channels = inChannels[b];
            inBuses[b].channel_silence_bitset = 0;
            inBuses[b].channel_buffers_32 = inPtrs[b].data();
        }
        next = 0;
        for (int32_t b = 0; b < numOutBuses; ++b)
        {
            for (int32_t c = 0; c < outChannels[b]; ++c)
                outPtrs[b].push_back(outStore[next++].data());
            outBuses[b].num_channels = outChannels[b];
            outBuses[b].channel_silence_bitset = 0;
            outBuses[b].channel_buffers_32 = outPtrs[b].data();
        }
    }

    // Sampled every block rather than at the two endpoints: a peak meter that
    // rises and decays back to rest between the first and last sample would
    // otherwise read as frozen and fail a correct plugin.
    std::vector<double> meterMin(outputParams.size()), meterMax(outputParams.size());
    std::vector<double> meterFirst(outputParams.size());
    for (size_t k = 0; k < outputParams.size(); ++k)
    {
        const double v = v3_cpp_obj(controller)->get_parameter_normalised(controller, params[outputParams[k]].id);
        meterFirst[k] = meterMin[k] = meterMax[k] = v;
    }

    HostParamChanges inputChanges, outputChanges;

    std::mt19937 rng(20260828u);
    std::uniform_real_distribution<float> noise(-0.5f, 0.5f);

    size_t outputParamEvents = 0, otherParamEvents = 0, unknownParamEvents = 0;
    std::vector<size_t> perParamEvents(params.size(), 0);

    auto countEvents = [&](const HostParamChanges& changes) {
        for (size_t qi = 0; qi < changes.used; ++qi)
        {
            const HostParamQueue& q(*changes.queues[qi]);
            const int idx = indexOf(q.paramId);

            if (idx < 0)
            {
                // Never excuse an id the controller did not enumerate: that is
                // exactly how a drift in the wrapper's id mapping would turn
                // this gate green with the bug fully present.
                unknownParamEvents += q.points.size();
                continue;
            }

            if (isOutputParameter(params[idx]))
                outputParamEvents += q.points.size();
            else
                otherParamEvents += q.points.size();

            perParamEvents[idx] += q.points.size();
        }
    };

    handler.processing = true;

    for (int block = 0; block < numBlocks; ++block)
    {
        // DAF reports output parameters from two places: the zero-frame early
        // return and the normal path. Both run every iteration so a regression
        // cannot hide on the empty-block branch hosts use at transport edges.
        {
            inputChanges.reset();
            outputChanges.reset();

            v3_process_data empty = {};
            empty.process_mode = V3_REALTIME;
            empty.symbolic_sample_size = V3_SAMPLE_32;
            empty.nframes = 0;
            empty.num_input_buses = numInBuses;
            empty.num_output_buses = numOutBuses;
            empty.inputs = numInBuses > 0 ? inBuses.data() : nullptr;
            empty.outputs = numOutBuses > 0 ? outBuses.data() : nullptr;
            empty.input_params = inputChanges.handle();
            empty.output_params = outputChanges.handle();
            empty.ctx = nullptr;

            if (v3_cpp_obj(processor)->process(processor, &empty) != V3_OK)
            {
                std::fprintf(stderr, "FAIL: process() returned an error on the zero-frame block %d\n", block);
                return 1;
            }
            countEvents(outputChanges);
        }

        if (numInBuses > 0)
        {
            for (int32_t c = 0; c < inChannels[0]; ++c)
            {
                float* const dst = inPtrs[0][c];
                for (int i = 0; i < numFrames; ++i)
                    dst[i] = silent ? 0.0f : noise(rng);
            }
        }

        inputChanges.reset();
        outputChanges.reset();

        // Armed values ride the first block, the way a host delivers a control
        // change: once applied they persist, so later blocks send nothing and
        // the "nothing reaches the host" assertions still see a quiet input.
        if (block == 0)
            for (const auto& a : armed)
            {
                int32_t queueIndex = 0;
                v3_param_value_queue** queue = v3_cpp_obj(inputChanges.handle())
                    ->add_param_data(inputChanges.handle(), &a.first, &queueIndex);
                if (queue != nullptr)
                {
                    int32_t pointIndex = 0;
                    v3_cpp_obj(queue)->add_point(queue, 0, a.second, &pointIndex);
                }
            }

        v3_process_data data = {};
        data.process_mode = V3_REALTIME;
        data.symbolic_sample_size = V3_SAMPLE_32;
        data.nframes = numFrames;
        data.num_input_buses = numInBuses;
        data.num_output_buses = numOutBuses;
        data.inputs = numInBuses > 0 ? inBuses.data() : nullptr;
        data.outputs = numOutBuses > 0 ? outBuses.data() : nullptr;
        data.input_params = inputChanges.handle();
        data.output_params = outputChanges.handle();
        data.input_events = nullptr;
        data.output_events = nullptr;
        data.ctx = nullptr;

        if (v3_cpp_obj(processor)->process(processor, &data) != V3_OK)
        {
            std::fprintf(stderr, "FAIL: process() returned an error on block %d\n", block);
            return 1;
        }

        countEvents(outputChanges);

        for (size_t k = 0; k < outputParams.size(); ++k)
        {
            const double v = v3_cpp_obj(controller)->get_parameter_normalised(controller,
                                                                             params[outputParams[k]].id);
            if (v < meterMin[k]) meterMin[k] = v;
            if (v > meterMax[k]) meterMax[k] = v;
        }
    }

    handler.processing = false;

    v3_cpp_obj(processor)->set_processing(processor, false);

    std::printf("\nblocks      : %d x %d frames @ 48 kHz, %s\n",
                numBlocks, numFrames, silent ? "digital silence" : "full-scale noise");
    std::printf("output-param events reaching the host : %zu\n", outputParamEvents);
    std::printf("other param events reaching the host  : %zu\n", otherParamEvents);
    std::printf("events for ids the controller never reported : %zu\n", unknownParamEvents);
    for (size_t i = 0; i < params.size(); ++i)
        if (perParamEvents[i] != 0)
            std::printf("  id=%u \"%s\" flags=0x%x -> %zu events\n",
                        params[i].id, params[i].title.c_str(), params[i].flags, perParamEvents[i]);

    std::printf("component-handler traffic during processing : %zu parameter edit(s), %zu restart(s)\n",
                handler.editsDuringProcessing, handler.restartsDuringProcessing);

    // Assertion 3 first: frozen meters would make 1 and 2 meaningless.
    bool meterMoved = false;
    std::printf("\nmeter read-back through get_parameter_normalised (first, min, max over the run):\n");
    for (size_t k = 0; k < outputParams.size(); ++k)
    {
        const ParamInfo& p(params[outputParams[k]]);
        std::printf("  id=%u \"%s\" first=%.6f min=%.6f max=%.6f\n",
                    p.id, p.title.c_str(), meterFirst[k], meterMin[k], meterMax[k]);
        if (meterMax[k] - meterMin[k] > 1.0e-4)
            meterMoved = true;
    }

    v3_cpp_obj(component)->set_active(component, false);

    // Failures go to stderr: it is unbuffered, so under `ctest --output-on-failure`
    // they stay next to any plugin diagnostics that explain them instead of being
    // reordered against block-buffered stdout.
    std::fflush(stdout);
    int failures = 0;

    if (! meterMoved)
    {
        std::fprintf(stderr,
                     "\nFAIL: no output parameter moved over %d blocks of %s;\n"
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
        std::fprintf(stderr,
                     "FAIL: %zu output-parameter events were written to outputParameterChanges.\n"
                     "      A VST3 host sees these as parameter edits made by the plugin; Bitwig\n"
                     "      records each as an undoable change and stops offering undo/redo\n"
                     "      (dusk-audio/plugins#233). Expected 0.\n", outputParamEvents);
        ++failures;
    }
    else
    {
        std::printf("PASS: no output-parameter events reached the host.\n");
    }

    // The IComponentHandler channel. DAF drives it only from the UI message
    // path, so anything at all arriving here from process() is a regression --
    // and a worse one than the outputParameterChanges flood, since begin_edit /
    // perform_edit is unambiguously "the user edited this".
    size_t outputParamEdits = 0;
    for (const std::vector<v3_param_id>* list : { &handler.beginEdits, &handler.performEdits, &handler.endEdits })
        for (const v3_param_id id : *list)
        {
            const int idx = indexOf(id);
            if (idx >= 0 && isOutputParameter(params[idx]))
                ++outputParamEdits;
        }

    if (outputParamEdits != 0 || handler.editsDuringProcessing != 0 || handler.restartsDuringProcessing != 0)
    {
        std::fprintf(stderr,
                     "FAIL: the plugin drove IComponentHandler from the audio callback\n"
                     "      (%zu edit(s) for output parameters, %zu edit(s) and %zu restart_component\n"
                     "      call(s) during processing). Expected 0 of each.\n",
                     outputParamEdits, handler.editsDuringProcessing, handler.restartsDuringProcessing);
        ++failures;
    }
    else
    {
        std::printf("PASS: no output parameter reached the host through IComponentHandler.\n");
    }

    if (unknownParamEvents != 0)
    {
        std::fprintf(stderr,
                     "FAIL: %zu events carried parameter ids the edit controller never enumerated,\n"
                     "      so they could not be classified. Until the id mapping is understood\n"
                     "      this test cannot tell a meter event from a legitimate one.\n",
                     unknownParamEvents);
        ++failures;
    }

    // Walk the plugin all the way back down, in the reverse order of the way up.
    // Not housekeeping: DAF refuses to destroy a component whose audio processor
    // or edit controller is still referenced, parks it in gComponentGarbage and
    // prints "asked to delete component while audio processor still active", so
    // skipping any one of these unrefs means the destructor under test never
    // runs at all.
    v3_cpp_obj(controller)->set_component_handler(controller, nullptr);
    v3_cpp_obj_unref(processor);        // query_interface above took a reference
    v3_cpp_obj_unref(controller);       // and so did that one
    v3_cpp_obj_terminate(component);
    v3_cpp_obj_unref(component);
    if (factory3 != nullptr)
        v3_cpp_obj_unref(factory3);
    if (moduleExit != nullptr)
        moduleExit();

    std::printf("\n%s\n", failures == 0 ? "RESULT: PASS" : "RESULT: FAIL");
    return failures == 0 ? 0 : 1;
}
