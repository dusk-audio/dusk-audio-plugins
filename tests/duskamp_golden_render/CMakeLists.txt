# Phase 0.1 — DuskAmp golden render generator.
# Standalone executable that links against DuskAmp's shared-code library and
# renders every factory preset through a synthetic DI to tests/golden_renders/.
#
# Enable with -DBUILD_DUSKAMP_TESTS=ON at configure time.

add_executable(duskamp_golden_render
    golden_render.cpp
)

# Link against the JUCE-generated shared-code library (created by
# juce_add_plugin in plugins/DuskAmp/CMakeLists.txt). This pulls in all
# of DuskAmp's source plus its transitive JUCE dependencies.
target_link_libraries(duskamp_golden_render
    PRIVATE
        DuskAmp
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_events
)

target_include_directories(duskamp_golden_render
    PRIVATE
        ${CMAKE_SOURCE_DIR}/tests/duskamp_test_common
)

# Same defines DuskAmp uses, so headers compile identically.
target_compile_definitions(duskamp_golden_render
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_MP3AUDIOFORMAT=0
)

# C++17 to match the rest of the tree.
target_compile_features(duskamp_golden_render PRIVATE cxx_std_17)

# Output to a predictable location.
set_target_properties(duskamp_golden_render PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/duskamp_golden_render"
)
