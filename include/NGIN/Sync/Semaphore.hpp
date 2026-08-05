#pragma once

#include <semaphore>

namespace NGIN::Sync
{
    /// @brief A simple semaphore wrapper.
    template<int MaxCount = std::counting_semaphore<>::max()>
    class Semaphore
    {
    public:
        /// @brief Constructs a semaphore with a given number of initial permits.
        /// @param count The initial number of permits available.
        explicit Semaphore(int count = MaxCount) noexcept
            : semaphore(count)
        {
        }

        Semaphore(const Semaphore&)            = delete;
        Semaphore& operator=(const Semaphore&) = delete;

        /// @brief Acquires a permit, blocking if none are available.
        void Lock() noexcept(noexcept(semaphore.acquire()))
        {
            this->semaphore.acquire();
        }

        /// @brief Attempts to acquire a permit without blocking.
        /// @return true if a permit was acquired, false otherwise.
        [[nodiscard]] bool TryLock() noexcept(noexcept(semaphore.try_acquire()))
        {
            return this->semaphore.try_acquire();
        }

        /// @brief Releases a permit, increasing the available permits.
        void Unlock() noexcept(noexcept(semaphore.release()))
        {
            this->semaphore.release();
        }

        void lock() noexcept(noexcept(Lock()))
        {
            Lock();
        }

        void unlock() noexcept(noexcept(Unlock()))
        {
            Unlock();
        }

        [[nodiscard]] bool try_lock() noexcept(noexcept(TryLock()))
        {
            return TryLock();
        }

    private:
        std::counting_semaphore<MaxCount> semaphore;
    };
}// namespace NGIN::Sync
