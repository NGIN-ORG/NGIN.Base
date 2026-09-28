#include <NGIN/Async/detail/CompletionObservation.hpp>
#include <cassert>

namespace NGIN::Async::detail
{
    std::optional<CompletionKind> CompletionObservation::Kind() const noexcept
    {
        std::lock_guard lock(m_mutex);
        return m_kind;
    }

    void CompletionObservation::Publish(CompletionKind kind) noexcept
    {
        std::lock_guard lock(m_mutex);
        assert(!m_kind);
        m_kind = kind;
        for (auto* observer = m_first; observer; observer = observer->next)
            observer->notify(*observer, kind);
    }

    void CompletionObservation::Register(CompletionObserver& observer) noexcept
    {
        std::lock_guard lock(m_mutex);
        assert(!observer.attached);
        if (m_kind)
        {
            observer.notify(observer, *m_kind);
            return;
        }
        observer.next     = m_first;
        observer.previous = nullptr;
        if (m_first)
            m_first->previous = &observer;
        m_first           = &observer;
        observer.attached = true;
    }

    void CompletionObservation::Unregister(CompletionObserver& observer) noexcept
    {
        std::lock_guard lock(m_mutex);
        if (!observer.attached)
            return;
        if (observer.previous)
            observer.previous->next = observer.next;
        else
            m_first = observer.next;
        if (observer.next)
            observer.next->previous = observer.previous;
        observer.attached = false;
        observer.previous = observer.next = nullptr;
    }
}// namespace NGIN::Async::detail
