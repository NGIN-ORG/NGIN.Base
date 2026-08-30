#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <latch>
#include <thread>
#include <vector>

#include "../Support/FailureInjection.hpp"
#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Execution/WorkItem.hpp>
#include <NGIN/Units.hpp>

namespace
{
    class ManualTimerExecutor
    {
    public:
        NGIN::Execution::ScheduleResult Execute(NGIN::Execution::WorkItem item) noexcept
        {
            m_ready.push_back(std::move(item));
            return {};
        }

        NGIN::Execution::ScheduleResult ExecuteAt(NGIN::Execution::WorkItem item, NGIN::Time::TimePoint) noexcept
        {
            m_delayed.push_back(std::move(item));
            return {};
        }

        [[nodiscard]] bool RunOne() noexcept
        {
            if (m_ready.empty())
            {
                return false;
            }
            auto item = std::move(m_ready.back());
            m_ready.pop_back();
            item.Invoke();
            return true;
        }

        void RunUntilIdle() noexcept
        {
            while (RunOne()) {}
        }

        void RunAllDelayed() noexcept
        {
            for (auto& item: m_delayed)
            {
                (void) Execute(std::move(item));
            }
            m_delayed.clear();
        }

    private:
        std::vector<NGIN::Execution::WorkItem> m_ready;
        std::vector<NGIN::Execution::WorkItem> m_delayed;
    };

    NGIN::Async::Task<void> DelayForever(NGIN::Async::TaskContext& ctx)
    {
        co_await ctx.Delay(NGIN::Units::Seconds(60.0));
        co_return;
    }

    bool IncrementCallback(void* context) noexcept
    {
        std::atomic<int>* const count = static_cast<std::atomic<int>*>(context);
        count->fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    struct SelfResetContext final
    {
        NGIN::Async::CancellationRegistration* registration;
        std::atomic<int>*                      count;
    };

    bool SelfResetCallback(void* rawContext) noexcept
    {
        SelfResetContext* const context = static_cast<SelfResetContext*>(rawContext);
        context->count->fetch_add(1, std::memory_order_relaxed);
        context->registration->Reset();
        return false;
    }

    struct BlockingCallbackContext final
    {
        std::latch*       started;
        std::latch*       release;
        std::atomic<int>* count;
    };

    bool BlockingCallback(void* rawContext) noexcept
    {
        BlockingCallbackContext* const context = static_cast<BlockingCallbackContext*>(rawContext);
        context->count->fetch_add(1, std::memory_order_relaxed);
        context->started->count_down();
        context->release->wait();
        return false;
    }
}// namespace

TEST_CASE("CreateLinkedCancellationSource cancels when any input cancels")
{
    NGIN::Async::CancellationSource a;
    NGIN::Async::CancellationSource b;

    auto linked = NGIN::Async::CreateLinkedCancellationSource({a.GetToken(), b.GetToken()});
    REQUIRE_FALSE(linked.IsCancellationRequested());

    a.Cancel();
    REQUIRE(linked.IsCancellationRequested());
    REQUIRE(linked.GetToken().IsCancellationRequested());
}

TEST_CASE("Linked cancellation token wakes Delay")
{
    ManualTimerExecutor exec;

    NGIN::Async::CancellationSource a;
    NGIN::Async::CancellationSource b;
    auto                            linked = NGIN::Async::CreateLinkedCancellationSource({a.GetToken(), b.GetToken()});

    NGIN::Async::TaskContext ctx(exec, linked.GetToken());
    auto                     task = DelayForever(ctx);
    auto                     op   = NGIN::Async::Spawn(ctx, std::move(task));

    exec.RunUntilIdle();
    REQUIRE_FALSE(op.IsCompleted());

    a.Cancel();
    exec.RunUntilIdle();

    REQUIRE(op.IsCompleted());
    REQUIRE(op.IsCanceled());
    auto result = op.TakeResult();
    REQUIRE_FALSE(result);
    REQUIRE(result.IsCanceled());
}

TEST_CASE("CancelAfter schedules cancellation via executor")
{
    ManualTimerExecutor             exec;
    NGIN::Async::CancellationSource src;

    REQUIRE(src.CancelAfter(NGIN::Execution::ExecutorRef::From(exec), NGIN::Units::Milliseconds(1.0)));
    REQUIRE_FALSE(src.IsCancellationRequested());

    exec.RunAllDelayed();
    exec.RunUntilIdle();

    REQUIRE(src.IsCancellationRequested());
}

TEST_CASE("Cancellation registration remains stable when its handle moves")
{
    NGIN::Async::CancellationSource       source;
    NGIN::Async::CancellationRegistration original;
    std::atomic<int>                      callbacks {0};
    REQUIRE(source.GetToken().Register(original, {}, {}, &IncrementCallback, &callbacks));

    NGIN::Async::CancellationRegistration moved(std::move(original));
    CHECK_FALSE(original.IsValid());
    CHECK(moved.IsValid());
    source.Cancel();

    CHECK(callbacks.load(std::memory_order_relaxed) == 1);
    moved.Reset();
}

TEST_CASE("Cancellation callback can reset its own registration")
{
    NGIN::Async::CancellationSource       source;
    NGIN::Async::CancellationRegistration registration;
    std::atomic<int>                      callbacks {0};
    SelfResetContext                      context {&registration, &callbacks};
    REQUIRE(source.GetToken().Register(registration, {}, {}, &SelfResetCallback, &context));

    source.Cancel();
    source.Cancel();

    CHECK(callbacks.load(std::memory_order_relaxed) == 1);
    CHECK_FALSE(registration.IsValid());
}

TEST_CASE("Cancellation reset waits for an in-flight callback on another thread")
{
    NGIN::Async::CancellationSource       source;
    NGIN::Async::CancellationRegistration registration;
    std::atomic<int>                      callbacks {0};
    std::atomic<bool>                     resetCompleted {false};
    std::latch                            callbackStarted(1);
    std::latch                            releaseCallback(1);
    std::latch                            resetStarted(1);
    BlockingCallbackContext               context {&callbackStarted, &releaseCallback, &callbacks};
    REQUIRE(source.GetToken().Register(registration, {}, {}, &BlockingCallback, &context));

    std::thread cancelThread([&source] { source.Cancel(); });
    callbackStarted.wait();
    std::thread resetThread([&] {
        resetStarted.count_down();
        registration.Reset();
        resetCompleted.store(true, std::memory_order_release);
    });
    resetStarted.wait();
    CHECK_FALSE(resetCompleted.load(std::memory_order_acquire));

    releaseCallback.count_down();
    cancelThread.join();
    resetThread.join();

    CHECK(resetCompleted.load(std::memory_order_acquire));
    CHECK(callbacks.load(std::memory_order_relaxed) == 1);
    CHECK_FALSE(registration.IsValid());
}

TEST_CASE("Cancellation reset before firing prevents callback invocation")
{
    NGIN::Async::CancellationSource       source;
    NGIN::Async::CancellationRegistration registration;
    std::atomic<int>                      callbacks {0};
    REQUIRE(source.GetToken().Register(registration, {}, {}, &IncrementCallback, &callbacks));

    registration.Reset();
    source.Cancel();

    CHECK(callbacks.load(std::memory_order_relaxed) == 0);
}

TEST_CASE("Cancellation registration reports node allocation failure")
{
    NGIN::Tests::FailureMemoryResource    resource;
    NGIN::Async::CancellationSource       source(&resource);
    NGIN::Async::CancellationRegistration registration;
    std::atomic<int>                      callbacks {0};

    resource.FailNextAllocation();
    const NGIN::Async::CancellationRegistrationResult result =
            source.GetToken().Register(registration, {}, {}, &IncrementCallback, &callbacks);

    REQUIRE_FALSE(result);
    CHECK(result.error() == NGIN::Async::CancellationRegistrationError::ResourceExhausted);
    CHECK_FALSE(registration.IsValid());
    source.Cancel();
    CHECK(callbacks.load(std::memory_order_relaxed) == 0);
}
