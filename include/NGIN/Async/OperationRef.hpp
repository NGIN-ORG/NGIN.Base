/// @file OperationRef.hpp
/// @brief Copyable, notification-only reference to a scoped operation.
#pragma once

#include <NGIN/Async/detail/CompletionObservation.hpp>
#include <memory>

namespace NGIN::Async
{
    template<typename T, typename E>
    class Operation;
    namespace detail
    {
        struct FirstCompletedState;
    }

    /// Retains terminal notification storage without owning execution or consuming
    /// its result. The originating scope remains responsible for joining execution.
    class OperationRef final
    {
    public:
        OperationRef() noexcept = default;
        [[nodiscard]] bool IsValid() const noexcept { return bool(m_state); }
        /// Empty until publication, or for an invalid reference. Remains available
        /// after another consumer takes the value or the scope is destroyed.
        [[nodiscard]] std::optional<CompletionKind> Kind() const noexcept
        {
            return m_state ? m_state->Kind() : std::nullopt;
        }

    private:
        explicit OperationRef(std::shared_ptr<detail::CompletionObservation> state) noexcept
            : m_state(std::move(state)) {}
        std::shared_ptr<detail::CompletionObservation> m_state;
        template<typename T, typename E>
        friend class Operation;
        friend struct detail::FirstCompletedState;
    };
}// namespace NGIN::Async
