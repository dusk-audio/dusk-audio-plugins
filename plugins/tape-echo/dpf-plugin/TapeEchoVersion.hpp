// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// TapeEchoVersion.hpp — version shared by the Tape Echo 2 plugin metadata and
// UI. CMake supplies these values from project(... VERSION ...); the fallbacks
// keep non-CMake source builds usable.

#pragma once

#ifndef TE2_VERSION_MAJOR
 #define TE2_VERSION_MAJOR 1
 #define TE2_VERSION_MINOR 0
 #define TE2_VERSION_PATCH 0
#endif

#define TE2_STRINGIFY_IMPL(value) #value
#define TE2_STRINGIFY(value) TE2_STRINGIFY_IMPL(value)
#define TE2_VERSION_STRING \
    TE2_STRINGIFY(TE2_VERSION_MAJOR) "." \
    TE2_STRINGIFY(TE2_VERSION_MINOR) "." \
    TE2_STRINGIFY(TE2_VERSION_PATCH)
