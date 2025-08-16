// CRC.hpp
// Implements CRC-8, CRC-16, CRC-32, CRC-64 in NGIN::Hashing
#pragma once

#include <NGIN/Primitives.hpp>
#include <array>

namespace NGIN::Hashing
{
    // Generic compile-time CRC engine with optional table and slicing acceleration.
    namespace CRCDetail
    {
        template<typename T>
        constexpr T BitReverse(T v) noexcept
        {
            constexpr unsigned Bits = sizeof(T) * 8;
            T r                     = 0;
            for (unsigned i = 0; i < Bits; ++i)
            {
                r <<= 1;
                r |= (v & 1);
                v >>= 1;
            }
            return r;
        }

        template<typename T, T Poly, T Init, bool RefIn, bool RefOut, T XorOut, unsigned Slices = 1, bool UseTable = true>
        struct CRCSpec
        {
            using value_type               = T;
            using array_type               = std::array<T, 256>;
            static constexpr unsigned Bits = sizeof(T) * 8;

            // Safe shift-right-by-8 that avoids triggering warnings for 8-bit CRCs.
            static constexpr T ShiftRight8(T v) noexcept
            {
                if constexpr (Bits > 8)
                    return static_cast<T>(v >> 8);
                else
                    return static_cast<T>(0);
            }

            // Single-table generation (RefIn dictates direction)
            static constexpr array_type MakeTable0()
            {
                array_type t {};
                for (unsigned n = 0; n < 256; ++n)
                {
                    T crc = static_cast<T>(n);
                    if constexpr (!RefIn)
                    {
                        if constexpr (Bits > 8)
                            crc <<= (Bits - 8);// avoid shifting by full width (undefined) for 8-bit CRCs
                    }
                    for (int k = 0; k < 8; ++k)
                    {
                        if constexpr (RefIn)
                            crc = (crc & static_cast<T>(1)) ? static_cast<T>((crc >> 1) ^ Poly) : static_cast<T>(crc >> 1);
                        else
                            crc = (crc & (static_cast<T>(1) << (Bits - 1))) ? static_cast<T>((crc << 1) ^ Poly) : static_cast<T>(crc << 1);
                    }
                    t[n] = crc;
                }
                return t;
            }

            static constexpr auto table0 = MakeTable0();

            // Additional tables for slicing-by-4 (RefIn only, reflected algorithm) - tables[0]..tables[3]
            static constexpr auto MakeSlicingTable()
            {
                if constexpr (Slices == 1)
                {
                    return std::array<array_type, 1> {table0};
                }
                else
                {
                    static_assert(RefIn, "Slicing implementation assumes reflected input (RefIn=true)");
                    std::array<array_type, Slices> tabs {};
                    tabs[0] = table0;
                    for (unsigned s = 1; s < Slices; ++s)
                    {
                        for (unsigned n = 0; n < 256; ++n)
                        {
                            T prev     = tabs[s - 1][n];
                            tabs[s][n] = static_cast<T>(tabs[0][prev & 0xFF] ^ ShiftRight8(prev));
                        }
                    }
                    return tabs;
                }
            }
            static constexpr auto tables = MakeSlicingTable();

            static constexpr T Initial() noexcept
            {
                return Init;
            }

            static constexpr T UpdateByteTable(T crc, UInt8 byte) noexcept
            {
                if constexpr (Bits == 8)
                {
                    // For 8-bit CRCs the shift operations would discard the entire register; the table already encodes update.
                    if constexpr (RefIn)
                        return static_cast<T>(tables[0][(crc ^ byte) & 0xFF]);
                    else
                        return static_cast<T>(tables[0][(crc ^ byte) & 0xFF]);
                }
                else
                {
                    if constexpr (RefIn)
                    {
                        return static_cast<T>(tables[0][(crc ^ byte) & 0xFF] ^ ShiftRight8(crc));
                    }
                    else
                    {
                        return static_cast<T>(tables[0][((crc >> (Bits - 8)) ^ byte) & 0xFF] ^ (crc << 8));
                    }
                }
            }

            static constexpr T UpdateBlockSlicing(T crc, const UInt8* data, UIntSize len) noexcept
            {
                if constexpr (Slices == 1)
                {
                    for (UIntSize i = 0; i < len; ++i)
                        crc = UpdateByteTable(crc, data[i]);
                    return crc;
                }
                else
                {
                    // Slicing-by-4 path (processing 4 bytes at a time). Valid for reflected algorithms only.
                    static_assert(sizeof(T) >= 4, "Slicing-by-4 only enabled for 32/64-bit CRC variants");
                    constexpr unsigned Group = 4;
                    while (len >= Group)
                    {
                        if constexpr (sizeof(T) == 4)
                        {
                            // Combine 4 bytes then apply 4 lookup tables.
                            crc ^= static_cast<T>(data[0]) | (static_cast<T>(data[1]) << 8) | (static_cast<T>(data[2]) << 16) | (static_cast<T>(data[3]) << 24);
                            T tmp = static_cast<T>(tables[3][(crc) & 0xFF] ^
                                                   tables[2][(crc >> 8) & 0xFF] ^
                                                   tables[1][(crc >> 16) & 0xFF] ^
                                                   tables[0][(crc >> 24) & 0xFF]);
                            crc   = tmp;
                        }
                        else// 64-bit (process 4 bytes; remaining high bits shift naturally)
                        {
                            crc ^= static_cast<T>(static_cast<T>(data[0]) | (static_cast<T>(data[1]) << 8) | (static_cast<T>(data[2]) << 16) | (static_cast<T>(data[3]) << 24));
                            T part = static_cast<T>(tables[3][(crc) & 0xFF] ^
                                                    tables[2][(crc >> 8) & 0xFF] ^
                                                    tables[1][(crc >> 16) & 0xFF] ^
                                                    tables[0][(crc >> 24) & 0xFF]);
                            crc    = static_cast<T>(part ^ (crc >> 32));
                        }
                        data += Group;
                        len -= Group;
                    }
                    for (UIntSize i = 0; i < len; ++i)
                        crc = UpdateByteTable(crc, data[i]);
                    return crc;
                }
            }

            static constexpr T Update(T crc, const UInt8* data, UIntSize len) noexcept
            {
                if (!data || len == 0)
                    return crc;
                if constexpr (UseTable)
                {
                    return UpdateBlockSlicing(crc, data, len);
                }
                else
                {
                    // Bit-at-a-time fallback.
                    for (UIntSize i = 0; i < len; ++i)
                    {
                        if constexpr (RefIn)
                        {
                            crc ^= data[i];
                            for (int b = 0; b < 8; ++b)
                                crc = (crc & 1) ? static_cast<T>((crc >> 1) ^ Poly) : static_cast<T>(crc >> 1);
                        }
                        else
                        {
                            crc ^= static_cast<T>(data[i]) << (Bits - 8);
                            for (int b = 0; b < 8; ++b)
                                crc = (crc & (static_cast<T>(1) << (Bits - 1))) ? static_cast<T>((crc << 1) ^ Poly) : static_cast<T>(crc << 1);
                        }
                    }
                    return crc;
                }
            }

            static constexpr T Finalize(T crc) noexcept
            {
                if constexpr (RefIn != RefOut)
                    crc = BitReverse(crc);
                return static_cast<T>(crc ^ XorOut);
            }

            static constexpr T Compute(const UInt8* data, UIntSize len) noexcept
            {
                return Finalize(Update(Initial(), data, len));
            }
        };

        // Aliases for specific variants (select slicing for high-throughput ones)
        using CRC8_SMBUS     = CRCSpec<UInt8, 0x07, 0x00, false, false, 0x00>;
        using CRC8_MAXIM     = CRCSpec<UInt8, 0x8C, 0x00, true, true, 0x00>;
        using CRC8_AUTOSAR   = CRCSpec<UInt8, 0x2F, 0xFF, false, false, 0xFF>;
        using CRC8_SAE_J1850 = CRCSpec<UInt8, 0x1D, 0xFF, false, false, 0xFF>;
        using CRC8_BLUETOOTH = CRCSpec<UInt8, 0xE5, 0x00, true, true, 0x00>;

        using CRC16_CCITT_FALSE  = CRCSpec<UInt16, 0x1021, 0xFFFF, false, false, 0x0000>;
        using CRC16_ARC          = CRCSpec<UInt16, 0xA001, 0x0000, true, true, 0x0000>;
        using CRC16_IBM_3740     = CRCSpec<UInt16, 0x1021, 0xFFFF, false, false, 0x0000>;
        using CRC16_XMODEM       = CRCSpec<UInt16, 0x1021, 0x0000, false, false, 0x0000>;
        using CRC16_KERMIT       = CRCSpec<UInt16, 0x8408, 0x0000, true, true, 0x0000>;
        using CRC16_MODBUS       = CRCSpec<UInt16, 0xA001, 0xFFFF, true, true, 0x0000>;
        using CRC16_IBM_SDLC     = CRCSpec<UInt16, 0x8408, 0xFFFF, true, true, 0xFFFF>;
        using CRC16_GENIBUS      = CRCSpec<UInt16, 0x1021, 0xFFFF, false, false, 0xFFFF>;
        using CRC16_USB          = CRCSpec<UInt16, 0xA001, 0xFFFF, true, true, 0xFFFF>;
        using CRC16_MAXIM        = CRCSpec<UInt16, 0xA001, 0x0000, true, true, 0xFFFF>;
        using CRC16_MCRF4XX      = CRCSpec<UInt16, 0x8408, 0xFFFF, true, true, 0x0000>;
        using CRC16_DNP          = CRCSpec<UInt16, 0xA6BC, 0x0000, true, true, 0xFFFF>;// reflected poly
        using CRC16_EN_13757     = CRCSpec<UInt16, 0x3D65, 0x0000, false, false, 0xFFFF>;
        using CRC16_DECT_R       = CRCSpec<UInt16, 0x0589, 0x0000, false, false, 0x0001>;
        using CRC16_DECT_X       = CRCSpec<UInt16, 0x0589, 0x0000, false, false, 0x0000>;
        using CRC16_UMTS         = CRCSpec<UInt16, 0x8005, 0x0000, false, false, 0x0000>;
        using CRC16_T10_DIF      = CRCSpec<UInt16, 0x8BB7, 0x0000, false, false, 0x0000>;
        using CRC16_PROFIBUS     = CRCSpec<UInt16, 0x1DCF, 0xFFFF, false, false, 0xFFFF>;
        using CRC16_LJ1200       = CRCSpec<UInt16, 0x6F63, 0x0000, false, false, 0x0000>;
        using CRC16_OPENSAFETY_A = CRCSpec<UInt16, 0x5935, 0x0000, false, false, 0x0000>;
        using CRC16_OPENSAFETY_B = CRCSpec<UInt16, 0x755B, 0x0000, false, false, 0x0000>;
        using CRC16_NRSC_5       = CRCSpec<UInt16, 0xD010, 0xFFFF, true, true, 0x0000>;
        using CRC16_CMS          = CRCSpec<UInt16, 0x8005, 0xFFFF, false, false, 0x0000>;
        using CRC16_DDS_110      = CRCSpec<UInt16, 0x8005, 0x800D, false, false, 0x0000>;
        using CRC16_M17          = CRCSpec<UInt16, 0x5935, 0xFFFF, false, false, 0x0000>;
        using CRC16_TELEDISK     = CRCSpec<UInt16, 0xA097, 0x0000, false, false, 0x0000>;
        using CRC16_TMS37157     = CRCSpec<UInt16, 0x8408, 0x3791, true, true, 0x0000>;

        using CRC32_IEEE       = CRCSpec<UInt32, 0xEDB88320u, 0xFFFFFFFFu, true, true, 0xFFFFFFFFu, 4>;
        using CRC32_MPEG2      = CRCSpec<UInt32, 0x04C11DB7u, 0xFFFFFFFFu, false, false, 0x00000000u>;
        using CRC32_AIXM       = CRCSpec<UInt32, 0x814141ABu, 0x00000000u, false, false, 0x00000000u>;
        using CRC32_AUTOSAR    = CRCSpec<UInt32, 0xC8DF352Fu, 0xFFFFFFFFu, true, true, 0xFFFFFFFFu>;
        using CRC32_BASE91_D   = CRCSpec<UInt32, 0xD419CC15u, 0xFFFFFFFFu, true, true, 0xFFFFFFFFu>;
        using CRC32_BZIP2      = CRCSpec<UInt32, 0x04C11DB7u, 0xFFFFFFFFu, false, false, 0xFFFFFFFFu>;
        using CRC32_CD_ROM_EDC = CRCSpec<UInt32, 0xD8018001u, 0x00000000u, true, true, 0x00000000u>;
        using CRC32_CKSUM      = CRCSpec<UInt32, 0x04C11DB7u, 0x00000000u, false, false, 0xFFFFFFFFu>;
        using CRC32_ISCSI      = CRCSpec<UInt32, 0x82F63B78u, 0xFFFFFFFFu, true, true, 0xFFFFFFFFu>;
        using CRC32_ISO_HDLC   = CRC32_IEEE;
        using CRC32_JAMCRC     = CRCSpec<UInt32, 0xEDB88320u, 0xFFFFFFFFu, true, true, 0x00000000u>;
        using CRC32_MEF        = CRCSpec<UInt32, 0xEB31D82Eu, 0xFFFFFFFFu, true, true, 0x00000000u>;
        using CRC32_XFER       = CRCSpec<UInt32, 0x000000AFu, 0x00000000u, false, false, 0x00000000u>;

        using CRC64_ISO  = CRCSpec<UInt64, 0xD800000000000000ULL, 0xFFFFFFFFFFFFFFFFULL, true, true, 0xFFFFFFFFFFFFFFFFULL, 4>;
        using CRC64_ECMA = CRCSpec<UInt64, 0x42F0E1EBA9EA3693ULL, 0x0000000000000000ULL, false, false, 0x0000000000000000ULL>;
    }// namespace CRCDetail
    namespace CRC8
    {
        /// @brief CRC-8/SMBUS (aka CRC-8)
        /// @details poly=0x07, init=0x00, refin=false, refout=false, xorout=0x00
        constexpr UInt8 SMBUS(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC8_SMBUS::Compute(data, len);
        }

        /// @brief CRC-8/MAXIM-DOW (Dallas/Maxim)
        /// @details reflected poly=0x8C (bit-reflected 0x31), init=0x00, refin=true, refout=true, xorout=0x00
        constexpr UInt8 MAXIM_DOW(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC8_MAXIM::Compute(data, len);
        }

        /// @brief CRC-8/AUTOSAR
        /// @details poly=0x2F, init=0xFF, refin=false, refout=false, xorout=0xFF
        constexpr UInt8 AUTOSAR(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC8_AUTOSAR::Compute(data, len);
        }

        /// @brief CRC-8/SAE-J1850
        /// @details poly=0x1D, init=0xFF, refin=false, refout=false, xorout=0xFF
        constexpr UInt8 SAE_J1850(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC8_SAE_J1850::Compute(data, len);
        }

        /// @brief CRC-8/BLUETOOTH
        /// @details reflected poly=0xE5 (bit-reflected 0xA7), init=0x00, refin=true, refout=true, xorout=0x00
        constexpr UInt8 BLUETOOTH(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC8_BLUETOOTH::Compute(data, len);
        }
    }// namespace CRC8

    namespace CRC16
    {
        /// @brief Compute CRC-16/CCITT-FALSE checksum.
        /// @details Implements CRC-16/CCITT-FALSE (poly=0x1021, init=0xFFFF, no reflection, xorout=0x0000).
        constexpr UInt16 CCITT_FALSE(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC16_CCITT_FALSE::Compute(data, len);
        }

        /// @brief CRC-16/ARC (aka CRC-16, CRC-IBM, CRC-16/LHA)
        /// @details reflected poly=0xA001 (bit-reflected 0x8005), init=0x0000, refin=true, refout=true, xorout=0x0000
        constexpr UInt16 ARC(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC16_ARC::Compute(data, len);
        }

        /// @brief CRC-16/IBM-3740 (aka CRC-16/AUTOSAR, CRC-16/CCITT-FALSE)
        /// @details poly=0x1021, init=0xFFFF, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 IBM_3740(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC16_IBM_3740::Compute(data, len);
        }

        /// @brief CRC-16/XMODEM (aka CRC-16/ACORN, CRC-16/LTE, CRC-16/V-41-MSB)
        /// @details poly=0x1021, init=0x0000, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 XMODEM(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC16_XMODEM::Compute(data, len);
        }

        /// @brief CRC-16/KERMIT (aka CRC-16/CCITT, CRC-16/BLUETOOTH)
        /// @details reflected poly=0x8408 (bit-reflected 0x1021), init=0x0000, refin=true, refout=true, xorout=0x0000
        constexpr UInt16 KERMIT(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC16_KERMIT::Compute(data, len);
        }

        /// @brief CRC-16/MODBUS
        /// @details reflected poly=0xA001 (bit-reflected 0x8005), init=0xFFFF, refin=true, refout=true, xorout=0x0000
        constexpr UInt16 MODBUS(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC16_MODBUS::Compute(data, len);
        }

        /// @brief CRC-16/IBM-SDLC (aka CRC-16/X-25, CRC-B)
        /// @details reflected poly=0x8408 (bit-reflected 0x1021), init=0xFFFF, refin=true, refout=true, xorout=0xFFFF
        constexpr UInt16 IBM_SDLC(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC16_IBM_SDLC::Compute(data, len);
        }

        /// @brief CRC-16/GENIBUS (aka CRC-16/DARC, CRC-16/EPC, CRC-16/I-CODE)
        /// @details poly=0x1021, init=0xFFFF, refin=false, refout=false, xorout=0xFFFF
        constexpr UInt16 GENIBUS(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC16_GENIBUS::Compute(data, len);
        }

        /// @brief CRC-16/USB
        /// @details reflected poly=0xA001 (bit-reflected 0x8005), init=0xFFFF, refin=true, refout=true, xorout=0xFFFF
        constexpr UInt16 USB(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC16_USB::Compute(data, len);
        }

        /// @brief CRC-16/MAXIM-DOW
        /// @details reflected poly=0xA001 (bit-reflected 0x8005), init=0x0000, refin=true, refout=true, xorout=0xFFFF
        constexpr UInt16 MAXIM_DOW(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC16_MAXIM::Compute(data, len);
        }

        /// @brief CRC-16/MCRF4XX
        /// @details reflected poly=0x8408 (bit-reflected 0x1021), init=0xFFFF, refin=true, refout=true, xorout=0x0000
        constexpr UInt16 MCRF4XX(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC16_MCRF4XX::Compute(data, len);
        }

        /// @brief CRC-16/DNP
        /// @details poly=0x3D65, init=0x0000, refin=true, refout=true, xorout=0xFFFF
        constexpr UInt16 DNP(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC16_DNP::Compute(data, len);
        }

        /// @brief CRC-16/EN-13757
        /// @details poly=0x3d65, init=0x0000, refin=false, refout=false, xorout=0xFFFF
        constexpr UInt16 EN_13757(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC16_EN_13757::Compute(data, len);
        }

        /// @brief CRC-16/DECT-R
        /// @details poly=0x0589, init=0x0000, refin=false, refout=false, xorout=0x0001
        constexpr UInt16 DECT_R(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC16_DECT_R::Compute(data, len);
        }

        /// @brief CRC-16/DECT-X
        /// @details poly=0x0589, init=0x0000, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 DECT_X(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC16_DECT_X::Compute(data, len);
        }

        /// @brief CRC-16/UMTS
        /// @details poly=0x8005, init=0x0000, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 UMTS(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC16_UMTS::Compute(data, len);
        }

        /// @brief CRC-16/ISO-IEC-14443-3-A (CRC-A)
        /// @details poly=0x1021 (reflected form 0x8408), init=0x6363 (little-endian repr. of 0x6363), refin=true, refout=true, xorout=0x0000
        constexpr UInt16 ISO_IEC_14443_3_A(const UInt8* data, UIntSize len) noexcept
        {
            // ISO/IEC 14443-3-A CRC (CRC-A) uses a custom byte-wise algorithm
            UInt16 crc = 0x6363;
            for (UIntSize i = 0; i < len; ++i)
            {
                UInt8 bt = data[i];
                bt ^= static_cast<UInt8>(crc & 0x00FF);
                bt ^= bt << 4;
                UInt16 bt16 = static_cast<UInt16>(bt);
                crc         = (crc >> 8) ^ (bt16 << 8) ^ (bt16 << 3) ^ (bt16 >> 4);
            }
            return crc;
        }

        /// @brief CRC-16/T10-DIF
        /// @details poly=0x8bb7, init=0x0000, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 T10_DIF(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC16_T10_DIF::Compute(data, len);
        }

        /// @brief CRC-16/PROFIBUS
        /// @details poly=0x1dcf, init=0xFFFF, refin=false, refout=false, xorout=0xFFFF
        constexpr UInt16 PROFIBUS(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC16_PROFIBUS::Compute(data, len);
        }

        /// @brief CRC-16/LJ1200
        /// @details poly=0x6f63, init=0x0000, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 LJ1200(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC16_LJ1200::Compute(data, len);
        }

        /// @brief CRC-16/OPENSAFETY-A
        /// @details poly=0x5935, init=0x0000, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 OPENSAFETY_A(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC16_OPENSAFETY_A::Compute(data, len);
        }

        /// @brief CRC-16/OPENSAFETY-B
        /// @details poly=0x755b, init=0x0000, refin=false, refout=false, xorout=0x0000
        /// @brief CRC-16/OPENSAFETY-B
        /// @details reflected poly=0xDDAA (bit-reflected 0x755B), init=0x0000, refin=true, refout=true, xorout=0x0000
        constexpr UInt16 OPENSAFETY_B(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC16_OPENSAFETY_B::Compute(data, len);
        }

        /// @brief CRC-16/NRSC-5 (reflected)
        /// @details poly=0x080B, init=0xFFFF, refin=true, refout=true, xorout=0x0000
        constexpr UInt16 NRSC_5(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC16_NRSC_5::Compute(data, len);
        }

        /// @brief CRC-16/CMS
        /// @details poly=0x8005, init=0xFFFF, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 CMS(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC16_CMS::Compute(data, len);
        }

        /// @brief CRC-16/DDS-110
        /// @details poly=0x8005, init=0x800d, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 DDS_110(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC16_DDS_110::Compute(data, len);
        }

        /// @brief CRC-16/M17
        /// @details poly=0x5935, init=0xFFFF, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 M17(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC16_M17::Compute(data, len);
        }

        /// @brief CRC-16/TELEDISK
        /// @details poly=0xa097, init=0x0000, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 TELEDISK(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC16_TELEDISK::Compute(data, len);
        }

        /// @brief CRC-16/TMS37157
        /// @details reflected poly=0x8408 (bit-reflected 0x1021), init=0x89EC, refin=true, refout=true, xorout=0x0000
        constexpr UInt16 TMS37157(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC16_TMS37157::Compute(data, len);
        }
    }// namespace CRC16

    namespace CRC32
    {
        /// @brief Compute CRC-32/IEEE-802.3 checksum.
        /// @details Implements CRC-32/IEEE-802.3 (reflected poly=0xEDB88320, init=0xFFFFFFFF,
        ///          input/output reflected, xorout=0xFFFFFFFF).
        constexpr UInt32 IEEE_802_3(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC32_IEEE::Compute(data, len);
        }

        /// @brief Compute CRC-32/MPEG-2 checksum.
        /// @details Implements CRC-32/MPEG-2 (poly=0x04C11DB7, init=0xFFFFFFFF, no reflection, xorout=0x00000000).
        /// @param data Pointer to input data buffer (as bytes).
        /// @param len Number of bytes to process.
        /// @param poly CRC polynomial (default 0x04C11DB7).
        /// @param init Initial CRC value (default 0xFFFFFFFF).
        /// @return CRC-32 checksum.
        constexpr UInt32 MPEG_2(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC32_MPEG2::Compute(data, len);
        }

        // Additional CRC-32 variants
        constexpr UInt32 AIXM(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC32_AIXM::Compute(data, len);
        }
        constexpr UInt32 AUTOSAR(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC32_AUTOSAR::Compute(data, len);
        }
        constexpr UInt32 BASE91_D(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC32_BASE91_D::Compute(data, len);
        }
        constexpr UInt32 BZIP2(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC32_BZIP2::Compute(data, len);
        }
        constexpr UInt32 CD_ROM_EDC(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC32_CD_ROM_EDC::Compute(data, len);
        }
        constexpr UInt32 CKSUM(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC32_CKSUM::Compute(data, len);
        }
        constexpr UInt32 ISCSI(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC32_ISCSI::Compute(data, len);
        }
        constexpr UInt32 ISO_HDLC(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC32_ISO_HDLC::Compute(data, len);
        }
        constexpr UInt32 JAMCRC(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC32_JAMCRC::Compute(data, len);
        }
        constexpr UInt32 MEF(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC32_MEF::Compute(data, len);
        }
        constexpr UInt32 XFER(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC32_XFER::Compute(data, len);
        }
    }// namespace CRC32

    namespace CRC64
    {
        /// @brief Compute CRC-64/ISO-3309 checksum.
        /// @details Implements CRC-64/ISO-3309 (reflected poly=0xD800000000000000, init=0xFFFFFFFFFFFFFFFF,
        ///          input/output reflected, xorout=0xFFFFFFFFFFFFFFFF).
        constexpr UInt64 ISO_3309(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC64_ISO::Compute(data, len);
        }

        /// @brief Compute CRC-64/ECMA-182 checksum.
        /// @details Implements CRC-64/ECMA-182 (poly=0x42F0E1EBA9EA3693, init=0x0000000000000000, no reflection, xorout=0x0000000000000000).
        /// @param data Pointer to input data buffer (as bytes).
        /// @param len Number of bytes to process.
        /// @param poly CRC polynomial (default 0x42F0E1EBA9EA3693).
        /// @param init Initial CRC value (default 0x0000000000000000).
        /// @return CRC-64 checksum.
        constexpr UInt64 ECMA_182(const UInt8* data, UIntSize len) noexcept
        {
            return CRCDetail::CRC64_ECMA::Compute(data, len);
        }
    }// namespace CRC64

}// namespace NGIN::Hashing
