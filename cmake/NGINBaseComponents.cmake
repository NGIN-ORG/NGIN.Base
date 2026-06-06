#-------------------------------------------------------------------------------
# Internal component boundary metadata
#-------------------------------------------------------------------------------
# These targets intentionally do not replace the current compatibility aggregate target.
# They document ownership and dependency direction for the future package split.

function(ngin_base_add_component component_name component_alias)
  add_library(${component_name} INTERFACE)
  add_library(${component_alias} ALIAS ${component_name})
  target_compile_features(${component_name} INTERFACE cxx_std_23)
  target_include_directories(${component_name}
    INTERFACE
      $<BUILD_INTERFACE:${NGIN_BASE_ROOT_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )
  set_target_properties(${component_name} PROPERTIES
    NGIN_BASE_COMPONENT TRUE
    NGIN_BASE_COMPONENT_SOURCES "${ARGN}"
  )
endfunction()

ngin_base_add_component(NGIN.Base.Foundation NGIN::Base::Foundation ${NGIN_BASE_FOUNDATION_SOURCES})
ngin_base_add_component(NGIN.Base.Execution NGIN::Base::Execution ${NGIN_BASE_EXECUTION_SOURCES})
ngin_base_add_component(NGIN.Base.IO NGIN::Base::IO ${NGIN_BASE_IO_SOURCES})
ngin_base_add_component(NGIN.Base.Crypto NGIN::Base::Crypto ${NGIN_BASE_CRYPTO_SOURCES})
ngin_base_add_component(NGIN.Base.Net NGIN::Base::Net ${NGIN_BASE_NET_SOURCES})
ngin_base_add_component(NGIN.Base.Serialization NGIN::Base::Serialization ${NGIN_BASE_SERIALIZATION_SOURCES})

target_link_libraries(NGIN.Base.Execution INTERFACE NGIN.Base.Foundation)
target_link_libraries(NGIN.Base.IO INTERFACE NGIN.Base.Foundation NGIN.Base.Execution)
target_link_libraries(NGIN.Base.Crypto INTERFACE NGIN.Base.Foundation NGIN.Base.IO)
target_link_libraries(NGIN.Base.Net INTERFACE NGIN.Base.Foundation NGIN.Base.IO NGIN.Base.Execution)
target_link_libraries(NGIN.Base.Serialization INTERFACE NGIN.Base.Foundation NGIN.Base.IO)

set_target_properties(NGIN.Base.Foundation PROPERTIES
  NGIN_BASE_ALLOWED_COMPONENT_DEPENDENCIES ""
)
set_target_properties(NGIN.Base.Execution PROPERTIES
  NGIN_BASE_ALLOWED_COMPONENT_DEPENDENCIES "NGIN.Base.Foundation"
)
set_target_properties(NGIN.Base.IO PROPERTIES
  NGIN_BASE_ALLOWED_COMPONENT_DEPENDENCIES "NGIN.Base.Foundation;NGIN.Base.Execution"
)
set_target_properties(NGIN.Base.Crypto PROPERTIES
  NGIN_BASE_ALLOWED_COMPONENT_DEPENDENCIES "NGIN.Base.Foundation;NGIN.Base.IO"
)
set_target_properties(NGIN.Base.Net PROPERTIES
  NGIN_BASE_ALLOWED_COMPONENT_DEPENDENCIES "NGIN.Base.Foundation;NGIN.Base.IO;NGIN.Base.Execution"
)
set_target_properties(NGIN.Base.Serialization PROPERTIES
  NGIN_BASE_ALLOWED_COMPONENT_DEPENDENCIES "NGIN.Base.Foundation;NGIN.Base.IO"
)
