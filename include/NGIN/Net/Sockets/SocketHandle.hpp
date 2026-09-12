/// @file SocketHandle.hpp
/// @brief Socket ownership with stable state for admitted asynchronous operations.
#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Primitives.hpp>
#include <memory>

namespace NGIN::Net
{
    class SocketHandle;
}
namespace NGIN::Net::detail
{
    class SocketState;
    struct SocketHandleAccess;
    [[nodiscard]] NGIN_NET_API bool CloseSocket(SocketHandle& handle) noexcept;
}// namespace NGIN::Net::detail
namespace NGIN::Net
{
    /// @brief Owns a native socket; operations can retain its stable lifetime state.
    /// @details Close prevents further admission and requests cancellation. Backend
    /// leases defer native release until their final access. Moving transfers the
    /// public owner without changing the state captured by admitted operations.
    class NGIN_NET_API SocketHandle final
    {
    public:
        using NativeHandle      = NGIN::IntPtr;
        SocketHandle() noexcept = default;
        /// @brief Takes native ownership, including closing it if state allocation fails.
        /// @note On Windows an imported native handle does not establish tracked socket
        /// mode; create asynchronous sockets through Open or listener acceptance.
        /// @throws std::bad_alloc if stable ownership state cannot be allocated.
        explicit SocketHandle(NativeHandle handle);
        SocketHandle(const SocketHandle&)            = delete;
        SocketHandle& operator=(const SocketHandle&) = delete;
        SocketHandle(SocketHandle&& other) noexcept;
        SocketHandle& operator=(SocketHandle&& other) noexcept;
        ~SocketHandle();

        [[nodiscard]] bool IsOpen() const noexcept;
        /// @brief Borrows the native value, or -1 after Close; does not pin native lifetime.
        [[nodiscard]] NativeHandle Native() const noexcept;
        /// @brief Requests close without waiting; repeated calls are harmless.
        void Close() noexcept;

    private:
        friend bool detail::CloseSocket(SocketHandle& handle) noexcept;
        friend struct detail::SocketHandleAccess;
        std::shared_ptr<detail::SocketState> m_state;
    };
}// namespace NGIN::Net
