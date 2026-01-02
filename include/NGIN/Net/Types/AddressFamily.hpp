/// @file AddressFamily.hpp
/// @brief Network address family selection.
#pragma once

#include <NGIN/Primitives.hpp>

namespace NGIN::Net
{
    /// @brief Address family selection for sockets.
    enum class AddressFamily : NGIN::UInt8
    {
        V4,
        V6,
        DualStack,
    };
}// namespace NGIN::Net
