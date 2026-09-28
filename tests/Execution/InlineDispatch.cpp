#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Execution/ExecutorRef.hpp>
#include <NGIN/Execution/InlineScheduler.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Inline dispatch requires executor context and preserves work on refusal", "[Execution][Dispatch]")
{
    using namespace NGIN::Execution;
    CooperativeScheduler scheduler;
    auto                 executor = ExecutorRef::From(scheduler);
    int                  calls    = 0;
    WorkItem             work([&] { ++calls; });
    REQUIRE_FALSE(executor.TryDispatchInline(work));
    REQUIRE_FALSE(work.IsEmpty());
    REQUIRE(calls == 0);
    REQUIRE(executor.Execute([&] {
        {
            detail::WorkBudget budget(0);
            REQUIRE_FALSE(executor.TryDispatchInline(work));
            REQUIRE_FALSE(work.IsEmpty());
        }
        REQUIRE(executor.TryDispatchInline(work));
        REQUIRE(work.IsEmpty());
        REQUIRE(calls == 1);
        REQUIRE(executor.Execute([&] { ++calls; }));
        REQUIRE(calls == 1);
    }));
    REQUIRE(scheduler.RunOne());
    REQUIRE(calls == 1);
    scheduler.RunUntilIdle();
    REQUIRE(calls == 2);
}

TEST_CASE("Recursive inline dispatch falls back to reserved delivery without losing work", "[Execution][Dispatch]")
{
    using namespace NGIN::Execution;
    CooperativeScheduler scheduler(1);
    auto                 executor = ExecutorRef::From(scheduler);
    WorkItem             pending;
    auto                 ticket = executor.ReserveCompletion(WorkItem([&] {
        auto work = std::move(pending);
        work.Invoke();
    }));
    REQUIRE(ticket);
    int                               calls = 0, depth = 0, maximum = 0, deferred = 0;
    NGIN::Utilities::Callable<void()> step;
    step = [&] {
        ++depth;
        maximum = std::max(maximum, depth);
        if (++calls < 100)
        {
            WorkItem work(step);
            if (!executor.TryDispatchInline(work))
            {
                ++deferred;
                pending = std::move(work);
                ticket->Schedule();
            }
        }
        --depth;
    };
    REQUIRE(executor.Execute(WorkItem(step)));
    scheduler.RunUntilIdle();
    REQUIRE(calls == 100);
    REQUIRE(maximum <= 16);
    REQUIRE(deferred > 0);
}

TEST_CASE("InlineScheduler requires explicit invocation and rejects queued submissions", "[Execution][Dispatch]")
{
    using namespace NGIN::Execution;
    InlineScheduler scheduler;
    auto            executor = ExecutorRef::From(scheduler);
    int             calls    = 0;
    REQUIRE(scheduler.Invoke(WorkItem([&] { ++calls; })));
    REQUIRE(calls == 1);
    REQUIRE_FALSE(executor.Execute([&] { ++calls; }));
    REQUIRE_FALSE(executor.ExecuteAt([&] { ++calls; }, NGIN::Time::TimePoint::FromNanoseconds(0)));
    REQUIRE(calls == 1);
    REQUIRE_FALSE(scheduler.RunOne());
}
