#-------------------------------------------------------------------------------
# Component ownership and dependency graph
#-------------------------------------------------------------------------------
set(NGIN_BASE_COMPONENTS
  Foundation
  Execution
  IO
  Serialization
  Crypto
  Net
)

set(NGIN_BASE_FOUNDATION_DEPENDENCIES)
set(NGIN_BASE_EXECUTION_DEPENDENCIES Foundation)
set(NGIN_BASE_IO_DEPENDENCIES Foundation Execution)
set(NGIN_BASE_SERIALIZATION_DEPENDENCIES Foundation IO)
set(NGIN_BASE_CRYPTO_DEPENDENCIES Foundation IO Serialization)
set(NGIN_BASE_NET_DEPENDENCIES Foundation Execution IO Crypto)

foreach(_ngin_component IN LISTS NGIN_BASE_COMPONENTS)
  string(TOUPPER "${_ngin_component}" _ngin_component_upper)
  if(NOT DEFINED NGIN_BASE_${_ngin_component_upper}_SOURCES)
    message(FATAL_ERROR "Missing source ownership list for component ${_ngin_component}")
  endif()
endforeach()
