#pragma once

#include <cstddef>

namespace NGIN::Utilities
{
    /// @brief Tag type representing an empty optional state.
    struct nullopt_t
    {
        /// @brief Constructs the tag from an internal sentinel.
        explicit constexpr nullopt_t(int) noexcept { }
    };

    /// @brief Global tag used to construct empty `Optional`-like objects.
    inline constexpr nullopt_t nullopt {0};

    /// @brief Tag type selecting in-place construction of a value of type `T`.
    ///
    /// @tparam T Type that will be constructed in place.
    template <class T>
    struct InPlaceType
    {
        /// @brief Constructs the tag.
        explicit constexpr InPlaceType() noexcept { }
    };

    /// @brief Tag type selecting in-place construction of indexed alternatives.
    ///
    /// @tparam I Compile-time index of the selected alternative.
    template <std::size_t I>
    struct InPlaceIndex
    {
        /// @brief Constructs the tag.
        explicit constexpr InPlaceIndex() noexcept { }
    };
}// namespace NGIN::Utilities
