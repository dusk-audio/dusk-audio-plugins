# Repair a DAF LV2 preset export edge case.
#
# DAF closes an lv2:port list whenever the *next* parameter is an output. If
# an output parameter is followed by more input parameters, the exporter emits
# a period before those later port entries and presets.ttl becomes invalid
# Turtle. Keep this build-local workaround until the equivalent exporter fix is
# available in the dusk-audio/DAF revision the builds use (.github/daf-ref).

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

# OMIT_SYMBOLS: comma-separated control ports a preset must not set. A port
# left out of a preset keeps the value it had when the host applies the preset.
if(DEFINED OMIT_SYMBOLS AND NOT OMIT_SYMBOLS STREQUAL "")
    string(REPLACE "," ";" _dusk_omit_symbols "${OMIT_SYMBOLS}")
    file(READ "${PRESETS_FILE}" _dusk_lv2_presets)
    string(REPLACE "\r\n" "\n" _dusk_lv2_presets "${_dusk_lv2_presets}")
    string(REPLACE "\r" "\n" _dusk_lv2_presets "${_dusk_lv2_presets}")
    foreach(_dusk_symbol IN LISTS _dusk_omit_symbols)
        set(_dusk_port_regex "\\[[^][]*lv2:symbol[ \t\n]+\"${_dusk_symbol}\"[^][]*\\]")
        # A port with another after it goes with its comma; the last port of a
        # list hands its full stop to the one before it.
        string(REGEX REPLACE "${_dusk_port_regex}[ \t\n]*,[ \t\n]*" ""
            _dusk_lv2_presets "${_dusk_lv2_presets}")
        string(REGEX REPLACE ",[ \t\n]*${_dusk_port_regex}[ \t\n]*\\." " ."
            _dusk_lv2_presets "${_dusk_lv2_presets}")
        string(FIND "${_dusk_lv2_presets}" "\"${_dusk_symbol}\"" _dusk_left)
        if(NOT _dusk_left EQUAL -1)
            message(FATAL_ERROR "Could not omit port ${_dusk_symbol} from ${PRESETS_FILE}")
        endif()
    endforeach()
    file(WRITE "${PRESETS_FILE}" "${_dusk_lv2_presets}")
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
