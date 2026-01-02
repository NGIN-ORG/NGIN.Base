/// @file Endpoint.hpp
/// @brief Network endpoint (address + port).
#pragma once

#include <NGIN/Net/Types/IpAddress.hpp>
#include <NGIN/Primitives.hpp>

namespace NGIN::Net
{
    /// @brief Address and port pair.
    struct Endpoint final
    {
        IpAddress   address {};
        NGIN::UInt16 port {0};
    };
}// namespace NGIN::Net
