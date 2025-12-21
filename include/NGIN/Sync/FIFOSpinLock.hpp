#pragma once

#include <atomic>
#include <thread>
#include <NGIN/Primitives.hpp>

namespace NGIN::Sync
{

    class TicketLock
    {
    public:
        TicketLock()                             = default;
        TicketLock(const TicketLock&)            = delete;
        TicketLock& operator=(const TicketLock&) = delete;

        void Lock() noexcept
        {
            // Fetch a ticket (the order in which threads will be served)
            const UInt32 ticket = m_nextTicket.fetch_add(1u, std::memory_order_relaxed);
            // Spin until our ticket is now being served
            while (m_nowServing.load(std::memory_order_acquire) != ticket)
            {
                std::this_thread::yield();
            }
        }

        [[nodiscard]]
        bool TryLock() noexcept
        {
            // If the lock is free, m_nextTicket and m_nowServing are equal.
            UInt32 current = m_nowServing.load(std::memory_order_acquire);
            if (m_nextTicket.load(std::memory_order_acquire) != current)
                return false;
            // Attempt to acquire the lock by claiming the ticket.
            if (m_nextTicket.compare_exchange_strong(current, current + 1u, std::memory_order_acquire))
            {
                // Since we were the only one, we don’t need to wait.
                return true;
            }
            return false;
        }

        void Unlock() noexcept
        {
            // Release the lock by allowing the next ticket to be served.
            m_nowServing.fetch_add(1u, std::memory_order_release);
        }

        void lock() noexcept
        {
            Lock();
        }

        void unlock() noexcept
        {
            Unlock();
        }

        [[nodiscard]] bool try_lock() noexcept
        {
            return TryLock();
        }

    private:
        std::atomic<UInt32> m_nextTicket {0};
        std::atomic<UInt32> m_nowServing {0};
    };

}// namespace NGIN::Sync
