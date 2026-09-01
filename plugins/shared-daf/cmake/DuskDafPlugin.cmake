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

# Register the shared output-parameter regression tests for a plugin that
# exposes meters (dusk-audio/plugins#233 for VST3, #231 for CLAP). Each builds a
# minimal host, runs noise through the plugin's own binary for that format, and
# fails if any output parameter reaches the host as a parameter event -- or if
# the meters stopped moving, which is what keeps the first assertion from being
# satisfiable by simply freezing them.
#
# Call AFTER daf_add_plugin, with the plugin base name. Inert where it cannot
# run: the harnesses dlopen() the plugin binary directly, so Windows and cross
# builds are skipped rather than failed.
#
# Wired for 4k-eq, TapeMachine and tape-echo. multi-comp and sunset-circuits
# also declare kParameterIsOutput meters and are covered by the same wrapper
# fixes, but not by these harnesses: multi-comp's gain-reduction meters only
# move once the compressor is actually compressing, and sunset-circuits is a
# synth with no audio input at all, so neither can satisfy the "meters still
# move" assertion from noise at default settings. Driving them would need
# per-plugin parameter and note setup; the three wired plugins already fail the
# moment either shared wrapper regresses.
#
# A macro, not a function, and deliberately so: enable_testing() called from
# inside a function() sets nothing -- no CTestTestfile.cmake is written and
# ctest reports "No tests were found" while the targets still build, which
# would leave these guards silently dead in a CI job that treats zero tests as a
# pass. A macro expands into the caller's own directory scope, where
# enable_testing() takes effect, so callers cannot forget it. Being a macro is
# also why the body is wrapped in if() rather than guarded with return(): a
# return() here would return from the caller's CMakeLists.
macro(dusk_daf_add_output_param_tests _dusk_op_plugin)
    # A missing target is a typo or a rename, not a platform the harness cannot
    # run on, and the two must not share a branch: silently skipping it would
    # register no test, leave enable_testing() uncalled, and let CI -- which
    # passes a build that reports zero tests -- go green with these guards dead.
    if(NOT CMAKE_CROSSCOMPILING AND NOT WIN32)
        foreach(_dusk_op_fmt vst3 clap)
            if(NOT TARGET ${_dusk_op_plugin}-${_dusk_op_fmt})
                message(FATAL_ERROR
                    "dusk_daf_add_output_param_tests(${_dusk_op_plugin}): no target "
                    "${_dusk_op_plugin}-${_dusk_op_fmt}. Call this after daf_add_plugin, "
                    "with the plugin base name, and with both formats in TARGETS.")
            endif()
        endforeach()
    endif()

    if(CMAKE_CROSSCOMPILING OR WIN32)
        message(STATUS "Output-parameter tests skipped for ${_dusk_op_plugin} (unsupported platform)")
    else()
        enable_testing()

        # travesty/ and clap/ are DAF's own vendored interfaces; the harnesses
        # speak the same ABI the wrappers are compiled against rather than a
        # second copy of either SDK.
        add_executable(${_dusk_op_plugin}Vst3OutputParamTest
            "${DUSK_SHARED_DAF_DIR}/tests/DafVst3OutputParamTest.cpp")
        target_include_directories(${_dusk_op_plugin}Vst3OutputParamTest PRIVATE "${DAF_PATH}/daf/src")
        target_compile_features(${_dusk_op_plugin}Vst3OutputParamTest PRIVATE cxx_std_17)
        target_link_libraries(${_dusk_op_plugin}Vst3OutputParamTest PRIVATE ${CMAKE_DL_LIBS})
        add_dependencies(${_dusk_op_plugin}Vst3OutputParamTest ${_dusk_op_plugin}-vst3)

        add_executable(${_dusk_op_plugin}ClapOutputParamTest
            "${DUSK_SHARED_DAF_DIR}/tests/DafClapOutputParamTest.cpp")
        target_include_directories(${_dusk_op_plugin}ClapOutputParamTest PRIVATE "${DAF_PATH}/daf/src")
        target_compile_features(${_dusk_op_plugin}ClapOutputParamTest PRIVATE cxx_std_17)
        target_link_libraries(${_dusk_op_plugin}ClapOutputParamTest PRIVATE ${CMAKE_DL_LIBS})
        add_dependencies(${_dusk_op_plugin}ClapOutputParamTest ${_dusk_op_plugin}-clap)

        # $<TARGET_FILE:...> is the loadable binary itself, so the harnesses
        # never have to reconstruct Contents/<arch>-linux/ and keep working on
        # architectures no table here would list.
        add_test(NAME ${_dusk_op_plugin}Vst3OutputParams
                 COMMAND ${_dusk_op_plugin}Vst3OutputParamTest
                         "$<TARGET_FILE:${_dusk_op_plugin}-vst3>" 100 256)
        add_test(NAME ${_dusk_op_plugin}ClapOutputParams
                 COMMAND ${_dusk_op_plugin}ClapOutputParamTest
                         "$<TARGET_FILE:${_dusk_op_plugin}-clap>" 100 256)
    endif()
endmacro()

# ---------------------------------------------------------------------------------------------------------------------
# UI drag guard (dusk-audio/plugins#233 follow-up)
#
# Opens the plugin's real editor from the built .clap and drives it with
# synthetic X input, asserting that a drag moves a parameter and keeps moving it
# across a focus change. This is the gate that would have caught the DAF-Widgets
# focus regression, which shipped through four releases because every other gate
# we run is blind to the editor: pluginval runs --skip-gui-tests, clap-validator
# never touches the UI, and the output-parameter harnesses open no window.
#
# Linux only, and that is not a temporary limitation to fix later: the harness
# needs XTest to synthesise input the way real hardware does. macOS would need a
# different injection path (CGEvent) and Windows another (SendInput). One
# platform exercising the shared ImGui backend is enough to catch a regression in
# it, since the backend is the same code everywhere.
#
# Needs a running X server, so ctest must be invoked under xvfb-run in CI. The
# harness fails loudly with that instruction when DISPLAY is unset rather than
# skipping, because a silent skip is exactly how a gate rots.
#
# Same macro-not-function reasoning as dusk_daf_add_output_param_tests above.
macro(dusk_daf_add_ui_drag_test _dusk_ui_plugin _dusk_ui_knob_x _dusk_ui_knob_y)
    if(NOT CMAKE_CROSSCOMPILING AND NOT WIN32 AND NOT APPLE)
        if(NOT TARGET ${_dusk_ui_plugin}-clap)
            message(FATAL_ERROR
                "dusk_daf_add_ui_drag_test(${_dusk_ui_plugin}): no target ${_dusk_ui_plugin}-clap. "
                "Call this after daf_add_plugin, with the plugin base name, and with clap in TARGETS.")
        endif()

        find_package(X11 REQUIRED)
        if(NOT X11_XTest_LIB)
            message(FATAL_ERROR
                "dusk_daf_add_ui_drag_test(${_dusk_ui_plugin}): XTest not found. "
                "Install libxtst-dev; without it the editor cannot be driven and the guard would be dead.")
        endif()

        enable_testing()

        add_executable(${_dusk_ui_plugin}UiDragTest
            "${DUSK_SHARED_DAF_DIR}/tests/DafClapUiDragTest.cpp")
        target_include_directories(${_dusk_ui_plugin}UiDragTest PRIVATE "${DAF_PATH}/daf/src")
        target_compile_features(${_dusk_ui_plugin}UiDragTest PRIVATE cxx_std_17)
        target_link_libraries(${_dusk_ui_plugin}UiDragTest PRIVATE
            ${CMAKE_DL_LIBS} ${X11_X11_LIB} ${X11_XTest_LIB})
        add_dependencies(${_dusk_ui_plugin}UiDragTest ${_dusk_ui_plugin}-clap)

        # The knob coordinate is per plugin and deliberately explicit: see the
        # header comment in DafClapUiDragTest.cpp for why the harness is aimed
        # rather than left to hunt for something clickable.
        add_test(NAME ${_dusk_ui_plugin}UiDrag
                 COMMAND ${_dusk_ui_plugin}UiDragTest "$<TARGET_FILE:${_dusk_ui_plugin}-clap>"
                         ${_dusk_ui_knob_x} ${_dusk_ui_knob_y})
        # The editor is opened, swept over a grid and dragged twice; well under
        # this on a normal machine, but a loaded CI runner is slower and a hang
        # must fail rather than block the job forever.
        set_tests_properties(${_dusk_ui_plugin}UiDrag PROPERTIES TIMEOUT 300)
    endif()
endmacro()

# ---------------------------------------------------------------------------------------------------------------------
# Editor size-contract guard
#
# Asserts that the aspect ratio the editor advertises is the one it is actually
# drawn to, and (where a toggle coordinate is given) that changing the design size
# renotifies the host BEFORE asking it to resize, and that the size returns
# exactly across toggles.
#
# Both halves shipped broken and neither was catchable by any other gate: 4K EQ 2
# advertised 560x373 against a 960x640 design, and DAF only ever called
# clap_host_gui->resize_hints_changed() at UI creation, so a host kept enforcing
# a stale ratio. See the header comment in DafClapResizeTest.cpp.
#
# The aspect half needs no interaction, so call this for EVERY plugin. Pass the
# coordinates of a control that changes the design size only where one exists.
#
# Same platform limits and macro-not-function reasoning as the guards above.
macro(dusk_daf_add_resize_test _dusk_rs_plugin)
    if(NOT CMAKE_CROSSCOMPILING AND NOT WIN32 AND NOT APPLE)
        if(NOT TARGET ${_dusk_rs_plugin}-clap)
            message(FATAL_ERROR
                "dusk_daf_add_resize_test(${_dusk_rs_plugin}): no target ${_dusk_rs_plugin}-clap. "
                "Call this after daf_add_plugin, with the plugin base name, and with clap in TARGETS.")
        endif()

        find_package(X11 REQUIRED)
        if(NOT X11_XTest_LIB)
            message(FATAL_ERROR
                "dusk_daf_add_resize_test(${_dusk_rs_plugin}): XTest not found. Install libxtst-dev.")
        endif()

        enable_testing()

        add_executable(${_dusk_rs_plugin}ResizeTest
            "${DUSK_SHARED_DAF_DIR}/tests/DafClapResizeTest.cpp")
        target_include_directories(${_dusk_rs_plugin}ResizeTest PRIVATE "${DAF_PATH}/daf/src")
        target_compile_features(${_dusk_rs_plugin}ResizeTest PRIVATE cxx_std_17)
        target_link_libraries(${_dusk_rs_plugin}ResizeTest PRIVATE
            ${CMAKE_DL_LIBS} ${X11_X11_LIB} ${X11_XTest_LIB})
        add_dependencies(${_dusk_rs_plugin}ResizeTest ${_dusk_rs_plugin}-clap)

        add_test(NAME ${_dusk_rs_plugin}Resize
                 COMMAND ${_dusk_rs_plugin}ResizeTest "$<TARGET_FILE:${_dusk_rs_plugin}-clap>" ${ARGN})
        set_tests_properties(${_dusk_rs_plugin}Resize PROPERTIES TIMEOUT 300)
    endif()
endmacro()
