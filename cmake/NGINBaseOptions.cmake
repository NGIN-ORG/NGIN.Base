#-------------------------------------------------------------------------------
# User-facing build options
#-------------------------------------------------------------------------------
option(NGIN_BASE_BUILD_STATIC "Build NGIN.Base as a static library" ON)
option(NGIN_BASE_BUILD_SHARED "Build NGIN.Base as a shared library" OFF)
option(NGIN_BASE_BUILD_TESTS "Build NGIN.Base tests" ON)
option(NGIN_BASE_BUILD_EXAMPLES "Build NGIN.Base examples" ON)
option(NGIN_BASE_BUILD_BENCHMARKS "Build NGIN.Base benchmarks" ON)
option(NGIN_BASE_DEVELOPMENT_MODE "Deprecated compatibility toggle for non-header builds" OFF)

# Extended developer / diagnostics options.
option(NGIN_BASE_ENABLE_ASAN "Enable Address + Undefined Sanitizers (GNU/Clang)" OFF)
option(NGIN_BASE_ENABLE_TSAN "Enable ThreadSanitizer (GNU/Clang)" OFF)
option(NGIN_BASE_ENABLE_LTO "Enable Link Time Optimization for Release/RelWithDebInfo" OFF)
option(NGIN_BASE_STRICT_WARNINGS "Enable extra warning flags" ON)
option(NGIN_BASE_ALL_FEATURES "Convenience: enable tests + examples + benchmarks" OFF)
option(NGIN_BASE_EXPORT_COMPILE_COMMANDS "Generate compile_commands.json" ON)
option(NGIN_BASE_CRYPTO_OPENSSL "Enable optional OpenSSL-backed crypto algorithms" OFF)
option(NGIN_CRYPTO_WITH_OPENSSL "Enable optional OpenSSL-backed crypto algorithms for split Crypto targets" OFF)
option(NGIN_BASE_BUILD_SPLIT_TARGETS "Create transitional NGIN::Crypto/Net/Serialization targets" ON)

set(NGIN_BASE_CLANG_GCC_TOOLCHAIN "" CACHE PATH "Clang (Linux): GCC toolchain root passed via --gcc-toolchain")
set(NGIN_BASE_FIBER_BACKEND "default" CACHE STRING "Fiber backend: default/ucontext/winfiber/custom_asm")
set_property(CACHE NGIN_BASE_FIBER_BACKEND PROPERTY STRINGS default ucontext winfiber custom_asm)

if(NGIN_BASE_ALL_FEATURES)
  set(NGIN_BASE_BUILD_TESTS ON CACHE BOOL "Build tests" FORCE)
  set(NGIN_BASE_BUILD_EXAMPLES ON CACHE BOOL "Build examples" FORCE)
  set(NGIN_BASE_BUILD_BENCHMARKS ON CACHE BOOL "Build benchmarks" FORCE)
endif()

if(NGIN_BASE_ENABLE_ASAN AND NGIN_BASE_ENABLE_TSAN)
  message(FATAL_ERROR "Cannot enable ASAN and TSAN simultaneously")
endif()

if(NGIN_BASE_CRYPTO_OPENSSL AND NOT NGIN_CRYPTO_WITH_OPENSSL)
  set(NGIN_CRYPTO_WITH_OPENSSL ON CACHE BOOL "Enable optional OpenSSL-backed crypto algorithms" FORCE)
elseif(NGIN_CRYPTO_WITH_OPENSSL AND NOT NGIN_BASE_CRYPTO_OPENSSL)
  set(NGIN_BASE_CRYPTO_OPENSSL ON CACHE BOOL "Enable optional OpenSSL-backed crypto algorithms" FORCE)
endif()

if(NGIN_BASE_DEVELOPMENT_MODE)
  message(DEPRECATION "NGIN_BASE_DEVELOPMENT_MODE is deprecated; enable explicit static/shared options instead.")
  if(NOT (NGIN_BASE_BUILD_STATIC OR NGIN_BASE_BUILD_SHARED))
    set(NGIN_BASE_BUILD_STATIC ON CACHE BOOL "Build NGIN.Base as a static library" FORCE)
  endif()
endif()

if(NOT NGIN_BASE_BUILD_STATIC AND NOT NGIN_BASE_BUILD_SHARED)
  message(FATAL_ERROR "At least one of static or shared build options must be enabled.")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ${NGIN_BASE_EXPORT_COMPILE_COMMANDS})
