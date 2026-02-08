# PluginVersion.cmake
# Automatic version injection from git tags or environment variables
#
# This module provides automatic version numbering for plugins based on:
# 1. PLUGIN_VERSION_OVERRIDE environment variable (set by CI from git tag)
# 2. Git tag matching the plugin slug pattern (e.g., 4k-eq-v1.0.5)
# 3. Fallback to the version specified in CMakeLists.txt
#
# Usage in plugin CMakeLists.txt:
#   include(${CMAKE_SOURCE_DIR}/cmake/PluginVersion.cmake)
#   get_plugin_version("4k-eq" PLUGIN_VERSION)
#   project(FourKEQ VERSION ${PLUGIN_VERSION})

# Function to extract version from git tag for a specific plugin
# Arguments:
#   PLUGIN_SLUG - The plugin identifier used in tags (e.g., "4k-eq", "multi-comp")
#   OUTPUT_VAR  - Variable to store the extracted version
function(get_plugin_version PLUGIN_SLUG OUTPUT_VAR)
    # Priority 1: Check for version override from CI (most reliable for releases)
    # Note: AND NOT STREQUAL "" guard prevents empty env vars (e.g. from workflow_dispatch)
    # from short-circuiting the git tag detection in Priority 2
    if(DEFINED ENV{PLUGIN_VERSION_OVERRIDE} AND NOT "$ENV{PLUGIN_VERSION_OVERRIDE}" STREQUAL "")
        set(${OUTPUT_VAR} $ENV{PLUGIN_VERSION_OVERRIDE} PARENT_SCOPE)
        message(STATUS "${PLUGIN_SLUG}: Using CI version override: $ENV{PLUGIN_VERSION_OVERRIDE}")
        return()
    endif()

    # Priority 2: Try to get version from git tag
    find_package(Git QUIET)
    if(GIT_FOUND)
        # Get the most recent tag matching this plugin's pattern
        execute_process(
            COMMAND ${GIT_EXECUTABLE} tag -l "${PLUGIN_SLUG}-v*" --sort=-version:refname
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_TAGS
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE GIT_RESULT
        )

        if(GIT_RESULT EQUAL 0 AND GIT_TAGS)
            # Get the first (most recent) tag
            string(REGEX MATCH "^[^\n]+" LATEST_TAG "${GIT_TAGS}")

            if(LATEST_TAG)
                # Extract version number from tag (e.g., "4k-eq-v1.0.5" -> "1.0.5")
                string(REGEX REPLACE "^${PLUGIN_SLUG}-v" "" VERSION_FROM_TAG "${LATEST_TAG}")

                # Validate it looks like a version number
                if(VERSION_FROM_TAG MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+")
                    # Check if current HEAD is at this tag
                    execute_process(
                        COMMAND ${GIT_EXECUTABLE} describe --exact-match --tags HEAD
                        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                        OUTPUT_VARIABLE HEAD_TAG
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET
                        RESULT_VARIABLE DESCRIBE_RESULT
                    )

                    if(DESCRIBE_RESULT EQUAL 0 AND HEAD_TAG MATCHES "^${PLUGIN_SLUG}-v")
                        # HEAD is exactly at a tag for this plugin
                        string(REGEX REPLACE "^${PLUGIN_SLUG}-v" "" EXACT_VERSION "${HEAD_TAG}")
                        set(${OUTPUT_VAR} ${EXACT_VERSION} PARENT_SCOPE)
                        message(STATUS "${PLUGIN_SLUG}: Using exact git tag version: ${EXACT_VERSION}")
                        return()
                    else()
                        # Not at exact tag - use latest tag version for reference
                        message(STATUS "${PLUGIN_SLUG}: Latest tag is ${LATEST_TAG}, but HEAD is not at this tag")
                    endif()
                endif()
            endif()
        endif()
    endif()

    # Priority 3: Fallback - version must be provided by caller
    # This happens during local development when not on a tag
    if(DEFINED ${OUTPUT_VAR})
        message(STATUS "${PLUGIN_SLUG}: Using fallback version: ${${OUTPUT_VAR}}")
    else()
        message(STATUS "${PLUGIN_SLUG}: No version found - using default")
        set(${OUTPUT_VAR} "0.0.0" PARENT_SCOPE)
    endif()
endfunction()

# Function to set version compile definitions for a plugin target
# This makes the version available in C++ code
function(set_plugin_version_defines TARGET_NAME VERSION_STRING)
    # Parse version components
    string(REPLACE "." ";" VERSION_LIST ${VERSION_STRING})
    list(LENGTH VERSION_LIST VERSION_LIST_LENGTH)

    if(VERSION_LIST_LENGTH GREATER_EQUAL 1)
        list(GET VERSION_LIST 0 VERSION_MAJOR)
    else()
        set(VERSION_MAJOR 0)
    endif()

    if(VERSION_LIST_LENGTH GREATER_EQUAL 2)
        list(GET VERSION_LIST 1 VERSION_MINOR)
    else()
        set(VERSION_MINOR 0)
    endif()

    if(VERSION_LIST_LENGTH GREATER_EQUAL 3)
        list(GET VERSION_LIST 2 VERSION_PATCH)
    else()
        set(VERSION_PATCH 0)
    endif()

    # Add compile definitions
    target_compile_definitions(${TARGET_NAME} PRIVATE
        LUNA_PLUGIN_VERSION="${VERSION_STRING}"
        LUNA_PLUGIN_VERSION_MAJOR=${VERSION_MAJOR}
        LUNA_PLUGIN_VERSION_MINOR=${VERSION_MINOR}
        LUNA_PLUGIN_VERSION_PATCH=${VERSION_PATCH}
    )

    message(STATUS "${TARGET_NAME}: Version defines set to ${VERSION_STRING}")
endfunction()
