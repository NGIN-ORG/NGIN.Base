cmake_minimum_required(VERSION 3.16)
project(NGINBaseTests LANGUAGES CXX)

include(${CMAKE_SOURCE_DIR}/cmake/CPM.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/BoostUT.cmake)

# Pull in Boost.UT
CPMAddPackage(
  NAME ut
  VERSION 2.3.1
  GITHUB_REPOSITORY boost-ext/ut
  OPTIONS 
    "BOOST_UT_DISABLE_MODULE ON"
    "BOOST_UT_BUILD_EXAMPLES OFF"
    "BOOST_UT_BUILD_TESTS OFF"
)

# ----------------------------------------------------------------------------
# Discover tests grouped by folder (one executable per folder)
# ----------------------------------------------------------------------------
# We'll build one test target per immediate subdirectory containing .cpp files,
# plus one for root-level test sources (excluding main.cpp).

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

## Root-level tests (directly under tests/)
file(GLOB ROOT_TEST_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
list(REMOVE_ITEM ROOT_TEST_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")
if(ROOT_TEST_SRCS)
  set(ROOT_TEST_TARGET "Root")
  message(STATUS "Adding grouped test: ${ROOT_TEST_TARGET}")
  add_executable(${ROOT_TEST_TARGET} ${MAIN_SRC} ${ROOT_TEST_SRCS})
  target_link_libraries(${ROOT_TEST_TARGET} PRIVATE ngin_ut_config)
  set_target_properties(${ROOT_TEST_TARGET} PROPERTIES FOLDER "Tests")
  source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${ROOT_TEST_SRCS})
  # Discover and register individual Boost.UT tests
  discover_boost_ut_test(${ROOT_TEST_TARGET})
endif()

## Subdirectory grouped tests
file(GLOB CHILDREN RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*")
foreach(child IN LISTS CHILDREN)
  if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${child}")
    file(GLOB_RECURSE SUBDIR_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${child}/*.cpp")
    if(SUBDIR_SRCS)
      # Normalize a target name: Tests_<Folder>
      string(REPLACE "-" "_" SUBDIR_TARGET_SAFE "${child}")
      set(GROUP_TARGET "${SUBDIR_TARGET_SAFE}")
      message(STATUS "Adding grouped test: ${GROUP_TARGET} from folder ${child}")
      add_executable(${GROUP_TARGET} ${MAIN_SRC} ${SUBDIR_SRCS})
      target_link_libraries(${GROUP_TARGET} PRIVATE ngin_ut_config)
      set_target_properties(${GROUP_TARGET} PROPERTIES FOLDER "Tests")
      source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SUBDIR_SRCS})
      # Discover and register individual Boost.UT tests
      discover_boost_ut_test(${GROUP_TARGET})
    endif()
  endif()
endforeach()
