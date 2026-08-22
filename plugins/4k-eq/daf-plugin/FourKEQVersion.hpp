// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// FourKEQVersion.hpp — version shared by the 4K EQ 2 plugin metadata and UI.
// CMake supplies these values from project(... VERSION ...); the fallbacks
// keep non-CMake source builds usable.

#pragma once

#ifndef FOURKEQ2_VERSION_MAJOR
 #define FOURKEQ2_VERSION_MAJOR 2
 #define FOURKEQ2_VERSION_MINOR 0
 #define FOURKEQ2_VERSION_PATCH 11
#endif

#define FOURKEQ2_STRINGIFY_IMPL(value) #value
#define FOURKEQ2_STRINGIFY(value) FOURKEQ2_STRINGIFY_IMPL(value)
#define FOURKEQ2_VERSION_STRING \
    FOURKEQ2_STRINGIFY(FOURKEQ2_VERSION_MAJOR) "." \
    FOURKEQ2_STRINGIFY(FOURKEQ2_VERSION_MINOR) "." \
    FOURKEQ2_STRINGIFY(FOURKEQ2_VERSION_PATCH)
