// Copyright (C) 2026 Dusk Audio — GPL-3.0-or-later.
// Native hosted layout regression. Host ABI setup follows the shared
// DafVst3OutputParamTest fixture; this plugin has no output parameters.
#include "travesty/audio_processor.h"
#include "travesty/bstream.h"
#include "travesty/component.h"
#include "travesty/edit_controller.h"
#include "travesty/factory.h"
#include "travesty/host.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "DuskVerbTestLibrary.hpp"
#include <utility>
#include <vector>
static uint32_t V3_API refStub(void *) { return 1; }
static uint32_t V3_API unrefStub(void *) { return 1; }
struct StateStream : v3_bstream_cpp
{
    StateStream *selfptr = this;
    std::vector<char> bytes;
    size_t position = 0;
    v3_bstream **handle() { return (v3_bstream **)&selfptr; }
    StateStream()
    {
        query_interface = query;
        ref = refStub;
        unref = unrefStub;
        stream.read = read;
        stream.write = write;
        stream.seek = seek;
        stream.tell = tell;
    }
    static StateStream &self(void *p) { return **(StateStream **)p; }
    static v3_result V3_API query(void *p, const v3_tuid iid, void **out)
    {
        if (v3_tuid_match(iid, v3_funknown_iid) || v3_tuid_match(iid, v3_bstream_iid))
        {
            *out = p;
            return V3_OK;
        }
        *out = nullptr;
        return V3_NO_INTERFACE;
    }
    static v3_result V3_API read(void *p, void *data, int32_t n, int32_t *done)
    {
        auto &s = self(p);
        if (n < 0)
            return V3_INVALID_ARG;
        const auto count = std::min(size_t(n), s.bytes.size() - s.position);
        std::memcpy(data, s.bytes.data() + s.position, count);
        s.position += count;
        if (done)
            *done = int32_t(count);
        return V3_OK;
    }
    static v3_result V3_API write(void *p, void *data, int32_t n, int32_t *done)
    {
        auto &s = self(p);
        if (n < 0)
            return V3_INVALID_ARG;
        s.bytes.resize(s.position + n);
        std::memcpy(s.bytes.data() + s.position, data, n);
        s.position += n;
        if (done)
            *done = n;
        return V3_OK;
    }
    static v3_result V3_API seek(void *p, int64_t n, int32_t mode, int64_t *result)
    {
        auto &s = self(p);
        const int64_t pos = n + (mode == V3_SEEK_CUR   ? int64_t(s.position)
                                 : mode == V3_SEEK_END ? int64_t(s.bytes.size())
                                                       : 0);
        if (pos < 0 || size_t(pos) > s.bytes.size())
            return V3_INVALID_ARG;
        s.position = size_t(pos);
        if (result)
            *result = pos;
        return V3_OK;
    }
    static v3_result V3_API tell(void *p, int64_t *position)
    {
        *position = self(p).position;
        return V3_OK;
    }
};
struct ParamQueue : v3_param_value_queue_cpp
{
    ParamQueue *selfptr = this;
    v3_param_id id = 0;
    double value = 0.23;
    bool twoPoints = false;
    v3_param_value_queue **handle() { return (v3_param_value_queue **)&selfptr; }
    ParamQueue()
    {
        query_interface = query;
        ref = refStub;
        unref = unrefStub;
        queue.get_param_id = paramId;
        queue.get_point_count = count;
        queue.get_point = point;
        queue.add_point = add;
    }
    static v3_result V3_API query(void *self, const v3_tuid iid, void **obj)
    {
        if (v3_tuid_match(iid, v3_funknown_iid) || v3_tuid_match(iid, v3_param_value_queue_iid))
        {
            *obj = self;
            return V3_OK;
        }
        *obj = nullptr;
        return V3_NO_INTERFACE;
    }
    static v3_param_id V3_API paramId(void *self) { return (*(ParamQueue **)self)->id; }
    static int32_t V3_API count(void *self) { return (*(ParamQueue **)self)->twoPoints ? 2 : 1; }
    static v3_result V3_API point(void *self, int32_t i, int32_t *offset, double *value)
    {
        auto &q = **(ParamQueue **)self;
        if (i < 0 || i >= (q.twoPoints ? 2 : 1))
            return V3_INVALID_ARG;
        *offset = q.twoPoints && i == 1 ? 63 : 0;
        *value = q.twoPoints && i == 0 ? 0.11 : q.value;
        return V3_OK;
    }
    static v3_result V3_API add(void *, int32_t, double, int32_t *) { return V3_NOT_IMPLEMENTED; }
};
struct EmptyChanges : v3_param_changes_cpp
{
    unsigned attemptedWrites = 0;
    ParamQueue *parameter = nullptr;
    EmptyChanges *selfptr = this;
    v3_param_changes **handle() { return (v3_param_changes **)&selfptr; }
    EmptyChanges()
    {
        query_interface = query;
        ref = refStub;
        unref = unrefStub;
        changes.get_param_count = count;
        changes.get_param_data = get;
        changes.add_param_data = add;
    }
    static v3_result V3_API query(void *self, const v3_tuid iid, void **obj)
    {
        if (v3_tuid_match(iid, v3_funknown_iid) || v3_tuid_match(iid, v3_param_changes_iid))
        {
            *obj = self;
            return V3_OK;
        }
        *obj = nullptr;
        return V3_NO_INTERFACE;
    }
    static int32_t V3_API count(void *self) { return (*(EmptyChanges **)self)->parameter ? 1 : 0; }
    static v3_param_value_queue **V3_API get(void *self, int32_t i)
    {
        auto *p = (*(EmptyChanges **)self)->parameter;
        return i == 0 && p ? p->handle() : nullptr;
    }
    static v3_param_value_queue **V3_API add(void *self, const v3_param_id *, int32_t *)
    {
        ++(*(EmptyChanges **)self)->attemptedWrites;
        return nullptr;
    }
};
struct HostApplication : v3_host_application_cpp
{
    HostApplication *selfptr = this;
    v3_funknown **handle() { return (v3_funknown **)&selfptr; }

    HostApplication()
    {
        query_interface = queryInterface;
        ref = refStub;
        unref = unrefStub;
        app.get_name = getName;
        app.create_instance = createInstance;
    }

    static v3_result V3_API queryInterface(void *const self, const v3_tuid iid, void **const obj)
    {
        if (v3_tuid_match(iid, v3_funknown_iid) || v3_tuid_match(iid, v3_host_application_iid))
        {
            *obj = self;
            return V3_OK;
        }
        *obj = nullptr;
        return V3_NO_INTERFACE;
    }

    static v3_result V3_API getName(void *, v3_str_128 name)
    {
        static const char *const kName = "DuskVerbVst3LayoutTest";
        int i = 0;
        for (; kName[i] != '\0'; ++i)
            name[i] = static_cast<int16_t>(kName[i]);
        name[i] = 0;
        return V3_OK;
    }

    // The wrapper only asks the host to create message and attribute-list
    // objects, which this test never exercises: it never opens the UI, and the
    // meter path under test runs entirely inside process().
    static v3_result V3_API createInstance(void *, v3_tuid, v3_tuid, void **const obj)
    {
        *obj = nullptr;
        return V3_NOT_IMPLEMENTED;
    }
};

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    auto lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib)
        return 2;
#ifdef __APPLE__
    auto moduleEntry = (bool (*)(void *))dlsym(lib, "bundleEntry");
    auto moduleExit = (bool (*)())dlsym(lib, "bundleExit");
#elif defined(_WIN32)
    auto moduleEntry = (bool (*)())dlsym(lib, "InitDll");
    auto moduleExit = (bool (*)())dlsym(lib, "ExitDll");
#else
    auto moduleEntry = (bool (*)(void *))dlsym(lib, "ModuleEntry");
    auto moduleExit = (bool (*)())dlsym(lib, "ModuleExit");
#endif
    auto getFactory = (const void *(*)())dlsym(lib, "GetPluginFactory");
#if defined(_WIN32)
    if (!getFactory || (moduleEntry && !moduleEntry()))
#else
    if (!getFactory || (moduleEntry && !moduleEntry(lib)))
#endif
        return 2;
    auto factory = (v3_plugin_factory **)getFactory();
    HostApplication host;
    v3_tuid cid{};
    bool found = false;
    for (int i = 0; i < v3_cpp_obj(factory)->num_classes(factory); ++i)
    {
        v3_class_info info{};
        v3_cpp_obj(factory)->get_class_info(factory, i, &info);
        if (std::strcmp(info.category, "Audio Module Class") == 0)
        {
            std::memcpy(cid, info.class_id, sizeof(cid));
            found = true;
            break;
        }
    }
    if (!found)
        return 2;
    int failures = 0;
    auto check = [&](bool ok, const char *msg)
    {
        if (!ok)
        {
            ++failures;
            std::printf("FAIL: %s\n", msg);
        }
    };
    std::vector<float> referenceStereo;
    for (bool twoPoints : {false, true})
    {
        std::vector<float> monoStereo, stereo;
        for (auto layout : {std::pair<int, int>{1, 1}, {1, 2}, {2, 2}})
        {
            v3_component **component = nullptr;
            if (v3_cpp_obj(factory)->create_instance(factory, cid, v3_component_iid, (void **)&component) !=
                V3_OK)
                return 2;
            if (v3_cpp_obj_initialize(component, host.handle()) != V3_OK)
                return 2;
            v3_audio_processor **processor = nullptr;
            if (v3_cpp_obj_query_interface(component, v3_audio_processor_iid, &processor) != V3_OK)
                return 2;
            v3_edit_controller **controller = nullptr;
            if (v3_cpp_obj_query_interface(component, v3_edit_controller_iid, &controller) != V3_OK)
                return 2;
            ParamQueue parameter;
            v3_param_id programId = 0;
            int programSteps = 0;
            bool foundMix = false;
            for (int i = 0; i < v3_cpp_obj(controller)->get_parameter_count(controller); ++i)
            {
                v3_param_info info{};
                v3_cpp_obj(controller)->get_parameter_info(controller, i, &info);
                if (info.flags & V3_PARAM_PROGRAM_CHANGE)
                {
                    programId = info.param_id;
                    programSteps = info.step_count;
                }
                char title[129]{};
                for (int j = 0; j < 128 && info.title[j]; ++j)
                    title[j] = char(info.title[j]);
                if (std::strcmp(title, "Dry/Wet") == 0)
                {
                    parameter.id = info.param_id;
                    foundMix = true;
                }
            }
            check(foundMix, "mix parameter enumerated");
            check(programSteps == 19, "twenty host factory programs enumerated");
            v3_cpp_obj(controller)->set_parameter_normalised(controller, programId, 2.0 / programSteps);
            StateStream saved;
            check(v3_cpp_obj(component)->get_state(component, saved.handle()) == V3_OK,
                  "complete native state saved");
            v3_cpp_obj(controller)->set_parameter_normalised(controller, programId, 5.0 / programSteps);
            saved.position = 0;
            check(v3_cpp_obj(component)->set_state(component, saved.handle()) == V3_OK,
                  "complete native state restored");
            check(std::abs(v3_cpp_obj(controller)->get_parameter_normalised(controller, programId) -
                           2.0 / programSteps) < 1e-6,
                  "snapshot restores host program identity without loading factory defaults");
            auto arr = [](int n)
            { return v3_speaker_arrangement(n == 1 ? V3_SPEAKER_M : V3_SPEAKER_L | V3_SPEAKER_R); };
            auto in = arr(layout.first), out = arr(layout.second);
            const bool accepted =
                v3_cpp_obj(processor)->set_bus_arrangements(processor, &in, 1, &out, 1) == V3_OK;
            check(accepted, "requested supported arrangement accepted");
            if (accepted)
            {
                v3_speaker_arrangement actual = 0;
                check(v3_cpp_obj(processor)->get_bus_arrangement(processor, V3_INPUT, 0, &actual) == V3_OK &&
                          actual == in,
                      "input arrangement readback matches selection");
                check(v3_cpp_obj(processor)->get_bus_arrangement(processor, V3_OUTPUT, 0, &actual) == V3_OK &&
                          actual == out,
                      "output arrangement readback matches selection");
                v3_bus_info info{};
                check(v3_cpp_obj(component)->get_bus_info(component, V3_AUDIO, V3_INPUT, 0, &info) == V3_OK &&
                          info.channel_count == layout.first,
                      "input metadata matches selection");
                check(v3_cpp_obj(component)->get_bus_info(component, V3_AUDIO, V3_OUTPUT, 0, &info) ==
                              V3_OK &&
                          info.channel_count == layout.second,
                      "output metadata matches selection");
                auto badIn = arr(2), badOut = arr(1);
                check(v3_cpp_obj(processor)->set_bus_arrangements(processor, &badIn, 1, &badOut, 1) != V3_OK,
                      "unadvertised 2/1 rejected");
                check(v3_cpp_obj(processor)->get_bus_arrangement(processor, V3_INPUT, 0, &actual) == V3_OK &&
                          actual == in,
                      "rejected arrangement leaves input selection unchanged");
                v3_cpp_obj(component)->activate_bus(component, V3_AUDIO, V3_INPUT, 0, true);
                v3_cpp_obj(component)->activate_bus(component, V3_AUDIO, V3_OUTPUT, 0, true);
                v3_process_setup setup{V3_REALTIME, V3_SAMPLE_32, 512, 48000};
                check(v3_cpp_obj(processor)->setup_processing(processor, &setup) == V3_OK,
                      "setup processing");
                auto invalidSetup = setup;
                invalidSetup.max_block_size = 0;
                check(v3_cpp_obj(processor)->setup_processing(processor, &invalidSetup) == V3_INVALID_ARG,
                      "zero maximum block size rejected");
                check(v3_cpp_obj(processor)->setup_processing(processor, &setup) == V3_OK,
                      "valid repeated setup remains supported");
                v3_cpp_obj(component)->set_active(component, true);
                v3_cpp_obj(processor)->set_processing(processor, true);
                float signal[512]{}, l[512]{}, r[512]{};
                float *inputs[] = {signal, signal};
                float *outputs[] = {l, r};
                v3_audio_bus_buffers input{}, output{};
                input.num_channels = layout.first;
                input.channel_buffers_32 = inputs;
                output.num_channels = layout.second;
                output.channel_buffers_32 = outputs;
                EmptyChanges inParams, outParams;
                v3_process_data data{};
                data.process_mode = V3_REALTIME;
                data.symbolic_sample_size = V3_SAMPLE_32;
                data.num_input_buses = data.num_output_buses = 1;
                data.inputs = &input;
                data.outputs = &output;
                data.input_params = inParams.handle();
                data.output_params = outParams.handle();
                std::vector<float> result;
                float peak = 0;
                for (int b = 0; b < 32; ++b)
                {
                    parameter.twoPoints = b == 2 && twoPoints;
                    parameter.value = b == 0 ? 0.23 : 0.73;
                    inParams.parameter = b == 0 || b == 2 ? &parameter : nullptr;
                    data.nframes = b == 0 ? 0 : (b == 1 ? 1 : 127);
                    for (int i = 0; i < 512; ++i)
                        signal[i] = 0.2f * std::sin(float(i + b * 127) * 0.09f);
                    check(v3_cpp_obj(processor)->process(processor, &data) == V3_OK,
                          "variable block renders");
                    if (b == 0)
                        check(std::abs(
                                  v3_cpp_obj(controller)->get_parameter_normalised(controller, parameter.id) -
                                  0.23) < 1e-6,
                              "zero-frame parameter flush updates the plugin");
                    if (b == 0)
                        check(outParams.attemptedWrites == 0,
                              "session recall does not emit host automation edits");
                    for (int i = 0; i < data.nframes; ++i)
                    {
                        check(std::isfinite(l[i]) && (layout.second == 1 || std::isfinite(r[i])),
                              "finite output");
                        peak = std::max(peak, std::abs(l[i]));
                        result.push_back(l[i]);
                        if (layout.second == 2)
                            result.push_back(r[i]);
                    }
                }
                check(peak > 0.01f, "nonzero input produces nonzero output");
                if (layout.first == 1 && layout.second == 2)
                    monoStereo = result;
                if (layout.first == 2 && layout.second == 2)
                    stereo = result;
                // Hosts deliver program changes in the processing queue too,
                // including zero-frame flushes. Compare complete parameter
                // readback with the controller's existing program-load path.
                for (int frames : {0, 127})
                {
                    const double target = (frames == 0 ? 5.0 : 7.0) / programSteps;
                    v3_cpp_obj(controller)->set_parameter_normalised(controller, programId, target);
                    std::vector<std::pair<v3_param_id, double>> expected;
                    for (int i = 0; i < v3_cpp_obj(controller)->get_parameter_count(controller); ++i)
                    {
                        v3_param_info info{};
                        v3_cpp_obj(controller)->get_parameter_info(controller, i, &info);
                        expected.emplace_back(info.param_id,
                            v3_cpp_obj(controller)->get_parameter_normalised(controller, info.param_id));
                    }
                    v3_cpp_obj(controller)->set_parameter_normalised(controller, programId, 2.0 / programSteps);
                    ParamQueue programQueue;
                    programQueue.id = programId;
                    programQueue.value = target;
                    programQueue.twoPoints = frames != 0;
                    inParams.parameter = &programQueue;
                    data.nframes = frames;
                    outParams.attemptedWrites = 0;
                    check(v3_cpp_obj(processor)->process(processor, &data) == V3_OK,
                          "queued program processing succeeds");
                    bool matches = true;
                    for (const auto& value : expected)
                        matches = matches && std::abs(v3_cpp_obj(controller)->get_parameter_normalised(
                            controller, value.first) - value.second) < 1e-6;
                    check(matches, "queued program restores complete controller parameter values and identity");
                    check(outParams.attemptedWrites == 0, "queued recall does not create automation edits");
                }
                v3_cpp_obj(processor)->set_processing(processor, false);
                v3_cpp_obj(component)->set_active(component, false);
            }
            v3_cpp_obj_unref(controller);
            v3_cpp_obj_unref(processor);
            v3_cpp_obj_terminate(component);
            v3_cpp_obj_unref(component);
        }
        check(!monoStereo.empty() && monoStereo == stereo, "1/2 audio exactly equals duplicated-mono stereo");
        if (!twoPoints)
            referenceStereo = stereo;
        else
            check(!stereo.empty() && stereo == referenceStereo,
                  "block automation uses the final queue value before audio, matching JUCE");
    }
    v3_cpp_obj_unref(factory);
    if (moduleExit)
        moduleExit();
    dlclose(lib);
    std::printf("VST3 layouts: %d failures\n", failures);
    return failures ? 1 : 0;
}
