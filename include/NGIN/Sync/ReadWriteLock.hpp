#pragma once

#include <NGIN/Sync/SharedMutex.hpp>

namespace NGIN::Sync
{
    /// @brief A read-write lock that allows multiple readers or a single writer at a time.
    /// @note This is a wrapper around the NGIN::Sync::SharedMutex class.
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
        void StartRead() noexcept(noexcept(LockShared()))
        {
            LockShared();
        }

        /// @brief Releases a previously acquired read lock
        void EndRead() noexcept(noexcept(UnlockShared()))
        {
            UnlockShared();
        }

        /// @brief Attempts to acquire a shared read lock without blocking
        /// @return true if the read lock was acquired; otherwise false.
        [[nodiscard]] bool TryStartRead() noexcept(noexcept(TryLockShared()))
        {
            return TryLockShared();
        }

        /// @brief Acquires an exclusive write lock, blocking if necessary
        /// @note Only one thread can hold a write lock at a time
        void StartWrite() noexcept(noexcept(Lock()))
        {
            Lock();
        }

        /// @brief Releases a previously acquired write lock
        void EndWrite() noexcept(noexcept(Unlock()))
        {
            Unlock();
        }

        /// @brief Attempts to acquire an exclusive write lock without blocking
        /// @return true if the write lock was acquired; otherwise false.
        [[nodiscard]] bool TryStartWrite() noexcept(noexcept(TryLock()))
        {
            return TryLock();
        }

        void lock() noexcept(noexcept(StartWrite()))
        {
            StartWrite();
        }

        void unlock() noexcept(noexcept(EndWrite()))
        {
            EndWrite();
        }

        void lock_shared() noexcept(noexcept(StartRead()))
        {
            StartRead();
        }

        void unlock_shared() noexcept(noexcept(EndRead()))
        {
            EndRead();
        }

        [[nodiscard]] bool try_lock() noexcept(noexcept(TryStartWrite()))
        {
            return TryStartWrite();
        }

        [[nodiscard]] bool try_lock_shared() noexcept(noexcept(TryStartRead()))
        {
            return TryStartRead();
        }
    };
}// namespace NGIN::Sync
