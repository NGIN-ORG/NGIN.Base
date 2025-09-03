# based on https://gitlab.kitware.com/cmake/cmake/-/blob/master/Modules/GoogleTestAddTests.cmake
# This script runs a Boost.UT test executable to list available tests and
# registers each one as an individual CTest test.
#
# Expected inputs:
#   TEST_EXECUTABLE  - path to the test binary
#   TEST_TARGET      - logical target/name for ctest fallback prefix
#   CTEST_FILE       - output file where generated add_test() calls are written
# Optional inputs:
#   TEST_SOURCE      - path to the .cpp containing the suite<> declaration; used to auto-detect the suite name
#   SUITE_HINT       - explicit suite name hint (overrides auto-detection)
#   CASE_SUITE_MAP   - list like "CaseA=>SuiteX;CaseB=>SuiteY" to map individual cases
#
# Behavior:
#   - Prefer parsing `--list --use-colour no` (suites + tests).
#   - Fall back to `--list-test-names-only` if needed.
#   - If no suite header is seen (e.g., names-only mode), it uses SUITE_HINT.
#   - If SUITE_HINT is not set, it attempts to read TEST_SOURCE and extract suite<"Name">.
#   - Registers each discovered test as "<suite>:<case>" where possible.

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

# --- Auto-detect SUITE_HINT from TEST_SOURCE (if not explicitly provided) ---
if(NOT DEFINED SUITE_HINT OR SUITE_HINT STREQUAL "")
  if(DEFINED TEST_SOURCE AND EXISTS "${TEST_SOURCE}")
    file(READ "${TEST_SOURCE}" _src_text)
    # Try to match suite<"Name"> (double quotes)
    string(REGEX MATCH "suite<\\s*\"([^\"]+)\"\\s*>" _m "${_src_text}")
    if(_m)
      set(SUITE_HINT "${CMAKE_MATCH_1}")
    else()
      # Try suite<'Name'> (single quotes)
      string(REGEX MATCH "suite<\\s*'([^']+)'\\s*>" _m2 "${_src_text}")
      if(_m2)
        set(SUITE_HINT "${CMAKE_MATCH_1}")
      endif()
    endif()
  endif()
endif()

# Run test executable to get list of available tests
if(NOT EXISTS "${TEST_EXECUTABLE}")
  message(FATAL_ERROR "Specified test executable '${TEST_EXECUTABLE}' does not exist")
endif()

set(output "")
set(result 0)
set(names_only_mode OFF)

# Prefer verbose list (includes suites)
execute_process(
  COMMAND "${TEST_EXECUTABLE}" --list --use-colour no
  OUTPUT_VARIABLE output
  RESULT_VARIABLE result
)

# Fall back to names-only if needed
if(NOT result EQUAL 0 OR output STREQUAL "")
  execute_process(
    COMMAND "${TEST_EXECUTABLE}" --list-test-names-only --use-colour no
    OUTPUT_VARIABLE output
    RESULT_VARIABLE result
  )
  set(names_only_mode ON)
endif()

if(NOT ${result} EQUAL 0)
  message(FATAL_ERROR
    "Error running test executable '${TEST_EXECUTABLE}':\n"
    "  Result: ${result}\n"
    "  Output: ${output}\n"
  )
endif()

string(REPLACE "\n" ";" output "${output}")
set(current_suite "")

foreach(line ${output})
  string(STRIP "${line}" line_stripped)
  if(line_stripped STREQUAL "")
    continue()
  endif()

  if(NOT names_only_mode)
    # Suite header: case-insensitive, optional colon, optional quotes, optional trailing colon
    # Matches: "suite Foo", "Suite: Foo", "suite 'Foo'", "Suite 'Foo':"
    if(line_stripped MATCHES "^[Ss]uite[: ]+[\"']?([^\"']+)[\"']?[:]?$")
      string(STRIP "${CMAKE_MATCH_1}" current_suite)
      continue() # move on to the next line
    endif()

    # Real test lines: "test: NAME" or "matching test: NAME"
    if(line_stripped MATCHES "^(matching[ ]+)?test:[ ]+(.+)$")
      set(test "${CMAKE_MATCH_2}")
    else()
      # Ignore non-test lines in verbose mode (e.g., "all tests passed" summaries)
      continue()
    endif()
  else()
    # names-only mode: every non-empty line is a test name
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

  # Compose the registered test name
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
endforeach()

# Optional: expose list of discovered tests to caller via TEST_LIST variable
if(DEFINED TEST_LIST)
  add_command(set ${TEST_LIST} ${tests})
endif()

# Write CTest script
file(WRITE "${CTEST_FILE}" "${script}")
