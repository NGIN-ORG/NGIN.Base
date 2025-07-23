#pragma once

#include <shared_mutex>
#include <NGIN/Async/ILockable.hpp>

namespace NGIN::Async
{

    class SharedMutex : public ILockable
    {
    public:
        SharedMutex()                              = default;
        SharedMutex(const SharedMutex&)            = delete;
        SharedMutex& operator=(const SharedMutex&) = delete;

        // Exclusive locking
        void Lock() noexcept override
        {
            mutex.lock();
        }
        bool TryLock() noexcept override
        {
            return mutex.try_lock();
        }
        void Unlock() noexcept override
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

    private:
        std::shared_mutex mutex;
    };

}// namespace NGIN::Async
