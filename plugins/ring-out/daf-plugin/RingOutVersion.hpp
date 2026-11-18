// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// RingOutVersion.hpp — version shared by the Ring Out plugin metadata and UI.
// CMake supplies these values from project(... VERSION ...); the fallbacks keep
// non-CMake source builds usable.

#pragma once

#ifndef RO_VERSION_MAJOR
 #define RO_VERSION_MAJOR 0
 #define RO_VERSION_MINOR 1
 #define RO_VERSION_PATCH 0
#endif

#define RO_STRINGIFY_IMPL(value) #value
#define RO_STRINGIFY(value) RO_STRINGIFY_IMPL(value)
#define RO_VERSION_STRING \
    RO_STRINGIFY(RO_VERSION_MAJOR) "." \
    RO_STRINGIFY(RO_VERSION_MINOR) "." \
    RO_STRINGIFY(RO_VERSION_PATCH)
