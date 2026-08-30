foreach(required_variable IN ITEMS
        NGIN_BASE_SOURCE_DIR
        NGIN_BASE_MATRIX_ROOT
        NGIN_BASE_TEST_GENERATOR
        NGIN_BASE_TEST_CONFIG
        NGIN_BASE_PACKAGE_COMPONENTS
        NGIN_BASE_CONSUMER_COMPONENT
        NGIN_BASE_EXPECT_AGGREGATE
        NGIN_BASE_EXPECTED_HEADER)
  if(NOT DEFINED ${required_variable})
    message(FATAL_ERROR "${required_variable} is required")
  endif()
endforeach()

set(matrix_root "${NGIN_BASE_MATRIX_ROOT}/${NGIN_BASE_CONSUMER_COMPONENT}")
set(package_build "${matrix_root}/package-build")
set(install_prefix "${matrix_root}/install")
set(consumer_build "${matrix_root}/consumer-build")
file(REMOVE_RECURSE "${matrix_root}")

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -S "${NGIN_BASE_SOURCE_DIR}"
    -B "${package_build}"
    -G "${NGIN_BASE_TEST_GENERATOR}"
    "-DNGIN_BASE_BUILD_COMPONENTS=${NGIN_BASE_PACKAGE_COMPONENTS}"
    -DNGIN_BASE_BUILD_TESTS=OFF
    -DNGIN_BASE_BUILD_EXAMPLES=OFF
    -DNGIN_BASE_BUILD_BENCHMARKS=OFF
    -DNGIN_BASE_BUILD_STATIC=ON
    -DNGIN_BASE_BUILD_SHARED=OFF
    -DNGIN_BASE_CRYPTO_WITH_OPENSSL=OFF
    -DNGIN_BASE_TLS_WITH_OPENSSL=OFF
    "-DCMAKE_INSTALL_PREFIX=${install_prefix}"
  RESULT_VARIABLE configure_result
)
if(NOT configure_result EQUAL 0)
  message(FATAL_ERROR "Configuring the ${NGIN_BASE_CONSUMER_COMPONENT} package failed")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${package_build}" --config "${NGIN_BASE_TEST_CONFIG}" --target install
  RESULT_VARIABLE package_build_result
)
if(NOT package_build_result EQUAL 0)
  message(FATAL_ERROR "Building/installing the ${NGIN_BASE_CONSUMER_COMPONENT} package failed")
endif()

if(NOT EXISTS "${install_prefix}/include/${NGIN_BASE_EXPECTED_HEADER}")
  message(FATAL_ERROR "Expected installed header is missing: ${NGIN_BASE_EXPECTED_HEADER}")
endif()
if(NOT NGIN_BASE_ABSENT_HEADER STREQUAL "NONE" AND
   EXISTS "${install_prefix}/include/${NGIN_BASE_ABSENT_HEADER}")
  message(FATAL_ERROR "Subset package installed an out-of-closure header: ${NGIN_BASE_ABSENT_HEADER}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}"
    -S "${NGIN_BASE_SOURCE_DIR}/tests/PackageConsumer"
    -B "${consumer_build}"
    -G "${NGIN_BASE_TEST_GENERATOR}"
    "-DCMAKE_PREFIX_PATH=${install_prefix}"
    "-DNGIN_BASE_TEST_COMPONENT=${NGIN_BASE_CONSUMER_COMPONENT}"
    "-DNGIN_BASE_EXPECT_AGGREGATE=${NGIN_BASE_EXPECT_AGGREGATE}"
  RESULT_VARIABLE consumer_configure_result
)
if(NOT consumer_configure_result EQUAL 0)
  message(FATAL_ERROR "Configuring the ${NGIN_BASE_CONSUMER_COMPONENT} consumer failed")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${consumer_build}" --config "${NGIN_BASE_TEST_CONFIG}"
  RESULT_VARIABLE consumer_build_result
)
if(NOT consumer_build_result EQUAL 0)
  message(FATAL_ERROR "Building the ${NGIN_BASE_CONSUMER_COMPONENT} consumer failed")
endif()

execute_process(
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${consumer_build}" -C "${NGIN_BASE_TEST_CONFIG}" --output-on-failure
  RESULT_VARIABLE consumer_test_result
)
if(NOT consumer_test_result EQUAL 0)
  message(FATAL_ERROR "Running the ${NGIN_BASE_CONSUMER_COMPONENT} consumer failed")
endif()
