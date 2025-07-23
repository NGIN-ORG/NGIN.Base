cmake_minimum_required(VERSION 3.16)
project(NGINBaseTests LANGUAGES CXX)

include(${CMAKE_SOURCE_DIR}/cmake/CPM.cmake)

# Pull in Boost.UT
CPMAddPackage(
  NAME ut
  VERSION 2.1.1
  GITHUB_REPOSITORY boost-ext/ut
  OPTIONS 
    "BOOST_UT_DISABLE_MODULE ON"
    "BOOST_UT_BUILD_EXAMPLES OFF"
    "BOOST_UT_BUILD_TESTS OFF"
)

# ----------------------------------------------------------------------------
# Auto-discover every .cpp under this directory, then remove main.cpp
# ----------------------------------------------------------------------------
file(GLOB_RECURSE ALL_TEST_SRCS
     CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

# Remove the harness entry point (we'll add that separately)
list(REMOVE_ITEM ALL_TEST_SRCS
     "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
)

# ----------------------------------------------------------------------------
# Common test‐target settings
# ----------------------------------------------------------------------------
add_library(ngin_ut_config INTERFACE)
target_link_libraries(ngin_ut_config INTERFACE Boost::ut NGIN::Base)
target_compile_features(ngin_ut_config INTERFACE cxx_std_23)

# ----------------------------------------------------------------------------
# main.cpp is our test runner entry point
# ----------------------------------------------------------------------------
set(MAIN_SRC "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")

# ----------------------------------------------------------------------------
# One executable per test file
# ----------------------------------------------------------------------------
foreach(TEST_SRC IN LISTS ALL_TEST_SRCS)
  get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)
  file(RELATIVE_PATH relPath "${CMAKE_CURRENT_SOURCE_DIR}" "${TEST_SRC}")
  get_filename_component(label "${relPath}" DIRECTORY)
  if(label STREQUAL "")
    set(label Root)
  endif()

  message(STATUS "Adding test: ${TEST_NAME} from ${TEST_SRC}")

  add_executable(${TEST_NAME} ${MAIN_SRC} ${TEST_SRC})
  target_link_libraries(${TEST_NAME} PRIVATE ngin_ut_config)
  set_target_properties(${TEST_NAME} PROPERTIES FOLDER "Tests")

  source_group(${label} FILES ${TEST_SRC})

  add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
  set_tests_properties(${TEST_NAME} PROPERTIES
    LABELS ${label}
    DESCRIPTION "Unit test for ${TEST_NAME}"
    TIMEOUT 30
  )
endforeach()

