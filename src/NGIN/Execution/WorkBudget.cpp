#include <NGIN/Execution/detail/WorkBudget.hpp>
#include <cassert>

namespace NGIN::Execution::detail
{
    thread_local WorkBudget* WorkBudget::s_current {};

    WorkBudget::WorkBudget(std::size_t allowance) noexcept : m_remaining(allowance), m_parent(s_current)
    {
        s_current = this;
    }

    WorkBudget::~WorkBudget()
    {
        assert(s_current == this);
        s_current = m_parent;
    }

    bool WorkBudget::TryConsume() noexcept
    {
        for (auto* budget = s_current; budget; budget = budget->m_parent)
            if (budget->m_remaining == 0)
                return false;
        for (auto* budget = s_current; budget; budget = budget->m_parent)
            --budget->m_remaining;
        return true;
    }

    void WorkBudget::RefundUnused() noexcept
    {
        for (auto* budget = s_current; budget; budget = budget->m_parent)
            ++budget->m_remaining;
    }
    thread_local unsigned DispatchBudget::s_depth {};

    DispatchBudget::DispatchBudget() noexcept
    {
        if (s_depth < 16 && WorkBudget::TryConsume())
        {
            ++s_depth;
            m_entered = true;
        }
    }

    DispatchBudget::~DispatchBudget()
    {
        if (m_entered)
        {
            --s_depth;
            if (!m_invoked)
                WorkBudget::RefundUnused();
        }
    }
}// namespace NGIN::Execution::detail
