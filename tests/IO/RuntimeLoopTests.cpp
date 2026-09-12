#include <catch2/catch_test_macros.hpp>

#include "../../src/NGIN/IO/RuntimeLoop.hpp"

#include <NGIN/Async/Task.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <poll.h>
#include <unistd.h>
#endif

namespace
{
    using NGIN::Execution::ScheduleError;
    using NGIN::Execution::WorkItem;
    using NGIN::IO::detail::RuntimeLoop;
    using NGIN::IO::detail::RuntimePoller;
    using NGIN::Time::MonotonicClock;
    using NGIN::Time::TimePoint;

    TimePoint Future(std::uint64_t milliseconds)
    {
        return TimePoint::FromNanoseconds(MonotonicClock::Now().ToNanoseconds() + milliseconds * 1'000'000);
    }

    NGIN::Async::Task<void> LongDelay(NGIN::Async::TaskContext& context)
    {
        co_await context.Delay(NGIN::Units::Seconds(3600));
    }

    // Always release the driver before a failed assertion unwinds the runtime.
    class Driver final
    {
    public:
        explicit Driver(RuntimeLoop& runtime) : loop(runtime), thread([&runtime] { runtime.Run(); }) {}
        ~Driver()
        {
            loop.RequestStop();
            thread.join();
        }
        RuntimeLoop& loop;
        std::thread  thread;
    };

    struct ShutdownGuard final
    {
        RuntimeLoop& loop;
        ~ShutdownGuard() { loop.Shutdown(); }
    };

#if !defined(_WIN32)
    int HostWait(std::span<const NGIN::IO::NativeWaitSource> sources, int timeout)
    {
        std::vector<pollfd> descriptors;
        for (const auto& source: sources)
            descriptors.push_back({static_cast<int>(source.handle),
                                   static_cast<short>((source.interests & NGIN::IO::NativeWaitSource::Read ? POLLIN : 0) |
                                                      (source.interests & NGIN::IO::NativeWaitSource::Write ? POLLOUT : 0)),
                                   0});
        return ::poll(descriptors.data(), descriptors.size(), timeout);
    }

    class PipeHandler final : public RuntimeLoop::Handler
    {
    public:
        explicit PipeHandler(RuntimeLoop& runtime) : loop(runtime)
        {
            if (::pipe(descriptors) != 0)
                throw std::runtime_error("test pipe creation failed");
        }
        ~PipeHandler() override
        {
            ::close(descriptors[0]);
            ::close(descriptors[1]);
        }
        void Ready(const RuntimePoller::Event& event) noexcept override
        {
            correctIdentifier = event.identifier == identifier;
            correctAffinity   = loop.IsCurrent();
            ++ready;
            char byte;
            readSucceeded = ::read(descriptors[0], &byte, 1) == 1;
            loop.Unwatch(identifier);
        }
        void Stop() noexcept override
        {
            correctAffinity = loop.IsCurrent();
            ++stops;
            loop.Unwatch(identifier);
        }
        void          Signal() { REQUIRE(::write(descriptors[1], "x", 1) == 1); }
        RuntimeLoop&  loop;
        int           descriptors[2] {};
        std::uint64_t identifier {};
        int           ready {};
        int           stops {};
        bool          readSucceeded {false};
        bool          correctIdentifier {false};
        bool          correctAffinity {false};
    };
#endif
}// namespace

TEST_CASE("Runtime loop queues submissions and preserves bounded backlog", "[IO][Runtime][Executor]")
{
    RuntimeLoop loop({.submissionCapacity = 2, .completionCapacity = 1, .batchSize = 1});
    auto        executor = loop.GetExecutor();
    int         calls    = 0;
    bool        affinity = false;
    REQUIRE_FALSE(executor.IsCurrent());
    REQUIRE(executor.Execute([&] { affinity = executor.IsCurrent(); ++calls; }));
    REQUIRE(executor.Execute([&] { ++calls; }));
    auto rejected = executor.Execute([] {});
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error() == ScheduleError::ResourceExhausted);
    REQUIRE(calls == 0);
    auto terminal = executor.ReserveCompletion(WorkItem([&] { ++calls; }));
    REQUIRE(terminal);
    terminal->Dispatch();
    REQUIRE(loop.PollOnce());
    REQUIRE(calls == 1);
    REQUIRE(affinity);
    REQUIRE_FALSE(executor.IsCurrent());
    REQUIRE(loop.PollOnce());
    REQUIRE(calls == 2);
    REQUIRE_FALSE(loop.PollOnce());
    REQUIRE(calls == 3);
    REQUIRE(executor.Execute([] {}));
    loop.Shutdown();
    REQUIRE(loop.GetState() == RuntimeLoop::State::Stopped);
    REQUIRE(executor.Execute([] {}).error() == ScheduleError::Stopped);
}

TEST_CASE("Runtime loop rotates submissions completions and timers across tiny batches", "[IO][Runtime][Fairness]")
{
    RuntimeLoop loop({.batchSize = 1});
    auto        executor    = loop.GetExecutor();
    int         submissions = 0;
    int         completions = 0;
    int         timers      = 0;
    for (int index = 0; index < 8; ++index)
    {
        REQUIRE(executor.Execute([&] { ++submissions; }));
        REQUIRE(executor.ExecuteAt([&] { ++timers; }, MonotonicClock::Now()));
        auto ticket = executor.ReserveCompletion(WorkItem([&] { ++completions; }));
        REQUIRE(ticket);
        ticket->Dispatch();
    }
    for (int index = 0; index < 3; ++index)
        REQUIRE(loop.PollOnce());
    REQUIRE(submissions == 1);
    REQUIRE(completions == 1);
    REQUIRE(timers == 1);
    for (int index = 3; index < 24; ++index)
        REQUIRE(loop.PollOnce() == (index != 23));
    REQUIRE(submissions == 8);
    REQUIRE(completions == 8);
    REQUIRE(timers == 8);
}

TEST_CASE("Runtime loop binds first drive ownership and rejects reentrancy", "[IO][Runtime][Ownership]")
{
    RuntimeLoop loop({});
    loop.PollOnce();
    bool        wrongThreadRejected = false;
    bool        shutdownRejected    = false;
    std::thread other([&] {
        try
        {
            loop.PollOnce();
        } catch (const std::logic_error&)
        {
            wrongThreadRejected = true;
        }
        try
        {
            loop.Shutdown();
        } catch (const std::logic_error&)
        {
            shutdownRejected = true;
        }
    });
    other.join();
    REQUIRE(wrongThreadRejected);
    REQUIRE(shutdownRejected);
    REQUIRE(loop.GetState() == RuntimeLoop::State::Running);
    bool reentrantRejected = false;
    bool blockingRejected  = false;
    REQUIRE(loop.GetExecutor().Execute([&] {
        try
        {
            loop.PollOnce();
        } catch (const std::logic_error&)
        {
            reentrantRejected = true;
        }
        try
        {
            loop.Shutdown();
        } catch (const std::logic_error&)
        {
            blockingRejected = true;
        }
        loop.RequestStop();
    }));
    loop.Run();
    REQUIRE(reentrantRejected);
    REQUIRE(blockingRejected);
    std::thread stoppedShutdown([&] { loop.Shutdown(); });
    stoppedShutdown.join();
}

TEST_CASE("Runtime loop accepts reserved completion after stop and drains on its owner", "[IO][Runtime][Shutdown]")
{
    RuntimeLoop     loop({});
    auto            executor         = loop.GetExecutor();
    bool            terminalRan      = false;
    bool            terminalAffinity = false;
    std::thread::id terminalThread;
    auto            ticket = executor.ReserveCompletion(WorkItem([&] {
        terminalRan      = true;
        terminalAffinity = executor.IsCurrent();
        terminalThread   = std::this_thread::get_id();
    }));
    REQUIRE(ticket);
    loop.RequestStop();
    REQUIRE(loop.GetState() == RuntimeLoop::State::Stopping);
    REQUIRE_FALSE(loop.PollOnce());
    REQUIRE(loop.GetState() == RuntimeLoop::State::Stopping);
    REQUIRE(executor.ReserveCompletion(WorkItem([] {})).error() == ScheduleError::Stopped);
    std::thread publisher([&] { ticket->Dispatch(); });
    publisher.join();
    REQUIRE_FALSE(terminalRan);
    loop.Shutdown();
    REQUIRE(terminalRan);
    REQUIRE(terminalAffinity);
    REQUIRE(terminalThread == std::this_thread::get_id());
}

TEST_CASE("Runtime loop shutdown waits for the committed driver without taking ownership", "[IO][Runtime][Shutdown]")
{
    RuntimeLoop                   loop({});
    auto                          executor = loop.GetExecutor();
    std::promise<std::thread::id> started;
    auto                          driverId = started.get_future();
    REQUIRE(executor.Execute([&] { started.set_value(std::this_thread::get_id()); }));
    std::thread::id completionThread;
    Driver          driver(loop);
    auto            ticket = executor.ReserveCompletion(WorkItem([&] { completionThread = std::this_thread::get_id(); }));
    REQUIRE(ticket);
    REQUIRE(driverId.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    const auto         expectedThread = driverId.get();
    std::promise<void> stopped;
    auto               result = stopped.get_future();
    std::thread        shutdown([&] {
        try
        {
            loop.Shutdown();
            stopped.set_value();
        } catch (...)
        {
            stopped.set_exception(std::current_exception());
        }
    });
    const bool         waited = result.wait_for(std::chrono::milliseconds(20)) == std::future_status::timeout;
    ticket->Dispatch();
    shutdown.join();
    REQUIRE(waited);
    REQUIRE_NOTHROW(result.get());
    REQUIRE(completionThread == expectedThread);
    REQUIRE(loop.GetState() == RuntimeLoop::State::Stopped);
}

TEST_CASE("Runtime loop removes canceled timer storage and recovers capacity immediately", "[IO][Runtime][Timer]")
{
    RuntimeLoop loop({.timerCapacity = 1});
    int         calls = 0;
    for (int iteration = 0; iteration < 256; ++iteration)
    {
        auto               lifetime = std::make_shared<int>(42);
        std::weak_ptr<int> retained = lifetime;
        auto               timer    = loop.ScheduleTimer(WorkItem([lifetime, &calls] { ++calls; }), Future(3'600'000));
        lifetime.reset();
        REQUIRE(timer);
        REQUIRE_FALSE(retained.expired());
        auto rejected = loop.GetExecutor().ExecuteAt([] {}, Future(3'600'000));
        REQUIRE(rejected.error() == ScheduleError::ResourceExhausted);
        REQUIRE(timer->Cancel());
        REQUIRE(retained.expired());
        REQUIRE_FALSE(timer->Cancel());
        REQUIRE_FALSE(loop.NextDeadline());
    }
    REQUIRE(calls == 0);
    REQUIRE_FALSE(loop.PollOnce());
}

TEST_CASE("Runtime timer ownership moves and removes undispatched work on destruction", "[IO][Runtime][Timer]")
{
    RuntimeLoop loop({.timerCapacity = 1});
    bool        invoked = false;
    {
        auto timer = loop.GetExecutor().ScheduleTimer(WorkItem([&] { invoked = true; }), Future(3'600'000));
        REQUIRE(timer);
        auto moved = std::move(*timer);
        REQUIRE_FALSE(timer->Cancel());
        REQUIRE(loop.NextDeadline());
    }
    REQUIRE_FALSE(loop.NextDeadline());
    REQUIRE_FALSE(invoked);
    REQUIRE(loop.GetExecutor().ExecuteAt([&] { invoked = true; }, MonotonicClock::Now()));
    REQUIRE_FALSE(loop.PollOnce());
    REQUIRE(invoked);
}

TEST_CASE("Runtime loop wakes for an earlier timer deadline", "[IO][Runtime][Wakeup]")
{
    RuntimeLoop        loop({});
    std::promise<void> entered;
    auto               running = entered.get_future();
    std::promise<void> fired;
    auto               ready = fired.get_future();
    REQUIRE(loop.GetExecutor().Execute([&] { entered.set_value(); }));
    REQUIRE(loop.GetExecutor().ExecuteAt([] {}, Future(3'600'000)));
    Driver driver(loop);
    REQUIRE(running.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    REQUIRE(loop.GetExecutor().ExecuteAt([&] { fired.set_value(); }, Future(10)));
    REQUIRE(ready.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
}

TEST_CASE("Runtime delay cancellation removes its long-duration timer immediately", "[IO][Runtime][Timer]")
{
    RuntimeLoop loop({.timerCapacity = 1});
    for (int iteration = 0; iteration < 128; ++iteration)
    {
        NGIN::Async::CancellationSource cancellation;
        NGIN::Async::TaskContext        context(loop.GetExecutor(), cancellation.GetToken());
        auto                            operation = NGIN::Async::Spawn(context, LongDelay(context));
        REQUIRE_FALSE(loop.PollOnce());
        REQUIRE(loop.NextDeadline());
        REQUIRE_FALSE(operation.IsCompleted());
        cancellation.Cancel();
        REQUIRE_FALSE(loop.NextDeadline());
        REQUIRE_FALSE(loop.PollOnce());
        REQUIRE(operation.IsCanceled());
    }
}

TEST_CASE("Runtime shutdown terminates a pending delay without an explicit cancellation token", "[IO][Runtime][Timer]")
{
    RuntimeLoop              loop({});
    NGIN::Async::TaskContext context(loop.GetExecutor());
    auto                     operation = NGIN::Async::Spawn(context, LongDelay(context));
    REQUIRE_FALSE(loop.PollOnce());
    REQUIRE_FALSE(operation.IsCompleted());
    loop.Shutdown();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operation.IsFaulted());
    auto result = operation.TakeResult();
    REQUIRE(result.Fault().code == NGIN::Async::AsyncFaultCode::SchedulerDispatchFailed);
    REQUIRE(result.Fault().native == static_cast<int>(ScheduleError::Stopped));
}

TEST_CASE("Runtime timer installation racing cancellation reaches one terminal outcome", "[IO][Runtime][Timer]")
{
    RuntimeLoop loop({});
    Driver      driver(loop);
    for (int iteration = 0; iteration < 256; ++iteration)
    {
        NGIN::Async::CancellationSource cancellation;
        NGIN::Async::TaskContext        context(loop.GetExecutor(), cancellation.GetToken());
        auto                            operation = NGIN::Async::Spawn(context, LongDelay(context));
        cancellation.Cancel();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!operation.IsCompleted() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        REQUIRE(operation.IsCanceled());
    }
}

TEST_CASE("Runtime loop does not lose submissions racing entry into platform wait", "[IO][Runtime][Wakeup]")
{
    RuntimeLoop      loop({});
    std::atomic<int> completed {0};
    Driver           driver(loop);
    for (int iteration = 1; iteration <= 2000; ++iteration)
    {
        REQUIRE(loop.GetExecutor().Execute([&] { completed.fetch_add(1, std::memory_order_release); }));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (completed.load(std::memory_order_acquire) != iteration && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        REQUIRE(completed.load(std::memory_order_acquire) == iteration);
    }
}

TEST_CASE("Runtime loop rejects invalid capacities and empty submissions", "[IO][Runtime][Admission]")
{
    REQUIRE_THROWS_AS(RuntimeLoop({.submissionCapacity = 0}), std::invalid_argument);
    REQUIRE_THROWS_AS(RuntimeLoop({.timerCapacity = 0}), std::invalid_argument);
    REQUIRE_THROWS_AS(RuntimeLoop({.completionCapacity = 0}), std::invalid_argument);
    REQUIRE_THROWS_AS(RuntimeLoop({.registrationCapacity = 0}), std::invalid_argument);
    REQUIRE_THROWS_AS(RuntimeLoop({.batchSize = 0}), std::invalid_argument);
    RuntimeLoop loop({});
    REQUIRE(loop.Execute({}).error() == ScheduleError::Rejected);
    REQUIRE(loop.ExecuteAt({}, Future(1)).error() == ScheduleError::Rejected);
}

#if !defined(_WIN32)
TEST_CASE("Runtime loop ignores cached readiness from a retired generation", "[IO][Runtime][Readiness]")
{
    RuntimeLoop loop({.batchSize = 1});
    auto        first  = std::make_shared<PipeHandler>(loop);
    auto        second = std::make_shared<PipeHandler>(loop);
    for (const auto& handler: {first, second})
    {
        auto watched = loop.Watch(handler->descriptors[0], RuntimePoller::Read, handler);
        REQUIRE(watched);
        handler->identifier = *watched;
        handler->Signal();
    }
    REQUIRE(loop.PollOnce());
    REQUIRE(first->ready + second->ready == 1);
    auto                pending       = first->ready ? second : first;
    const std::uint64_t oldIdentifier = pending->identifier;
    loop.Unwatch(oldIdentifier);
    auto replacement = loop.Watch(pending->descriptors[0], RuntimePoller::Read, pending);
    REQUIRE(replacement);
    REQUIRE(*replacement != oldIdentifier);
    pending->identifier = *replacement;
    REQUIRE(loop.PollOnce());// Consume the already returned, now stale event.
    REQUIRE(pending->ready == 0);
    REQUIRE_FALSE(loop.PollOnce());
    REQUIRE(pending->ready == 1);
    REQUIRE(pending->correctIdentifier);
}

TEST_CASE("Runtime loop retains readiness beyond a full native event batch", "[IO][Runtime][Readiness]")
{
    RuntimeLoop                               loop({.registrationCapacity = 96, .batchSize = 7});
    std::vector<std::shared_ptr<PipeHandler>> handlers;
    for (int index = 0; index < 96; ++index)
    {
        auto handler = std::make_shared<PipeHandler>(loop);
        auto watched = loop.Watch(handler->descriptors[0], RuntimePoller::Read, handler);
        REQUIRE(watched);
        handler->identifier = *watched;
        handler->Signal();
        handlers.push_back(handler);
    }
    int turns = 0;
    while (loop.PollOnce() && ++turns < 100) {}
    REQUIRE(turns < 100);
    for (const auto& handler: handlers)
    {
        REQUIRE(handler->ready == 1);
        REQUIRE(handler->correctIdentifier);
        REQUIRE(handler->correctAffinity);
        REQUIRE(handler->readSucceeded);
    }
}

TEST_CASE("Runtime loop rejects duplicate and excess registrations and retires them on stop", "[IO][Runtime][Readiness]")
{
    RuntimeLoop loop({.registrationCapacity = 1, .batchSize = 1});
    auto        first   = std::make_shared<PipeHandler>(loop);
    auto        second  = std::make_shared<PipeHandler>(loop);
    auto        watched = loop.Watch(first->descriptors[0], RuntimePoller::Read, first);
    REQUIRE(watched);
    first->identifier = *watched;
    REQUIRE(loop.Watch(first->descriptors[0], RuntimePoller::Read, first).error() == std::errc::device_or_resource_busy);
    REQUIRE(loop.Watch(second->descriptors[0], RuntimePoller::Read, second).error() == std::errc::no_buffer_space);
    loop.Shutdown();
    REQUIRE(first->stops == 1);
    REQUIRE(first->correctAffinity);
    REQUIRE(first->ready == 0);
    REQUIRE(loop.Watch(second->descriptors[0], RuntimePoller::Read, second).error() == std::errc::operation_canceled);
}

TEST_CASE("Runtime loop reserves admission without native monitoring and arms on demand", "[IO][Runtime][Readiness]")
{
    RuntimeLoop   loop({.registrationCapacity = 1});
    auto          first  = std::make_shared<PipeHandler>(loop);
    auto          second = std::make_shared<PipeHandler>(loop);
    ShutdownGuard cleanup {loop};
    auto          reserved = loop.ReserveWatch(first->descriptors[0], first);
    REQUIRE(reserved);
    first->identifier = *reserved;
    REQUIRE(loop.ReserveWatch(first->descriptors[0], first).error() == std::errc::device_or_resource_busy);
    REQUIRE(loop.Watch(second->descriptors[0], RuntimePoller::Read, second).error() == std::errc::no_buffer_space);
    first->Signal();
    for (int attempt = 0; attempt < 4; ++attempt)
        loop.PollOnce();
    REQUIRE(first->ready == 0);

    SECTION("arm delivers already ready data")
    {
        REQUIRE_FALSE(loop.Modify(first->identifier, RuntimePoller::Read));
        for (int attempt = 0; attempt < 4 && first->ready == 0; ++attempt)
            loop.PollOnce();
        REQUIRE(first->ready == 1);
        REQUIRE(first->correctIdentifier);
        REQUIRE(first->correctAffinity);
        REQUIRE(first->readSucceeded);
    }
    SECTION("failed arm preserves admission and can recover")
    {
        REQUIRE(loop.Modify(first->identifier, 0) == std::errc::invalid_argument);
        REQUIRE(loop.ReserveWatch(second->descriptors[0], second).error() == std::errc::no_buffer_space);
        REQUIRE_FALSE(loop.Modify(first->identifier, RuntimePoller::Read));
        for (int attempt = 0; attempt < 4 && first->ready == 0; ++attempt)
            loop.PollOnce();
        REQUIRE(first->ready == 1);
        REQUIRE(first->readSucceeded);
    }
    SECTION("unarmed retirement releases capacity")
    {
        loop.Unwatch(first->identifier);
    }
    SECTION("shutdown retires unarmed ownership")
    {
        loop.Shutdown();
        REQUIRE(first->stops == 1);
        REQUIRE(first->correctAffinity);
        REQUIRE(first->ready == 0);
        REQUIRE(loop.ReserveWatch(second->descriptors[0], second).error() == std::errc::operation_canceled);
        return;
    }
    auto reused = loop.ReserveWatch(second->descriptors[0], second);
    REQUIRE(reused);
    second->identifier = *reused;
    REQUIRE(second->identifier != first->identifier);
    loop.Shutdown();
    REQUIRE(second->stops == 1);
}

TEST_CASE("Runtime host wakes when the final unarmed registration retires off owner", "[IO][Runtime][Host]")
{
    struct Handler final : RuntimeLoop::Handler
    {
        bool stopped = false;
        void Ready(const RuntimePoller::Event&) noexcept override {}
        void Stop() noexcept override { stopped = true; }
    };
    RuntimeLoop   loop({});
    auto          pipe    = std::make_shared<PipeHandler>(loop);
    auto          handler = std::make_shared<Handler>();
    ShutdownGuard cleanup {loop};
    struct Retirement final
    {
        RuntimeLoop&  loop;
        std::uint64_t identifier {};
        ~Retirement() { loop.Unwatch(identifier); }
    } retirement {loop};
    auto reserved = loop.ReserveWatch(pipe->descriptors[0], handler);
    REQUIRE(reserved);
    retirement.identifier = *reserved;
    loop.RequestStop();
    while (loop.PollOnce()) {}
    REQUIRE(handler->stopped);
    REQUIRE(loop.GetState() == RuntimeLoop::State::Stopping);
    std::array<NGIN::IO::NativeWaitSource, 1> sources;
    auto                                      copied = loop.CopyNativeWaitSources(sources);
    REQUIRE(copied);
    REQUIRE(*copied == 1);
    REQUIRE(HostWait(sources, 0) == 0);
    std::thread producer([&] { loop.Unwatch(*reserved); });
    producer.join();
    REQUIRE(HostWait(sources, 1000) > 0);
    loop.Shutdown();
    REQUIRE(loop.GetState() == RuntimeLoop::State::Stopped);
}

TEST_CASE("Runtime loop host descriptor signals submissions and changed timer deadlines", "[IO][Runtime][Host]")
{
    RuntimeLoop                               loop({});
    std::array<NGIN::IO::NativeWaitSource, 1> sources;
    auto                                      copied = loop.CopyNativeWaitSources(sources);
    REQUIRE(copied);
    REQUIRE(*copied == 1);
    REQUIRE(HostWait(sources, 0) == 0);
    bool          ran       = false;
    bool          submitted = false;
    bool          timerRan  = false;
    ShutdownGuard cleanup {loop};
    std::thread   producer([&] { submitted = loop.GetExecutor().Execute([&] { ran = loop.IsCurrent(); }).has_value(); });
    producer.join();
    REQUIRE(submitted);
    REQUIRE(HostWait(sources, 1000) > 0);
    REQUIRE_FALSE(loop.PollOnce());
    REQUIRE(ran);
    REQUIRE(HostWait(sources, 0) == 0);
    REQUIRE(loop.GetExecutor().ExecuteAt([] {}, Future(3'600'000)));
    REQUIRE(HostWait(sources, 1000) > 0);
    loop.PollOnce();
    REQUIRE(loop.NextDeadline());
    const auto earlier = Future(20);
    REQUIRE(loop.GetExecutor().ExecuteAt([&] { timerRan = loop.IsCurrent(); }, earlier));
    REQUIRE(loop.NextDeadline() == earlier);
    REQUIRE(HostWait(sources, 1000) > 0);
    while (loop.PollOnce()) {}
    if (!timerRan)
    {
        const auto now   = MonotonicClock::Now().ToNanoseconds();
        const auto nanos = earlier.ToNanoseconds() > now ? earlier.ToNanoseconds() - now : 0;
        // A host timer expires even when no descriptor is signaled.
        REQUIRE(HostWait(sources, static_cast<int>(nanos / 1'000'000 + (nanos % 1'000'000 != 0))) == 0);
        while (loop.PollOnce()) {}
    }
    REQUIRE(timerRan);
    loop.Shutdown();
}

TEST_CASE("Runtime host snapshots include native readiness and refresh after registration changes", "[IO][Runtime][Host]")
{
    RuntimeLoop                               loop({.registrationCapacity = 2});
    std::array<NGIN::IO::NativeWaitSource, 3> sources;
    auto                                      copied = loop.CopyNativeWaitSources(sources);
    REQUIRE(copied);
    const auto oldSources = sources;
    const auto oldCount   = *copied;
    auto       handler    = std::make_shared<PipeHandler>(loop);
    auto       registered = loop.Watch(handler->descriptors[0], RuntimePoller::Read, handler);
    REQUIRE(registered);
    handler->identifier = *registered;
    REQUIRE(HostWait(std::span(oldSources).first(oldCount), 1000) > 0);
    while (loop.PollOnce()) {}
    copied = loop.CopyNativeWaitSources(sources);
    REQUIRE(copied);
    REQUIRE(*copied == (loop.NativeHandle() < 0 ? 2 : 1));
    REQUIRE(HostWait(std::span(sources).first(*copied), 0) == 0);
    handler->Signal();
    REQUIRE(HostWait(std::span(sources).first(*copied), 1000) > 0);
    while (loop.PollOnce()) {}
    REQUIRE(handler->ready == 1);
    REQUIRE(handler->correctAffinity);
    REQUIRE(handler->readSucceeded);
    copied = loop.CopyNativeWaitSources(sources);
    REQUIRE(copied);
    REQUIRE(*copied == 1);
    REQUIRE(HostWait(std::span(sources).first(*copied), 0) == 0);
    loop.Shutdown();
}
#endif

TEST_CASE("Runtime host snapshot rejects unsupported or insufficient storage without partial output", "[IO][Runtime][Host][Admission]")
{
    RuntimeLoop                               loop({});
    std::array<NGIN::IO::NativeWaitSource, 1> sources {{{-42, 99}}};
#if defined(_WIN32)
    auto copied = loop.CopyNativeWaitSources(sources);
    REQUIRE_FALSE(copied);
    REQUIRE(copied.error() == std::errc::operation_not_supported);
#else
    auto copied = loop.CopyNativeWaitSources({});
    REQUIRE_FALSE(copied);
    REQUIRE(copied.error() == std::errc::no_buffer_space);
    if (loop.NativeHandle() < 0)
    {
        auto handler    = std::make_shared<PipeHandler>(loop);
        auto registered = loop.Watch(handler->descriptors[0], RuntimePoller::Read, handler);
        REQUIRE(registered);
        handler->identifier = *registered;
        copied              = loop.CopyNativeWaitSources(sources);
        REQUIRE_FALSE(copied);
        REQUIRE(copied.error() == std::errc::no_buffer_space);
    }
#endif
    REQUIRE(sources[0].handle == -42);
    REQUIRE(sources[0].interests == 99);
    loop.Shutdown();
}

#if !defined(_WIN32)
TEST_CASE("Runtime loop rejects IOCP requests without retaining registration capacity", "[IO][Runtime][Admission]")
{
    RuntimeLoop loop({.registrationCapacity = 1});
    auto        handler = std::make_shared<PipeHandler>(loop);
    int         operation {};
    REQUIRE(loop.WatchCompletion(handler->descriptors[0], nullptr, handler).error() == std::errc::invalid_argument);
    REQUIRE(loop.WatchCompletion(handler->descriptors[0], &operation, handler).error() == std::errc::operation_not_supported);
    auto watched = loop.Watch(handler->descriptors[0], RuntimePoller::Read, handler);
    REQUIRE(watched);
    handler->identifier = *watched;
    handler->Signal();
    loop.PollOnce();
    REQUIRE(handler->ready == 1);
    loop.Shutdown();
}
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace
{
    struct CompletionFile final
    {
        CompletionFile()
        {
            std::array<wchar_t, MAX_PATH + 1> directory {}, path {};
            if (!GetTempPathW(static_cast<DWORD>(directory.size()), directory.data()) ||
                !GetTempFileNameW(directory.data(), L"ngi", 0, path.data()))
                throw std::runtime_error("create IOCP test file");
            handle = CreateFileW(path.data(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
            {
                DeleteFileW(path.data());
                throw std::runtime_error("open IOCP test file");
            }
        }
        ~CompletionFile() { CloseHandle(handle); }
        HANDLE handle {INVALID_HANDLE_VALUE};
    };

    struct CompletionHandler final : RuntimeLoop::Handler
    {
        explicit CompletionHandler(RuntimeLoop& loop) : loop(loop) {}
        void Ready(const RuntimePoller::Event& event) noexcept override
        {
            ++ready;
            correct = loop.IsCurrent() && event.identifier == identifier && event.operation == &operation && event.error == 0;
            bytes   = event.bytes;
            loop.Unwatch(identifier);
        }
        void Stop() noexcept override
        {
            ++stops;
            loop.Unwatch(identifier);
        }
        RuntimeLoop&  loop;
        OVERLAPPED    operation {};
        std::uint64_t identifier {};
        unsigned      ready {}, stops {}, bytes {};
        bool          correct {false};
    };
}// namespace

TEST_CASE("Runtime IOCP routes bounded batches of requests sharing one OS handle", "[IO][Runtime][IOCP]")
{
    CompletionFile                                  file;
    RuntimeLoop                                     loop({.registrationCapacity = 96, .batchSize = 7});
    std::vector<std::shared_ptr<CompletionHandler>> handlers;
    for (unsigned i = 0; i != 96; ++i)
    {
        auto handler    = std::make_shared<CompletionHandler>(loop);
        auto registered = loop.WatchCompletion(reinterpret_cast<std::uintptr_t>(file.handle), &handler->operation, handler);
        REQUIRE(registered);
        handler->identifier = *registered;
        REQUIRE(PostQueuedCompletionStatus(reinterpret_cast<HANDLE>(loop.NativeHandle()), i, 0, &handler->operation));
        handlers.push_back(std::move(handler));
    }
    unsigned rounds = 0;
    while (loop.PollOnce())
        REQUIRE(++rounds < 100);
    REQUIRE(rounds > 1);
    for (unsigned i = 0; i != handlers.size(); ++i)
    {
        REQUIRE(handlers[i]->ready == 1);
        REQUIRE(handlers[i]->bytes == i);
        REQUIRE(handlers[i]->correct);
    }
    // Reusing both the live file handle and completed OVERLAPPED must create a
    // new runtime generation despite Windows retaining the handle association.
    auto&      again      = handlers.front();
    const auto old        = again->identifier;
    auto       registered = loop.WatchCompletion(reinterpret_cast<std::uintptr_t>(file.handle), &again->operation, again);
    REQUIRE(registered);
    REQUIRE(*registered != old);
    again->identifier = *registered;
    REQUIRE(PostQueuedCompletionStatus(reinterpret_cast<HANDLE>(loop.NativeHandle()), 123, 0, &again->operation));
    while (loop.PollOnce()) {}
    REQUIRE(again->ready == 2);
    REQUIRE(again->bytes == 123);
    REQUIRE(again->correct);
    loop.Shutdown();
}

TEST_CASE("Runtime IOCP registration bounds and shutdown preserve existing handlers", "[IO][Runtime][IOCP]")
{
    CompletionFile file;
    RuntimeLoop    loop({.registrationCapacity = 1});
    auto           first  = std::make_shared<CompletionHandler>(loop);
    auto           second = std::make_shared<CompletionHandler>(loop);
    const auto     native = reinterpret_cast<std::uintptr_t>(file.handle);
    REQUIRE(loop.WatchCompletion(native, nullptr, first).error() == std::errc::invalid_argument);
    REQUIRE_FALSE(loop.WatchCompletion(reinterpret_cast<std::uintptr_t>(INVALID_HANDLE_VALUE), &first->operation, first));
    auto registered = loop.WatchCompletion(native, &first->operation, first);
    REQUIRE(registered);
    first->identifier = *registered;
    REQUIRE(loop.WatchCompletion(native, &first->operation, first).error() == std::errc::device_or_resource_busy);
    REQUIRE(loop.WatchCompletion(native, &second->operation, second).error() == std::errc::no_buffer_space);
    REQUIRE(loop.Modify(first->identifier, RuntimePoller::Read) == std::errc::operation_not_supported);
    loop.RequestStop();
    REQUIRE(loop.WatchCompletion(native, &second->operation, second).error() == std::errc::operation_canceled);
    loop.Shutdown();
    REQUIRE(first->stops == 1);
    REQUIRE(first->ready == 0);
}
#endif
