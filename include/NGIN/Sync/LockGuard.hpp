/// @file LockGuard.hpp
/// @brief Small RAII helpers for NGIN synchronization primitives.
#pragma once

#include <utility>

#include <NGIN/Sync/Concepts.hpp>

namespace NGIN::Sync
{
    template<BasicLockableConcept TLockable>
    class LockGuard final
    {
    public:
        explicit LockGuard(TLockable& lockable)
            : m_lockable(lockable)
        {
            m_lockable.lock();
        }

        LockGuard(const LockGuard&)            = delete;
        LockGuard& operator=(const LockGuard&) = delete;

        LockGuard(LockGuard&& other) noexcept
            : m_lockable(other.m_lockable)
            , m_owns(other.m_owns)
        {
            other.m_owns = false;
        }

        LockGuard& operator=(LockGuard&&) = delete;

        ~LockGuard()
        {
            if (m_owns)
            {
                m_lockable.unlock();
            }
        }

    private:
        TLockable& m_lockable;
        bool       m_owns {true};
    };

    template<SharedLockableConcept TLockable>
    class SharedLockGuard final
    {
    public:
        explicit SharedLockGuard(TLockable& lockable)
            : m_lockable(lockable)
        {
            m_lockable.lock_shared();
        }

        SharedLockGuard(const SharedLockGuard&)            = delete;
        SharedLockGuard& operator=(const SharedLockGuard&) = delete;

        SharedLockGuard(SharedLockGuard&& other) noexcept
            : m_lockable(other.m_lockable)
            , m_owns(other.m_owns)
        {
            other.m_owns = false;
        }

        SharedLockGuard& operator=(SharedLockGuard&&) = delete;

        ~SharedLockGuard()
        {
            if (m_owns)
            {
                m_lockable.unlock_shared();
            }
        }

    private:
        TLockable& m_lockable;
        bool       m_owns {true};
    };
}// namespace NGIN::Sync

