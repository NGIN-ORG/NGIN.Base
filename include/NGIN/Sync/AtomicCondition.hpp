
#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Primitives.hpp>// Assumes UInt32 is defined here
#include <NGIN/Units.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <thread>

#ifdef _DEBUG
#include <cassert>
#endif

namespace NGIN::Sync
{
    namespace detail
    {
#if defined(NGIN_PLATFORM_WINDOWS) || defined(NGIN_PLATFORM_LINUX)
        NGIN_FOUNDATION_API void AtomicConditionWait(UInt32& generation, UInt32 observedGeneration) noexcept;
        NGIN_FOUNDATION_API bool AtomicConditionWaitFor(UInt32& generation, UInt32 observedGeneration, UInt64 nanoseconds) noexcept;
        NGIN_FOUNDATION_API void AtomicConditionNotifyOne(UInt32& generation) noexcept;
        NGIN_FOUNDATION_API void AtomicConditionNotifyAll(UInt32& generation) noexcept;
#endif
    }// namespace detail

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
            Wait(Load());
#ifdef _DEBUG
            m_waitingThreads.fetch_sub(1, std::memory_order_relaxed);
#endif
        }

        /// @brief Wait until the generation differs from @p observedGeneration.
        ///
        /// This is the safe building block for predicate loops (prevents missed notifications).
        void Wait(UInt32 observedGeneration) noexcept
        {
#if defined(NGIN_PLATFORM_WINDOWS) || defined(NGIN_PLATFORM_LINUX)
            detail::AtomicConditionWait(m_generation, observedGeneration);
#else
            auto generation = Generation();
            generation.wait(observedGeneration, std::memory_order_acquire);
#endif
        }

        [[nodiscard]] UInt32 Load() const noexcept
        {
            auto generation = Generation();
            return generation.load(std::memory_order_acquire);
        }

        template<typename TUnit>
            requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
        [[nodiscard]] bool WaitFor(const TUnit& duration) noexcept
        {
#ifdef _DEBUG
            m_waitingThreads.fetch_add(1, std::memory_order_relaxed);
#endif
            const UInt32 gen = Load();

            const auto nanoseconds = ToWaitNanoseconds(duration);
            if (nanoseconds == 0)
            {
#ifdef _DEBUG
                m_waitingThreads.fetch_sub(1, std::memory_order_relaxed);
#endif
                return false;
            }

#if defined(NGIN_PLATFORM_WINDOWS) || defined(NGIN_PLATFORM_LINUX)
            const bool ok = detail::AtomicConditionWaitFor(m_generation, gen, nanoseconds);
#ifdef _DEBUG
            m_waitingThreads.fetch_sub(1, std::memory_order_relaxed);
#endif
            return ok;
#else
            const bool notified = WaitFor(gen, duration);
#ifdef _DEBUG
            m_waitingThreads.fetch_sub(1, std::memory_order_relaxed);
#endif
            return notified;
#endif
        }

        template<typename TUnit>
            requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
        [[nodiscard]] bool WaitFor(UInt32 observedGeneration, const TUnit& duration) noexcept
        {
            const auto nanoseconds = ToWaitNanoseconds(duration);
            if (nanoseconds == 0)
            {
                return false;
            }

#if defined(NGIN_PLATFORM_WINDOWS) || defined(NGIN_PLATFORM_LINUX)
            return detail::AtomicConditionWaitFor(m_generation, observedGeneration, nanoseconds);
#else
            using Nanoseconds = std::chrono::nanoseconds;
            using Rep         = Nanoseconds::rep;

            const auto clampedNanoseconds =
                    (std::min) (nanoseconds, static_cast<UInt64>((std::numeric_limits<Rep>::max)()));
            const auto timeout = Nanoseconds {static_cast<Rep>(clampedNanoseconds)};
            const auto started = std::chrono::steady_clock::now();

            while (Load() == observedGeneration)
            {
                const auto now = std::chrono::steady_clock::now();
                if (now - started >= timeout)
                {
                    return false;
                }

                const auto remaining = timeout - (now - started);
                if (remaining > std::chrono::milliseconds {1})
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds {1});
                }
                else
                {
                    std::this_thread::yield();
                }
            }
            return true;
#endif
        }

        /// @brief Notifies a single waiting thread.
        ///
        /// Increments the generation counter, then wakes one waiting thread.
        void NotifyOne() noexcept
        {
            auto generation = Generation();
            generation.fetch_add(1u, std::memory_order_release);
#if defined(NGIN_PLATFORM_WINDOWS) || defined(NGIN_PLATFORM_LINUX)
            detail::AtomicConditionNotifyOne(m_generation);
#else
            generation.notify_one();
#endif
        }

        /// @brief Notifies all waiting threads.
        ///
        /// Increments the generation counter, then wakes all waiting threads.
        void NotifyAll() noexcept
        {
            auto generation = Generation();
            generation.fetch_add(1u, std::memory_order_release);
#if defined(NGIN_PLATFORM_WINDOWS) || defined(NGIN_PLATFORM_LINUX)
            detail::AtomicConditionNotifyAll(m_generation);
#else
            generation.notify_all();
#endif
        }

#ifdef _DEBUG
        /// @brief Get the current generation number (debug only)
        /// @return The current generation number
        UInt32 GetGeneration() const noexcept
        {
            auto generation = Generation();
            return generation.load(std::memory_order_relaxed);
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
        template<typename TUnit>
            requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
        [[nodiscard]] static UInt64 ToWaitNanoseconds(const TUnit& duration) noexcept
        {
            const auto value = NGIN::Units::UnitCast<NGIN::Units::Nanoseconds>(duration).GetValue();
            if (!(value > 0.0))
            {
                return 0;
            }

            constexpr auto maximum = (std::numeric_limits<UInt64>::max)();
            if (!std::isfinite(value) || value >= static_cast<double>(maximum))
            {
                return maximum;
            }

            const auto truncated = static_cast<UInt64>(value);
            return (static_cast<double>(truncated) < value) ? truncated + 1ull : truncated;
        }

        [[nodiscard]] std::atomic_ref<UInt32> Generation() noexcept
        {
            return std::atomic_ref<UInt32>(m_generation);
        }

        [[nodiscard]] std::atomic_ref<UInt32> Generation() const noexcept
        {
            return std::atomic_ref<UInt32>(const_cast<UInt32&>(m_generation));
        }

        // The generation counter serves as the shared state. Threads wait for its change.
        alignas(alignof(UInt32)) UInt32 m_generation;
#ifdef _DEBUG
        // Counter for the number of threads currently waiting
        std::atomic<UInt32> m_waitingThreads;
#endif
    };

}// namespace NGIN::Sync
