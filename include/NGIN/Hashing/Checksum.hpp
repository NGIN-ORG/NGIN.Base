// Checksum.hpp
// Implements various checksum algorithms in NGIN::Hashing
#pragma once

#include <NGIN/Primitives.hpp>
#include <string_view>

namespace NGIN::Hashing
{
    /// @brief Compute BSD checksum (16-bit, sum with circular rotation).
    constexpr UInt16 BSDChecksum(const UInt8* data, UIntSize len) noexcept
    {
        UInt16 sum = 0;
        for (UIntSize i = 0; i < len; ++i)
            sum = (sum >> 1) + ((sum & 1) << 15) + data[i];
        return sum;
    }
    constexpr UInt16 BSDChecksum(const char* data, UIntSize len) noexcept
    {
        UInt16 sum = 0;
        for (UIntSize i = 0; i < len; ++i)
            sum = (sum >> 1) + ((sum & 1) << 15) + static_cast<UInt8>(data[i]);
        return sum;
    }
    constexpr UInt16 BSDChecksum(std::string_view sv) noexcept
    {
        return BSDChecksum(sv.data(), sv.size());
    }

    /// @brief Compute SYSV checksum (16-bit, sum with circular rotation).
    constexpr UInt16 SYSVChecksum(const UInt8* data, UIntSize len) noexcept
    {
        UInt32 sum = 0;
        for (UIntSize i = 0; i < len; ++i)
            sum += data[i];
        sum = (sum & 0xFFFF) + (sum >> 16);
        sum = (sum & 0xFFFF) + (sum >> 16);
        return static_cast<UInt16>(sum & 0xFFFF);
    }
    constexpr UInt16 SYSVChecksum(const char* data, UIntSize len) noexcept
    {
        UInt32 sum = 0;
        for (UIntSize i = 0; i < len; ++i)
            sum += static_cast<UInt8>(data[i]);
        sum = (sum & 0xFFFF) + (sum >> 16);
        sum = (sum & 0xFFFF) + (sum >> 16);
        return static_cast<UInt16>(sum & 0xFFFF);
    }
    constexpr UInt16 SYSVChecksum(std::string_view sv) noexcept
    {
        return SYSVChecksum(sv.data(), sv.size());
    }

    /// @brief Compute sum8 (8-bit sum).
    constexpr UInt8 Sum8(const UInt8* data, UIntSize len) noexcept
    {
        UInt8 sum = 0;
        for (UIntSize i = 0; i < len; ++i)
            sum += data[i];
        return sum;
    }
    constexpr UInt8 Sum8(const char* data, UIntSize len) noexcept
    {
        UInt8 sum = 0;
        for (UIntSize i = 0; i < len; ++i)
            sum += static_cast<UInt8>(data[i]);
        return sum;
    }
    constexpr UInt8 Sum8(std::string_view sv) noexcept
    {
        return Sum8(sv.data(), sv.size());
    }

    /// @brief Compute Internet Checksum (16-bit ones' complement sum).
    constexpr UInt16 InternetChecksum(const UInt8* data, UIntSize len) noexcept
    {
        UInt32 sum = 0;
        for (UIntSize i = 0; i + 1 < len; i += 2)
            sum += (static_cast<UInt16>(data[i]) << 8) | data[i + 1];
        if (len & 1)
            sum += static_cast<UInt16>(data[len - 1]) << 8;
        while (sum >> 16)
            sum = (sum & 0xFFFF) + (sum >> 16);
        return static_cast<UInt16>(~sum);
    }
    constexpr UInt16 InternetChecksum(const char* data, UIntSize len) noexcept
    {
        UInt32 sum = 0;
        for (UIntSize i = 0; i + 1 < len; i += 2)
            sum += (static_cast<UInt16>(static_cast<UInt8>(data[i])) << 8) | static_cast<UInt8>(data[i + 1]);
        if (len & 1)
            sum += static_cast<UInt16>(static_cast<UInt8>(data[len - 1])) << 8;
        while (sum >> 16)
            sum = (sum & 0xFFFF) + (sum >> 16);
        return static_cast<UInt16>(~sum);
    }
    constexpr UInt16 InternetChecksum(std::string_view sv) noexcept
    {
        return InternetChecksum(sv.data(), sv.size());
    }

    /// @brief Compute sum24 (24-bit sum).
    constexpr UInt32 Sum24(const UInt8* data, UIntSize len) noexcept
    {
        UInt32 sum = 0;
        for (UIntSize i = 0; i < len; ++i)
            sum += data[i];
        return sum & 0xFFFFFF;
    }
    constexpr UInt32 Sum24(const char* data, UIntSize len) noexcept
    {
        UInt32 sum = 0;
        for (UIntSize i = 0; i < len; ++i)
            sum += static_cast<UInt8>(data[i]);
        return sum & 0xFFFFFF;
    }
    constexpr UInt32 Sum24(std::string_view sv) noexcept
    {
        return Sum24(sv.data(), sv.size());
    }

    /// @brief Compute sum32 (32-bit sum).
    constexpr UInt32 Sum32(const UInt8* data, UIntSize len) noexcept
    {
        UInt32 sum = 0;
        for (UIntSize i = 0; i < len; ++i)
            sum += data[i];
        return sum;
    }
    constexpr UInt32 Sum32(const char* data, UIntSize len) noexcept
    {
        UInt32 sum = 0;
        for (UIntSize i = 0; i < len; ++i)
            sum += static_cast<UInt8>(data[i]);
        return sum;
    }
    constexpr UInt32 Sum32(std::string_view sv) noexcept
    {
        return Sum32(sv.data(), sv.size());
    }

    /// @brief Compute fletcher-4 (4-bit sum).
    constexpr UInt8 Fletcher4(const UInt8* data, UIntSize len) noexcept
    {
        UInt8 sum1 = 0, sum2 = 0;
        for (UIntSize i = 0; i < len; ++i)
        {
            sum1 = (sum1 + (data[i] & 0xF)) % 0xF;
            sum2 = (sum2 + sum1) % 0xF;
        }
        return (sum2 << 4) | sum1;
    }
    constexpr UInt8 Fletcher4(const char* data, UIntSize len) noexcept
    {
        UInt8 sum1 = 0, sum2 = 0;
        for (UIntSize i = 0; i < len; ++i)
        {
            sum1 = (sum1 + (static_cast<UInt8>(data[i]) & 0xF)) % 0xF;
            sum2 = (sum2 + sum1) % 0xF;
        }
        return (sum2 << 4) | sum1;
    }
    constexpr UInt8 Fletcher4(std::string_view sv) noexcept
    {
        return Fletcher4(sv.data(), sv.size());
    }

    /// @brief Compute fletcher-8 (8-bit sum).
    constexpr UInt8 Fletcher8(const UInt8* data, UIntSize len) noexcept
    {
        UInt8 sum1 = 0, sum2 = 0;
        for (UIntSize i = 0; i < len; ++i)
        {
            sum1 = (sum1 + data[i]) % 0xFF;
            sum2 = (sum2 + sum1) % 0xFF;
        }
        return (sum2 << 4) | sum1;
    }
    constexpr UInt8 Fletcher8(const char* data, UIntSize len) noexcept
    {
        UInt8 sum1 = 0, sum2 = 0;
        for (UIntSize i = 0; i < len; ++i)
        {
            sum1 = (sum1 + static_cast<UInt8>(data[i])) % 0xFF;
            sum2 = (sum2 + sum1) % 0xFF;
        }
        return (sum2 << 4) | sum1;
    }
    constexpr UInt8 Fletcher8(std::string_view sv) noexcept
    {
        return Fletcher8(sv.data(), sv.size());
    }

    /// @brief Compute fletcher-16 (16-bit sum).
    constexpr UInt16 Fletcher16(const UInt8* data, UIntSize len) noexcept
    {
        UInt16 sum1 = 0, sum2 = 0;
        for (UIntSize i = 0; i < len; ++i)
        {
            sum1 = (sum1 + data[i]) % 0xFF;
            sum2 = (sum2 + sum1) % 0xFF;
        }
        return (sum2 << 8) | sum1;
    }
    constexpr UInt16 Fletcher16(const char* data, UIntSize len) noexcept
    {
        UInt16 sum1 = 0, sum2 = 0;
        for (UIntSize i = 0; i < len; ++i)
        {
            sum1 = (sum1 + static_cast<UInt8>(data[i])) % 0xFF;
            sum2 = (sum2 + sum1) % 0xFF;
        }
        return (sum2 << 8) | sum1;
    }
    constexpr UInt16 Fletcher16(std::string_view sv) noexcept
    {
        return Fletcher16(sv.data(), sv.size());
    }

    /// @brief Compute fletcher-32 (32-bit sum).
    constexpr UInt32 Fletcher32(const UInt8* data, UIntSize len) noexcept
    {
        UInt32 sum1 = 0, sum2 = 0;
        for (UIntSize i = 0; i < len; ++i)
        {
            sum1 = (sum1 + data[i]) % 0xFFFF;
            sum2 = (sum2 + sum1) % 0xFFFF;
        }
        return (sum2 << 16) | sum1;
    }
    constexpr UInt32 Fletcher32(const char* data, UIntSize len) noexcept
    {
        UInt32 sum1 = 0, sum2 = 0;
        for (UIntSize i = 0; i < len; ++i)
        {
            sum1 = (sum1 + static_cast<UInt8>(data[i])) % 0xFFFF;
            sum2 = (sum2 + sum1) % 0xFFFF;
        }
        return (sum2 << 16) | sum1;
    }
    constexpr UInt32 Fletcher32(std::string_view sv) noexcept
    {
        return Fletcher32(sv.data(), sv.size());
    }

    /// @brief Compute Adler-32 checksum.
    constexpr UInt32 Adler32(const UInt8* data, UIntSize len) noexcept
    {
        UInt32 a = 1, b = 0;
        for (UIntSize i = 0; i < len; ++i)
        {
            a = (a + data[i]) % 65521;
            b = (b + a) % 65521;
        }
        return (b << 16) | a;
    }
    constexpr UInt32 Adler32(const char* data, UIntSize len) noexcept
    {
        UInt32 a = 1, b = 0;
        for (UIntSize i = 0; i < len; ++i)
        {
            a = (a + static_cast<UInt8>(data[i])) % 65521;
            b = (b + a) % 65521;
        }
        return (b << 16) | a;
    }
    constexpr UInt32 Adler32(std::string_view sv) noexcept
    {
        return Adler32(sv.data(), sv.size());
    }

    /// @brief Compute xor8 (8-bit xor sum).
    constexpr UInt8 Xor8(const UInt8* data, UIntSize len) noexcept
    {
        UInt8 x = 0;
        for (UIntSize i = 0; i < len; ++i)
            x ^= data[i];
        return x;
    }
    constexpr UInt8 Xor8(const char* data, UIntSize len) noexcept
    {
        UInt8 x = 0;
        for (UIntSize i = 0; i < len; ++i)
            x ^= static_cast<UInt8>(data[i]);
        return x;
    }
    constexpr UInt8 Xor8(std::string_view sv) noexcept
    {
        return Xor8(sv.data(), sv.size());
    }

    // Luhn, Verhoeff, and Damm algorithms are for decimal digit strings only.
    /// @brief Compute Luhn checksum digit (mod 10) for a decimal string.
    constexpr UInt8 Luhn(const char* digits, UIntSize len) noexcept
    {
        int sum  = 0;
        bool alt = false;
        for (IntSize i = static_cast<IntSize>(len) - 1; i >= 0; --i)
        {
            int d = digits[i] - '0';
            if (d < 0 || d > 9)
                return 0xFF;// invalid input
            if (alt)
            {
                d *= 2;
                if (d > 9)
                    d -= 9;
            }
            sum += d;
            alt = !alt;
        }
        return static_cast<UInt8>((10 - (sum % 10)) % 10);
    }

    // Verhoeff algorithm tables at namespace scope for constexpr compatibility
    namespace Detail
    {
        inline constexpr UInt8 Verhoeff_d[10][10] = {
                {0, 1, 2, 3, 4, 5, 6, 7, 8, 9},
                {1, 2, 3, 4, 0, 6, 7, 8, 9, 5},
                {2, 3, 4, 0, 1, 7, 8, 9, 5, 6},
                {3, 4, 0, 1, 2, 8, 9, 5, 6, 7},
                {4, 0, 1, 2, 3, 9, 5, 6, 7, 8},
                {5, 9, 8, 7, 6, 0, 4, 3, 2, 1},
                {6, 5, 9, 8, 7, 1, 0, 4, 3, 2},
                {7, 6, 5, 9, 8, 2, 1, 0, 4, 3},
                {8, 7, 6, 5, 9, 3, 2, 1, 0, 4},
                {9, 8, 7, 6, 5, 4, 3, 2, 1, 0}};
        inline constexpr UInt8 Verhoeff_p[8][10] = {
                {0, 1, 2, 3, 4, 5, 6, 7, 8, 9},
                {1, 5, 7, 6, 2, 8, 3, 0, 9, 4},
                {5, 8, 0, 3, 7, 9, 6, 1, 4, 2},
                {8, 9, 1, 6, 0, 4, 3, 5, 2, 7},
                {9, 4, 5, 3, 1, 2, 6, 8, 7, 0},
                {4, 2, 8, 6, 5, 7, 3, 9, 0, 1},
                {2, 7, 9, 3, 8, 0, 6, 4, 1, 5},
                {7, 0, 4, 6, 9, 1, 3, 2, 5, 8}};
        inline constexpr UInt8 Verhoeff_inv[10] = {0, 4, 3, 2, 1, 5, 6, 7, 8, 9};
    }// namespace Detail
    constexpr UInt8 Verhoeff(const char* digits, UIntSize len) noexcept
    {
        UInt8 c = 0;
        for (UIntSize i = 0; i < len; ++i)
            c = Detail::Verhoeff_d[c][Detail::Verhoeff_p[(len - i) % 8][digits[i] - '0']];
        return Detail::Verhoeff_inv[c];
    }

    // Damm algorithm table at namespace scope for constexpr compatibility
    namespace Detail
    {
        inline constexpr UInt8 Damm_table[10][10] = {
                {0, 3, 1, 7, 5, 9, 8, 6, 4, 2},
                {7, 0, 9, 2, 1, 5, 4, 8, 6, 3},
                {4, 2, 0, 6, 8, 7, 1, 3, 5, 9},
                {1, 7, 5, 0, 9, 8, 3, 4, 2, 6},
                {6, 1, 2, 3, 0, 4, 5, 9, 7, 8},
                {3, 6, 7, 4, 2, 0, 9, 5, 8, 1},
                {5, 8, 6, 9, 7, 2, 0, 1, 3, 4},
                {8, 9, 4, 5, 3, 6, 2, 0, 1, 7},
                {9, 4, 3, 8, 6, 1, 7, 2, 0, 5},
                {2, 5, 8, 1, 4, 3, 6, 7, 9, 0}};
    }
    constexpr UInt8 Damm(const char* digits, UIntSize len) noexcept
    {
        UInt8 interim = 0;
        for (UIntSize i = 0; i < len; ++i)
        {
            int d = digits[i] - '0';
            if (d < 0 || d > 9)
                return 0xFF;// invalid input
            interim = Detail::Damm_table[interim][d];
        }
        return interim;
    }
}// namespace NGIN::Hashing
