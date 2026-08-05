/// @file Completion.hpp
/// @brief Explicit success, domain-error, cancellation, and fault outcomes for asynchronous tasks.
#pragma once

#include <NGIN/Async/AsyncFault.hpp>
#include <NGIN/Primitives.hpp>

#include <cassert>
#include <coroutine>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace NGIN::Async
{
    /// @brief Terminal outcome category of a completed asynchronous operation.
    enum class CompletionKind : NGIN::UInt8
    {
        Succeeded,
        DomainError,
        Canceled,
        Fault,
    };

    /// @brief Observable lifecycle status of a task.
    enum class TaskStatus : NGIN::UInt8
    {
        Pending,
        Succeeded,
        DomainError,
        Canceled,
        Fault,
    };

    /// @brief Converts a terminal completion kind to the corresponding task status.
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

    /// @brief Terminal asynchronous outcome containing a value, domain error, cancellation, or fault.
    template<typename T, typename E>
    class Completion
    {
    public:
        /// @brief Successful value type.
        using ValueType = T;
        /// @brief Recoverable domain-error type.
        using ErrorType = E;

        /// @brief Returns the terminal outcome category.
        [[nodiscard]] CompletionKind Kind() const noexcept
        {
            return m_kind;
        }

        /// @brief Returns the task status corresponding to this completion.
        [[nodiscard]] TaskStatus Status() const noexcept
        {
            return ToTaskStatus(m_kind);
        }

        /// @brief Returns whether the operation succeeded.
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return m_kind == CompletionKind::Succeeded;
        }

        /// @brief Returns whether the completion contains a success value.
        [[nodiscard]] bool HasValue() const noexcept
        {
            return Succeeded() && m_value.has_value();
        }

        /// @brief Returns whether the operation produced a recoverable domain error.
        [[nodiscard]] bool IsDomainError() const noexcept
        {
            return m_kind == CompletionKind::DomainError;
        }

        /// @brief Returns whether the operation was canceled.
        [[nodiscard]] bool IsCanceled() const noexcept
        {
            return m_kind == CompletionKind::Canceled;
        }

        /// @brief Returns whether the operation produced an unexpected fault.
        [[nodiscard]] bool IsFault() const noexcept
        {
            return m_kind == CompletionKind::Fault;
        }

        /// @brief Returns whether the operation succeeded.
        [[nodiscard]] explicit operator bool() const noexcept
        {
            return Succeeded();
        }

        /// @brief Returns the success value from a mutable lvalue.
        /// @pre `HasValue()` is `true`.
        [[nodiscard]] T& Value() &
        {
            assert(HasValue());
            return *m_value;
        }

        /// @brief Returns the success value from an immutable lvalue.
        /// @pre `HasValue()` is `true`.
        [[nodiscard]] const T& Value() const&
        {
            assert(HasValue());
            return *m_value;
        }

        /// @brief Moves the success value from an rvalue completion.
        /// @pre `HasValue()` is `true`.
        [[nodiscard]] T&& Value() &&
        {
            assert(HasValue());
            return std::move(*m_value);
        }

        /// @brief Returns the success value from a mutable lvalue.
        [[nodiscard]] T& operator*() &
        {
            return Value();
        }

        /// @brief Returns the success value from an immutable lvalue.
        [[nodiscard]] const T& operator*() const&
        {
            return Value();
        }

        /// @brief Returns a pointer to the success value.
        /// @pre `HasValue()` is `true`.
        [[nodiscard]] T* operator->()
        {
            assert(HasValue());
            return &*m_value;
        }

        /// @brief Returns an immutable pointer to the success value.
        /// @pre `HasValue()` is `true`.
        [[nodiscard]] const T* operator->() const
        {
            assert(HasValue());
            return &*m_value;
        }

        /// @brief Returns the domain error from a mutable lvalue.
        /// @pre `IsDomainError()` is `true`.
        [[nodiscard]] E& DomainError() &
        {
            assert(IsDomainError() && m_domainError.has_value());
            return *m_domainError;
        }

        /// @brief Returns the domain error from an immutable lvalue.
        /// @pre `IsDomainError()` is `true`.
        [[nodiscard]] const E& DomainError() const&
        {
            assert(IsDomainError() && m_domainError.has_value());
            return *m_domainError;
        }

        /// @brief Moves the domain error from an rvalue completion.
        /// @pre `IsDomainError()` is `true`.
        [[nodiscard]] E&& DomainError() &&
        {
            assert(IsDomainError() && m_domainError.has_value());
            return std::move(*m_domainError);
        }

        /// @brief Returns the unexpected fault from a mutable lvalue.
        /// @pre `IsFault()` is `true`.
        [[nodiscard]] AsyncFault& Fault() &
        {
            assert(IsFault() && m_fault.has_value());
            return *m_fault;
        }

        /// @brief Returns the unexpected fault from an immutable lvalue.
        /// @pre `IsFault()` is `true`.
        [[nodiscard]] const AsyncFault& Fault() const&
        {
            assert(IsFault() && m_fault.has_value());
            return *m_fault;
        }

        /// @brief Moves the unexpected fault from an rvalue completion.
        /// @pre `IsFault()` is `true`.
        [[nodiscard]] AsyncFault&& Fault() &&
        {
            assert(IsFault() && m_fault.has_value());
            return std::move(*m_fault);
        }

        /// @brief Creates a successful completion containing a value.
        [[nodiscard]] static Completion Success(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        {
            Completion completion;
            completion.m_kind = CompletionKind::Succeeded;
            completion.m_value.emplace(std::move(value));
            return completion;
        }

        /// @brief Creates a completion containing a recoverable domain error.
        [[nodiscard]] static Completion DomainFailure(E error) noexcept(std::is_nothrow_move_constructible_v<E>)
        {
            Completion completion;
            completion.m_kind = CompletionKind::DomainError;
            completion.m_domainError.emplace(std::move(error));
            return completion;
        }

        /// @brief Creates a canceled completion.
        [[nodiscard]] static Completion Canceled() noexcept
        {
            Completion completion;
            completion.m_kind = CompletionKind::Canceled;
            return completion;
        }

        /// @brief Creates a completion containing an unexpected fault.
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

    /// @brief Terminal asynchronous outcome without a success value.
    template<typename E>
    class Completion<void, E>
    {
    public:
        /// @brief Successful value type for this specialization.
        using ValueType = void;
        /// @brief Recoverable domain-error type.
        using ErrorType = E;

        /// @brief Returns the terminal outcome category.
        [[nodiscard]] CompletionKind Kind() const noexcept
        {
            return m_kind;
        }

        /// @brief Returns the task status corresponding to this completion.
        [[nodiscard]] TaskStatus Status() const noexcept
        {
            return ToTaskStatus(m_kind);
        }

        /// @brief Returns whether the operation succeeded.
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return m_kind == CompletionKind::Succeeded;
        }

        /// @brief Returns whether the operation produced a recoverable domain error.
        [[nodiscard]] bool IsDomainError() const noexcept
        {
            return m_kind == CompletionKind::DomainError;
        }

        /// @brief Returns whether the operation was canceled.
        [[nodiscard]] bool IsCanceled() const noexcept
        {
            return m_kind == CompletionKind::Canceled;
        }

        /// @brief Returns whether the operation produced an unexpected fault.
        [[nodiscard]] bool IsFault() const noexcept
        {
            return m_kind == CompletionKind::Fault;
        }

        /// @brief Returns whether the operation succeeded.
        [[nodiscard]] explicit operator bool() const noexcept
        {
            return Succeeded();
        }

        /// @brief Returns the domain error from a mutable lvalue.
        /// @pre `IsDomainError()` is `true`.
        [[nodiscard]] E& DomainError() &
        {
            assert(IsDomainError() && m_domainError.has_value());
            return *m_domainError;
        }

        /// @brief Returns the domain error from an immutable lvalue.
        /// @pre `IsDomainError()` is `true`.
        [[nodiscard]] const E& DomainError() const&
        {
            assert(IsDomainError() && m_domainError.has_value());
            return *m_domainError;
        }

        /// @brief Moves the domain error from an rvalue completion.
        /// @pre `IsDomainError()` is `true`.
        [[nodiscard]] E&& DomainError() &&
        {
            assert(IsDomainError() && m_domainError.has_value());
            return std::move(*m_domainError);
        }

        /// @brief Returns the unexpected fault from a mutable lvalue.
        /// @pre `IsFault()` is `true`.
        [[nodiscard]] AsyncFault& Fault() &
        {
            assert(IsFault() && m_fault.has_value());
            return *m_fault;
        }

        /// @brief Returns the unexpected fault from an immutable lvalue.
        /// @pre `IsFault()` is `true`.
        [[nodiscard]] const AsyncFault& Fault() const&
        {
            assert(IsFault() && m_fault.has_value());
            return *m_fault;
        }

        /// @brief Moves the unexpected fault from an rvalue completion.
        /// @pre `IsFault()` is `true`.
        [[nodiscard]] AsyncFault&& Fault() &&
        {
            assert(IsFault() && m_fault.has_value());
            return std::move(*m_fault);
        }

        /// @brief Creates a successful value-less completion.
        [[nodiscard]] static Completion Success() noexcept
        {
            Completion completion;
            completion.m_kind = CompletionKind::Succeeded;
            return completion;
        }

        /// @brief Creates a completion containing a recoverable domain error.
        [[nodiscard]] static Completion DomainFailure(E error) noexcept(std::is_nothrow_move_constructible_v<E>)
        {
            Completion completion;
            completion.m_kind = CompletionKind::DomainError;
            completion.m_domainError.emplace(std::move(error));
            return completion;
        }

        /// @brief Creates a canceled completion.
        [[nodiscard]] static Completion Canceled() noexcept
        {
            Completion completion;
            completion.m_kind = CompletionKind::Canceled;
            return completion;
        }

        /// @brief Creates a completion containing an unexpected fault.
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

    /// @brief Awaiter that completes the awaiting task with a domain error without suspending.
    template<typename E>
    struct DomainFailureAwaiter final
    {
        E error {};

        /// @brief Always returns `false` so the promise receives the outcome.
        [[nodiscard]] bool await_ready() const noexcept
        {
            return false;
        }

        /// @brief Stores the domain error in a compatible task promise.
        template<typename Promise>
        bool await_suspend(std::coroutine_handle<Promise> handle) noexcept
        {
            handle.promise().SetDomainError(std::move(error));
            return false;
        }

        /// @brief Performs no resume-time work.
        void await_resume() const noexcept {}
    };

    /// @brief Awaiter that completes the awaiting task as canceled without suspending.
    struct CanceledAwaiter final
    {
        /// @brief Always returns `false` so the promise receives the outcome.
        [[nodiscard]] bool await_ready() const noexcept
        {
            return false;
        }

        /// @brief Marks a compatible task promise as canceled.
        template<typename Promise>
        bool await_suspend(std::coroutine_handle<Promise> handle) const noexcept
        {
            handle.promise().SetCanceled();
            return false;
        }

        /// @brief Performs no resume-time work.
        void await_resume() const noexcept {}
    };

    /// @brief Awaiter that completes the awaiting task with an unexpected fault without suspending.
    struct FaultedAwaiter final
    {
        AsyncFault fault {};

        /// @brief Always returns `false` so the promise receives the outcome.
        [[nodiscard]] bool await_ready() const noexcept
        {
            return false;
        }

        /// @brief Stores the fault in a compatible task promise.
        template<typename Promise>
        bool await_suspend(std::coroutine_handle<Promise> handle) noexcept
        {
            handle.promise().SetFault(std::move(fault));
            return false;
        }

        /// @brief Performs no resume-time work.
        void await_resume() const noexcept {}
    };

    /// @brief Creates an awaiter that returns a domain error from the current task.
    template<typename E>
    [[nodiscard]] DomainFailureAwaiter<E> DomainFailure(E error) noexcept
    {
        return DomainFailureAwaiter<E> {std::move(error)};
    }

    /// @brief Creates an awaiter that cancels the current task.
    [[nodiscard]] constexpr CanceledAwaiter Canceled() noexcept
    {
        return {};
    }

    /// @brief Creates an awaiter that faults the current task.
    [[nodiscard]] inline FaultedAwaiter Faulted(AsyncFault fault) noexcept
    {
        return FaultedAwaiter {std::move(fault)};
    }

}// namespace NGIN::Async
