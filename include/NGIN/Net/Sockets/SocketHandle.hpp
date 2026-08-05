/// @file SocketHandle.hpp
/// @brief Lightweight socket handle wrapper.
#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Primitives.hpp>

namespace NGIN::Net
{
    class SocketHandle;
}

namespace NGIN::Net::detail
{
    [[nodiscard]] NGIN_NET_API bool CloseSocket(SocketHandle& handle) noexcept;
}

namespace NGIN::Net
{
    /// @brief Lightweight socket handle wrapper with RAII lifetime.
    class NGIN_NET_API SocketHandle final
    {
    public:
        using NativeHandle = NGIN::IntPtr;

        /// @brief Constructs a closed socket handle.
        constexpr SocketHandle() noexcept = default;
        /// @brief Takes ownership of a native socket handle.
        explicit constexpr SocketHandle(NativeHandle handle) noexcept
            : m_handle(handle)
        {
        }

        /// @brief Socket handles are non-copyable because they uniquely own native state.
        SocketHandle(const SocketHandle&) = delete;
        /// @brief Socket handles are non-copy-assignable because they uniquely own native state.
        SocketHandle& operator=(const SocketHandle&) = delete;

        /// @brief Transfers native socket ownership from another handle.
        SocketHandle(SocketHandle&& other) noexcept
            : m_handle(other.m_handle)
        {
            other.Reset();
        }

        /// @brief Closes this socket and transfers native ownership from another handle.
        SocketHandle& operator=(SocketHandle&& other) noexcept
        {
            if (this != &other)
            {
                (void) detail::CloseSocket(*this);
                m_handle = other.m_handle;
                other.Reset();
            }
            return *this;
        }

        /// @brief Closes the owned native socket.
        ~SocketHandle() { Close(); }

        /// @brief Returns whether this wrapper owns a native socket.
        [[nodiscard]] constexpr bool IsOpen() const noexcept { return m_handle != InvalidHandle(); }

        /// @brief Returns the native socket value without transferring ownership.
        [[nodiscard]] constexpr NativeHandle Native() const noexcept { return m_handle; }

        /// @brief Closes the socket; calling Close() repeatedly is safe.
        void Close() noexcept;

    private:
        friend bool detail::CloseSocket(SocketHandle& handle) noexcept;

        static constexpr NativeHandle InvalidHandle() noexcept { return static_cast<NativeHandle>(-1); }

        void Reset() noexcept { m_handle = InvalidHandle(); }

        NativeHandle m_handle {InvalidHandle()};
    };
}// namespace NGIN::Net
