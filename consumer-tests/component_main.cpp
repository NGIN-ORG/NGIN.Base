#if defined(NGIN_CONSUMER_FOUNDATION)
#include <NGIN/SIMD.hpp>
#elif defined(NGIN_CONSUMER_EXECUTION)
#include <NGIN/Execution/ThisThread.hpp>
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
