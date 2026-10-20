# Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
cmake_minimum_required(VERSION 3.15)
project(ChordAnalyzerHeadless LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
enable_testing()
add_subdirectory(../core core)
find_path(CHORD_ANALYZER_LV2_HEADERS lv2/core/lv2.h
    DOC "Directory containing the public LV2 SDK's lv2/ headers")
if(NOT CHORD_ANALYZER_LV2_HEADERS)
    message(FATAL_ERROR "Install the LV2 SDK or set CHORD_ANALYZER_LV2_HEADERS to its include directory")
endif()
include(ChordAnalyzerHeadless.cmake)
chord_add_headless_lv2("${CMAKE_CURRENT_SOURCE_DIR}" "${CHORD_ANALYZER_LV2_HEADERS}"
    "${CMAKE_CURRENT_BINARY_DIR}/bin/Chord Analyzer Headless.lv2")
