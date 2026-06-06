#-------------------------------------------------------------------------------
# Transitional split package targets
#-------------------------------------------------------------------------------
# These targets are deliberately EXCLUDE_FROM_ALL while NGIN::Base remains a compatibility
# aggregate that still contains every subsystem. They let package wrappers and users start
# depending on the future target names without changing the default Base build yet.

function(ngin_base_add_split_static_target target_name alias_name output_name)
  add_library(${target_name} STATIC EXCLUDE_FROM_ALL ${ARGN})
  add_library(${alias_name} ALIAS ${target_name})
  target_compile_features(${target_name} PUBLIC cxx_std_23)
  set_target_properties(${target_name} PROPERTIES
    CXX_EXTENSIONS OFF
    OUTPUT_NAME "${output_name}"
  )
  target_include_directories(${target_name}
    PUBLIC
      $<BUILD_INTERFACE:${NGIN_BASE_ROOT_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  target_compile_definitions(${target_name}
    PUBLIC
      ${NGIN_BASE_PLATFORM_DEFINITIONS}
    PRIVATE
      ${NGIN_BASE_PRIVATE_DEFINITIONS}
  )
  target_link_libraries(${target_name}
    PUBLIC
      NGIN::Base
    PRIVATE
      NGIN.Base.BuildOptions
      ${NGIN_BASE_PRIVATE_LIBRARIES}
  )
  ngin_enable_warnings(${target_name})
endfunction()

if(NGIN_BASE_BUILD_SPLIT_TARGETS)
  if(NOT TARGET NGIN::Crypto)
    ngin_base_add_split_static_target(NGIN.Crypto.Static NGIN::Crypto NGINCrypto ${NGIN_BASE_CRYPTO_SOURCES})
  endif()

  if(NOT TARGET NGIN::Net)
    ngin_base_add_split_static_target(NGIN.Net.Static NGIN::Net NGINNet ${NGIN_BASE_NET_SOURCES})
  endif()

  if(NOT TARGET NGIN::Serialization)
    ngin_base_add_split_static_target(
      NGIN.Serialization.Static
      NGIN::Serialization
      NGINSerialization
      ${NGIN_BASE_SERIALIZATION_SOURCES}
    )
  endif()
endif()
