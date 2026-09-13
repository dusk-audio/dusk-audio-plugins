// Copyright (C) 2026 Dusk Audio — GPL-3.0-or-later.
// Small loader shim for the native binary host tests on Windows and POSIX.
#pragma once
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
constexpr int RTLD_NOW = 0;
constexpr int RTLD_LOCAL = 0;
inline void* dlopen(const char* path, int) { return reinterpret_cast<void*>(LoadLibraryA(path)); }
inline void* dlsym(void* library, const char* symbol)
{ return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), symbol)); }
inline int dlclose(void* library) { return FreeLibrary(static_cast<HMODULE>(library)) ? 0 : -1; }
inline const char* dlerror()
{
    static thread_local char text[64];
    std::snprintf(text, sizeof(text), "Windows loader error %lu", GetLastError());
    return text;
}
#else
#include <dlfcn.h>
#endif
