// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// DuskVerb 2's VST3 identity, read from the built binary as a host reads it: the class ID
// string every platform has had since 1.0.1, and the ID the Windows build of 1.0.0 had
// declared an older ID of the same plugin, so projects saved with any version find it.
#include "travesty/bstream.h"
#include "travesty/component.h"
#include "travesty/factory.h"
#include "travesty/plugin_compatibility.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#ifdef _WIN32
# include <windows.h>
#else
# include <dlfcn.h>
#endif

namespace {
// VST3's canonical GUID text uses native COM fields on Windows and byte order
// elsewhere. Compare the host-visible identity, rather than identical raw bytes.
std::string canonicalId(const v3_tuid id)
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(id);
    char text[33]{};
    int start = 0;
#if V3_COM_COMPAT
    uint32_t data1;
    uint16_t data2, data3;
    std::memcpy(&data1, bytes, 4);
    std::memcpy(&data2, bytes + 4, 2);
    std::memcpy(&data3, bytes + 6, 2);
    std::snprintf(text, sizeof(text), "%08X%04X%04X", unsigned(data1), unsigned(data2), unsigned(data3));
    start = 8;
#endif
    for (int i = start; i < 16; ++i)
        std::snprintf(text + 2 * i, sizeof(text) - 2 * i, "%02X", unsigned(bytes[i]));
    return text;
}

uint32_t V3_API staticRef(void*) { return 1; }

// collects what the compatibility class writes
struct TextStream : v3_bstream_cpp
{
    TextStream* self = this;
    std::string text;

    TextStream()
    {
        query_interface = [](void*, const v3_tuid, void** out) -> v3_result { *out = nullptr; return V3_NO_INTERFACE; };
        ref = unref = staticRef;
        stream.read = [](void*, void*, int32_t, int32_t*) -> v3_result { return V3_NOT_IMPLEMENTED; };
        stream.write = [](void* handle, void* data, int32_t size, int32_t* written) -> v3_result
        {
            (*static_cast<TextStream**>(handle))->text.append(static_cast<const char*>(data), size_t(size));
            if (written != nullptr) *written = size;
            return V3_OK;
        };
        stream.seek = [](void*, int64_t, int32_t, int64_t*) -> v3_result { return V3_NOT_IMPLEMENTED; };
        stream.tell = [](void*, int64_t*) -> v3_result { return V3_NOT_IMPLEMENTED; };
    }

    v3_bstream** handle() { return reinterpret_cast<v3_bstream**>(&self); }
};
}

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    // The class ID string hosts show, the same on every platform. It must never change.
    // The old ID is what the Windows build of 1.0.0 showed (the same words in DAF's default
    // layout); the plugin declares it an older ID of itself.
    const char* expectedId = "2046504473616C63764473446B737544";
    const char* expectedOldId = "445046206173636C764473446B737544";
    const char* expectedName = "DuskVerb 2";
#ifdef _WIN32
    const auto library = LoadLibraryA(argv[1]);
    const auto symbol = [library](const char* name) { return GetProcAddress(library, name); };
    const auto entry = reinterpret_cast<bool(*)()>(symbol("InitDll"));
    const auto leave = reinterpret_cast<bool(*)()>(symbol("ExitDll"));
#else
    void* const library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    const auto symbol = [library](const char* name) { return library != nullptr ? dlsym(library, name) : nullptr; };
# ifdef __APPLE__
    const auto entry = reinterpret_cast<bool(*)(void*)>(symbol("bundleEntry"));
    const auto leave = reinterpret_cast<bool(*)()>(symbol("bundleExit"));
# else
    const auto entry = reinterpret_cast<bool(*)(void*)>(symbol("ModuleEntry"));
    const auto leave = reinterpret_cast<bool(*)()>(symbol("ModuleExit"));
# endif
#endif
    const auto getFactory = reinterpret_cast<const void*(*)()>(symbol("GetPluginFactory"));
    if (library == nullptr || entry == nullptr || leave == nullptr || getFactory == nullptr)
    {
        std::fprintf(stderr, "FAIL: native VST3 library or module exports unavailable\n");
        return 1;
    }
#ifdef _WIN32
    const bool entered = entry();
#else
    const bool entered = entry(library);
#endif
    if (!entered)
        return 1;

    int failures = 0;
    int checks = 0;
    const auto check = [&](bool ok, const char* message) {
        ++checks;
        if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
    };
    auto** factory = static_cast<v3_plugin_factory**>(const_cast<void*>(getFactory()));
    check(factory != nullptr, "factory exists");
    if (factory != nullptr)
    {
        const int count = v3_cpp_obj(factory)->num_classes(factory);
        check(count == 2, "one monolithic audio class and its compatibility class");
        int audioClasses = 0, compatibilityClasses = 0;
        for (int i = 0; i < count; ++i)
        {
            v3_class_info info{};
            const bool haveInfo = v3_cpp_obj(factory)->get_class_info(factory, i, &info) == V3_OK;
            check(haveInfo, "factory class info");
            if (!haveInfo) continue;
            check(std::strcmp(info.name, expectedName) == 0, "product name");

            if (std::strcmp(info.category, V3_PLUGIN_COMPATIBILITY_CLASS) == 0)
            {
                ++compatibilityClasses;
                v3_plugin_compatibility** compatibility = nullptr;
                check(v3_cpp_obj(factory)->create_instance(factory, info.class_id, v3_plugin_compatibility_iid,
                                                           reinterpret_cast<void**>(&compatibility)) == V3_OK
                    && compatibility != nullptr, "compatibility class instantiates");
                if (compatibility != nullptr)
                {
                    TextStream json;
                    check(v3_cpp_obj(compatibility)->get_compatibility_json(compatibility, json.handle()) == V3_OK,
                          "compatibility JSON written");
                    const std::string expectedJson = std::string("[{\"New\":\"") + expectedId + "\",\"Old\":[\""
                                                   + expectedOldId + "\"]}]";
                    std::printf("compatibility: %s\n", json.text.c_str());
                    check(json.text == expectedJson, "the 1.0.0 Windows class ID is declared an old ID of the plugin");
                    v3_cpp_obj_unref(compatibility);
                }
                continue;
            }

            ++audioClasses;
            check(std::strcmp(info.category, "Audio Module Class") == 0, "audio class category");
            const std::string id = canonicalId(info.class_id);
            std::printf("%s: %s (expected %s)\n", info.name, id.c_str(), expectedId);
            check(id == expectedId, "class ID string matches every platform and every release since 1.0.1");

            // a host that asks for the plugin by the old ID gets it: the old ID is this one's GUID fields swapped
            v3_tuid oldId;
            std::memcpy(oldId, info.class_id, sizeof(v3_tuid));
            std::swap(oldId[0], oldId[3]);
            std::swap(oldId[1], oldId[2]);
            std::swap(oldId[4], oldId[5]);
            std::swap(oldId[6], oldId[7]);
            check(canonicalId(oldId) == expectedOldId, "the old ID is the other layout of the same bytes");
            v3_component** byOldId = nullptr;
            check(v3_cpp_obj(factory)->create_instance(factory, oldId, v3_component_iid,
                                                       reinterpret_cast<void**>(&byOldId)) == V3_OK
                && byOldId != nullptr, "the factory creates the plugin for the old ID");
            if (byOldId != nullptr)
            {
                check(v3_cpp_obj_initialize(byOldId, nullptr) == V3_OK, "the plugin created by the old ID initializes");
                v3_cpp_obj_terminate(byOldId);
                v3_cpp_obj_unref(byOldId);
            }

            v3_plugin_factory_2** factory2 = nullptr;
            check(v3_cpp_obj_query_interface(factory, v3_plugin_factory_2_iid, &factory2) == V3_OK
                && factory2 != nullptr, "factory v2 available");
            if (factory2 != nullptr)
            {
                v3_class_info_2 info2{};
                check(v3_cpp_obj(factory2)->get_class_info_2(factory2, i, &info2) == V3_OK, "factory v2 class info");
                check(std::memcmp(info2.class_id, info.class_id, sizeof(v3_tuid)) == 0, "factory v2 identity agrees");
                v3_cpp_obj_unref(factory2);
            }
            v3_plugin_factory_3** factory3 = nullptr;
            check(v3_cpp_obj_query_interface(factory, v3_plugin_factory_3_iid, &factory3) == V3_OK
                && factory3 != nullptr, "factory v3 available");
            if (factory3 != nullptr)
            {
                v3_class_info_3 info3{};
                check(v3_cpp_obj(factory3)->get_class_info_utf16(factory3, i, &info3) == V3_OK, "factory v3 class info");
                check(std::memcmp(info3.class_id, info.class_id, sizeof(v3_tuid)) == 0, "factory v3 identity agrees");
                v3_cpp_obj_unref(factory3);
            }
        }
        check(audioClasses == 1, "exactly one audio class");
        check(compatibilityClasses == 1, "exactly one compatibility class");
        v3_cpp_obj_unref(factory);
    }
    check(leave(), "module shutdown");
#ifdef _WIN32
    FreeLibrary(library);
#else
    dlclose(library);
#endif
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
