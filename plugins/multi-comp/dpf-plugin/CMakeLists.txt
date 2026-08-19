cmake_minimum_required(VERSION 3.15)
project(MultiComp2DPF VERSION 0.1.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/../../shared-dpf/cmake/DuskDpfPlugin.cmake")
dpf_add_plugin(multi_comp_2
  TARGETS clap vst3 lv2 jack au
  # Keep MONOLITHIC: direct access is required for the CLAP meter bridge and
  # LV2 export otherwise declares a UI object that this Phase-2 shell has not got.
  MONOLITHIC
  FILES_DSP MultiCompPlugin.cpp ../core/MultiCompDSP.cpp)
target_include_directories(multi_comp_2 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../core" ${DUSK_DPF_INCLUDE_DIRS})
target_compile_definitions(multi_comp_2 PUBLIC MULTICOMP2_VERSION_MAJOR=${PROJECT_VERSION_MAJOR} MULTICOMP2_VERSION_MINOR=${PROJECT_VERSION_MINOR} MULTICOMP2_VERSION_PATCH=${PROJECT_VERSION_PATCH})
dusk_dpf_install_local(multi_comp_2 LV2_PROGRAMS)
