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

        void Lock() noexcept(noexcept(m_mutex.lock()))
        {
            m_mutex.lock();
        }

        void Unlock() noexcept(noexcept(m_mutex.unlock()))
        {
            m_mutex.unlock();
        }

        [[nodiscard]] bool TryLock() noexcept(noexcept(m_mutex.try_lock()))
        {
            return m_mutex.try_lock();
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
        std::mutex m_mutex {};
    };
}// namespace NGIN::Sync
