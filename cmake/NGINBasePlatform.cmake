#-------------------------------------------------------------------------------
# Platform definitions and system libraries
#-------------------------------------------------------------------------------
set(NGIN_BASE_PLATFORM_DEFINITIONS
  $<$<STREQUAL:$<PLATFORM_ID>,Windows>:NGIN_PLATFORM_WINDOWS>
  $<$<STREQUAL:$<PLATFORM_ID>,Darwin>:NGIN_PLATFORM_MACOS>
  $<$<STREQUAL:$<PLATFORM_ID>,Linux>:NGIN_PLATFORM_LINUX>
)

if(NGIN_BASE_FIBER_BACKEND STREQUAL "ucontext")
  if(NOT UNIX)
    message(FATAL_ERROR "NGIN_BASE_FIBER_BACKEND=ucontext is only supported on UNIX builds")
  endif()
  list(APPEND NGIN_BASE_PLATFORM_DEFINITIONS NGIN_EXECUTION_FIBER_BACKEND=NGIN_EXECUTION_FIBER_BACKEND_UCONTEXT)
elseif(NGIN_BASE_FIBER_BACKEND STREQUAL "winfiber")
  if(NOT WIN32)
    message(FATAL_ERROR "NGIN_BASE_FIBER_BACKEND=winfiber is only supported on Windows builds")
  endif()
  list(APPEND NGIN_BASE_PLATFORM_DEFINITIONS NGIN_EXECUTION_FIBER_BACKEND=NGIN_EXECUTION_FIBER_BACKEND_WIN_FIBER)
elseif(NGIN_BASE_FIBER_BACKEND STREQUAL "custom_asm")
  if(NOT UNIX)
    message(FATAL_ERROR "NGIN_BASE_FIBER_BACKEND=custom_asm is only supported on UNIX builds")
  endif()
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "NGIN_BASE_FIBER_BACKEND=custom_asm is currently only supported on Linux")
  endif()
  if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64|aarch64|arm64)$")
    message(FATAL_ERROR "NGIN_BASE_FIBER_BACKEND=custom_asm currently requires x86_64 or aarch64")
  endif()
  list(APPEND NGIN_BASE_PLATFORM_DEFINITIONS NGIN_EXECUTION_FIBER_BACKEND=NGIN_EXECUTION_FIBER_BACKEND_CUSTOM_ASM)
elseif(NGIN_BASE_FIBER_BACKEND STREQUAL "default")
  # Use platform defaults from include/NGIN/Execution/Config.hpp.
else()
  message(FATAL_ERROR "Unknown NGIN_BASE_FIBER_BACKEND value: ${NGIN_BASE_FIBER_BACKEND}")
endif()

# On Windows, prevent the global min/max macros from <windows.h> from colliding with std::min/std::max.
if(WIN32)
  list(APPEND NGIN_BASE_PLATFORM_DEFINITIONS NOMINMAX)
endif()

function(ngin_base_link_if_target target_name visibility)
  if(TARGET ${target_name})
    target_link_libraries(${target_name} ${visibility} ${ARGN})
  endif()
endfunction()

function(ngin_base_find_windows_library output_variable)
  unset(_ngin_windows_library CACHE)
  unset(_ngin_windows_library)
  find_library(_ngin_windows_library NAMES ${ARGN})
  if(_ngin_windows_library)
    set(${output_variable} "${_ngin_windows_library}" PARENT_SCOPE)
    unset(_ngin_windows_library CACHE)
    return()
  endif()

  set(_ngin_windows_sdk_roots)
  if(DEFINED ENV{WindowsSdkDir} AND NOT "$ENV{WindowsSdkDir}" STREQUAL "")
    list(APPEND _ngin_windows_sdk_roots "$ENV{WindowsSdkDir}")
  endif()
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.24)
    cmake_host_system_information(
      RESULT _ngin_windows_sdk_registry_root
      QUERY WINDOWS_REGISTRY
        "HKLM/SOFTWARE/Microsoft/Windows Kits/Installed Roots"
      VALUE "KitsRoot10"
    )
    if(_ngin_windows_sdk_registry_root
       AND IS_DIRECTORY "${_ngin_windows_sdk_registry_root}")
      list(APPEND _ngin_windows_sdk_roots
        "${_ngin_windows_sdk_registry_root}")
    endif()
  endif()
  list(REMOVE_DUPLICATES _ngin_windows_sdk_roots)

  if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(_ngin_windows_sdk_arch x86)
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
    set(_ngin_windows_sdk_arch arm64)
  else()
    set(_ngin_windows_sdk_arch x64)
  endif()

  set(_ngin_windows_sdk_library_dirs)
  foreach(_ngin_windows_sdk_root IN LISTS _ngin_windows_sdk_roots)
    file(GLOB _ngin_windows_sdk_versions
      LIST_DIRECTORIES TRUE
      "${_ngin_windows_sdk_root}/Lib/*"
    )
    list(SORT _ngin_windows_sdk_versions COMPARE NATURAL ORDER DESCENDING)
    foreach(_ngin_windows_sdk_version IN LISTS _ngin_windows_sdk_versions)
      if(IS_DIRECTORY
         "${_ngin_windows_sdk_version}/um/${_ngin_windows_sdk_arch}")
        list(APPEND _ngin_windows_sdk_library_dirs
          "${_ngin_windows_sdk_version}/um/${_ngin_windows_sdk_arch}")
      endif()
    endforeach()
  endforeach()

  find_library(
    _ngin_windows_library
    NAMES ${ARGN}
    PATHS ${_ngin_windows_sdk_library_dirs}
    NO_DEFAULT_PATH
  )
  set(${output_variable} "${_ngin_windows_library}" PARENT_SCOPE)
  unset(_ngin_windows_library CACHE)
endfunction()

function(ngin_base_link_platform_libraries)
  # Windows: link Synchronization, Winsock, and BCrypt for WaitOnAddress/WakeByAddress*,
  # socket APIs, and platform secure random. For MSVC, rely on the toolchain's default library search
  # instead of find_library, which is brittle on hosted CI images.
  if(WIN32)
    if(MSVC)
      ngin_base_link_if_target(NGIN.Base.Foundation.Static PUBLIC Synchronization)
      ngin_base_link_if_target(NGIN.Base.Foundation.Shared PRIVATE Synchronization)
      ngin_base_link_if_target(NGIN.Base.Net.Static PUBLIC Ws2_32)
      ngin_base_link_if_target(NGIN.Base.Net.Shared PRIVATE Ws2_32)
      ngin_base_link_if_target(NGIN.Base.Crypto.Static PUBLIC Bcrypt Crypt32)
      ngin_base_link_if_target(NGIN.Base.Crypto.Shared PRIVATE Bcrypt Crypt32)
    else()
      ngin_base_find_windows_library(
        _ngin_synchronization_lib Synchronization synchronization)
      if(_ngin_synchronization_lib)
        ngin_base_link_if_target(NGIN.Base.Foundation.Static PUBLIC ${_ngin_synchronization_lib})
        ngin_base_link_if_target(NGIN.Base.Foundation.Shared PRIVATE ${_ngin_synchronization_lib})
      else()
        message(WARNING "Windows Synchronization.lib not found; WaitOnAddress/WakeByAddress* will fail to link")
      endif()

      ngin_base_find_windows_library(_ngin_ws2_32_lib Ws2_32 ws2_32)
      if(_ngin_ws2_32_lib)
        ngin_base_link_if_target(NGIN.Base.Net.Static PUBLIC ${_ngin_ws2_32_lib})
        ngin_base_link_if_target(NGIN.Base.Net.Shared PRIVATE ${_ngin_ws2_32_lib})
      else()
        message(WARNING "Windows Ws2_32.lib not found; socket APIs will fail to link")
      endif()

      ngin_base_find_windows_library(_ngin_bcrypt_lib Bcrypt bcrypt)
      if(_ngin_bcrypt_lib)
        ngin_base_link_if_target(NGIN.Base.Crypto.Static PUBLIC ${_ngin_bcrypt_lib})
        ngin_base_link_if_target(NGIN.Base.Crypto.Shared PRIVATE ${_ngin_bcrypt_lib})
      else()
        message(WARNING "Windows Bcrypt.lib not found; secure random APIs will fail to link")
      endif()

      ngin_base_find_windows_library(_ngin_crypt32_lib Crypt32 crypt32)
      if(_ngin_crypt32_lib)
        ngin_base_link_if_target(NGIN.Base.Crypto.Static PUBLIC ${_ngin_crypt32_lib})
        ngin_base_link_if_target(NGIN.Base.Crypto.Shared PRIVATE ${_ngin_crypt32_lib})
      else()
        message(WARNING "Windows Crypt32.lib not found; certificate store APIs will fail to link")
      endif()
    endif()
  endif()

  if(APPLE)
    find_library(_ngin_security_framework Security)
    if(_ngin_security_framework)
      ngin_base_link_if_target(NGIN.Base.Crypto.Static PUBLIC ${_ngin_security_framework})
      ngin_base_link_if_target(NGIN.Base.Crypto.Shared PRIVATE ${_ngin_security_framework})
    else()
      message(WARNING "Apple Security framework not found; secure random APIs will fail to link")
    endif()

    find_library(_ngin_corefoundation_framework CoreFoundation)
    if(_ngin_corefoundation_framework)
      ngin_base_link_if_target(NGIN.Base.Crypto.Static PUBLIC ${_ngin_corefoundation_framework})
      ngin_base_link_if_target(NGIN.Base.Crypto.Shared PRIVATE ${_ngin_corefoundation_framework})
    else()
      message(WARNING "Apple CoreFoundation framework not found; certificate store APIs will fail to link")
    endif()
  endif()

  set(_ngin_need_atomic FALSE)
  if(UNIX AND NOT APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    if(CMAKE_CROSSCOMPILING)
      find_library(_ngin_atomic_lib atomic)
      if(_ngin_atomic_lib)
        set(_ngin_need_atomic TRUE)
      endif()
    else()
      include(CheckCXXSourceCompiles)
      string(CONCAT _ngin_atomic_test_source
        "#include <atomic>\n"
        "#include <cstddef>\n"
        "int main() {\n"
        "  std::atomic<std::size_t> v {0};\n"
        "  v.fetch_add(1, std::memory_order_relaxed);\n"
        "  return static_cast<int>(v.load(std::memory_order_relaxed));\n"
        "}\n"
      )
      set(_ngin_required_quiet_prev "${CMAKE_REQUIRED_QUIET}")
      set(CMAKE_REQUIRED_QUIET TRUE)
      check_cxx_source_compiles("${_ngin_atomic_test_source}" NGIN_BASE_ATOMIC_LINKS_WITHOUT_LIBATOMIC)
      if(NOT NGIN_BASE_ATOMIC_LINKS_WITHOUT_LIBATOMIC)
        set(CMAKE_REQUIRED_LIBRARIES atomic)
        check_cxx_source_compiles("${_ngin_atomic_test_source}" NGIN_BASE_ATOMIC_LINKS_WITH_LIBATOMIC)
        if(NGIN_BASE_ATOMIC_LINKS_WITH_LIBATOMIC)
          set(_ngin_need_atomic TRUE)
        endif()
        unset(CMAKE_REQUIRED_LIBRARIES)
      endif()
      set(CMAKE_REQUIRED_QUIET "${_ngin_required_quiet_prev}")
      unset(_ngin_required_quiet_prev)
    endif()
  endif()

  if(_ngin_need_atomic)
    ngin_base_link_if_target(NGIN.Base.Foundation.Static PUBLIC atomic)
    ngin_base_link_if_target(NGIN.Base.Foundation.Shared PRIVATE atomic)
  endif()
endfunction()

function(ngin_base_apply_lto)
  if(NGIN_BASE_ENABLE_LTO)
    if(NGIN_BASE_ENABLE_ASAN OR NGIN_BASE_ENABLE_TSAN)
      message(WARNING "LTO requested but sanitizers are enabled; skipping IPO/LTO.")
    else()
      include(CheckIPOSupported)
      check_ipo_supported(RESULT NGIN_HAS_IPO OUTPUT NGIN_IPO_ERR)
      if(NGIN_HAS_IPO)
        set(_ngin_ipo_enabled_targets)
        foreach(tgt IN LISTS NGIN_BASE_STATIC_COMPONENT_TARGETS NGIN_BASE_SHARED_COMPONENT_TARGETS)
          if(TARGET ${tgt})
            foreach(cfg IN ITEMS Release RelWithDebInfo MinSizeRel)
              set_property(TARGET ${tgt} PROPERTY INTERPROCEDURAL_OPTIMIZATION_${cfg} ON)
            endforeach()
            list(APPEND _ngin_ipo_enabled_targets ${tgt})
          endif()
        endforeach()
        if(_ngin_ipo_enabled_targets)
          string(JOIN ", " _ngin_ipo_enabled_targets_str ${_ngin_ipo_enabled_targets})
          message(STATUS "IPO/LTO enabled for: ${_ngin_ipo_enabled_targets_str}")
        endif()
      else()
        message(WARNING "LTO requested but not supported: ${NGIN_IPO_ERR}")
      endif()
    endif()
  endif()
endfunction()
