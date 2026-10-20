# Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
# Shared by the retained JUCE build, standalone headless build and DAF successors.
# Only the numeric core and public LV2 headers enter the headless binary.
function(chord_add_headless_lv2 source_dir sdk_dir bundle_dir)
    foreach(header core/lv2.h atom/atom.h atom/util.h midi/midi.h urid/urid.h)
        if(NOT EXISTS "${sdk_dir}/lv2/${header}")
            message(FATAL_ERROR "Missing LV2 SDK header: ${sdk_dir}/lv2/${header}")
        endif()
    endforeach()
    add_library(ChordAnalyzerHeadless_LV2 MODULE "${source_dir}/chord-analyzer-lv2.cpp")
    target_include_directories(ChordAnalyzerHeadless_LV2 PRIVATE "${sdk_dir}")
    target_link_libraries(ChordAnalyzerHeadless_LV2 PRIVATE ChordAnalyzerCore)
    target_compile_features(ChordAnalyzerHeadless_LV2 PRIVATE cxx_std_17)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(ChordAnalyzerHeadless_LV2 PRIVATE LINKER:--no-undefined)
    endif()
    set_target_properties(ChordAnalyzerHeadless_LV2 PROPERTIES
        PREFIX "" SUFFIX ".so" OUTPUT_NAME "ChordAnalyzerHeadless"
        POSITION_INDEPENDENT_CODE ON)
    if(CMAKE_BUILD_TYPE STREQUAL "Release" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(ChordAnalyzerHeadless_LV2 PRIVATE -O3 -ffast-math -fno-finite-math-only)
    endif()

    # Metadata-only changes must refresh the bundle even when C++ is unchanged.
    set_property(TARGET ChordAnalyzerHeadless_LV2 APPEND PROPERTY LINK_DEPENDS
        "${source_dir}/manifest.ttl" "${source_dir}/chord-analyzer-headless.ttl")
    add_custom_command(TARGET ChordAnalyzerHeadless_LV2 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${bundle_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:ChordAnalyzerHeadless_LV2>" "${bundle_dir}/ChordAnalyzerHeadless.so"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${source_dir}/manifest.ttl" "${bundle_dir}/manifest.ttl"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${source_dir}/chord-analyzer-headless.ttl" "${bundle_dir}/chord-analyzer-headless.ttl"
        COMMENT "Assembling Chord Analyzer Headless.lv2 bundle" VERBATIM)

    if(DUSK_COPY_AFTER_BUILD OR DUSK_DAF_INSTALL_LOCAL)
        if(APPLE)
            set(install_dir "$ENV{HOME}/Library/Audio/Plug-Ins/LV2")
        elseif(UNIX)
            set(install_dir "$ENV{HOME}/.lv2")
        elseif(WIN32)
            set(install_dir "$ENV{APPDATA}/LV2")
        endif()
        if(DEFINED install_dir)
            add_custom_command(TARGET ChordAnalyzerHeadless_LV2 POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${install_dir}"
                COMMAND ${CMAKE_COMMAND} -E copy_directory "${bundle_dir}" "${install_dir}/Chord Analyzer Headless.lv2"
                COMMENT "Installing Chord Analyzer Headless.lv2 to ${install_dir}" VERBATIM)
        endif()
    endif()

    if(NOT CMAKE_CROSSCOMPILING)
        chord_add_native_midi_test("${source_dir}/.." "${sdk_dir}")
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME ChordAnalyzerHeadless COMMAND "${Python3_EXECUTABLE}"
            "${source_dir}/../tests/test_chord_analyzer_lv2.py" "${bundle_dir}/ChordAnalyzerHeadless.so")
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            add_test(NAME ChordAnalyzerHeadlessMetadata COMMAND "${Python3_EXECUTABLE}"
                "${source_dir}/../tests/test_chord_headless_bundle.py" "${bundle_dir}")
        endif()
    endif()
endfunction()
