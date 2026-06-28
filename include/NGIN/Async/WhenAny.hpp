/// @file WhenAny.hpp
/// @brief Task combinator that completes when any owned child task completes.
#pragma once

#include <coroutine>
#include <memory>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <utility>

#include <NGIN/Async/Task.hpp>
#include <NGIN/Primitives.hpp>

namespace NGIN::Async
{
    namespace detail::when_any
    {
        struct SharedState final
        {
            explicit SharedState(NGIN::Execution::ExecutorRef executor) noexcept
                : exec(executor)
            {
            }

            NGIN::Execution::ExecutorRef exec {};
            std::mutex                   mutex {};
            bool                         completed {false};
            NGIN::UIntSize               index {0};
            std::coroutine_handle<>      continuation {};

            [[nodiscard]] bool SetContinuation(std::coroutine_handle<> handle)
            {
                std::lock_guard lock(mutex);
                if (completed)
                {
                    return false;
                }

                continuation = handle;
                return true;
            }

            void Complete(NGIN::UIntSize completedIndex)
            {
                std::coroutine_handle<> toResume {};
                {
                    std::lock_guard lock(mutex);
                    if (completed)
                    {
                        return;
                    }

                    completed = true;
                    index     = completedIndex;
                    toResume  = continuation;
                }

                if (toResume)
                {
                    if (exec.IsValid())
                    {
                        exec.Execute(toResume);
                    }
                    else
                    {
                        toResume.resume();
                    }
                }
            }
        };

        class AwaitFirst final
        {
        public:
            explicit AwaitFirst(std::shared_ptr<SharedState> state) noexcept
                : m_state(std::move(state))
            {
            }

            [[nodiscard]] bool await_ready() const
            {
                std::lock_guard lock(m_state->mutex);
                return m_state->completed;
            }

            [[nodiscard]] bool await_suspend(std::coroutine_handle<> awaiting)
            {
                return m_state->SetContinuation(awaiting);
            }

            [[nodiscard]] NGIN::UIntSize await_resume() const
            {
                std::lock_guard lock(m_state->mutex);
                return m_state->index;
            }

        private:
            std::shared_ptr<SharedState> m_state;
        };

        template<std::size_t Index, typename T, typename E>
        inline Task<void, E> Watch(std::shared_ptr<SharedState> state, Operation<T, E> operation)
        {
            static_cast<void>(co_await operation);
            state->Complete(static_cast<NGIN::UIntSize>(Index));
            co_return;
        }

        template<typename State, typename OperationsTuple, std::size_t... Indices>
        inline void DetachWatchers(TaskContext& ctx, State state, OperationsTuple& operations, std::index_sequence<Indices...>)
        {
            (Detach(ctx, Watch<Indices>(state, std::move(std::get<Indices>(operations)))), ...);
        }
    }// namespace detail::when_any

    template<typename... TTasks>
        requires(sizeof...(TTasks) > 0) && (detail::IsTaskTypeV<TTasks> && ...) &&
                (std::is_same_v<typename TTasks::ErrorType, typename std::tuple_element_t<0, std::tuple<TTasks...>>::ErrorType> &&
                 ...)
    [[nodiscard]] inline Task<NGIN::UIntSize, typename std::tuple_element_t<0, std::tuple<TTasks...>>::ErrorType> WhenAny(TaskContext& ctx, TTasks... tasks)
    {
        using E             = typename std::tuple_element_t<0, std::tuple<TTasks...>>::ErrorType;
        using OutCompletion = Completion<NGIN::UIntSize, E>;

        if (ctx.IsCancellationRequested())
        {
            co_return OutCompletion::Canceled();
        }

        auto state      = std::make_shared<detail::when_any::SharedState>(ctx.GetExecutor());
        auto operations = std::tuple {Spawn(ctx, std::move(tasks))...};
        detail::when_any::DetachWatchers(ctx, state, operations, std::make_index_sequence<sizeof...(TTasks)> {});

        co_return co_await detail::when_any::AwaitFirst {std::move(state)};
    }
}// namespace NGIN::Async
