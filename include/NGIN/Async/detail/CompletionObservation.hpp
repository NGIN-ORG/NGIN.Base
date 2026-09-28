/// @file CompletionObservation.hpp
/// @brief Internal non-consuming terminal notification registry.
#pragma once

#include <NGIN/Async/Completion.hpp>
#include <NGIN/Defines.hpp>
#include <mutex>
#include <optional>

namespace NGIN::Async::detail
{
    struct CompletionObserver final
    {
        void (*notify)(CompletionObserver&, CompletionKind) noexcept {};
        void*               context {};
        std::size_t         index {};
        CompletionObserver* previous {};
        CompletionObserver* next {};
        bool                attached {};
    };

    /// Callbacks arbitrate and enqueue only. Unregister joins any active callback;
    /// neither callbacks nor publication may resume user code under this lock.
    class NGIN_EXECUTION_API CompletionObservation final
    {
    public:
        [[nodiscard]] std::optional<CompletionKind> Kind() const noexcept;
        void                                        Publish(CompletionKind kind) noexcept;
        void                                        Register(CompletionObserver& observer) noexcept;
        void                                        Unregister(CompletionObserver& observer) noexcept;

    private:
        mutable std::mutex            m_mutex;
        std::optional<CompletionKind> m_kind;
        CompletionObserver*           m_first {};
    };
}// namespace NGIN::Async::detail
