/// @file SocketHandle.hpp
/// @brief Lightweight socket handle wrapper.
#pragma once

#include <NGIN/Primitives.hpp>

namespace NGIN::Net
{
    /// @brief Lightweight, non-owning socket handle wrapper.
    class SocketHandle final
    {
    public:
        using NativeHandle = NGIN::IntPtr;

        constexpr SocketHandle() noexcept = default;
        explicit constexpr SocketHandle(NativeHandle handle) noexcept
            : m_handle(handle)
        {
        }

        [[nodiscard]] constexpr bool IsOpen() const noexcept { return m_handle != InvalidHandle(); }

        [[nodiscard]] constexpr NativeHandle Native() const noexcept { return m_handle; }

        void Close() noexcept;

    private:
        static constexpr NativeHandle InvalidHandle() noexcept { return static_cast<NativeHandle>(-1); }

        NativeHandle m_handle {InvalidHandle()};
    };
}// namespace NGIN::Net
