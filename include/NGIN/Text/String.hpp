/// @file String.hpp
/// @brief Facade header for NGIN text string aliases.
#pragma once

#include <NGIN/Text/BasicString.hpp>

namespace NGIN::Text
{
    using String      = BasicString<char, 48, NGIN::Memory::SystemAllocator, DefaultGrowthPolicy>;
    using WString     = BasicString<wchar_t, 48, NGIN::Memory::SystemAllocator, DefaultGrowthPolicy>;
    using AnsiString  = BasicString<char, 16, NGIN::Memory::SystemAllocator, DefaultGrowthPolicy>;
    using AsciiString = BasicString<char, 16, NGIN::Memory::SystemAllocator, DefaultGrowthPolicy>;

    using UTF8String  = BasicString<char, 48, NGIN::Memory::SystemAllocator, DefaultGrowthPolicy>;
    using UTF16String = BasicString<char16_t, 48, NGIN::Memory::SystemAllocator, DefaultGrowthPolicy>;
    using UTF32String = BasicString<char32_t, 48, NGIN::Memory::SystemAllocator, DefaultGrowthPolicy>;

#if defined(NGIN_PLATFORM_WINDOWS) || defined(NGIN_PLATFORM_XBOX)
    using NativeString = WString;
#else
    using NativeString = String;
#endif
}// namespace NGIN::Text
