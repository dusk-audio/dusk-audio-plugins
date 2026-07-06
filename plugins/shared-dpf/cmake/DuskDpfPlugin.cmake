# Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
# Third-party components (DPF — ISC; Dear ImGui — MIT; and others) are attributed
# in plugins/shared-dpf/THIRD_PARTY_LICENSES.md.
#
# DuskDpfPlugin.cmake — shared DPF wiring for Dusk Audio DPF plugins.
#
# include() this from a plugin's CMakeLists after project(). It locates the
# DPF and DPF-Widgets checkouts (siblings of the repo by default, overridable
# with -DDPF_PATH=... / -DDPFWIDGETS_PATH=...), adds DPF as a subdirectory, and
# exposes DUSK_DPF_UI_SOURCES (the DearImGui wrapper) and DUSK_DPF_INCLUDE_DIRS
# (shared-dpf dsp/ui + DPF-Widgets opengl) for the caller to attach. Plugins
# still call dpf_add_plugin themselves so per-plugin TARGETS/FILES stay local.

if(NOT DEFINED DUSK_SHARED_DPF_DIR)
    set(DUSK_SHARED_DPF_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
endif()

# repo root is three levels up from plugins/<name>/dpf-plugin
set(_dusk_repo_root "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
set(DPF_PATH        "${_dusk_repo_root}/../DPF"         CACHE PATH "Path to DISTRHO Plugin Framework")
set(DPFWIDGETS_PATH "${_dusk_repo_root}/../DPF-Widgets" CACHE PATH "Path to DPF-Widgets (Dear ImGui wrapper)")

if(NOT EXISTS "${DPF_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "DPF not found at ${DPF_PATH} — clone https://github.com/DISTRHO/DPF or pass -DDPF_PATH=...")
endif()
if(NOT EXISTS "${DPFWIDGETS_PATH}/opengl/DearImGui.cpp")
    message(FATAL_ERROR "DPF-Widgets not found at ${DPFWIDGETS_PATH} — clone https://github.com/DISTRHO/DPF-Widgets or pass -DDPFWIDGETS_PATH=...")
endif()

if(NOT TARGET dpf)
    add_subdirectory("${DPF_PATH}" dpf EXCLUDE_FROM_ALL)
endif()

set(DUSK_DPF_UI_SOURCES  "${DPFWIDGETS_PATH}/opengl/DearImGui.cpp")
set(DUSK_DPF_INCLUDE_DIRS
    "${DUSK_SHARED_DPF_DIR}"
    "${DUSK_SHARED_DPF_DIR}/dsp"
    "${DUSK_SHARED_DPF_DIR}/ui"
    "${DPFWIDGETS_PATH}/opengl")

# Copy the built CLAP/VST3/LV2 artefacts into the user plugin dirs after each
# build, so hosts always load the freshly-built binary (DPF's ninja target only
# writes to <build>/bin). Call AFTER dpf_add_plugin with the plugin base name.
# Disable with -DDUSK_DPF_INSTALL_LOCAL=OFF (e.g. on CI / release runners).
option(DUSK_DPF_INSTALL_LOCAL "Copy built DPF plugins into the user plugin dirs after build" ON)

function(dusk_dpf_install_local plugin_name)
    if(NOT DUSK_DPF_INSTALL_LOCAL)
        return()
    endif()
    if(NOT DEFINED ENV{HOME} OR "$ENV{HOME}" STREQUAL "")
        message(STATUS "DUSK_DPF_INSTALL_LOCAL: HOME unset, skipping local install of ${plugin_name}")
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
    # AU is a macOS-only .component bundle; DPF only creates the -au target when
    # building on macOS, so this branch is inert (target absent) elsewhere.
    if(TARGET ${plugin_name}-au)
        add_custom_command(TARGET ${plugin_name}-au POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_au}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${_bin}/${plugin_name}.component" "${_au}/${plugin_name}.component"
            COMMENT "Installing ${plugin_name}.component -> ${_au}"
            VERBATIM)
    endif()
endfunction()
