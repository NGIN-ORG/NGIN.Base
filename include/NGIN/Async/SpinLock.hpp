#pragma once

#include <NGIN/Async/ILockable.hpp>
#include <atomic>
#include <thread>

namespace NGIN::Async
{
    /// @brief A simple spin lock implementation
    class SpinLock : public ILockable
    {
    public:
        SpinLock()                           = default;
        SpinLock(const SpinLock&)            = delete;
        SpinLock& operator=(const SpinLock&) = delete;

        void Lock() noexcept override
        {
            int backoff = 1;
            while (true)
            {
                bool wasLocked = lock.load(std::memory_order_relaxed);
                if (!wasLocked && lock.compare_exchange_weak(wasLocked, true, std::memory_order_acquire))
                    break;

                for (int i = 0; i < backoff; ++i)
                    std::this_thread::yield();

                if (backoff < 1024)// Cap the backoff to avoid excessive delays
                    backoff *= 2;
            }
        }


        void Unlock() noexcept override
        {
            lock.store(false, std::memory_order_release);
        }

        [[nodiscard]] bool TryLock() noexcept override
        {
            bool expected = false;
            return lock.compare_exchange_strong(expected, true, std::memory_order_acquire);
        }

    private:
        std::atomic<bool> lock {false};
    };
}// namespace NGIN::Async