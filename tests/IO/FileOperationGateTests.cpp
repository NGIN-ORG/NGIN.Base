#include "../../src/NGIN/IO/FileOperationGate.hpp"
#include "../../src/NGIN/IO/FileSystemDriver.hpp"
#include "../Support/FailureInjection.hpp"
#include <limits>

#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <barrier>
#include <thread>

namespace
{
    using Gate   = NGIN::IO::detail::FileOperationGate;
    using Kind   = Gate::Kind;
    using Status = Gate::Status;

    struct FileFixture final
    {
        explicit FileFixture(NGIN::UInt32 capacity = 1024)
            : runtime({.files = {.queueDepthHint    = capacity,
                                 .backendPreference = NGIN::IO::Runtime::FileBackendPreference::Fallback}}),
              driver(NGIN::IO::detail::AcquireFileSystemDriver(runtime)), gate(*driver) {}
        NGIN::IO::Runtime                                   runtime;
        std::shared_ptr<NGIN::IO::detail::FileSystemDriver> driver;
        Gate                                                gate;
    };

    struct Pending final
    {
        bool                    entered {false};
        bool                    close {false};
        NGIN::Async::AsyncFault fault;
        std::coroutine_handle<> continuation {};
        bool                    await_ready() const noexcept { return false; }
        void                    await_suspend(std::coroutine_handle<> handle) noexcept { continuation = handle; }
        void                    await_resume() const noexcept {}
        void                    Finish()
        {
            auto handle = std::exchange(continuation, {});
            REQUIRE(handle);
            handle.resume();
        }
    };

    NGIN::Async::Task<Status> Hold(NGIN::Async::TaskContext& context, Gate& gate, Kind kind, Pending& pending)
    {
        auto admission = gate.Acquire(context, kind);
        co_await admission;
        const auto status = admission.GetStatus();
        pending.fault     = admission.Fault();
        if (status == Status::Granted)
        {
            pending.entered = true;
            co_await pending;
            admission.Reset(pending.close);
        }
        co_return status;
    }
}// namespace

TEST_CASE("File admission overlaps independent offsets and orders the shared position", "[IO][Admission]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              context(scheduler);
    FileFixture                           files;
    auto&                                 gate = files.gate;
    std::array<Pending, 4>                pending;
    auto                                  first = NGIN::Async::Spawn(context, Hold(context, gate, Kind::Position, pending[0]));
    scheduler.RunUntilIdle();
    auto second = NGIN::Async::Spawn(context, Hold(context, gate, Kind::Position, pending[1]));
    scheduler.RunUntilIdle();
    auto independent = NGIN::Async::Spawn(context, Hold(context, gate, Kind::Independent, pending[2]));
    auto another     = NGIN::Async::Spawn(context, Hold(context, gate, Kind::Independent, pending[3]));
    scheduler.RunUntilIdle();
    REQUIRE(pending[0].entered);
    REQUIRE_FALSE(pending[1].entered);
    REQUIRE(pending[2].entered);
    REQUIRE(pending[3].entered);
    pending[0].Finish();
    scheduler.RunUntilIdle();
    REQUIRE(pending[1].entered);
    pending[1].Finish();
    pending[2].Finish();
    pending[3].Finish();
    scheduler.RunUntilIdle();
    REQUIRE(first.TakeResult().Value() == Status::Granted);
    REQUIRE(second.TakeResult().Value() == Status::Granted);
    REQUIRE(independent.TakeResult().Value() == Status::Granted);
    REQUIRE(another.TakeResult().Value() == Status::Granted);
}

TEST_CASE("File flush waits for earlier active and queued work and blocks later offsets", "[IO][Admission]")
{
    NGIN::Execution::CooperativeScheduler         scheduler;
    NGIN::Async::TaskContext                      context(scheduler);
    FileFixture                                   files;
    auto&                                         gate = files.gate;
    std::array<Pending, 5>                        pending;
    std::array<NGIN::Async::Operation<Status>, 5> operations;
    const std::array                              kinds {Kind::Position, Kind::Position, Kind::Independent, Kind::Flush, Kind::Independent};
    for (std::size_t i = 0; i < pending.size(); ++i)
    {
        operations[i] = NGIN::Async::Spawn(context, Hold(context, gate, kinds[i], pending[i]));
        scheduler.RunUntilIdle();
    }
    REQUIRE(pending[0].entered);
    REQUIRE(pending[2].entered);
    REQUIRE_FALSE(pending[1].entered);
    REQUIRE_FALSE(pending[3].entered);
    REQUIRE_FALSE(pending[4].entered);
    pending[0].Finish();
    scheduler.RunUntilIdle();
    REQUIRE(pending[1].entered);
    pending[2].Finish();
    scheduler.RunUntilIdle();
    REQUIRE_FALSE(pending[3].entered);
    pending[1].Finish();
    scheduler.RunUntilIdle();
    REQUIRE(pending[3].entered);
    REQUIRE_FALSE(pending[4].entered);
    pending[3].Finish();
    scheduler.RunUntilIdle();
    REQUIRE(pending[4].entered);
    pending[4].Finish();
    scheduler.RunUntilIdle();
    for (auto& operation: operations)
        REQUIRE(operation.TakeResult().Value() == Status::Granted);
}

TEST_CASE("File close closes admission while draining earlier backend work", "[IO][Admission]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              context(scheduler);
    FileFixture                           files;
    auto&                                 gate = files.gate;
    Pending                               read, closing, rejected, overlapping, repeated;
    closing.close = true;
    auto first    = NGIN::Async::Spawn(context, Hold(context, gate, Kind::Independent, read));
    scheduler.RunUntilIdle();
    auto close = NGIN::Async::Spawn(context, Hold(context, gate, Kind::Close, closing));
    scheduler.RunUntilIdle();
    REQUIRE(gate.IsClosing());
    REQUIRE_FALSE(closing.entered);
    auto later     = NGIN::Async::Spawn(context, Hold(context, gate, Kind::Independent, rejected));
    auto duplicate = NGIN::Async::Spawn(context, Hold(context, gate, Kind::Close, overlapping));
    scheduler.RunUntilIdle();
    REQUIRE(later.TakeResult().Value() == Status::Closing);
    REQUIRE(duplicate.TakeResult().Value() == Status::Closing);
    read.Finish();
    scheduler.RunUntilIdle();
    REQUIRE(closing.entered);
    closing.Finish();
    scheduler.RunUntilIdle();
    auto closed = NGIN::Async::Spawn(context, Hold(context, gate, Kind::Close, repeated));
    scheduler.RunUntilIdle();
    REQUIRE(closed.TakeResult().Value() == Status::Closed);
    REQUIRE(first.TakeResult().Value() == Status::Granted);
    REQUIRE(close.TakeResult().Value() == Status::Granted);
}

TEST_CASE("Canceling queued file admission removes the waiter and releases barriers", "[IO][Admission][Cancellation]")
{
    const auto                            kind = GENERATE(Kind::Position, Kind::Flush, Kind::Close);
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              context(scheduler);
    NGIN::Async::CancellationSource       cancellation;
    NGIN::Async::TaskContext              canceledContext(scheduler, cancellation.GetToken());
    FileFixture                           files;
    auto&                                 gate = files.gate;
    Pending                               active, waiting, later;
    auto                                  first = NGIN::Async::Spawn(context, Hold(context, gate, Kind::Position, active));
    scheduler.RunUntilIdle();
    auto canceled = NGIN::Async::Spawn(canceledContext, Hold(canceledContext, gate, kind, waiting));
    scheduler.RunUntilIdle();
    REQUIRE_FALSE(waiting.entered);
    cancellation.Cancel();
    scheduler.RunUntilIdle();
    REQUIRE(canceled.TakeResult().Value() == Status::Canceled);
    REQUIRE_FALSE(gate.IsClosing());
    auto next = NGIN::Async::Spawn(context, Hold(context, gate, Kind::Independent, later));
    scheduler.RunUntilIdle();
    REQUIRE(later.entered);
    later.Finish();
    active.Finish();
    scheduler.RunUntilIdle();
    REQUIRE(next.TakeResult().Value() == Status::Granted);
    REQUIRE(first.TakeResult().Value() == Status::Granted);
}

TEST_CASE("File admission cancellation racing release retains the waiting frame", "[IO][Admission][Cancellation][Lifetime]")
{
    for (int iteration = 0; iteration != 300; ++iteration)
    {
        NGIN::Execution::CooperativeScheduler scheduler;
        NGIN::Async::TaskContext              context(scheduler);
        NGIN::Async::CancellationSource       cancellation;
        NGIN::Async::TaskContext              canceledContext(scheduler, cancellation.GetToken());
        FileFixture                           files;
        auto&                                 gate = files.gate;
        Pending                               active, waiting;
        auto                                  first = NGIN::Async::Spawn(context, Hold(context, gate, Kind::Position, active));
        scheduler.RunUntilIdle();
        auto second = NGIN::Async::Spawn(canceledContext, Hold(canceledContext, gate, Kind::Position, waiting));
        scheduler.RunUntilIdle();
        std::thread cancel([&] { cancellation.Cancel(); });
        active.Finish();
        scheduler.RunUntilIdle();
        cancel.join();
        scheduler.RunUntilIdle();
        if (waiting.entered)
            waiting.Finish();
        scheduler.RunUntilIdle();
        REQUIRE(second.TakeResult().Value() == (waiting.entered ? Status::Granted : Status::Canceled));
        REQUIRE(first.TakeResult().Value() == Status::Granted);
    }
}

namespace
{
    struct BackendState final
    {
        explicit BackendState(std::shared_ptr<NGIN::IO::detail::FileSystemDriver> owner)
            : driver(std::move(owner)), operations(*driver) {}
        std::shared_ptr<NGIN::IO::detail::FileSystemDriver> driver;
        Gate                                                operations;
        NGIN::IO::Path                                      path {"test-file"};
        bool                                                nativeOpen {true};
        bool                                                frameRetired {false};
        int                                                 calls {0};
        bool                                                NativeIsOpen() const noexcept { return nativeOpen; }
    };

    NGIN::IO::AsyncTaskVoid CloseBackend(const std::shared_ptr<void>& raw, NGIN::Async::TaskContext&, int outcome)
    {
        auto state = std::static_pointer_cast<BackendState>(raw);
        struct Guard final
        {
            BackendState& state;
            bool          rollback;
            ~Guard()
            {
                state.frameRetired = true;
                if (rollback)
                    state.nativeOpen = true;
            }
        } guard {*state, outcome != 0};
        ++state->calls;
        state->nativeOpen = false;
        if (outcome == 1)
            co_await NGIN::Async::Canceled();
        if (outcome == 2)
            co_await NGIN::Async::Faulted(NGIN::Async::MakeAsyncFault(NGIN::Async::AsyncFaultCode::InvalidTaskUsage));
        if (outcome == 3)
            co_await NGIN::Async::DomainFailure(NGIN::IO::IOError(NGIN::IO::IOErrorCode::SystemError));
        co_return;
    }
}// namespace

TEST_CASE("File close retires the backend rollback guard before releasing admission", "[IO][Admission][Lifetime]")
{
    const int                             outcome = GENERATE(0, 1, 2, 3);
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              context(scheduler);
    FileFixture                           files(1);
    auto                                  state = std::make_shared<BackendState>(files.driver);
    auto                                  close = NGIN::Async::Spawn(context, NGIN::IO::detail::RunFileOperation<void, BackendState>(
                                                     context, state, Kind::Close, &CloseBackend, outcome));
    scheduler.RunUntilIdle();
    REQUIRE(close.IsCompleted());
    const auto result = close.TakeResult();
    REQUIRE(result.Succeeded() == (outcome == 0));
    REQUIRE(result.IsCanceled() == (outcome == 1));
    REQUIRE(result.IsFault() == (outcome == 2));
    REQUIRE(result.IsDomainError() == (outcome == 3));
    REQUIRE(state->frameRetired);
    REQUIRE(state->nativeOpen == (outcome != 0));
    REQUIRE(state->operations.IsClosing() == (outcome == 0));
    auto next = NGIN::Async::Spawn(context, NGIN::IO::detail::RunFileOperation<void, BackendState>(
                                                    context, state, Kind::Close, &CloseBackend, 0));
    scheduler.RunUntilIdle();
    REQUIRE(next.TakeResult());
    REQUIRE(state->calls == (outcome == 0 ? 1 : 2));
    REQUIRE_FALSE(state->nativeOpen);
}

TEST_CASE("File transfer validation rejects lengths and offsets before backend truncation", "[IO][Admission]")
{
    using NGIN::IO::detail::ValidateFileTransfer;
    const NGIN::IO::Path path {"test-file"};
    REQUIRE(ValidateFileTransfer(path, true, false, false, 0, 0));
    REQUIRE(ValidateFileTransfer(path, true, true, false, std::numeric_limits<NGIN::UInt32>::max(),
                                 static_cast<NGIN::UInt64>(std::numeric_limits<NGIN::Int64>::max())));
    auto offset = ValidateFileTransfer(path, true, false, false, 1, std::numeric_limits<NGIN::UInt64>::max());
    REQUIRE_FALSE(offset);
    REQUIRE(offset.error().code == NGIN::IO::IOErrorCode::InvalidArgument);
    if constexpr (sizeof(NGIN::UIntSize) > sizeof(NGIN::UInt32))
    {
        auto length = ValidateFileTransfer(path, true, false, false,
                                           static_cast<NGIN::UIntSize>(std::numeric_limits<NGIN::UInt32>::max()) + 1);
        REQUIRE_FALSE(length);
        REQUIRE(length.error().code == NGIN::IO::IOErrorCode::InvalidArgument);
    }
    REQUIRE(ValidateFileTransfer(path, true, true, true, 1));
    auto append = ValidateFileTransfer(path, true, true, true, 1, 0);
    REQUIRE_FALSE(append);
    REQUIRE(append.error().code == NGIN::IO::IOErrorCode::NotSupported);
    auto readOnly = ValidateFileTransfer(path, false, true, false, 1);
    REQUIRE_FALSE(readOnly);
    REQUIRE(readOnly.error().code == NGIN::IO::IOErrorCode::NotSupported);
}

TEST_CASE("File admission preserves accepted work when completion capacity is exhausted", "[IO][Admission]")
{
    const bool                            cancelWaiting = GENERATE(false, true);
    NGIN::Execution::CooperativeScheduler scheduler(2);
    NGIN::Async::TaskContext              context(scheduler);
    NGIN::Async::CancellationSource       cancellation;
    NGIN::Async::TaskContext              canceledContext(scheduler, cancellation.GetToken());
    FileFixture                           files;
    auto&                                 gate = files.gate;
    Pending                               active, waiting;
    auto                                  first = NGIN::Async::Spawn(context, Hold(context, gate, Kind::Position, active));
    scheduler.RunUntilIdle();
    auto second = NGIN::Async::Spawn(canceledContext, Hold(canceledContext, gate, Kind::Position, waiting));
    scheduler.RunUntilIdle();
    auto unavailable = scheduler.ReserveCompletion(NGIN::Execution::WorkItem([] {}));
    REQUIRE_FALSE(unavailable);
    REQUIRE(unavailable.error() == NGIN::Execution::ScheduleError::ResourceExhausted);
    if (cancelWaiting)
        cancellation.Cancel();
    active.Finish();
    scheduler.RunUntilIdle();
    if (!cancelWaiting)
    {
        REQUIRE(waiting.entered);
        waiting.Finish();
    }
    scheduler.RunUntilIdle();
    REQUIRE(first.TakeResult().Value() == Status::Granted);
    REQUIRE(second.TakeResult().Value() == (cancelWaiting ? Status::Canceled : Status::Granted));
}

TEST_CASE("File admission registration failure publishes no backend lease", "[IO][Admission]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Tests::FailureMemoryResource    resource;
    NGIN::Async::CancellationSource       cancellation(&resource);
    NGIN::Async::TaskContext              context(scheduler, cancellation.GetToken());
    FileFixture                           files;
    auto&                                 gate = files.gate;
    Pending                               pending;
    auto                                  operation = NGIN::Async::Spawn(context, Hold(context, gate, Kind::Close, pending));
    resource.FailNextAllocation();
    scheduler.RunUntilIdle();
    resource.DisableFailure();
    REQUIRE(operation.TakeResult().Value() == Status::Fault);
    REQUIRE_FALSE(pending.entered);
    REQUIRE_FALSE(gate.IsClosing());
    cancellation.Cancel();
}

TEST_CASE("File admission bounds all handles and executors and recovers canceled capacity", "[IO][Admission]")
{
    FileFixture                           files(4);
    Gate                                  other(*files.driver);
    NGIN::Execution::CooperativeScheduler firstScheduler, secondScheduler, thirdScheduler;
    NGIN::Async::CancellationSource       cancellation;
    NGIN::Async::TaskContext              firstContext(firstScheduler), secondContext(secondScheduler, cancellation.GetToken()),
            thirdContext(thirdScheduler);
    std::array<Pending, 6> pending;
    auto                   first = NGIN::Async::Spawn(firstContext, Hold(firstContext, files.gate, Kind::Position, pending[0]));
    firstScheduler.RunUntilIdle();
    auto waiting = NGIN::Async::Spawn(secondContext, Hold(secondContext, files.gate, Kind::Position, pending[1]));
    secondScheduler.RunUntilIdle();
    auto independent = NGIN::Async::Spawn(thirdContext, Hold(thirdContext, other, Kind::Independent, pending[2]));
    thirdScheduler.RunUntilIdle();
    auto close = NGIN::Async::Spawn(secondContext, Hold(secondContext, other, Kind::Close, pending[3]));
    secondScheduler.RunUntilIdle();
    REQUIRE(pending[0].entered);
    REQUIRE(pending[2].entered);
    REQUIRE_FALSE(pending[1].entered);
    REQUIRE_FALSE(pending[3].entered);
    REQUIRE(other.IsClosing());
    for (int attempt = 0; attempt != 256; ++attempt)
    {
        Pending    rejected;
        const Kind kind   = attempt % 2 == 0 ? Kind::Independent : Kind::Close;
        auto       excess = NGIN::Async::Spawn(thirdContext, Hold(thirdContext, files.gate, kind, rejected));
        thirdScheduler.RunUntilIdle();
        REQUIRE(excess.TakeResult().Value() == Status::Fault);
        REQUIRE(rejected.fault.code == NGIN::Async::AsyncFaultCode::SchedulerDispatchFailed);
        REQUIRE(rejected.fault.native == static_cast<int>(NGIN::Execution::ScheduleError::ResourceExhausted));
        REQUIRE_FALSE(rejected.entered);
        REQUIRE_FALSE(files.gate.IsClosing());
    }
    cancellation.Cancel();
    Pending awaitingCleanup;
    auto    stillFull = NGIN::Async::Spawn(thirdContext, Hold(thirdContext, files.gate, Kind::Independent, awaitingCleanup));
    thirdScheduler.RunUntilIdle();
    REQUIRE(stillFull.TakeResult().Value() == Status::Fault);
    REQUIRE(awaitingCleanup.fault.native == static_cast<int>(NGIN::Execution::ScheduleError::ResourceExhausted));
    secondScheduler.RunUntilIdle();
    REQUIRE(waiting.TakeResult().Value() == Status::Canceled);
    REQUIRE(close.TakeResult().Value() == Status::Canceled);
    REQUIRE_FALSE(other.IsClosing());
    auto recoveredFirst = NGIN::Async::Spawn(firstContext, Hold(firstContext, files.gate, Kind::Independent, pending[4]));
    auto recoveredOther = NGIN::Async::Spawn(thirdContext, Hold(thirdContext, other, Kind::Independent, pending[5]));
    firstScheduler.RunUntilIdle();
    thirdScheduler.RunUntilIdle();
    REQUIRE(pending[4].entered);
    REQUIRE(pending[5].entered);
    for (const std::size_t index: {0, 2, 4, 5})
        pending[index].Finish();
    firstScheduler.RunUntilIdle();
    thirdScheduler.RunUntilIdle();
    REQUIRE(first.TakeResult().Value() == Status::Granted);
    REQUIRE(independent.TakeResult().Value() == Status::Granted);
    REQUIRE(recoveredFirst.TakeResult().Value() == Status::Granted);
    REQUIRE(recoveredOther.TakeResult().Value() == Status::Granted);
    files.runtime.Shutdown();
    Pending stopped;
    auto    afterStop = NGIN::Async::Spawn(firstContext, Hold(firstContext, files.gate, Kind::Independent, stopped));
    firstScheduler.RunUntilIdle();
    REQUIRE(afterStop.TakeResult().Value() == Status::Canceled);
    REQUIRE_FALSE(stopped.entered);
}

TEST_CASE("Concurrent file admission shares one bounded budget across producers", "[IO][Admission][Concurrency]")
{
    constexpr std::size_t  producerCount = 4;
    constexpr std::size_t  requests      = 32;
    constexpr NGIN::UInt32 capacity      = 8;
    FileFixture            files(capacity);
    struct Producer final
    {
        explicit Producer(NGIN::IO::detail::FileSystemDriver& driver) : gate(driver), context(scheduler) {}
        Gate                                                 gate;
        NGIN::Execution::CooperativeScheduler                scheduler;
        NGIN::Async::TaskContext                             context;
        std::array<Pending, requests>                        pending;
        std::array<NGIN::Async::Operation<Status>, requests> operations;
    };
    std::array<std::unique_ptr<Producer>, producerCount> producers;
    for (auto& producer: producers)
        producer = std::make_unique<Producer>(*files.driver);
    for (int round = 0; round != 16; ++round)
    {
        std::barrier                           start(static_cast<std::ptrdiff_t>(producerCount));
        std::array<std::thread, producerCount> threads;
        for (std::size_t index = 0; index != producerCount; ++index)
            threads[index] = std::thread([&, index] {
                auto& producer = *producers[index];
                start.arrive_and_wait();
                for (std::size_t request = 0; request != requests; ++request)
                {
                    producer.operations[request] = NGIN::Async::Spawn(producer.context,
                                                                      Hold(producer.context, producer.gate, Kind::Independent, producer.pending[request]));
                    producer.scheduler.RunUntilIdle();
                }
            });
        for (auto& thread: threads)
            thread.join();
        std::size_t accepted = 0;
        std::size_t rejected = 0;
        for (auto& producer: producers)
        {
            for (std::size_t request = 0; request != requests; ++request)
            {
                auto& pending = producer->pending[request];
                if (pending.entered)
                {
                    ++accepted;
                    pending.Finish();
                    producer->scheduler.RunUntilIdle();
                    REQUIRE(producer->operations[request].TakeResult().Value() == Status::Granted);
                }
                else
                {
                    ++rejected;
                    REQUIRE(producer->operations[request].TakeResult().Value() == Status::Fault);
                }
                pending = {};
            }
        }
        REQUIRE(accepted == capacity);
        REQUIRE(rejected == producerCount * requests - capacity);
    }
}
