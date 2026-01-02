#include "SocketPlatform.hpp"

#if defined(NGIN_PLATFORM_WINDOWS)
namespace NGIN::Net::detail
{
    struct IocpPoller final
    {
        // IOCP scaffolding: wire CompletionPort + overlapped operations in a future iteration.
    };
}
#endif
