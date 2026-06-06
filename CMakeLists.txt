cmake_minimum_required(VERSION 3.15)
project(NGIN.Base VERSION 0.1 LANGUAGES CXX)

cmake_policy(SET CMP0077 NEW)

set(NGIN_BASE_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(NGIN_BASE_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}")

include(cmake/NGINBaseOptions.cmake)
include(cmake/NGINBaseWarnings.cmake)
include(cmake/NGINBasePlatform.cmake)
include(cmake/NGINBaseSources.cmake)
include(cmake/NGINBaseComponents.cmake)
include(cmake/NGINBaseTargets.cmake)
include(cmake/NGINBaseSplitTargets.cmake)
include(cmake/NGINBaseInstall.cmake)
include(cmake/NGINBaseSubdirectories.cmake)
include(cmake/NGINBaseCPack.cmake)
