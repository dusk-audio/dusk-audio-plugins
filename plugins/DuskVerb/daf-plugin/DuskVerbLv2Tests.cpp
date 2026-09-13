// Copyright (C) 2026 Dusk Audio — GPL-3.0-or-later.
// Exercise state recall through the built LV2, including host-owned controls.
#include "DuskVerbParams.hpp"
#include "lv2/atom.h"
#include "lv2/buf-size.h"
#include "lv2/control-input-port-change-request.h"
#include "lv2/lv2.h"
#include "lv2/options.h"
#include "lv2/lv2_programs.h"
#include "lv2/state.h"
#include "lv2/urid.h"
#include "lv2/worker.h"
#include <cstdio>
#include <dlfcn.h>
#include <map>

namespace
{
int failures = 0;
void check(bool ok, const char *message)
{
    if (!ok)
    {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}
struct Host
{
    std::map<std::string, uint32_t> urids;
    std::string state;
    std::array<float, duskverb::kNumParams> requested{};
    unsigned requests = 0;
    LV2_Atom scheduled{};
    unsigned scheduledCount = 0;
    static uint32_t map(void *h, const char *uri)
    {
        auto &ids = static_cast<Host *>(h)->urids;
        auto found = ids.find(uri);
        if (found != ids.end())
            return found->second;
        const auto id = uint32_t(ids.size() + 1);
        ids.emplace(uri, id);
        return id;
    }
    static LV2_Worker_Status schedule(void *h, uint32_t size, const void *data)
    {
        auto& host = *static_cast<Host*>(h);
        if (size != sizeof(host.scheduled)) return LV2_WORKER_ERR_NO_SPACE;
        std::memcpy(&host.scheduled, data, size);
        ++host.scheduledCount;
        return LV2_WORKER_SUCCESS;
    }
    static const void *retrieve(void *h, uint32_t key, size_t *size, uint32_t *type, uint32_t *flags)
    {
        auto &host = *static_cast<Host *>(h);
        if (key != map(h, "https://dusk-audio.github.io/plugins/duskverb-2#parameters"))
            return nullptr;
        *size = host.state.size() + 1;
        *type = map(h, LV2_ATOM__String);
        *flags = LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE;
        return host.state.c_str();
    }
    static LV2_State_Status store(void *h, uint32_t key, const void *value, size_t, uint32_t, uint32_t)
    {
        if (key == map(h, "https://dusk-audio.github.io/plugins/duskverb-2#parameters"))
            static_cast<Host *>(h)->state = static_cast<const char *>(value);
        return LV2_STATE_SUCCESS;
    }
    static LV2_ControlInputPort_Change_Status request(void *h, uint32_t port, float value)
    {
        auto &host = *static_cast<Host *>(h);
        if (port < 6 || port >= 6 + duskverb::kNumParams)
            return LV2_CONTROL_INPUT_PORT_CHANGE_ERR_INVALID_INDEX;
        host.requested[port - 6] = value;
        ++host.requests;
        return LV2_CONTROL_INPUT_PORT_CHANGE_SUCCESS;
    }
};
} // namespace
int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library)
    {
        std::fprintf(stderr, "%s\n", dlerror());
        return 2;
    }
    auto entry = reinterpret_cast<const LV2_Descriptor *(*)(uint32_t)>(dlsym(library, "lv2_descriptor"));
    if (!entry)
        return 2;
    const auto *descriptor = entry(0);
    const auto *state =
        static_cast<const LV2_State_Interface *>(descriptor->extension_data(LV2_STATE__interface));
    if (!state)
        return 2;
    for (bool supportsRequests : {false, true})
    {
        Host host;
        LV2_URID_Map map{&host, Host::map};
        LV2_Worker_Schedule worker{&host, Host::schedule};
        LV2_ControlInputPort_Change_Request request{&host, Host::request};
        int block = 64;
        LV2_Options_Option options[] = {{LV2_OPTIONS_INSTANCE, 0,
                                         Host::map(&host, LV2_BUF_SIZE__nominalBlockLength), sizeof(block),
                                         Host::map(&host, LV2_ATOM__Int), &block},
                                        {}};
        LV2_Feature mapFeature{LV2_URID__map, &map}, workerFeature{LV2_WORKER__schedule, &worker},
            optionsFeature{LV2_OPTIONS__options, options},
            requestFeature{LV2_CONTROL_INPUT_PORT_CHANGE_REQUEST_URI, &request};
        const LV2_Feature *features[] = {&mapFeature, &workerFeature, &optionsFeature,
                                         supportsRequests ? &requestFeature : nullptr, nullptr};
        auto instance = descriptor->instantiate(descriptor, 48000, "", features);
        if (!instance)
            return 2;
        std::array<float, duskverb::kNumParams> controls;
        for (int i = 0; i < duskverb::kNumParams; ++i)
        {
            controls[i] = duskverb::hostDefault(duskverb::paramDesc(i));
            if (i == duskverb::Bypass)
                controls[i] = 1 - controls[i];
            descriptor->connect_port(instance, 6 + i, &controls[i]);
        }
        const auto originalControls = controls;
        std::array<float, 64> silence{}, left{}, right{};
        for (int i = 0; i < 2; ++i)
            descriptor->connect_port(instance, i, silence.data());
        descriptor->connect_port(instance, 2, left.data());
        descriptor->connect_port(instance, 3, right.data());
        alignas(8) std::array<char, 65536> events{};
        LV2_Atom_Sequence inputEvents{};
        inputEvents.atom.type = Host::map(&host, LV2_ATOM__Sequence);
        inputEvents.atom.size = sizeof(LV2_Atom_Sequence_Body);
        descriptor->connect_port(instance, 4, &inputEvents);
        descriptor->connect_port(instance, 5, events.data());
        auto run = [&]
        {
            reinterpret_cast<LV2_Atom *>(events.data())->size = events.size() - sizeof(LV2_Atom);
            descriptor->run(instance, 64);
        };
        descriptor->activate(instance);
        run();
        duskverb::StateValues recalled;
        for (int i = 0; i < duskverb::kNumParams; ++i)
            recalled.params[i] = duskverb::hostDefault(duskverb::paramDesc(i));
        recalled.params[duskverb::Mix] = 0.23f;
        recalled.sixAP.densityBaseline = 0.2f;
        host.state = duskverb::encodeState(recalled);
        check(state->restore(instance, Host::retrieve, &host, 0, features) == LV2_STATE_SUCCESS,
              "valid recall accepted");
        run();
        check(controls == originalControls, "recall never writes host input buffers");
        state->save(instance, Host::store, &host, 0, features);
        duskverb::StateValues observed;
        check(duskverb::decodeState(host.state, observed) && observed.params == recalled.params &&
                  observed.sixAP.densityBaseline == 0.2f,
              "stale host controls cannot overwrite recalled sound");
        if (supportsRequests)
        {
            check(host.requests == duskverb::kNumParams,
                  "all recalled controls requested through host callback");
            check(host.requested[duskverb::Mix] == 0.23f && host.requested[duskverb::Bypass] == 1,
                  "host request carries mix and LV2 enabled polarity");
            controls = host.requested;
            run();
            state->save(instance, Host::store, &host, 0, features);
            check(duskverb::decodeState(host.state, observed) && !observed.edited,
                  "host echo of recalled controls preserves unedited identity");
        }
        controls[duskverb::Mix] = 0.71f;
        run();
        state->save(instance, Host::store, &host, 0, features);
        check(duskverb::decodeState(host.state, observed) && observed.params[duskverb::Mix] == 0.71f,
              "subsequent host automation still applies");
        const auto beforeInvalid = host.state;
        host.state = "invalid snapshot";
        check(state->restore(instance, Host::retrieve, &host, 0, features) != LV2_STATE_SUCCESS,
              "malformed recall reports failure");
        run();
        state->save(instance, Host::store, &host, 0, features);
        check(host.state == beforeInvalid, "malformed recall leaves sound unchanged");
        host.state = duskverb::encodeState(recalled);
        check(state->restore(instance, Host::retrieve, &host, 0, features) == LV2_STATE_SUCCESS,
              "second recall accepted");
        controls[duskverb::Mix] = 0.67f;
        run();
        state->save(instance, Host::store, &host, 0, features);
        check(duskverb::decodeState(host.state, observed) && observed.params[duskverb::Mix] == 0.67f,
              "host automation in the first recall cycle is not swallowed");
        const auto* programs = static_cast<const LV2_Programs_Interface*>(
            descriptor->extension_data(LV2_PROGRAMS__Interface));
        const auto* workers = static_cast<const LV2_Worker_Interface*>(
            descriptor->extension_data(LV2_WORKER__interface));
        if (!programs || !workers) return 2;
        programs->select_program(instance, 0, 5);
        check(host.scheduledCount == 1, "program serialization is deferred to the worker");
        if (host.scheduledCount == 1)
            check(workers->work(instance, nullptr, nullptr, sizeof(host.scheduled), &host.scheduled) == LV2_WORKER_SUCCESS,
                  "deferred program state refresh succeeds");
        run();
        state->save(instance, Host::store, &host, 0, features);
        check(duskverb::decodeState(host.state, observed)
                  && observed.presetName == getFactoryPresets()[5].name && !observed.edited,
              "deferred refresh and host control echo preserve selected program identity");
        descriptor->deactivate(instance);
        descriptor->cleanup(instance);
    }
    dlclose(library);
    std::printf("LV2 hosted state: %d failures\n", failures);
    return failures ? 1 : 0;
}
