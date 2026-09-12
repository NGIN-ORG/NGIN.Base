/// @file TaskScope.hpp
/// @brief Bounded ownership, cancellation, and explicit joining of child tasks.
#pragma once

#include <NGIN/Async/Task.hpp>

#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace NGIN::Async
{
    /// @brief Owns child operations and their contexts until an explicit join.
    /// @details A child failure requests sibling cancellation. Join closes admission,
    /// waits for every child, and exposes the first observed failure. Destruction
    /// with unfinished children terminates; it never blocks or silently detaches.
    /// Factories are retained until joining, including coroutine lambda captures.
    /// Spawn, RequestCancel, and status inspection are thread-safe. There may be
    /// only one pending Join; the scope and its executors must outlive that join.
    template<typename E = NoError>
    class TaskScope final
    {
        struct ChildBase
        {
            virtual ~ChildBase()                                     = default;
            virtual Completion<void, E>            Consume()         = 0;
            virtual void                           Retire() noexcept = 0;
            bool                                   observed {};
            NGIN::Execution::CompletionReservation notification;
        };

        template<typename Factory>
        struct Child final : ChildBase
        {
            using ChildTask = std::remove_cvref_t<std::invoke_result_t<Factory&, TaskContext&>>;
            using Value     = typename ChildTask::ValueType;

            Child(Factory callback, NGIN::Execution::ExecutorRef executor, CancellationToken cancellation)
                : factory(std::move(callback)), context(executor, std::move(cancellation)) {}

            Completion<void, E> Consume() override
            {
                if (creationFailure)
                    return Completion<void, E>::Faulted(std::move(*creationFailure));
                auto result = operation.TakeResult();
                if (result.Succeeded())
                    return Completion<void, E>::Success();
                if (result.IsDomainError())
                    return Completion<void, E>::DomainFailure(std::move(result).DomainError());
                if (result.IsCanceled())
                    return Completion<void, E>::Canceled();
                return Completion<void, E>::Faulted(std::move(result).Fault());
            }

            void Retire() noexcept override
            {
                detail::OperationAccess::Retire(operation, this->notification);
            }

            Factory                   factory;
            TaskContext               context;
            Operation<Value, E>       operation;
            std::optional<AsyncFault> creationFailure;
        };

        struct State final
        {
            State(NGIN::Execution::ExecutorRef executor, CancellationToken parent, std::size_t capacity)
                : executor(executor), cancellation({parent}), capacity(capacity)
            {
                if (!executor.SupportsCompletionReservations() || capacity == 0)
                    throw std::invalid_argument("TaskScope requires a tracked executor and positive child capacity");
                children.reserve(capacity);
            }

            void Complete(ChildBase& child) noexcept
            {
                if (!child.observed)
                {
                    child.observed             = true;
                    Completion<void, E> result = Completion<void, E>::Success();
                    try
                    {
                        result = child.Consume();
                    } catch (...)
                    {
                        AsyncFault fault = MakeAsyncFault(AsyncFaultCode::UnhandledException);
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                        fault.capturedException = std::current_exception();
#endif
                        result = Completion<void, E>::Faulted(std::move(fault));
                    }
                    bool cancel = false;
                    {
                        std::lock_guard lock(mutex);
                        if (!result.Succeeded() && outcome.Succeeded())
                        {
                            try
                            {
                                outcome = std::move(result);
                            } catch (...)
                            {
                                outcome = Completion<void, E>::Faulted(MakeAsyncFault(AsyncFaultCode::UnhandledException));
                            }
                            cancel = true;
                        }
                    }
                    if (cancel)
                        cancellation.Cancel();
                    child.Retire();
                    return;
                }
                // A second delivery on the same reserved slot follows final
                // frame destruction, not merely terminal result publication.
                child.notification.Reset();
                NGIN::Execution::WorkItem     resume;
                detail::PromiseRuntimeCommon* target = nullptr;
                {
                    std::lock_guard lock(mutex);
                    assert(pending != 0);
                    --pending;
                    if (pending == 0 && joinTarget)
                    {
                        target = std::exchange(joinTarget, nullptr);
                        resume = std::move(joinResume);
                    }
                }
                if (target)
                    target->QueueContinuation(std::move(resume));
            }

            NGIN::Execution::ExecutorRef            executor;
            LinkedCancellationSource                cancellation;
            const std::size_t                       capacity;
            mutable std::mutex                      mutex;
            std::vector<std::unique_ptr<ChildBase>> children;
            std::size_t                             pending {};
            Completion<void, E>                     outcome {Completion<void, E>::Success()};
            bool                                    closing {};
            bool                                    joining {};
            bool                                    joined {};
            bool                                    consumed {};
            detail::PromiseRuntimeCommon*           joinTarget {};
            NGIN::Execution::WorkItem               joinResume;
        };

    public:
        /// @brief Creates an empty scope whose terminal notifications run on executor.
        /// @param parent Requests cancellation of every child when canceled.
        /// @param capacity Maximum children retained before Join (default 256).
        explicit TaskScope(NGIN::Execution::ExecutorRef executor, CancellationToken parent = {}, std::size_t capacity = 256)
            : m_state(std::make_shared<State>(executor, std::move(parent), capacity)) {}

        TaskScope(const TaskScope&)            = delete;
        TaskScope& operator=(const TaskScope&) = delete;
        TaskScope(TaskScope&&)                 = delete;
        TaskScope& operator=(TaskScope&&)      = delete;

        ~TaskScope()
        {
            std::lock_guard lock(m_state->mutex);
            if (m_state->pending != 0 || m_state->joining)
                std::terminate();
        }

        /// @brief Starts a factory-created child on the scope executor.
        template<typename Factory>
        [[nodiscard]] NGIN::Execution::ScheduleResult Spawn(Factory&& factory)
        {
            return SpawnOn(m_state->executor, std::forward<Factory>(factory));
        }

        /// @brief Starts a child on an explicitly chosen, independently driven executor.
        /// @details Admission is nonblocking and reserves terminal notification before
        /// invoking the factory. Rejection starts no child. An admitted child whose
        /// factory or executor fails contributes a fault to Join. Values are discarded;
        /// child domain errors must use the scope's E. Each factory borrows only the
        /// stable TaskContext passed to it, and retains its captures until Join.
        template<typename Factory>
        [[nodiscard]] NGIN::Execution::ScheduleResult SpawnOn(NGIN::Execution::ExecutorRef executor, Factory&& factory)
        {
            using OwnedFactory = std::decay_t<Factory>;
            using OwnedChild   = Child<OwnedFactory>;
            static_assert(std::is_same_v<typename OwnedChild::ChildTask::ErrorType, E>,
                          "TaskScope children must use the scope's domain error type");
            std::shared_ptr<State>      state = m_state;
            std::unique_ptr<OwnedChild> child;
            OwnedChild*                 admitted;
            {
                std::lock_guard lock(state->mutex);
                if (state->closing || state->cancellation.IsCancellationRequested())
                    return std::unexpected(NGIN::Execution::ScheduleError::Stopped);
                if (state->children.size() == state->capacity)
                    return std::unexpected(NGIN::Execution::ScheduleError::ResourceExhausted);
                if (!executor.SupportsCompletionReservations())
                    return std::unexpected(NGIN::Execution::ScheduleError::Rejected);
                try
                {
                    child         = std::make_unique<OwnedChild>(std::forward<Factory>(factory), executor, state->cancellation.GetToken());
                    admitted      = child.get();
                    auto reserved = state->executor.ReserveCompletion(NGIN::Execution::WorkItem([state, admitted] {
                        state->Complete(*admitted);
                    }));
                    if (!reserved)
                        return std::unexpected(reserved.error());
                    admitted->notification = std::move(*reserved);
                } catch (const std::bad_alloc&)
                {
                    return std::unexpected(NGIN::Execution::ScheduleError::ResourceExhausted);
                }
                state->children.push_back(std::move(child));
                ++state->pending;
            }
            try
            {
                admitted->operation = NGIN::Async::Spawn(admitted->context, std::invoke(admitted->factory, admitted->context));
            } catch (...)
            {
                AsyncFault fault = MakeAsyncFault(AsyncFaultCode::UnhandledException);
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                fault.capturedException = std::current_exception();
#endif
                admitted->creationFailure = std::move(fault);
                admitted->notification.Schedule();
                return {};
            }
            if (!detail::OperationAccess::ObserveReusable(admitted->operation, admitted->notification))
                std::terminate();
            return {};
        }

        /// @brief Requests child cancellation without waiting for backend or task completion.
        void                            RequestCancel() noexcept { m_state->cancellation.Cancel(); }
        [[nodiscard]] CancellationToken GetCancellationToken() const noexcept { return m_state->cancellation.GetToken(); }
        [[nodiscard]] std::size_t       Pending() const noexcept
        {
            std::lock_guard lock(m_state->mutex);
            return m_state->pending;
        }

        class JoinAwaiter final
        {
        public:
            explicit JoinAwaiter(std::shared_ptr<State> state) : m_state(std::move(state)) {}
            [[nodiscard]] bool await_ready()
            {
                std::lock_guard lock(m_state->mutex);
                if (m_state->joining)
                    throw std::logic_error("TaskScope permits only one pending Join");
                m_state->closing = true;
                m_state->joining = true;
                return m_state->pending == 0;
            }

            template<typename Promise>
                requires std::derived_from<Promise, detail::PromiseRuntimeCommon>
            bool await_suspend(std::coroutine_handle<Promise> awaiting)
            {
                std::lock_guard lock(m_state->mutex);
                if (m_state->pending == 0)
                    return false;
                if (!awaiting.promise().m_taskContinuation.IsValid())
                {
                    m_state->joining = false;
                    throw std::logic_error("TaskScope::Join requires a task on a tracked executor");
                }
                awaiting.promise().RetainFrameReference();
                m_state->joinTarget = &awaiting.promise();
                m_state->joinResume = NGIN::Execution::WorkItem([awaiting] {
                    detail::PromiseRuntimeCommon::ResumeRetained(awaiting);
                });
                return true;
            }

            /// @brief Returns a borrowed outcome valid until TakeResult or scope destruction.
            /// @details Completed child frames and factory captures are released before return.
            const Completion<void, E>& await_resume()
            {
                std::vector<std::unique_ptr<ChildBase>> children;
                {
                    std::lock_guard lock(m_state->mutex);
                    assert(m_state->pending == 0);
                    m_state->joining = false;
                    m_state->joined  = true;
                    children.swap(m_state->children);
                }
                return m_state->outcome;
            }

        private:
            std::shared_ptr<State> m_state;
        };

        /// @brief Closes child admission and asynchronously joins every admitted child.
        /// @details Cancellation does not abandon the join. It uses the awaiting task's
        /// existing reservation and remains available during executor shutdown. Later
        /// sequential joins observe the same borrowed outcome without admitting work.
        [[nodiscard]] JoinAwaiter Join() { return JoinAwaiter(m_state); }

        /// @brief Moves the joined outcome to the caller once.
        /// @throws std::logic_error if no join completed or the result was already consumed.
        [[nodiscard]] Completion<void, E> TakeResult()
        {
            std::lock_guard lock(m_state->mutex);
            if (!m_state->joined || m_state->consumed)
                throw std::logic_error("TaskScope::TakeResult requires an unconsumed completed Join");
            m_state->consumed = true;
            return std::move(m_state->outcome);
        }

    private:
        std::shared_ptr<State> m_state;
    };
}// namespace NGIN::Async
