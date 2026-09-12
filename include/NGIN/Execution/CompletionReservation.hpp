/// @file CompletionReservation.hpp
/// @brief Reserved executor storage for terminal and reusable continuations.
#pragma once

#include <utility>

namespace NGIN::Execution
{
    namespace detail
    {
        class CompletionQueue;
    }

    /// @brief Move-only admission ticket for allocation-free terminal delivery.
    /// @details The executor and its driver must outlive this ticket and the delivered work.
    /// Destruction releases ownership; already queued work still finishes. Releasing
    /// an unused terminal ticket is valid only before backend admission or when no
    /// terminal completion is required.
    /// Ticket access is single-owner; distinct tickets may be dispatched concurrently.
    class CompletionReservation final
    {
    public:
        CompletionReservation() noexcept                               = default;
        CompletionReservation(const CompletionReservation&)            = delete;
        CompletionReservation& operator=(const CompletionReservation&) = delete;
        CompletionReservation(CompletionReservation&& other) noexcept
            : m_state(std::exchange(other.m_state, nullptr)), m_dispatch(other.m_dispatch), m_release(other.m_release), m_schedule(other.m_schedule)
        {
        }
        CompletionReservation& operator=(CompletionReservation&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                m_state    = std::exchange(other.m_state, nullptr);
                m_dispatch = other.m_dispatch;
                m_release  = other.m_release;
                m_schedule = other.m_schedule;
            }
            return *this;
        }
        ~CompletionReservation() { Reset(); }

        /// @brief Whether terminal delivery is still reserved by this ticket.
        [[nodiscard]] bool IsValid() const noexcept { return m_state != nullptr; }

        /// @brief Transfers the reserved work to its executor, without allocation or rejection.
        /// @details Never invokes user work inline. Repeated calls on an empty ticket do nothing.
        void Dispatch() noexcept
        {
            if (void* state = std::exchange(m_state, nullptr))
                m_dispatch(state);
        }

        /// @brief Queues the reserved callback while retaining its storage for another delivery.
        /// @details At most one delivery may be pending. Scheduling from a running callback
        /// queues its next invocation after the current invocation returns. Reset releases
        /// ownership; a queued or running invocation still finishes before storage is freed.
        /// This permits an admitted task to resume for cleanup after admission closes.
        void Schedule() const noexcept
        {
            if (void* state = m_state)
                m_schedule(state);
        }

        /// @brief Releases ownership; queued or running work finishes before storage is freed.
        void Reset() noexcept
        {
            if (void* state = std::exchange(m_state, nullptr))
                m_release(state);
        }

    private:
        friend class detail::CompletionQueue;
        CompletionReservation(void* state, void (*dispatch)(void*) noexcept, void (*release)(void*) noexcept, void (*schedule)(void*) noexcept) noexcept
            : m_state(state), m_dispatch(dispatch), m_release(release), m_schedule(schedule)
        {
        }
        void* m_state {};
        void (*m_dispatch)(void*) noexcept {};
        void (*m_release)(void*) noexcept {};
        void (*m_schedule)(void*) noexcept {};
    };
}// namespace NGIN::Execution
