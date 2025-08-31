cmake_minimum_required(VERSION 3.15)
project(NGIN.Base VERSION 0.1 LANGUAGES CXX)

#-------------------------------------------------------------------------------
# Policies
#-------------------------------------------------------------------------------
cmake_policy(SET CMP0077 NEW)

#-------------------------------------------------------------------------------
# Options
#-------------------------------------------------------------------------------
option(NGIN_BASE_BUILD_TESTS      "Build NGIN.Base tests"     ON)
option(NGIN_BASE_BUILD_EXAMPLES   "Build NGIN.Base examples"  ON)
option(NGIN_BASE_DEVELOPMENT_MODE
       "Enable Development Mode with dummy source file"
       OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_USE_RESPONSE_FILE_FOR_INCLUDES Off)

#-------------------------------------------------------------------------------
# Library Definition
#-------------------------------------------------------------------------------
if(NGIN_BASE_DEVELOPMENT_MODE)
  message(STATUS "NGIN.Base: Development Mode ON")
  add_library(NGIN.Base OBJECT
    NGINBaseDummy.cpp
  )
  target_compile_features(NGIN.Base PUBLIC
    cxx_std_23
  )
  target_include_directories(NGIN.Base
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
else()
  message(STATUS "NGIN.Base: Development Mode OFF (INTERFACE library)")
  add_library(NGIN.Base INTERFACE)
  target_compile_features(NGIN.Base INTERFACE
    cxx_std_23
  )
  target_include_directories(NGIN.Base
    INTERFACE
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
endif()

# Single macro for platform
target_compile_definitions(NGIN.Base
  INTERFACE
    $<$<STREQUAL:$<PLATFORM_ID>,Windows>:NGIN_PLATFORM=\"Windows\">
    $<$<STREQUAL:$<PLATFORM_ID>,Darwin>:NGIN_PLATFORM=\"macOS\">
    $<$<STREQUAL:$<PLATFORM_ID>,Linux>:NGIN_PLATFORM=\"Linux\">
)

# Add -latomic for GCC/Clang if needed
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(NEED_ATOMIC TRUE)
endif()


# Alias
add_library(NGIN::Base ALIAS NGIN.Base) 

if(NEED_ATOMIC)
  target_link_libraries(NGIN.Base INTERFACE atomic)
endif()

#-------------------------------------------------------------------------------
# Installation
#-------------------------------------------------------------------------------
include(GNUInstallDirs)

install(
  DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
  TARGETS NGIN.Base
  EXPORT NGINBaseTargets
)

install(
  EXPORT NGINBaseTargets
  FILE NGINBaseTargets.cmake
  NAMESPACE NGIN::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NGINBase
)

#-------------------------------------------------------------------------------
# Package‐config
#-------------------------------------------------------------------------------
include(CMakePackageConfigHelpers)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/NGINBaseConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NGINBaseConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/NGINBaseConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NGINBase
)

install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/NGINBaseConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/NGINBaseConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NGINBase
)

#-------------------------------------------------------------------------------
# Tests & Examples
#-------------------------------------------------------------------------------
if(NGIN_BASE_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
endif()

if(NGIN_BASE_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

#-------------------------------------------------------------------------------
# Benchmarks
#-------------------------------------------------------------------------------
option(NGIN_BASE_BUILD_BENCHMARKS "Build NGIN.Base benchmarks" ON)

if(NGIN_BASE_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

#-------------------------------------------------------------------------------
# CPack
#-------------------------------------------------------------------------------
include(InstallRequiredSystemLibraries)

set(CPACK_PACKAGE_NAME         "NGIN.Base")
set(CPACK_PACKAGE_VENDOR       "NGIN Team")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "NGIN.Base - A lightweight C++ header-only foundational library")
set(CPACK_PACKAGE_VERSION      ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT      "berggrenmille+NGIN@hotmail.se")
set(CPACK_PACKAGE_FILE_NAME    "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}")

if(WIN32)
  set(CPACK_GENERATOR "ZIP")
else()
  set(CPACK_GENERATOR "TGZ")
endif()

set(CPACK_SOURCE_GENERATOR "TGZ;ZIP")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
  set(CPACK_RESOURCE_FILE_LICENSE
      "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
endif()

include(CPack)
