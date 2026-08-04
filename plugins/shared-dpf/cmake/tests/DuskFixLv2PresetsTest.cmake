foreach(_required FIX_SCRIPT FIXTURE SERDI_EXECUTABLE TEST_ROOT)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
file(READ "${FIXTURE}" _fixture_lf)
string(REPLACE "    ] .\n\n    [\n" "] .\t[\n" _fixture_compact "${_fixture_lf}")
string(REPLACE "\n" "\r\n" _fixture_crlf "${_fixture_lf}")

function(_dusk_test_variant _name _content_variable)
    set(_file "${TEST_ROOT}/${_name}.ttl")
    file(WRITE "${_file}" "${${_content_variable}}")

    execute_process(
        COMMAND "${SERDI_EXECUTABLE}" -q -i turtle -o turtle "${_file}"
        RESULT_VARIABLE _malformed_serdi_result
        OUTPUT_QUIET ERROR_QUIET)
    if(_malformed_serdi_result EQUAL 0)
        message(FATAL_ERROR "serdi unexpectedly accepted malformed ${_name} fixture")
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DPRESETS_FILE=${_file}"
            -DEXPECT_PRESETS=ON
            -DDUSK_LV2_REPAIR=OFF
            -P "${FIX_SCRIPT}"
        RESULT_VARIABLE _postcheck_result
        OUTPUT_QUIET ERROR_VARIABLE _postcheck_error)
    if(_postcheck_result EQUAL 0
       OR NOT _postcheck_error MATCHES "Malformed LV2 preset port-list boundary")
        message(FATAL_ERROR
            "post-check did not reject malformed ${_name} fixture: ${_postcheck_error}")
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DPRESETS_FILE=${_file}"
            -DEXPECT_PRESETS=ON
            -P "${FIX_SCRIPT}"
        RESULT_VARIABLE _repair_result
        OUTPUT_VARIABLE _repair_output ERROR_VARIABLE _repair_error)
    if(NOT _repair_result EQUAL 0)
        message(FATAL_ERROR
            "repair failed for ${_name} fixture: ${_repair_output}${_repair_error}")
    endif()

    execute_process(
        COMMAND "${SERDI_EXECUTABLE}" -q -i turtle -o turtle "${_file}"
        RESULT_VARIABLE _repaired_serdi_result
        OUTPUT_QUIET ERROR_VARIABLE _serdi_error)
    if(NOT _repaired_serdi_result EQUAL 0)
        message(FATAL_ERROR
            "serdi rejected repaired ${_name} fixture: ${_serdi_error}")
    endif()
endfunction()

_dusk_test_variant(lf _fixture_lf)
_dusk_test_variant(compact _fixture_compact)
_dusk_test_variant(crlf _fixture_crlf)

set(_missing "${TEST_ROOT}/missing-presets.ttl")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DPRESETS_FILE=${_missing}"
        -DEXPECT_PRESETS=OFF
        -P "${FIX_SCRIPT}"
    RESULT_VARIABLE _optional_missing_result
    OUTPUT_QUIET ERROR_QUIET)
if(NOT _optional_missing_result EQUAL 0)
    message(FATAL_ERROR "target without programs rejected a missing presets.ttl")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DPRESETS_FILE=${_missing}"
        -DEXPECT_PRESETS=ON
        -P "${FIX_SCRIPT}"
    RESULT_VARIABLE _required_missing_result
    OUTPUT_QUIET ERROR_VARIABLE _required_missing_error)
if(_required_missing_result EQUAL 0
   OR NOT _required_missing_error MATCHES "provides programs")
    message(FATAL_ERROR "target with programs accepted a missing presets.ttl")
endif()

message(STATUS "LV2 preset repair regression variants passed")
