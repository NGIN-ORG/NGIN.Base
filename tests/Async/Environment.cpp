#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/TaskSupervisor.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory_resource>
#include <stdexcept>

namespace
{
    struct CountedResource final : std::pmr::memory_resource
    {
        unsigned allocations {};
        unsigned releases {};
        bool     reject {};
        void*    do_allocate(std::size_t bytes, std::size_t alignment) override
        {
            if (reject)
                throw std::bad_alloc();
            ++allocations;
            return std::pmr::new_delete_resource()->allocate(bytes, alignment);
        }
        void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
        {
            ++releases;
            std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
        }
        bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
    };

    NGIN::Async::Task<NGIN::Async::AsyncEnvironment> Inspect(int depth)
    {
        if (depth > 1)
            co_return co_await Inspect(depth - 1);
        auto                     before = co_await NGIN::Async::CurrentEnvironment();
        NGIN::Async::TaskContext current(before);
        co_await current.YieldNow();
        co_return co_await NGIN::Async::CurrentEnvironment();
    }
}// namespace

TEST_CASE("Nested tasks preserve an environment across suspension", "[Async][Environment]")
{
    NGIN::Execution::CooperativeScheduler executor(3);
    CountedResource                       resource;
    int                                   metadata = 42;
    NGIN::Async::CancellationSource       cancellation;
    NGIN::Async::AsyncEnvironment         environment {
                    .executor     = NGIN::Execution::ExecutorRef::From(executor),
                    .cancellation = cancellation.GetToken(),
                    .deadline     = NGIN::Time::TimePoint::FromNanoseconds(987654321),
                    .resource     = &resource,
                    .metadata     = &metadata,
                    .priority     = 3,
    };
    NGIN::Async::TaskContext context(environment);
    auto                     task = Inspect(64);
    REQUIRE(resource.allocations == 0);
    {
        auto operationTask = std::move(task);

        NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

        auto       operation           = operationRoot.Spawn(std::move(operationTask)).value();
        const auto admittedAllocations = resource.allocations;
        executor.RunUntilIdle();
        REQUIRE(operation.IsCompleted());
        REQUIRE(operationRoot.TryJoin());
        auto result = operation.TakeResult();
        REQUIRE(result.Succeeded());
        const auto& observed = result.Value();
        REQUIRE(observed.executor == environment.executor);
        REQUIRE(observed.deadline == environment.deadline);
        REQUIRE(observed.resource == &resource);
        REQUIRE(observed.metadata == &metadata);
        REQUIRE(observed.priority == 3);
        REQUIRE(observed.cancellation.HasState());
        cancellation.Cancel();
        REQUIRE(observed.cancellation.IsCancellationRequested());
        // Preserve the two execution-boundary allocations independently of root ownership storage.
        REQUIRE(resource.allocations == admittedAllocations + 2);
        operation = {};
    }
    REQUIRE(resource.releases == resource.allocations);
}

TEST_CASE("Environment construction rejects a null resource", "[Async][Environment]")
{
    NGIN::Async::AsyncEnvironment environment;
    environment.resource = nullptr;
    REQUIRE_THROWS_AS(NGIN::Async::TaskContext(environment), std::invalid_argument);
}

TEST_CASE("Rejected environment allocation never starts a task", "[Async][Environment]")
{
    NGIN::Execution::CooperativeScheduler executor;
    CountedResource                       resource;
    NGIN::Async::TaskContext              context(NGIN::Async::AsyncEnvironment {
                         .executor = NGIN::Execution::ExecutorRef::From(executor),
                         .resource = &resource});
    bool                                  entered       = false;
    auto                                  make          = [&]() -> NGIN::Async::Task<void> { entered = true; co_return; };
    auto                                  operationTask = make();

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    resource.reject = true;
    auto admitted   = operationRoot.Spawn(std::move(operationTask));
    REQUIRE_FALSE(admitted);
    REQUIRE(admitted.error() == NGIN::Execution::ScheduleError::ResourceExhausted);
    executor.RunUntilIdle();
    REQUIRE_FALSE(entered);
    REQUIRE(operationRoot.TryJoin());
}

TEST_CASE("Startup snapshots context properties before queued execution", "[Async][Environment]")
{
    NGIN::Execution::CooperativeScheduler executor(3);// Execution, owner publication, and the yielding continuation.
    int                                   original    = 1;
    int                                   replacement = 2;
    NGIN::Async::TaskContext              context(NGIN::Async::AsyncEnvironment {
                         .executor = NGIN::Execution::ExecutorRef::From(executor),
                         .metadata = &original});
    auto                                  operationTask = Inspect(8);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    context        = NGIN::Async::TaskContext(NGIN::Async::AsyncEnvironment {
                   .executor = NGIN::Execution::ExecutorRef::From(executor),
                   .metadata = &replacement});
    executor.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    REQUIRE(result.Succeeded());
    REQUIRE(result.Value().metadata == &original);
    REQUIRE(context.GetEnvironment().metadata == &replacement);
}

TEST_CASE("Context cancellation replacement preserves other environment properties", "[Async][Environment]")
{
    int                             metadata = 7;
    CountedResource                 resource;
    NGIN::Async::TaskContext        context(NGIN::Async::AsyncEnvironment {
                   .deadline = NGIN::Time::TimePoint::FromNanoseconds(42),
                   .resource = &resource,
                   .metadata = &metadata,
                   .priority = -2});
    NGIN::Async::CancellationSource source;
    auto                            copy = context.WithCancellationToken(source.GetToken());
    source.Cancel();
    const auto environment = copy.GetEnvironment();
    REQUIRE(environment.deadline == context.GetEnvironment().deadline);
    REQUIRE(environment.resource == &resource);
    REQUIRE(environment.metadata == &metadata);
    REQUIRE(environment.priority == -2);
    REQUIRE(environment.cancellation.IsCancellationRequested());
    REQUIRE_FALSE(context.IsCancellationRequested());
    REQUIRE(resource.allocations == 0);
}
