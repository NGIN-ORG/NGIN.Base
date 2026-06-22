#-------------------------------------------------------------------------------
# Source ownership groups
#-------------------------------------------------------------------------------
set(NGIN_BASE_PRIVATE_DEFINITIONS)
set(NGIN_BASE_PRIVATE_INCLUDE_DIRECTORIES)
set(NGIN_BASE_PRIVATE_LIBRARIES)
set(NGIN_BASE_CRYPTO_AVAILABLE_PROVIDERS platform platform-random)
set(NGIN_BASE_CRYPTO_AVAILABLE_ALGORITHMS random)

set(NGIN_BASE_FOUNDATION_SOURCES
  ${NGIN_BASE_ROOT_DIR}/src/CoreInit.cpp
)

set(NGIN_BASE_EXECUTION_SOURCES
  ${NGIN_BASE_ROOT_DIR}/src/Async/Fiber/FiberCommon.cpp
)

set(NGIN_BASE_IO_SOURCES
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/IO/Path.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/IO/VirtualFileSystem.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/IO/FileSystemDriver.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/IO/FileSystemUtilities.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/IO/LocalFileSystem.cpp
)

set(NGIN_BASE_CRYPTO_SOURCES
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Backends/BackendDispatch.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Certificates/Certificate.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Certificates/CertificateStore.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Errors/CryptoError.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Encoding/Base64.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Encoding/Base64Url.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Encoding/Der.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Encoding/Hex.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Encoding/Pem.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Asymmetric/Ed25519.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Asymmetric/Rsa.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Asymmetric/X25519.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Memory/ConstantTime.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Memory/ZeroMemory.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Memory/SecureBuffer.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Hashing/Hash.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Kdf/KeyDerivation.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Kdf/PasswordHash.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Keys/KeyFormat.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Mac/Hmac.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Random/SecureRandom.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Signatures/Signature.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Symmetric/Aead.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Tokens/Jwt.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Tokens/Paseto.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Tokens/TokenGenerator.cpp
)

set(NGIN_BASE_NET_SOURCES
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Net/TcpListener.cpp
)

set(NGIN_BASE_SERIALIZATION_SOURCES
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Serialization/JSON/JsonParser.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Serialization/JSON/JsonArchive.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Serialization/XML/XmlParser.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Serialization/XML/XmlArchive.cpp
)

if(NGIN_BASE_CRYPTO_WITH_OPENSSL)
  find_package(OpenSSL REQUIRED COMPONENTS Crypto)
  list(APPEND NGIN_BASE_CRYPTO_SOURCES
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Backends/OpenSslBackend.cpp
  )
  list(APPEND NGIN_BASE_PRIVATE_DEFINITIONS NGIN_BASE_CRYPTO_HAS_OPENSSL NGIN_BASE_CRYPTO_HAS_OPENSSL_COMPAT)
  list(APPEND NGIN_BASE_PRIVATE_LIBRARIES OpenSSL::Crypto)
  list(APPEND NGIN_BASE_CRYPTO_AVAILABLE_PROVIDERS openssl)
  list(APPEND NGIN_BASE_CRYPTO_AVAILABLE_ALGORITHMS
    sha256
    sha512
    hmac-sha256
    hmac-sha512
    hkdf-sha256
    hkdf-sha512
    pbkdf2-sha256
    pbkdf2-sha512
    aes-128-gcm
    aes-256-gcm
    chacha20-poly1305
    ecdsa-p256-sha256
    ed25519
    rsa-oaep-sha256
    rsa-pss-sha256
    x25519
  )
endif()

if(NGIN_BASE_CRYPTO_WITH_BORINGSSL)
  if(NOT TARGET BoringSSL::Crypto)
    find_package(BoringSSL CONFIG QUIET)
  endif()

  if(NOT TARGET BoringSSL::Crypto AND TARGET BoringSSL::crypto)
    add_library(BoringSSL::Crypto INTERFACE IMPORTED)
    set_property(TARGET BoringSSL::Crypto PROPERTY INTERFACE_LINK_LIBRARIES BoringSSL::crypto)
  endif()

  if(NOT TARGET BoringSSL::Crypto AND TARGET crypto)
    add_library(BoringSSL::Crypto INTERFACE IMPORTED)
    set_property(TARGET BoringSSL::Crypto PROPERTY INTERFACE_LINK_LIBRARIES crypto)
  endif()

  if(NOT TARGET BoringSSL::Crypto)
    find_path(BORINGSSL_INCLUDE_DIR openssl/base.h)
    set(_ngin_base_boringssl_header_is_boringssl OFF)
    if(BORINGSSL_INCLUDE_DIR)
      file(STRINGS
        "${BORINGSSL_INCLUDE_DIR}/openssl/base.h"
        _ngin_base_boringssl_marker
        REGEX "OPENSSL_IS_BORINGSSL")
      if(_ngin_base_boringssl_marker)
        set(_ngin_base_boringssl_header_is_boringssl ON)
      endif()
    endif()

    find_library(BORINGSSL_CRYPTO_LIBRARY NAMES crypto libcrypto)
    if(BORINGSSL_INCLUDE_DIR AND BORINGSSL_CRYPTO_LIBRARY AND _ngin_base_boringssl_header_is_boringssl)
      add_library(BoringSSL::Crypto UNKNOWN IMPORTED)
      set_target_properties(
        BoringSSL::Crypto
        PROPERTIES
          IMPORTED_LOCATION "${BORINGSSL_CRYPTO_LIBRARY}"
          INTERFACE_INCLUDE_DIRECTORIES "${BORINGSSL_INCLUDE_DIR}")
      list(APPEND NGIN_BASE_PRIVATE_INCLUDE_DIRECTORIES "${BORINGSSL_INCLUDE_DIR}")
    elseif(BORINGSSL_INCLUDE_DIR AND BORINGSSL_CRYPTO_LIBRARY)
      message(FATAL_ERROR
        "NGIN_BASE_CRYPTO_WITH_BORINGSSL found openssl/base.h and libcrypto, but the header does not define "
        "OPENSSL_IS_BORINGSSL. Refusing to treat a non-BoringSSL OpenSSL-compatible install as BoringSSL."
      )
    endif()
  endif()

  if(NOT TARGET BoringSSL::Crypto)
    message(FATAL_ERROR
      "NGIN_BASE_CRYPTO_WITH_BORINGSSL requires a BoringSSL CMake package, BoringSSL::crypto target, "
      "or discoverable BoringSSL crypto library/header"
    )
  endif()

  list(APPEND NGIN_BASE_CRYPTO_SOURCES
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Backends/OpenSslBackend.cpp
  )
  list(APPEND NGIN_BASE_PRIVATE_DEFINITIONS NGIN_BASE_CRYPTO_HAS_BORINGSSL NGIN_BASE_CRYPTO_HAS_OPENSSL_COMPAT)
  list(APPEND NGIN_BASE_PRIVATE_LIBRARIES BoringSSL::Crypto)
  list(APPEND NGIN_BASE_CRYPTO_AVAILABLE_PROVIDERS boringssl)
  list(APPEND NGIN_BASE_CRYPTO_AVAILABLE_ALGORITHMS
    sha256
    sha512
    hmac-sha256
    hmac-sha512
    hkdf-sha256
    hkdf-sha512
    pbkdf2-sha256
    pbkdf2-sha512
    aes-128-gcm
    aes-256-gcm
    chacha20-poly1305
    ecdsa-p256-sha256
    ed25519
    rsa-oaep-sha256
    rsa-pss-sha256
    x25519
  )
endif()

if(NGIN_BASE_CRYPTO_WITH_LIBSODIUM)
  if(NOT TARGET libsodium::libsodium)
    find_package(unofficial-sodium CONFIG QUIET)
    if(TARGET unofficial-sodium::sodium)
      add_library(libsodium::libsodium INTERFACE IMPORTED)
      set_property(TARGET libsodium::libsodium PROPERTY INTERFACE_LINK_LIBRARIES unofficial-sodium::sodium)
    endif()
  endif()

  if(NOT TARGET libsodium::libsodium)
    find_package(sodium CONFIG QUIET)
    if(TARGET sodium::sodium)
      add_library(libsodium::libsodium INTERFACE IMPORTED)
      set_property(TARGET libsodium::libsodium PROPERTY INTERFACE_LINK_LIBRARIES sodium::sodium)
    elseif(TARGET Sodium::Sodium)
      add_library(libsodium::libsodium INTERFACE IMPORTED)
      set_property(TARGET libsodium::libsodium PROPERTY INTERFACE_LINK_LIBRARIES Sodium::Sodium)
    endif()
  endif()

  if(NOT TARGET libsodium::libsodium)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(LIBSODIUM QUIET IMPORTED_TARGET libsodium)
      if(TARGET PkgConfig::LIBSODIUM)
        set(_ngin_base_libsodium_pkgconfig_has_header OFF)
        foreach(_ngin_base_libsodium_pkgconfig_include IN LISTS LIBSODIUM_INCLUDE_DIRS)
          if(EXISTS "${_ngin_base_libsodium_pkgconfig_include}/sodium.h")
            set(_ngin_base_libsodium_pkgconfig_has_header ON)
          endif()
        endforeach()
        if(_ngin_base_libsodium_pkgconfig_has_header)
          add_library(libsodium::libsodium INTERFACE IMPORTED)
          set_property(TARGET libsodium::libsodium PROPERTY INTERFACE_LINK_LIBRARIES PkgConfig::LIBSODIUM)
        endif()
      endif()
    endif()
  endif()

  if(NOT TARGET libsodium::libsodium)
    find_path(LIBSODIUM_INCLUDE_DIR sodium.h)
    find_library(LIBSODIUM_LIBRARY NAMES sodium libsodium)
    if(LIBSODIUM_INCLUDE_DIR AND LIBSODIUM_LIBRARY)
      add_library(libsodium::libsodium UNKNOWN IMPORTED)
      set_target_properties(
        libsodium::libsodium
        PROPERTIES
          IMPORTED_LOCATION "${LIBSODIUM_LIBRARY}"
          INTERFACE_INCLUDE_DIRECTORIES "${LIBSODIUM_INCLUDE_DIR}")
      list(APPEND NGIN_BASE_PRIVATE_INCLUDE_DIRECTORIES "${LIBSODIUM_INCLUDE_DIR}")
    endif()
  endif()

  if(NOT TARGET libsodium::libsodium)
    message(FATAL_ERROR
      "NGIN_BASE_CRYPTO_WITH_LIBSODIUM requires a libsodium CMake package, pkg-config module, "
      "or discoverable sodium library/header"
    )
  endif()

  list(APPEND NGIN_BASE_CRYPTO_SOURCES
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Backends/LibsodiumBackend.cpp
  )
  list(APPEND NGIN_BASE_PRIVATE_DEFINITIONS NGIN_BASE_CRYPTO_HAS_LIBSODIUM)
  list(APPEND NGIN_BASE_PRIVATE_LIBRARIES libsodium::libsodium)
  list(APPEND NGIN_BASE_CRYPTO_AVAILABLE_PROVIDERS libsodium)
  list(APPEND NGIN_BASE_CRYPTO_AVAILABLE_ALGORITHMS
    xchacha20-poly1305
    ed25519
    x25519
    argon2id
  )
endif()

if(WIN32 AND NGIN_BASE_CRYPTO_WITH_CNG)
  list(APPEND NGIN_BASE_CRYPTO_SOURCES
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Backends/CngBackend.win32.cpp
  )
  list(APPEND NGIN_BASE_PRIVATE_DEFINITIONS NGIN_BASE_CRYPTO_HAS_CNG)
  list(APPEND NGIN_BASE_CRYPTO_AVAILABLE_PROVIDERS cng)
  list(APPEND NGIN_BASE_CRYPTO_AVAILABLE_ALGORITHMS
    sha256
    sha512
    hmac-sha256
    hmac-sha512
    pbkdf2-sha256
    pbkdf2-sha512
    aes-128-gcm
    aes-256-gcm
  )
endif()

if(APPLE AND NGIN_BASE_CRYPTO_WITH_APPLE)
  list(APPEND NGIN_BASE_CRYPTO_SOURCES
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Backends/AppleBackend.cpp
  )
  list(APPEND NGIN_BASE_PRIVATE_DEFINITIONS NGIN_BASE_CRYPTO_HAS_APPLE)
  list(APPEND NGIN_BASE_CRYPTO_AVAILABLE_PROVIDERS apple)
  list(APPEND NGIN_BASE_CRYPTO_AVAILABLE_ALGORITHMS
    sha256
    sha512
    hmac-sha256
    hmac-sha512
    pbkdf2-sha256
    pbkdf2-sha512
  )
endif()

function(ngin_base_normalize_crypto_requirement_list output_var input_value)
  set(normalized)
  if(NOT "${input_value}" STREQUAL "")
    string(REPLACE "," ";" items "${input_value}")
    string(REPLACE " " ";" items "${items}")
    foreach(item IN LISTS items)
      string(STRIP "${item}" item)
      if(NOT item STREQUAL "")
        string(TOLOWER "${item}" item)
        list(APPEND normalized "${item}")
      endif()
    endforeach()
    if(normalized)
      list(REMOVE_DUPLICATES normalized)
    endif()
  endif()

  set(${output_var} ${normalized} PARENT_SCOPE)
endfunction()

function(ngin_base_validate_crypto_requirements)
  list(REMOVE_DUPLICATES NGIN_BASE_CRYPTO_AVAILABLE_PROVIDERS)
  list(REMOVE_DUPLICATES NGIN_BASE_CRYPTO_AVAILABLE_ALGORITHMS)
  list(SORT NGIN_BASE_CRYPTO_AVAILABLE_PROVIDERS)
  list(SORT NGIN_BASE_CRYPTO_AVAILABLE_ALGORITHMS)

  ngin_base_normalize_crypto_requirement_list(
    required_providers
    "${NGIN_BASE_CRYPTO_REQUIRE_PROVIDER}"
  )
  ngin_base_normalize_crypto_requirement_list(
    required_algorithms
    "${NGIN_BASE_CRYPTO_REQUIRE_ALGORITHMS}"
  )

  set(known_providers
    platform
    platform-random
    apple
    boringssl
    cng
    libsodium
    openssl
  )
  set(known_algorithms
    random
    sha256
    sha512
    hmac-sha256
    hmac-sha512
    hkdf-sha256
    hkdf-sha512
    pbkdf2-sha256
    pbkdf2-sha512
    aes-128-gcm
    aes-256-gcm
    chacha20-poly1305
    xchacha20-poly1305
    ecdsa-p256-sha256
    ed25519
    rsa-oaep-sha256
    rsa-pss-sha256
    x25519
    argon2id
  )

  foreach(provider IN LISTS required_providers)
    if(NOT provider IN_LIST known_providers)
      message(FATAL_ERROR
        "Unknown crypto provider '${provider}' in NGIN_BASE_CRYPTO_REQUIRE_PROVIDER. "
        "Known providers: ${known_providers}"
      )
    endif()
    if(NOT provider IN_LIST NGIN_BASE_CRYPTO_AVAILABLE_PROVIDERS)
      message(FATAL_ERROR
        "Required crypto provider '${provider}' is not available in this build. "
        "Available providers: ${NGIN_BASE_CRYPTO_AVAILABLE_PROVIDERS}"
      )
    endif()
  endforeach()

  foreach(algorithm IN LISTS required_algorithms)
    if(NOT algorithm IN_LIST known_algorithms)
      message(FATAL_ERROR
        "Unknown crypto algorithm '${algorithm}' in NGIN_BASE_CRYPTO_REQUIRE_ALGORITHMS. "
        "Known algorithms: ${known_algorithms}"
      )
    endif()
    if(NOT algorithm IN_LIST NGIN_BASE_CRYPTO_AVAILABLE_ALGORITHMS)
      message(FATAL_ERROR
        "Required crypto algorithm '${algorithm}' is not available in this build. "
        "Available algorithms: ${NGIN_BASE_CRYPTO_AVAILABLE_ALGORITHMS}"
      )
    endif()
  endforeach()

  if(required_providers OR required_algorithms)
    message(STATUS "NGIN.Base crypto required providers: ${required_providers}")
    message(STATUS "NGIN.Base crypto required algorithms: ${required_algorithms}")
    message(STATUS "NGIN.Base crypto available providers: ${NGIN_BASE_CRYPTO_AVAILABLE_PROVIDERS}")
    message(STATUS "NGIN.Base crypto available algorithms: ${NGIN_BASE_CRYPTO_AVAILABLE_ALGORITHMS}")
  endif()
endfunction()

ngin_base_validate_crypto_requirements()

if(WIN32)
  list(APPEND NGIN_BASE_EXECUTION_SOURCES
    ${NGIN_BASE_ROOT_DIR}/src/Async/Fiber/Fiber.win32.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Execution/ThisThread.win32.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Execution/Thread.win32.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Execution/AtomicCondition.win32.cpp
  )
  list(APPEND NGIN_BASE_IO_SOURCES
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/IO/DynamicLibrary.win32.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/IO/File.win32.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/IO/FileView.win32.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/IO/FileSystemDriver.win32.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/IO/LocalFileSystem.win32.cpp
  )
  list(APPEND NGIN_BASE_NET_SOURCES
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Net/SocketPlatform.win32.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Net/NetworkDriver.win32.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Net/TcpSocket.win32.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Net/UdpSocket.win32.cpp
  )
  list(APPEND NGIN_BASE_CRYPTO_SOURCES
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Random/SecureRandom.win32.cpp
  )
  list(APPEND NGIN_BASE_FOUNDATION_SOURCES
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Time/MonotonicClock.win32.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Time/Sleep.win32.cpp
  )
elseif(APPLE)
  list(APPEND NGIN_BASE_EXECUTION_SOURCES
    ${NGIN_BASE_ROOT_DIR}/src/Async/Fiber/Fiber.posix.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Execution/ThisThread.posix.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Execution/Thread.posix.cpp
  )
  list(APPEND NGIN_BASE_IO_SOURCES
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/IO/DynamicLibrary.posix.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/IO/File.posix.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/IO/FileView.posix.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/IO/FileSystemDriver.posix.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/IO/LocalFileSystem.posix.cpp
  )
  list(APPEND NGIN_BASE_NET_SOURCES
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Net/SocketPlatform.posix.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Net/NetworkDriver.posix.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Net/TcpSocket.posix.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Net/UdpSocket.posix.cpp
  )
  list(APPEND NGIN_BASE_CRYPTO_SOURCES
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Random/SecureRandom.apple.cpp
  )
  list(APPEND NGIN_BASE_FOUNDATION_SOURCES
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Time/MonotonicClock.posix.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Time/Sleep.posix.cpp
  )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND NGIN_BASE_EXECUTION_SOURCES
    ${NGIN_BASE_ROOT_DIR}/src/Async/Fiber/Fiber.posix.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Execution/ThisThread.linux.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Execution/Thread.posix.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Execution/AtomicCondition.linux.cpp
  )
  list(APPEND NGIN_BASE_IO_SOURCES
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/IO/DynamicLibrary.posix.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/IO/File.posix.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/IO/FileView.posix.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/IO/FileSystemDriver.linux.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/IO/LocalFileSystem.posix.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/IO/LocalFileSystem.linux.cpp
  )
  list(APPEND NGIN_BASE_NET_SOURCES
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Net/SocketPlatform.posix.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Net/NetworkDriver.linux.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Net/TcpSocket.posix.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Net/UdpSocket.posix.cpp
  )
  list(APPEND NGIN_BASE_CRYPTO_SOURCES
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Random/SecureRandom.linux.cpp
  )
  list(APPEND NGIN_BASE_FOUNDATION_SOURCES
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Time/MonotonicClock.posix.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Time/Sleep.posix.cpp
  )
elseif(UNIX)
  list(APPEND NGIN_BASE_EXECUTION_SOURCES
    ${NGIN_BASE_ROOT_DIR}/src/Async/Fiber/Fiber.posix.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Execution/ThisThread.posix.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Execution/Thread.posix.cpp
  )
  list(APPEND NGIN_BASE_IO_SOURCES
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/IO/DynamicLibrary.posix.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/IO/File.posix.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/IO/FileView.posix.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/IO/FileSystemDriver.posix.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/IO/LocalFileSystem.posix.cpp
  )
  list(APPEND NGIN_BASE_NET_SOURCES
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Net/SocketPlatform.posix.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Net/NetworkDriver.posix.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Net/TcpSocket.posix.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Net/UdpSocket.posix.cpp
  )
  list(APPEND NGIN_BASE_CRYPTO_SOURCES
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Random/SecureRandom.posix.cpp
  )
  list(APPEND NGIN_BASE_FOUNDATION_SOURCES
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Time/MonotonicClock.posix.cpp
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Time/Sleep.posix.cpp
  )
endif()

if(UNIX AND NGIN_BASE_FIBER_BACKEND STREQUAL "custom_asm")
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    list(APPEND NGIN_BASE_EXECUTION_SOURCES
      ${NGIN_BASE_ROOT_DIR}/src/Async/Fiber/FiberContext.x86_64.cpp
    )
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    list(APPEND NGIN_BASE_EXECUTION_SOURCES
      ${NGIN_BASE_ROOT_DIR}/src/Async/Fiber/FiberContext.aarch64.cpp
    )
  endif()
elseif(UNIX AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
  list(APPEND NGIN_BASE_EXECUTION_SOURCES
    ${NGIN_BASE_ROOT_DIR}/src/Async/Fiber/FiberContext.x86_64.cpp
  )
elseif(UNIX AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
  list(APPEND NGIN_BASE_EXECUTION_SOURCES
    ${NGIN_BASE_ROOT_DIR}/src/Async/Fiber/FiberContext.aarch64.cpp
  )
endif()

set(NGIN_BASE_CORE_SOURCES
  ${NGIN_BASE_FOUNDATION_SOURCES}
  ${NGIN_BASE_EXECUTION_SOURCES}
  ${NGIN_BASE_IO_SOURCES}
  ${NGIN_BASE_CRYPTO_SOURCES}
  ${NGIN_BASE_NET_SOURCES}
  ${NGIN_BASE_SERIALIZATION_SOURCES}
)
