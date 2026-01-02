cmake_minimum_required(VERSION 3.15)
project(NGIN.Base VERSION 0.1 LANGUAGES CXX)

#-------------------------------------------------------------------------------
# Policies
#-------------------------------------------------------------------------------
cmake_policy(SET CMP0077 NEW)

#-------------------------------------------------------------------------------
# Options
#-------------------------------------------------------------------------------
option(NGIN_BASE_BUILD_STATIC      "Build NGIN.Base as a static library" ON)
option(NGIN_BASE_BUILD_SHARED      "Build NGIN.Base as a shared library" OFF)
option(NGIN_BASE_BUILD_TESTS       "Build NGIN.Base tests"     ON)
option(NGIN_BASE_BUILD_EXAMPLES    "Build NGIN.Base examples"  ON)
option(NGIN_BASE_BUILD_BENCHMARKS  "Build NGIN.Base benchmarks" ON)
option(NGIN_BASE_DEVELOPMENT_MODE
       "Deprecated compatibility toggle for non-header builds"
       OFF)

# Extended developer / diagnostics options
option(NGIN_BASE_ENABLE_ASAN       "Enable Address + Undefined Sanitizers (GNU/Clang)" OFF)
option(NGIN_BASE_ENABLE_TSAN       "Enable ThreadSanitizer (GNU/Clang)" OFF)
option(NGIN_BASE_ENABLE_LTO        "Enable Link Time Optimization for Release/RelWithDebInfo" OFF)
option(NGIN_BASE_STRICT_WARNINGS   "Enable extra warning flags" ON)
option(NGIN_BASE_ALL_FEATURES      "Convenience: enable tests + examples + benchmarks" OFF)
option(NGIN_BASE_EXPORT_COMPILE_COMMANDS "Generate compile_commands.json" ON)

set(NGIN_BASE_CLANG_GCC_TOOLCHAIN "" CACHE PATH "Clang (Linux): GCC toolchain root passed via --gcc-toolchain")

if(NGIN_BASE_ALL_FEATURES)
  set(NGIN_BASE_BUILD_TESTS ON CACHE BOOL "Build tests" FORCE)
  set(NGIN_BASE_BUILD_EXAMPLES ON CACHE BOOL "Build examples" FORCE)
  set(NGIN_BASE_BUILD_BENCHMARKS ON CACHE BOOL "Build benchmarks" FORCE)
endif()

if(NGIN_BASE_ENABLE_ASAN AND NGIN_BASE_ENABLE_TSAN)
  message(FATAL_ERROR "Cannot enable ASAN and TSAN simultaneously")
endif()

function(ngin_enable_warnings tgt)
  if(NGIN_BASE_STRICT_WARNINGS)
    get_target_property(_ngin_target_type ${tgt} TYPE)
    set(_ngin_visibility PRIVATE)
    if(_ngin_target_type STREQUAL "INTERFACE_LIBRARY")
      set(_ngin_visibility INTERFACE)
    endif()
    if(MSVC)
      target_compile_options(${tgt} ${_ngin_visibility} /W4 /permissive- /w44265 /w44263)
    else()
      target_compile_options(${tgt} ${_ngin_visibility} -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wdouble-promotion)
    endif()
  endif()
endfunction()

if(NGIN_BASE_DEVELOPMENT_MODE)
  message(DEPRECATION "NGIN_BASE_DEVELOPMENT_MODE is deprecated; enable explicit static/shared options instead.")
  if(NOT (NGIN_BASE_BUILD_STATIC OR NGIN_BASE_BUILD_SHARED))
    set(NGIN_BASE_BUILD_STATIC ON CACHE BOOL "Build NGIN.Base as a static library" FORCE)
  endif()
endif()

if(NOT NGIN_BASE_BUILD_STATIC AND NOT NGIN_BASE_BUILD_SHARED)
  message(FATAL_ERROR "At least one of static or shared build options must be enabled.")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ${NGIN_BASE_EXPORT_COMPILE_COMMANDS})

#-------------------------------------------------------------------------------
# Internal build options (private to this repo build)
#-------------------------------------------------------------------------------
add_library(NGIN.Base.BuildOptions INTERFACE)
add_library(NGIN::Base::BuildOptions ALIAS NGIN.Base.BuildOptions)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  if(NGIN_BASE_ENABLE_ASAN)
    target_compile_options(NGIN.Base.BuildOptions INTERFACE
      $<BUILD_INTERFACE:-fsanitize=address,undefined>
      $<BUILD_INTERFACE:-fno-omit-frame-pointer>
    )
    target_link_options(NGIN.Base.BuildOptions INTERFACE $<BUILD_INTERFACE:-fsanitize=address,undefined>)
  endif()
  if(NGIN_BASE_ENABLE_TSAN)
    target_compile_options(NGIN.Base.BuildOptions INTERFACE
      $<BUILD_INTERFACE:-fsanitize=thread>
      $<BUILD_INTERFACE:-fno-omit-frame-pointer>
    )
    target_link_options(NGIN.Base.BuildOptions INTERFACE $<BUILD_INTERFACE:-fsanitize=thread>)
  endif()
endif()

# Clang (Linux): allow selecting an explicit GCC toolchain for libstdc++/runtime.
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NGIN_BASE_CLANG_GCC_TOOLCHAIN)
  if(NOT EXISTS "${NGIN_BASE_CLANG_GCC_TOOLCHAIN}")
    message(WARNING "NGIN_BASE_CLANG_GCC_TOOLCHAIN does not exist: ${NGIN_BASE_CLANG_GCC_TOOLCHAIN}")
  endif()
  target_compile_options(NGIN.Base.BuildOptions INTERFACE $<BUILD_INTERFACE:--gcc-toolchain=${NGIN_BASE_CLANG_GCC_TOOLCHAIN}>)
  target_link_options(NGIN.Base.BuildOptions INTERFACE $<BUILD_INTERFACE:--gcc-toolchain=${NGIN_BASE_CLANG_GCC_TOOLCHAIN}>)
endif()

#-------------------------------------------------------------------------------
# Execution backend selection (internal)
#-------------------------------------------------------------------------------
set(NGIN_BASE_FIBER_BACKEND "default" CACHE STRING "Fiber backend: default/ucontext/winfiber/custom_asm")
set_property(CACHE NGIN_BASE_FIBER_BACKEND PROPERTY STRINGS default ucontext winfiber custom_asm)

#-------------------------------------------------------------------------------
# Library Definition
#-------------------------------------------------------------------------------
set(NGIN_BASE_PLATFORM_DEFINITIONS
  $<$<STREQUAL:$<PLATFORM_ID>,Windows>:NGIN_PLATFORM_WINDOWS>
  $<$<STREQUAL:$<PLATFORM_ID>,Darwin>:NGIN_PLATFORM_MACOS>
  $<$<STREQUAL:$<PLATFORM_ID>,Linux>:NGIN_PLATFORM_LINUX>
)

if(NGIN_BASE_FIBER_BACKEND STREQUAL "ucontext")
  if(NOT UNIX)
    message(FATAL_ERROR "NGIN_BASE_FIBER_BACKEND=ucontext is only supported on UNIX builds")
  endif()
  list(APPEND NGIN_BASE_PLATFORM_DEFINITIONS NGIN_EXECUTION_FIBER_BACKEND=NGIN_EXECUTION_FIBER_BACKEND_UCONTEXT)
elseif(NGIN_BASE_FIBER_BACKEND STREQUAL "winfiber")
  if(NOT WIN32)
    message(FATAL_ERROR "NGIN_BASE_FIBER_BACKEND=winfiber is only supported on Windows builds")
  endif()
  list(APPEND NGIN_BASE_PLATFORM_DEFINITIONS NGIN_EXECUTION_FIBER_BACKEND=NGIN_EXECUTION_FIBER_BACKEND_WIN_FIBER)
elseif(NGIN_BASE_FIBER_BACKEND STREQUAL "custom_asm")
  if(NOT UNIX)
    message(FATAL_ERROR "NGIN_BASE_FIBER_BACKEND=custom_asm is only supported on UNIX builds")
  endif()
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "NGIN_BASE_FIBER_BACKEND=custom_asm is currently only supported on Linux")
  endif()
  if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64|aarch64|arm64)$")
    message(FATAL_ERROR "NGIN_BASE_FIBER_BACKEND=custom_asm currently requires x86_64 or aarch64")
  endif()
  list(APPEND NGIN_BASE_PLATFORM_DEFINITIONS NGIN_EXECUTION_FIBER_BACKEND=NGIN_EXECUTION_FIBER_BACKEND_CUSTOM_ASM)
elseif(NGIN_BASE_FIBER_BACKEND STREQUAL "default")
  # Use platform defaults from include/NGIN/Execution/Config.hpp
else()
  message(FATAL_ERROR "Unknown NGIN_BASE_FIBER_BACKEND value: ${NGIN_BASE_FIBER_BACKEND}")
endif()

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

  if(NGIN_BASE_FIBER_BACKEND STREQUAL "custom_asm")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
      list(APPEND NGIN_BASE_CORE_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Async/Fiber/FiberContext.x86_64.cpp
      )
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
      list(APPEND NGIN_BASE_CORE_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Async/Fiber/FiberContext.aarch64.cpp
      )
    endif()
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    list(APPEND NGIN_BASE_CORE_SOURCES
      ${CMAKE_CURRENT_SOURCE_DIR}/src/Async/Fiber/FiberContext.x86_64.cpp
    )
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    list(APPEND NGIN_BASE_CORE_SOURCES
      ${CMAKE_CURRENT_SOURCE_DIR}/src/Async/Fiber/FiberContext.aarch64.cpp
    )
  endif()
endif()

list(APPEND NGIN_BASE_CORE_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/NGIN/Net/NetworkDriver.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/NGIN/Net/SocketPlatform.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/NGIN/Net/TcpSocket.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/NGIN/Net/TcpListener.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/NGIN/Net/UdpSocket.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/NGIN/Net/PollerIocp.cpp
)

if(NGIN_BASE_BUILD_STATIC OR NGIN_BASE_BUILD_SHARED)
  add_library(NGIN.Base.Object OBJECT ${NGIN_BASE_CORE_SOURCES})
  target_compile_features(NGIN.Base.Object PRIVATE cxx_std_23)
  set_target_properties(NGIN.Base.Object PROPERTIES CXX_EXTENSIONS OFF)
  target_include_directories(NGIN.Base.Object PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_compile_definitions(NGIN.Base.Object PRIVATE ${NGIN_BASE_PLATFORM_DEFINITIONS})
  target_link_libraries(NGIN.Base.Object PRIVATE NGIN.Base.BuildOptions)
  ngin_enable_warnings(NGIN.Base.Object)
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
  set_target_properties(NGIN.Base.Static PROPERTIES CXX_EXTENSIONS OFF)
  target_include_directories(NGIN.Base.Static
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  target_compile_definitions(NGIN.Base.Static
    PUBLIC
      ${NGIN_BASE_PLATFORM_DEFINITIONS}
  )
  set_target_properties(NGIN.Base.Static PROPERTIES
    OUTPUT_NAME "NGINBase"
    EXPORT_NAME BaseStatic)
  target_link_libraries(NGIN.Base.Static PRIVATE NGIN.Base.BuildOptions)
  ngin_enable_warnings(NGIN.Base.Static)
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
  set_target_properties(NGIN.Base.Shared PROPERTIES CXX_EXTENSIONS OFF)
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
  set_target_properties(NGIN.Base.Shared PROPERTIES
    OUTPUT_NAME "NGINBase"
    EXPORT_NAME BaseShared)
  if(NOT WIN32)
    set_target_properties(NGIN.Base.Shared PROPERTIES
      SOVERSION ${PROJECT_VERSION_MAJOR}
      VERSION ${PROJECT_VERSION})
  endif()
  target_link_libraries(NGIN.Base.Shared PRIVATE NGIN.Base.BuildOptions)
  ngin_enable_warnings(NGIN.Base.Shared)
  if(WIN32)
    set_target_properties(NGIN.Base.Shared PROPERTIES ARCHIVE_OUTPUT_NAME "NGINBaseShared")
  endif()
  list(APPEND NGIN_BASE_EXPORT_TARGETS NGIN.Base.Shared)
  add_library(NGIN::Base::Shared ALIAS NGIN.Base.Shared)
  set(ngin_base_primary_target NGIN.Base.Shared)
endif()

if(NOT ngin_base_primary_target)
  message(FATAL_ERROR "No primary NGIN.Base target could be determined.")
endif()

if(NGIN_BASE_BUILD_STATIC AND NGIN_BASE_BUILD_SHARED)
  message(STATUS "Both static and shared are enabled; NGIN::Base aliases the shared library.")
endif()

add_library(NGIN::Base ALIAS ${ngin_base_primary_target})

# Link Time Optimization (IPO)
if(NGIN_BASE_ENABLE_LTO)
  if(NGIN_BASE_ENABLE_ASAN OR NGIN_BASE_ENABLE_TSAN)
    message(WARNING "LTO requested but sanitizers are enabled; skipping IPO/LTO.")
  else()
    include(CheckIPOSupported)
    check_ipo_supported(RESULT NGIN_HAS_IPO OUTPUT NGIN_IPO_ERR)
    if(NGIN_HAS_IPO)
      set(_ngin_ipo_enabled_targets)
      foreach(tgt IN ITEMS NGIN.Base.Object NGIN.Base.Static NGIN.Base.Shared)
        if(TARGET ${tgt})
          foreach(cfg IN ITEMS Release RelWithDebInfo MinSizeRel)
            set_property(TARGET ${tgt} PROPERTY INTERPROCEDURAL_OPTIMIZATION_${cfg} ON)
          endforeach()
          list(APPEND _ngin_ipo_enabled_targets ${tgt})
        endif()
      endforeach()
      if(_ngin_ipo_enabled_targets)
        string(JOIN ", " _ngin_ipo_enabled_targets_str ${_ngin_ipo_enabled_targets})
        message(STATUS "IPO/LTO enabled for: ${_ngin_ipo_enabled_targets_str}")
      endif()
    else()
      message(WARNING "LTO requested but not supported: ${NGIN_IPO_ERR}")
    endif()
  endif()
endif()

# Windows: link Synchronization.lib for WaitOnAddress/WakeByAddress* APIs
if(WIN32)
  find_library(_ngin_synchronization_lib Synchronization)
  if(_ngin_synchronization_lib)
    if(TARGET NGIN.Base.Static)
      target_link_libraries(NGIN.Base.Static PRIVATE ${_ngin_synchronization_lib})
    endif()
    if(TARGET NGIN.Base.Shared)
      target_link_libraries(NGIN.Base.Shared PRIVATE ${_ngin_synchronization_lib})
    endif()
  else()
    message(WARNING "Windows Synchronization.lib not found; WaitOnAddress/WakeByAddress* will fail to link")
  endif()

  find_library(_ngin_ws2_32_lib Ws2_32)
  if(_ngin_ws2_32_lib)
    if(TARGET NGIN.Base.Static)
      target_link_libraries(NGIN.Base.Static PRIVATE ${_ngin_ws2_32_lib})
    endif()
    if(TARGET NGIN.Base.Shared)
      target_link_libraries(NGIN.Base.Shared PRIVATE ${_ngin_ws2_32_lib})
    endif()
  else()
    message(WARNING "Windows Ws2_32.lib not found; socket APIs will fail to link")
  endif()
endif()

set(_ngin_need_atomic FALSE)
if(UNIX AND NOT APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  if(CMAKE_CROSSCOMPILING)
    find_library(_ngin_atomic_lib atomic)
    if(_ngin_atomic_lib)
      set(_ngin_need_atomic TRUE)
    endif()
  else()
    include(CheckCXXSourceCompiles)
    string(CONCAT _ngin_atomic_test_source
      "#include <atomic>\n"
      "#include <cstddef>\n"
      "int main() {\n"
      "  std::atomic<std::size_t> v {0};\n"
      "  v.fetch_add(1, std::memory_order_relaxed);\n"
      "  return static_cast<int>(v.load(std::memory_order_relaxed));\n"
      "}\n")
    set(_ngin_required_quiet_prev "${CMAKE_REQUIRED_QUIET}")
    set(CMAKE_REQUIRED_QUIET TRUE)
    check_cxx_source_compiles("${_ngin_atomic_test_source}" NGIN_BASE_ATOMIC_LINKS_WITHOUT_LIBATOMIC)
    if(NOT NGIN_BASE_ATOMIC_LINKS_WITHOUT_LIBATOMIC)
      set(CMAKE_REQUIRED_LIBRARIES atomic)
      check_cxx_source_compiles("${_ngin_atomic_test_source}" NGIN_BASE_ATOMIC_LINKS_WITH_LIBATOMIC)
      if(NGIN_BASE_ATOMIC_LINKS_WITH_LIBATOMIC)
        set(_ngin_need_atomic TRUE)
      endif()
      unset(CMAKE_REQUIRED_LIBRARIES)
    endif()
    set(CMAKE_REQUIRED_QUIET "${_ngin_required_quiet_prev}")
    unset(_ngin_required_quiet_prev)
  endif()
endif()

if(_ngin_need_atomic)
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

set_target_properties(NGIN.Base.BuildOptions PROPERTIES EXPORT_NAME BaseBuildOptions)
install(
  TARGETS NGIN.Base.BuildOptions
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
    "NGIN.Base - A lightweight C++ foundational library")
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
