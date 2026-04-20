#pragma once

#include <NGIN/Primitives.hpp>

#include <type_traits>

namespace NGIN::Utilities
{
    enum class ErrorDomain : UInt16
    {
        None = 0,
        Generic,
        Async,
        IO,
        Net,
        Serialization,
        Core,
    };

    struct ErrorInfo final
    {
        ErrorDomain domain {ErrorDomain::None};
        UInt32      code {0};
        Int32       native {0};

        constexpr ErrorInfo() noexcept = default;

        template<typename T>
            requires(std::is_enum_v<T>)
        constexpr ErrorInfo(ErrorDomain errorDomain, T errorCode, Int32 nativeCode = 0) noexcept
            : domain(errorDomain)
            , code(static_cast<UInt32>(errorCode))
            , native(nativeCode)
        {
        }
    };
}// namespace NGIN::Utilities
