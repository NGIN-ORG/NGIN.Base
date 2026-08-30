/// @file Optional.hpp
/// @brief NGIN name for the C++17 standard optional-value vocabulary type.
#pragma once

#include <optional>

namespace NGIN::Utilities
{
    /// @brief Standard optional value type retained under the NGIN namespace.
    template<typename T>
    using Optional = std::optional<T>;
}// namespace NGIN::Utilities
