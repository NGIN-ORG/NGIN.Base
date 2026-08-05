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
  NetTLS
)

set(NGIN_BASE_FOUNDATION_DEPENDENCIES)
set(NGIN_BASE_EXECUTION_DEPENDENCIES Foundation)
set(NGIN_BASE_IO_DEPENDENCIES Foundation Execution)
set(NGIN_BASE_SERIALIZATION_DEPENDENCIES Foundation IO)
set(NGIN_BASE_CRYPTO_DEPENDENCIES Foundation IO Serialization)
set(NGIN_BASE_NET_DEPENDENCIES Foundation Execution IO)
set(NGIN_BASE_NETTLS_DEPENDENCIES Net Crypto)

set(_ngin_base_requested_components ${NGIN_BASE_BUILD_COMPONENTS})
if(NOT _ngin_base_requested_components OR
   _ngin_base_requested_components STREQUAL "all")
  set(_ngin_base_requested_components ${NGIN_BASE_COMPONENTS})
endif()

if(NGIN_BASE_BUILD_TESTS OR NGIN_BASE_BUILD_EXAMPLES OR
   NGIN_BASE_BUILD_BENCHMARKS OR NGIN_BASE_BUILD_FUZZERS)
  set(_ngin_base_requested_components ${NGIN_BASE_COMPONENTS})
endif()

if(NGIN_BASE_CRYPTO_WITH_CNG OR NGIN_BASE_CRYPTO_WITH_APPLE OR
   NGIN_BASE_CRYPTO_WITH_OPENSSL OR NGIN_BASE_CRYPTO_WITH_BORINGSSL OR
   NGIN_BASE_CRYPTO_WITH_LIBSODIUM OR NGIN_BASE_CRYPTO_REQUIRE_PROVIDER OR
   NGIN_BASE_CRYPTO_REQUIRE_ALGORITHMS)
  list(APPEND _ngin_base_requested_components Crypto)
endif()

if(NGIN_BASE_TLS_WITH_OPENSSL OR NGIN_BASE_TLS_REQUIRE_PROVIDER)
  list(APPEND _ngin_base_requested_components NetTLS)
endif()

list(REMOVE_DUPLICATES _ngin_base_requested_components)
foreach(_ngin_component IN LISTS _ngin_base_requested_components)
  if(NOT _ngin_component IN_LIST NGIN_BASE_COMPONENTS)
    message(FATAL_ERROR
      "Unknown NGIN.Base component '${_ngin_component}' in NGIN_BASE_BUILD_COMPONENTS. "
      "Known components: ${NGIN_BASE_COMPONENTS}")
  endif()
endforeach()

set(NGIN_BASE_ENABLED_COMPONENTS ${_ngin_base_requested_components})
set(_ngin_base_component_closure_changed ON)
while(_ngin_base_component_closure_changed)
  set(_ngin_base_component_closure_changed OFF)
  foreach(_ngin_component IN LISTS NGIN_BASE_ENABLED_COMPONENTS)
    string(TOUPPER "${_ngin_component}" _ngin_component_upper)
    foreach(_ngin_dependency IN LISTS NGIN_BASE_${_ngin_component_upper}_DEPENDENCIES)
      if(NOT _ngin_dependency IN_LIST NGIN_BASE_ENABLED_COMPONENTS)
        list(APPEND NGIN_BASE_ENABLED_COMPONENTS "${_ngin_dependency}")
        set(_ngin_base_component_closure_changed ON)
      endif()
    endforeach()
  endforeach()
endwhile()

set(_ngin_base_ordered_enabled_components)
foreach(_ngin_component IN LISTS NGIN_BASE_COMPONENTS)
  if(_ngin_component IN_LIST NGIN_BASE_ENABLED_COMPONENTS)
    list(APPEND _ngin_base_ordered_enabled_components "${_ngin_component}")
  endif()
endforeach()
set(NGIN_BASE_ENABLED_COMPONENTS ${_ngin_base_ordered_enabled_components})

string(JOIN ", " _ngin_base_enabled_component_text ${NGIN_BASE_ENABLED_COMPONENTS})
message(STATUS "NGIN.Base component closure: ${_ngin_base_enabled_component_text}")
