// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Ring Out's trigger parameters through the real CLAP plugin, as a host sees them.
//
// SETUP, ADD and RESET are triggers: momentary actions, not settings. A trigger is never part of a
// saved session, so a project saved while the detector was listening cannot re-arm it on a live PA.
// The wrapper has to deliver the press to the plugin and leave the parameter at its default, because
// that is the state a save captures and a reload restores.
//
// It did not. Five clap-validator 0.4.1 tests failed on exactly that:
//
//   param-set-events, param-set-no-cookies
//     A press applied through clap_plugin_params::flush() (where there is no run to complete it)
//     left the parameter reading 1, while the same press through process() was retired by the run.
//     The validator compares the two.
//   state-reproducibility-basic/-binary/-buffered
//     The validator flushes parameters, reads their values, saves and reloads. The trigger was not
//     in the saved state (by design), so the reloaded instance read 0 against a saved 1.
//
// The wrapper now reports a trigger's default value while a pulse is still held, and completes any
// pulse the run did not by setting the value back through the plugin's own setter and reporting the
// change to the host -- the same contract VST2 and VST3 have always enforced. A plugin that consumes
// triggers in setParameterValue (Ring Out, which acts on the press as it arrives) and one that reads
// the value on its next run both receive it exactly once (DAF's own tests/CLAPWrapper.cpp covers the
// two consumption points; this harness covers what Ring Out's parameters do).
//
// The assertions here are the five failures in miniature, plus the two things a fix must not trade
// away to make them pass: the press still reaches the plugin (a RESET delivered through flush clears
// a loaded filter table), and a trigger is still absent from the saved state.
//
// Linux and macOS only (dlopen plus the macOS bundle layout below).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <dlfcn.h>

// DAF vendors a trimmed CLAP SDK with no umbrella clap.h, so the pieces this harness needs are
// included individually.
#include "clap/entry.h"
#include "clap/plugin.h"
#include "clap/plugin-features.h"
#include "clap/ext/audio-ports.h"
#include "clap/ext/params.h"
#include "clap/ext/state.h"
#include "clap/factory/plugin-factory.h"

// --------------------------------------------------------------------------------------------------------------------
// minimal host

static const void* CLAP_ABI hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
static void CLAP_ABI hostRequestRestart(const clap_host_t*) {}
static void CLAP_ABI hostRequestProcess(const clap_host_t*) {}
static void CLAP_ABI hostRequestCallback(const clap_host_t*) {}

static clap_host_t gHost = {
    CLAP_VERSION_INIT, nullptr,
    "DafClapTriggerTest", "Dusk Audio", "https://dusk-audio.github.io/", "1.0.0",
    hostGetExtension, hostRequestRestart, hostRequestProcess, hostRequestCallback
};

// ---- events ---------------------------------------------------------------------------------------------------------

struct InEvents {
    clap_input_events_t iface;
    std::vector<clap_event_param_value> events;

    InEvents()
    {
        iface.ctx = this;
        iface.size = size;
        iface.get = get;
    }

    void add(clap_id paramId, double value)
    {
        clap_event_param_value ev = {};
        ev.header.size = sizeof(ev);
        ev.header.time = 0;
        ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.header.type = CLAP_EVENT_PARAM_VALUE;
        ev.header.flags = 0;
        ev.param_id = paramId;
        ev.cookie = nullptr;
        ev.note_id = ev.port_index = -1;
        ev.channel = ev.key = -1;
        ev.value = value;
        events.push_back(ev);
    }

    void clear() { events.clear(); }

    static uint32_t CLAP_ABI size(const clap_input_events_t* list)
    {
        return static_cast<uint32_t>(static_cast<const InEvents*>(list->ctx)->events.size());
    }

    static const clap_event_header_t* CLAP_ABI get(const clap_input_events_t* list, uint32_t index)
    {
        const InEvents* self = static_cast<const InEvents*>(list->ctx);
        return index < self->events.size() ? &self->events[index].header : nullptr;
    }
};

struct CollectedEvent {
    clap_id paramId;
    double value;
};

struct OutEvents {
    clap_output_events_t iface;
    std::vector<CollectedEvent> events;

    OutEvents()
    {
        iface.ctx = this;
        iface.try_push = tryPush;
    }

    static bool CLAP_ABI tryPush(const clap_output_events_t* list, const clap_event_header_t* event)
    {
        OutEvents* self = static_cast<OutEvents*>(list->ctx);
        if (event->type == CLAP_EVENT_PARAM_VALUE)
        {
            const clap_event_param_value_t* pv = (const clap_event_param_value_t*) event;
            self->events.push_back({ pv->param_id, pv->value });
        }
        return true;
    }

    size_t countFor(clap_id id) const
    {
        size_t n = 0;
        for (const CollectedEvent& e : events)
            if (e.paramId == id)
                ++n;
        return n;
    }

    bool reportedValue(clap_id id, double value) const
    {
        for (const CollectedEvent& e : events)
            if (e.paramId == id && std::fabs(e.value - value) < 1.0e-9)
                return true;
        return false;
    }
};

// ---- state streams --------------------------------------------------------------------------------------------------

struct OStream { std::string data; };

static int64_t CLAP_ABI ostreamWrite(const clap_ostream_t* stream, const void* buffer, uint64_t size)
{
    static_cast<OStream*>(stream->ctx)->data.append(static_cast<const char*>(buffer), size);
    return static_cast<int64_t>(size);
}

struct IStream { const std::string* data; size_t pos = 0; };

static int64_t CLAP_ABI istreamRead(const clap_istream_t* stream, void* buffer, uint64_t size)
{
    IStream* in = static_cast<IStream*>(stream->ctx);
    const size_t count = std::min<size_t>(size, in->data->size() - in->pos);
    std::memcpy(buffer, in->data->data() + in->pos, count);
    in->pos += count;
    return static_cast<int64_t>(count);
}

// The blob a host saves and loads is DAF's own stream format, and this harness is a host, so it
// builds the one row a host needs: a single host-readable state entry.
static std::string makeStateBlob(const std::string& key, const std::string& value)
{
    std::string blob;
    blob += "__daf_state_begin__"; blob += '\0';
    blob += key; blob += '\0';
    blob += value; blob += '\0';
    blob += "__daf_state_end__"; blob += '\0';
    blob += '\xfe';
    return blob;
}

// The value of `key` in a saved blob, or a marker when the key is absent. (Not "\x01absent": a hex
// escape swallows the 'a' that follows it.)
static const std::string kKeyAbsent = std::string(1, '\x01') + "absent" + std::string(1, '\x01');

static std::string stateValue(const std::string& blob, const std::string& key)
{
    const std::string needle = key + '\0';
    const size_t at = blob.find(needle);
    if (at == std::string::npos)
        return kKeyAbsent;
    const size_t valueAt = at + needle.size();
    const size_t end = blob.find('\0', valueAt);
    if (end == std::string::npos)
        return kKeyAbsent;
    return blob.substr(valueAt, end - valueAt);
}

// --------------------------------------------------------------------------------------------------------------------

// The audio buffers for one process() call, sized from the ports the plugin itself declares: a plugin
// that declares more than the harness hands it indexes past the end of audio_inputs. Every channel
// store is filled before any .data() is taken, because a later emplace_back can move the ones already
// handed out.
struct Audio {
    uint32_t frames = 0;
    uint32_t numInPorts = 0, numOutPorts = 0;
    std::vector<std::vector<float>> inStore, outStore;
    std::vector<std::vector<float*>> inPtrs, outPtrs;
    std::vector<clap_audio_buffer_t> inBuses, outBuses;

    bool setup(const clap_plugin_t* const plugin, const uint32_t frames_)
    {
        const clap_plugin_audio_ports_t* const ports =
            (const clap_plugin_audio_ports_t*) plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS);
        if (ports == nullptr)
            return false;

        frames = frames_;
        numInPorts = ports->count(plugin, true);
        numOutPorts = ports->count(plugin, false);
        if (numInPorts == 0 || numOutPorts == 0)
            return false;

        std::vector<uint32_t> inCh(numInPorts), outCh(numOutPorts);
        for (uint32_t i = 0; i < numInPorts; ++i)
        {
            clap_audio_port_info_t info = {};
            ports->get(plugin, i, true, &info);
            inCh[i] = info.channel_count;
        }
        for (uint32_t i = 0; i < numOutPorts; ++i)
        {
            clap_audio_port_info_t info = {};
            ports->get(plugin, i, false, &info);
            outCh[i] = info.channel_count;
        }

        for (uint32_t b = 0; b < numInPorts; ++b)
            for (uint32_t c = 0; c < inCh[b]; ++c)
                inStore.emplace_back(frames, 0.0f);
        for (uint32_t b = 0; b < numOutPorts; ++b)
            for (uint32_t c = 0; c < outCh[b]; ++c)
                outStore.emplace_back(frames, 0.0f);

        inPtrs.resize(numInPorts);
        outPtrs.resize(numOutPorts);
        inBuses.resize(numInPorts);
        outBuses.resize(numOutPorts);

        size_t next = 0;
        for (uint32_t b = 0; b < numInPorts; ++b)
        {
            for (uint32_t c = 0; c < inCh[b]; ++c)
                inPtrs[b].push_back(inStore[next++].data());
            inBuses[b].data32 = inPtrs[b].data();
            inBuses[b].channel_count = inCh[b];
        }
        next = 0;
        for (uint32_t b = 0; b < numOutPorts; ++b)
        {
            for (uint32_t c = 0; c < outCh[b]; ++c)
                outPtrs[b].push_back(outStore[next++].data());
            outBuses[b].data32 = outPtrs[b].data();
            outBuses[b].channel_count = outCh[b];
        }
        return true;
    }

    // not const: clap_process_t wants non-const buffer pointers
    void attach(clap_process_t& process)
    {
        process.frames_count = frames;
        process.audio_inputs = inBuses.data();
        process.audio_inputs_count = numInPorts;
        process.audio_outputs = outBuses.data();
        process.audio_outputs_count = numOutPorts;
    }
};

struct Param { clap_id id; std::string name; uint32_t flags; double def; };

struct Plugin {
    const clap_plugin_t* plugin = nullptr;
    const clap_plugin_params_t* params = nullptr;
    const clap_plugin_state_t* state = nullptr;
    std::vector<Param> list;

    bool create(const clap_plugin_factory_t* factory, const char* const id)
    {
        plugin = factory->create_plugin(factory, &gHost, id);
        if (plugin == nullptr || ! plugin->init(plugin))
            return false;
        params = (const clap_plugin_params_t*) plugin->get_extension(plugin, CLAP_EXT_PARAMS);
        state = (const clap_plugin_state_t*) plugin->get_extension(plugin, CLAP_EXT_STATE);
        if (params == nullptr || state == nullptr)
            return false;

        for (uint32_t i = 0; i < params->count(plugin); ++i)
        {
            clap_param_info_t info = {};
            if (! params->get_info(plugin, i, &info))
                return false;
            list.push_back({ info.id, info.name, info.flags, info.default_value });
        }
        return true;
    }

    void destroy()
    {
        if (plugin != nullptr)
            plugin->destroy(plugin);
        plugin = nullptr;
    }

    const Param* byName(const char* const name) const
    {
        for (const Param& p : list)
            if (p.name == name)
                return &p;
        return nullptr;
    }

    double getValue(clap_id id) const
    {
        double value = -1.0;
        params->get_value(plugin, id, &value);
        return value;
    }

    std::string save() const
    {
        OStream out;
        const clap_ostream_t stream = { &out, ostreamWrite };
        if (! state->save(plugin, &stream))
            return std::string();
        return out.data;
    }

    bool load(const std::string& blob) const
    {
        IStream in = { &blob, 0 };
        const clap_istream_t stream = { &in, istreamRead };
        return state->load(plugin, &stream);
    }
};

// --------------------------------------------------------------------------------------------------------------------

static int gFailures = 0;

static void check(const bool condition, const char* const what)
{
    if (condition)
    {
        std::printf("  ok   : %s\n", what);
        return;
    }
    std::fprintf(stderr, "  FAIL : %s\n", what);
    ++gFailures;
}

static std::string binaryInsideBundle(const std::string& path)
{
   #ifdef __APPLE__
    if (path.size() > 5 && path.compare(path.size() - 5, 5, ".clap") == 0)
    {
        std::string name(path);
        const size_t slash = name.find_last_of('/');
        if (slash != std::string::npos)
            name = name.substr(slash + 1);
        name = name.substr(0, name.size() - 5);
        const std::string inner = path + "/Contents/MacOS/" + name;
        if (FILE* const f = std::fopen(inner.c_str(), "rb")) { std::fclose(f); return inner; }
    }
   #endif
    return path;
}

int main(const int argc, const char* const* const argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: %s <ring-out.clap>\n", argv[0]);
        return 2;
    }

    const std::string path(binaryInsideBundle(argv[0 + 1]));

    void* const lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (lib == nullptr)
    {
        std::fprintf(stderr, "FAIL: dlopen(%s): %s\n", path.c_str(), dlerror());
        return 1;
    }

    const clap_plugin_entry_t* const entry = (const clap_plugin_entry_t*) dlsym(lib, "clap_entry");
    if (entry == nullptr || ! entry->init(path.c_str()))
    {
        std::fprintf(stderr, "FAIL: no usable clap_entry in %s\n", path.c_str());
        return 1;
    }

    const clap_plugin_factory_t* const factory =
        (const clap_plugin_factory_t*) entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    if (factory == nullptr || factory->get_plugin_count(factory) == 0)
    {
        std::fprintf(stderr, "FAIL: no plugin factory\n");
        return 1;
    }
    const clap_plugin_descriptor_t* const desc = factory->get_plugin_descriptor(factory, 0);
    if (desc == nullptr || desc->id == nullptr)
    {
        std::fprintf(stderr, "FAIL: factory returned no descriptor\n");
        return 1;
    }

    std::printf("plugin: %s\n", desc->id);

    // ---------------------------------------------------------------------------------------------------------------
    // A. a flush press must leave the host-visible state at the default, and a save/load round trip must reproduce
    //    exactly what the flush left behind. This is clap-validator's param-set-events and the three
    //    state-reproducibility tests in miniature; the DAF wrapper half is covered by DAF's own CLAPWrapper test.
    // ---------------------------------------------------------------------------------------------------------------
    std::printf("\nA. flush: presses read back at their defaults, and state reproduces\n");

    Plugin flushInstance;
    if (! flushInstance.create(factory, desc->id))
    {
        std::fprintf(stderr, "FAIL: could not create the plugin\n");
        return 1;
    }

    const Param* const setup = flushInstance.byName("Setup");
    const Param* const add = flushInstance.byName("Add");
    const Param* const reset = flushInstance.byName("Reset");
    const Param* const gainOut = flushInstance.byName("Gain Out");
    const Param* const globalQ = flushInstance.byName("Global Q");
    if (setup == nullptr || add == nullptr || reset == nullptr || gainOut == nullptr || globalQ == nullptr)
    {
        std::fprintf(stderr, "FAIL: the plugin is missing an expected parameter\n");
        return 1;
    }

    InEvents flushIn;
    OutEvents flushOut;
    flushIn.add(setup->id, 1.0);
    flushIn.add(add->id, 1.0);
    flushIn.add(reset->id, 1.0);
    // a control for the "the values did change" half of param-set-events: a batch of trigger presses
    // alone leaves every *readable* parameter where it was, which is the point, and is also why the
    // validator's own batch carries the fuzzed controls too.
    flushIn.add(gainOut->id, 12.0);
    flushIn.add(globalQ->id, 3.0);
    flushInstance.params->flush(flushInstance.plugin, &flushIn.iface, &flushOut.iface);

    check(std::fabs(flushInstance.getValue(gainOut->id) - 12.0) < 1.0e-4, "a flushed control reads back its new value");
    check(std::fabs(flushInstance.getValue(globalQ->id) - 3.0) < 1.0e-4, "a flushed control reads back its new value");
    check(std::fabs(flushInstance.getValue(setup->id) - setup->def) < 1.0e-6,
          "Setup reads its default after flush, not the press");
    check(std::fabs(flushInstance.getValue(add->id) - add->def) < 1.0e-6,
          "Add reads its default after flush, not the press");
    check(std::fabs(flushInstance.getValue(reset->id) - reset->def) < 1.0e-6,
          "Reset reads its default after flush, not the press");

    std::vector<double> flushedValues;
    for (const Param& p : flushInstance.list)
        flushedValues.push_back(flushInstance.getValue(p.id));

    const std::string savedFlush = flushInstance.save();
    check(! savedFlush.empty(), "the state saves");
    // A saved state is a stream of NUL-terminated entries, so a parameter's entry is "<symbol>\0" and
    // nothing shorter: a bare substring search could match part of another key, and a C string literal
    // like "\0add\0" reaches std::string::find as an empty string, which is always found.
    const auto hasEntry = [](const std::string& blob, const char* const key) {
        const std::string needle = std::string(key) + '\0';
        for (size_t at = blob.find(needle); at != std::string::npos; at = blob.find(needle, at + 1))
            if (at == 0 || blob[at - 1] == '\0')
                return true;
        return false;
    };
    check(! hasEntry(savedFlush, "setup") && ! hasEntry(savedFlush, "add") && ! hasEntry(savedFlush, "reset"),
          "no trigger is written into the saved state");

    {
        Plugin reloaded;
        if (! reloaded.create(factory, desc->id))
        {
            std::fprintf(stderr, "FAIL: could not create the second plugin\n");
            return 1;
        }
        check(reloaded.load(savedFlush), "the state loads into a second instance");

        bool valuesMatch = reloaded.list.size() == flushInstance.list.size();
        if (valuesMatch)
            for (size_t i = 0; i < reloaded.list.size(); ++i)
            {
                const double a = flushedValues[i];
                const double b = reloaded.getValue(reloaded.list[i].id);
                if (std::fabs(a - b) > 1.0e-6)
                {
                    std::fprintf(stderr, "       \"%s\": %f after the flush, %f after reloading\n",
                                 reloaded.list[i].name.c_str(), a, b);
                    valuesMatch = false;
                }
            }
        check(valuesMatch, "every parameter survives save/reload (this is the state-reproducibility failure)");

        const std::string resaved = reloaded.save();
        check(resaved == savedFlush, "reloading and saving again is byte-for-byte identical");
        reloaded.destroy();
    }

    // ---------------------------------------------------------------------------------------------------------------
    // B. the process path must leave the same values behind as the flush path. This is param-set-events.
    // ---------------------------------------------------------------------------------------------------------------
    std::printf("\nB. process: the same presses leave the same values\n");

    {
        Plugin processInstance;
        if (! processInstance.create(factory, desc->id))
        {
            std::fprintf(stderr, "FAIL: could not create the plugin\n");
            return 1;
        }

        Audio audio;
        if (! audio.setup(processInstance.plugin, 256))
        {
            std::fprintf(stderr, "FAIL: could not build the audio buffers\n");
            return 1;
        }
        const uint32_t frames = audio.frames;

        check(processInstance.plugin->activate(processInstance.plugin, 48000.0, 1, frames), "activate");
        check(processInstance.plugin->start_processing(processInstance.plugin), "start_processing");

        InEvents in;
        OutEvents out;
        in.add(setup->id, 1.0);
        in.add(add->id, 1.0);
        in.add(reset->id, 1.0);
        in.add(gainOut->id, 12.0);
        in.add(globalQ->id, 3.0);

        clap_process_t process = {};
        audio.attach(process);
        process.in_events = &in.iface;
        process.out_events = &out.iface;
        process.steady_time = 0;
        check(processInstance.plugin->process(processInstance.plugin, &process) != CLAP_PROCESS_ERROR,
              "process the batch of presses");

        bool parity = processInstance.list.size() == flushedValues.size();
        if (parity)
            for (size_t i = 0; i < processInstance.list.size(); ++i)
            {
                const double a = flushedValues[i];
                const double b = processInstance.getValue(processInstance.list[i].id);
                if (std::fabs(a - b) > 1.0e-6)
                {
                    std::fprintf(stderr, "       \"%s\": %f after flush, %f after process\n",
                                 processInstance.list[i].name.c_str(), a, b);
                    parity = false;
                }
            }
        check(parity, "flush and process leave identical parameter values (param-set-events)");

        // the run completed the presses, so the host is told the parameters are back at their defaults
        check(out.reportedValue(setup->id, setup->def), "the Setup reset reaches the host's output list");
        check(out.reportedValue(add->id, add->def), "the Add reset reaches the host's output list");
        check(out.reportedValue(reset->id, reset->def), "the Reset reset reaches the host's output list");

        processInstance.plugin->stop_processing(processInstance.plugin);
        processInstance.plugin->deactivate(processInstance.plugin);
        processInstance.destroy();
    }

    // ---------------------------------------------------------------------------------------------------------------
    // C. the press must still reach the plugin. A RESET delivered through flush() has to clear a filter table
    //    that came from a loaded state, or the passing assertions above would just mean the action was dropped.
    // ---------------------------------------------------------------------------------------------------------------
    std::printf("\nC. the press still reaches the plugin\n");

    const char* const kTableText = "1,1000,-6,4;1,250,-3,5";

    {
        Plugin p;
        if (! p.create(factory, desc->id))
        {
            std::fprintf(stderr, "FAIL: could not create the plugin\n");
            return 1;
        }

        check(p.load(makeStateBlob("filters", kTableText)), "a state with two filters loads");
        check(stateValue(p.save(), "filters") == kTableText, "the filters come back out of the saved state");

        InEvents in;
        OutEvents out;
        in.add(reset->id, 1.0);
        p.params->flush(p.plugin, &in.iface, &out.iface);

        check(std::fabs(p.getValue(reset->id) - reset->def) < 1.0e-6, "Reset still reads its default after flush");
        check(stateValue(p.save(), "filters").empty(), "the flush press still cleared the table (the action was delivered)");
        p.destroy();
    }

    {
        Plugin p;
        if (! p.create(factory, desc->id))
        {
            std::fprintf(stderr, "FAIL: could not create the plugin\n");
            return 1;
        }

        check(p.load(makeStateBlob("filters", kTableText)), "a state with two filters loads");
        check(stateValue(p.save(), "filters") == kTableText, "the filters come back out of the saved state");

        Audio audio;
        if (! audio.setup(p.plugin, 256))
        {
            std::fprintf(stderr, "FAIL: could not build the audio buffers\n");
            return 1;
        }

        check(p.plugin->activate(p.plugin, 48000.0, 1, audio.frames), "activate");
        check(p.plugin->start_processing(p.plugin), "start_processing");

        InEvents in;
        OutEvents out;
        in.add(reset->id, 1.0);

        clap_process_t process = {};
        audio.attach(process);
        process.in_events = &in.iface;
        process.out_events = &out.iface;
        check(p.plugin->process(p.plugin, &process) != CLAP_PROCESS_ERROR, "process a RESET press");

        check(std::fabs(p.getValue(reset->id) - reset->def) < 1.0e-6, "Reset still reads its default after process");
        check(stateValue(p.save(), "filters").empty(), "the process press cleared the table");

        p.plugin->stop_processing(p.plugin);
        p.plugin->deactivate(p.plugin);
        p.destroy();
    }

    // ---------------------------------------------------------------------------------------------------------------
    // D. repeated presses, a release, and a missing output list. Each has to leave the host-visible state at the
    //    default, and a reset made while the host passed no output list has to be reported by the next call that
    //    has one: dropping it there would strand the host on a value the plugin no longer holds.
    // ---------------------------------------------------------------------------------------------------------------
    std::printf("\nD. repeated presses, releases, and a missing output list\n");

    {
        Plugin p;
        if (! p.create(factory, desc->id))
        {
            std::fprintf(stderr, "FAIL: could not create the plugin\n");
            return 1;
        }

        InEvents in;
        OutEvents out;
        in.add(setup->id, 1.0);
        p.params->flush(p.plugin, &in.iface, nullptr);   // no output list at all
        check(std::fabs(p.getValue(setup->id) - setup->def) < 1.0e-6,
              "Setup reads its default after a flush with no output list");

        in.clear();
        in.add(setup->id, 0.0);
        p.params->flush(p.plugin, &in.iface, &out.iface);
        check(std::fabs(p.getValue(setup->id) - setup->def) < 1.0e-6, "a release leaves the default");

        in.clear();
        in.add(setup->id, 1.0);
        in.add(setup->id, 1.0);
        p.params->flush(p.plugin, &in.iface, &out.iface);
        check(std::fabs(p.getValue(setup->id) - setup->def) < 1.0e-6, "two presses in one flush leave the default");

        // The run completes those presses, but this host passes no output list, so the change has to be
        // held back rather than lost -- the next caller with a real list is the one that is told.
        Audio audio;
        if (! audio.setup(p.plugin, 256))
        {
            std::fprintf(stderr, "FAIL: could not build the audio buffers\n");
            return 1;
        }
        check(p.plugin->activate(p.plugin, 48000.0, 1, audio.frames), "activate");
        check(p.plugin->start_processing(p.plugin), "start_processing");
        {
            InEvents processIn;
            clap_process_t process = {};
            audio.attach(process);
            process.in_events = &processIn.iface;
            process.out_events = nullptr;   // the spec allows it, and the validator does it on purpose
            check(p.plugin->process(p.plugin, &process) != CLAP_PROCESS_ERROR, "process with no output list");
        }
        check(std::fabs(p.getValue(setup->id) - setup->def) < 1.0e-6,
              "Setup reads its default after a run with no output list");
        check(out.countFor(setup->id) == 0, "a run with no output list reports nothing");

        InEvents none;
        p.params->flush(p.plugin, &none.iface, &out.iface);
        check(out.reportedValue(setup->id, setup->def),
              "the reset held back for a missing output list is reported by the next call that has one");

        p.plugin->stop_processing(p.plugin);
        p.plugin->deactivate(p.plugin);
        p.destroy();
    }

    entry->deinit();

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "RESULT: PASS" : "RESULT: FAIL",
                gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
