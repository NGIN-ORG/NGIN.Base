/// @file ScopeReport.hpp
/// @brief Stable error and stopped diagnostics retained after a scope joins.
#pragma once
#include <NGIN/Async/Completion.hpp>
#include <memory>
#include <memory_resource>
#include <span>
#include <vector>

namespace NGIN::Async
{
    namespace detail
    {
        template<typename E>
        struct ScopeReportStorage final
        {
            explicit ScopeReportStorage(std::pmr::memory_resource* resource) : errors(resource) {}
            std::pmr::vector<AsyncError<E>>        errors;
            std::optional<NGIN::Async::StopReason> stopped;
        };
    }// namespace detail

    template<typename E>
    class TaskScope;

    /// @brief Owns spawn-ordered diagnostics without copying move-only error payloads.
    template<typename E>
    class ScopeReport final
    {
    public:
        bool                           HasError() const noexcept { return m_storage && !m_storage->errors.empty(); }
        bool                           IsStopped() const noexcept { return !HasError() && m_storage && m_storage->stopped.has_value(); }
        bool                           Succeeded() const noexcept { return !HasError() && !IsStopped(); }
        CompletionKind                 Kind() const noexcept { return HasError() ? CompletionKind::Error : IsStopped() ? CompletionKind::Stopped
                                                                                                                       : CompletionKind::Value; }
        std::span<const AsyncError<E>> Errors() const noexcept { return m_storage ? std::span<const AsyncError<E>>(m_storage->errors) : std::span<const AsyncError<E>> {}; }
        const AsyncError<E>&           Error() const
        {
            assert(HasError());
            return m_storage->errors.front();
        }
        NGIN::Async::StopReason StopReason() const noexcept
        {
            assert(IsStopped());
            return *m_storage->stopped;
        }
        std::optional<NGIN::Async::StopReason> StoppedReason() const noexcept { return m_storage ? m_storage->stopped : std::nullopt; }
        /// Creates an owning immutable alias for forwarding one error without consuming the report.
        AsyncError<E> RetainError(std::size_t index = 0) const
        {
            if (!m_storage || index >= m_storage->errors.size())
                throw std::out_of_range("Scope error index out of range");
            return AsyncError<E>::Retain(std::shared_ptr<const AsyncError<E>>(m_storage, &m_storage->errors[index]));
        }

    private:
        friend class TaskScope<E>;
        explicit ScopeReport(std::shared_ptr<const detail::ScopeReportStorage<E>> storage) : m_storage(std::move(storage)) {}
        std::shared_ptr<const detail::ScopeReportStorage<E>> m_storage;
    };

    /// @brief Body-primary or lowest-spawn-index child error, with all secondary child diagnostics.
    template<typename E>
    struct ScopeFailure final
    {
        AsyncError<E>                  primary;
        ScopeReport<E>                 children;
        bool                           bodyPrimary;
        const AsyncError<E>&           Primary() const noexcept { return primary; }
        std::span<const AsyncError<E>> SecondaryErrors() const noexcept
        {
            auto errors = children.Errors();
            return bodyPrimary || errors.empty() ? errors : errors.subspan(1);
        }
    };
}// namespace NGIN::Async
