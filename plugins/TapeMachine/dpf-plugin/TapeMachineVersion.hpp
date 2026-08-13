// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// TapeMachineVersion.hpp — version shared by the TapeMachine 2 plugin metadata
// and UI. CMake supplies these values from project(... VERSION ...); the
// fallbacks keep non-CMake source builds usable.

#pragma once

#ifndef TM2_VERSION_MAJOR
 #define TM2_VERSION_MAJOR 1
 #define TM2_VERSION_MINOR 0
 #define TM2_VERSION_PATCH 5
#endif

#define TM2_STRINGIFY_IMPL(value) #value
#define TM2_STRINGIFY(value) TM2_STRINGIFY_IMPL(value)
#define TM2_VERSION_STRING \
    TM2_STRINGIFY(TM2_VERSION_MAJOR) "." \
    TM2_STRINGIFY(TM2_VERSION_MINOR) "." \
    TM2_STRINGIFY(TM2_VERSION_PATCH)
