#if NGIN_BASE_TEST_COMPONENT_Foundation
#include <NGIN/Primitives.hpp>
#elif NGIN_BASE_TEST_COMPONENT_Execution
#include <NGIN/Execution/InlineScheduler.hpp>
#elif NGIN_BASE_TEST_COMPONENT_IORuntime
#include <NGIN/IO/RunTask.hpp>
#include <NGIN/IO/RuntimeRunner.hpp>
#elif NGIN_BASE_TEST_COMPONENT_IO
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
    NGIN::Execution::InlineScheduler scheduler;
    (void) scheduler;
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
    auto                                  read    = NGIN::Async::Spawn(context, file.ReadAsync(context, bytes));
    auto                                  write   = NGIN::Async::Spawn(context, file.WriteAsync(context, bytes));
    auto                                  readAt  = NGIN::Async::Spawn(context, file.ReadAtAsync(context, 0, bytes));
    auto                                  writeAt = NGIN::Async::Spawn(context, file.WriteAtAsync(context, 0, bytes));
    auto                                  flush   = NGIN::Async::Spawn(context, file.FlushAsync(context));
    auto                                  close   = NGIN::Async::Spawn(context, file.CloseAsync(context));
    scheduler.RunUntilIdle();
    if (!read.TakeResult().IsDomainError() || !write.TakeResult().IsDomainError() ||
        !readAt.TakeResult().IsDomainError() || !writeAt.TakeResult().IsDomainError() ||
        !flush.TakeResult().IsDomainError() || !close.TakeResult().IsDomainError())
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
