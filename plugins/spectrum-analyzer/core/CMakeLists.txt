# Spectrum Analyzer framework-free core and its gates (GH #184, migration plan
# step 1). Needs only a C++17 toolchain: no JUCE, no DAF. Standalone:
#   cmake -S plugins/spectrum-analyzer/core -B build-core
#   cmake --build build-core && ctest --test-dir build-core --output-on-failure
# A shell build may add_subdirectory() it instead. The JUCE plugin build keeps
# its own copies of the golden and ring executables and does not include this.
cmake_minimum_required(VERSION 3.15)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(SpectrumAnalyzerCore LANGUAGES CXX)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Release)
    endif()
endif()

set(SPECTRUM_SHARED_DSP "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp")

# FFTProcessor reaches shared-daf/dsp/DuskFft.hpp by relative include.
add_library(SpectrumAnalyzerCore STATIC FFTProcessor.cpp LUFSMeter.cpp)
target_compile_features(SpectrumAnalyzerCore PUBLIC cxx_std_17)
target_include_directories(SpectrumAnalyzerCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(SpectrumAnalyzerCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_CROSSCOMPILING)
    enable_testing()
    find_package(Threads REQUIRED)

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        set(SPECTRUM_TEST_WARNINGS -Wall -Wextra)
    elseif(MSVC)
        set(SPECTRUM_TEST_WARNINGS /W4)
    endif()

    # Existing golden calibration suite (meters and FFT at 44.1/48/96 kHz).
    add_executable(SpectrumCoreGoldenTest tests/test_meters_golden.cpp)
    target_link_libraries(SpectrumCoreGoldenTest PRIVATE SpectrumAnalyzerCore)
    add_test(NAME SpectrumCoreGolden COMMAND SpectrumCoreGoldenTest)

    # Existing SpectrumRing writer/reader stress: 10 s, fails on a torn read.
    add_executable(SpectrumCoreRingStressTest tests/ring_tsan.cpp)
    target_link_libraries(SpectrumCoreRingStressTest PRIVATE Threads::Threads)
    add_test(NAME SpectrumCoreRingStress COMMAND SpectrumCoreRingStressTest)
    set_tests_properties(SpectrumCoreRingStress PROPERTIES TIMEOUT 60)

    # Descriptor table against the plan's frozen table and the JUCE adapter's
    # declarations, both parsed from source.
    add_executable(SpectrumCoreParamsTest tests/test_params_contract.cpp)
    target_link_libraries(SpectrumCoreParamsTest PRIVATE SpectrumAnalyzerCore)
    target_compile_options(SpectrumCoreParamsTest PRIVATE ${SPECTRUM_TEST_WARNINGS})
    add_test(NAME SpectrumCoreParams COMMAND SpectrumCoreParamsTest
        "${CMAKE_CURRENT_SOURCE_DIR}/../docs/DAF_MIGRATION.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Source/PluginProcessor.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/../Source/PluginProcessor.cpp")

    # Prepare/reset/re-prepare behaviour, frozen meter readings and spectrum
    # digest, display laws, loudness gates, the gated-history cap, and the
    # tripwire on the adapter bodies the test transcribes.
    add_executable(SpectrumCoreCaptureTest tests/test_core_capture.cpp tests/CaptureStimulus.cpp)
    target_include_directories(SpectrumCoreCaptureTest PRIVATE "${SPECTRUM_SHARED_DSP}")
    target_link_libraries(SpectrumCoreCaptureTest PRIVATE SpectrumAnalyzerCore)
    target_compile_options(SpectrumCoreCaptureTest PRIVATE ${SPECTRUM_TEST_WARNINGS})
    # The stimulus must be bit-identical in every build, including one that
    # contracts the analyzer under test, so its translation unit never fuses
    # multiply-add (MSVC's default /fp:precise does not contract).
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        set_source_files_properties(tests/CaptureStimulus.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
    endif()
    add_test(NAME SpectrumCoreCapture COMMAND SpectrumCoreCaptureTest
        "${CMAKE_CURRENT_SOURCE_DIR}/../Source/PluginProcessor.cpp")
    set_tests_properties(SpectrumCoreCapture PROPERTIES TIMEOUT 300)

    # Opt-in ThreadSanitizer run of the ring stress harness. It checks the
    # ring's index atomicity, not the ordering of its sample copies: on Apple
    # clang 21 TSan does not see the ring's memcpy (a ring with relaxed
    # publishes passes; element-wise copies and non-atomic indices are
    # reported), and on GCC 11.4 aarch64 a relaxed ring also passes, cause not
    # established. TSan exits non-zero on any report, independently of the
    # harness's torn-read verdict. On aarch64 Linux, GCC 11's libtsan
    # re-executes itself with ASLR off through personality(), which the default
    # container seccomp profile refuses.
    option(SPECTRUM_CORE_TSAN "Also build and register the ThreadSanitizer run of the ring stress harness" OFF)
    if(SPECTRUM_CORE_TSAN)
        if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            message(FATAL_ERROR "SPECTRUM_CORE_TSAN needs Clang or GCC (-fsanitize=thread)")
        endif()
        add_executable(SpectrumCoreRingTsanTest tests/ring_tsan.cpp)
        target_compile_options(SpectrumCoreRingTsanTest PRIVATE -fsanitize=thread -O1 -g)
        target_link_options(SpectrumCoreRingTsanTest PRIVATE -fsanitize=thread)
        target_link_libraries(SpectrumCoreRingTsanTest PRIVATE Threads::Threads)
        add_test(NAME SpectrumCoreRingTsan COMMAND SpectrumCoreRingTsanTest)
        set_tests_properties(SpectrumCoreRingTsan PROPERTIES TIMEOUT 120
            ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1")
    endif()
endif()
