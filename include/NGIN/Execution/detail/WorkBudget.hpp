#pragma once

#include <NGIN/Defines.hpp>
#include <cstddef>

namespace NGIN::Execution::detail
{
    /// Internal, thread-bound accounting shared by nested dispatch and coroutine transfer.
    /// This counts cooperative work; it cannot preempt arbitrary callback instructions.
    class NGIN_EXECUTION_API WorkBudget final
    {
    public:
        explicit WorkBudget(std::size_t allowance) noexcept;
        ~WorkBudget();
        WorkBudget(const WorkBudget&)            = delete;
        WorkBudget& operator=(const WorkBudget&) = delete;

        /// Charges every enclosing budget, or leaves all unchanged when one is exhausted.
        [[nodiscard]] static bool TryConsume() noexcept;
        /// Returns a charge when a selected source had no work to invoke.
        static void RefundUnused() noexcept;

    private:
        std::size_t                     m_remaining;
        WorkBudget*                     m_parent;
        static thread_local WorkBudget* s_current;
    };
    /// Bounds reentrant callback dispatch and charges an enclosing work budget once.
    class NGIN_EXECUTION_API DispatchBudget final
    {
    public:
        DispatchBudget() noexcept;
        ~DispatchBudget();
        DispatchBudget(const DispatchBudget&)               = delete;
        DispatchBudget&    operator=(const DispatchBudget&) = delete;
        [[nodiscard]] bool Entered() const noexcept { return m_entered; }
        void               Invoked() noexcept { m_invoked = true; }

    private:
        bool                         m_entered {false};
        bool                         m_invoked {false};
        static thread_local unsigned s_depth;
    };
}// namespace NGIN::Execution::detail
