/// @file NativeWaitSource.hpp
/// @brief Borrowed descriptor interest for integration with a POSIX host wait.
#pragma once

#include <cstdint>

namespace NGIN::IO
{
    /// @brief One borrowed descriptor to monitor without reading or closing it.
    /// @details A source only notifies the host to call Runtime::PollOnce. It is
    /// not an operation identifier, and readiness never authorizes native I/O.
    struct NativeWaitSource final
    {
        enum Interest : unsigned
        {
            Read  = 1,
            Write = 2
        };
        std::intptr_t handle {-1};
        unsigned      interests {};
    };
}// namespace NGIN::IO
