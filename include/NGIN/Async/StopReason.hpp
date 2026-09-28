/// @file StopReason.hpp
/// @brief Origin of a cooperative stop request.
#pragma once
#include <NGIN/Primitives.hpp>

namespace NGIN::Async
{
    /// @brief Why cooperative completion stopped; requesting stop alone is not completion.
    enum class StopReason : NGIN::UInt8
    {
        Requested,
        Deadline,
        OwnerClosed,
        RuntimeShutdown
    };
}// namespace NGIN::Async
