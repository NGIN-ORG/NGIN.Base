#pragma once

#include <mutex>

namespace NGIN::Sync
{
    /// @brief A simple recursive mutex wrapper.
    class RecursiveMutex
    {
    public:
        RecursiveMutex()                                 = default;
        RecursiveMutex(const RecursiveMutex&)            = delete;
        RecursiveMutex& operator=(const RecursiveMutex&) = delete;
        ~RecursiveMutex()                                = default;

        void Lock() noexcept
        {
            m_recursiveMutex.lock();
        }

        void Unlock() noexcept
        {
            m_recursiveMutex.unlock();
        }

        [[nodiscard]] bool TryLock() noexcept
        {
            return m_recursiveMutex.try_lock();
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
        std::recursive_mutex m_recursiveMutex;
    };
}// namespace NGIN::Sync
