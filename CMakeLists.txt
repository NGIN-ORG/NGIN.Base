cmake_minimum_required(VERSION 3.15)
project(NGIN.Base VERSION 0.1 LANGUAGES CXX)

#-------------------------------------------------------------------------------
# Policies
#-------------------------------------------------------------------------------
cmake_policy(SET CMP0077 NEW)

# Force libstdc++ only for Clang builds (Linux)
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  add_compile_options(-stdlib=libstdc++)
  add_link_options(-stdlib=libstdc++)
  set(CMAKE_CXX_FLAGS "-stdlib=libstdc++ --gcc-toolchain=/opt/gcc-15.2") 
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#-------------------------------------------------------------------------------
# Options
#-------------------------------------------------------------------------------
option(NGIN_BASE_BUILD_STATIC      "Build NGIN.Base as a static library" ON)
option(NGIN_BASE_BUILD_SHARED      "Build NGIN.Base as a shared library" OFF)
option(NGIN_BASE_BUILD_TESTS       "Build NGIN.Base tests"     ON)
option(NGIN_BASE_BUILD_EXAMPLES    "Build NGIN.Base examples"  ON)
option(NGIN_BASE_DEVELOPMENT_MODE
       "Deprecated compatibility toggle for non-header builds"
       OFF)

if(NGIN_BASE_DEVELOPMENT_MODE)
  message(DEPRECATION "NGIN_BASE_DEVELOPMENT_MODE is deprecated; enable explicit static/shared options instead.")
  if(NOT (NGIN_BASE_BUILD_STATIC OR NGIN_BASE_BUILD_SHARED))
    set(NGIN_BASE_BUILD_STATIC ON CACHE BOOL "Build NGIN.Base as a static library" FORCE)
  endif()
endif()

if(NOT NGIN_BASE_BUILD_STATIC AND NOT NGIN_BASE_BUILD_SHARED)
  message(FATAL_ERROR "At least one of static or shared build options must be enabled.")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#-------------------------------------------------------------------------------
# Library Definition
#-------------------------------------------------------------------------------
set(NGIN_BASE_PLATFORM_DEFINITIONS
  $<$<STREQUAL:$<PLATFORM_ID>,Windows>:NGIN_PLATFORM=\"Windows\">
  $<$<STREQUAL:$<PLATFORM_ID>,Darwin>:NGIN_PLATFORM=\"macOS\">
  $<$<STREQUAL:$<PLATFORM_ID>,Linux>:NGIN_PLATFORM=\"Linux\">
)

# On Windows, prevent the global min/max macros from <windows.h> from colliding with std::min/std::max.
# This is a ubiquitous defensive definition across the codebase to avoid needing to locally parenthesize
# every std::max / std::min usage. (See build failure in LinearAllocator / LSBFlag.)
if (WIN32)
  list(APPEND NGIN_BASE_PLATFORM_DEFINITIONS NOMINMAX)
endif()

set(NGIN_BASE_EXPORT_TARGETS)
set(ngin_base_primary_target "")

set(NGIN_BASE_CORE_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/CoreInit.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Async/Fiber/FiberCommon.cpp
)

if(WIN32)
  list(APPEND NGIN_BASE_CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Async/Fiber/Fiber.win32.cpp
  )
elseif(UNIX)
  list(APPEND NGIN_BASE_CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Async/Fiber/Fiber.posix.cpp
  )
endif()

if(NGIN_BASE_BUILD_STATIC OR NGIN_BASE_BUILD_SHARED)
  add_library(NGIN.Base.Object OBJECT ${NGIN_BASE_CORE_SOURCES})
  target_compile_features(NGIN.Base.Object PRIVATE cxx_std_23)
  target_include_directories(NGIN.Base.Object PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_compile_definitions(NGIN.Base.Object PRIVATE ${NGIN_BASE_PLATFORM_DEFINITIONS})
endif()

if(NGIN_BASE_BUILD_SHARED AND TARGET NGIN.Base.Object)
  set_property(TARGET NGIN.Base.Object PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

if(NGIN_BASE_BUILD_STATIC)
  add_library(NGIN.Base.Static STATIC)
  if(TARGET NGIN.Base.Object)
    target_sources(NGIN.Base.Static PRIVATE $<TARGET_OBJECTS:NGIN.Base.Object>)
  endif()
  target_compile_features(NGIN.Base.Static PUBLIC cxx_std_23)
  target_include_directories(NGIN.Base.Static
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  target_compile_definitions(NGIN.Base.Static
    PUBLIC
      ${NGIN_BASE_PLATFORM_DEFINITIONS}
  )
  set_target_properties(NGIN.Base.Static PROPERTIES OUTPUT_NAME "NGINBase")
  list(APPEND NGIN_BASE_EXPORT_TARGETS NGIN.Base.Static)
  add_library(NGIN::Base::Static ALIAS NGIN.Base.Static)
  set(ngin_base_primary_target NGIN.Base.Static)
endif()

if(NGIN_BASE_BUILD_SHARED)
  add_library(NGIN.Base.Shared SHARED)
  if(TARGET NGIN.Base.Object)
    target_sources(NGIN.Base.Shared PRIVATE $<TARGET_OBJECTS:NGIN.Base.Object>)
  endif()
  target_compile_features(NGIN.Base.Shared PUBLIC cxx_std_23)
  target_include_directories(NGIN.Base.Shared
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  target_compile_definitions(NGIN.Base.Shared
    PUBLIC
      ${NGIN_BASE_PLATFORM_DEFINITIONS}
      NGIN_BASE_SHARED
  )
  target_compile_definitions(NGIN.Base.Shared PRIVATE NGIN_BASE_SHARED_BUILD)
  set_target_properties(NGIN.Base.Shared PROPERTIES OUTPUT_NAME "NGINBase")
  if(NOT WIN32)
    set_target_properties(NGIN.Base.Shared PROPERTIES
      SOVERSION ${PROJECT_VERSION_MAJOR}
      VERSION ${PROJECT_VERSION})
  endif()
  list(APPEND NGIN_BASE_EXPORT_TARGETS NGIN.Base.Shared)
  add_library(NGIN::Base::Shared ALIAS NGIN.Base.Shared)
  set(ngin_base_primary_target NGIN.Base.Shared)
endif()

if(NOT ngin_base_primary_target)
  message(FATAL_ERROR "No primary NGIN.Base target could be determined.")
endif()

add_library(NGIN::Base ALIAS ${ngin_base_primary_target})

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(NEED_ATOMIC TRUE)
endif()

if(NEED_ATOMIC)
  if(TARGET NGIN.Base.Static)
    target_link_libraries(NGIN.Base.Static PRIVATE atomic)
  endif()
  if(TARGET NGIN.Base.Shared)
    target_link_libraries(NGIN.Base.Shared PRIVATE atomic)
  endif()
endif()


string(JOIN ", " NGIN_BASE_ENABLED_TARGETS ${NGIN_BASE_EXPORT_TARGETS})
message(STATUS "NGIN.Base targets enabled: ${NGIN_BASE_ENABLED_TARGETS}")

#-------------------------------------------------------------------------------
# Installation
#-------------------------------------------------------------------------------
include(GNUInstallDirs)

install(
  DIRECTORY include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
  TARGETS ${NGIN_BASE_EXPORT_TARGETS}
  EXPORT NGINBaseTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
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
