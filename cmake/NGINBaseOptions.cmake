#-------------------------------------------------------------------------------
# User-facing build options
#-------------------------------------------------------------------------------
option(NGIN_BASE_BUILD_STATIC "Build NGIN.Base as a static library" ON)
option(NGIN_BASE_BUILD_SHARED "Build NGIN.Base as a shared library" OFF)
option(NGIN_BASE_BUILD_TESTS "Build NGIN.Base tests" ${PROJECT_IS_TOP_LEVEL})
option(NGIN_BASE_BUILD_EXAMPLES "Build NGIN.Base examples" OFF)
option(NGIN_BASE_BUILD_BENCHMARKS "Build NGIN.Base benchmarks" OFF)
option(NGIN_BASE_BUILD_FUZZERS "Build optional LLVM libFuzzer harnesses for parser-heavy components" OFF)
set(NGIN_BASE_BUILD_COMPONENTS "all" CACHE STRING
  "Components to build: all, or a semicolon-separated subset of Foundation;Execution;IO;Serialization;Crypto;Net;NetTLS")

# Extended developer / diagnostics options.
set(_ngin_base_crypto_cng_default OFF)
if(WIN32 AND (NGIN_BASE_BUILD_COMPONENTS STREQUAL "all" OR
              Crypto IN_LIST NGIN_BASE_BUILD_COMPONENTS))
  set(_ngin_base_crypto_cng_default ON)
endif()
set(_ngin_base_crypto_apple_default OFF)
if(APPLE AND (NGIN_BASE_BUILD_COMPONENTS STREQUAL "all" OR
              Crypto IN_LIST NGIN_BASE_BUILD_COMPONENTS))
  set(_ngin_base_crypto_apple_default ON)
endif()

option(NGIN_BASE_ENABLE_ASAN "Enable Address + Undefined Sanitizers (GNU/Clang)" OFF)
option(NGIN_BASE_ENABLE_TSAN "Enable ThreadSanitizer (GNU/Clang)" OFF)
option(NGIN_BASE_ENABLE_COVERAGE "Enable gcov-compatible coverage instrumentation (GNU/Clang)" OFF)
option(NGIN_BASE_ENABLE_LTO "Enable Link Time Optimization for Release/RelWithDebInfo" OFF)
option(NGIN_BASE_STRICT_WARNINGS "Enable extra warning flags" ON)
option(NGIN_BASE_ALL_FEATURES "Convenience: enable tests + examples + benchmarks" OFF)
option(NGIN_BASE_EXPORT_COMPILE_COMMANDS "Generate compile_commands.json" ON)
option(NGIN_BASE_CRYPTO_WITH_CNG "Enable Windows CNG-backed crypto algorithms" ${_ngin_base_crypto_cng_default})
option(NGIN_BASE_CRYPTO_WITH_APPLE "Enable Apple CommonCrypto-backed crypto algorithms" ${_ngin_base_crypto_apple_default})
option(NGIN_BASE_CRYPTO_WITH_OPENSSL "Enable optional OpenSSL-backed crypto algorithms" OFF)
option(NGIN_BASE_CRYPTO_WITH_BORINGSSL "Enable optional BoringSSL-backed crypto algorithms" OFF)
option(NGIN_BASE_CRYPTO_WITH_LIBSODIUM "Enable optional libsodium-backed crypto algorithms" OFF)
option(NGIN_BASE_TLS_WITH_OPENSSL "Enable the OpenSSL 3 TLS provider" OFF)

set(NGIN_BASE_CLANG_GCC_TOOLCHAIN "" CACHE PATH "Clang (Linux): GCC toolchain root passed via --gcc-toolchain")
set(NGIN_BASE_FIBER_BACKEND "default" CACHE STRING "Fiber backend: default/ucontext/winfiber/custom_asm")
set(NGIN_BASE_CRYPTO_REQUIRE_PROVIDER "" CACHE STRING "Required crypto providers: platform, platform-random, cng, openssl, boringssl, libsodium")
set(NGIN_BASE_CRYPTO_REQUIRE_ALGORITHMS "" CACHE STRING "Required crypto algorithms, separated by semicolons, commas, or spaces")
set(NGIN_BASE_TLS_REQUIRE_PROVIDER "" CACHE STRING "Required TLS provider: openssl")
set_property(CACHE NGIN_BASE_FIBER_BACKEND PROPERTY STRINGS default ucontext winfiber custom_asm)

if(NGIN_BASE_ALL_FEATURES)
  set(NGIN_BASE_BUILD_TESTS ON CACHE BOOL "Build tests" FORCE)
  set(NGIN_BASE_BUILD_EXAMPLES ON CACHE BOOL "Build examples" FORCE)
  set(NGIN_BASE_BUILD_BENCHMARKS ON CACHE BOOL "Build benchmarks" FORCE)
endif()

if(NGIN_BASE_ENABLE_ASAN AND NGIN_BASE_ENABLE_TSAN)
  message(FATAL_ERROR "Cannot enable ASAN and TSAN simultaneously")
endif()

if(NGIN_BASE_ENABLE_COVERAGE AND (NGIN_BASE_ENABLE_ASAN OR NGIN_BASE_ENABLE_TSAN))
  message(FATAL_ERROR "Coverage instrumentation cannot be combined with ASAN or TSAN")
endif()

if(NGIN_BASE_ENABLE_COVERAGE AND
   (MSVC OR NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
  message(FATAL_ERROR "NGIN_BASE_ENABLE_COVERAGE requires GCC or Clang")
endif()

if(NGIN_BASE_CRYPTO_WITH_CNG AND NOT WIN32)
  message(FATAL_ERROR "NGIN_BASE_CRYPTO_WITH_CNG is only supported on Windows")
endif()

if(NGIN_BASE_CRYPTO_WITH_APPLE AND NOT APPLE)
  message(FATAL_ERROR "NGIN_BASE_CRYPTO_WITH_APPLE is only supported on Apple platforms")
endif()

if(NGIN_BASE_CRYPTO_WITH_OPENSSL AND NGIN_BASE_CRYPTO_WITH_BORINGSSL)
  message(FATAL_ERROR
    "NGIN_BASE_CRYPTO_WITH_OPENSSL and NGIN_BASE_CRYPTO_WITH_BORINGSSL cannot both be enabled in one NGIN.Base build. "
    "Select exactly one OpenSSL-compatible libcrypto provider."
  )
endif()

string(TOLOWER "${NGIN_BASE_TLS_REQUIRE_PROVIDER}" _ngin_base_tls_required_provider)
if(_ngin_base_tls_required_provider)
  if(NOT _ngin_base_tls_required_provider STREQUAL "openssl")
    message(FATAL_ERROR "Unsupported NGIN_BASE_TLS_REQUIRE_PROVIDER value: ${NGIN_BASE_TLS_REQUIRE_PROVIDER}")
  endif()
  set(NGIN_BASE_TLS_WITH_OPENSSL ON CACHE BOOL "Enable the required OpenSSL 3 TLS provider" FORCE)
endif()

if(NOT NGIN_BASE_BUILD_STATIC AND NOT NGIN_BASE_BUILD_SHARED)
  message(FATAL_ERROR "At least one of static or shared build options must be enabled.")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ${NGIN_BASE_EXPORT_COMPILE_COMMANDS})
