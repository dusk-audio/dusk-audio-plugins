cmake_minimum_required(VERSION 3.15)
project(MultiComp2DAF VERSION 0.1.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/cmake/DuskDafPlugin.cmake")
# Bit-identical DSP on every platform: no fused multiply-add. Clang fuses
# a*b+c within an expression by default and GCC on arm64 across statements,
# while x86-64 without FMA fuses nothing, so the same source rendered audio
# that differed by ~70 float ulps between macOS and Linux and broke the
# captured witnesses there. MSVC's default /fp:precise already does not
# contract. Applies to the plugin and every test target below.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  add_compile_options(-ffp-contract=off)
endif()
daf_add_plugin(multi-comp-2
  TARGETS clap vst3 lv2 jack au
  # Keep MONOLITHIC: the UI uses the same-process meter bridge and LV2 must
  # advertise the embedded UI alongside the DSP binary.
  MONOLITHIC
  UI_TYPE opengl
  FILES_DSP MultiCompPlugin.cpp ../core/MultiCompDSP.cpp
  FILES_UI MultiCompUI.cpp ${DUSK_DAF_UI_SOURCES})
target_include_directories(multi-comp-2 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../core" ${DUSK_DAF_INCLUDE_DIRS})
target_compile_definitions(multi-comp-2 PUBLIC MULTICOMP2_VERSION_MAJOR=${PROJECT_VERSION_MAJOR} MULTICOMP2_VERSION_MINOR=${PROJECT_VERSION_MINOR} MULTICOMP2_VERSION_PATCH=${PROJECT_VERSION_PATCH})
if(APPLE)
  # DAF's generic AU path lookup can resolve the remote host executable in
  # AUHostingService. Resolve from code linked into this component instead.
  target_sources(multi-comp-2-au PRIVATE MultiCompAUComponentBundlePath.mm)
endif()
dusk_daf_install_local(multi-comp-2 LV2_PROGRAMS)

if(NOT CMAKE_CROSSCOMPILING)
  enable_testing()
  add_executable(MultiCompCoreTest
    ../core/tests/MultiCompCoreTests.cpp
    ../core/MultiCompDSP.cpp)
  target_include_directories(MultiCompCoreTest PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../core"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp")
  target_compile_features(MultiCompCoreTest PRIVATE cxx_std_17)
  add_test(NAME MultiCompCore COMMAND MultiCompCoreTest)

  add_executable(MultiCompBusControlTest ../core/tests/MultiCompBusControlTests.cpp ../core/MultiCompDSP.cpp)
  target_include_directories(MultiCompBusControlTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../core" "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp")
  target_compile_features(MultiCompBusControlTest PRIVATE cxx_std_17)
  add_test(NAME MultiCompBusControl COMMAND MultiCompBusControlTest)

  add_executable(MultiCompBusDynamicsTest ../core/tests/MultiCompBusDynamicsTests.cpp ../core/MultiCompDSP.cpp)
  target_include_directories(MultiCompBusDynamicsTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../core" "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp")
  target_compile_features(MultiCompBusDynamicsTest PRIVATE cxx_std_17)
  add_test(NAME MultiCompBusDynamics COMMAND MultiCompBusDynamicsTest)

  add_executable(MultiCompBusFrequencyTest ../core/tests/MultiCompBusFrequencyTests.cpp ../core/MultiCompDSP.cpp)
  target_include_directories(MultiCompBusFrequencyTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../core" "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp")
  target_compile_features(MultiCompBusFrequencyTest PRIVATE cxx_std_17)
  add_test(NAME MultiCompBusFrequency COMMAND MultiCompBusFrequencyTest)

  add_executable(MultiCompBusProgrammeTest ../core/tests/MultiCompBusProgrammeTests.cpp ../core/MultiCompDSP.cpp)
  target_include_directories(MultiCompBusProgrammeTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../core" "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp")
  target_compile_features(MultiCompBusProgrammeTest PRIVATE cxx_std_17)
  add_test(NAME MultiCompBusProgramme COMMAND MultiCompBusProgrammeTest)

  add_executable(MultiCompBusMeterTest ../core/tests/MultiCompBusMeterTests.cpp ../core/MultiCompDSP.cpp)
  target_include_directories(MultiCompBusMeterTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../core" "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp")
  target_compile_features(MultiCompBusMeterTest PRIVATE cxx_std_17)
  add_test(NAME MultiCompBusMeter COMMAND MultiCompBusMeterTest)

  add_executable(MultiCompBusCompletionTest ../core/tests/MultiCompBusCompletionTests.cpp ../core/MultiCompDSP.cpp)
  target_include_directories(MultiCompBusCompletionTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../core" "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp")
  target_compile_features(MultiCompBusCompletionTest PRIVATE cxx_std_17)
  add_test(NAME MultiCompBusCompletion COMMAND MultiCompBusCompletionTest)

  # One executable, two independently reported gates: the dry/wet mix and the
  # compressed output ceiling share the native programme and scoring helpers.
  # Pins the known, unfixed BUS drive-law divergence so it cannot grow silently.
  # This is a witness, not a parity gate: see the header of the test source.
  add_executable(MultiCompBusWitnessTest ../core/tests/MultiCompBusWitnessTests.cpp ../core/MultiCompDSP.cpp)
  target_include_directories(MultiCompBusWitnessTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../core" "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp")
  target_compile_features(MultiCompBusWitnessTest PRIVATE cxx_std_17)
  add_test(NAME MultiCompBusWitness COMMAND MultiCompBusWitnessTest)

  add_executable(MultiCompBusOutputTest ../core/tests/MultiCompBusOutputTests.cpp ../core/MultiCompDSP.cpp)
  target_include_directories(MultiCompBusOutputTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../core" "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp")
  target_compile_features(MultiCompBusOutputTest PRIVATE cxx_std_17)
  add_test(NAME MultiCompBusMix COMMAND MultiCompBusOutputTest --mix)
  add_test(NAME MultiCompBusCeiling COMMAND MultiCompBusOutputTest --ceiling)

  # One inf/NaN sample, or a finite burst that overflows a stage, must not latch:
  # every mode stays finite and recovers, from the main input and the sidechain.
  add_executable(MultiCompNonFiniteTest ../core/tests/MultiCompNonFiniteTests.cpp ../core/MultiCompDSP.cpp)
  target_include_directories(MultiCompNonFiniteTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../core" "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp")
  target_compile_features(MultiCompNonFiniteTest PRIVATE cxx_std_17)
  add_test(NAME MultiCompNonFinite COMMAND MultiCompNonFiniteTest)

  add_executable(MultiCompPluginLayerTest
    MultiCompPluginLayerTests.cpp)
  target_include_directories(MultiCompPluginLayerTest PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../core"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/dsp")
  target_compile_features(MultiCompPluginLayerTest PRIVATE cxx_std_17)
  add_test(NAME MultiCompPluginLayer COMMAND MultiCompPluginLayerTest)

  add_executable(MultiCompVintageControlTest MultiCompVintageControlTests.cpp)
  target_include_directories(MultiCompVintageControlTest PRIVATE ${DUSK_DAF_INCLUDE_DIRS})
  add_test(NAME MultiCompVintageControls COMMAND MultiCompVintageControlTest)

  add_executable(MultiCompMeterRenderTest MultiCompMeterRenderTests.cpp
    "${DAFWIDGETS_PATH}/imgui/DearImGui/imgui.cpp"
    "${DAFWIDGETS_PATH}/imgui/DearImGui/imgui_draw.cpp"
    "${DAFWIDGETS_PATH}/imgui/DearImGui/imgui_tables.cpp"
    "${DAFWIDGETS_PATH}/imgui/DearImGui/imgui_widgets.cpp")
  target_include_directories(MultiCompMeterRenderTest PRIVATE ${DUSK_DAF_INCLUDE_DIRS})
  add_test(NAME MultiCompMeterRender COMMAND MultiCompMeterRenderTest)

  if(APPLE)
    add_executable(MultiCompAUTest MultiCompAUTests.cpp)
    target_compile_features(MultiCompAUTest PRIVATE cxx_std_17)
    target_compile_definitions(MultiCompAUTest PRIVATE
      MULTICOMP_AU_BINARY="$<TARGET_FILE:multi-comp-2-au>")
    target_link_libraries(MultiCompAUTest PRIVATE
      "-framework AudioToolbox"
      "-framework AudioUnit")
    add_dependencies(MultiCompAUTest multi-comp-2-au)
    add_test(NAME MultiCompAU COMMAND MultiCompAUTest)
  endif()

  add_executable(DuskUserPresetStoreTest
    ../../shared-daf/ui/tests/DuskUserPresetStoreTest.cpp)
  target_include_directories(DuskUserPresetStoreTest PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../shared-daf/ui")
  target_compile_features(DuskUserPresetStoreTest PRIVATE cxx_std_17)
  add_test(NAME DuskUserPresetStore COMMAND DuskUserPresetStoreTest)
endif()

# Fleet gates. Multi-Comp 2 had none of these: nothing opened its editor. The
# drag gate exists because a DAF-Widgets bump broke dragging in every plugin's
# editor and shipped through four green releases, so a plugin outside it is
# exactly the one that would ship broken.
#
# The output-parameter gates arm Peak Reduction first. Multi-Comp 2's five GR
# meters correctly report nothing at the default preset, where
# opto_peak_reduction is 0 and the Opto cell does not compress, so a run that
# only feeds noise cannot tell working meters from dead ones.
dusk_daf_add_output_param_tests(multi-comp-2 "Peak Reduction=0.8")
dusk_daf_add_ui_drag_test(multi-comp-2 287 237)   # Opto GAIN knob (default face)
dusk_daf_add_resize_test(multi-comp-2)
