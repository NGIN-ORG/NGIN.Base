#if defined(NGIN_CONSUMER_FOUNDATION)
#include <NGIN/SIMD.hpp>
#elif defined(NGIN_CONSUMER_EXECUTION)
#include <NGIN/Execution/ThisThread.hpp>
#elif defined(NGIN_CONSUMER_IORUNTIME)
#include <NGIN/IO/RunTask.hpp>
#include <NGIN/IO/RuntimeRunner.hpp>
#elif defined(NGIN_CONSUMER_IO)
#include <NGIN/IO/Path.hpp>
#elif defined(NGIN_CONSUMER_SERIALIZATION)
#include <NGIN/Serialization/JSON/JsonParser.hpp>
#elif defined(NGIN_CONSUMER_CRYPTO)
#include <NGIN/Crypto/Random/SecureRandom.hpp>
#elif defined(NGIN_CONSUMER_NET)
#include <NGIN/Net/Types/IpAddress.hpp>
#elif defined(NGIN_CONSUMER_NETTLS)
#include <NGIN/NetTLS.hpp>
#endif

int main()
{
#if defined(NGIN_CONSUMER_FOUNDATION)
    constexpr unsigned char bytes[] {'a', 'b', 'c'};
    const auto              backend = NGIN::SIMD::GetRuntimeBackend();
    const auto              found   = NGIN::SIMD::FindEqByteRuntime(bytes, 3, static_cast<unsigned char>('b'));
    return NGIN::SIMD::GetRuntimeFeatures().Supports(backend) && found == 1 ? 0 : 1;
#elif defined(NGIN_CONSUMER_EXECUTION)
    static_cast<void>(NGIN::Execution::ThisThread::HardwareConcurrency());
    return 0;
#elif defined(NGIN_CONSUMER_IORUNTIME)
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
    return 0;
#elif defined(NGIN_CONSUMER_IO)
    return NGIN::IO::Path {"a/../b"}.LexicallyNormal().View() == "b" ? 0 : 1;
#elif defined(NGIN_CONSUMER_SERIALIZATION)
    return NGIN::Serialization::JSON::Parse("{}") ? 0 : 1;
#elif defined(NGIN_CONSUMER_CRYPTO)
    return NGIN::Crypto::Random::IsAvailable() ? 0 : 1;
#elif defined(NGIN_CONSUMER_NET)
    return NGIN::Net::IpAddress::Parse("127.0.0.1") ? 0 : 1;
#elif defined(NGIN_CONSUMER_NETTLS)
    static_cast<void>(NGIN::Net::TLS::TlsProviderAvailable());
    return 0;
#else
#error "No NGIN.Base component consumer selected"
#endif
}
