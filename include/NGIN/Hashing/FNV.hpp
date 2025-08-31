// FNV.hpp
// Implements FNV-1a 32-bit and 64-bit hash functions in NGIN::Hashing
#pragma once

#include <NGIN/Primitives.hpp>
#include <string_view>

namespace NGIN::Hashing
{
    ////////////////////////////////////////////////////////////////////////////////
    // FNV-1a 32-bit
    ////////////////////////////////////////////////////////////////////////////////

    /// @brief Compute FNV-1a 32-bit hash (byte buffer).
    /// @tparam Offset Initial offset basis (default 2166136261U).
    /// @tparam Prime  FNV prime (default 16777619U).
    template<UInt32 Offset = 2166136261u, UInt32 Prime = 16777619u>
    constexpr UInt32 FNV1a32(const UInt8* data, UIntSize len) noexcept
    {
        UInt32 hash = Offset;
        for (UIntSize i = 0; i < len; ++i)
            hash = (hash ^ data[i]) * Prime;
        return hash;
    }

    /// @brief Compute FNV-1a 32-bit hash for a char buffer.
    /// @details Convenience overload for C-style strings (casts chars to UInt8).
    template<UInt32 Offset = 2166136261u, UInt32 Prime = 16777619u>
    constexpr UInt32 FNV1a32(const char* data, UIntSize len) noexcept
    {
        UInt32 hash = Offset;
        for (UIntSize i = 0; i < len; ++i)
            hash = (hash ^ static_cast<UInt8>(data[i])) * Prime;
        return hash;
    }

    /// @brief Compute FNV-1a 32-bit hash for a string_view.
    template<UInt32 Offset = 2166136261u, UInt32 Prime = 16777619u>
    constexpr UInt32 FNV1a32(std::string_view sv) noexcept
    {
        UInt32 hash = Offset;
        for (UIntSize i = 0; i < sv.size(); ++i)
            hash = (hash ^ static_cast<UInt8>(sv[i])) * Prime;
        return hash;
    }

    ////////////////////////////////////////////////////////////////////////////////
    // FNV-1a 64-bit
    ////////////////////////////////////////////////////////////////////////////////

    /// @brief Compute FNV-1a 64-bit hash (byte buffer).
    /// @tparam Offset Initial offset basis (default 14695981039346656037ULL).
    /// @tparam Prime  FNV prime (default 1099511628211ULL).
    template<UInt64 Offset = 14695981039346656037ull, UInt64 Prime = 1099511628211ull>
    constexpr UInt64 FNV1a64(const UInt8* data, UIntSize len) noexcept
    {
        UInt64 hash = Offset;
        for (UIntSize i = 0; i < len; ++i)
            hash = (hash ^ data[i]) * Prime;
        return hash;
    }

    /// @brief Compute FNV-1a 64-bit hash for a char buffer.
    /// @details Convenience overload for C-style strings (casts chars to UInt8).
    template<UInt64 Offset = 14695981039346656037ull, UInt64 Prime = 1099511628211ull>
    constexpr UInt64 FNV1a64(const char* data, UIntSize len) noexcept
    {
        UInt64 hash = Offset;
        for (UIntSize i = 0; i < len; ++i)
            hash = (hash ^ static_cast<UInt8>(data[i])) * Prime;
        return hash;
    }

    /// @brief Compute FNV-1a 64-bit hash for a string_view.
    template<UInt64 Offset = 14695981039346656037ull, UInt64 Prime = 1099511628211ull>
    constexpr UInt64 FNV1a64(std::string_view sv) noexcept
    {
        UInt64 hash = Offset;
        for (UIntSize i = 0; i < sv.size(); ++i)
            hash = (hash ^ static_cast<UInt8>(sv[i])) * Prime;
        return hash;
    }
}// namespace NGIN::Hashing
