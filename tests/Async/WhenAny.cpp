#include <catch2/catch_test_macros.hpp>

#include <NGIN/Async/WhenAny.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/Execution/detail/CompletionQueue.hpp>

#include <atomic>
#include <memory_resource>
#include <stdexcept>

namespace
{
    using namespace NGIN;

    class FailingResource final : public std::pmr::memory_resource
    {
    public:
        std::size_t calls {};
        std::size_t failAt {static_cast<std::size_t>(-1)};
        std::size_t outstanding {};

    private:
        void* do_allocate(std::size_t size, std::size_t alignment) override
        {
            if (calls++ == failAt)
                throw std::bad_alloc();
            void* allocation = std::pmr::new_delete_resource()->allocate(size, alignment);
            ++outstanding;
            return allocation;
        }
        void do_deallocate(void* allocation, std::size_t size, std::size_t alignment) override
        {
            --outstanding;
            std::pmr::new_delete_resource()->deallocate(allocation, size, alignment);
        }
        bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
    };

    class Executor final
    {
    public:
        Executor(std::size_t capacity, std::pmr::memory_resource* resource = std::pmr::get_default_resource())
            : completions(capacity, nullptr, nullptr, resource) {}
        auto                      ReserveCompletion(Execution::WorkItem work) noexcept { return completions.Reserve(std::move(work)); }
        Execution::ScheduleResult Execute(Execution::WorkItem work) noexcept
        {
            if (stopped)
                return std::unexpected(Execution::ScheduleError::Stopped);
            return ordinary.Execute(std::move(work));
        }
        auto ExecuteAt(Execution::WorkItem work, Time::TimePoint) noexcept { return Execute(std::move(work)); }
        bool IsCurrent() const noexcept { return current; }
        bool RunOne()
        {
            current        = true;
            const bool ran = completions.RunOne() || ordinary.RunOne();
            current        = false;
            return ran;
        }
        void RunUntilIdle()
        {
            while (RunOne()) {}
        }
        void Stop()
        {
            stopped = true;
            completions.Close();
        }
        std::size_t Outstanding() const { return completions.Outstanding(); }

    private:
        Execution::CooperativeScheduler    ordinary;
        Execution::detail::CompletionQueue completions;
        bool                               current {};
        bool                               stopped {};
    };

    Async::Task<int> Immediate(Async::TaskContext&, int& entered)
    {
        ++entered;
        co_return 7;
    }
}// namespace

TEST_CASE("WhenAny reserves every notification before invoking a factory", "[Async][WhenAny][Admission]")
{
    FailingResource memory;
    std::size_t     capacity = 8;
    SECTION("capacity exhaustion")
    {
        capacity = 2;
    }
    SECTION("allocation failure")
    {
        memory.failAt = 2;
    }
    Executor           executor(capacity, &memory);
    Async::TaskContext ctx(executor);
    int                factories = 0;
    int                entered   = 0;
    auto               child     = [&](Async::TaskContext& context) {
        ++factories;
        return Immediate(context, entered);
    };
    auto operation = Async::Spawn(ctx, Async::WhenAny(ctx, child, child));
    executor.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    auto result = operation.TakeResult();
    REQUIRE(result.IsFault());
    REQUIRE(result.Fault().code == Async::AsyncFaultCode::SchedulerDispatchFailed);
    REQUIRE(result.Fault().native == static_cast<int>(Execution::ScheduleError::ResourceExhausted));
    REQUIRE(factories == 0);
    REQUIRE(entered == 0);
    REQUIRE(executor.Outstanding() == 0);
    REQUIRE(memory.outstanding == 0);
    auto recovered = Async::Spawn(ctx, Immediate(ctx, entered));
    executor.RunUntilIdle();
    REQUIRE(recovered.IsCompleted());
    REQUIRE(recovered.TakeResult().Value() == 7);
    REQUIRE(entered == 1);
    REQUIRE(executor.Outstanding() == 0);
}

TEST_CASE("WhenAny joins children rejected after notification admission", "[Async][WhenAny][Admission]")
{
    Executor           executor(3);// Parent plus both notifications; no child lifetime slot.
    Async::TaskContext ctx(executor);
    int                entered   = 0;
    auto               child     = [&](Async::TaskContext& context) { return Immediate(context, entered); };
    auto               operation = Async::Spawn(ctx, Async::WhenAny(ctx, child, child));
    executor.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    auto result = operation.TakeResult();
    REQUIRE(result.IsFault());
    REQUIRE(result.Fault().native == static_cast<int>(Execution::ScheduleError::ResourceExhausted));
    REQUIRE(entered == 0);
    REQUIRE(executor.Outstanding() == 0);
}

TEST_CASE("WhenAny joins existing children when another factory fails", "[Async][WhenAny][Lifetime]")
{
    Executor           executor(16);
    Async::TaskContext ctx(executor);
    bool               empty = false;
    SECTION("throwing factory") {}
    SECTION("empty task")
    {
        empty = true;
    }
    int destroyed = 0;
    struct Guard
    {
        int& count;
        ~Guard() { ++count; }
    };
    auto loser = [&](Async::TaskContext& child) -> Async::Task<int> {
        Guard guard {destroyed};
        co_await child.YieldNow();
        co_return 9;
    };
    auto failed = [&](Async::TaskContext&) -> Async::Task<int> {
        if (empty)
            return {};
        throw std::runtime_error("factory failed");
    };
    auto operation = Async::Spawn(ctx, Async::WhenAny(ctx, loser, failed));
    executor.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    auto result = operation.TakeResult();
    REQUIRE(result.IsFault());
    REQUIRE(result.Fault().code == (empty ? Async::AsyncFaultCode::InvalidTaskUsage : Async::AsyncFaultCode::UnhandledException));
    REQUIRE(destroyed == 1);
    REQUIRE(executor.Outstanding() == 0);
}

TEST_CASE("WhenAny releases coroutine factory captures before parent completion", "[Async][WhenAny][Lifetime]")
{
    Executor           executor(16);
    Async::TaskContext ctx(executor);
    auto               owner     = std::make_shared<int>(42);
    std::weak_ptr<int> lifetime  = owner;
    int                observed  = 0;
    auto               operation = Async::Spawn(ctx, Async::WhenAny(ctx,
                                                                    [owner = std::move(owner), &observed](Async::TaskContext& child) -> Async::Task<int> {
                                                          co_await child.YieldNow();
                                                          observed = *owner;
                                                          co_return observed;
                                                      }));
    executor.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operation.TakeResult().Value() == 0);
    REQUIRE(observed == 42);
    REQUIRE(lifetime.expired());
}

TEST_CASE("WhenAny reports mixed value and void child domain failures", "[Async][WhenAny]")
{
    Executor           executor(16);
    Async::TaskContext ctx(executor);
    auto               first = [](Async::TaskContext&) -> Async::Task<void, int> {
        co_await Async::DomainFailure(47);
        co_return;
    };
    auto second = [](Async::TaskContext& child) -> Async::Task<int, int> {
        co_await child.YieldNow();
        co_return 8;
    };
    // The cooperative ordinary queue is LIFO; make the domain failure run first.
    auto operation = Async::Spawn(ctx, Async::WhenAny(ctx, second, first));
    executor.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    auto result = operation.TakeResult();
    REQUIRE(result.IsDomainError());
    REQUIRE(result.DomainError() == 47);
    REQUIRE(executor.Outstanding() == 0);
}

TEST_CASE("WhenAny joins simultaneous completions and releases factory captures", "[Async][WhenAny][Race]")
{
    Execution::ThreadPoolScheduler executor(4);
    Async::TaskContext             ctx(executor);
    for (int iteration = 0; iteration < 128; ++iteration)
    {
        std::atomic<int> destroyed {0};
        struct Guard
        {
            std::atomic<int>& count;
            ~Guard() { ++count; }
        };
        auto               owner    = std::make_shared<int>(42);
        std::weak_ptr<int> lifetime = owner;
        auto               factory  = [owner = std::move(owner), &destroyed](Async::TaskContext& child) -> Async::Task<int> {
            Guard guard {destroyed};
            co_await child.YieldNow();
            co_return *owner;
        };
        auto second = factory;
        auto third  = factory;
        auto result = Async::SyncWait(ctx, Async::WhenAny(ctx, std::move(factory), std::move(second), std::move(third)));
        REQUIRE(result.Succeeded());
        REQUIRE(result.Value() < 3);
        REQUIRE(destroyed == 3);
        REQUIRE(lifetime.expired());
    }
}

TEST_CASE("WhenAny rejects an untracked parent before starting children", "[Async][WhenAny][Admission]")
{
    struct UntrackedExecutor
    {
        Execution::CooperativeScheduler queue;
        auto                            Execute(Execution::WorkItem work) noexcept { return queue.Execute(std::move(work)); }
        auto                            ExecuteAt(Execution::WorkItem work, Time::TimePoint when) noexcept { return queue.ExecuteAt(std::move(work), when); }
    } executor;
    Async::TaskContext context(executor);
    int                entered   = 0;
    int                invoked   = 0;
    auto               operation = Async::Spawn(context, Async::WhenAny(context,
                                                                        [&](Async::TaskContext& child) { ++invoked; return Immediate(child, entered); }));
    executor.queue.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    auto result = operation.TakeResult();
    REQUIRE(result.IsFault());
    REQUIRE(result.Fault().code == Async::AsyncFaultCode::SchedulerDispatchFailed);
    REQUIRE(result.Fault().native == static_cast<int>(Execution::ScheduleError::Rejected));
    REQUIRE(invoked == 0);
    REQUIRE(entered == 0);
}
