/// @file AsyncSemaphore.hpp
/// @brief Cancellation-aware counting semaphore for coroutine synchronization.
#pragma once

#include <NGIN/Async/Task.hpp>
#include <NGIN/Defines.hpp>
#include <NGIN/Primitives.hpp>
#include <limits>
#include <mutex>
#include <utility>

namespace NGIN::Async
{
    /// @brief A thread-safe counting semaphore whose pending waits suspend the coroutine.
    /// @details Pending waiters receive permits in FIFO order, on their inherited executor.
    /// Cancellation and release race to claim a waiter; a granted permit is never revoked.
    /// The semaphore must outlive all calls, tasks returned by it, and Permit guards.
    /// Destruction does not cancel waits. Cancel and join outstanding work first.
    /// Methods briefly lock internal bookkeeping, but never block a thread waiting for a permit.
    class NGIN_EXECUTION_API AsyncSemaphore final
    {
    public:
        /// @brief Move-only ownership of one permit. Destruction releases it.
        /// @details May be moved across threads; access to the same guard must be serialized.
        class NGIN_EXECUTION_API Permit final
        {
        public:
            Permit() noexcept                = default;
            Permit(const Permit&)            = delete;
            Permit& operator=(const Permit&) = delete;
            Permit(Permit&& other) noexcept;
            Permit& operator=(Permit&& other) noexcept;
            ~Permit();

            /// @brief Releases ownership early; repeated calls are harmless.
            void               Release() noexcept;
            [[nodiscard]] bool OwnsPermit() const noexcept { return m_semaphore != nullptr; }

        private:
            friend class AsyncSemaphore;
            explicit Permit(AsyncSemaphore& semaphore) noexcept : m_semaphore(&semaphore) {}
            AsyncSemaphore* m_semaphore {};
        };

        /// @pre maximumCount > 0 and initialCount <= maximumCount.
        explicit AsyncSemaphore(UIntSize initialCount, UIntSize maximumCount = (std::numeric_limits<UIntSize>::max)()) noexcept;
        ~AsyncSemaphore();
        AsyncSemaphore(const AsyncSemaphore&)            = delete;
        AsyncSemaphore& operator=(const AsyncSemaphore&) = delete;
        AsyncSemaphore(AsyncSemaphore&&)                 = delete;
        AsyncSemaphore& operator=(AsyncSemaphore&&)      = delete;

        /// @brief Cold task acquiring one permit; the caller must Release after success.
        /// @details Inherits cancellation, executor, and allocation resource from its caller.
        /// Pending storage uses that resource, which must outlive the wait and its delivery.
        /// Cancellation propagates Stopped; allocation/admission/registration failures propagate
        /// an AsyncFault without consuming a permit. No implicit deadline timer is installed.
        [[nodiscard]] Task<void> WaitAsync();

        /// @brief Acquires a permit in a task with domain-error type E.
        /// @details Use WaitAsync<E>() when awaiting from Task<T, E>; semaphore failures
        /// remain infrastructure faults and never manufacture a domain error.
        template<typename E>
        [[nodiscard]] Task<void, E> WaitAsync()
        {
            auto result = co_await WaitAsync().AsCompletion();
            if (result.HasError())
                co_await Faulted(std::move(result.Error().Fault()));
            else if (result.IsStopped())
                co_await Stopped(result.StopReason());
        }

        /// @brief Acquires one permit and returns a guard that releases it automatically.
        [[nodiscard]] Task<Permit> AcquireAsync();

        /// @brief Acquires a guard in a task with domain-error type E.
        template<typename E>
        [[nodiscard]] Task<Permit, E> AcquireAsync()
        {
            co_await WaitAsync<E>();
            co_return Permit(*this);
        }

        /// @brief Immediately acquires an available permit; does not bypass queued waiters.
        /// @details On success the caller must Release. Does not observe cancellation.
        [[nodiscard]] bool TryAcquire() noexcept;

        /// @brief Releases one permit, handing it to the oldest pending waiter if present.
        /// @pre If no waiters remain, the available count must be below maximumCount.
        void Release() noexcept;

        /// @brief A snapshot of available permits, excluding permits granted to queued deliveries.
        [[nodiscard]] UIntSize CurrentCount() const noexcept;

    private:
        struct Waiter;
        struct WaitAwaiter;
        void Remove(Waiter& waiter) noexcept;// Requires m_mutex.

        mutable std::mutex m_mutex;
        UIntSize           m_count;
        const UIntSize     m_maximumCount;
        Waiter*            m_head {};
        Waiter*            m_tail {};
    };
}// namespace NGIN::Async
