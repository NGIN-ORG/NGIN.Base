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
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/NGINBaseTestHelpers.cmake")
include(CTest)

# Cache third-party sources to speed up repeated configuration runs (CI, IDE refresh).
set(CPM_SOURCE_CACHE "${PROJECT_BINARY_DIR}/cpm-cache" CACHE PATH "CPM cache")

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

if(TARGET Catch2)
  target_compile_features(Catch2 PRIVATE cxx_std_17)
endif()
if(TARGET Catch2WithMain)
  target_compile_features(Catch2WithMain PRIVATE cxx_std_17)
endif()

add_library(ngin_base_test_config INTERFACE)
if(TARGET NGIN.Base.BuildOptions)
  target_link_libraries(ngin_base_test_config INTERFACE NGIN.Base.BuildOptions)
endif()
target_compile_features(ngin_base_test_config INTERFACE cxx_std_23)

set(NGIN_BASE_DYNAMIC_LIBRARY_TEST_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/IO")

add_library(NGINBaseDynamicLibraryFixture SHARED
  ${CMAKE_CURRENT_SOURCE_DIR}/IO/fixtures/DynamicLibraryFixture.cpp
)
target_compile_features(NGINBaseDynamicLibraryFixture PRIVATE cxx_std_23)
set_target_properties(NGINBaseDynamicLibraryFixture PROPERTIES
  FOLDER "Tests"
  LIBRARY_OUTPUT_DIRECTORY "${NGIN_BASE_DYNAMIC_LIBRARY_TEST_OUTPUT_DIR}"
  RUNTIME_OUTPUT_DIRECTORY "${NGIN_BASE_DYNAMIC_LIBRARY_TEST_OUTPUT_DIR}"
)

option(NGIN_BASE_TESTS_ONE_EXE_PER_FILE "Split each test source into its own executable" ON)
option(NGIN_BASE_TEST_ENABLE_ASAN "Enable Address/Undefined sanitizers for test executables" ${NGIN_BASE_ENABLE_ASAN})

file(GLOB ROOT_TEST_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
set(NGIN_BASE_ALL_TEST_SRCS ${ROOT_TEST_SRCS})

file(GLOB CHILDREN CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*")
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
    set(suite_name "${test_name}")
    string(REGEX REPLACE "Tests$" "" suite_name "${suite_name}")
    set(exe_name "Base_${test_name}Tests")
    ngin_base_add_test_executable(${exe_name} "${test_src}" "Base" "Base.${suite_name}." "NGIN::Base")
  endforeach()

  foreach(child IN LISTS CHILDREN)
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${child}")
      file(GLOB SUBDIR_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${child}/*.cpp")
      foreach(test_src IN LISTS SUBDIR_SRCS)
        get_filename_component(test_name "${test_src}" NAME_WE)
        set(suite_name "${test_name}")
        string(REGEX REPLACE "Tests$" "" suite_name "${suite_name}")
        string(REGEX REPLACE "[^A-Za-z0-9_]" "_" child_safe "${child}")
        set(exe_name "${child_safe}_${test_name}Tests")
        set(test_label "${child_safe}")
        if(child STREQUAL "Include")
          set(test_label "Include.${suite_name}")
        endif()
        ngin_base_resolve_test_link_target("${child_safe}" "${suite_name}" test_link_target)
        ngin_base_add_test_executable(
          ${exe_name}
          "${test_src}"
          "${test_label}"
          "Base.${child_safe}.${suite_name}."
          "${test_link_target}"
        )
      endforeach()
    endif()
  endforeach()
else()
  add_executable(Base_AllTests ${NGIN_BASE_ALL_TEST_SRCS})
  list(APPEND NGIN_BASE_TEST_TARGETS Base_AllTests)
  target_link_libraries(Base_AllTests PRIVATE Catch2::Catch2WithMain ngin_base_test_config NGIN::Base)
  set_target_properties(Base_AllTests PROPERTIES FOLDER "Tests" LABELS "Base")
  source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${NGIN_BASE_ALL_TEST_SRCS})
  catch_discover_tests(Base_AllTests
    TEST_PREFIX "Base.AllTests."
    WORKING_DIRECTORY $<TARGET_FILE_DIR:Base_AllTests>
    PROPERTIES LABELS "Base"
  )
endif()

ngin_base_configure_test_targets("${NGIN_BASE_TEST_TARGETS}")

if(TARGET IO_DynamicLibraryTests)
  add_dependencies(IO_DynamicLibraryTests NGINBaseDynamicLibraryFixture)
  set_target_properties(IO_DynamicLibraryTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${NGIN_BASE_DYNAMIC_LIBRARY_TEST_OUTPUT_DIR}"
  )
  target_compile_definitions(IO_DynamicLibraryTests PRIVATE
    NGIN_BASE_TEST_DYNAMIC_LIBRARY_FILENAME="$<TARGET_FILE_NAME:NGINBaseDynamicLibraryFixture>"
  )
endif()

if(NGIN_BASE_BUILD_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "NGIN_BASE_BUILD_FUZZERS requires Clang with LLVM libFuzzer support")
  endif()

  set(NGIN_BASE_CRYPTO_FUZZERS
    Crypto/Fuzz/DerFuzzer.cpp
    Crypto/Fuzz/JwtFuzzer.cpp
    Crypto/Fuzz/KeyFormatFuzzer.cpp
    Crypto/Fuzz/PasetoFuzzer.cpp
    Crypto/Fuzz/PemFuzzer.cpp
    Crypto/Fuzz/X509Fuzzer.cpp
  )

  foreach(fuzzer_src IN LISTS NGIN_BASE_CRYPTO_FUZZERS)
    get_filename_component(fuzzer_name "${fuzzer_src}" NAME_WE)
    add_executable("Crypto_${fuzzer_name}" "${CMAKE_CURRENT_SOURCE_DIR}/${fuzzer_src}")
    target_link_libraries("Crypto_${fuzzer_name}" PRIVATE ngin_base_test_config NGIN::Crypto)
    target_compile_options("Crypto_${fuzzer_name}" PRIVATE -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    target_link_options("Crypto_${fuzzer_name}" PRIVATE -fsanitize=fuzzer,address,undefined)
    set_target_properties("Crypto_${fuzzer_name}" PROPERTIES FOLDER "Fuzzers" LABELS "Crypto.Fuzz")
    string(REGEX REPLACE "Fuzzer$" "" fuzzer_case "${fuzzer_name}")
    add_test(
      NAME "Base.Crypto.Fuzz.${fuzzer_case}.Startup"
      COMMAND "Crypto_${fuzzer_name}" -runs=1
    )
    set_tests_properties(
      "Base.Crypto.Fuzz.${fuzzer_case}.Startup"
      PROPERTIES LABELS "Crypto.Fuzz"
    )
  endforeach()
endif()
