/// @file ErrorPolicy.hpp
/// @brief Shared Unicode conversion policies and error codes.
#pragma once

namespace NGIN::Text::Unicode
{
    /// @brief Controls how Unicode conversion and iteration handle malformed input.
    enum class ErrorPolicy
    {
        /// @brief Stop at the first malformed sequence and report an error.
        Strict,
        /// @brief Substitute malformed input with the Unicode replacement character.
        Replace,
        /// @brief Drop malformed input and continue with the next decodable unit.
        Skip
    };

    /// @brief Categorizes Unicode decoding and conversion failures.
    enum class EncodingError
    {
        /// @brief No error occurred.
        None,
        /// @brief The input ended before a complete encoded sequence was available.
        UnexpectedEnd,
        /// @brief The byte or code-unit pattern is not valid for the encoding.
        InvalidSequence,
        /// @brief The encoded sequence uses more units than required for the decoded value.
        OverlongSequence,
        /// @brief The decoded value is a UTF surrogate code point where it is not allowed.
        SurrogateCodePoint,
        /// @brief The decoded value exceeds the Unicode maximum code point.
        CodePointTooLarge,
        /// @brief A UTF-16 high or low surrogate was not paired correctly.
        UnpairedSurrogate
    };
}// namespace NGIN::Text::Unicode
