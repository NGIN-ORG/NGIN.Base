#if NGIN_BASE_TEST_COMPONENT_Foundation
#include <NGIN/Primitives.hpp>
#elif NGIN_BASE_TEST_COMPONENT_Execution
#include <NGIN/Async/ExecutorTransitions.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/TaskSupervisor.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Execution/InlineScheduler.hpp>
#elif NGIN_BASE_TEST_COMPONENT_IORuntime
#include <NGIN/IO/RunTask.hpp>
#include <NGIN/IO/RuntimeRunner.hpp>
#elif NGIN_BASE_TEST_COMPONENT_IO
#include <NGIN/Async/TaskSupervisor.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/IO/AsyncFileHandle.hpp>
#include <array>
#elif NGIN_BASE_TEST_COMPONENT_Net
#include <NGIN/IO/Runtime.hpp>
#include <NGIN/Net/Sockets/UdpSocket.hpp>
#elif NGIN_BASE_TEST_COMPONENT_NetTLS
#include <NGIN/Net/TLS/TlsError.hpp>
#elif NGIN_BASE_TEST_COMPONENT_Complete
#include <NGIN/NGIN.hpp>
#include <NGIN/Net.hpp>
#include <NGIN/NetTLS.hpp>
#include <NGIN/Serialization.hpp>
#endif

#include <type_traits>

int main()
{
#if NGIN_BASE_TEST_COMPONENT_Foundation
    static_assert(std::is_same_v<NGIN::UInt32, std::uint32_t>);
#elif NGIN_BASE_TEST_COMPONENT_Execution
#include <NGIN/Async/TaskSupervisor.hpp>
    NGIN::Execution::CooperativeScheduler scheduler;
    int                                   metadata = 42;
    NGIN::Async::TaskContext              context(NGIN::Async::AsyncEnvironment {
                         .executor = NGIN::Execution::ExecutorRef::From(scheduler),
                         .metadata = &metadata});
    auto                                  makeTask = []() -> NGIN::Async::Task<int> {
        auto environment = co_await NGIN::Async::CurrentEnvironment();
        co_return *static_cast<const int*>(environment.metadata);
    };
    auto initialTask = makeTask();

    NGIN::Async::TaskSupervisor<typename decltype(initialTask)::ErrorType> initialRoot(context.GetEnvironment(), 1);

    auto initial = initialRoot.Spawn(std::move(initialTask)).value();
    scheduler.RunUntilIdle();
    if (!initialRoot.TryJoin())
        return 1;
    auto result = initial.TakeResult();
    if (!result.Succeeded() || result.Value() != 42)
        return 1;
    auto ready = NGIN::Async::SyncWait(context, NGIN::Async::Task<void>::FromValue());
    if (!ready.Succeeded())
        return 1;
    NGIN::Execution::CooperativeScheduler source, target;
    const auto                            targetRef = NGIN::Execution::ExecutorRef::From(target);
    NGIN::Async::TaskContext              transitionContext(NGIN::Async::AsyncEnvironment {
                         .executor = NGIN::Execution::ExecutorRef::From(source),
                         .metadata = &metadata});
    auto                                  transitionTask = [&]() -> NGIN::Async::Task<bool> {
        auto value = co_await NGIN::Async::ContinuesOn(targetRef, makeTask());
        co_await NGIN::Async::YieldNow();
        auto environment = co_await NGIN::Async::CurrentEnvironment();
        co_return value == 42 && targetRef.IsCurrent() && environment.executor == targetRef;
    };
    auto operationTask = transitionTask();

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(transitionContext.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    source.RunUntilIdle();
    target.RunUntilIdle();
    source.RunUntilIdle();
    if (!operation.IsCompleted())
        return 1;
    if (!operationRoot.TryJoin())
        return 1;
    auto transitioned = operation.TakeResult();
    if (!transitioned.HasValue() || !transitioned.Value())
        return 1;
#elif NGIN_BASE_TEST_COMPONENT_IORuntime
    NGIN::IO::Runtime          runtime;
    NGIN::IO::NativeWaitSource sources[1];
    const auto                 copied = runtime.CopyNativeWaitSources(sources);
#if defined(_WIN32)
    if (copied || copied.error() != std::errc::operation_not_supported)
        return 1;
#else
    if (!copied || *copied != 1 || sources[0].handle < 0)
        return 1;
#endif

    auto result = NGIN::IO::RunTask(runtime,
                                    [](NGIN::Async::TaskContext& ctx, NGIN::Async::TaskScope<>&) -> NGIN::Async::Task<int> {
                                        co_await ctx.YieldNow();
                                        co_await ctx.Delay(NGIN::Units::Milliseconds(1));
                                        co_return 42;
                                    });
    if (!result.Succeeded() || result.Value() != 42 || !runtime.IsStopped() ||
        runtime.HasFileBackend() || runtime.HasNetworkBackend())
        return 1;
    NGIN::IO::Runtime       background;
    NGIN::IO::RuntimeRunner runner(background);
    runner.Shutdown();
#elif NGIN_BASE_TEST_COMPONENT_IO
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              context(scheduler);
    NGIN::IO::AsyncFileHandle             file;
    std::array<NGIN::Byte, 1>             bytes {};
    auto                                  readTask = file.ReadAsync(context, bytes);

    NGIN::Async::TaskSupervisor<typename decltype(readTask)::ErrorType> readRoot(context.GetEnvironment(), 1);

    auto read      = readRoot.Spawn(std::move(readTask)).value();
    auto writeTask = file.WriteAsync(context, bytes);

    NGIN::Async::TaskSupervisor<typename decltype(writeTask)::ErrorType> writeRoot(context.GetEnvironment(), 1);

    auto write      = writeRoot.Spawn(std::move(writeTask)).value();
    auto readAtTask = file.ReadAtAsync(context, 0, bytes);

    NGIN::Async::TaskSupervisor<typename decltype(readAtTask)::ErrorType> readAtRoot(context.GetEnvironment(), 1);

    auto readAt      = readAtRoot.Spawn(std::move(readAtTask)).value();
    auto writeAtTask = file.WriteAtAsync(context, 0, bytes);

    NGIN::Async::TaskSupervisor<typename decltype(writeAtTask)::ErrorType> writeAtRoot(context.GetEnvironment(), 1);

    auto writeAt   = writeAtRoot.Spawn(std::move(writeAtTask)).value();
    auto flushTask = file.FlushAsync(context);

    NGIN::Async::TaskSupervisor<typename decltype(flushTask)::ErrorType> flushRoot(context.GetEnvironment(), 1);

    auto flush     = flushRoot.Spawn(std::move(flushTask)).value();
    auto closeTask = file.CloseAsync(context);

    NGIN::Async::TaskSupervisor<typename decltype(closeTask)::ErrorType> closeRoot(context.GetEnvironment(), 1);

    auto close = closeRoot.Spawn(std::move(closeTask)).value();
    scheduler.RunUntilIdle();
    if (!readRoot.TryJoin() || !writeRoot.TryJoin() || !readAtRoot.TryJoin() ||
        !writeAtRoot.TryJoin() || !flushRoot.TryJoin() || !closeRoot.TryJoin())
        return 1;
    if (!read.TakeResult().Error().IsDomainError() || !write.TakeResult().Error().IsDomainError() ||
        !readAt.TakeResult().Error().IsDomainError() || !writeAt.TakeResult().Error().IsDomainError() ||
        !flush.TakeResult().Error().IsDomainError() || !close.TakeResult().Error().IsDomainError())
        return 1;
#elif NGIN_BASE_TEST_COMPONENT_Net
    NGIN::IO::Runtime    runtime;
    NGIN::Net::UdpSocket socket(runtime);
    if (!socket.Open(NGIN::Net::AddressFamily::V4) || runtime.HasFileBackend())
        return 1;
#elif NGIN_BASE_TEST_COMPONENT_NetTLS
    NGIN::Net::TLS::TlsError error;
    (void) error;
#elif NGIN_BASE_TEST_COMPONENT_Complete
    static_assert(std::is_same_v<NGIN::UInt64, std::uint64_t>);
#endif
    return 0;
}
