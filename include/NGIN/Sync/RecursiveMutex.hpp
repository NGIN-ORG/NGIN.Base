#pragma once

#include <mutex>
#include <NGIN/Sync/ILockable.hpp>

namespace NGIN::Sync
{
    /// @brief A simple recursive mutex wrapper.
    class RecursiveMutex : public ILockable
    {
    public:
        RecursiveMutex()                                 = default;
        RecursiveMutex(const RecursiveMutex&)            = delete;
        RecursiveMutex& operator=(const RecursiveMutex&) = delete;
        ~RecursiveMutex()                                = default;

        void Lock() noexcept override
        {
            m_recursiveMutex.lock();
        }

        void Unlock() noexcept override
        {
            m_recursiveMutex.unlock();
        }

        [[nodiscard]] bool TryLock() noexcept override
        {
            return m_recursiveMutex.try_lock();
        }

    private:
        std::recursive_mutex m_recursiveMutex;
    };
}// namespace NGIN::Sync
