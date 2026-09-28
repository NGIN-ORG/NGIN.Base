/// @file Completion.hpp
/// @brief Value, error, and stopped outcomes for asynchronous tasks.
#pragma once
#include <NGIN/Async/AsyncError.hpp>
#include <NGIN/Async/StopReason.hpp>
#include <NGIN/Primitives.hpp>
#include <cassert>
#include <coroutine>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace NGIN::Async
{
    /// @brief The three terminal channels; pending state belongs to the running operation.
    enum class CompletionKind : NGIN::UInt8
    {
        Value,
        Error,
        Stopped
    };
    /// @brief Running state or one of the terminal channels.
    enum class TaskStatus : NGIN::UInt8
    {
        Pending,
        Value,
        Error,
        Stopped
    };
    constexpr TaskStatus ToTaskStatus(CompletionKind kind) noexcept
    {
        switch (kind)
        {
            case CompletionKind::Value:
                return TaskStatus::Value;
            case CompletionKind::Error:
                return TaskStatus::Error;
            case CompletionKind::Stopped:
                return TaskStatus::Stopped;
        }
        return TaskStatus::Error;
    }

    /// @brief A value (including void), typed asynchronous error, or stopped reason.
    /// @details Accessors require their matching channel. Failure forwards the entire error payload;
    /// DomainFailure and Faulted select payload alternatives within that same Error channel.
    template<typename T, typename E>
    class Completion
    {
        using Stored = std::conditional_t<std::is_void_v<T>, std::monostate, T>;

    public:
        using ValueType = T;
        using ErrorType = E;
        CompletionKind Kind() const noexcept { return m_kind; }
        TaskStatus     Status() const noexcept { return ToTaskStatus(m_kind); }
        bool           Succeeded() const noexcept { return m_kind == CompletionKind::Value; }
        bool           HasValue() const noexcept { return Succeeded(); }
        bool           HasError() const noexcept { return m_kind == CompletionKind::Error; }
        bool           IsStopped() const noexcept { return m_kind == CompletionKind::Stopped; }
        explicit       operator bool() const noexcept { return Succeeded(); }
        Stored&        Value() &
            requires(!std::is_void_v<T>)
        {
            assert(HasValue());
            return *m_value;
        }
        const Stored& Value() const&
            requires(!std::is_void_v<T>)
        {
            assert(HasValue());
            return *m_value;
        }
        Stored&& Value() && requires(!std::is_void_v<T>) { assert(HasValue()); return std::move(*m_value); } void Value() const
                    requires std::is_void_v<T>
        {
            assert(HasValue());
        }
        Stored& operator*() &
            requires(!std::is_void_v<T>)
        {
            return Value();
        }
        const Stored& operator*() const&
            requires(!std::is_void_v<T>)
        {
            return Value();
        }
        Stored* operator->()
            requires(!std::is_void_v<T>)
        {
            return &Value();
        }
        const Stored* operator->() const
            requires(!std::is_void_v<T>)
        {
            return &Value();
        }
        AsyncError<E>& Error() &
        {
            assert(HasError());
            return *m_error;
        }
        const AsyncError<E>& Error() const&
        {
            assert(HasError());
            return *m_error;
        }
        AsyncError<E>&& Error() &&
        {
            assert(HasError());
            return std::move(*m_error);
        }
        NGIN::Async::StopReason StopReason() const noexcept
        {
            assert(IsStopped());
            return m_stop;
        }
        static Completion Success(Stored value) noexcept(std::is_nothrow_move_constructible_v<Stored>)
            requires(!std::is_void_v<T>)
        {
            Completion result;
            result.m_value.emplace(std::move(value));
            result.m_kind = CompletionKind::Value;
            return result;
        }
        static Completion Success() noexcept
            requires std::is_void_v<T>
        {
            Completion result;
            result.m_kind = CompletionKind::Value;
            return result;
        }
        static Completion Failure(AsyncError<E> error)
        {
            Completion result;
            result.m_error.emplace(std::move(error));
            result.m_kind = CompletionKind::Error;
            return result;
        }
        static Completion DomainFailure(E error) { return Failure(AsyncError<E>::FromDomain(std::move(error))); }
        static Completion Faulted(AsyncFault fault) { return Failure(AsyncError<E>::FromFault(std::move(fault))); }
        static Completion Stopped(NGIN::Async::StopReason reason = NGIN::Async::StopReason::Requested) noexcept
        {
            Completion result;
            result.m_stop = reason;
            return result;
        }

    private:
        CompletionKind               m_kind {CompletionKind::Stopped};
        std::optional<Stored>        m_value;
        std::optional<AsyncError<E>> m_error;
        NGIN::Async::StopReason      m_stop {NGIN::Async::StopReason::Requested};
    };

    /// @brief Publishes an error payload without extracting its domain/fault alternative.
    template<typename E>
    struct FailureAwaiter final
    {
        AsyncError<E> error;
        bool          await_ready() const noexcept { return false; }
        template<typename Promise>
        bool await_suspend(std::coroutine_handle<Promise> handle)
        {
            handle.promise().SetError(std::move(error));
            return false;
        }
        void await_resume() const noexcept {}
    };

    template<typename E>
    [[nodiscard]] FailureAwaiter<E> Failure(AsyncError<E> error)
    {
        return {std::move(error)};
    }

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
    struct StoppedAwaiter final
    {
        NGIN::Async::StopReason reason {NGIN::Async::StopReason::Requested};
        /// @brief Always returns `false` so the promise receives the outcome.
        [[nodiscard]] bool await_ready() const noexcept
        {
            return false;
        }

        /// @brief Marks a compatible task promise as canceled.
        template<typename Promise>
        bool await_suspend(std::coroutine_handle<Promise> handle) const noexcept
        {
            handle.promise().SetStopped(reason);
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
    [[nodiscard]] constexpr StoppedAwaiter Stopped(NGIN::Async::StopReason reason = NGIN::Async::StopReason::Requested) noexcept
    {
        return {reason};
    }

    /// @brief Creates an awaiter that faults the current task.
    [[nodiscard]] inline FaultedAwaiter Faulted(AsyncFault fault) noexcept
    {
        return FaultedAwaiter {std::move(fault)};
    }

}// namespace NGIN::Async
