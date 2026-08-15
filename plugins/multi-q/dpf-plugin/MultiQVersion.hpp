// Copyright (C) 2026 Dusk Audio - GNU GPL v3.0 or later (see repository LICENSE).
//
// MultiQVersion.hpp - version shared by Multi-Q 2 metadata and diagnostics.
// CMake supplies these values from project(... VERSION ...); the fallbacks keep
// non-CMake source builds usable.

#pragma once

#ifndef MQ2_VERSION_MAJOR
 #define MQ2_VERSION_MAJOR 2
#endif
#ifndef MQ2_VERSION_MINOR
 #define MQ2_VERSION_MINOR 0
#endif
#ifndef MQ2_VERSION_PATCH
 #define MQ2_VERSION_PATCH 1
#endif

#define MQ2_STRINGIFY_IMPL(value) #value
#define MQ2_STRINGIFY(value) MQ2_STRINGIFY_IMPL(value)
#ifndef MQ2_VERSION_STRING
 #define MQ2_VERSION_STRING \
     MQ2_STRINGIFY(MQ2_VERSION_MAJOR) "." \
     MQ2_STRINGIFY(MQ2_VERSION_MINOR) "." \
     MQ2_STRINGIFY(MQ2_VERSION_PATCH)
#endif
