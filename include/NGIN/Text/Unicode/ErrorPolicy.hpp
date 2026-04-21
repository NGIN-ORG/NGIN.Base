/// @file ErrorPolicy.hpp
/// @brief Shared Unicode conversion policies and error codes.
#pragma once

namespace NGIN::Text::Unicode
{
    enum class ErrorPolicy
    {
        Strict,
        Replace,
        Skip
    };

    enum class EncodingError
    {
        None,
        UnexpectedEnd,
        InvalidSequence,
        OverlongSequence,
        SurrogateCodePoint,
        CodePointTooLarge,
        UnpairedSurrogate
    };
}// namespace NGIN::Text::Unicode
