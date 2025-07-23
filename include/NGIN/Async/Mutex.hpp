#pragma once

#include <mutex>
#include <NGIN/Async/ILockable.hpp>

namespace NGIN::Async
{
    /// @brief A simple mutex wrapper.
    class Mutex : public ILockable
    {
    public:
        Mutex()                        = default;
        Mutex(const Mutex&)            = delete;
        Mutex& operator=(const Mutex&) = delete;
        ~Mutex()                       = default;

        void Lock() override
        {
            m_mutex.lock();
        }

        void Unlock() override
        {
            m_mutex.unlock();
        }

        [[nodiscard]] bool TryLock() noexcept override
        {
            return m_mutex.try_lock();
        }

    private:
        std::mutex m_mutex {};
    };
}// namespace NGIN::Async