
#pragma once

#include <atomic>
#include <thread>
#include <chrono>
#include <NGIN/Primitives.hpp>// Assumes UInt32 is defined here

#ifdef _DEBUG
#include <cassert>
#endif

namespace NGIN::Sync
{

    /// @brief A minimal condition-like object using C++20 atomic wait/notify.
    ///
    /// This object is designed for simple thread pool scenarios where threads
    /// just wait until a notification occurs (either one thread or all). There are
    /// no predicates or locks involved.
    ///
    /// @note Wait() will indefinitely block the calling thread until a notification is received.
    class AtomicCondition
    {
    public:
        AtomicCondition() noexcept
            : m_generation(0)
#ifdef _DEBUG
              ,
              m_waitingThreads(0)
#endif

        {
        }

        AtomicCondition(const AtomicCondition&)            = delete;
        AtomicCondition& operator=(const AtomicCondition&) = delete;

        /// @brief Blocks the calling thread until a notification is received.
        ///
        /// Each waiting thread captures the current generation value and waits until
        /// that value is changed by a call to NotifyOne or NotifyAll.
        void Wait() noexcept
        {
#ifdef _DEBUG
            m_waitingThreads.fetch_add(1, std::memory_order_relaxed);
#endif
            // Capture the current generation.
            UInt32 gen = m_generation.load(std::memory_order_acquire);
            // Block until m_generation != gen.
            // The wait() call will automatically yield the thread and resume when notified.
            m_generation.wait(gen, std::memory_order_acquire);
#ifdef _DEBUG
            m_waitingThreads.fetch_sub(1, std::memory_order_relaxed);
#endif
        }

        /// @brief Notifies a single waiting thread.
        ///
        /// Increments the generation counter, then wakes one waiting thread.
        void NotifyOne() noexcept
        {
#ifdef _DEBUG
            // Assert that there are threads waiting when notify is called
            assert(m_waitingThreads.load(std::memory_order_relaxed) > 0 &&
                   "NotifyOne called but no threads are waiting!");
#endif
            m_generation.fetch_add(1u, std::memory_order_release);
            m_generation.notify_one();
        }

        /// @brief Notifies all waiting threads.
        ///
        /// Increments the generation counter, then wakes all waiting threads.
        void NotifyAll() noexcept
        {
#ifdef _DEBUG
            // Assert that there are threads waiting when notify is called
            assert(m_waitingThreads.load(std::memory_order_relaxed) > 0 &&
                   "NotifyAll called but no threads are waiting!");
#endif
            m_generation.fetch_add(1u, std::memory_order_release);
            m_generation.notify_all();
        }

#ifdef _DEBUG
        /// @brief Get the current generation number (debug only)
        /// @return The current generation number
        UInt32 GetGeneration() const noexcept
        {
            return m_generation.load(std::memory_order_relaxed);
        }

        /// @brief Get the number of threads currently waiting (debug only)
        /// @return The number of waiting threads
        UInt32 GetWaitingThreadCount() const noexcept
        {
            return m_waitingThreads.load(std::memory_order_relaxed);
        }

        /// @brief Check if any threads are currently waiting (debug only)
        /// @return true if there are threads waiting
        bool HasWaitingThreads() const noexcept
        {
            return m_waitingThreads.load(std::memory_order_relaxed) > 0;
        }
#endif

    private:
        // The generation counter serves as the shared state. Threads wait for its change.
        std::atomic<UInt32> m_generation;
#ifdef _DEBUG
        // Counter for the number of threads currently waiting
        std::atomic<UInt32> m_waitingThreads;
#endif
    };

}// namespace NGIN::Sync
