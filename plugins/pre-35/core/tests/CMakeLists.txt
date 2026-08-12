# PRE-35 core gates. Plain main() programs, no test framework: the core has no
# dependencies and its gates are not going to introduce one. Each returns 0 only
# if every check in it passed, and prints the measured number either way.
#
#     ctest --test-dir <build> --output-on-failure
#
# iron_test renders 6 s tones at 8x and measures them with a direct DFT, so it is
# the slow one (tens of seconds). That is the price of measuring THD the same way
# the Python reference does instead of a way that would be quicker to disagree.

set(PRE35_TESTS coeff_test chain_test iron_test rail_test robust_test)

foreach(test IN LISTS PRE35_TESTS)
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE pre35_core pre35_warnings)
    target_include_directories(${test} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME ${test} COMMAND ${test})
endforeach()

set_tests_properties(iron_test PROPERTIES TIMEOUT 900)
