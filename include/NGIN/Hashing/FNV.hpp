// FNV.hpp
// Implements FNV-1a 32-bit and 64-bit hash functions in NGIN::Hashing
#pragma once

#include <NGIN/Primitives.hpp>
#include <string_view>

namespace NGIN::Hashing
{
    /// @brief Compute FNV-1a 32-bit hash.
    /// @details Uses FNV-1a algorithm with default offset basis and prime.
    /// @param data Pointer to input data buffer (as bytes).
    /// @param len Number of bytes to process.
    /// @param offset Initial offset basis (default 2166136261U).
    /// @param prime FNV prime (default 16777619U).
    /// @return 32-bit FNV-1a hash.
    constexpr UInt32 FNV1a32(const UInt8* data, UIntSize len,
                             UInt32 offset = 2166136261u,
                             UInt32 prime  = 16777619u) noexcept
    {
        UInt32 hash = offset;
        for (UIntSize i = 0; i < len; ++i)
            hash = (hash ^ data[i]) * prime;
        return hash;
    }

    /// @brief Compute FNV-1a 32-bit hash for a char buffer.
    /// @details Convenience overload for C-style strings using static_cast.
    constexpr UInt32 FNV1a32(const char* data, UIntSize len,
                             UInt32 offset = 2166136261u,
                             UInt32 prime  = 16777619u) noexcept
    {
        UInt32 hash = offset;
        for (UIntSize i = 0; i < len; ++i)
            hash = (hash ^ static_cast<UInt8>(data[i])) * prime;
        return hash;
    }

    /// @brief Compute FNV-1a 32-bit hash for a string_view.
    /// @details Convenience overload for std::string_view.
    constexpr UInt32 FNV1a32(std::string_view sv,
                             UInt32 offset = 2166136261u,
                             UInt32 prime  = 16777619u) noexcept
    {
        UInt32 hash = offset;
        for (UIntSize i = 0; i < sv.size(); ++i)
            hash = (hash ^ static_cast<UInt8>(sv[i])) * prime;
        return hash;
    }

    /// @brief Compute FNV-1a 64-bit hash.
    /// @details Uses FNV-1a algorithm with default offset basis and prime.
    /// @param data Pointer to input data buffer (as bytes).
    /// @param len Number of bytes to process.
    /// @param offset Initial offset basis (default 14695981039346656037ULL).
    /// @param prime FNV prime (default 1099511628211ULL).
    /// @return 64-bit FNV-1a hash.
    constexpr UInt64 FNV1a64(const UInt8* data, UIntSize len,
                             UInt64 offset = 14695981039346656037ull,
                             UInt64 prime  = 1099511628211ull) noexcept
    {
        UInt64 hash = offset;
        for (UIntSize i = 0; i < len; ++i)
            hash = (hash ^ data[i]) * prime;
        return hash;
    }

    /// @brief Compute FNV-1a 64-bit hash for a char buffer.
    /// @details Convenience overload for C-style strings using static_cast.
    constexpr UInt64 FNV1a64(const char* data, UIntSize len,
                             UInt64 offset = 14695981039346656037ull,
                             UInt64 prime  = 1099511628211ull) noexcept
    {
        UInt64 hash = offset;
        for (UIntSize i = 0; i < len; ++i)
            hash = (hash ^ static_cast<UInt8>(data[i])) * prime;
        return hash;
    }

    /// @brief Compute FNV-1a 64-bit hash for a string_view.
    /// @details Convenience overload for std::string_view.
    constexpr UInt64 FNV1a64(std::string_view sv,
                             UInt64 offset = 14695981039346656037ull,
                             UInt64 prime  = 1099511628211ull) noexcept
    {
        UInt64 hash = offset;
        for (UIntSize i = 0; i < sv.size(); ++i)
            hash = (hash ^ static_cast<UInt8>(sv[i])) * prime;
        return hash;
    }
}// namespace NGIN::Hashing
