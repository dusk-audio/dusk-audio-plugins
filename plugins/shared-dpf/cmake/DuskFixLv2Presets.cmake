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
if(NOT EXISTS "${PRESETS_FILE}")
    # Plugins without programs legitimately do not emit presets.ttl.
    return()
endif()

file(READ "${PRESETS_FILE}" _dusk_lv2_presets)
set(_dusk_bad_boundary "    ] .\n\n    [\n")
set(_dusk_fixed_boundary "    ] ,\n    [\n")
string(FIND "${_dusk_lv2_presets}" "${_dusk_bad_boundary}" _dusk_bad_index)

if(NOT _dusk_bad_index EQUAL -1)
    string(REPLACE
        "${_dusk_bad_boundary}"
        "${_dusk_fixed_boundary}"
        _dusk_lv2_presets
        "${_dusk_lv2_presets}")
    file(WRITE "${PRESETS_FILE}" "${_dusk_lv2_presets}")
    message(STATUS "Repaired non-terminal LV2 preset port-list boundary: ${PRESETS_FILE}")
endif()

# Refuse to package a file that still contains the known malformed boundary.
file(READ "${PRESETS_FILE}" _dusk_lv2_verified)
string(FIND "${_dusk_lv2_verified}" "${_dusk_bad_boundary}" _dusk_remaining)
if(NOT _dusk_remaining EQUAL -1)
    message(FATAL_ERROR "Malformed LV2 preset port-list boundary remains in ${PRESETS_FILE}")
endif()
