#include <NGIN/Async/TaskScope.hpp>
#include <NGIN/Async/TaskSupervisor.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace
{
    using namespace NGIN::Async;
    struct Resource final : std::pmr::memory_resource
    {
        std::size_t attempts {}, live {}, failAt = std::numeric_limits<std::size_t>::max();
        void*       do_allocate(std::size_t bytes, std::size_t alignment) override
        {
            if (attempts++ == failAt)
                throw std::bad_alloc();
            auto pointer = std::pmr::new_delete_resource()->allocate(bytes, alignment);
            ++live;
            return pointer;
        }
        void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
        {
            --live;
            std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
        }
        bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
    };
    Task<void> Join(TaskScope<int>& scope)
    {
        (void) co_await scope.Join();
    }
}// namespace

TEST_CASE("Scope construction releases every partial allocation", "[Async][Scope][Allocation]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    Resource                              resource;
    AsyncEnvironment                      environment {.executor = NGIN::Execution::ExecutorRef::From(scheduler), .resource = &resource};
    std::size_t                           allocations;
    {
        TaskScope<int> scope(environment, 2);
        allocations = resource.attempts;
        REQUIRE(allocations > 0);
    }
    REQUIRE(resource.live == 0);
    for (std::size_t index = 0; index != allocations; ++index)
    {
        resource.failAt = resource.attempts + index;
        REQUIRE_THROWS_AS((TaskScope<int>(environment, 2)), std::bad_alloc);
        REQUIRE(resource.live == 0);
    }
    environment.resource = nullptr;
    REQUIRE_THROWS_AS((TaskScope<int>(environment, 2)), std::invalid_argument);
}

TEST_CASE("Scope record allocation failures reject admission before invoking the factory", "[Async][Scope][Allocation]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    Resource                              resource;
    const std::size_t                     offset = GENERATE(0u, 1u, 2u);
    {
        TaskScope<int> scope(AsyncEnvironment {.executor = NGIN::Execution::ExecutorRef::From(scheduler), .resource = &resource}, 1);
        const auto     live = resource.live;
        resource.failAt     = resource.attempts + offset;
        bool invoked        = false;
        auto rejected       = scope.Spawn([&](TaskContext&) {
            invoked = true;
            return Task<int, int>::FromCompletion(Completion<int, int>::Success(42));
        });
        REQUIRE_FALSE(rejected);
        REQUIRE(rejected.error() == NGIN::Execution::ScheduleError::ResourceExhausted);
        REQUIRE_FALSE(invoked);
        REQUIRE(scope.Pending() == 0);
        REQUIRE(resource.live == live);
    }
    REQUIRE(resource.live == 0);
}

TEST_CASE("Retained scope reports release resource storage after the last diagnostic", "[Async][Scope][Allocation]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    TaskContext                           context(scheduler);
    Resource                              resource;
    std::optional<AsyncError<int>>        retained;
    {
        TaskScope<int> scope(AsyncEnvironment {.executor = context.GetExecutor(), .resource = &resource}, 1);
        REQUIRE(scope.Spawn([](TaskContext&) -> Task<int, int> {
            co_return Completion<int, int>::DomainFailure(42);
        }));
        scheduler.RunUntilIdle();
        const auto attempts = resource.attempts;
        resource.failAt     = attempts;
        auto joinedTask     = Join(scope);

        TaskSupervisor<typename decltype(joinedTask)::ErrorType> joinedRoot(context.GetEnvironment(), 1);

        auto joined = joinedRoot.Spawn(std::move(joinedTask)).value();
        scheduler.RunUntilIdle();
        REQUIRE(joinedRoot.TryJoin());
        REQUIRE(joined.TakeResult().Succeeded());
        retained.emplace(scope.TakeResult().RetainError());
        REQUIRE(resource.attempts == attempts);
    }
    REQUIRE(retained->DomainError() == 42);
    REQUIRE(resource.live > 0);
    retained.reset();
    REQUIRE(resource.live == 0);
}
