// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
#include "travesty/factory.h"

#include <cstdio>
#include <cstring>
#include <string>

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
}

int main(int argc, char** argv)
{
    if (argc != 3 || (std::strcmp(argv[2], "0") != 0 && std::strcmp(argv[2], "1") != 0))
    {
        std::fprintf(stderr, "Usage: %s VST3_BINARY MIDI_MODE(0|1)\n", argv[0]);
        return 2;
    }
    const bool midi = argv[2][0] == '1';
    // Freeze the new products' Linux/macOS identities before their first release.
    const char* expectedId = midi ? "2046504473616C634D4373446B737544" : "2046504473616C63414373446B737544";
    const char* expectedName = midi ? "Chord Analyzer MIDI 2" : "Chord Analyzer 2";
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
        check(count == 1, "one monolithic audio class");
        for (int i = 0; i < count; ++i)
        {
            v3_class_info info{};
            const bool haveInfo = v3_cpp_obj(factory)->get_class_info(factory, i, &info) == V3_OK;
            check(haveInfo, "factory class info");
            if (!haveInfo) continue;
            check(std::strcmp(info.category, "Audio Module Class") == 0, "audio class category");
            check(std::strcmp(info.name, expectedName) == 0, "product name");
            const std::string id = canonicalId(info.class_id);
            std::printf("%s: %s (expected %s)\n", info.name, id.c_str(), expectedId);
            check(id == expectedId, "canonical class ID matches every platform");

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
