#include <NGIN/Execution/detail/CompletionQueue.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <memory>

#include <atomic>
#include <latch>
#include <thread>
#include <vector>

#include "../Support/FailureInjection.hpp"
#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Execution/InlineScheduler.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/Execution/WorkItem.hpp>
#include <NGIN/Units.hpp>

namespace
{
    class ManualTimerExecutor
    {
    public:
        auto ReserveCompletion(NGIN::Execution::WorkItem item) noexcept
        {
            return m_completions.Reserve(std::move(item));
        }
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
            if (m_completions.RunOne())
                return true;
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
        NGIN::Execution::detail::CompletionQueue m_completions;
        std::vector<NGIN::Execution::WorkItem>   m_ready;
        std::vector<NGIN::Execution::WorkItem>   m_delayed;
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

    // A non-owning registered continuation needs a separately owned frame.
    struct ResumeProbe final
    {
        struct promise_type final
        {
            ResumeProbe get_return_object() noexcept
            {
                return ResumeProbe(std::coroutine_handle<promise_type>::from_promise(*this));
            }
            std::suspend_always initial_suspend() noexcept { return {}; }
            std::suspend_always final_suspend() noexcept { return {}; }
            void                return_void() noexcept {}
            void                unhandled_exception() noexcept { std::terminate(); }
        };

        explicit ResumeProbe(std::coroutine_handle<promise_type> coroutine) noexcept : handle(coroutine) {}
        ResumeProbe(const ResumeProbe&)            = delete;
        ResumeProbe& operator=(const ResumeProbe&) = delete;
        ~ResumeProbe() { handle.destroy(); }

        std::coroutine_handle<promise_type> handle;
    };

    ResumeProbe RecordResume(int& calls, std::thread::id& thread,
                             NGIN::Async::CancellationRegistration* registration = nullptr)
    {
        if (registration)
            registration->Reset();
        ++calls;
        thread = std::this_thread::get_id();
        co_return;
    }

    class CompletionOnlyExecutor final
    {
    public:
        explicit CompletionOnlyExecutor(std::pmr::memory_resource* resource = std::pmr::get_default_resource())
            : completions(1, nullptr, nullptr, resource)
        {
        }

        auto ReserveCompletion(NGIN::Execution::WorkItem item) noexcept
        {
            return completions.Reserve(std::move(item));
        }
        NGIN::Execution::ScheduleResult Execute(NGIN::Execution::WorkItem) noexcept
        {
            ++ordinarySubmissions;
            return std::unexpected(NGIN::Execution::ScheduleError::Stopped);
        }
        NGIN::Execution::ScheduleResult ExecuteAt(NGIN::Execution::WorkItem item, NGIN::Time::TimePoint) noexcept
        {
            return Execute(std::move(item));
        }

        NGIN::Execution::detail::CompletionQueue completions;
        std::atomic<int>                         ordinarySubmissions {0};
    };
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
TEST_CASE("Cancellation can destroy a registration owner while registration returns", "[Async][Lifetime]")
{
    for (int iteration = 0; iteration < 512; ++iteration)
    {
        NGIN::Async::CancellationSource source;
        auto                            registration = std::make_unique<NGIN::Async::CancellationRegistration>();
        std::thread                     canceler([&] { source.Cancel(); });
        const auto                      result = source.GetToken().Register(*registration, {}, {}, +[](void* owner) noexcept {
            static_cast<std::unique_ptr<NGIN::Async::CancellationRegistration>*>(owner)->reset();
            return false; }, &registration);
        canceler.join();
        REQUIRE(result);
        REQUIRE_FALSE(registration);
    }
}

TEST_CASE("Cancellation reserves continuation delivery before firing", "[Async][Affinity][Reservation]")
{
    NGIN::Tests::FailureMemoryResource    completionResource;
    NGIN::Tests::FailureMemoryResource    registrationResource;
    CompletionOnlyExecutor                scheduler(&completionResource);
    NGIN::Async::CancellationSource       source(&registrationResource);
    NGIN::Async::CancellationRegistration registration;
    int                                   resumes = 0;
    std::thread::id                       resumeThread;
    auto                                  probe = RecordResume(resumes, resumeThread);
    SECTION("cancellation follows registration") {}
    SECTION("cancellation precedes registration")
    {
        source.Cancel();
    }

    REQUIRE(source.GetToken().Register(registration, NGIN::Execution::ExecutorRef::From(scheduler), probe.handle));
    REQUIRE(scheduler.completions.Outstanding() == 1);
    scheduler.completions.Close();
    completionResource.FailNextAllocation();
    registrationResource.FailNextAllocation();
    std::thread canceler([&] { source.Cancel(); });
    canceler.join();
    registration.Reset();
    REQUIRE(resumes == 0);
    REQUIRE(scheduler.ordinarySubmissions == 0);
    REQUIRE(scheduler.completions.RunOne());
    REQUIRE(resumes == 1);
    REQUIRE(resumeThread == std::this_thread::get_id());
    REQUIRE_FALSE(scheduler.completions.RunOne());
    REQUIRE(scheduler.completions.Outstanding() == 0);
}

TEST_CASE("Cancellation releases unused continuation reservations", "[Async][Reservation]")
{
    CompletionOnlyExecutor                scheduler;
    NGIN::Async::CancellationSource       source;
    NGIN::Async::CancellationRegistration registration;
    std::atomic<int>                      callbacks {0};
    SelfResetContext                      selfReset {&registration, &callbacks};
    int                                   resumes = 0;
    std::thread::id                       resumeThread;
    auto                                  probe           = RecordResume(resumes, resumeThread);
    NGIN::Async::CancellationCallback     callback        = &IncrementCallback;
    void*                                 callbackContext = &callbacks;
    bool                                  unregister      = false;
    bool                                  shouldResume    = false;
    SECTION("unregister before cancellation")
    {
        unregister = true;
    }
    SECTION("callback declines continuation") {}
    SECTION("callback resets registration and declines continuation")
    {
        callback        = &SelfResetCallback;
        callbackContext = &selfReset;
    }
    SECTION("callback resets registration and requests continuation")
    {
        callback = +[](void* context) noexcept {
            (void) SelfResetCallback(context);
            return true;
        };
        callbackContext = &selfReset;
        shouldResume    = true;
    }
    REQUIRE(source.GetToken().Register(registration, NGIN::Execution::ExecutorRef::From(scheduler),
                                       probe.handle, callback, callbackContext));
    REQUIRE(scheduler.completions.Outstanding() == 1);
    if (unregister)
        registration.Reset();
    source.Cancel();
    source.Cancel();
    REQUIRE(callbacks == (unregister ? 0 : 1));
    REQUIRE(resumes == 0);
    REQUIRE(scheduler.completions.RunOne() == shouldResume);
    REQUIRE(resumes == (shouldResume ? 1 : 0));
    REQUIRE(scheduler.completions.Outstanding() == 0);
    REQUIRE(scheduler.ordinarySubmissions == 0);
    auto recovered = scheduler.ReserveCompletion(NGIN::Execution::WorkItem([] {}));
    REQUIRE(recovered);
}

TEST_CASE("Cancellation rejects unavailable continuation storage before publishing callbacks", "[Async][Reservation]")
{
    const bool alreadyCanceled = GENERATE(false, true);
    {
        NGIN::Tests::FailureMemoryResource     resource;
        CompletionOnlyExecutor                 scheduler(&resource);
        NGIN::Execution::InlineScheduler       inlineScheduler;
        NGIN::Async::CancellationSource        source;
        NGIN::Async::CancellationRegistration  registration;
        NGIN::Execution::CompletionReservation occupied;
        NGIN::Execution::ExecutorRef           executor = NGIN::Execution::ExecutorRef::From(scheduler);
        auto                                   error    = NGIN::Async::CancellationRegistrationError::CompletionUnavailable;
        std::atomic<int>                       callbacks {0};
        int                                    resumes = 0;
        std::thread::id                        resumeThread;
        auto                                   probe = RecordResume(resumes, resumeThread);
        if (alreadyCanceled)
            source.Cancel();
        SECTION("stopped executor")
        {
            scheduler.completions.Close();
        }
        SECTION("unsupported executor")
        {
            executor = NGIN::Execution::ExecutorRef::From(inlineScheduler);
        }
        SECTION("invalid executor")
        {
            executor = {};
            error    = NGIN::Async::CancellationRegistrationError::InvalidTarget;
        }
        SECTION("completion capacity exhausted")
        {
            auto reservation = scheduler.ReserveCompletion(NGIN::Execution::WorkItem([] {}));
            REQUIRE(reservation);
            occupied = std::move(*reservation);
            error    = NGIN::Async::CancellationRegistrationError::ResourceExhausted;
        }
        SECTION("completion allocation fails")
        {
            resource.FailNextAllocation();
            error = NGIN::Async::CancellationRegistrationError::ResourceExhausted;
        }
        auto result = source.GetToken().Register(registration, executor, probe.handle, &IncrementCallback, &callbacks);
        REQUIRE_FALSE(result);
        REQUIRE(result.error() == error);
        REQUIRE_FALSE(registration.IsValid());
        source.Cancel();
        REQUIRE(callbacks == 0);
        REQUIRE(resumes == 0);
        REQUIRE_FALSE(scheduler.completions.RunOne());
        REQUIRE(scheduler.ordinarySubmissions == 0);
        occupied.Reset();
        REQUIRE(scheduler.completions.Outstanding() == 0);
    }
}

TEST_CASE("Cancellation continuation admission is inert for an empty token", "[Async][Reservation]")
{
    CompletionOnlyExecutor                scheduler;
    NGIN::Async::CancellationRegistration registration;
    int                                   resumes = 0;
    std::thread::id                       resumeThread;
    auto                                  probe = RecordResume(resumes, resumeThread);
    scheduler.completions.Close();
    REQUIRE(NGIN::Async::CancellationToken {}.Register(
            registration, NGIN::Execution::ExecutorRef::From(scheduler), probe.handle));
    REQUIRE_FALSE(registration.IsValid());
    REQUIRE(scheduler.completions.Outstanding() == 0);
    REQUIRE(resumes == 0);
}

TEST_CASE("Concurrent cancellation and reset retire continuation storage exactly once", "[Async][Reservation][Lifetime]")
{
    for (int iteration = 0; iteration < 512; ++iteration)
    {
        CompletionOnlyExecutor                scheduler;
        NGIN::Async::CancellationSource       source;
        NGIN::Async::CancellationRegistration registration;
        int                                   resumes = 0;
        std::thread::id                       resumeThread;
        auto                                  probe = RecordResume(resumes, resumeThread);
        REQUIRE(source.GetToken().Register(registration, NGIN::Execution::ExecutorRef::From(scheduler), probe.handle));
        std::thread canceler([&] { source.Cancel(); });
        registration.Reset();
        canceler.join();
        REQUIRE(resumes == 0);
        const bool delivered = scheduler.completions.RunOne();
        REQUIRE(resumes == (delivered ? 1 : 0));
        REQUIRE_FALSE(scheduler.completions.RunOne());
        REQUIRE(scheduler.completions.Outstanding() == 0);
        REQUIRE(scheduler.ordinarySubmissions == 0);
    }
}

TEST_CASE("Cancellation delivers an admitted continuation while its thread pool drains", "[Async][Affinity][Reservation]")
{
    auto                                  scheduler = std::make_unique<NGIN::Execution::ThreadPoolScheduler>(1);
    auto                                  executor  = NGIN::Execution::ExecutorRef::From(*scheduler);
    NGIN::Async::CancellationSource       source;
    NGIN::Async::CancellationRegistration registration;
    int                                   resumes = 0;
    std::thread::id                       resumeThread;
    auto                                  probe = RecordResume(resumes, resumeThread, &registration);
    REQUIRE(source.GetToken().Register(registration, executor, probe.handle));
    std::thread shutdown([owner = std::move(scheduler)]() mutable { owner.reset(); });
    const auto  shutdownThread = shutdown.get_id();
    while (executor.Execute(NGIN::Execution::WorkItem([] {})))
        std::this_thread::yield();
    source.Cancel();
    shutdown.join();
    REQUIRE(resumes == 1);
    REQUIRE(resumeThread != std::this_thread::get_id());
    REQUIRE(resumeThread != shutdownThread);
}
