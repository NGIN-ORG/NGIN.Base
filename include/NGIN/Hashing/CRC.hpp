// CRC.hpp
// Implements CRC-8, CRC-16, CRC-32, CRC-64 in NGIN::Hashing
#pragma once

#include <NGIN/Primitives.hpp>

namespace NGIN::Hashing
{
    namespace CRC8
    {
        namespace detail
        {
            /// @brief Reflect (bit-reverse) an 8-bit value.
            constexpr UInt8 Reflect8(UInt8 v) noexcept
            {
                v = static_cast<UInt8>((v & 0xF0u) >> 4 | (v & 0x0Fu) << 4);
                v = static_cast<UInt8>((v & 0xCCu) >> 2 | (v & 0x33u) << 2);
                v = static_cast<UInt8>((v & 0xAAu) >> 1 | (v & 0x55u) << 1);
                return v;
            }

            /// @brief Generic CRC-8 bitwise (slow) computation supporting reflected and non-reflected variants.
            /// @param data Input buffer pointer (may be nullptr if len==0).
            /// @param len Number of bytes.
            /// @param poly Polynomial (normal form if refin==false, reflected form if refin==true).
            /// @param init Initial CRC value.
            /// @param refin If true process LSB-first (reflected input processing).
            /// @param refout If true final CRC is reflected (if refin differs from refout a final reflect is applied).
            /// @param xorout Final XOR value applied after optional reflection.
            constexpr UInt8 Compute(const UInt8* data,
                                    UIntSize len,
                                    UInt8 poly,
                                    UInt8 init,
                                    bool refin,
                                    bool refout,
                                    UInt8 xorout) noexcept
            {
                UInt8 crc = init;
                if (len && data)
                {
                    for (UIntSize i = 0; i < len; ++i)
                    {
                        crc ^= data[i];
                        if (refin)
                        {
                            for (int b = 0; b < 8; ++b)
                                crc = (crc & 0x01u) ? static_cast<UInt8>((crc >> 1) ^ poly) : static_cast<UInt8>(crc >> 1);
                        }
                        else
                        {
                            for (int b = 0; b < 8; ++b)
                                crc = (crc & 0x80u) ? static_cast<UInt8>((crc << 1) ^ poly) : static_cast<UInt8>(crc << 1);
                        }
                    }
                }
                if (refin != refout)
                    crc = Reflect8(crc);
                return static_cast<UInt8>(crc ^ xorout);
            }
        }// namespace detail

        /// @brief CRC-8/SMBUS (aka CRC-8)
        /// @details poly=0x07, init=0x00, refin=false, refout=false, xorout=0x00
        constexpr UInt8 SMBUS(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0x07, 0x00, false, false, 0x00);
        }

        /// @brief CRC-8/MAXIM-DOW (Dallas/Maxim)
        /// @details reflected poly=0x8C (bit-reflected 0x31), init=0x00, refin=true, refout=true, xorout=0x00
        constexpr UInt8 MAXIM_DOW(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0x8C, 0x00, true, true, 0x00);
        }

        /// @brief CRC-8/AUTOSAR
        /// @details poly=0x2F, init=0xFF, refin=false, refout=false, xorout=0xFF
        constexpr UInt8 AUTOSAR(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0x2F, 0xFF, false, false, 0xFF);
        }

        /// @brief CRC-8/SAE-J1850
        /// @details poly=0x1D, init=0xFF, refin=false, refout=false, xorout=0xFF
        constexpr UInt8 SAE_J1850(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0x1D, 0xFF, false, false, 0xFF);
        }

        /// @brief CRC-8/BLUETOOTH
        /// @details reflected poly=0xE5 (bit-reflected 0xA7), init=0x00, refin=true, refout=true, xorout=0x00
        constexpr UInt8 BLUETOOTH(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0xE5, 0x00, true, true, 0x00);
        }
    }// namespace CRC8

    namespace CRC16
    {
        namespace detail
        {
            constexpr UInt16 Reflect16(UInt16 v) noexcept
            {
                v = static_cast<UInt16>(((v & 0xFF00u) >> 8) | ((v & 0x00FFu) << 8));
                v = static_cast<UInt16>(((v & 0xF0F0u) >> 4) | ((v & 0x0F0Fu) << 4));
                v = static_cast<UInt16>(((v & 0xCCCCu) >> 2) | ((v & 0x3333u) << 2));
                v = static_cast<UInt16>(((v & 0xAAAAu) >> 1) | ((v & 0x5555u) << 1));
                return v;
            }

            /// Generic bitwise CRC-16. If refin=true poly must be reflected form, else normal.
            constexpr UInt16 Compute(const UInt8* data,
                                     UIntSize len,
                                     UInt16 poly,
                                     UInt16 init,
                                     bool refin,
                                     bool refout,
                                     UInt16 xorout) noexcept
            {
                UInt16 crc = init;
                if (data && len)
                {
                    for (UIntSize i = 0; i < len; ++i)
                    {
                        if (refin)
                        {
                            crc ^= data[i];
                            for (int b = 0; b < 8; ++b)
                                crc = (crc & 0x0001u) ? static_cast<UInt16>((crc >> 1) ^ poly) : static_cast<UInt16>(crc >> 1);
                        }
                        else
                        {
                            crc ^= static_cast<UInt16>(data[i]) << 8;
                            for (int b = 0; b < 8; ++b)
                                crc = (crc & 0x8000u) ? static_cast<UInt16>((crc << 1) ^ poly) : static_cast<UInt16>(crc << 1);
                        }
                    }
                }
                if (refin != refout)
                    crc = Reflect16(crc);
                return static_cast<UInt16>(crc ^ xorout);
            }
        }// namespace detail

        /// @brief Compute CRC-16/CCITT-FALSE checksum.
        /// @details Implements CRC-16/CCITT-FALSE (poly=0x1021, init=0xFFFF, no reflection, xorout=0x0000).
        constexpr UInt16 CCITT_FALSE(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0x1021, 0xFFFF, false, false, 0x0000);
        }

        /// @brief CRC-16/ARC (aka CRC-16, CRC-IBM, CRC-16/LHA)
        /// @details reflected poly=0xA001 (bit-reflected 0x8005), init=0x0000, refin=true, refout=true, xorout=0x0000
        constexpr UInt16 ARC(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0xA001, 0x0000, true, true, 0x0000);
        }

        /// @brief CRC-16/IBM-3740 (aka CRC-16/AUTOSAR, CRC-16/CCITT-FALSE)
        /// @details poly=0x1021, init=0xFFFF, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 IBM_3740(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0x1021, 0xFFFF, false, false, 0x0000);
        }

        /// @brief CRC-16/XMODEM (aka CRC-16/ACORN, CRC-16/LTE, CRC-16/V-41-MSB)
        /// @details poly=0x1021, init=0x0000, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 XMODEM(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0x1021, 0x0000, false, false, 0x0000);
        }

        /// @brief CRC-16/KERMIT (aka CRC-16/CCITT, CRC-16/BLUETOOTH)
        /// @details reflected poly=0x8408 (bit-reflected 0x1021), init=0x0000, refin=true, refout=true, xorout=0x0000
        constexpr UInt16 KERMIT(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0x8408, 0x0000, true, true, 0x0000);
        }

        /// @brief CRC-16/MODBUS
        /// @details reflected poly=0xA001 (bit-reflected 0x8005), init=0xFFFF, refin=true, refout=true, xorout=0x0000
        constexpr UInt16 MODBUS(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0xA001, 0xFFFF, true, true, 0x0000);
        }

        /// @brief CRC-16/IBM-SDLC (aka CRC-16/X-25, CRC-B)
        /// @details reflected poly=0x8408 (bit-reflected 0x1021), init=0xFFFF, refin=true, refout=true, xorout=0xFFFF
        constexpr UInt16 IBM_SDLC(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0x8408, 0xFFFF, true, true, 0xFFFF);
        }

        /// @brief CRC-16/GENIBUS (aka CRC-16/DARC, CRC-16/EPC, CRC-16/I-CODE)
        /// @details poly=0x1021, init=0xFFFF, refin=false, refout=false, xorout=0xFFFF
        constexpr UInt16 GENIBUS(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0x1021, 0xFFFF, false, false, 0xFFFF);
        }

        /// @brief CRC-16/USB
        /// @details reflected poly=0xA001 (bit-reflected 0x8005), init=0xFFFF, refin=true, refout=true, xorout=0xFFFF
        constexpr UInt16 USB(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0xA001, 0xFFFF, true, true, 0xFFFF);
        }

        /// @brief CRC-16/MAXIM-DOW
        /// @details reflected poly=0xA001 (bit-reflected 0x8005), init=0x0000, refin=true, refout=true, xorout=0xFFFF
        constexpr UInt16 MAXIM_DOW(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0xA001, 0x0000, true, true, 0xFFFF);
        }

        /// @brief CRC-16/MCRF4XX
        /// @details reflected poly=0x8408 (bit-reflected 0x1021), init=0xFFFF, refin=true, refout=true, xorout=0x0000
        constexpr UInt16 MCRF4XX(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0x8408, 0xFFFF, true, true, 0x0000);
        }

        /// @brief CRC-16/DNP
        /// @details poly=0x3D65, init=0x0000, refin=true, refout=true, xorout=0xFFFF
        constexpr UInt16 DNP(const UInt8* data, UIntSize len) noexcept
        {
            // Use reflected polynomial (0xA6BC is bit-reflection of canonical 0x3D65) with generic engine.
            return detail::Compute(data, len, 0xA6BC, 0x0000, true, true, 0xFFFF);
        }

        /// @brief CRC-16/EN-13757
        /// @details poly=0x3d65, init=0x0000, refin=false, refout=false, xorout=0xFFFF
        constexpr UInt16 EN_13757(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0x3D65, 0x0000, false, false, 0xFFFF);
        }

        /// @brief CRC-16/DECT-R
        /// @details poly=0x0589, init=0x0000, refin=false, refout=false, xorout=0x0001
        constexpr UInt16 DECT_R(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0x0589, 0x0000, false, false, 0x0001);
        }

        /// @brief CRC-16/DECT-X
        /// @details poly=0x0589, init=0x0000, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 DECT_X(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0x0589, 0x0000, false, false, 0x0000);
        }

        /// @brief CRC-16/UMTS
        /// @details poly=0x8005, init=0x0000, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 UMTS(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0x8005, 0x0000, false, false, 0x0000);
        }

        /// @brief CRC-16/ISO-IEC-14443-3-A (CRC-A)
        /// @details poly=0x1021, init=0xc6c6, refin=true, refout=true, xorout=0x0000
        /// @brief CRC-16/ISO-IEC-14443-3-A
        /// @details reflected poly=0x8408 (bit-reflected 0x1021), init=0xC6C6, refin=true, refout=true, xorout=0x0000
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
            return detail::Compute(data, len, 0x8BB7, 0x0000, false, false, 0x0000);
        }

        /// @brief CRC-16/PROFIBUS
        /// @details poly=0x1dcf, init=0xFFFF, refin=false, refout=false, xorout=0xFFFF
        constexpr UInt16 PROFIBUS(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0x1DCF, 0xFFFF, false, false, 0xFFFF);
        }

        /// @brief CRC-16/LJ1200
        /// @details poly=0x6f63, init=0x0000, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 LJ1200(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0x6F63, 0x0000, false, false, 0x0000);
        }

        /// @brief CRC-16/OPENSAFETY-A
        /// @details poly=0x5935, init=0x0000, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 OPENSAFETY_A(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0x5935, 0x0000, false, false, 0x0000);
        }

        /// @brief CRC-16/OPENSAFETY-B
        /// @details poly=0x755b, init=0x0000, refin=false, refout=false, xorout=0x0000
        /// @brief CRC-16/OPENSAFETY-B
        /// @details reflected poly=0xDDAA (bit-reflected 0x755B), init=0x0000, refin=true, refout=true, xorout=0x0000
        constexpr UInt16 OPENSAFETY_B(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0x755B, 0x0000, false, false, 0x0000);
        }

        /// @brief CRC-16/NRSC-5 (reflected)
        /// @details poly=0x080B, init=0xFFFF, refin=true, refout=true, xorout=0x0000
        constexpr UInt16 NRSC_5(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0xD010, 0xFFFF, true, true, 0x0000);
        }

        /// @brief CRC-16/CMS
        /// @details poly=0x8005, init=0xFFFF, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 CMS(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0x8005, 0xFFFF, false, false, 0x0000);
        }

        /// @brief CRC-16/DDS-110
        /// @details poly=0x8005, init=0x800d, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 DDS_110(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0x8005, 0x800D, false, false, 0x0000);
        }

        /// @brief CRC-16/M17
        /// @details poly=0x5935, init=0xFFFF, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 M17(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0x5935, 0xFFFF, false, false, 0x0000);
        }

        /// @brief CRC-16/TELEDISK
        /// @details poly=0xa097, init=0x0000, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 TELEDISK(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0xA097, 0x0000, false, false, 0x0000);
        }

        /// @brief CRC-16/TMS37157
        /// @details reflected poly=0x8408 (bit-reflected 0x1021), init=0x89EC, refin=true, refout=true, xorout=0x0000
        constexpr UInt16 TMS37157(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0x8408, 0x3791, true, true, 0x0000);
        }
    }// namespace CRC16

    namespace CRC32
    {
        namespace detail
        {
            constexpr UInt32 Reflect32(UInt32 v) noexcept
            {
                v = (v >> 16) | (v << 16);
                v = ((v & 0xFF00FF00u) >> 8) | ((v & 0x00FF00FFu) << 8);
                v = ((v & 0xF0F0F0F0u) >> 4) | ((v & 0x0F0F0F0Fu) << 4);
                v = ((v & 0xCCCCCCCCu) >> 2) | ((v & 0x33333333u) << 2);
                v = ((v & 0xAAAAAAAAu) >> 1) | ((v & 0x55555555u) << 1);
                return v;
            }
            constexpr UInt32 Compute(const UInt8* data,
                                     UIntSize len,
                                     UInt32 poly,
                                     UInt32 init,
                                     bool refin,
                                     bool refout,
                                     UInt32 xorout) noexcept
            {
                UInt32 crc = init;
                if (data && len)
                {
                    for (UIntSize i = 0; i < len; ++i)
                    {
                        if (refin)
                        {
                            crc ^= data[i];
                            for (int b = 0; b < 8; ++b)
                                crc = (crc & 1u) ? (crc >> 1) ^ poly : (crc >> 1);
                        }
                        else
                        {
                            crc ^= static_cast<UInt32>(data[i]) << 24;
                            for (int b = 0; b < 8; ++b)
                                crc = (crc & 0x80000000u) ? (crc << 1) ^ poly : (crc << 1);
                        }
                    }
                }
                if (refin != refout)
                    crc = Reflect32(crc);
                return crc ^ xorout;
            }
        }// namespace detail
        /// @brief Compute CRC-32/IEEE-802.3 checksum.
        /// @details Implements CRC-32/IEEE-802.3 (reflected poly=0xEDB88320, init=0xFFFFFFFF,
        ///          input/output reflected, xorout=0xFFFFFFFF).
        constexpr UInt32 IEEE_802_3(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0xEDB88320u, 0xFFFFFFFFu, true, true, 0xFFFFFFFFu);
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
            return detail::Compute(data, len, 0x04C11DB7u, 0xFFFFFFFFu, false, false, 0x00000000u);
        }
    }// namespace CRC32

    namespace CRC64
    {
        namespace detail
        {
            constexpr UInt64 Reflect64(UInt64 v) noexcept
            {
                v = (v >> 32) | (v << 32);
                v = ((v & 0xFFFF0000FFFF0000ULL) >> 16) | ((v & 0x0000FFFF0000FFFFULL) << 16);
                v = ((v & 0xFF00FF00FF00FF00ULL) >> 8) | ((v & 0x00FF00FF00FF00FFULL) << 8);
                v = ((v & 0xF0F0F0F0F0F0F0F0ULL) >> 4) | ((v & 0x0F0F0F0F0F0F0F0FULL) << 4);
                v = ((v & 0xCCCCCCCCCCCCCCCCULL) >> 2) | ((v & 0x3333333333333333ULL) << 2);
                v = ((v & 0xAAAAAAAAAAAAAAAAULL) >> 1) | ((v & 0x5555555555555555ULL) << 1);
                return v;
            }
            constexpr UInt64 Compute(const UInt8* data,
                                     UIntSize len,
                                     UInt64 poly,
                                     UInt64 init,
                                     bool refin,
                                     bool refout,
                                     UInt64 xorout) noexcept
            {
                UInt64 crc = init;
                if (data && len)
                {
                    for (UIntSize i = 0; i < len; ++i)
                    {
                        if (refin)
                        {
                            crc ^= data[i];
                            for (int b = 0; b < 8; ++b)
                                crc = (crc & 1ULL) ? (crc >> 1) ^ poly : (crc >> 1);
                        }
                        else
                        {
                            crc ^= static_cast<UInt64>(data[i]) << 56;
                            for (int b = 0; b < 8; ++b)
                                crc = (crc & 0x8000000000000000ULL) ? (crc << 1) ^ poly : (crc << 1);
                        }
                    }
                }
                if (refin != refout)
                    crc = Reflect64(crc);
                return crc ^ xorout;
            }
        }// namespace detail
        /// @brief Compute CRC-64/ISO-3309 checksum.
        /// @details Implements CRC-64/ISO-3309 (reflected poly=0xD800000000000000, init=0xFFFFFFFFFFFFFFFF,
        ///          input/output reflected, xorout=0xFFFFFFFFFFFFFFFF).
        constexpr UInt64 ISO_3309(const UInt8* data, UIntSize len) noexcept
        {
            return detail::Compute(data, len, 0xD800000000000000ULL, 0xFFFFFFFFFFFFFFFFULL, true, true, 0xFFFFFFFFFFFFFFFFULL);
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
            return detail::Compute(data, len, 0x42F0E1EBA9EA3693ULL, 0x0000000000000000ULL, false, false, 0x0000000000000000ULL);
        }
    }// namespace CRC64

}// namespace NGIN::Hashing
