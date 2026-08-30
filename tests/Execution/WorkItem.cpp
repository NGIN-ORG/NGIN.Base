/// @file WorkItem.cpp
/// @brief Tests for NGIN::Execution::WorkItem scheduling.

#include "../Support/FailureInjection.hpp"
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Execution/ExecutorRef.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/Execution/WorkItem.hpp>
#include <NGIN/Units.hpp>

#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <coroutine>
#include <thread>

using namespace std::chrono_literals;

namespace
{
    struct ResumeOnceCoroutine final
    {
        struct promise_type final
        {
            std::atomic<int>* counter {nullptr};

            ResumeOnceCoroutine get_return_object() noexcept
            {
                return ResumeOnceCoroutine {std::coroutine_handle<promise_type>::from_promise(*this)};
            }

            std::suspend_always initial_suspend() noexcept
            {
                return {};
            }

            std::suspend_always final_suspend() noexcept
            {
                return {};
            }

            void return_void() noexcept {}

            void unhandled_exception() noexcept
            {
                std::terminate();
            }
        };

        using handle_type = std::coroutine_handle<promise_type>;

        explicit ResumeOnceCoroutine(handle_type h) noexcept
            : handle(h)
        {
        }

        ResumeOnceCoroutine(ResumeOnceCoroutine&& other) noexcept
            : handle(other.handle)
        {
            other.handle = {};
        }

        ResumeOnceCoroutine(const ResumeOnceCoroutine&)            = delete;
        ResumeOnceCoroutine& operator=(const ResumeOnceCoroutine&) = delete;
        ResumeOnceCoroutine& operator=(ResumeOnceCoroutine&&)      = delete;

        ~ResumeOnceCoroutine()
        {
            if (handle)
            {
                handle.destroy();
            }
        }

        handle_type handle {};
    };

    ResumeOnceCoroutine MakeResumeOnce(std::atomic<int>& counter)
    {
        counter.fetch_add(1, std::memory_order_relaxed);
        co_return;
    }

    class LargeFailingJob final
    {
    public:
        LargeFailingJob(
                NGIN::Tests::FailureCountdown& failures,
                std::atomic<int>&              live,
                std::atomic<int>&              invocations) noexcept
            : m_failures(&failures), m_live(&live), m_invocations(&invocations)
        {
            m_live->fetch_add(1, std::memory_order_relaxed);
        }

        LargeFailingJob(const LargeFailingJob& other)
            : m_failures(other.m_failures), m_live(other.m_live), m_invocations(other.m_invocations)
        {
            m_failures->Hit();
            m_live->fetch_add(1, std::memory_order_relaxed);
        }

        LargeFailingJob(LargeFailingJob&& other)
            : m_failures(other.m_failures), m_live(other.m_live), m_invocations(other.m_invocations)
        {
            m_failures->Hit();
            m_live->fetch_add(1, std::memory_order_relaxed);
        }

        ~LargeFailingJob()
        {
            m_live->fetch_sub(1, std::memory_order_relaxed);
        }

        void operator()() noexcept
        {
            m_invocations->fetch_add(1, std::memory_order_relaxed);
        }

    private:
        std::array<std::byte, 128>     m_padding {};
        NGIN::Tests::FailureCountdown* m_failures;
        std::atomic<int>*              m_live;
        std::atomic<int>*              m_invocations;
    };

    class RejectingScheduler final
    {
    public:
        [[nodiscard]] NGIN::Execution::ScheduleResult Execute(NGIN::Execution::WorkItem) noexcept
        {
            return std::unexpected(NGIN::Execution::ScheduleError::Rejected);
        }

        [[nodiscard]] NGIN::Execution::ScheduleResult ExecuteAt(
                NGIN::Execution::WorkItem,
                NGIN::Time::TimePoint) noexcept
        {
            return std::unexpected(NGIN::Execution::ScheduleError::Stopped);
        }
    };
}// namespace

TEST_CASE("WorkItem executes a large lambda job through direct heap storage", "[Execution][WorkItem]")
{
    struct BigJob
    {
        std::array<std::byte, 512> padding {};
        std::atomic<int>*          counter {nullptr};

        void operator()() noexcept
        {
            counter->fetch_add(1, std::memory_order_relaxed);
        }
    };

    std::atomic<int> counter {0};
    auto             item = NGIN::Execution::WorkItem(BigJob {.counter = &counter});
    item.Invoke();
    REQUIRE(counter.load(std::memory_order_relaxed) == 1);
}

TEST_CASE("WorkItem executes an inline lambda job", "[Execution][WorkItem]")
{
    int  value = 0;
    auto item  = NGIN::Execution::WorkItem([&]() noexcept { value = 42; });
    item.Invoke();
    REQUIRE(value == 42);
}

TEST_CASE("WorkItem releases heap storage when callable construction throws", "[Execution][WorkItem]")
{
    NGIN::Tests::FailureCountdown failures;
    std::atomic<int>              live {0};
    std::atomic<int>              invocations {0};
    LargeFailingJob               source(failures, live, invocations);
    failures.Arm(0);

    CHECK_THROWS_AS(NGIN::Execution::WorkItem(source), std::runtime_error);
    failures.Disable();
    CHECK(live.load(std::memory_order_relaxed) == 1);
    CHECK(invocations.load(std::memory_order_relaxed) == 0);
}

TEST_CASE("WorkItem destroys each heap callable exactly once", "[Execution][WorkItem]")
{
    NGIN::Tests::FailureCountdown failures;
    std::atomic<int>              live {0};
    std::atomic<int>              invocations {0};
    failures.Disable();
    {
        LargeFailingJob           source(failures, live, invocations);
        NGIN::Execution::WorkItem first(source);
        NGIN::Execution::WorkItem second(std::move(first));
        second.Invoke();
        CHECK(live.load(std::memory_order_relaxed) == 2);
    }
    CHECK(live.load(std::memory_order_relaxed) == 0);
    CHECK(invocations.load(std::memory_order_relaxed) == 1);
}

TEST_CASE("WorkItem heap construction and destruction is independent across threads", "[Execution][WorkItem]")
{
    constexpr int                        threadCount   = 4;
    constexpr int                        jobsPerThread = 250;
    std::atomic<int>                     invocations {0};
    std::array<std::thread, threadCount> threads;

    for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex)
    {
        threads[threadIndex] = std::thread([&invocations] {
            for (int jobIndex = 0; jobIndex < jobsPerThread; ++jobIndex)
            {
                struct ConcurrentJob final
                {
                    std::array<std::byte, 128> padding {};
                    std::atomic<int>*          invocations;

                    void operator()() const noexcept
                    {
                        invocations->fetch_add(1, std::memory_order_relaxed);
                    }
                };

                NGIN::Execution::WorkItem item(ConcurrentJob {{}, &invocations});
                item.Invoke();
            }
        });
    }
    for (std::thread& thread: threads)
        thread.join();

    CHECK(invocations.load(std::memory_order_relaxed) == threadCount * jobsPerThread);
}

TEST_CASE("ThreadPoolScheduler executes a WorkItem job", "[Execution][ThreadPoolScheduler][WorkItem]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler(2);
    std::atomic<int>                     completed {0};

    REQUIRE(scheduler.Execute(NGIN::Execution::WorkItem(NGIN::Utilities::Callable<void()>([&] {
        completed.store(1, std::memory_order_release);
    }))));

    for (int i = 0; i < 200 && completed.load(std::memory_order_acquire) == 0; ++i)
    {
        std::this_thread::sleep_for(1ms);
    }

    REQUIRE(completed.load(std::memory_order_acquire) == 1);
}

TEST_CASE("ExecutorRef schedules a job on a scheduler", "[Execution][ExecutorRef][WorkItem]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler(2);
    const auto                           executor = NGIN::Execution::ExecutorRef::From(scheduler);

    std::atomic<int> completed {0};
    REQUIRE(executor.Execute(NGIN::Utilities::Callable<void()>([&] {
        completed.store(1, std::memory_order_release);
    })));

    for (int i = 0; i < 200 && completed.load(std::memory_order_acquire) == 0; ++i)
    {
        std::this_thread::sleep_for(1ms);
    }

    REQUIRE(completed.load(std::memory_order_acquire) == 1);
}

TEST_CASE("ExecutorRef ExecuteAfter(0) schedules a job immediately", "[Execution][ExecutorRef]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    const auto                            executor = NGIN::Execution::ExecutorRef::From(scheduler);

    std::atomic<int> completed {0};
    REQUIRE(executor.ExecuteAfter([&]() noexcept { completed.fetch_add(1, std::memory_order_relaxed); }, NGIN::Units::Nanoseconds(0.0)));

    REQUIRE(scheduler.RunOne());
    REQUIRE(completed.load(std::memory_order_relaxed) == 1);
}

TEST_CASE("WorkItem rejects an empty job", "[Execution][WorkItem]")
{
    NGIN::Utilities::Callable<void()> empty;
    REQUIRE_THROWS_AS(NGIN::Execution::WorkItem(std::move(empty)), std::invalid_argument);
}

TEST_CASE("WorkItem resumes a coroutine handle", "[Execution][WorkItem]")
{
    std::atomic<int> counter {0};
    auto             coro = MakeResumeOnce(counter);

    REQUIRE(counter.load(std::memory_order_relaxed) == 0);

    auto item = NGIN::Execution::WorkItem(std::coroutine_handle<>(coro.handle));
    item.Invoke();

    REQUIRE(counter.load(std::memory_order_relaxed) == 1);
    REQUIRE(coro.handle.done());
}

TEST_CASE("ExecutorRef ExecuteAt schedules a job for a specific timepoint", "[Execution][ExecutorRef]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    const auto                            executor = NGIN::Execution::ExecutorRef::From(scheduler);

    std::atomic<int> counter {0};
    REQUIRE(executor.ExecuteAt([&]() noexcept { counter.fetch_add(1, std::memory_order_relaxed); }, NGIN::Time::TimePoint::FromNanoseconds(10)));

    REQUIRE_FALSE(scheduler.RunOneAt(NGIN::Time::TimePoint::FromNanoseconds(9)));
    REQUIRE(counter.load(std::memory_order_relaxed) == 0);

    REQUIRE(scheduler.RunOneAt(NGIN::Time::TimePoint::FromNanoseconds(10)));
    REQUIRE(counter.load(std::memory_order_relaxed) == 1);
}

TEST_CASE("ExecutorRef reports invalid and rejected submissions", "[Execution][ExecutorRef]")
{
    NGIN::Execution::ExecutorRef    invalid;
    NGIN::Execution::ScheduleResult invalidResult = invalid.Execute(NGIN::Execution::WorkItem([]() noexcept {}));
    REQUIRE_FALSE(invalidResult);
    CHECK(invalidResult.error() == NGIN::Execution::ScheduleError::InvalidExecutor);

    RejectingScheduler              scheduler;
    NGIN::Execution::ExecutorRef    executor = NGIN::Execution::ExecutorRef::From(scheduler);
    NGIN::Execution::ScheduleResult rejected = executor.Execute(NGIN::Execution::WorkItem([]() noexcept {}));
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error() == NGIN::Execution::ScheduleError::Rejected);

    NGIN::Execution::ScheduleResult stopped = executor.ExecuteAt(
            NGIN::Execution::WorkItem([]() noexcept {}),
            NGIN::Time::TimePoint::FromNanoseconds(1));
    REQUIRE_FALSE(stopped);
    CHECK(stopped.error() == NGIN::Execution::ScheduleError::Stopped);
}
