# Standalone numeric model and preservation proof; requires only a C++17 toolchain.
cmake_minimum_required(VERSION 3.15)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(ChordAnalyzerCore LANGUAGES CXX)
endif()

add_library(ChordAnalyzerCore STATIC ChordAnalyzerCore.cpp)
target_compile_features(ChordAnalyzerCore PUBLIC cxx_std_17)
target_include_directories(ChordAnalyzerCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(ChordAnalyzerCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Separate text layer: desktop/DAF UI consumers share one formatter, while the
# headless numeric wrapper keeps its dependency and real-time boundary intact.
add_library(ChordAnalyzerText STATIC ChordAnalyzerText.cpp)
target_link_libraries(ChordAnalyzerText PUBLIC ChordAnalyzerCore)
set_target_properties(ChordAnalyzerText PROPERTIES POSITION_INDEPENDENT_CODE ON)

find_package(Threads REQUIRED)
add_library(ChordAnalyzerRuntime STATIC ChordAnalyzerRuntime.cpp)
target_link_libraries(ChordAnalyzerRuntime PUBLIC ChordAnalyzerText Threads::Threads)
set_target_properties(ChordAnalyzerRuntime PROPERTIES POSITION_INDEPENDENT_CODE ON)

function(chord_add_native_midi_test source_root lv2_headers)
    add_executable(ChordAnalyzerMidiTest "${source_root}/tests/test_chord_midi.cpp")
    target_include_directories(ChordAnalyzerMidiTest PRIVATE "${lv2_headers}")
    target_link_libraries(ChordAnalyzerMidiTest PRIVATE ChordAnalyzerCore)
    add_test(NAME ChordAnalyzerMidi COMMAND ChordAnalyzerMidiTest)
endfunction()

if(NOT CMAKE_CROSSCOMPILING)
    enable_testing()
    add_executable(ChordAnalyzerCoreTest ../tests/test_chord_analyzer_core.cpp)
    target_link_libraries(ChordAnalyzerCoreTest PRIVATE ChordAnalyzerCore)
    add_test(NAME ChordAnalyzerCore COMMAND ChordAnalyzerCoreTest)

    add_executable(ChordAnalyzerTextTest ../tests/test_chord_analyzer_text.cpp)
    target_link_libraries(ChordAnalyzerTextTest PRIVATE ChordAnalyzerText)
    add_test(NAME ChordAnalyzerText COMMAND ChordAnalyzerTextTest)

    add_executable(ChordRecordingTest ../tests/test_chord_recording.cpp)
    target_link_libraries(ChordRecordingTest PRIVATE ChordAnalyzerText)
    add_test(NAME ChordRecording COMMAND ChordRecordingTest)

    add_executable(ChordHistoryTest ../tests/test_chord_history.cpp)
    target_link_libraries(ChordHistoryTest PRIVATE ChordAnalyzerText)
    add_test(NAME ChordHistory COMMAND ChordHistoryTest)

    add_executable(ChordMidiNoteStateTest ../tests/test_midi_note_state.cpp)
    target_link_libraries(ChordMidiNoteStateTest PRIVATE ChordAnalyzerCore)
    add_test(NAME ChordMidiNoteState COMMAND ChordMidiNoteStateTest)

    add_executable(ChordNoteSnapshotTest ../tests/test_note_snapshot.cpp)
    target_link_libraries(ChordNoteSnapshotTest PRIVATE ChordAnalyzerCore Threads::Threads)
    add_test(NAME ChordNoteSnapshot COMMAND ChordNoteSnapshotTest)
    set_tests_properties(ChordNoteSnapshot PROPERTIES TIMEOUT 20)

    # Optional standalone ABI proof uses only the public LV2 headers, allowing
    # native Mac/MSVC tests without configuring or linking JUCE.
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND CHORD_ANALYZER_LV2_TEST_HEADERS)
        chord_add_native_midi_test("${CMAKE_CURRENT_SOURCE_DIR}/.." "${CHORD_ANALYZER_LV2_TEST_HEADERS}")
    endif()
endif()
