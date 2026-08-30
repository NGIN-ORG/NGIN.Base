#-------------------------------------------------------------------------------
# Installation and package config
#-------------------------------------------------------------------------------
include(GNUInstallDirs)

# Install exactly the public-header closure owned by enabled components. A
# subset package must not appear to provide APIs whose compiled component is
# absent.
foreach(component IN LISTS NGIN_BASE_ENABLED_COMPONENTS)
  string(TOUPPER "${component}" component_upper)
  foreach(public_header IN LISTS NGIN_BASE_${component_upper}_PUBLIC_HEADERS)
    file(RELATIVE_PATH relative_header "${NGIN_BASE_ROOT_DIR}/include/NGIN" "${public_header}")
    get_filename_component(relative_directory "${relative_header}" DIRECTORY)
    if(relative_directory STREQUAL "")
      set(header_destination "${CMAKE_INSTALL_INCLUDEDIR}/NGIN")
    else()
      set(header_destination "${CMAKE_INSTALL_INCLUDEDIR}/NGIN/${relative_directory}")
    endif()
    install(FILES "${public_header}" DESTINATION "${header_destination}")
  endforeach()
endforeach()

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

export(
  EXPORT NGINBaseTargets
  FILE "${CMAKE_CURRENT_BINARY_DIR}/NGINBaseTargets.cmake"
  NAMESPACE NGIN::
)

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/NGINBaseConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMinorVersion
)

configure_package_config_file(
  "${NGIN_BASE_ROOT_DIR}/cmake/NGINBaseConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/NGINBaseConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NGINBase
)

install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/NGINBaseConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/NGINBaseConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NGINBase
)
