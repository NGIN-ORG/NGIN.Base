#pragma once

#include <NGIN/Async/AsyncError.hpp>
#include <NGIN/Primitives.hpp>

#include <cassert>
#include <coroutine>
#include <optional>
#include <type_traits>
#include <utility>

namespace NGIN::Async
{
    enum class CompletionKind : NGIN::UInt8
    {
        Succeeded,
        DomainError,
        Canceled,
        Fault,
    };

    enum class TaskStatus : NGIN::UInt8
    {
        Pending,
        Succeeded,
        DomainError,
        Canceled,
        Fault,
    };

    [[nodiscard]] constexpr TaskStatus ToTaskStatus(const CompletionKind kind) noexcept
    {
        switch (kind)
        {
            case CompletionKind::Succeeded:
                return TaskStatus::Succeeded;
            case CompletionKind::DomainError:
                return TaskStatus::DomainError;
            case CompletionKind::Canceled:
                return TaskStatus::Canceled;
            case CompletionKind::Fault:
                return TaskStatus::Fault;
        }

        return TaskStatus::Fault;
    }

    template<typename T, typename E>
    class Completion
    {
    public:
        using ValueType = T;
        using ErrorType = E;

        [[nodiscard]] CompletionKind Kind() const noexcept
        {
            return m_kind;
        }

        [[nodiscard]] TaskStatus Status() const noexcept
        {
            return ToTaskStatus(m_kind);
        }

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return m_kind == CompletionKind::Succeeded;
        }

        [[nodiscard]] bool HasValue() const noexcept
        {
            return Succeeded() && m_value.has_value();
        }

        [[nodiscard]] bool IsDomainError() const noexcept
        {
            return m_kind == CompletionKind::DomainError;
        }

        [[nodiscard]] bool IsCanceled() const noexcept
        {
            return m_kind == CompletionKind::Canceled;
        }

        [[nodiscard]] bool IsFault() const noexcept
        {
            return m_kind == CompletionKind::Fault;
        }

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return Succeeded();
        }

        [[nodiscard]] T& Value() &
        {
            assert(HasValue());
            return *m_value;
        }

        [[nodiscard]] const T& Value() const&
        {
            assert(HasValue());
            return *m_value;
        }

        [[nodiscard]] T&& Value() &&
        {
            assert(HasValue());
            return std::move(*m_value);
        }

        [[nodiscard]] T& operator*() &
        {
            return Value();
        }

        [[nodiscard]] const T& operator*() const&
        {
            return Value();
        }

        [[nodiscard]] T* operator->()
        {
            assert(HasValue());
            return &*m_value;
        }

        [[nodiscard]] const T* operator->() const
        {
            assert(HasValue());
            return &*m_value;
        }

        [[nodiscard]] E& DomainError() &
        {
            assert(IsDomainError() && m_domainError.has_value());
            return *m_domainError;
        }

        [[nodiscard]] const E& DomainError() const&
        {
            assert(IsDomainError() && m_domainError.has_value());
            return *m_domainError;
        }

        [[nodiscard]] E&& DomainError() &&
        {
            assert(IsDomainError() && m_domainError.has_value());
            return std::move(*m_domainError);
        }

        [[nodiscard]] AsyncFault& Fault() &
        {
            assert(IsFault() && m_fault.has_value());
            return *m_fault;
        }

        [[nodiscard]] const AsyncFault& Fault() const&
        {
            assert(IsFault() && m_fault.has_value());
            return *m_fault;
        }

        [[nodiscard]] AsyncFault&& Fault() &&
        {
            assert(IsFault() && m_fault.has_value());
            return std::move(*m_fault);
        }

        [[nodiscard]] static Completion Success(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        {
            Completion completion;
            completion.m_kind = CompletionKind::Succeeded;
            completion.m_value.emplace(std::move(value));
            return completion;
        }

        [[nodiscard]] static Completion DomainFailure(E error) noexcept(std::is_nothrow_move_constructible_v<E>)
        {
            Completion completion;
            completion.m_kind = CompletionKind::DomainError;
            completion.m_domainError.emplace(std::move(error));
            return completion;
        }

        [[nodiscard]] static Completion Canceled() noexcept
        {
            Completion completion;
            completion.m_kind = CompletionKind::Canceled;
            return completion;
        }

        [[nodiscard]] static Completion Faulted(AsyncFault fault) noexcept
        {
            Completion completion;
            completion.m_kind = CompletionKind::Fault;
            completion.m_fault.emplace(std::move(fault));
            return completion;
        }

    private:
        CompletionKind            m_kind {CompletionKind::Canceled};
        std::optional<T>          m_value {};
        std::optional<E>          m_domainError {};
        std::optional<AsyncFault> m_fault {};
    };

    template<typename E>
    class Completion<void, E>
    {
    public:
        using ValueType = void;
        using ErrorType = E;

        [[nodiscard]] CompletionKind Kind() const noexcept
        {
            return m_kind;
        }

        [[nodiscard]] TaskStatus Status() const noexcept
        {
            return ToTaskStatus(m_kind);
        }

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return m_kind == CompletionKind::Succeeded;
        }

        [[nodiscard]] bool IsDomainError() const noexcept
        {
            return m_kind == CompletionKind::DomainError;
        }

        [[nodiscard]] bool IsCanceled() const noexcept
        {
            return m_kind == CompletionKind::Canceled;
        }

        [[nodiscard]] bool IsFault() const noexcept
        {
            return m_kind == CompletionKind::Fault;
        }

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return Succeeded();
        }

        [[nodiscard]] E& DomainError() &
        {
            assert(IsDomainError() && m_domainError.has_value());
            return *m_domainError;
        }

        [[nodiscard]] const E& DomainError() const&
        {
            assert(IsDomainError() && m_domainError.has_value());
            return *m_domainError;
        }

        [[nodiscard]] E&& DomainError() &&
        {
            assert(IsDomainError() && m_domainError.has_value());
            return std::move(*m_domainError);
        }

        [[nodiscard]] AsyncFault& Fault() &
        {
            assert(IsFault() && m_fault.has_value());
            return *m_fault;
        }

        [[nodiscard]] const AsyncFault& Fault() const&
        {
            assert(IsFault() && m_fault.has_value());
            return *m_fault;
        }

        [[nodiscard]] AsyncFault&& Fault() &&
        {
            assert(IsFault() && m_fault.has_value());
            return std::move(*m_fault);
        }

        [[nodiscard]] static Completion Success() noexcept
        {
            Completion completion;
            completion.m_kind = CompletionKind::Succeeded;
            return completion;
        }

        [[nodiscard]] static Completion DomainFailure(E error) noexcept(std::is_nothrow_move_constructible_v<E>)
        {
            Completion completion;
            completion.m_kind = CompletionKind::DomainError;
            completion.m_domainError.emplace(std::move(error));
            return completion;
        }

        [[nodiscard]] static Completion Canceled() noexcept
        {
            Completion completion;
            completion.m_kind = CompletionKind::Canceled;
            return completion;
        }

        [[nodiscard]] static Completion Faulted(AsyncFault fault) noexcept
        {
            Completion completion;
            completion.m_kind = CompletionKind::Fault;
            completion.m_fault.emplace(std::move(fault));
            return completion;
        }

    private:
        CompletionKind            m_kind {CompletionKind::Canceled};
        std::optional<E>          m_domainError {};
        std::optional<AsyncFault> m_fault {};
    };

    template<typename E>
    struct DomainFailureAwaiter final
    {
        E error {};

        [[nodiscard]] bool await_ready() const noexcept
        {
            return false;
        }

        template<typename Promise>
        bool await_suspend(std::coroutine_handle<Promise> handle) noexcept
        {
            handle.promise().SetDomainError(std::move(error));
            return false;
        }

        void await_resume() const noexcept {}
    };

    struct CanceledAwaiter final
    {
        [[nodiscard]] bool await_ready() const noexcept
        {
            return false;
        }

        template<typename Promise>
        bool await_suspend(std::coroutine_handle<Promise> handle) const noexcept
        {
            handle.promise().SetCanceled();
            return false;
        }

        void await_resume() const noexcept {}
    };

    struct FaultedAwaiter final
    {
        AsyncFault fault {};

        [[nodiscard]] bool await_ready() const noexcept
        {
            return false;
        }

        template<typename Promise>
        bool await_suspend(std::coroutine_handle<Promise> handle) noexcept
        {
            handle.promise().SetFault(std::move(fault));
            return false;
        }

        void await_resume() const noexcept {}
    };

    template<typename E>
    [[nodiscard]] DomainFailureAwaiter<E> DomainFailure(E error) noexcept
    {
        return DomainFailureAwaiter<E> {std::move(error)};
    }

    [[nodiscard]] constexpr CanceledAwaiter Canceled() noexcept
    {
        return {};
    }

    [[nodiscard]] inline FaultedAwaiter Faulted(AsyncFault fault) noexcept
    {
        return FaultedAwaiter {std::move(fault)};
    }

    template<typename T, typename E>
    class TaskResult
    {
    public:
        constexpr TaskResult() noexcept = default;

        constexpr explicit TaskResult(const Completion<T, E>* completion) noexcept
            : m_completion(completion)
        {
        }

        [[nodiscard]] CompletionKind Kind() const noexcept
        {
            return m_completion ? m_completion->Kind() : CompletionKind::Fault;
        }

        [[nodiscard]] TaskStatus Status() const noexcept
        {
            return m_completion ? m_completion->Status() : TaskStatus::Pending;
        }

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return m_completion && m_completion->Succeeded();
        }

        [[nodiscard]] bool HasValue() const noexcept
        {
            return m_completion && m_completion->HasValue();
        }

        [[nodiscard]] bool IsDomainError() const noexcept
        {
            return m_completion && m_completion->IsDomainError();
        }

        [[nodiscard]] bool IsCanceled() const noexcept
        {
            return m_completion && m_completion->IsCanceled();
        }

        [[nodiscard]] bool IsFault() const noexcept
        {
            return m_completion && m_completion->IsFault();
        }

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return Succeeded();
        }

        [[nodiscard]] T& Value() const
        {
            assert(m_completion != nullptr);
            return const_cast<T&>(m_completion->Value());
        }

        [[nodiscard]] T& operator*() const
        {
            return Value();
        }

        [[nodiscard]] T* operator->() const
        {
            assert(m_completion != nullptr);
            return const_cast<T*>(&m_completion->Value());
        }

        [[nodiscard]] E& DomainError() const
        {
            assert(m_completion != nullptr);
            return const_cast<E&>(m_completion->DomainError());
        }

        [[nodiscard]] AsyncFault& Fault() const
        {
            assert(m_completion != nullptr);
            return const_cast<AsyncFault&>(m_completion->Fault());
        }

        [[nodiscard]] const Completion<T, E>& CompletionRef() const
        {
            assert(m_completion != nullptr);
            return *m_completion;
        }

    private:
        const Completion<T, E>* m_completion {nullptr};
    };

    template<typename E>
    class TaskResult<void, E>
    {
    public:
        constexpr TaskResult() noexcept = default;

        constexpr explicit TaskResult(const Completion<void, E>* completion) noexcept
            : m_completion(completion)
        {
        }

        [[nodiscard]] CompletionKind Kind() const noexcept
        {
            return m_completion ? m_completion->Kind() : CompletionKind::Fault;
        }

        [[nodiscard]] TaskStatus Status() const noexcept
        {
            return m_completion ? m_completion->Status() : TaskStatus::Pending;
        }

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return m_completion && m_completion->Succeeded();
        }

        [[nodiscard]] bool IsDomainError() const noexcept
        {
            return m_completion && m_completion->IsDomainError();
        }

        [[nodiscard]] bool IsCanceled() const noexcept
        {
            return m_completion && m_completion->IsCanceled();
        }

        [[nodiscard]] bool IsFault() const noexcept
        {
            return m_completion && m_completion->IsFault();
        }

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return Succeeded();
        }

        [[nodiscard]] E& DomainError() const
        {
            assert(m_completion != nullptr);
            return const_cast<E&>(m_completion->DomainError());
        }

        [[nodiscard]] AsyncFault& Fault() const
        {
            assert(m_completion != nullptr);
            return const_cast<AsyncFault&>(m_completion->Fault());
        }

        [[nodiscard]] const Completion<void, E>& CompletionRef() const
        {
            assert(m_completion != nullptr);
            return *m_completion;
        }

    private:
        const Completion<void, E>* m_completion {nullptr};
    };

    template<typename T, typename E>
    using TaskOutcome = TaskResult<T, E>;
}// namespace NGIN::Async
