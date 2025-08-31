# Lightweight Boost.UT test discovery integration for CTest.
# Defines function discover_boost_ut_test(TARGET) that adds a post-build step
# to enumerate tests from the executable and registers them individually.

set(_UT_DISCOVER_TESTS_SCRIPT
  ${CMAKE_CURRENT_LIST_DIR}/BoostUTAddTests.cmake
)

function (discover_boost_ut_test TARGET)
  set(ctest_file_base "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}")
  set(ctest_include_file "${ctest_file_base}_include.cmake")
  set(ctest_tests_file "${ctest_file_base}_tests.cmake")

  # Attempt to infer a single suite name from the target's sources.
  # If exactly one suite<"..."> appears across all non-main sources,
  # pass it as a hint to the discovery script so test names can include it.
  get_target_property(_target_sources ${TARGET} SOURCES)
  set(_suite_candidates)
  set(_case_suite_map_list)
  foreach(_src ${_target_sources})
    get_filename_component(_name_we "${_src}" NAME_WE)
    if(_name_we STREQUAL "main")
      continue()
    endif()
    if(EXISTS "${_src}")
      file(READ "${_src}" _src_content)
      # Detect suite declarations and test case names within this file
      string(REGEX MATCH "suite<\"[^\"]+\">" _suite_decl "${_src_content}")
      if(_suite_decl)
        string(REGEX REPLACE "^.*suite<\"([^\"]+)\">.*$" "\\1" _suite_name "${_suite_decl}")
        if(_suite_name)
          list(APPEND _suite_candidates "${_suite_name}")
          # Find all test names in this file and associate with the suite
          string(REGEX MATCHALL "\"[^\"]+\"_test" _test_matches "${_src_content}")
          foreach(_tm ${_test_matches})
            string(REGEX REPLACE "^\"|\"_test$" "" _test_name "${_tm}")
            if(_test_name)
              list(APPEND _case_suite_map_list "${_test_name}=>${_suite_name}")
            endif()
          endforeach()
        endif()
      endif()
    endif()
  endforeach()
  if(_suite_candidates)
    list(REMOVE_DUPLICATES _suite_candidates)
  endif()

  file(WRITE "${ctest_include_file}"
      "if(EXISTS \"${ctest_tests_file}\")\n"
      "  include(\"${ctest_tests_file}\")\n"
      "else()\n"
      "  add_test(${TARGET}_NOT_BUILT ${TARGET}_NOT_BUILT)\n"
      "endif()\n"
  )

  # Build optional -D SUITE_HINT argument when exactly one suite is inferred
  set(_suite_hint_arg "")
  list(LENGTH _suite_candidates _suite_len)
  if(_suite_len EQUAL 1)
    list(GET _suite_candidates 0 _suite_hint)
    set(_suite_hint_arg -D "SUITE_HINT=${_suite_hint}")
  endif()

  add_custom_command(
    TARGET ${TARGET} POST_BUILD
    BYPRODUCTS "${ctest_tests_file}"
    COMMAND "${CMAKE_COMMAND}"
            -D "TEST_TARGET=${TARGET}"
            -D "TEST_EXECUTABLE=$<TARGET_FILE:${TARGET}>"
            ${_suite_hint_arg}
            -D "CASE_SUITE_MAP=${_case_suite_map_list}"
            -D "CTEST_FILE=${ctest_tests_file}"
            -P "${_UT_DISCOVER_TESTS_SCRIPT}"
    VERBATIM
  )

  # Add discovered tests to directory TEST_INCLUDE_FILES
  set_property(DIRECTORY
    APPEND PROPERTY TEST_INCLUDE_FILES "${ctest_include_file}"
  )
endfunction()
