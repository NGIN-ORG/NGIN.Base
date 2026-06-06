#-------------------------------------------------------------------------------
# Source ownership groups
#-------------------------------------------------------------------------------
set(NGIN_BASE_PRIVATE_DEFINITIONS)
set(NGIN_BASE_PRIVATE_LIBRARIES)

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
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Errors/CryptoError.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Encoding/Base64.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Encoding/Base64Url.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Encoding/Hex.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Asymmetric/Ed25519.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Asymmetric/X25519.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Memory/ConstantTime.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Memory/ZeroMemory.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Memory/SecureBuffer.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Hashing/Hash.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Kdf/KeyDerivation.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Mac/Hmac.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Random/SecureRandom.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Signatures/Signature.cpp
  ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Symmetric/Aead.cpp
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

if(NGIN_CRYPTO_WITH_OPENSSL)
  find_package(OpenSSL REQUIRED COMPONENTS Crypto)
  list(APPEND NGIN_BASE_CRYPTO_SOURCES
    ${NGIN_BASE_ROOT_DIR}/src/NGIN/Crypto/Backends/OpenSslBackend.cpp
  )
  list(APPEND NGIN_BASE_PRIVATE_DEFINITIONS NGIN_BASE_CRYPTO_HAS_OPENSSL)
  list(APPEND NGIN_BASE_PRIVATE_LIBRARIES OpenSSL::Crypto)
endif()

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
