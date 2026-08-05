function(ngin_base_resolve_test_link_target test_group suite_name out_var)
  set(link_target NGIN::Base::Foundation)
  if(test_group STREQUAL "Crypto" OR (test_group STREQUAL "Include" AND suite_name STREQUAL "Crypto"))
    set(link_target NGIN::Base::Crypto)
  elseif((test_group STREQUAL "Net" AND suite_name STREQUAL "TLS") OR
         (test_group STREQUAL "Include" AND suite_name STREQUAL "NetTLS"))
    set(link_target NGIN::Base::NetTLS)
  elseif(test_group STREQUAL "Net" OR (test_group STREQUAL "Include" AND suite_name STREQUAL "Net"))
    set(link_target NGIN::Base::Net)
  elseif(test_group STREQUAL "Serialization" OR (test_group STREQUAL "Include" AND suite_name STREQUAL "Serialization"))
    set(link_target NGIN::Base::Serialization)
  elseif(test_group STREQUAL "IO" OR (test_group STREQUAL "Include" AND suite_name STREQUAL "IO"))
    set(link_target NGIN::Base::IO)
  elseif(test_group STREQUAL "Async" OR test_group STREQUAL "Execution" OR
         (test_group STREQUAL "Include" AND suite_name STREQUAL "Execution"))
    set(link_target NGIN::Base::Execution)
  endif()
  set(${out_var} ${link_target} PARENT_SCOPE)
endfunction()

function(ngin_base_add_test_executable exe_name test_src test_label test_prefix link_target)
  message(STATUS "Adding test executable: ${exe_name} from ${test_src}")
  add_executable(${exe_name} ${test_src})
  list(APPEND NGIN_BASE_TEST_TARGETS ${exe_name})
  set(NGIN_BASE_TEST_TARGETS ${NGIN_BASE_TEST_TARGETS} PARENT_SCOPE)

  target_link_libraries(${exe_name} PRIVATE Catch2::Catch2WithMain ngin_base_test_config ${link_target})
  if(WIN32 AND test_label STREQUAL "Net")
    target_link_libraries(${exe_name} PRIVATE ws2_32)
  endif()
  set_target_properties(${exe_name} PROPERTIES
    FOLDER "Tests"
    LABELS "${test_label}"
  )
  source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${test_src})
  catch_discover_tests(${exe_name}
    TEST_PREFIX "${test_prefix}"
    WORKING_DIRECTORY $<TARGET_FILE_DIR:${exe_name}>
    DL_PATHS $<TARGET_FILE_DIR:${link_target}>
    PROPERTIES LABELS "${test_label}" SKIP_RETURN_CODE 4
  )
endfunction()

function(ngin_base_configure_test_targets test_targets)
  foreach(test_tgt IN LISTS test_targets)
    if(MSVC)
      target_compile_options(${test_tgt} PRIVATE /W4 /permissive-)
    else()
      target_compile_options(${test_tgt} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
  endforeach()

  if(NGIN_BASE_TEST_ENABLE_ASAN AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    foreach(test_tgt IN LISTS test_targets)
      target_compile_options(${test_tgt} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
      target_link_options(${test_tgt} PRIVATE -fsanitize=address,undefined)
    endforeach()
  endif()
endfunction()
