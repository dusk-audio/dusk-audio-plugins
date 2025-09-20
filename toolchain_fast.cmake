# Fast build toolchain configuration

# Use ccache if available
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCACHE_PROGRAM}")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK "${CCACHE_PROGRAM}")
endif()

# Use ninja generator if available
find_program(NINJA_PROGRAM ninja)
if(NINJA_PROGRAM AND NOT DEFINED CMAKE_GENERATOR)
    set(CMAKE_GENERATOR "Ninja" CACHE INTERNAL "")
endif()

# Use mold linker if available for faster linking
find_program(MOLD_LINKER mold)
if(MOLD_LINKER)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=mold")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=mold")
endif()

# Optimization flags - Conservative settings to avoid linking issues
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG -pipe" CACHE STRING "")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG -pipe" CACHE STRING "")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -pipe" CACHE STRING "")
set(CMAKE_C_FLAGS_DEBUG "-O0 -g -pipe" CACHE STRING "")

# Disable problematic optimizations that can cause linking issues
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-lto" CACHE STRING "" FORCE)

# Disable unnecessary features for faster builds
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "")
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "")
