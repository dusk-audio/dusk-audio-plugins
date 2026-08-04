# Repair a DPF LV2 preset export edge case.
#
# DPF closes an lv2:port list whenever the *next* parameter is an output. If
# an output parameter is followed by more input parameters, the exporter emits
# a period before those later port entries and presets.ttl becomes invalid
# Turtle. Keep this build-local workaround until the equivalent exporter fix is
# available in the pinned dusk-audio/DPF revision.

if(NOT DEFINED PRESETS_FILE OR PRESETS_FILE STREQUAL "")
    message(FATAL_ERROR "PRESETS_FILE is required")
endif()
if(NOT DEFINED EXPECT_PRESETS)
    message(FATAL_ERROR "EXPECT_PRESETS is required")
endif()
if(NOT EXISTS "${PRESETS_FILE}")
    if(EXPECT_PRESETS)
        message(FATAL_ERROR
            "${PRESETS_FILE} is missing for an LV2 target that provides programs")
    else()
        # Plugins without host programs legitimately do not emit presets.ttl.
        return()
    endif()
endif()

file(READ "${PRESETS_FILE}" _dusk_lv2_presets)
string(REPLACE "\r\n" "\n" _dusk_lv2_presets "${_dusk_lv2_presets}")
string(REPLACE "\r" "\n" _dusk_lv2_presets "${_dusk_lv2_presets}")

# Token-level form of the invalid non-terminal list boundary. Correct preset
# resources end with `] .` followed by the next preset URI; only the exporter
# bug produces `] .` followed by another anonymous port node (`[`). Match the
# tokens independently of indentation, blank lines, or line endings.
set(_dusk_bad_boundary_regex "\\][ \t\n]*\\.[ \t\n]*\\[")
string(REGEX MATCH "${_dusk_bad_boundary_regex}" _dusk_bad_boundary
    "${_dusk_lv2_presets}")

if(NOT _dusk_bad_boundary STREQUAL ""
   AND (NOT DEFINED DUSK_LV2_REPAIR OR DUSK_LV2_REPAIR))
    string(REGEX REPLACE
        "${_dusk_bad_boundary_regex}"
        "] ,\n    ["
        _dusk_lv2_presets
        "${_dusk_lv2_presets}")
    file(WRITE "${PRESETS_FILE}" "${_dusk_lv2_presets}")
    message(STATUS "Repaired non-terminal LV2 preset port-list boundary: ${PRESETS_FILE}")
endif()

# Refuse to package a file that still contains the known malformed boundary.
file(READ "${PRESETS_FILE}" _dusk_lv2_verified)
string(REPLACE "\r\n" "\n" _dusk_lv2_verified "${_dusk_lv2_verified}")
string(REPLACE "\r" "\n" _dusk_lv2_verified "${_dusk_lv2_verified}")
string(REGEX MATCH "${_dusk_bad_boundary_regex}" _dusk_remaining
    "${_dusk_lv2_verified}")
if(NOT _dusk_remaining STREQUAL "")
    message(FATAL_ERROR "Malformed LV2 preset port-list boundary remains in ${PRESETS_FILE}")
endif()
