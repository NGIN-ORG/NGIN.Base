
#pragma once

#include <atomic>
#include <thread>
#include <cstdint>
#include <climits>
#include <NGIN/Primitives.hpp>// Assumes UInt32 is defined here
#include <NGIN/Units.hpp>

#ifdef _DEBUG
#include <cassert>
#endif

#if defined(_WIN32)
#include <Windows.h>
#include <synchapi.h>
#elif defined(__linux__)
#include <errno.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
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
#if defined(_WIN32)
            (void)::WaitOnAddress(static_cast<volatile void*>(&m_generation), &observedGeneration, sizeof(UInt32), INFINITE);
#elif defined(__linux__)
            for (;;)
            {
                const int rc = static_cast<int>(::syscall(SYS_futex, static_cast<std::uint32_t*>(&m_generation),
                                                          FUTEX_WAIT_PRIVATE, observedGeneration, nullptr, nullptr, 0));
                if (rc == 0)
                {
                    break;
                }
                if (errno == EAGAIN)
                {
                    break;
                }
                if (errno == EINTR)
                {
                    continue;
                }
                break;
            }
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

            const auto nsDouble = NGIN::Units::UnitCast<NGIN::Units::Nanoseconds>(duration).GetValue();
            if (nsDouble <= 0.0)
            {
#ifdef _DEBUG
                m_waitingThreads.fetch_sub(1, std::memory_order_relaxed);
#endif
                return false;
            }

            const auto nsTruncated = static_cast<UInt64>(nsDouble);
            const auto ns          = (static_cast<double>(nsTruncated) < nsDouble) ? (nsTruncated + 1ull) : nsTruncated;

#if defined(_WIN32)
            const DWORD ms = static_cast<DWORD>((ns + 999'999ull) / 1'000'000ull);
            const BOOL  ok = ::WaitOnAddress(static_cast<volatile void*>(&m_generation), &gen, sizeof(UInt32), ms);
#ifdef _DEBUG
            m_waitingThreads.fetch_sub(1, std::memory_order_relaxed);
#endif
            return ok != 0;
#elif defined(__linux__)
            timespec ts {};
            ts.tv_sec  = static_cast<time_t>(ns / 1'000'000'000ull);
            ts.tv_nsec = static_cast<long>(ns % 1'000'000'000ull);

            int rc = -1;
            for (;;)
            {
                rc = static_cast<int>(::syscall(SYS_futex, static_cast<std::uint32_t*>(&m_generation),
                                                FUTEX_WAIT_PRIVATE, gen, &ts, nullptr, 0));
                if (rc == 0 || errno != EINTR)
                {
                    break;
                }
            }
#ifdef _DEBUG
            m_waitingThreads.fetch_sub(1, std::memory_order_relaxed);
#endif
            if (rc == 0)
            {
                return true;
            }
            if (errno == ETIMEDOUT)
            {
                return false;
            }
            return Load() != gen;
#else
            Wait(gen);
#ifdef _DEBUG
            m_waitingThreads.fetch_sub(1, std::memory_order_relaxed);
#endif
            return true;
#endif
        }

        template<typename TUnit>
            requires NGIN::Units::QuantityOf<NGIN::Units::TIME, TUnit>
        [[nodiscard]] bool WaitFor(UInt32 observedGeneration, const TUnit& duration) noexcept
        {
            const auto nsDouble = NGIN::Units::UnitCast<NGIN::Units::Nanoseconds>(duration).GetValue();
            if (nsDouble <= 0.0)
            {
                return false;
            }

            const auto nsTruncated = static_cast<UInt64>(nsDouble);
            const auto ns          = (static_cast<double>(nsTruncated) < nsDouble) ? (nsTruncated + 1ull) : nsTruncated;

#if defined(_WIN32)
            const DWORD ms = static_cast<DWORD>((ns + 999'999ull) / 1'000'000ull);
            const BOOL  ok = ::WaitOnAddress(static_cast<volatile void*>(&m_generation), &observedGeneration, sizeof(UInt32), ms);
            return ok != 0;
#elif defined(__linux__)
            timespec ts {};
            ts.tv_sec  = static_cast<time_t>(ns / 1'000'000'000ull);
            ts.tv_nsec = static_cast<long>(ns % 1'000'000'000ull);

            int rc = -1;
            for (;;)
            {
                rc = static_cast<int>(::syscall(SYS_futex, static_cast<std::uint32_t*>(&m_generation),
                                                FUTEX_WAIT_PRIVATE, observedGeneration, &ts, nullptr, 0));
                if (rc == 0 || errno != EINTR)
                {
                    break;
                }
            }
            if (rc == 0)
            {
                return true;
            }
            if (errno == ETIMEDOUT)
            {
                return false;
            }
            return Load() != observedGeneration;
#else
            (void)duration;
            Wait(observedGeneration);
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
#if defined(_WIN32)
            ::WakeByAddressSingle(static_cast<void*>(&m_generation));
#elif defined(__linux__)
            (void)::syscall(SYS_futex, static_cast<std::uint32_t*>(&m_generation), FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0);
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
#if defined(_WIN32)
            ::WakeByAddressAll(static_cast<void*>(&m_generation));
#elif defined(__linux__)
            (void)::syscall(SYS_futex, static_cast<std::uint32_t*>(&m_generation), FUTEX_WAKE_PRIVATE, INT_MAX, nullptr,
                            nullptr, 0);
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
