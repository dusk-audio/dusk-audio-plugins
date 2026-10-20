# Standalone numeric model and preservation proof; requires only a C++17 toolchain.
cmake_minimum_required(VERSION 3.15)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(ChordAnalyzerCore LANGUAGES CXX)
endif()

add_library(ChordAnalyzerCore STATIC ChordAnalyzerCore.cpp)
target_compile_features(ChordAnalyzerCore PUBLIC cxx_std_17)
target_include_directories(ChordAnalyzerCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(ChordAnalyzerCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_CROSSCOMPILING)
    enable_testing()
    add_executable(ChordAnalyzerCoreTest ../tests/test_chord_analyzer_core.cpp)
    target_link_libraries(ChordAnalyzerCoreTest PRIVATE ChordAnalyzerCore)
    add_test(NAME ChordAnalyzerCore COMMAND ChordAnalyzerCoreTest)
endif()
