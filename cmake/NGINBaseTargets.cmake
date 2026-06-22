#-------------------------------------------------------------------------------
# Library targets
#-------------------------------------------------------------------------------
set(NGIN_BASE_EXPORT_TARGETS)
set(ngin_base_primary_target "")

if(NGIN_BASE_BUILD_STATIC OR NGIN_BASE_BUILD_SHARED)
  add_library(NGIN.Base.Object OBJECT ${NGIN_BASE_CORE_SOURCES})
  target_compile_features(NGIN.Base.Object PRIVATE cxx_std_23)
  set_target_properties(NGIN.Base.Object PROPERTIES CXX_EXTENSIONS OFF)
  target_include_directories(NGIN.Base.Object PRIVATE
    ${NGIN_BASE_ROOT_DIR}/include
    ${NGIN_BASE_PRIVATE_INCLUDE_DIRECTORIES}
  )
  target_compile_definitions(NGIN.Base.Object
    PRIVATE
      ${NGIN_BASE_PLATFORM_DEFINITIONS}
      ${NGIN_BASE_PRIVATE_DEFINITIONS}
  )
  target_link_libraries(NGIN.Base.Object PRIVATE NGIN.Base.BuildOptions ${NGIN_BASE_PRIVATE_LIBRARIES})
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
      $<BUILD_INTERFACE:${NGIN_BASE_ROOT_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
      ${NGIN_BASE_PRIVATE_INCLUDE_DIRECTORIES}
  )
  target_compile_definitions(NGIN.Base.Static
    PUBLIC
      ${NGIN_BASE_PLATFORM_DEFINITIONS}
  )
  set_target_properties(NGIN.Base.Static PROPERTIES
    OUTPUT_NAME "NGINBase"
    EXPORT_NAME BaseStatic
  )
  target_link_libraries(NGIN.Base.Static PRIVATE NGIN.Base.BuildOptions ${NGIN_BASE_PRIVATE_LIBRARIES})
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
      $<BUILD_INTERFACE:${NGIN_BASE_ROOT_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
      ${NGIN_BASE_PRIVATE_INCLUDE_DIRECTORIES}
  )
  target_compile_definitions(NGIN.Base.Shared
    PUBLIC
      ${NGIN_BASE_PLATFORM_DEFINITIONS}
      NGIN_BASE_SHARED
  )
  target_compile_definitions(NGIN.Base.Shared PRIVATE NGIN_BASE_SHARED_BUILD)
  set_target_properties(NGIN.Base.Shared PROPERTIES
    OUTPUT_NAME "NGINBase"
    EXPORT_NAME BaseShared
  )
  if(NOT WIN32)
    set_target_properties(NGIN.Base.Shared PROPERTIES
      SOVERSION ${PROJECT_VERSION_MAJOR}
      VERSION ${PROJECT_VERSION}
    )
  endif()
  target_link_libraries(NGIN.Base.Shared PRIVATE NGIN.Base.BuildOptions ${NGIN_BASE_PRIVATE_LIBRARIES})
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

ngin_base_apply_lto()
ngin_base_link_platform_libraries()

string(JOIN ", " NGIN_BASE_ENABLED_TARGETS ${NGIN_BASE_EXPORT_TARGETS})
message(STATUS "NGIN.Base targets enabled: ${NGIN_BASE_ENABLED_TARGETS}")
