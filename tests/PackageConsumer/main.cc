#if NGIN_BASE_TEST_COMPONENT_Foundation
#include <NGIN/Primitives.hpp>
#elif NGIN_BASE_TEST_COMPONENT_Execution
#include <NGIN/Execution/InlineScheduler.hpp>
#elif NGIN_BASE_TEST_COMPONENT_Net
#include <NGIN/Net/Types/Endpoint.hpp>
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
    (void)scheduler;
#elif NGIN_BASE_TEST_COMPONENT_Net
    NGIN::Net::Endpoint endpoint;
    (void)endpoint;
#elif NGIN_BASE_TEST_COMPONENT_NetTLS
    NGIN::Net::TLS::TlsError error;
    (void)error;
#elif NGIN_BASE_TEST_COMPONENT_Complete
    static_assert(std::is_same_v<NGIN::UInt64, std::uint64_t>);
#endif
    return 0;
}
