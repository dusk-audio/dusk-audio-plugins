# Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
# Third-party components (DAF — ISC; Dear ImGui — MIT; and others) are attributed
# in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
#
# DuskDafPlugin.cmake — shared DAF wiring for Dusk Audio DAF plugins.
#
# include() this from a plugin's CMakeLists after project(). It locates the
# DAF and DAF-Widgets checkouts (siblings of the repo by default, overridable
# with -DDAF_PATH=... / -DDAFWIDGETS_PATH=...), adds DAF as a subdirectory, and
# exposes DUSK_DAF_UI_SOURCES (the DearImGui wrapper) and DUSK_DAF_INCLUDE_DIRS
# (shared-daf dsp/ui + DAF-Widgets opengl) for the caller to attach. Plugins
# still call daf_add_plugin themselves so per-plugin TARGETS/FILES stay local.

if(NOT DEFINED DUSK_SHARED_DAF_DIR)
    set(DUSK_SHARED_DAF_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
endif()
set(DUSK_SHARED_DAF_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

# repo root is three levels up from plugins/<name>/daf-plugin
set(_dusk_repo_root "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
set(DAF_PATH        "${_dusk_repo_root}/../DAF"         CACHE PATH "Path to Dusk Audio Framework")
set(DAFWIDGETS_PATH "${_dusk_repo_root}/../DAF-Widgets" CACHE PATH "Path to DAF-Widgets (Dear ImGui wrapper)")

if(NOT EXISTS "${DAF_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "DAF not found at ${DAF_PATH} — clone https://github.com/dusk-audio/DAF (our fork; do not use upstream DISTRHO/DPF) or pass -DDAF_PATH=...")
endif()
if(NOT EXISTS "${DAFWIDGETS_PATH}/opengl/DearImGui.cpp")
    message(FATAL_ERROR "DAF-Widgets not found at ${DAFWIDGETS_PATH} — clone https://github.com/dusk-audio/DAF-Widgets (our fork; do not use upstream DAF) or pass -DDAFWIDGETS_PATH=...")
endif()

if(NOT TARGET daf)
    add_subdirectory("${DAF_PATH}" daf EXCLUDE_FROM_ALL)
endif()

set(DUSK_DAF_UI_SOURCES  "${DAFWIDGETS_PATH}/opengl/DearImGui.cpp")
set(DUSK_DAF_INCLUDE_DIRS
    "${DUSK_SHARED_DAF_DIR}"
    "${DUSK_SHARED_DAF_DIR}/dsp"
    "${DUSK_SHARED_DAF_DIR}/ui"
    "${DAFWIDGETS_PATH}/opengl")

# Copy the built CLAP/VST3/LV2 artefacts into the user plugin dirs after each
# build, so hosts always load the freshly-built binary (DAF's ninja target only
# writes to <build>/bin). Call AFTER daf_add_plugin with the plugin base name.
# Disable with -DDUSK_DAF_INSTALL_LOCAL=OFF (e.g. on CI / release runners).
option(DUSK_DAF_INSTALL_LOCAL "Copy built DAF plugins into the user plugin dirs after build" ON)

function(dusk_daf_install_local plugin_name)
    cmake_parse_arguments(DUSK_DAF "LV2_PROGRAMS" "" "" ${ARGN})

    # DAF's preset exporter can prematurely close an lv2:port list when an
    # output parameter appears before later input parameters. Repair the
    # generated Turtle before validation, packaging, or local installation.
    if(TARGET ${plugin_name}-lv2)
        add_custom_command(TARGET ${plugin_name}-lv2 POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                "-DPRESETS_FILE=${CMAKE_BINARY_DIR}/bin/${plugin_name}.lv2/presets.ttl"
                "-DEXPECT_PRESETS=${DUSK_DAF_LV2_PROGRAMS}"
                -P "${DUSK_SHARED_DAF_CMAKE_DIR}/DuskFixLv2Presets.cmake"
            COMMENT "Checking ${plugin_name}.lv2 factory-preset metadata"
            VERBATIM)
    endif()

    # Apple's AU registrar validates the signature of the completed component
    # bundle, including Info.plist.  The arm64 linker only ad-hoc signs the
    # executable, which leaves a bundle that codesign --strict (and Logic) will
    # reject once DAF adds the remaining bundle files.  Re-sign the finished AU
    # before either validation or local installation.
    # The verify pass is not decoration: a stale resource left in the bundle, or a
    # post-sign edit of Info.plist, breaks the seal in a way that only shows up as
    # the component silently missing from the host's plugin menu. Fail the build
    # instead, matching the identical sign+verify pair in .github/workflows/daf-build.yml.
    if(CMAKE_HOST_APPLE AND TARGET ${plugin_name}-au)
        add_custom_command(TARGET ${plugin_name}-au POST_BUILD
            COMMAND /usr/bin/codesign --force --deep --sign -
                "${CMAKE_BINARY_DIR}/bin/${plugin_name}.component"
            COMMAND /usr/bin/codesign --verify --deep --strict --verbose=2
                "${CMAKE_BINARY_DIR}/bin/${plugin_name}.component"
            COMMENT "Signing ${plugin_name}.component"
            VERBATIM)
    endif()

    if(NOT DUSK_DAF_INSTALL_LOCAL)
        return()
    endif()
    if(NOT DEFINED ENV{HOME} OR "$ENV{HOME}" STREQUAL "")
        message(STATUS "DUSK_DAF_INSTALL_LOCAL: HOME unset, skipping local install of ${plugin_name}")
        return()
    endif()

    if(CMAKE_HOST_APPLE)
        set(_clap "$ENV{HOME}/Library/Audio/Plug-Ins/CLAP")
        set(_vst3 "$ENV{HOME}/Library/Audio/Plug-Ins/VST3")
        set(_lv2  "$ENV{HOME}/Library/Audio/Plug-Ins/LV2")
        set(_au   "$ENV{HOME}/Library/Audio/Plug-Ins/Components")  # AU is macOS-only
    else()
        set(_clap "$ENV{HOME}/.clap")
        set(_vst3 "$ENV{HOME}/.vst3")
        set(_lv2  "$ENV{HOME}/.lv2")
    endif()
    set(_bin "${CMAKE_BINARY_DIR}/bin")

    if(TARGET ${plugin_name}-clap)
        # macOS emits a .clap BUNDLE (directory, like VST3/LV2); Linux/Windows a
        # single-file .clap. Copy accordingly so the macOS install isn't a
        # truncated single-file copy of a bundle.
        if(CMAKE_HOST_APPLE)
            add_custom_command(TARGET ${plugin_name}-clap POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${_clap}"
                COMMAND ${CMAKE_COMMAND} -E copy_directory "${_bin}/${plugin_name}.clap" "${_clap}/${plugin_name}.clap"
                COMMENT "Installing ${plugin_name}.clap -> ${_clap}"
                VERBATIM)
        else()
            add_custom_command(TARGET ${plugin_name}-clap POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${_clap}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_bin}/${plugin_name}.clap" "${_clap}/${plugin_name}.clap"
                COMMENT "Installing ${plugin_name}.clap -> ${_clap}"
                VERBATIM)
        endif()
    endif()
    if(TARGET ${plugin_name}-vst3)
        add_custom_command(TARGET ${plugin_name}-vst3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_vst3}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${_bin}/${plugin_name}.vst3" "${_vst3}/${plugin_name}.vst3"
            COMMENT "Installing ${plugin_name}.vst3 -> ${_vst3}"
            VERBATIM)
    endif()
    if(TARGET ${plugin_name}-lv2)
        add_custom_command(TARGET ${plugin_name}-lv2 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_lv2}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${_bin}/${plugin_name}.lv2" "${_lv2}/${plugin_name}.lv2"
            COMMENT "Installing ${plugin_name}.lv2 -> ${_lv2}"
            VERBATIM)
    endif()
    # AU is a macOS-only .component bundle; DAF only creates the -au target when
    # building on macOS, so this branch is inert (target absent) elsewhere.
    # copy_directory MERGES, and the AU is code-signed: any file left over from an
    # older build stays behind, is absent from the fresh _CodeSignature seal, and
    # makes `codesign --verify` report "a sealed resource is missing or invalid" —
    # which Apple's AU registrar turns into a component that loads nowhere. Wipe
    # the installed bundle first so the copy is exactly what was signed.
    if(TARGET ${plugin_name}-au)
        if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.17)
            set(_rm_installed_au ${CMAKE_COMMAND} -E rm -rf "${_au}/${plugin_name}.component")
        else()
            set(_rm_installed_au ${CMAKE_COMMAND} -E remove_directory "${_au}/${plugin_name}.component")
        endif()
        add_custom_command(TARGET ${plugin_name}-au POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_au}"
            COMMAND ${_rm_installed_au}
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${_bin}/${plugin_name}.component" "${_au}/${plugin_name}.component"
            COMMAND /usr/bin/codesign --verify --deep --strict --verbose=2 "${_au}/${plugin_name}.component"
            COMMENT "Installing ${plugin_name}.component -> ${_au}"
            VERBATIM)
    endif()
endfunction()
