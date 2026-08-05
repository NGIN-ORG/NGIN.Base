/// @file TicketLock.hpp
/// @brief Fair spin lock that admits waiters in ticket order.
#pragma once

#include <NGIN/Primitives.hpp>
#include <atomic>
#include <thread>

namespace NGIN::Sync
{
    /// @brief Mutual-exclusion primitive that serves contending threads in FIFO ticket order.
    ///
    /// TicketLock satisfies the C++ BasicLockable and Lockable named requirements. Waiting
    /// threads yield their time slice while polling, so this lock is intended for short
    /// critical sections where fairness matters.
    class TicketLock
    {
    public:
        /// @brief Constructs an unlocked ticket lock.
        TicketLock()                             = default;
        TicketLock(const TicketLock&)            = delete;
        TicketLock& operator=(const TicketLock&) = delete;

        /// @brief Blocks until the caller's ticket owns the lock.
        void Lock() noexcept
        {
            const UInt32 ticket = m_nextTicket.fetch_add(1u, std::memory_order_relaxed);
            while (m_nowServing.load(std::memory_order_acquire) != ticket)
            {
                std::this_thread::yield();
            }
        }

        /// @brief Attempts to acquire the lock without waiting.
        /// @return `true` when the caller acquired the lock; otherwise `false`.
        [[nodiscard]] bool TryLock() noexcept
        {
            UInt32 current = m_nowServing.load(std::memory_order_acquire);
            if (m_nextTicket.load(std::memory_order_acquire) != current)
            {
                return false;
            }
            if (m_nextTicket.compare_exchange_strong(current, current + 1u, std::memory_order_acquire))
            {
                return true;
            }
            return false;
        }

        /// @brief Releases the lock and admits the next ticket holder.
        void Unlock() noexcept
        {
            m_nowServing.fetch_add(1u, std::memory_order_release);
        }

        /// @brief Standard-library-compatible spelling of Lock().
        void lock() noexcept
        {
            Lock();
        }

        /// @brief Standard-library-compatible spelling of Unlock().
        void unlock() noexcept
        {
            Unlock();
        }

        /// @brief Standard-library-compatible spelling of TryLock().
        /// @return `true` when the caller acquired the lock; otherwise `false`.
        [[nodiscard]] bool try_lock() noexcept
        {
            return TryLock();
        }

    private:
        std::atomic<UInt32> m_nextTicket {0};
        std::atomic<UInt32> m_nowServing {0};
    };

}// namespace NGIN::Sync
