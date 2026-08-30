/// @file String.hpp
/// @brief Facade header for NGIN text string aliases.
#pragma once

#include <NGIN/Text/BasicString.hpp>

#include <string_view>

namespace NGIN::Text
{
    // These aliases intentionally share the same container implementation while expressing different text contracts.
    using String  = BasicString<char, 32, NGIN::Memory::SystemAllocator, DefaultGrowthPolicy>;
    using WString = BasicString<wchar_t, 32, NGIN::Memory::SystemAllocator, DefaultGrowthPolicy>;

    // UTF aliases are code-unit containers; validation and conversion live in NGIN::Text::Unicode.
    using UTF8String  = BasicString<char8_t, 32, NGIN::Memory::SystemAllocator, DefaultGrowthPolicy>;
    using UTF16String = BasicString<char16_t, 32, NGIN::Memory::SystemAllocator, DefaultGrowthPolicy>;
    using UTF32String = BasicString<char32_t, 32, NGIN::Memory::SystemAllocator, DefaultGrowthPolicy>;

    /// @brief Exposes UTF-8 code units as bytes without copying.
    /// @details Character types may inspect any object representation, so this view is safe while
    ///          the source view remains alive and unchanged.
    [[nodiscard]] inline std::string_view AsBytes(std::u8string_view text) noexcept
    {
        return std::string_view {reinterpret_cast<const char*>(text.data()), text.size()};
    }

    /// @brief Copies an explicitly byte-oriented UTF-8 representation into `char8_t` storage.
    /// @details Validation remains the caller's responsibility; use `Text::Unicode` when the
    ///          byte sequence is not already known to be UTF-8.
    [[nodiscard]] inline UTF8String UTF8FromBytes(std::string_view bytes)
    {
        UTF8String result;
        result.ReserveExact(bytes.size());
        for (const char byte: bytes)
            result.Append(static_cast<char8_t>(static_cast<unsigned char>(byte)));
        return result;
    }

#if defined(NGIN_PLATFORM_WINDOWS) || defined(NGIN_PLATFORM_XBOX)
    using NativeString = WString;
#else
    using NativeString = String;
#endif
}// namespace NGIN::Text
