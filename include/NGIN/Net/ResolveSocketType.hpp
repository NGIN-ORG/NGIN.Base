/// @file ResolveSocketType.hpp
/// @brief Socket transport hint used during endpoint resolution.
#pragma once

#include <NGIN/Primitives.hpp>

namespace NGIN::Net
{
    /// @brief Selects which socket transport results a resolver may return.
    enum class ResolveSocketType : NGIN::UInt8
    {
        Any,     ///< Return any compatible socket transport.
        Stream,  ///< Return stream-oriented endpoints such as TCP.
        Datagram,///< Return datagram-oriented endpoints such as UDP.
    };
}// namespace NGIN::Net
