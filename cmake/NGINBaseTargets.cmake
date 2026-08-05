#-------------------------------------------------------------------------------
# Compiled component and aggregate targets
#-------------------------------------------------------------------------------
include(GNUInstallDirs)

set(NGIN_BASE_EXPORT_TARGETS)
set(NGIN_BASE_STATIC_COMPONENT_TARGETS)
set(NGIN_BASE_SHARED_COMPONENT_TARGETS)

function(ngin_base_configure_component_target target_name component linkage)
  string(TOUPPER "${component}" component_upper)

  target_compile_features(${target_name} PUBLIC cxx_std_23)
  set_target_properties(${target_name} PROPERTIES
    CXX_EXTENSIONS OFF
    OUTPUT_NAME "NGINBase${component}"
    EXPORT_NAME "Base${component}${linkage}"
  )
  target_include_directories(${target_name}
    PUBLIC
      $<BUILD_INTERFACE:${NGIN_BASE_ROOT_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
      ${NGIN_BASE_${component_upper}_PRIVATE_INCLUDE_DIRECTORIES}
  )
  target_compile_definitions(${target_name}
    PUBLIC
      ${NGIN_BASE_PLATFORM_DEFINITIONS}
    PRIVATE
      ${NGIN_BASE_${component_upper}_PRIVATE_DEFINITIONS}
  )
  target_link_libraries(${target_name}
    PRIVATE
      NGIN.Base.BuildOptions
      ${NGIN_BASE_${component_upper}_PRIVATE_LIBRARIES}
  )
  ngin_enable_warnings(${target_name})

  if(linkage STREQUAL "Shared")
    set_target_properties(${target_name} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_definitions(${target_name}
      PUBLIC NGIN_${component_upper}_SHARED
      PRIVATE NGIN_${component_upper}_SHARED_BUILD
    )
    if(NOT WIN32)
      set_target_properties(${target_name} PROPERTIES
        SOVERSION ${PROJECT_VERSION_MAJOR}
        VERSION ${PROJECT_VERSION}
      )
    else()
      set_target_properties(${target_name} PROPERTIES
        ARCHIVE_OUTPUT_NAME "NGINBase${component}Shared"
      )
    endif()
  endif()
endfunction()

foreach(component IN LISTS NGIN_BASE_ENABLED_COMPONENTS)
  string(TOUPPER "${component}" component_upper)

  if(NGIN_BASE_BUILD_STATIC)
    set(target_name "NGIN.Base.${component}.Static")
    add_library(${target_name} STATIC ${NGIN_BASE_${component_upper}_SOURCES})
    ngin_base_configure_component_target(${target_name} ${component} Static)
    foreach(dependency IN LISTS NGIN_BASE_${component_upper}_DEPENDENCIES)
      target_link_libraries(${target_name} PUBLIC NGIN.Base.${dependency}.Static)
    endforeach()
    add_library(NGIN::Base::${component}::Static ALIAS ${target_name})
    list(APPEND NGIN_BASE_STATIC_COMPONENT_TARGETS ${target_name})
    list(APPEND NGIN_BASE_EXPORT_TARGETS ${target_name})
  endif()

  if(NGIN_BASE_BUILD_SHARED)
    set(target_name "NGIN.Base.${component}.Shared")
    add_library(${target_name} SHARED ${NGIN_BASE_${component_upper}_SOURCES})
    ngin_base_configure_component_target(${target_name} ${component} Shared)
    foreach(dependency IN LISTS NGIN_BASE_${component_upper}_DEPENDENCIES)
      target_link_libraries(${target_name} PUBLIC NGIN.Base.${dependency}.Shared)
    endforeach()
    add_library(NGIN::Base::${component}::Shared ALIAS ${target_name})
    list(APPEND NGIN_BASE_SHARED_COMPONENT_TARGETS ${target_name})
    list(APPEND NGIN_BASE_EXPORT_TARGETS ${target_name})
  endif()
endforeach()

if(NGIN_BASE_BUILD_STATIC)
  add_library(NGIN.Base.Static INTERFACE)
  target_link_libraries(NGIN.Base.Static INTERFACE ${NGIN_BASE_STATIC_COMPONENT_TARGETS})
  set_target_properties(NGIN.Base.Static PROPERTIES EXPORT_NAME BaseStatic)
  add_library(NGIN::Base::Static ALIAS NGIN.Base.Static)
  list(APPEND NGIN_BASE_EXPORT_TARGETS NGIN.Base.Static)
endif()

if(NGIN_BASE_BUILD_SHARED)
  add_library(NGIN.Base.Shared INTERFACE)
  target_link_libraries(NGIN.Base.Shared INTERFACE ${NGIN_BASE_SHARED_COMPONENT_TARGETS})
  set_target_properties(NGIN.Base.Shared PROPERTIES EXPORT_NAME BaseShared)
  add_library(NGIN::Base::Shared ALIAS NGIN.Base.Shared)
  list(APPEND NGIN_BASE_EXPORT_TARGETS NGIN.Base.Shared)
endif()

foreach(component IN LISTS NGIN_BASE_ENABLED_COMPONENTS)
  if(NGIN_BASE_BUILD_SHARED)
    add_library(NGIN::Base::${component} ALIAS NGIN.Base.${component}.Shared)
  else()
    add_library(NGIN::Base::${component} ALIAS NGIN.Base.${component}.Static)
  endif()
endforeach()

if(NGIN_BASE_BUILD_SHARED)
  add_library(NGIN::Base ALIAS NGIN.Base.Shared)
else()
  add_library(NGIN::Base ALIAS NGIN.Base.Static)
endif()

if(NGIN_BASE_BUILD_STATIC AND NGIN_BASE_BUILD_SHARED)
  message(STATUS "Both static and shared are enabled; preferred component and aggregate aliases select shared.")
endif()

ngin_base_apply_lto()
ngin_base_link_platform_libraries()

string(JOIN ", " NGIN_BASE_ENABLED_TARGETS ${NGIN_BASE_EXPORT_TARGETS})
message(STATUS "NGIN.Base targets enabled: ${NGIN_BASE_ENABLED_TARGETS}")
