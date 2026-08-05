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
        void Lock() noexcept(noexcept(mutex.lock()))
        {
            mutex.lock();
        }
        [[nodiscard]] bool TryLock() noexcept(noexcept(mutex.try_lock()))
        {
            return mutex.try_lock();
        }
        void Unlock() noexcept(noexcept(mutex.unlock()))
        {
            mutex.unlock();
        }

        // Shared locking
        void LockShared() noexcept(noexcept(mutex.lock_shared()))
        {
            mutex.lock_shared();
        }
        [[nodiscard]] bool TryLockShared() noexcept(noexcept(mutex.try_lock_shared()))
        {
            return mutex.try_lock_shared();
        }
        void UnlockShared() noexcept(noexcept(mutex.unlock_shared()))
        {
            mutex.unlock_shared();
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

        void lock_shared() noexcept(noexcept(LockShared()))
        {
            LockShared();
        }

        void unlock_shared() noexcept(noexcept(UnlockShared()))
        {
            UnlockShared();
        }

        [[nodiscard]] bool try_lock_shared() noexcept(noexcept(TryLockShared()))
        {
            return TryLockShared();
        }

    private:
        std::shared_mutex mutex;
    };

}// namespace NGIN::Sync
