/// @file Race.hpp
/// @brief Owning first-terminal competition with joined cleanup and retained errors.
#pragma once

#include <NGIN/Async/FirstCompleted.hpp>
#include <NGIN/Async/TaskScope.hpp>
#include <array>
#include <tuple>
#include <variant>

namespace NGIN::Async
{
    /// Input index and its successful value. Void competitors use monostate;
    /// duplicate value types remain distinct alternatives selected by index.
    template<typename... Ts>
    struct RaceWinner final
    {
        using Values = std::variant<std::conditional_t<std::is_void_v<Ts>, std::monostate, Ts>...>;
        std::size_t index;
        Values      value;
    };

    /// Bounded, input-indexed child errors. primaryIndex selects the error winner,
    /// otherwise the first input with an error. Successful/stopped winner information
    /// remains available when a loser error changes the group's final outcome.
    template<typename E, typename... Ts>
    struct RaceFailure final
    {
        CompletionNotification                                  winner;
        std::shared_ptr<const RaceWinner<Ts...>>                value;
        std::optional<NGIN::Async::StopReason>                  stopped;
        std::array<std::optional<AsyncError<E>>, sizeof...(Ts)> errors;
        std::size_t                                             primaryIndex {};
        const AsyncError<E>&                                    Primary() const noexcept { return *errors[primaryIndex]; }
    };

    namespace detail::race
    {
        struct Tracked final
        {
            bool valid {};
            bool await_ready() const noexcept { return false; }
            template<typename Promise>
            bool await_suspend(std::coroutine_handle<Promise> awaiting) noexcept
            {
                if constexpr (std::derived_from<Promise, PromiseRuntimeCommon>)
                    valid = awaiting.promise().HasContinuation();
                return false;
            }
            bool await_resume() const noexcept { return valid; }
        };

        template<typename Factory>
        using FactoryTask = std::remove_cvref_t<std::invoke_result_t<Factory&, TaskContext&>>;
        template<typename... Factories>
        using Winner = RaceWinner<typename FactoryTask<Factories>::ValueType...>;
        template<typename... Factories>
        using Error = typename FactoryTask<std::tuple_element_t<0, std::tuple<Factories...>>>::ErrorType;
        template<typename... Factories>
        using Failure = RaceFailure<Error<Factories...>, typename FactoryTask<Factories>::ValueType...>;

        template<std::size_t Count>
        struct Arbitration final
        {
            struct Candidate final
            {
                Arbitration* owner;
                std::size_t  index;
            };
            explicit Arbitration(const AsyncEnvironment& environment) : cancellation(environment.resource)
            {
                cancellation.reserve(Count);
                for (std::size_t index = 0; index != Count; ++index)
                {
                    cancellation.emplace_back(std::initializer_list<CancellationToken> {environment.cancellation}, environment.resource);
                    candidates[index] = Candidate {this, index};
                }
            }
            static void Publish(void* context) noexcept
            {
                const auto& candidate = *static_cast<Candidate*>(context);
                auto&       state     = *candidate.owner;
                std::size_t empty     = Count;
                if (state.winner.compare_exchange_strong(empty, candidate.index, std::memory_order_acq_rel))
                    for (std::size_t index = 0; index != Count; ++index)
                        if (index != candidate.index)
                            state.cancellation[index].Cancel(StopReason::OwnerClosed);
            }
            std::atomic<std::size_t>                   winner {Count};
            std::pmr::vector<LinkedCancellationSource> cancellation;
            std::array<Candidate, Count>               candidates;
        };

        template<std::size_t Index, typename E, typename Factory, std::size_t Count>
        auto Admit(TaskScope<E>& scope, Arbitration<Count>& arbitration, Factory&& factory,
                   std::optional<NGIN::Execution::ScheduleError>& failure)
        {
            using Value = typename FactoryTask<std::decay_t<Factory>>::ValueType;
            if (failure)
                return Operation<Value, E> {};
            auto admitted = ScopeBatchAccess::Prepare(scope, std::forward<Factory>(factory),
                                                      arbitration.cancellation[Index].GetToken(), &Arbitration<Count>::Publish,
                                                      &arbitration.candidates[Index]);
            if (!admitted)
            {
                failure = admitted.error();
                return Operation<Value, E> {};
            }
            return std::move(*admitted);
        }

        inline AsyncFault ExceptionFault() noexcept
        {
            auto fault = MakeAsyncFault(AsyncFaultCode::UnhandledException);
#if NGIN_ASYNC_CAPTURE_EXCEPTIONS
            fault.capturedException = std::current_exception();
#endif
            return fault;
        }

        template<std::size_t Index, typename Failure, typename Operation>
        void CaptureError(Failure& failure, Operation& operation)
        {
            if (operation.HasError())
                failure.errors[Index].emplace(std::move(operation.TakeResult()).Error());
        }

        template<std::size_t Index, typename Failure, typename Operation, typename Storage>
        void CaptureWinner(Failure& failure, Operation& operation, const std::shared_ptr<Storage>& storage)
        {
            if (failure.winner.index != Index)
                return;
            if (failure.errors[Index])
            {
                failure.winner.kind = CompletionKind::Error;
                return;
            }
            try
            {
                auto result         = operation.TakeResult();
                failure.winner.kind = result.Kind();
                if (result.IsStopped())
                    failure.stopped = result.StopReason();
                else if (result.HasError())
                    failure.errors[Index].emplace(std::move(result).Error());
                else
                {
                    using WinnerType = typename Storage::value_type;
                    if constexpr (std::is_void_v<typename Operation::ValueType>)
                        storage->emplace(WinnerType {Index, typename WinnerType::Values(std::in_place_index<Index>)});
                    else
                        storage->emplace(WinnerType {Index, typename WinnerType::Values(std::in_place_index<Index>, std::move(result).Value())});
                    failure.value = std::shared_ptr<const WinnerType>(storage, &**storage);
                }
            } catch (...)
            {
                failure.errors[Index].emplace(AsyncError<typename Operation::ErrorType>::FromFault(ExceptionFault()));
            }
        }

        template<typename... Factories, std::size_t... Indices>
        Task<Winner<Factories...>, Failure<Factories...>> Run(
                TaskContext& context, std::tuple<Factories...> factories, std::index_sequence<Indices...>)
        {
            using E      = Error<Factories...>;
            using Result = Completion<Winner<Factories...>, Failure<Factories...>>;
            static_assert((std::is_same_v<E, typename FactoryTask<Factories>::ErrorType> && ...),
                          "Race competitors must share their domain-error type");
            if (context.GetCancellationToken().IsCancellationRequested())
                co_return Result::Stopped(context.GetCancellationToken().GetStopReason());
            if (!(co_await Tracked {}))
                co_return Result::Faulted(MakeSchedulingFault(AsyncFaultCode::SchedulerDispatchFailed, NGIN::Execution::ScheduleError::Rejected));
            std::optional<TaskScope<E>>                      scopeOwner;
            std::optional<Arbitration<sizeof...(Factories)>> arbitrationOwner;
            using WinnerStorage = std::optional<Winner<Factories...>>;
            std::shared_ptr<WinnerStorage>                                          valueStorage;
            std::tuple<Operation<typename FactoryTask<Factories>::ValueType, E>...> operations;
            std::optional<NGIN::Execution::ScheduleError>                           admissionFailure;
            std::optional<AsyncFault>                                               fault;
            try
            {
                scopeOwner.emplace(context.GetEnvironment(), sizeof...(Factories));
                arbitrationOwner.emplace(scopeOwner->GetEnvironment());
                // Reserve retained winner storage before startup. Returning a loser
                // error must not move a potentially throwing successful payload.
                valueStorage = std::allocate_shared<WinnerStorage>(
                        std::pmr::polymorphic_allocator<WinnerStorage>(context.GetEnvironment().resource));
                operations = decltype(operations) {Admit<Indices>(*scopeOwner, *arbitrationOwner,
                                                                  std::move(std::get<Indices>(factories)), admissionFailure)...};
            } catch (const std::bad_alloc&)
            {
                fault = MakeSchedulingFault(AsyncFaultCode::SchedulerDispatchFailed, NGIN::Execution::ScheduleError::ResourceExhausted);
            } catch (...)
            {
                fault = ExceptionFault();
            }
            if (fault || admissionFailure)
            {
                if (scopeOwner)
                    ScopeBatchAccess::Abort(*scopeOwner);
                if (fault)
                    co_return Result::Faulted(std::move(*fault));
                if (*admissionFailure == NGIN::Execution::ScheduleError::Stopped)
                    co_return Result::Stopped(context.GetCancellationToken().GetStopReason());
                co_return Result::Faulted(MakeSchedulingFault(AsyncFaultCode::SchedulerDispatchFailed, *admissionFailure));
            }
            auto& scope       = *scopeOwner;
            auto& arbitration = *arbitrationOwner;
            ScopeBatchAccess::Start(scope);
            (void) co_await scope.Join();
            Failure<Factories...> failure {};
            failure.winner.index = arbitration.winner.load(std::memory_order_acquire);
            assert(failure.winner.index < sizeof...(Factories));
            (CaptureError<Indices>(failure, std::get<Indices>(operations)), ...);
            (CaptureWinner<Indices>(failure, std::get<Indices>(operations), valueStorage), ...);
            auto primary = failure.winner.index;
            if (!failure.errors[primary])
                for (primary = 0; primary != sizeof...(Factories) && !failure.errors[primary]; ++primary) {}
            if (primary != sizeof...(Factories))
            {
                failure.primaryIndex = primary;
                co_return Result::DomainFailure(std::move(failure));
            }
            if (failure.stopped)
                co_return Result::Stopped(*failure.stopped);
            co_return std::move(**valueStorage);
        }
    }// namespace detail::race

    /// Admits every factory before invoking any of them. First terminal publication
    /// requests loser stop; all frames retire before the winner or errors are returned.
    /// Error winners remain primary; otherwise any loser error overrides the winner.
    template<typename... Factories>
        requires(sizeof...(Factories) > 0)
    [[nodiscard]] auto Race(TaskContext& context, Factories... factories)
            -> Task<detail::race::Winner<Factories...>, detail::race::Failure<Factories...>>
    {
        return detail::race::Run(context, std::tuple<Factories...>(std::move(factories)...),
                                 std::index_sequence_for<Factories...> {});
    }
}// namespace NGIN::Async
