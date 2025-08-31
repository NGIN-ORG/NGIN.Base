# based on https://gitlab.kitware.com/cmake/cmake/-/blob/master/Modules/GoogleTestAddTests.cmake
# This script runs a Boost.UT test executable to list available tests and
# registers each one as an individual CTest test.

set(script)
set(tests)
set(current_suite "")
set(case_suite_map "")

# Build a simple lookup from CASE_SUITE_MAP entries of the form "case=>suite"
if(DEFINED CASE_SUITE_MAP)
  foreach(_pair ${CASE_SUITE_MAP})
    if(_pair MATCHES "^(.+)=>(.+)$")
      # Store as a flat list: key1;val1;key2;val2;...
      list(APPEND case_suite_map "${CMAKE_MATCH_1}" "${CMAKE_MATCH_2}")
    endif()
  endforeach()
endif()

function(add_command NAME)
  set(_args "")
  foreach(_arg ${ARGN})
    if(_arg MATCHES "[^-./:a-zA-Z0-9_]")
      set(_args "${_args} [==[${_arg}]==]")
    else()
      set(_args "${_args} ${_arg}")
    endif()
  endforeach()
  set(script "${script}${NAME}(${_args})\n" PARENT_SCOPE)
endfunction()

# Run test executable to get list of available tests
if(NOT EXISTS "${TEST_EXECUTABLE}")
  message(FATAL_ERROR
    "Specified test executable '${TEST_EXECUTABLE}' does not exist"
  )
endif()
set(output "")
set(result 0)

# Prefer a verbose list that includes suites, then fall back
execute_process(
  COMMAND "${TEST_EXECUTABLE}" --list --use-colour no
  OUTPUT_VARIABLE output
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0 OR output STREQUAL "")
  execute_process(
    COMMAND "${TEST_EXECUTABLE}" --list-test-names-only --use-colour no
    OUTPUT_VARIABLE output
    RESULT_VARIABLE result
  )
endif()
if(NOT ${result} EQUAL 0)
  message(FATAL_ERROR
    "Error running test executable '${TEST_EXECUTABLE}':\n"
    "  Result: ${result}\n"
    "  Output: ${output}\n"
  )
endif()

string(REPLACE "\n" ";" output "${output}")

foreach(line ${output})
  string(STRIP "${line}" line_stripped)
  if(line_stripped STREQUAL "")
    continue()
  endif()
  # Capture suite headers (case-insensitive, single or double quotes)
  if(line_stripped MATCHES "^[Ss]uite ['\"]([^'\"]+)['\"]")
    set(current_suite "${CMAKE_MATCH_1}")
  else()
    # Extract test name from formats like "matching test: NAME" or "test: NAME"
    if(line_stripped MATCHES "^(matching[ ]+)?test: (.+)$")
      set(test "${CMAKE_MATCH_2}")
    else()
      # Fallback: treat the whole line as the test name (names-only mode)
      set(test "${line_stripped}")
    endif()
    # If we didn't detect suites but have a provided hint, use it
    if(NOT current_suite AND DEFINED SUITE_HINT AND NOT SUITE_HINT STREQUAL "")
      set(current_suite "${SUITE_HINT}")
    endif()
    # If still no suite, try mapping case name to suite from static analysis
    if(NOT current_suite AND case_suite_map)
      list(LENGTH case_suite_map _map_len)
      set(_i 0)
      while(_i LESS _map_len)
        list(GET case_suite_map ${_i} _k)
        math(EXPR _j "${_i}+1")
        list(GET case_suite_map ${_j} _v)
        if(_k STREQUAL "${test}")
          set(current_suite "${_v}")
          break()
        endif()
        math(EXPR _i "${_i}+2")
      endwhile()
    endif()
    if(current_suite)
      set(test_name "${current_suite}:${test}")
    else()
      set(test_name "${TEST_TARGET}:${test}")
    endif()
    add_command(add_test
            "${test_name}"
            "${TEST_EXECUTABLE}"
            "${test}"
            "--success"
            "--durations"
    )
    message(CONFIGURE_LOG "Discovered test: ${test_name}")
    list(APPEND tests "${test_name}")
  endif()
endforeach()

# Optional: expose list of discovered tests to caller via TEST_LIST variable
# Optional: expose list of discovered tests to caller via TEST_LIST variable
if(DEFINED TEST_LIST)
  add_command(set ${TEST_LIST} ${tests})
endif()

# Write CTest script
file(WRITE "${CTEST_FILE}" "${script}")
