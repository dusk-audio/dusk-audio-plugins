# DuskDpfPlugin.cmake — shared DPF wiring for Dusk Audio DPF plugins.
#
# include() this from a plugin's CMakeLists after project(). It locates the
# DPF and DPF-Widgets checkouts (siblings of the repo by default, overridable
# with -DDPF_PATH=... / -DDPFWIDGETS_PATH=...), adds DPF as a subdirectory, and
# exposes DUSK_DPF_UI_SOURCES (the DearImGui wrapper) and DUSK_DPF_INCLUDE_DIRS
# (shared-dpf dsp/ui + DPF-Widgets opengl) for the caller to attach. Plugins
# still call dpf_add_plugin themselves so per-plugin TARGETS/FILES stay local.

if(NOT DEFINED DUSK_SHARED_DPF_DIR)
    set(DUSK_SHARED_DPF_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
endif()

# repo root is three levels up from plugins/<name>/dpf-plugin
set(_dusk_repo_root "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
set(DPF_PATH        "${_dusk_repo_root}/../DPF"         CACHE PATH "Path to DISTRHO Plugin Framework")
set(DPFWIDGETS_PATH "${_dusk_repo_root}/../DPF-Widgets" CACHE PATH "Path to DPF-Widgets (Dear ImGui wrapper)")

if(NOT EXISTS "${DPF_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "DPF not found at ${DPF_PATH} — clone https://github.com/DISTRHO/DPF or pass -DDPF_PATH=...")
endif()
if(NOT EXISTS "${DPFWIDGETS_PATH}/opengl/DearImGui.cpp")
    message(FATAL_ERROR "DPF-Widgets not found at ${DPFWIDGETS_PATH} — clone https://github.com/DISTRHO/DPF-Widgets or pass -DDPFWIDGETS_PATH=...")
endif()

if(NOT TARGET dpf)
    add_subdirectory("${DPF_PATH}" dpf EXCLUDE_FROM_ALL)
endif()

set(DUSK_DPF_UI_SOURCES  "${DPFWIDGETS_PATH}/opengl/DearImGui.cpp")
set(DUSK_DPF_INCLUDE_DIRS
    "${DUSK_SHARED_DPF_DIR}"
    "${DUSK_SHARED_DPF_DIR}/dsp"
    "${DUSK_SHARED_DPF_DIR}/ui"
    "${DPFWIDGETS_PATH}/opengl")
