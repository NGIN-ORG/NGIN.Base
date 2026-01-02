/// @file SocketOptions.hpp
/// @brief Common socket option flags.
#pragma once

namespace NGIN::Net
{
    /// @brief Common socket options applied at open/bind.
    struct SocketOptions final
    {
        bool nonBlocking {true};
        bool reuseAddress {true};
        bool reusePort {false};
        bool noDelay {false};
        bool broadcast {false};
        bool v6Only {false};
    };
}// namespace NGIN::Net
