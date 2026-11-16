// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Pins the parameter list the built CLAP and VST3 report to a host, flag for
// flag, to an expected table that started as what the released Tape Echo 2
// 1.0.8 reported. AU is not covered: its wrapper never read the hidden hint.
//
// Why: the legacy Dry Level and Sync Division parameters must stay visible and
// automatable in host parameter lists, or automation saved by the 0.1 series
// stops being shown, edited or played back. They used to carry
// kParameterIsHidden, which DAF passed to LV2 only; since DAF 4556beb6 it also
// sets CLAP_PARAM_IS_HIDDEN and V3_PARAM_IS_HIDDEN, and a build from main then
// reported both as hidden where 1.0.8 did not. Nothing in the plugin's own code
// changed, so only a test of the built binary sees it. This one asserts the
// complete list rather than the two parameters, so that any later framework or
// plugin change to any parameter's host flags fails here and has to be decided
// on, instead of reaching a release unnoticed.
//
// The expected tables are a measurement, not a derivation: every row was
// dumped from the 1.0.8 release binaries (tag tape-echo-2-v1.0.8, built with
// DAF b5c242e2), and this test passes when pointed at that release's bundles.
// The one derived value is the VST3 program parameter's step count (see
// there). Change a row only for a deliberate, reviewed change to what hosts
// see. The tables must cover every parameter (static_asserts below), so a
// parameter appended later needs a row 1.0.8 never had: give it the flags it
// is meant to have and mark it "// added in <version>, not from 1.0.8" so the
// measured rows stay distinguishable from the asserted ones.
//
//   TapeEchoHostParamFlagsTest --clap <tape-echo-2.clap or its binary>
//   TapeEchoHostParamFlagsTest --vst3 <tape-echo-2.vst3 or its binary>
//
// Linux and macOS only (dlopen).

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include <dlfcn.h>

#include "clap/entry.h"
#include "clap/plugin.h"
#include "clap/ext/params.h"
#include "clap/factory/plugin-factory.h"

#include "travesty/base.h"
#include "travesty/component.h"
#include "travesty/edit_controller.h"
#include "travesty/factory.h"
#include "travesty/host.h"

#include "TapeEchoParams.hpp"

namespace
{
// ---------------------------------------------------------------------------------------------------------------------
// Expected host flags (as measured on 1.0.8, except where a row says otherwise)

constexpr uint32_t kClapAuto    = CLAP_PARAM_IS_AUTOMATABLE;
constexpr uint32_t kClapStepped = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_AUTOMATABLE;

struct ClapRow { clap_id id; const char* module; uint32_t flags; };

// clap_param_info.module is the parameter symbol (DAF puts it there), which
// keeps a reordering from passing on matching flags alone.
constexpr ClapRow kClapExpected[] =
{
    {  0, "mode",           kClapStepped },
    {  1, "repeat_rate",    kClapAuto },
    {  2, "intensity",      kClapAuto },
    {  3, "echo_volume",    kClapAuto },
    {  4, "reverb_volume",  kClapAuto },
    {  5, "bass",           kClapAuto },
    {  6, "treble",         kClapAuto },
    {  7, "input_volume",   kClapAuto },
    {  8, "wow_flutter",    kClapAuto },
    {  9, "dry_level",      kClapAuto },     // legacy: automatable, NOT hidden
    { 10, "tempo_sync",     kClapStepped },
    { 11, "sync_division",  kClapStepped },  // legacy: automatable, NOT hidden
    { 12, "tape_age",       kClapAuto },
    { 13, "daf_bypass",     CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_BYPASS | CLAP_PARAM_IS_AUTOMATABLE },
    { 14, "out_level",      CLAP_PARAM_IS_READONLY },
    { 15, "output_volume",  kClapAuto },
    { 16, "echo_pan",       kClapAuto },
    { 17, "reverb_pan",     kClapAuto },
    { 18, "input_send",     kClapStepped },
    { 19, "peak_level",     CLAP_PARAM_IS_READONLY },
    { 20, "mix",            kClapAuto },
    { 21, "echo_rate_note", kClapStepped },
    { 22, "noise",          kClapAuto },
    { 23, "hum_frequency",  kClapStepped },
};
static_assert(sizeof(kClapExpected) / sizeof(kClapExpected[0]) == kParamCount,
              "the CLAP table must cover every parameter");

constexpr int32_t kV3Auto = V3_PARAM_CAN_AUTOMATE;
constexpr int32_t kV3List = V3_PARAM_CAN_AUTOMATE | V3_PARAM_IS_LIST;

// VST3 has no "stepped" flag: step_count carries it (0 = continuous), so it is
// asserted alongside the flags, and the title stands in for the CLAP module.
// VST3 id 0 is DAF's program-change parameter and plugin parameter N is id
// N + 1. The program parameter has one step per factory program after the
// first (12 in 1.0.8, which had 13), so its count is derived from the preset
// table rather than failing this test whenever a program is added.
struct Vst3Row { v3_param_id id; const char* title; int32_t flags; int32_t steps; };

constexpr Vst3Row kVst3Expected[] =
{
    {  0, "Current Program", V3_PARAM_CAN_AUTOMATE | V3_PARAM_IS_LIST | V3_PARAM_IS_HIDDEN | V3_PARAM_PROGRAM_CHANGE,
                             kNumFactoryPresets - 1 },
    {  1, "Mode",            kV3Auto, 11 },
    {  2, "Repeat Rate",     kV3Auto, 0 },
    {  3, "Intensity",       kV3Auto, 0 },
    {  4, "Echo Volume",     kV3Auto, 0 },
    {  5, "Reverb Volume",   kV3Auto, 0 },
    {  6, "Bass",            kV3Auto, 0 },
    {  7, "Treble",          kV3Auto, 0 },
    {  8, "Input Volume",    kV3Auto, 0 },
    {  9, "Wow & Flutter",   kV3Auto, 0 },
    { 10, "Dry Level",       kV3Auto, 0 },   // legacy: automatable, NOT hidden
    { 11, "Tempo Sync",      kV3Auto, 1 },
    { 12, "Sync Division",   kV3Auto, 15 },  // legacy: automatable, NOT hidden
    { 13, "Tape Age",        kV3List, 2 },
    { 14, "Bypass",          V3_PARAM_CAN_AUTOMATE | V3_PARAM_IS_BYPASS, 1 },
    { 15, "Record VU",       V3_PARAM_READ_ONLY, 0 },
    { 16, "Output Volume",   kV3Auto, 0 },
    { 17, "Echo Pan",        kV3Auto, 0 },
    { 18, "Reverb Pan",      kV3Auto, 0 },
    { 19, "Input Send",      kV3Auto, 1 },
    { 20, "Record Peak",     V3_PARAM_READ_ONLY, 0 },
    { 21, "Mix",             kV3Auto, 0 },
    { 22, "Echo Rate Note",  kV3Auto, 10 },
    { 23, "Noise",           kV3Auto, 0 },
    { 24, "Hum Frequency",   kV3List, 1 },
};
static_assert(sizeof(kVst3Expected) / sizeof(kVst3Expected[0]) == kParamCount + 1,
              "the VST3 table must cover every parameter plus the program parameter");

// ---------------------------------------------------------------------------------------------------------------------

std::string clapFlagNames(uint32_t f)
{
    static const struct { uint32_t bit; const char* name; } kNames[] = {
        { CLAP_PARAM_IS_STEPPED, "STEPPED" }, { CLAP_PARAM_IS_PERIODIC, "PERIODIC" },
        { CLAP_PARAM_IS_HIDDEN, "HIDDEN" }, { CLAP_PARAM_IS_READONLY, "READONLY" },
        { CLAP_PARAM_IS_BYPASS, "BYPASS" }, { CLAP_PARAM_IS_AUTOMATABLE, "AUTOMATABLE" },
        { CLAP_PARAM_IS_MODULATABLE, "MODULATABLE" }, { CLAP_PARAM_REQUIRES_PROCESS, "REQUIRES_PROCESS" },
        { CLAP_PARAM_IS_ENUM, "ENUM" },
    };
    std::string s;
    uint32_t named = 0;
    for (const auto& n : kNames)
    {
        named |= n.bit;
        if (f & n.bit) s += (s.empty() ? "" : "|") + std::string(n.name);
    }
    if (f & ~named)
    {
        char other[24];
        std::snprintf(other, sizeof(other), "0x%x", f & ~named);
        s += (s.empty() ? "" : "|") + std::string(other);
    }
    return s.empty() ? "none" : s;
}

std::string vst3FlagNames(int32_t f)
{
    static const struct { int32_t bit; const char* name; } kNames[] = {
        { V3_PARAM_CAN_AUTOMATE, "CAN_AUTOMATE" }, { V3_PARAM_READ_ONLY, "READ_ONLY" },
        { V3_PARAM_WRAP_AROUND, "WRAP_AROUND" }, { V3_PARAM_IS_LIST, "IS_LIST" },
        { V3_PARAM_IS_HIDDEN, "IS_HIDDEN" }, { V3_PARAM_PROGRAM_CHANGE, "PROGRAM_CHANGE" },
        { V3_PARAM_IS_BYPASS, "IS_BYPASS" },
    };
    std::string s;
    int32_t named = 0;
    for (const auto& n : kNames)
    {
        named |= n.bit;
        if (f & n.bit) s += (s.empty() ? "" : "|") + std::string(n.name);
    }
    if (f & ~named)
    {
        char other[24];
        std::snprintf(other, sizeof(other), "0x%x", (unsigned)(f & ~named));
        s += (s.empty() ? "" : "|") + std::string(other);
    }
    return s.empty() ? "none" : s;
}

int gFailures = 0;

#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
void fail(const char* const format, ...)
{
    ++gFailures;
    // The table goes to stdout, which is block-buffered under ctest; flush it
    // first so each FAIL line lands below the row it refers to.
    std::fflush(stdout);
    std::fputs("FAIL: ", stderr);
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fputc('\n', stderr);
}

// CMake passes the loadable binary itself. A bundle directory is accepted too,
// for running this by hand against a release download.
std::string binaryInsideBundle(const std::string& path, const char* const ext)
{
    const size_t n = std::strlen(ext);
    if (path.size() <= n || path.compare(path.size() - n, n, ext) != 0)
        return path;
    std::string name(path);
    const size_t slash = name.find_last_of('/');
    if (slash != std::string::npos)
        name = name.substr(slash + 1);
    name = name.substr(0, name.size() - n);
   #ifdef __APPLE__
    const std::string inner = path + "/Contents/MacOS/" + name;
   #elif defined(__aarch64__)
    const std::string inner = path + "/Contents/aarch64-linux/" + name + ".so";
   #else
    const std::string inner = path + "/Contents/x86_64-linux/" + name + ".so";
   #endif
    // A Linux .clap is the binary itself, so the inner path does not exist.
    if (FILE* const f = std::fopen(inner.c_str(), "rb"))
    {
        std::fclose(f);
        return inner;
    }
    return path;
}

// ---------------------------------------------------------------------------------------------------------------------
// CLAP

const void* CLAP_ABI hostGetExtension(const clap_host_t*, const char*) { return nullptr; }
void CLAP_ABI hostNoop(const clap_host_t*) {}

int checkClap(const std::string& arg)
{
    const std::string path = binaryInsideBundle(arg, ".clap");
    void* const lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (lib == nullptr)
    {
        std::fprintf(stderr, "FAIL: dlopen(%s): %s\n", path.c_str(), dlerror());
        return 1;
    }
    const auto* const entry = (const clap_plugin_entry_t*) dlsym(lib, "clap_entry");
    if (entry == nullptr || ! entry->init(path.c_str()))
    {
        std::fprintf(stderr, "FAIL: no usable clap_entry in %s\n", path.c_str());
        return 1;
    }
    const auto* const factory = (const clap_plugin_factory_t*) entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    const clap_plugin_descriptor_t* const desc =
        factory != nullptr && factory->get_plugin_count(factory) == 1 ? factory->get_plugin_descriptor(factory, 0)
                                                                       : nullptr;
    if (desc == nullptr)
    {
        std::fprintf(stderr, "FAIL: expected a factory with exactly one plugin\n");
        return 1;
    }

    static const clap_host_t host = {
        CLAP_VERSION_INIT, nullptr, "TapeEchoHostParamFlagsTest", "Dusk Audio", "https://dusk-audio.github.io/", "1.0.0",
        hostGetExtension, hostNoop, hostNoop, hostNoop
    };
    const clap_plugin_t* const plugin = factory->create_plugin(factory, &host, desc->id);
    if (plugin == nullptr || ! plugin->init(plugin))
    {
        std::fprintf(stderr, "FAIL: could not create the plugin\n");
        return 1;
    }
    const auto* const params = (const clap_plugin_params_t*) plugin->get_extension(plugin, CLAP_EXT_PARAMS);
    if (params == nullptr)
    {
        std::fprintf(stderr, "FAIL: no params extension\n");
        return 1;
    }

    const uint32_t count = params->count(plugin);
    constexpr uint32_t kExpected = sizeof(kClapExpected) / sizeof(kClapExpected[0]);
    std::printf("CLAP %s: %u parameters (expected %u)\n", path.c_str(), count, kExpected);
    if (count != kExpected)
        fail("%u parameters, expected %u", count, kExpected);

    for (uint32_t i = 0; i < count && i < kExpected; ++i)
    {
        clap_param_info_t info = {};
        if (! params->get_info(plugin, i, &info))
        {
            fail("get_info(%u) failed", i);
            continue;
        }
        const ClapRow& want = kClapExpected[i];
        std::printf("  index %2u  id=%-2u %-15s %-28s%s\n", i, info.id, info.module,
                    clapFlagNames(info.flags).c_str(), info.flags == want.flags ? "" : " <- differs");
        if (info.id != want.id || std::strcmp(info.module, want.module) != 0)
            fail("index %u is id %u \"%s\", expected id %u \"%s\"",
                 i, info.id, info.module, want.id, want.module);
        if (info.flags != want.flags)
            fail("%s reports flags 0x%x (%s), expected 0x%x (%s)", want.module,
                 info.flags, clapFlagNames(info.flags).c_str(), want.flags, clapFlagNames(want.flags).c_str());
    }

    plugin->destroy(plugin);
    entry->deinit();
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// VST3

uint32_t V3_API refStub(void*) { return 1; }

// The wrapper is initialised with a host context; it never asks this one to
// create anything, because nothing here opens a UI or exchanges messages.
struct HostApplication : v3_host_application_cpp {
    HostApplication* selfptr = this;
    v3_funknown** handle() { return (v3_funknown**) &selfptr; }

    HostApplication()
    {
        query_interface = queryInterface;
        ref = refStub;
        unref = refStub;
        app.get_name = getName;
        app.create_instance = createInstance;
    }
    HostApplication(const HostApplication&) = delete;
    HostApplication& operator=(const HostApplication&) = delete;

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
        static const char* const kName = "TapeEchoHostParamFlagsTest";
        int i = 0;
        for (; kName[i] != '\0'; ++i)
            name[i] = static_cast<int16_t>(kName[i]);
        name[i] = 0;
        return V3_OK;
    }
    static v3_result V3_API createInstance(void*, v3_tuid, v3_tuid, void** const obj)
    {
        *obj = nullptr;
        return V3_NOT_IMPLEMENTED;
    }
};

std::string utf16ToAscii(const v3_str_128 s)
{
    std::string out;
    for (int i = 0; i < 128 && s[i] != 0; ++i)
        out += static_cast<char>(s[i] & 0x7f);
    return out;
}

int checkVst3(const std::string& arg)
{
    const std::string path = binaryInsideBundle(arg, ".vst3");
    void* const lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (lib == nullptr)
    {
        std::fprintf(stderr, "FAIL: dlopen(%s): %s\n", path.c_str(), dlerror());
        return 1;
    }
   #ifdef __APPLE__
    auto* const moduleEntry = (bool (*)(void*)) dlsym(lib, "bundleEntry");
    auto* const moduleExit = (bool (*)()) dlsym(lib, "bundleExit");
   #else
    auto* const moduleEntry = (bool (*)(void*)) dlsym(lib, "ModuleEntry");
    auto* const moduleExit = (bool (*)()) dlsym(lib, "ModuleExit");
   #endif
    auto* const getFactory = (const void* (*)()) dlsym(lib, "GetPluginFactory");
    if (getFactory == nullptr || (moduleEntry != nullptr && ! moduleEntry(lib)))
    {
        std::fprintf(stderr, "FAIL: no usable VST3 module entry in %s\n", path.c_str());
        return 1;
    }
    auto** const factory = (v3_plugin_factory**) getFactory();
    if (factory == nullptr)
    {
        std::fprintf(stderr, "FAIL: GetPluginFactory returned null\n");
        return 1;
    }

    HostApplication host;
    v3_plugin_factory_3** factory3 = nullptr;
    if (v3_cpp_obj_query_interface(factory, v3_plugin_factory_3_iid, &factory3) == V3_OK && factory3 != nullptr)
        v3_cpp_obj(factory3)->set_host_context(factory3, host.handle());

    v3_tuid cid = {};
    bool haveCid = false;
    for (int32_t i = 0, n = v3_cpp_obj(factory)->num_classes(factory); i < n && ! haveCid; ++i)
    {
        v3_class_info info = {};
        if (v3_cpp_obj(factory)->get_class_info(factory, i, &info) == V3_OK
            && std::strcmp(info.category, "Audio Module Class") == 0)
        {
            std::memcpy(cid, info.class_id, sizeof(v3_tuid));
            haveCid = true;
        }
    }
    v3_component** component = nullptr;
    if (! haveCid
        || v3_cpp_obj(factory)->create_instance(factory, cid, v3_component_iid, (void**) &component) != V3_OK
        || component == nullptr
        || v3_cpp_obj_initialize(component, host.handle()) != V3_OK)
    {
        std::fprintf(stderr, "FAIL: could not create and initialise the component\n");
        return 1;
    }
    // Every DAF plugin in this repository is a single-component build, whose
    // component answers for the edit controller as well.
    v3_edit_controller** controller = nullptr;
    if (v3_cpp_obj_query_interface(component, v3_edit_controller_iid, &controller) != V3_OK || controller == nullptr)
    {
        std::fprintf(stderr, "FAIL: the component does not implement v3_edit_controller\n");
        return 1;
    }

    const int32_t count = v3_cpp_obj(controller)->get_parameter_count(controller);
    constexpr int32_t kExpected = static_cast<int32_t>(sizeof(kVst3Expected) / sizeof(kVst3Expected[0]));
    std::printf("VST3 %s: %d parameters (expected %d)\n", path.c_str(), count, kExpected);
    if (count != kExpected)
        fail("%d parameters, expected %d", count, kExpected);

    for (int32_t i = 0; i < count && i < kExpected; ++i)
    {
        v3_param_info info = {};
        if (v3_cpp_obj(controller)->get_parameter_info(controller, i, &info) != V3_OK)
        {
            fail("get_parameter_info(%d) failed", i);
            continue;
        }
        const Vst3Row& want = kVst3Expected[i];
        const std::string title = utf16ToAscii(info.title);
        const bool same = info.flags == want.flags && info.step_count == want.steps;
        std::printf("  index %2d  id=%-2u %-16s %-48s steps=%-2d%s\n", i, info.param_id, title.c_str(),
                    vst3FlagNames(info.flags).c_str(), info.step_count, same ? "" : " <- differs");
        if (info.param_id != want.id || title != want.title)
            fail("index %d is id %u \"%s\", expected id %u \"%s\"",
                 i, info.param_id, title.c_str(), want.id, want.title);
        if (info.flags != want.flags)
            fail("\"%s\" reports flags 0x%x (%s), expected 0x%x (%s)", want.title,
                 (unsigned) info.flags, vst3FlagNames(info.flags).c_str(),
                 (unsigned) want.flags, vst3FlagNames(want.flags).c_str());
        if (info.step_count != want.steps)
            fail("\"%s\" reports step_count %d, expected %d", want.title, info.step_count, want.steps);
    }

    // Down in the reverse order: DAF parks a component whose controller is
    // still referenced instead of destroying it. GetPluginFactory() hands over
    // a factory holding one reference, which is ours to release.
    v3_cpp_obj_unref(controller);
    v3_cpp_obj_terminate(component);
    v3_cpp_obj_unref(component);
    if (factory3 != nullptr)
        v3_cpp_obj_unref(factory3);
    v3_cpp_obj_unref(factory);
    if (moduleExit != nullptr)
        moduleExit();
    return 0;
}
} // namespace

int main(const int argc, const char* const* const argv)
{
    if (argc != 3 || (std::strcmp(argv[1], "--clap") != 0 && std::strcmp(argv[1], "--vst3") != 0))
    {
        std::fprintf(stderr, "usage: %s --clap|--vst3 <plugin bundle or binary>\n", argv[0]);
        return 2;
    }
    const bool clap = std::strcmp(argv[1], "--clap") == 0;
    if ((clap ? checkClap(argv[2]) : checkVst3(argv[2])) != 0)
        return 1;
    std::printf("%s: %s\n", clap ? "CLAP" : "VST3",
                gFailures == 0 ? "PASS, every parameter reports its expected host flags"
                               : "FAIL, host flags differ from the expected table (see above)");
    return gFailures == 0 ? 0 : 1;
}
