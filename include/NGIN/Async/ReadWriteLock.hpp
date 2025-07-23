#pragma once

#include <shared_mutex>
#include <NGIN/Async/SharedMutex.hpp>

namespace NGIN::Async
{
    /// @brief A read-write lock that allows multiple readers or a single writer at a time.
    /// @note This is a wrapper around the NGIN::Async::SharedMutex class.
    class ReadWriteLock : private SharedMutex
    {
    public:
        /// @brief Default constructor
        ReadWriteLock() = default;
        /// @brief Copy constructor (deleted)
        ReadWriteLock(const ReadWriteLock&) = delete;
        /// @brief Copy assignment operator (deleted)
        ReadWriteLock& operator=(const ReadWriteLock&) = delete;

        /// @brief Acquires a shared read lock, blocking if necessary
        /// @note Multiple threads can hold read locks simultaneously
        void StartRead() noexcept
        {
            LockShared();
        }

        /// @brief Releases a previously acquired read lock
        void EndRead() noexcept
        {
            UnlockShared();
        }

        /// @brief Attempts to acquire a shared read lock without blocking
        /// @note Returns immediately if the lock cannot be acquired
        void TryStartRead() noexcept
        {
            TryLockShared();
        }

        /// @brief Acquires an exclusive write lock, blocking if necessary
        /// @note Only one thread can hold a write lock at a time
        void StartWrite() noexcept
        {
            Lock();
        }

        /// @brief Releases a previously acquired write lock
        void EndWrite() noexcept
        {
            Unlock();
        }

        /// @brief Attempts to acquire an exclusive write lock without blocking
        /// @note Returns immediately if the lock cannot be acquired
        void TryStartWrite() noexcept
        {
            TryLock();
        }
    };
}// namespace NGIN::Async