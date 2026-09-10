# Multi-Synth core offline validation harness (framework-free; no JUCE/DAF).
cmake_minimum_required(VERSION 3.15)
project(multisynth_core_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CORE_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(SHARED_DSP ${CMAKE_CURRENT_SOURCE_DIR}/../../../shared-daf/dsp)
set(DAF_PLUGIN ${CMAKE_CURRENT_SOURCE_DIR}/../../daf-plugin)

add_executable(render_test
    render_test.cpp
    ${CORE_DIR}/MultiSynthDSP.cpp)

# DAF_PLUGIN is on the include path for MultiSynthParams.hpp only (the static
# factory-preset table behind progat=); render_test still links the core alone.
target_include_directories(render_test PRIVATE ${CORE_DIR} ${SHARED_DSP} ${DAF_PLUGIN})
target_compile_options(render_test PRIVATE -O2 -Wall -Wextra)

# preset_render — includes the static factory-preset table (MultiSynthParams.hpp)
# from the DAF shell plus the core DSP; renders mode-appropriate performances for
# the Phase 5 preset re-voice + audit (no plugin hosting / MIDI injection).
add_executable(preset_render
    presets/preset_render.cpp
    ${CORE_DIR}/MultiSynthDSP.cpp)

target_include_directories(preset_render PRIVATE ${CORE_DIR} ${SHARED_DSP} ${DAF_PLUGIN})
target_compile_options(preset_render PRIVATE -O2 -Wall -Wextra)

# cpu_bench — offline worst-case CPU profiler (Phase 5 QA). Times the
# processBlock loop only; reports engine cost as %% of real time at 48 kHz.
add_executable(cpu_bench
    cpu_bench.cpp
    ${CORE_DIR}/MultiSynthDSP.cpp)

target_include_directories(cpu_bench PRIVATE ${CORE_DIR} ${SHARED_DSP})
# cpu_bench must compile like the shipping plugin (-O3 from Release) — a stray
# -O2 here already produced one wrong perf conclusion; do not add one back.
target_compile_options(cpu_bench PRIVATE -Wall -Wextra)

# user_preset_test — unit gate for daf-plugin/UserPresetStore.hpp (file-based user
# preset library). Header-only; pulls MultiSynthParams.hpp for the symbol table.
add_executable(user_preset_test user_preset_test.cpp)
target_include_directories(user_preset_test PRIVATE ${DAF_PLUGIN})
target_compile_options(user_preset_test PRIVATE -O2 -Wall -Wextra)

# lv2_smoke — LV2 host-integration smoke test (daf-plugin/tools/lv2_smoke.c).
# Unlike every other target here it hosts the BUILT LV2 bundle through lilv (the
# library jalv uses) rather than linking the core: instantiate + activate + run,
# oversampling-vs-reported-latency sweep, MIDI note-on, MIDI program change.
#
# OPTIONAL by design. lilv is a system dependency the rest of the suite does not
# need, so a machine without lilv-dev must still configure and run everything
# else; when it is missing the target is simply not created and run_all.sh
# prints a SKIP. It is C, so the C language is enabled only on this path.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LILV QUIET lilv-0)
endif()

if(LILV_FOUND)
    enable_language(C)
    add_executable(lv2_smoke ${DAF_PLUGIN}/tools/lv2_smoke.c)
    target_include_directories(lv2_smoke PRIVATE ${LILV_INCLUDE_DIRS})
    # LINK_LIBRARIES carries absolute paths, so no target_link_directories search
    # path is needed and the linker cannot pick up a same-named library from an
    # unrelated directory.
    target_link_libraries(lv2_smoke PRIVATE ${LILV_LINK_LIBRARIES} m)
    target_compile_options(lv2_smoke PRIVATE -O2 -Wall -Wextra ${LILV_CFLAGS_OTHER})
    message(STATUS "lv2_smoke: building against lilv-0 ${LILV_VERSION}")
else()
    message(STATUS "lv2_smoke: lilv-0 not found via pkg-config -- the LV2 host "
                   "smoke test will be SKIPPED (apt install lilv-utils liblilv-dev)")
endif()
