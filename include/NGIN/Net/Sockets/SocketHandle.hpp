/// @file SocketHandle.hpp
/// @brief Lightweight socket handle wrapper.
#pragma once

#include <NGIN/Primitives.hpp>

namespace NGIN::Net
{
    class SocketHandle;
}

namespace NGIN::Net::detail
{
    [[nodiscard]] bool CloseSocket(SocketHandle& handle) noexcept;
}

namespace NGIN::Net
{
    /// @brief Lightweight socket handle wrapper with RAII lifetime.
    class SocketHandle final
    {
    public:
        using NativeHandle = NGIN::IntPtr;

        constexpr SocketHandle() noexcept = default;
        explicit constexpr SocketHandle(NativeHandle handle) noexcept
            : m_handle(handle)
        {
        }

        SocketHandle(const SocketHandle&)            = delete;
        SocketHandle& operator=(const SocketHandle&) = delete;

        SocketHandle(SocketHandle&& other) noexcept
            : m_handle(other.m_handle)
        {
            other.Reset();
        }

        SocketHandle& operator=(SocketHandle&& other) noexcept
        {
            if (this != &other)
            {
                (void)detail::CloseSocket(*this);
                m_handle = other.m_handle;
                other.Reset();
            }
            return *this;
        }

        ~SocketHandle() { Close(); }

        [[nodiscard]] constexpr bool IsOpen() const noexcept { return m_handle != InvalidHandle(); }

        [[nodiscard]] constexpr NativeHandle Native() const noexcept { return m_handle; }

        void Close() noexcept;

    private:
        friend bool detail::CloseSocket(SocketHandle& handle) noexcept;

        static constexpr NativeHandle InvalidHandle() noexcept { return static_cast<NativeHandle>(-1); }

        void Reset() noexcept { m_handle = InvalidHandle(); }

        NativeHandle m_handle {InvalidHandle()};
    };
}// namespace NGIN::Net
