// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// Pre35Version.hpp — version shared by the PRE-35 plugin metadata and UI. CMake
// supplies these values from project(... VERSION ...); the fallbacks keep a
// non-CMake source build usable.

#pragma once

#ifndef PRE35_VERSION_MAJOR
 #define PRE35_VERSION_MAJOR 1
 #define PRE35_VERSION_MINOR 0
 #define PRE35_VERSION_PATCH 0
#endif

#define PRE35_STRINGIFY_IMPL(value) #value
#define PRE35_STRINGIFY(value) PRE35_STRINGIFY_IMPL(value)
#define PRE35_VERSION_STRING \
    PRE35_STRINGIFY(PRE35_VERSION_MAJOR) "." \
    PRE35_STRINGIFY(PRE35_VERSION_MINOR) "." \
    PRE35_STRINGIFY(PRE35_VERSION_PATCH)
