// Copyright (C) 2026 Dusk Audio - GNU GPL v3.0 or later (see repository LICENSE).
//
// SunsetVersion.hpp - version shared by Sunset Circuits metadata, UI and
// diagnostics. CMake supplies these values from project(... VERSION ...); the
// fallbacks keep non-CMake source builds consistent.

#pragma once

#ifndef SC_VERSION_MAJOR
 #define SC_VERSION_MAJOR 1
#endif
#ifndef SC_VERSION_MINOR
 #define SC_VERSION_MINOR 0
#endif
#ifndef SC_VERSION_PATCH
 #define SC_VERSION_PATCH 1
#endif

#ifndef SC_VERSION_STRING
 #define SC_STRINGIFY_IMPL(value) #value
 #define SC_STRINGIFY(value) SC_STRINGIFY_IMPL(value)
 #define SC_VERSION_STRING \
    SC_STRINGIFY(SC_VERSION_MAJOR) "." \
    SC_STRINGIFY(SC_VERSION_MINOR) "." \
    SC_STRINGIFY(SC_VERSION_PATCH)
#endif
