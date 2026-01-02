/// @file ShutdownMode.hpp
/// @brief Socket shutdown direction.
#pragma once

#include <NGIN/Primitives.hpp>

namespace NGIN::Net
{
    /// @brief Shutdown direction for TCP sockets.
    enum class ShutdownMode : NGIN::UInt8
    {
        Receive,
        Send,
        Both,
    };
}// namespace NGIN::Net
