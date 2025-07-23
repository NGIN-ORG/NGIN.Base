#pragma once
#include <NGIN/Primitives.hpp>
#include <type_traits>

namespace NGIN
{
    namespace Meta
    {

        /// <summary>
        /// EnumTraits is a template struct that provides information about an enum type.
        /// Specialize this template for each enum type you want to use with the EnumTraits.
        /// A valid specialization must provide the following static members:
        /// static constexpr NGIN::Size count;
        /// static constexpr std::string_view ToString(T value);
        /// static T FromString(const NGIN::String& name);
        /// </summary>
        template<typename T>
        struct EnumTraits
        {
            // Creates a nice error message when template specialization is missing.
            static_assert(std::is_enum_v<T> && !std::is_enum_v<T>,
                          "EnumTraits specialization is required for this enum type.");
        };
    }// namespace Meta
}// namespace NGIN