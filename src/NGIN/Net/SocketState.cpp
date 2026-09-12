#include "SocketState.hpp"
#include "SocketPlatform.hpp"

#include <cassert>

namespace NGIN::Net::detail
{
    namespace
    {
        bool CloseNative(SocketState::NativeHandle native) noexcept
        {
            if (native == SocketState::Invalid)
                return true;
#if defined(NGIN_PLATFORM_WINDOWS)
            return ::closesocket(static_cast<SOCKET>(native)) == 0;
#else
            // Never retry close: an interrupted call can already have released
            // the descriptor, which another thread may immediately reuse.
            return ::close(static_cast<int>(native)) == 0;
#endif
        }
    }// namespace

    SocketState::~SocketState()
    {
        assert(m_busy == 0);
        (void) RequestClose();
    }

    SocketState::NativeHandle SocketState::VisibleNative() const noexcept
    {
        return IsClosing() ? Invalid : m_native.load(std::memory_order_acquire);
    }

    bool SocketState::RequestClose() noexcept
    {
        NativeHandle retired = Invalid;
        {
            std::lock_guard lock(m_mutex);
            if (m_closing.exchange(true, std::memory_order_acq_rel))
                return true;
            if (m_busy == 0)
                retired = m_native.exchange(Invalid, std::memory_order_acq_rel);
        }
        // Cancellation callbacks may queue runtime work. They cannot run under
        // this mutex, and the native handle remains pinned by admitted leases.
        m_close.Cancel();
        return CloseNative(retired);
    }

    void SocketState::Adopt(NativeHandle native) noexcept
    {
        std::lock_guard lock(m_mutex);
        assert(!m_closing.load(std::memory_order_relaxed) && m_busy == 0 && m_native == Invalid);
        m_native.store(native, std::memory_order_release);
    }

    std::expected<SocketState::NativeHandle, NetError> SocketState::Acquire(unsigned directions) noexcept
    {
        std::lock_guard lock(m_mutex);
        if (directions == 0 || (directions & ~Exclusive) != 0)
            return std::unexpected(NetError {NetErrorCode::InvalidArgument});
        if (m_closing.load(std::memory_order_relaxed) || m_native == Invalid)
            return std::unexpected(NetError {NetErrorCode::Disconnected});
        if ((m_busy & directions) != 0)
            return std::unexpected(NetError {NetErrorCode::OperationInProgress});
        m_busy |= directions;
        return m_native.load(std::memory_order_relaxed);
    }

    void SocketState::Release(unsigned directions) noexcept
    {
        NativeHandle retired = Invalid;
        {
            std::lock_guard lock(m_mutex);
            assert((m_busy & directions) == directions);
            m_busy &= ~directions;
            if (m_busy == 0 && m_closing.load(std::memory_order_relaxed))
                retired = m_native.exchange(Invalid, std::memory_order_acq_rel);
        }
        (void) CloseNative(retired);
    }

    std::expected<SocketLease, NetError> SocketLease::Acquire(std::shared_ptr<SocketState> state, unsigned directions) noexcept
    {
        if (!state)
            return std::unexpected(NetError {NetErrorCode::Disconnected});
        auto admitted = state->Acquire(directions);
        if (!admitted)
            return std::unexpected(admitted.error());
        SocketLease lease;
        lease.m_state      = std::move(state);
        lease.m_native     = *admitted;
        lease.m_directions = directions;
        return lease;
    }

    void SocketLease::Reset() noexcept
    {
        if (m_state)
        {
            m_state->Release(m_directions);
            m_state.reset();
            m_native     = SocketState::Invalid;
            m_directions = 0;
        }
    }

    bool CloseSocket(SocketHandle& handle) noexcept
    {
        return !handle.m_state || handle.m_state->RequestClose();
    }
}// namespace NGIN::Net::detail

namespace NGIN::Net
{
    SocketHandle::SocketHandle(NativeHandle native)
    {
        if (native == detail::SocketState::Invalid)
            return;
        try
        {
            m_state = std::make_shared<detail::SocketState>(native);
        } catch (...)
        {
#if defined(NGIN_PLATFORM_WINDOWS)
            (void) ::closesocket(static_cast<SOCKET>(native));
#else
            (void) ::close(static_cast<int>(native));
#endif
            throw;
        }
    }
    SocketHandle::SocketHandle(SocketHandle&& other) noexcept : m_state(std::move(other.m_state)) {}
    SocketHandle& SocketHandle::operator=(SocketHandle&& other) noexcept
    {
        if (this != &other)
        {
            Close();
            m_state = std::move(other.m_state);
        }
        return *this;
    }
    SocketHandle::~SocketHandle()
    {
        Close();
    }
    bool SocketHandle::IsOpen() const noexcept
    {
        return Native() != detail::SocketState::Invalid;
    }
    SocketHandle::NativeHandle SocketHandle::Native() const noexcept
    {
        return m_state ? m_state->VisibleNative() : detail::SocketState::Invalid;
    }
    void SocketHandle::Close() noexcept
    {
        (void) detail::CloseSocket(*this);
    }
}// namespace NGIN::Net
