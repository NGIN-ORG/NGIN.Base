/// @file SpinLock.hpp
/// @brief Exponential-backoff spin lock for short critical sections.
#pragma once

#include <atomic>
#include <thread>

namespace NGIN::Sync
{
    /// @brief Mutual-exclusion primitive that uses bounded exponential yield backoff while waiting.
    class SpinLock
    {
    public:
        /// @brief Constructs an unlocked spin lock.
        SpinLock()                           = default;
        SpinLock(const SpinLock&)            = delete;
        SpinLock& operator=(const SpinLock&) = delete;

        /// @brief Blocks until the caller acquires the lock.
        void Lock() noexcept
        {
            int backoff = 1;
            while (true)
            {
                bool wasLocked = m_locked.load(std::memory_order_relaxed);
                if (!wasLocked && m_locked.compare_exchange_weak(wasLocked, true, std::memory_order_acquire))
                    break;

                for (int i = 0; i < backoff; ++i)
                    std::this_thread::yield();

                if (backoff < 1024)
                    backoff *= 2;
            }
        }

        /// @brief Releases the lock.
        void Unlock() noexcept
        {
            m_locked.store(false, std::memory_order_release);
        }

        /// @brief Attempts to acquire the lock without waiting.
        /// @return `true` when the caller acquired the lock; otherwise `false`.
        [[nodiscard]] bool TryLock() noexcept
        {
            bool expected = false;
            return m_locked.compare_exchange_strong(expected, true, std::memory_order_acquire);
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
        [[nodiscard]] bool try_lock() noexcept
        {
            return TryLock();
        }

    private:
        std::atomic<bool> m_locked {false};
    };
}// namespace NGIN::Sync
