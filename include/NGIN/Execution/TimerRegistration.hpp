/// @file TimerRegistration.hpp
/// @brief Move-only ownership of a removable executor timer.
#pragma once

#include <cstdint>
#include <utility>

namespace NGIN::Execution
{
    /// @brief Removes undispatched timed work on cancellation or destruction.
    /// @details Cancellation destroys queued work; it never invokes that work.
    /// If dispatch already claimed the timer, cancellation returns false and
    /// cannot preempt the callback. The executor must outlive this registration,
    /// including after the timer fires. Distinct registrations may be canceled
    /// concurrently; access to the same registration requires synchronization.
    class TimerRegistration final
    {
    public:
        /// @brief Private executor adapters use a borrowed state and stable key.
        using CancelFn                         = bool (*)(void*, std::uint64_t, std::uint64_t) noexcept;
        constexpr TimerRegistration() noexcept = default;
        constexpr TimerRegistration(void* state, std::uint64_t deadline, std::uint64_t identifier, CancelFn cancel) noexcept
            : m_state(state), m_deadline(deadline), m_identifier(identifier), m_cancel(cancel) {}
        TimerRegistration(TimerRegistration&& other) noexcept
            : m_state(other.m_state), m_deadline(other.m_deadline), m_identifier(other.m_identifier),
              m_cancel(std::exchange(other.m_cancel, nullptr)) {}
        TimerRegistration& operator=(TimerRegistration&& other) noexcept
        {
            if (this != &other)
            {
                Cancel();
                m_state      = other.m_state;
                m_deadline   = other.m_deadline;
                m_identifier = other.m_identifier;
                m_cancel     = std::exchange(other.m_cancel, nullptr);
            }
            return *this;
        }
        TimerRegistration(const TimerRegistration&)            = delete;
        TimerRegistration& operator=(const TimerRegistration&) = delete;
        ~TimerRegistration() { Cancel(); }

        /// @brief Removes and destroys queued work; false if already claimed or canceled.
        bool Cancel() noexcept
        {
            const CancelFn cancel = std::exchange(m_cancel, nullptr);
            return cancel && cancel(m_state, m_deadline, m_identifier);
        }
        /// @brief Relinquishes cancellation ownership while leaving the timer queued.
        void Detach() noexcept { m_cancel = nullptr; }

    private:
        void*         m_state {};
        std::uint64_t m_deadline {};
        std::uint64_t m_identifier {};
        CancelFn      m_cancel {};
    };
}// namespace NGIN::Execution
