cmake_minimum_required(VERSION 3.24)
project(NGIN.Base VERSION 0.1.0 LANGUAGES CXX)

cmake_policy(SET CMP0077 NEW)

set(NGIN_BASE_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(NGIN_BASE_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}")

file(READ "${NGIN_BASE_ROOT_DIR}/include/NGIN/BaseVersion.hpp" _ngin_base_version_header)
foreach(_ngin_version_part IN ITEMS MAJOR MINOR PATCH)
  string(REGEX MATCH
    "#define NGIN_BASE_VERSION_${_ngin_version_part} ([0-9]+)"
    _ngin_version_match
    "${_ngin_base_version_header}"
  )
  if(NOT _ngin_version_match)
    message(FATAL_ERROR "BaseVersion.hpp does not define NGIN_BASE_VERSION_${_ngin_version_part}")
  endif()
  if(NOT CMAKE_MATCH_1 STREQUAL PROJECT_VERSION_${_ngin_version_part})
    message(FATAL_ERROR
      "BaseVersion.hpp ${_ngin_version_part} (${CMAKE_MATCH_1}) does not match project version "
      "${PROJECT_VERSION_${_ngin_version_part}}"
    )
  endif()
endforeach()
unset(_ngin_base_version_header)

include(cmake/NGINBaseOptions.cmake)
include(cmake/NGINBaseWarnings.cmake)
include(cmake/NGINBasePlatform.cmake)
include(cmake/NGINBaseComponents.cmake)
include(cmake/NGINBaseSources.cmake)
include(cmake/NGINBaseComponentHeaders.cmake)
include(cmake/NGINBaseTargets.cmake)
include(cmake/NGINBaseInstall.cmake)
include(cmake/NGINBaseSubdirectories.cmake)
include(cmake/NGINBaseCPack.cmake)
