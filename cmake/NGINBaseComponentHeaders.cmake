#-------------------------------------------------------------------------------
# Public header ownership and active source validation
#-------------------------------------------------------------------------------

set(NGIN_BASE_COMPONENT_NAMES
  Foundation
  Execution
  IO
  Serialization
  Crypto
  Net
  NetTLS
)

set(NGIN_BASE_FOUNDATION_PUBLIC_HEADER_ROOTS
  Containers
  Exceptions
  Hashing
  Math
  Memory
  Meta
  SIMD
  Sync
  Text
  Time
  Utilities
)
set(NGIN_BASE_FOUNDATION_PUBLIC_HEADER_FILES
  Benchmark.hpp
  BaseVersion.hpp
  Defines.hpp
  NGIN.hpp
  Primitives.hpp
  SIMD.hpp
  Timer.hpp
  Units.hpp
)

set(NGIN_BASE_EXECUTION_PUBLIC_HEADER_ROOTS Async Execution)
set(NGIN_BASE_EXECUTION_PUBLIC_HEADER_FILES Execution.hpp)
set(NGIN_BASE_IO_PUBLIC_HEADER_ROOTS IO)
set(NGIN_BASE_IO_PUBLIC_HEADER_FILES IO.hpp)
set(NGIN_BASE_SERIALIZATION_PUBLIC_HEADER_ROOTS Serialization)
set(NGIN_BASE_SERIALIZATION_PUBLIC_HEADER_FILES Serialization.hpp)
set(NGIN_BASE_CRYPTO_PUBLIC_HEADER_ROOTS Crypto)
set(NGIN_BASE_NET_PUBLIC_HEADER_ROOTS
  Net/Runtime
  Net/Sockets
  Net/Transport
  Net/Types
)
set(NGIN_BASE_NET_PUBLIC_HEADER_FILES
  Net.hpp
  Net/ResolveError.hpp
  Net/ResolvedAddress.hpp
  Net/ResolveOptions.hpp
  Net/ResolveSocketType.hpp
  Net/Resolve.hpp
  Net/ResolverDriver.hpp
)
set(NGIN_BASE_NETTLS_PUBLIC_HEADER_ROOTS Net/TLS)
set(NGIN_BASE_NETTLS_PUBLIC_HEADER_FILES NetTLS.hpp)

file(GLOB_RECURSE NGIN_BASE_PUBLIC_HEADERS CONFIGURE_DEPENDS
  "${NGIN_BASE_ROOT_DIR}/include/NGIN/*.hpp"
)
list(SORT NGIN_BASE_PUBLIC_HEADERS)
set(NGIN_BASE_PUBLIC_CONTRACT_HEADERS)

foreach(public_header IN LISTS NGIN_BASE_PUBLIC_HEADERS)
  file(RELATIVE_PATH relative_header "${NGIN_BASE_ROOT_DIR}/include/NGIN" "${public_header}")
  string(REPLACE "\\" "/" relative_header "${relative_header}")
  set(header_components)

  if(NOT relative_header MATCHES "(^|/)detail/")
    list(APPEND NGIN_BASE_PUBLIC_CONTRACT_HEADERS "${public_header}")
  endif()

  foreach(component IN LISTS NGIN_BASE_COMPONENT_NAMES)
    string(TOUPPER "${component}" component_upper)
    set(root_variable "NGIN_BASE_${component_upper}_PUBLIC_HEADER_ROOTS")
    set(file_variable "NGIN_BASE_${component_upper}_PUBLIC_HEADER_FILES")

    foreach(header_root IN LISTS ${root_variable})
      string(FIND "${relative_header}" "${header_root}/" root_position)
      if(root_position EQUAL 0)
        list(APPEND header_components "${component}")
      endif()
    endforeach()

    if(relative_header IN_LIST ${file_variable})
      list(APPEND header_components "${component}")
    endif()
  endforeach()

  list(REMOVE_DUPLICATES header_components)
  list(LENGTH header_components component_count)
  if(NOT component_count EQUAL 1)
    message(FATAL_ERROR
      "Public header '${relative_header}' must belong to exactly one NGIN.Base component; "
      "matched: '${header_components}'"
    )
  endif()

  list(GET header_components 0 header_component)
  string(TOUPPER "${header_component}" header_component_upper)
  list(APPEND NGIN_BASE_${header_component_upper}_PUBLIC_HEADERS "${public_header}")
endforeach()

# Public headers may include only their own component or an explicitly allowed
# lower-level component. This converts the documented direction into a
# configure-time contract before the compiled-target migration.
set(NGIN_BASE_FOUNDATION_ALLOWED_HEADER_DEPENDENCIES Foundation)
set(NGIN_BASE_EXECUTION_ALLOWED_HEADER_DEPENDENCIES Foundation Execution)
set(NGIN_BASE_IO_ALLOWED_HEADER_DEPENDENCIES Foundation Execution IO)
set(NGIN_BASE_SERIALIZATION_ALLOWED_HEADER_DEPENDENCIES Foundation IO Serialization)
set(NGIN_BASE_CRYPTO_ALLOWED_HEADER_DEPENDENCIES Foundation IO Serialization Crypto)
set(NGIN_BASE_NET_ALLOWED_HEADER_DEPENDENCIES Foundation Execution IO Net)
set(NGIN_BASE_NETTLS_ALLOWED_HEADER_DEPENDENCIES Foundation Execution IO Crypto Net NetTLS)

foreach(component IN LISTS NGIN_BASE_COMPONENT_NAMES)
  string(TOUPPER "${component}" component_upper)
  foreach(public_header IN LISTS NGIN_BASE_${component_upper}_PUBLIC_HEADERS)
    file(READ "${public_header}" header_content)
    string(REGEX MATCHALL "#[ \t]*include[ \t]*[<\"]NGIN/[^>\"]+[>\"]" header_includes "${header_content}")
    foreach(header_include IN LISTS header_includes)
      string(REGEX REPLACE ".*[<\"]NGIN/([^>\"]+)[>\"].*" "\\1" included_relative "${header_include}")
      set(included_path "${NGIN_BASE_ROOT_DIR}/include/NGIN/${included_relative}")
      if(NOT EXISTS "${included_path}")
        continue()
      endif()

      set(included_component "")
      foreach(candidate IN LISTS NGIN_BASE_COMPONENT_NAMES)
        string(TOUPPER "${candidate}" candidate_upper)
        if(included_path IN_LIST NGIN_BASE_${candidate_upper}_PUBLIC_HEADERS)
          set(included_component "${candidate}")
          break()
        endif()
      endforeach()

      if(included_component AND
         NOT included_component IN_LIST NGIN_BASE_${component_upper}_ALLOWED_HEADER_DEPENDENCIES)
        file(RELATIVE_PATH relative_header "${NGIN_BASE_ROOT_DIR}/include/NGIN" "${public_header}")
        message(FATAL_ERROR
          "NGIN.Base public-header boundary violation: ${relative_header} (${component}) "
          "includes ${included_relative} (${included_component})"
        )
      endif()
    endforeach()
  endforeach()
endforeach()

# Every source selected for the active platform must exist and have one owner.
set(active_component_sources)
foreach(component IN LISTS NGIN_BASE_COMPONENT_NAMES)
  string(TOUPPER "${component}" component_upper)
  set(source_variable "NGIN_BASE_${component_upper}_SOURCES")
  foreach(component_source IN LISTS ${source_variable})
    if(NOT EXISTS "${component_source}")
      message(FATAL_ERROR "NGIN.Base ${component} source does not exist: ${component_source}")
    endif()
    if(component_source IN_LIST active_component_sources)
      message(FATAL_ERROR "NGIN.Base source has more than one component owner: ${component_source}")
    endif()
    list(APPEND active_component_sources "${component_source}")
  endforeach()
endforeach()

list(LENGTH NGIN_BASE_PUBLIC_HEADERS NGIN_BASE_PUBLIC_HEADER_COUNT)
list(LENGTH NGIN_BASE_PUBLIC_CONTRACT_HEADERS NGIN_BASE_PUBLIC_CONTRACT_HEADER_COUNT)
message(STATUS
  "NGIN.Base header ownership validated: ${NGIN_BASE_PUBLIC_HEADER_COUNT} installed, "
  "${NGIN_BASE_PUBLIC_CONTRACT_HEADER_COUNT} public contract headers"
)
