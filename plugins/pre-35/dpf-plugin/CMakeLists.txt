# Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
# Third-party components (DPF — ISC; Dear ImGui — MIT; and others) are attributed
# in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
#
# Standalone CMake for the DPF-based PRE-35 plugin. Like every other DPF plugin
# in this repo it is NOT referenced from the top-level CMakeLists.txt — that file
# is the JUCE fleet's, and pulls in JUCE unconditionally. Configure this
# directory directly:
#   cmake -B build -GNinja [-DDPF_PATH=/path/to/DPF]
#   cmake --build build
#
# The framework-free core (plugins/pre-35/core) is compiled straight into the DSP
# translation-unit list rather than linked as the pre35_core library: its own
# CMakeLists is a full project that also builds the gate suite and the offline
# render harness, and a plugin build has no business dragging those in. The core
# gates stay where they are and are run from plugins/pre-35/core.

cmake_minimum_required(VERSION 3.15)

# Single-source version: mirror the fleet pattern (cmake/PluginVersion.cmake).
# Priority: CI PLUGIN_VERSION_OVERRIDE env -> exact git tag pre-35-vX.Y.Z ->
# PRE35_DEFAULT_VERSION fallback below. The parsed value feeds project(), the C++
# getVersion()/d_version(...) (via PRE35_VERSION_* compile defs) and the UI
# nameplate, so the tag, the binary and the displayed version cannot drift.
include("${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/PluginVersion.cmake")
set(PRE35_DEFAULT_VERSION "1.0.0")
get_plugin_version("pre-35" PRE35_VERSION)
# project(VERSION) accepts ONLY numeric major[.minor[.patch[.tweak]]] and hard-
# FAILS configure on anything else, while get_plugin_version can return a
# non-numeric string (PLUGIN_VERSION_OVERRIDE is passed through unvalidated, and
# its git-tag regex is not anchored at the end, so pre-35-v1.0.0-rc1 yields
# "1.0.0-rc1"). Require a strict X.Y.Z here so a pre-release tag degrades to a
# buildable version instead of breaking configure.
if(NOT PRE35_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$"
   OR PRE35_VERSION STREQUAL "0.0.0")
    if(PRE35_VERSION AND NOT PRE35_VERSION STREQUAL "0.0.0")
        message(WARNING "pre-35: version '${PRE35_VERSION}' is not a strict X.Y.Z; "
                        "using ${PRE35_DEFAULT_VERSION} instead.")
    endif()
    set(PRE35_VERSION ${PRE35_DEFAULT_VERSION})
endif()

project(Pre35DPF VERSION ${PRE35_VERSION})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/../../shared-dpf/cmake/DuskDpfPlugin.cmake")

dpf_add_plugin(pre_35
    TARGETS clap vst3 lv2 jack au # au builds on macOS only, silently skipped elsewhere
    # MONOLITHIC: DISTRHO_PLUGIN_WANT_DIRECT_ACCESS makes DPF's LV2 TTL export
    # declare the UI inside the dsp binary; the LV2 build must match or hosts
    # fail to load. Single-object LV2 also gives the meters direct access.
    MONOLITHIC
    UI_TYPE opengl
    FILES_DSP
        Pre35Plugin.cpp
        ../core/Pre35DSP.cpp
    FILES_UI
        Pre35UI.cpp
        ${DUSK_DPF_UI_SOURCES})

target_include_directories(pre_35 PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../core"
    ${DUSK_DPF_INCLUDE_DIRS})   # shared-dpf dsp/ui + DPF-Widgets opengl

# Version reaches both the DSP shell (getVersion()/d_version) and the UI
# nameplate. PUBLIC on the base target propagates to the -dsp and -ui static libs
# (they link pre_35 PUBLIC), so every format binary sees the same value.
target_compile_definitions(pre_35 PUBLIC
    PRE35_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    PRE35_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    PRE35_VERSION_PATCH=${PROJECT_VERSION_PATCH})

# The core is compiled with -Wall -Wextra by its own build; keep the same floor
# here so a warning cannot hide just because the file was pulled into the plugin.
#
# Per SOURCE, not per target. dpf_add_plugin compiles FILES_DSP/FILES_UI into the
# generated pre_35-dsp / pre_35-ui static libs, which only LINK the pre_35 base
# library (PUBLIC) — so a PRIVATE flag on pre_35 never reaches them (verified in
# build.ninja: the sources compiled without it), and a PUBLIC one would also land
# on DPF's own DistrhoPluginMain.cpp / DistrhoUIMain.cpp and Dear ImGui, burying
# our warnings in third-party noise.
if(NOT MSVC)
    set_source_files_properties(
        Pre35Plugin.cpp
        Pre35UI.cpp
        ../core/Pre35DSP.cpp
        PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
endif()

# On older glibc (Ubuntu 20.04 / glibc 2.31, our release baseline) shm_open/
# shm_unlink live in librt and pthread_* in libpthread as separate objects that
# pugl (DPF's windowing) references; newer glibc folded them into libc, so a
# modern dev box links without them. Link them explicitly on Linux so the release
# containers link too. Applied to the final module targets where pugl resolves.
if(UNIX AND NOT APPLE)
    foreach(_fmt clap vst3 lv2 jack)
        if(TARGET pre_35-${_fmt})
            # --no-as-needed (emitted before the link libraries) forces librt /
            # libpthread to stay on the line even though they appear before the
            # dgl archive that references shm_*/pthread_*; being shared libs they
            # then resolve those refs regardless of archive order.
            target_link_options(pre_35-${_fmt} PRIVATE "LINKER:--no-as-needed")
            target_link_libraries(pre_35-${_fmt} PRIVATE rt pthread)
        endif()
    endforeach()
endif()

# Auto-deploy the built CLAP/VST3/LV2 into the user plugin dirs after each build
# (disable with -DDUSK_DPF_INSTALL_LOCAL=OFF). No LV2_PROGRAMS: PRE-35 ships no
# factory programs, so there is no presets.ttl to repair.
dusk_dpf_install_local(pre_35)
