/// @file TaskScope.hpp
/// @brief Bounded ownership, cancellation, and explicit joining of child tasks.
#pragma once

#include <NGIN/Async/ScopeReport.hpp>
#include <NGIN/Async/Task.hpp>

#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace NGIN::Async
{
    namespace detail
    {
        struct RootScopeAccess;
        struct ScopeBatchAccess;
        struct SupervisorAccess;
    }// namespace detail
    /// @brief Owns child operations and their contexts until an explicit join.
    /// @details A child failure requests sibling cancellation. Join closes admission,
    /// waits for every child, and exposes the lowest-spawn-index error and all secondary diagnostics. Destruction
    /// with unfinished children terminates; it never blocks or silently detaches.
    /// Factories are retained until joining, including coroutine lambda captures.
    /// Spawn, RequestStop, and status inspection are thread-safe. There may be
    /// only one pending Join; the scope and its executors must outlive that join.
    template<typename E = NoError>
    class TaskScope final
    {
        struct ChildBase
        {
            virtual void                    Start() noexcept = 0;
            detail::TaskExecutionStateOwner execution;
            void (*terminal)(void*) noexcept {};
            void* terminalContext {};
            virtual ~ChildBase()                                                 = default;
            virtual Completion<void, E>                Consume()                 = 0;
            virtual void                               Retire() noexcept         = 0;
            virtual detail::OwnedOperationContinuation Publish() noexcept        = 0;
            virtual void                               ReleaseFactory() noexcept = 0;
            virtual AsyncError<E>                      RetainError()             = 0;
            std::optional<Completion<void, E>>         outcome;
            bool                                       observed {};
            NGIN::Execution::CompletionReservation     notification;
        };

        template<typename Factory>
        struct Child final : ChildBase
        {
            using ChildTask = std::remove_cvref_t<std::invoke_result_t<Factory&, TaskContext&>>;
            using Value     = typename ChildTask::ValueType;

            Child(Factory callback, AsyncEnvironment environment)
                : factory(std::move(callback)), context(environment),
                  result(std::allocate_shared<detail::OwnedOperationState<Value, E>>(
                          std::pmr::polymorphic_allocator<detail::OwnedOperationState<Value, E>>(environment.resource))) {}

            void Start() noexcept override
            {
                try
                {
                    auto task = std::invoke(*factory, context);
                    operation = detail::OperationAccess::Start(context, std::move(task), std::move(this->execution));
                } catch (...)
                {
                    AsyncFault fault = MakeAsyncFault(AsyncFaultCode::UnhandledException);
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                    fault.capturedException = std::current_exception();
#endif
                    creationFailure = std::move(fault);
                    // No producer owns this reservation after factory failure.
                    // Retaining diagnostics must not retain execution capacity.
                    this->execution.reset();
                    if (this->terminal)
                        this->terminal(this->terminalContext);
                    this->notification.Schedule();
                    return;
                }
                // Inline inputs have no promise publication hook. A concurrently
                // finished coroutine may repeat the owner's idempotent claim.
                if (this->terminal && (!operation.IsValid() || operation.IsCompleted()))
                    this->terminal(this->terminalContext);
                if (!detail::OperationAccess::ObserveReusable(operation, this->notification))
                    std::terminate();
            }

            Completion<void, E> Consume() override
            {
                try
                {
                    if (creationFailure)
                        result->StoreFault(std::move(*creationFailure));
                    else
                        result->Store(operation.TakeResult());
                } catch (...)
                {
                    AsyncFault fault = MakeAsyncFault(AsyncFaultCode::UnhandledException);
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
                    fault.capturedException = std::current_exception();
#endif
                    result->StoreFault(std::move(fault));
                }
                return result->Report();
            }

            detail::OwnedOperationContinuation Publish() noexcept override { return result->Publish(); }
            AsyncError<E>                      RetainError() override { return std::move(result->Report()).Error(); }

            void Retire() noexcept override
            {
                detail::OperationAccess::Retire(operation, this->notification);
            }

            void                                                   ReleaseFactory() noexcept override { factory.reset(); }
            std::optional<Factory>                                 factory;
            TaskContext                                            context;
            detail::ProducerOperation<Value, E>                    operation;
            std::shared_ptr<detail::OwnedOperationState<Value, E>> result;
            std::optional<AsyncFault>                              creationFailure;
        };

        struct State final : std::enable_shared_from_this<State>
        {
            State(AsyncEnvironment initialEnvironment, std::size_t childCapacity)
                : environment(std::move(initialEnvironment)), executor(this->environment.executor),
                  cancellation({this->environment.cancellation}, this->environment.resource), capacity(childCapacity),
                  children(this->environment.resource),
                  report(std::allocate_shared<detail::ScopeReportStorage<E>>(
                          std::pmr::polymorphic_allocator<detail::ScopeReportStorage<E>>(this->environment.resource),
                          this->environment.resource))
            {
                this->environment.Validate();
                if (!executor.SupportsCompletionReservations() || capacity == 0)
                    throw std::invalid_argument("TaskScope requires a tracked executor and positive child capacity");
                this->environment.cancellation = cancellation.GetToken();
                children.reserve(capacity);
                report->errors.reserve(capacity);
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
                    try
                    {
                        child.outcome.emplace(std::move(result));
                    } catch (...)
                    {
                        child.outcome.emplace(Completion<void, E>::Faulted(MakeAsyncFault(AsyncFaultCode::UnhandledException)));
                    }
                    if (child.outcome->HasError())
                    {
                        if (!supervised)
                            cancellation.Cancel(StopReason::OwnerClosed);
                        else if (!errorSink)
                            Unhandled();
                    }
                    child.Retire();
                    return;
                }
                // A second delivery on the same reserved slot follows final
                // frame destruction, not merely terminal result publication.
                child.notification.Reset();
                bool recycle = supervised;
                if (supervised && child.outcome->HasError())
                {
                    // Reporting runs after producer retirement and outside all
                    // owner locks. A rejected report keeps this diagnostic record.
                    const bool accepted = errorSink && errorSink(errorContext, child.RetainError());
                    recycle             = accepted;
                    if (!accepted)
                        Unhandled();
                }
                if (recycle)
                    child.ReleaseFactory();
                std::shared_ptr<ChildBase> retiredChild;
                void (*retired)(void*) noexcept              = nullptr;
                void*                         retiredContext = nullptr;
                auto                          consumer       = child.Publish();
                NGIN::Execution::WorkItem     resume;
                detail::PromiseRuntimeCommon* target = nullptr;
                {
                    std::lock_guard lock(mutex);
                    assert(pending != 0);
                    if (recycle)
                    {
                        if (child.outcome->IsStopped() && !report->stopped)
                            report->stopped = child.outcome->StopReason();
                        const auto found = std::find_if(children.begin(), children.end(),
                                                        [&child](const auto& entry) { return entry.get() == &child; });
                        assert(found != children.end());
                        retiredChild = std::move(*found);
                        children.erase(found);
                    }
                }
                // Finish allocator-owned record cleanup before publishing the last
                // pending decrement. An independent ready Join may release the owner
                // as soon as that decrement becomes visible.
                retiredChild.reset();
                {
                    std::lock_guard lock(mutex);
                    --pending;
                    if (pending == 0)
                    {
                        retired        = retirementCallback;
                        retiredContext = retirementContext;
                    }
                    if (pending == 0 && joinTarget)
                    {
                        target = std::exchange(joinTarget, nullptr);
                        resume = std::move(joinResume);
                    }
                }
                // Pending ownership kept this raw callback valid until the final
                // lock release. No owner/child access follows possible user dispatch.
                consumer.Dispatch();
                if (target)
                {
                    const auto destination = target->ChainState().m_executor;
                    if (!destination.TryDispatchInline(resume))
                        target->QueueContinuation(std::move(resume));
                }
                if (retired)
                    retired(retiredContext);
            }

            void Unhandled() noexcept
            {
                {
                    std::lock_guard lock(mutex);
                    closing = true;
                }
                cancellation.Cancel(StopReason::OwnerClosed);
            }

            bool supervised {};
            bool (*errorSink)(void*, AsyncError<E>) noexcept {};
            void* errorContext {};
            void (*retirementCallback)(void*) noexcept {};
            void*                                          retirementContext {};
            AsyncEnvironment                               environment;
            NGIN::Execution::ExecutorRef                   executor;
            LinkedCancellationSource                       cancellation;
            const std::size_t                              capacity;
            mutable std::mutex                             mutex;
            std::pmr::vector<std::shared_ptr<ChildBase>>   children;
            std::size_t                                    pending {};
            std::shared_ptr<detail::ScopeReportStorage<E>> report;
            bool                                           closing {};
            bool                                           joining {};
            bool                                           joined {};
            bool                                           consumed {};
            detail::PromiseRuntimeCommon*                  joinTarget {};
            NGIN::Execution::WorkItem                      joinResume;
        };

    public:
        /// @brief Creates an empty scope whose terminal notifications run on executor.
        /// @param parent Requests cancellation of every child when canceled.
        /// @param capacity Maximum children retained before Join (default 256).
        explicit TaskScope(NGIN::Execution::ExecutorRef executor, CancellationToken parent = {}, std::size_t capacity = 256)
            : TaskScope(AsyncEnvironment {.executor = executor, .cancellation = std::move(parent)}, capacity) {}

        explicit TaskScope(AsyncEnvironment environment, std::size_t capacity = 256)
            : m_state(CreateState(std::move(environment), capacity)) {}

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
            requires std::invocable<std::decay_t<Factory>&, TaskContext&>
        [[nodiscard]] auto Spawn(Factory&& factory)
        {
            return SpawnOn(m_state->executor, std::forward<Factory>(factory));
        }

        /// @brief Starts a child on an explicitly chosen, independently driven executor.
        /// @details Admission is nonblocking and reserves terminal notification before
        /// invoking the factory. Rejection starts no child. An admitted child whose
        /// factory or executor fails contributes a fault to Join and the result handle;
        /// child domain errors must use the scope's E. Each factory borrows only the
        /// stable TaskContext passed to it, and retains its captures until Join.
        template<typename Factory>
            requires std::invocable<std::decay_t<Factory>&, TaskContext&>
        [[nodiscard]] auto SpawnOn(NGIN::Execution::ExecutorRef executor, Factory&& factory)
                -> std::expected<Operation<typename std::remove_cvref_t<std::invoke_result_t<std::decay_t<Factory>&, TaskContext&>>::ValueType, E>, NGIN::Execution::ScheduleError>
        {
            auto admitted = Prepare(executor, std::forward<Factory>(factory));
            if (!admitted)
                return std::unexpected(admitted.error());
            (*admitted)->Start();
            return detail::OperationAccess::ResultHandle((*admitted)->result);
        }

        /// @brief Admits a cold task; rejection destroys it without starting its body.
        template<typename T>
        [[nodiscard]] std::expected<Operation<T, E>, NGIN::Execution::ScheduleError> Spawn(Task<T, E> task)
        {
            return SpawnOn(m_state->executor, std::move(task));
        }

        template<typename T>
        [[nodiscard]] std::expected<Operation<T, E>, NGIN::Execution::ScheduleError>
        SpawnOn(NGIN::Execution::ExecutorRef executor, Task<T, E> task)
        {
            return SpawnOn(executor, [task = std::move(task)](TaskContext&) mutable {
                return std::move(task);
            });
        }

        /// @brief Requests child cancellation without waiting for backend or task completion.
        void RequestStop(StopReason reason = StopReason::Requested) noexcept { m_state->cancellation.Cancel(reason); }
        void Close() noexcept
        {
            std::lock_guard lock(m_state->mutex);
            m_state->closing = true;
        }
        AsyncEnvironment                GetEnvironment() const noexcept { return m_state->environment; }
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
                if (!awaiting.promise().HasContinuation())
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

            /// @brief Returns an owning report with stable, spawn-ordered diagnostics.
            /// @details Completed child frames and factory captures are released before return.
            ScopeReport<E> await_resume()
            {
                {
                    std::lock_guard lock(m_state->mutex);
                    assert(m_state->pending == 0);
                    if (m_state->joined)
                    {
                        m_state->joining = false;
                        return ScopeReport<E>(m_state->report);
                    }
                    if (!m_state->joined)
                    {
                        for (const auto& child: m_state->children)
                        {
                            assert(child->outcome);
                            if (child->outcome->HasError())
                                m_state->report->errors.push_back(AsyncError<E>::Retain(
                                        std::shared_ptr<const AsyncError<E>>(child, &child->outcome->Error())));
                            else if (child->outcome->IsStopped() && !m_state->report->stopped)
                                m_state->report->stopped = child->outcome->StopReason();
                        }
                    }
                }
                // User capture destructors run outside the owner lock, before join returns.
                for (const auto& child: m_state->children)
                    child->ReleaseFactory();
                {
                    std::lock_guard lock(m_state->mutex);
                    m_state->joined  = true;
                    m_state->joining = false;
                }
                return ScopeReport<E>(m_state->report);
            }

        private:
            std::shared_ptr<State> m_state;
        };

        /// @brief Closes child admission and asynchronously joins every admitted child.
        /// @details Cancellation does not abandon the join. It uses the awaiting task's
        /// existing reservation and remains available during executor shutdown. Later
        /// sequential joins observe the same report without admitting work.
        [[nodiscard]] JoinAwaiter Join() { return JoinAwaiter(m_state); }

        /// @brief Returns the joined report to the caller once.
        /// @throws std::logic_error if no join completed or the result was already consumed.
        [[nodiscard]] ScopeReport<E> TakeResult()
        {
            std::lock_guard lock(m_state->mutex);
            if (!m_state->joined || m_state->consumed)
                throw std::logic_error("TaskScope::TakeResult requires an unconsumed completed Join");
            m_state->consumed = true;
            return ScopeReport<E>(m_state->report);
        }

    private:
        friend struct detail::RootScopeAccess;
        friend struct detail::ScopeBatchAccess;
        friend struct detail::SupervisorAccess;
        template<typename Factory>
        auto Prepare(NGIN::Execution::ExecutorRef executor, Factory&& factory,
                     const CancellationToken* cancellation = nullptr,
                     void (*terminal)(void*) noexcept = nullptr, void* terminalContext = nullptr)
                -> std::expected<std::shared_ptr<Child<std::decay_t<Factory>>>, NGIN::Execution::ScheduleError>
        {
            using OwnedFactory = std::decay_t<Factory>;
            using OwnedChild   = Child<OwnedFactory>;
            static_assert(std::is_same_v<typename OwnedChild::ChildTask::ErrorType, E>,
                          "TaskScope children must use the scope's domain error type");
            std::shared_ptr<State>      state = m_state;
            std::shared_ptr<OwnedChild> child;
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
                    auto environment     = state->environment;
                    environment.executor = executor;
                    if (cancellation)
                        environment.cancellation = *cancellation;
                    child = std::allocate_shared<OwnedChild>(
                            std::pmr::polymorphic_allocator<OwnedChild>(environment.resource),
                            std::forward<Factory>(factory), environment);
                    admitted      = child.get();
                    auto reserved = state->executor.ReserveCompletion(NGIN::Execution::WorkItem([owner = state.get(), admitted] {
                        owner->Complete(*admitted);
                    }));
                    if (!reserved)
                        return std::unexpected(reserved.error());
                    admitted->notification = std::move(*reserved);
                    admitted->execution    = detail::TaskExecutionState::Create(detail::TaskContinuationState::Empty, environment.resource);
                    if (!admitted->execution)
                        return std::unexpected(NGIN::Execution::ScheduleError::ResourceExhausted);
                    const auto executionReserved = admitted->execution->Reserve(executor);
                    if (!executionReserved)
                        return std::unexpected(executionReserved.error());
                    admitted->terminal                      = terminal;
                    admitted->terminalContext               = terminalContext;
                    admitted->execution->m_terminalObserver = terminal;
                    admitted->execution->m_terminalContext  = terminalContext;
                } catch (const std::bad_alloc&)
                {
                    return std::unexpected(NGIN::Execution::ScheduleError::ResourceExhausted);
                }
                state->children.push_back(child);
                ++state->pending;
            }
            return child;
        }

        static std::shared_ptr<State> CreateState(AsyncEnvironment environment, std::size_t capacity)
        {
            environment.Validate();
            return std::allocate_shared<State>(std::pmr::polymorphic_allocator<State>(environment.resource),
                                               environment, capacity);
        }
        std::shared_ptr<State> m_state;
    };
    namespace detail
    {
        struct SupervisorAccess final
        {
            template<typename E>
            static void Configure(TaskScope<E>& scope, bool (*sink)(void*, AsyncError<E>) noexcept, void* context) noexcept
            {
                auto& state = *scope.m_state;
                assert(state.children.empty() && !state.closing);
                state.supervised   = true;
                state.errorSink    = sink;
                state.errorContext = context;
            }
        };

        /// An owning combinator prepares every child before committing startup.
        /// No external callers can observe handles from an aborted admission batch.
        struct ScopeBatchAccess final
        {
            template<typename E, typename Factory>
            static auto Prepare(TaskScope<E>& scope, Factory&& factory, const CancellationToken& token,
                                void (*terminal)(void*) noexcept, void* context)
            {
                using Value   = typename std::remove_cvref_t<std::invoke_result_t<std::decay_t<Factory>&, TaskContext&>>::ValueType;
                using Result  = std::expected<Operation<Value, E>, NGIN::Execution::ScheduleError>;
                auto admitted = scope.Prepare(scope.m_state->executor, std::forward<Factory>(factory), &token, terminal, context);
                if (!admitted)
                    return Result(std::unexpected(admitted.error()));
                return Result(OperationAccess::ResultHandle((*admitted)->result));
            }
            template<typename E>
            static void Start(TaskScope<E>& scope) noexcept
            {
                scope.Close();
                for (const auto& child: scope.m_state->children)
                    child->Start();
            }
            template<typename E>
            static void Abort(TaskScope<E>& scope) noexcept
            {
                auto&                                                               state = *scope.m_state;
                std::pmr::vector<std::shared_ptr<typename TaskScope<E>::ChildBase>> discarded(state.environment.resource);
                {
                    std::lock_guard lock(state.mutex);
                    state.closing = true;
                    state.pending = 0;
                    discarded.swap(state.children);
                }
                // Factory destructors run outside the owner lock.
                discarded.clear();
            }
        };

        /// Internal entry points install their signal before admission and finalize after it fires.
        struct RootScopeAccess final
        {
            template<typename E>
            static void OnRetirement(TaskScope<E>& scope, void (*callback)(void*) noexcept, void* context) noexcept
            {
                assert(scope.m_state->pending == 0 && !scope.m_state->retirementCallback);
                scope.m_state->retirementCallback = callback;
                scope.m_state->retirementContext  = context;
            }
            template<typename E>
            static void JoinRetired(TaskScope<E>& scope)
            {
                auto join = scope.Join();
                if (!join.await_ready())
                    std::terminate();
                (void) join.await_resume();
            }
        };
    }// namespace detail
}// namespace NGIN::Async

#include <NGIN/Async/SyncWait.hpp>
