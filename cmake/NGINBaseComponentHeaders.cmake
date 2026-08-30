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
  Containers.hpp
  Defines.hpp
  Exceptions.hpp
  Hashing.hpp
  Math.hpp
  Memory.hpp
  Meta.hpp
  NGIN.hpp
  Primitives.hpp
  SIMD.hpp
  Sync.hpp
  Text.hpp
  Time.hpp
  Timer.hpp
  Units.hpp
  Utilities.hpp
)

set(NGIN_BASE_EXECUTION_PUBLIC_HEADER_ROOTS Async Execution)
set(NGIN_BASE_EXECUTION_PUBLIC_HEADER_FILES Async.hpp Execution.hpp)
set(NGIN_BASE_IO_PUBLIC_HEADER_ROOTS IO)
set(NGIN_BASE_IO_PUBLIC_HEADER_FILES IO.hpp)
set(NGIN_BASE_SERIALIZATION_PUBLIC_HEADER_ROOTS Serialization)
set(NGIN_BASE_SERIALIZATION_PUBLIC_HEADER_FILES Serialization.hpp)
set(NGIN_BASE_CRYPTO_PUBLIC_HEADER_ROOTS Crypto)
set(NGIN_BASE_CRYPTO_PUBLIC_HEADER_FILES Crypto.hpp)
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
  Net/Runtime.hpp
  Net/Sockets.hpp
  Net/Transport.hpp
  Net/Types.hpp
)
set(NGIN_BASE_NETTLS_PUBLIC_HEADER_ROOTS Net/TLS)
set(NGIN_BASE_NETTLS_PUBLIC_HEADER_FILES Net/TLS.hpp NetTLS.hpp)

file(GLOB_RECURSE NGIN_BASE_PUBLIC_HEADERS CONFIGURE_DEPENDS
  "${NGIN_BASE_ROOT_DIR}/include/NGIN/*.hpp"
)
list(SORT NGIN_BASE_PUBLIC_HEADERS)
set(NGIN_BASE_PUBLIC_CONTRACT_HEADERS)

# Public module directories and their umbrellas are parallel: NGIN/Foo/ is
# aggregated by NGIN/Foo.hpp, including nested modules. Implementation-only
# detail directories do not expose umbrellas.
file(GLOB_RECURSE NGIN_BASE_PUBLIC_HEADER_ENTRIES CONFIGURE_DEPENDS LIST_DIRECTORIES true
  "${NGIN_BASE_ROOT_DIR}/include/NGIN/*"
)
foreach(public_entry IN LISTS NGIN_BASE_PUBLIC_HEADER_ENTRIES)
  if(IS_DIRECTORY "${public_entry}")
    file(RELATIVE_PATH relative_directory "${NGIN_BASE_ROOT_DIR}/include/NGIN" "${public_entry}")
    string(REPLACE "\\" "/" relative_directory "${relative_directory}")
    if(NOT relative_directory MATCHES "(^|/)detail($|/)")
      set(umbrella_header "${public_entry}.hpp")
      if(NOT EXISTS "${umbrella_header}")
        message(FATAL_ERROR
          "Public module directory 'NGIN/${relative_directory}/' requires parallel umbrella "
          "'NGIN/${relative_directory}.hpp'"
        )
      endif()
    endif()
  endif()
endforeach()

foreach(public_header IN LISTS NGIN_BASE_PUBLIC_HEADERS)
  file(RELATIVE_PATH relative_header "${NGIN_BASE_ROOT_DIR}/include/NGIN" "${public_header}")
  string(REPLACE "\\" "/" relative_header "${relative_header}")
  set(header_components)

  if(relative_header MATCHES "/" AND NOT relative_header MATCHES "(^|/)detail/")
    get_filename_component(header_stem "${relative_header}" NAME_WE)
    get_filename_component(header_directory "${relative_header}" DIRECTORY)
    get_filename_component(directory_name "${header_directory}" NAME)
    if(header_stem STREQUAL directory_name)
      message(FATAL_ERROR
        "Nested self-named header 'NGIN/${relative_header}' is not a valid umbrella location; "
        "use the parallel 'NGIN/${header_directory}.hpp' surface"
      )
    endif()
  endif()

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
