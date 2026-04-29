# Phase 0.6 — DuskAmp CPU harness.
# Standalone executable measuring per-block wall-clock time across all factory
# presets. Outputs Markdown to stdout and JSON to a file.
#
# Enable with -DBUILD_DUSKAMP_TESTS=ON at configure time.

add_executable(duskamp_cpu_harness
    cpu_harness.cpp
)

target_link_libraries(duskamp_cpu_harness
    PRIVATE
        DuskAmp
        juce::juce_audio_basics
        juce::juce_events
)

target_include_directories(duskamp_cpu_harness
    PRIVATE
        ${CMAKE_SOURCE_DIR}/tests/duskamp_test_common
)

target_compile_definitions(duskamp_cpu_harness
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_MP3AUDIOFORMAT=0
)

target_compile_features(duskamp_cpu_harness PRIVATE cxx_std_17)

set_target_properties(duskamp_cpu_harness PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/duskamp_cpu_harness"
)
