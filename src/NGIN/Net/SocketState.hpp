#pragma once

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Net/Sockets/SocketHandle.hpp>
#include <NGIN/Net/Types/NetError.hpp>

#include <atomic>
#include <expected>
#include <memory>
#include <mutex>
#include <utility>

namespace NGIN::Net::detail
{
    // Stable native ownership shared by a socket wrapper and admitted operations.
    // Native handles remain pinned until the final backend access ends.
    class SocketState final
    {
    public:
        using NativeHandle                      = SocketHandle::NativeHandle;
        static constexpr NativeHandle Invalid   = static_cast<NativeHandle>(-1);
        static constexpr unsigned     Read      = 1;
        static constexpr unsigned     Write     = 2;
        static constexpr unsigned     Exclusive = Read | Write;

        explicit SocketState(NativeHandle native = Invalid) : m_native(native) {}
        ~SocketState();
        SocketState(const SocketState&)            = delete;
        SocketState& operator=(const SocketState&) = delete;

        NativeHandle                   VisibleNative() const noexcept;
        bool                           IsClosing() const noexcept { return m_closing.load(std::memory_order_acquire); }
        NGIN::Async::CancellationToken CloseToken() const noexcept { return m_close.GetToken(); }
        bool                           RequestClose() noexcept;
        // WinSock has no FIONBIO query. Platform mode setters keep this state
        // consistent; imported native handles must establish their mode first.
        bool IsNonBlocking() const noexcept { return m_nonBlocking.load(std::memory_order_acquire); }
        void SetNonBlocking(bool enabled) noexcept { m_nonBlocking.store(enabled, std::memory_order_release); }
        // Used only while an accepted socket's preallocated state is unpublished.
        void Adopt(NativeHandle native) noexcept;

    private:
        friend class SocketLease;
        std::expected<NativeHandle, NetError> Acquire(unsigned directions) noexcept;
        void                                  Release(unsigned directions) noexcept;

        std::atomic<NativeHandle>       m_native;
        std::atomic<bool>               m_closing {false};
        std::atomic<bool>               m_nonBlocking {false};
        std::mutex                      m_mutex;
        unsigned                        m_busy {};
        NGIN::Async::CancellationSource m_close;
    };

    class SocketLease final
    {
    public:
        SocketLease() noexcept = default;
        ~SocketLease() { Reset(); }
        SocketLease(const SocketLease&)            = delete;
        SocketLease& operator=(const SocketLease&) = delete;
        SocketLease(SocketLease&& other) noexcept
            : m_state(std::move(other.m_state)), m_native(other.m_native), m_directions(other.m_directions) {}
        SocketLease& operator=(SocketLease&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                m_state      = std::move(other.m_state);
                m_native     = other.m_native;
                m_directions = other.m_directions;
            }
            return *this;
        }
        static std::expected<SocketLease, NetError> Acquire(std::shared_ptr<SocketState> state, unsigned directions) noexcept;
        void                                        Reset() noexcept;
        void                                        RequestClose() noexcept
        {
            if (m_state)
                (void) m_state->RequestClose();
        }
        SocketState::NativeHandle      Native() const noexcept { return m_native; }
        bool                           IsValid() const noexcept { return static_cast<bool>(m_state); }
        bool                           IsClosing() const noexcept { return !m_state || m_state->IsClosing(); }
        NGIN::Async::CancellationToken CloseToken() const noexcept { return m_state ? m_state->CloseToken() : NGIN::Async::CancellationToken {}; }

    private:
        std::shared_ptr<SocketState> m_state;
        SocketState::NativeHandle    m_native {SocketState::Invalid};
        unsigned                     m_directions {};
    };

    struct SocketHandleAccess final
    {
        static std::shared_ptr<SocketState> State(const SocketHandle& handle) noexcept { return handle.m_state; }
        static SocketHandle                 FromState(std::shared_ptr<SocketState> state) noexcept
        {
            SocketHandle handle;
            handle.m_state = std::move(state);
            return handle;
        }
    };
}// namespace NGIN::Net::detail
