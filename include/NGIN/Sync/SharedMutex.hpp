#pragma once

#include <shared_mutex>

namespace NGIN::Sync
{

    class SharedMutex
    {
    public:
        SharedMutex()                              = default;
        SharedMutex(const SharedMutex&)            = delete;
        SharedMutex& operator=(const SharedMutex&) = delete;

        // Exclusive locking
        void Lock() noexcept
        {
            mutex.lock();
        }
        bool TryLock() noexcept
        {
            return mutex.try_lock();
        }
        void Unlock() noexcept
        {
            mutex.unlock();
        }

        // Shared locking
        void LockShared() noexcept
        {
            mutex.lock_shared();
        }
        bool TryLockShared() noexcept
        {
            return mutex.try_lock_shared();
        }
        void UnlockShared() noexcept
        {
            mutex.unlock_shared();
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

        void lock_shared() noexcept
        {
            LockShared();
        }

        void unlock_shared() noexcept
        {
            UnlockShared();
        }

        [[nodiscard]] bool try_lock_shared() noexcept
        {
            return TryLockShared();
        }

    private:
        std::shared_mutex mutex;
    };

}// namespace NGIN::Sync
