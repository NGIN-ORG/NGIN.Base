/// @file Expected.hpp
/// @brief NGIN names for the C++23 standard expected-value vocabulary types.
#pragma once

#include <expected>

namespace NGIN::Utilities
{
    /// @brief Standard value-or-error result type retained under the NGIN namespace.
    template<typename T, typename E>
    using Expected = std::expected<T, E>;

    /// @brief Standard explicit error wrapper retained under the NGIN namespace.
    template<typename E>
    using Unexpected = std::unexpected<E>;
}// namespace NGIN::Utilities
