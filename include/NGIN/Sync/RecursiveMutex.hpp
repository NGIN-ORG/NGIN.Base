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

        void Lock() noexcept(noexcept(m_recursiveMutex.lock()))
        {
            m_recursiveMutex.lock();
        }

        void Unlock() noexcept(noexcept(m_recursiveMutex.unlock()))
        {
            m_recursiveMutex.unlock();
        }

        [[nodiscard]] bool TryLock() noexcept(noexcept(m_recursiveMutex.try_lock()))
        {
            return m_recursiveMutex.try_lock();
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
        std::recursive_mutex m_recursiveMutex;
    };
}// namespace NGIN::Sync
