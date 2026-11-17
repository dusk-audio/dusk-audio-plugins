// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
//
// ChordAnalyzerVersion.hpp — the Chord Analyzer 2 version as a string, for the
// editor's nameplate and supporters panel. CMake supplies CA2_VERSION_* from
// project(... VERSION ...); the plugin's getVersion() reads the same values.

#pragma once

#if !defined(CA2_VERSION_MAJOR) || !defined(CA2_VERSION_MINOR) || !defined(CA2_VERSION_PATCH)
 #error "CA2_VERSION_* come from project(... VERSION ...) in plugins/chord-analyzer/daf-plugin/CMakeLists.txt"
#endif

#define CA2_STRINGIFY_IMPL(value) #value
#define CA2_STRINGIFY(value) CA2_STRINGIFY_IMPL(value)
#define CA2_VERSION_STRING \
    CA2_STRINGIFY(CA2_VERSION_MAJOR) "." \
    CA2_STRINGIFY(CA2_VERSION_MINOR) "." \
    CA2_STRINGIFY(CA2_VERSION_PATCH)
