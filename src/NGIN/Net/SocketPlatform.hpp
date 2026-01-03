#pragma once

#include <cstddef>
#include <cstdint>

#include <NGIN/Net/Sockets/SocketHandle.hpp>
#include <NGIN/Net/Types/AddressFamily.hpp>
#include <NGIN/Net/Types/Endpoint.hpp>
#include <NGIN/Net/Types/NetError.hpp>
#include <NGIN/Net/Types/ShutdownMode.hpp>
#include <NGIN/Net/Types/SocketOptions.hpp>

#if defined(NGIN_PLATFORM_WINDOWS)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <mswsock.h>
#else
  #include <arpa/inet.h>
  #include <errno.h>
  #include <fcntl.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <sys/select.h>
  #include <sys/socket.h>
  #include <unistd.h>
#endif

namespace NGIN::Net::detail
{
#if defined(NGIN_PLATFORM_WINDOWS)
    using NativeSocket = SOCKET;
    constexpr NativeSocket InvalidNativeSocket = INVALID_SOCKET;
    using AcceptExFn = BOOL (PASCAL *)(SOCKET, SOCKET, PVOID, DWORD, DWORD, DWORD, LPDWORD, LPOVERLAPPED);
    using ConnectExFn = BOOL (PASCAL *)(SOCKET, const sockaddr*, int, PVOID, DWORD, LPDWORD, LPOVERLAPPED);
#else
    using NativeSocket = int;
    constexpr NativeSocket InvalidNativeSocket = -1;
#endif

    [[nodiscard]] bool EnsureInitialized() noexcept;

    [[nodiscard]] NetError MapError(int native) noexcept;
    [[nodiscard]] NetError LastError() noexcept;
    [[nodiscard]] bool IsWouldBlock(const NetError& error) noexcept;
    [[nodiscard]] bool IsInProgress(const NetError& error) noexcept;

    [[nodiscard]] NativeSocket ToNative(const SocketHandle& handle) noexcept;
    [[nodiscard]] SocketHandle FromNative(NativeSocket socket) noexcept;

    [[nodiscard]] SocketHandle CreateSocket(AddressFamily family,
                                            int type,
                                            int protocol,
                                            bool dualStack,
                                            NetError& error) noexcept;

    [[nodiscard]] bool SetNonBlocking(SocketHandle& handle, bool value) noexcept;
    [[nodiscard]] bool SetReuseAddress(SocketHandle& handle, bool value) noexcept;
    [[nodiscard]] bool SetReusePort(SocketHandle& handle, bool value) noexcept;
    [[nodiscard]] bool SetNoDelay(SocketHandle& handle, bool value) noexcept;
    [[nodiscard]] bool SetBroadcast(SocketHandle& handle, bool value) noexcept;
    [[nodiscard]] bool SetV6Only(SocketHandle& handle, bool value) noexcept;

    [[nodiscard]] NetExpected<void> Shutdown(SocketHandle& handle, ShutdownMode mode) noexcept;
    [[nodiscard]] bool CloseSocket(SocketHandle& handle) noexcept;

    [[nodiscard]] bool ToSockAddr(const Endpoint& endpoint, sockaddr_storage& storage, socklen_t& length) noexcept;
    [[nodiscard]] Endpoint FromSockAddr(const sockaddr_storage& storage, socklen_t length) noexcept;

    [[nodiscard]] NetExpected<void> CheckConnectResult(SocketHandle& handle) noexcept;

#if defined(NGIN_PLATFORM_WINDOWS)
    [[nodiscard]] AcceptExFn GetAcceptEx() noexcept;
    [[nodiscard]] ConnectExFn GetConnectEx() noexcept;
    [[nodiscard]] AddressFamily GetSocketFamily(SocketHandle& handle) noexcept;
    [[nodiscard]] bool EnsureBoundForConnectEx(SocketHandle& handle, const Endpoint& remoteEndpoint) noexcept;
    [[nodiscard]] bool IsV6Only(SocketHandle& handle) noexcept;
#endif
}
