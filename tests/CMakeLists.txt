cmake_minimum_required(VERSION 3.16)
project(NGINBaseTests LANGUAGES CXX)

# Determine the top-level source/binary directories even when this subproject
# is included from an aggregate build.
set(_base_root_dir "${NGIN_BASE_ROOT_DIR}")
if(NOT _base_root_dir)
  get_filename_component(_base_root_dir "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
endif()
set(_base_binary_dir "${NGIN_BASE_BINARY_DIR}")
if(NOT _base_binary_dir)
  get_filename_component(_base_binary_dir "${CMAKE_CURRENT_BINARY_DIR}/.." ABSOLUTE)
endif()

include("${_base_root_dir}/cmake/CPM.cmake")
include(CTest)

# Cache third-party sources to speed up repeated configuration runs (CI, IDE refresh).
set(CPM_SOURCE_CACHE "${PROJECT_BINARY_DIR}/cpm-cache" CACHE PATH "CPM cache")

# ---------------------------------------------------------------------------
# Pull in Catch2 via CPM
# ---------------------------------------------------------------------------
CPMAddPackage(
  NAME Catch2
  VERSION 3.5.3
  GITHUB_REPOSITORY catchorg/Catch2
  OPTIONS
    "CATCH_ENABLE_COVERAGE OFF"
    "CATCH_ENABLE_WERROR OFF"
    "CATCH_INSTALL_DOCS OFF"
    "CATCH_INSTALL_EXTRAS OFF"
)

if(Catch2_SOURCE_DIR)
  list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
endif()
if(Catch2_DIR)
  list(APPEND CMAKE_MODULE_PATH ${Catch2_DIR})
endif()
include(Catch)

# ---------------------------------------------------------------------------
# Common test config / options
# ---------------------------------------------------------------------------
add_library(ngin_base_test_config INTERFACE)
target_link_libraries(ngin_base_test_config INTERFACE NGIN::Base)
target_compile_features(ngin_base_test_config INTERFACE cxx_std_23)

option(NGIN_BASE_TESTS_ONE_EXE_PER_FILE "Split each test source into its own executable" ON)
option(NGIN_BASE_ENABLE_ASAN "Enable Address/Undefined sanitizers for tests" OFF)

# Gather every test source so we can either create per-file binaries or a single aggregator.
file(GLOB ROOT_TEST_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
set(NGIN_BASE_ALL_TEST_SRCS ${ROOT_TEST_SRCS})

file(GLOB CHILDREN RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*")
foreach(child IN LISTS CHILDREN)
  if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${child}")
    file(GLOB SUBDIR_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${child}/*.cpp")
    list(APPEND NGIN_BASE_ALL_TEST_SRCS ${SUBDIR_SRCS})
  endif()
endforeach()

set(NGIN_BASE_TEST_TARGETS)

if(NGIN_BASE_TESTS_ONE_EXE_PER_FILE)
  foreach(test_src IN LISTS ROOT_TEST_SRCS)
    get_filename_component(test_name "${test_src}" NAME_WE)
    set(exe_name "Base_${test_name}Tests")
    message(STATUS "Adding test executable: ${exe_name} from ${test_src}")
    add_executable(${exe_name} ${test_src})
    list(APPEND NGIN_BASE_TEST_TARGETS ${exe_name})
    target_link_libraries(${exe_name} PRIVATE Catch2::Catch2WithMain ngin_base_test_config)
    set_target_properties(${exe_name} PROPERTIES FOLDER "Tests")
    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${test_src})
    set(suite_name "${test_name}")
    string(REGEX REPLACE "Tests$" "" suite_name "${suite_name}")
    set(test_prefix "Base.${suite_name}.")
    catch_discover_tests(${exe_name}
      TEST_PREFIX "${test_prefix}"
      WORKING_DIRECTORY $<TARGET_FILE_DIR:${exe_name}>)
  endforeach()

  foreach(child IN LISTS CHILDREN)
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${child}")
      file(GLOB SUBDIR_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${child}/*.cpp")
      foreach(test_src IN LISTS SUBDIR_SRCS)
        get_filename_component(test_name "${test_src}" NAME_WE)
        string(REGEX REPLACE "[^A-Za-z0-9_]" "_" child_safe "${child}")
        set(exe_name "${child_safe}_${test_name}Tests")
        message(STATUS "Adding test executable: ${exe_name} from ${test_src}")
        add_executable(${exe_name} ${test_src})
        list(APPEND NGIN_BASE_TEST_TARGETS ${exe_name})
        target_link_libraries(${exe_name} PRIVATE Catch2::Catch2WithMain ngin_base_test_config)
        set_target_properties(${exe_name} PROPERTIES FOLDER "Tests")
        source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${test_src})
        set(suite_name "${test_name}")
        string(REGEX REPLACE "Tests$" "" suite_name "${suite_name}")
        set(test_prefix "Base.${child_safe}.${suite_name}.")
        catch_discover_tests(${exe_name}
          TEST_PREFIX "${test_prefix}"
          WORKING_DIRECTORY $<TARGET_FILE_DIR:${exe_name}>)
      endforeach()
    endif()
  endforeach()
else()
  add_executable(Base_AllTests ${NGIN_BASE_ALL_TEST_SRCS})
  list(APPEND NGIN_BASE_TEST_TARGETS Base_AllTests)
  target_link_libraries(Base_AllTests PRIVATE Catch2::Catch2WithMain ngin_base_test_config)
  set_target_properties(Base_AllTests PROPERTIES FOLDER "Tests")
  source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${NGIN_BASE_ALL_TEST_SRCS})
  catch_discover_tests(Base_AllTests
    TEST_PREFIX "Base.AllTests."
    WORKING_DIRECTORY $<TARGET_FILE_DIR:Base_AllTests>)
endif()

# ---------------------------------------------------------------------------
# Common diagnostics / sanitizer toggles for tests
# ---------------------------------------------------------------------------
foreach(test_tgt IN LISTS NGIN_BASE_TEST_TARGETS)
  if(MSVC)
    target_compile_options(${test_tgt} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${test_tgt} PRIVATE -Wall -Wextra -Wpedantic -mavx2)
    #target_compile_options(${test_tgt} PRIVATE -Wall -Wextra -Wpedantic -mavx2)
  endif()
endforeach()

if(NGIN_BASE_ENABLE_ASAN AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  foreach(test_tgt IN LISTS NGIN_BASE_TEST_TARGETS)
    target_compile_options(${test_tgt} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${test_tgt} PRIVATE -fsanitize=address,undefined)
  endforeach()
endif()
