# PRE-35 offline tools. Pulled in by ../core/CMakeLists.txt; not a project of its
# own, because everything here links pre35_core and there is no reason to be able
# to build the harness without the thing it renders.
if(NOT TARGET pre35_core)
    message(FATAL_ERROR "plugins/pre-35/tools is a subdirectory of plugins/pre-35/core; "
                        "configure that instead (cmake -S plugins/pre-35/core -B <dir>)")
endif()

add_executable(pre35_render pre35_render.cpp)
target_link_libraries(pre35_render PRIVATE pre35_core pre35_warnings)
