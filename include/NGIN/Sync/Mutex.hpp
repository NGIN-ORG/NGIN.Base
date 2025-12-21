#pragma once

#include <mutex>

namespace NGIN::Sync
{
    /// @brief A simple mutex wrapper.
    class Mutex
    {
    public:
        Mutex()                        = default;
        Mutex(const Mutex&)            = delete;
        Mutex& operator=(const Mutex&) = delete;
        ~Mutex()                       = default;

        void Lock()
        {
            m_mutex.lock();
        }

        void Unlock()
        {
            m_mutex.unlock();
        }

        [[nodiscard]] bool TryLock() noexcept
        {
            return m_mutex.try_lock();
        }

        void lock()
        {
            Lock();
        }

        void unlock()
        {
            Unlock();
        }

        [[nodiscard]] bool try_lock() noexcept
        {
            return TryLock();
        }

    private:
        std::mutex m_mutex {};
    };
}// namespace NGIN::Sync
