cmake_minimum_required(VERSION 3.16)
project(NGINBaseTests LANGUAGES CXX)

include(${CMAKE_SOURCE_DIR}/cmake/CPM.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/BoostUT.cmake)

# ---------------------------------------------------------------------------
# Pull in Boost.UT
# ---------------------------------------------------------------------------
CPMAddPackage(
  NAME ut
  VERSION 2.3.1
  GITHUB_REPOSITORY boost-ext/ut
  OPTIONS
    "BOOST_UT_DISABLE_MODULE ON"
    "BOOST_UT_BUILD_EXAMPLES OFF"
    "BOOST_UT_BUILD_TESTS OFF"
)

# ---------------------------------------------------------------------------
# Common test config
# ---------------------------------------------------------------------------
add_library(ngin_ut_config INTERFACE)
target_link_libraries(ngin_ut_config INTERFACE Boost::ut NGIN::Base)
target_compile_features(ngin_ut_config INTERFACE cxx_std_23)

# main.cpp is our test runner entry point
set(MAIN_SRC "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")

# ---------------------------------------------------------------------------
# Root-level tests (one exe per .cpp file, excluding main.cpp)
# ---------------------------------------------------------------------------
file(GLOB ROOT_TEST_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(REMOVE_ITEM ROOT_TEST_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")

foreach(test_src IN LISTS ROOT_TEST_SRCS)
  get_filename_component(test_name "${test_src}" NAME_WE)
  set(exe_name "Base_${test_name}")
  message(STATUS "Adding test executable: ${exe_name} from ${test_src}")
  add_executable(${exe_name} ${MAIN_SRC} ${test_src})
  target_link_libraries(${exe_name} PRIVATE ngin_ut_config)
  set_target_properties(${exe_name} PROPERTIES FOLDER "Tests")
  source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${test_src})
  # Register individual Boost.UT tests with suite names from this file
  discover_boost_ut_test(${exe_name} TEST_SOURCE ${test_src})
endforeach()

# ---------------------------------------------------------------------------
# Subdirectory tests (one exe per .cpp file)
# ---------------------------------------------------------------------------
file(GLOB CHILDREN RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*")
foreach(child IN LISTS CHILDREN)
  if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${child}")
    file(GLOB SUBDIR_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${child}/*.cpp")
    foreach(test_src IN LISTS SUBDIR_SRCS)
      get_filename_component(test_name "${test_src}" NAME_WE)
      string(REPLACE "-" "_" child_safe "${child}")
      set(exe_name "${child_safe}_${test_name}")
      message(STATUS "Adding test executable: ${exe_name} from ${test_src}")
      add_executable(${exe_name} ${MAIN_SRC} ${test_src})
      target_link_libraries(${exe_name} PRIVATE ngin_ut_config)
      set_target_properties(${exe_name} PROPERTIES FOLDER "Tests")
      source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${test_src})
      # Register individual Boost.UT tests with suite names from this file
      discover_boost_ut_test(${exe_name} TEST_SOURCE ${test_src})
    endforeach()
  endif()
endforeach()
