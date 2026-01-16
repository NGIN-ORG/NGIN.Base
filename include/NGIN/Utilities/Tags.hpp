#pragma once

#include <cstddef>

namespace NGIN::Utilities
{
    struct nullopt_t
    {
        explicit constexpr nullopt_t(int) noexcept { }
    };

    inline constexpr nullopt_t nullopt {0};

    template <class T>
    struct InPlaceType
    {
        explicit constexpr InPlaceType() noexcept { }
    };

    template <std::size_t I>
    struct InPlaceIndex
    {
        explicit constexpr InPlaceIndex() noexcept { }
    };
}
