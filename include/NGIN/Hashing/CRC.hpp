// CRC.hpp
// Implements CRC-8, CRC-16, CRC-32, CRC-64 in NGIN::Hashing
#pragma once

#include <NGIN/Primitives.hpp>

namespace NGIN::Hashing
{
    namespace CRC8
    {
        /// @brief CRC-8/SMBUS (aka CRC-8)
        /// @details poly=0x07, init=0x00, refin=false, refout=false, xorout=0x00
        constexpr UInt8 SMBUS(const UInt8* data, UIntSize len, UInt8 poly = 0x07, UInt8 init = 0x00) noexcept
        {
            UInt8 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= data[i];
                for (int j = 0; j < 8; ++j)
                    crc = (crc & 0x80) ? (crc << 1) ^ poly : (crc << 1);
            }
            return crc;
        }

        /// @brief CRC-8/MAXIM-DOW (Dallas/Maxim)
        /// @details reflected poly=0x8C (bit-reflected 0x31), init=0x00, refin=true, refout=true, xorout=0x00
        constexpr UInt8 MAXIM_DOW(const UInt8* data, UIntSize len, UInt8 poly = 0x8C, UInt8 init = 0x00) noexcept
        {
            UInt8 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= data[i];
                for (int j = 0; j < 8; ++j)
                    crc = (crc & 0x01) ? (crc >> 1) ^ poly : (crc >> 1);
            }
            return crc;
        }

        /// @brief CRC-8/AUTOSAR
        /// @details poly=0x2F, init=0xFF, refin=false, refout=false, xorout=0xFF
        constexpr UInt8 AUTOSAR(const UInt8* data, UIntSize len, UInt8 poly = 0x2F, UInt8 init = 0xFF) noexcept
        {
            UInt8 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= data[i];
                for (int j = 0; j < 8; ++j)
                    crc = (crc & 0x80) ? (crc << 1) ^ poly : (crc << 1);
            }
            return crc ^ 0xFF;
        }

        /// @brief CRC-8/SAE-J1850
        /// @details poly=0x1D, init=0xFF, refin=false, refout=false, xorout=0xFF
        constexpr UInt8 SAE_J1850(const UInt8* data, UIntSize len, UInt8 poly = 0x1D, UInt8 init = 0xFF) noexcept
        {
            UInt8 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= data[i];
                for (int j = 0; j < 8; ++j)
                    crc = (crc & 0x80) ? (crc << 1) ^ poly : (crc << 1);
            }
            return crc ^ 0xFF;
        }

        /// @brief CRC-8/BLUETOOTH
        /// @details reflected poly=0xE5 (bit-reflected 0xA7), init=0x00, refin=true, refout=true, xorout=0x00
        constexpr UInt8 BLUETOOTH(const UInt8* data, UIntSize len, UInt8 poly = 0xE5, UInt8 init = 0x00) noexcept
        {
            UInt8 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= data[i];
                for (int j = 0; j < 8; ++j)
                    crc = (crc & 0x01) ? (crc >> 1) ^ poly : (crc >> 1);
            }
            return crc;
        }
    }// namespace CRC8

    namespace CRC16
    {
        /// @brief Compute CRC-16/CCITT-FALSE checksum.
        /// @details Implements CRC-16/CCITT-FALSE (poly=0x1021, init=0xFFFF, no reflection, xorout=0x0000).
        /// @param data Pointer to input data buffer (as bytes).
        /// @param len Number of bytes to process.
        /// @param poly CRC polynomial (default 0x1021).
        /// @param init Initial CRC value (default 0xFFFF).
        /// @return CRC-16 checksum.
        constexpr UInt16 CCITT_FALSE(const UInt8* data, UIntSize len, UInt16 poly = 0x1021, UInt16 init = 0xFFFF) noexcept
        {
            UInt16 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= static_cast<UInt16>(data[i]) << 8;
                for (int j = 0; j < 8; ++j)
                {
                    crc = (crc & 0x8000) ? (crc << 1) ^ poly : (crc << 1);
                }
            }
            return crc;
        }

        /// @brief CRC-16/ARC (aka CRC-16, CRC-IBM, CRC-16/LHA)
        /// @details reflected poly=0xA001 (bit-reflected 0x8005), init=0x0000, refin=true, refout=true, xorout=0x0000
        constexpr UInt16 ARC(const UInt8* data, UIntSize len, UInt16 poly = 0xA001, UInt16 init = 0x0000) noexcept
        {
            UInt16 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= data[i];
                for (int j = 0; j < 8; ++j)
                    crc = (crc & 0x0001) ? (crc >> 1) ^ poly : (crc >> 1);
            }
            return crc;
        }

        /// @brief CRC-16/IBM-3740 (aka CRC-16/AUTOSAR, CRC-16/CCITT-FALSE)
        /// @details poly=0x1021, init=0xFFFF, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 IBM_3740(const UInt8* data, UIntSize len, UInt16 poly = 0x1021, UInt16 init = 0xFFFF) noexcept
        {
            UInt16 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= static_cast<UInt16>(data[i]) << 8;
                for (int j = 0; j < 8; ++j)
                    crc = (crc & 0x8000) ? (crc << 1) ^ poly : (crc << 1);
            }
            return crc;
        }

        /// @brief CRC-16/XMODEM (aka CRC-16/ACORN, CRC-16/LTE, CRC-16/V-41-MSB)
        /// @details poly=0x1021, init=0x0000, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 XMODEM(const UInt8* data, UIntSize len, UInt16 poly = 0x1021, UInt16 init = 0x0000) noexcept
        {
            UInt16 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= static_cast<UInt16>(data[i]) << 8;
                for (int j = 0; j < 8; ++j)
                    crc = (crc & 0x8000) ? (crc << 1) ^ poly : (crc << 1);
            }
            return crc;
        }

        /// @brief CRC-16/KERMIT (aka CRC-16/CCITT, CRC-16/BLUETOOTH)
        /// @details reflected poly=0x8408 (bit-reflected 0x1021), init=0x0000, refin=true, refout=true, xorout=0x0000
        constexpr UInt16 KERMIT(const UInt8* data, UIntSize len, UInt16 poly = 0x8408, UInt16 init = 0x0000) noexcept
        {
            UInt16 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= data[i];
                for (int j = 0; j < 8; ++j)
                    crc = (crc & 0x0001) ? (crc >> 1) ^ poly : (crc >> 1);
            }
            return crc;
        }

        /// @brief CRC-16/MODBUS
        /// @details reflected poly=0xA001 (bit-reflected 0x8005), init=0xFFFF, refin=true, refout=true, xorout=0x0000
        constexpr UInt16 MODBUS(const UInt8* data, UIntSize len, UInt16 poly = 0xA001, UInt16 init = 0xFFFF) noexcept
        {
            UInt16 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= data[i];
                for (int j = 0; j < 8; ++j)
                    crc = (crc & 0x0001) ? (crc >> 1) ^ poly : (crc >> 1);
            }
            return crc;
        }

        /// @brief CRC-16/IBM-SDLC (aka CRC-16/X-25, CRC-B)
        /// @details reflected poly=0x8408 (bit-reflected 0x1021), init=0xFFFF, refin=true, refout=true, xorout=0xFFFF
        constexpr UInt16 IBM_SDLC(const UInt8* data, UIntSize len, UInt16 poly = 0x8408, UInt16 init = 0xFFFF) noexcept
        {
            UInt16 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= data[i];
                for (int j = 0; j < 8; ++j)
                    crc = (crc & 0x0001) ? (crc >> 1) ^ poly : (crc >> 1);
            }
            return crc ^ 0xFFFF;
        }

        /// @brief CRC-16/GENIBUS (aka CRC-16/DARC, CRC-16/EPC, CRC-16/I-CODE)
        /// @details poly=0x1021, init=0xFFFF, refin=false, refout=false, xorout=0xFFFF
        constexpr UInt16 GENIBUS(const UInt8* data, UIntSize len, UInt16 poly = 0x1021, UInt16 init = 0xFFFF) noexcept
        {
            UInt16 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= static_cast<UInt16>(data[i]) << 8;
                for (int j = 0; j < 8; ++j)
                    crc = (crc & 0x8000) ? (crc << 1) ^ poly : (crc << 1);
            }
            return crc ^ 0xFFFF;
        }

        /// @brief CRC-16/USB
        /// @details reflected poly=0xA001 (bit-reflected 0x8005), init=0xFFFF, refin=true, refout=true, xorout=0xFFFF
        constexpr UInt16 USB(const UInt8* data, UIntSize len, UInt16 poly = 0xA001, UInt16 init = 0xFFFF) noexcept
        {
            UInt16 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= data[i];
                for (int j = 0; j < 8; ++j)
                    crc = (crc & 0x0001) ? (crc >> 1) ^ poly : (crc >> 1);
            }
            return crc ^ 0xFFFF;
        }

        /// @brief CRC-16/MAXIM-DOW
        /// @details reflected poly=0xA001 (bit-reflected 0x8005), init=0x0000, refin=true, refout=true, xorout=0xFFFF
        constexpr UInt16 MAXIM_DOW(const UInt8* data, UIntSize len, UInt16 poly = 0xA001, UInt16 init = 0x0000) noexcept
        {
            UInt16 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= data[i];
                for (int j = 0; j < 8; ++j)
                    crc = (crc & 0x0001) ? (crc >> 1) ^ poly : (crc >> 1);
            }
            return crc ^ 0xFFFF;
        }

        /// @brief CRC-16/MCRF4XX
        /// @details reflected poly=0x8408 (bit-reflected 0x1021), init=0xFFFF, refin=true, refout=true, xorout=0x0000
        constexpr UInt16 MCRF4XX(const UInt8* data, UIntSize len, UInt16 poly = 0x8408, UInt16 init = 0xFFFF) noexcept
        {
            UInt16 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= data[i];
                for (int j = 0; j < 8; ++j)
                    crc = (crc & 0x0001) ? (crc >> 1) ^ poly : (crc >> 1);
            }
            return crc;
        }

        /// @brief CRC-16/DNP
        /// @details poly=0x3D65, init=0x0000, refin=true, refout=true, xorout=0xFFFF
        constexpr UInt16 DNP(const UInt8* data, UIntSize len, UInt16 poly = 0x3D65, UInt16 init = 0x0000) noexcept
        {
            UInt16 out          = init;
            UIntSize bits_read  = 0;
            UIntSize total_bits = len * 8;
            UIntSize data_idx   = 0;
            while (total_bits > 0)
            {
                int bit_flag = out >> 15;
                out <<= 1;
                out |= (data[data_idx] >> bits_read) & 1;
                bits_read++;
                if (bits_read > 7)
                {
                    bits_read = 0;
                    data_idx++;
                }
                if (bit_flag)
                    out ^= poly;
                total_bits--;
            }
            // Push out last 16 bits
            for (int i = 0; i < 16; ++i)
            {
                int bit_flag = out >> 15;
                out <<= 1;
                if (bit_flag)
                    out ^= poly;
            }
            // Bit reversal
            UInt16 crc = 0;
            UInt16 i   = 0x8000;
            UInt16 j   = 0x0001;
            for (; i != 0; i >>= 1, j <<= 1)
            {
                if (i & out)
                    crc |= j;
            }
            return crc ^ 0xFFFF;
        }

        /// @brief CRC-16/EN-13757
        /// @details poly=0x3d65, init=0x0000, refin=false, refout=false, xorout=0xFFFF
        constexpr UInt16 EN_13757(const UInt8* data, UIntSize len, UInt16 poly = 0x3d65, UInt16 init = 0x0000) noexcept
        {
            UInt16 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= static_cast<UInt16>(data[i]) << 8;
                for (int j = 0; j < 8; ++j)
                    crc = (crc & 0x8000) ? (crc << 1) ^ poly : (crc << 1);
            }
            return crc ^ 0xFFFF;
        }

        /// @brief CRC-16/DECT-R
        /// @details poly=0x0589, init=0x0000, refin=false, refout=false, xorout=0x0001
        constexpr UInt16 DECT_R(const UInt8* data, UIntSize len, UInt16 poly = 0x0589, UInt16 init = 0x0000) noexcept
        {
            UInt16 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= static_cast<UInt16>(data[i]) << 8;
                for (int j = 0; j < 8; ++j)
                    crc = (crc & 0x8000) ? (crc << 1) ^ poly : (crc << 1);
            }
            return crc ^ 0x0001;
        }

        /// @brief CRC-16/DECT-X
        /// @details poly=0x0589, init=0x0000, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 DECT_X(const UInt8* data, UIntSize len, UInt16 poly = 0x0589, UInt16 init = 0x0000) noexcept
        {
            UInt16 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= static_cast<UInt16>(data[i]) << 8;
                for (int j = 0; j < 8; ++j)
                    crc = (crc & 0x8000) ? (crc << 1) ^ poly : (crc << 1);
            }
            return crc;
        }

        /// @brief CRC-16/UMTS
        /// @details poly=0x8005, init=0x0000, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 UMTS(const UInt8* data, UIntSize len, UInt16 poly = 0x8005, UInt16 init = 0x0000) noexcept
        {
            UInt16 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= static_cast<UInt16>(data[i]) << 8;
                for (int j = 0; j < 8; ++j)
                    crc = (crc & 0x8000) ? (crc << 1) ^ poly : (crc << 1);
            }
            return crc;
        }

        /// @brief CRC-16/ISO-IEC-14443-3-A (CRC-A)
        /// @details poly=0x1021, init=0xc6c6, refin=true, refout=true, xorout=0x0000
        /// @brief CRC-16/ISO-IEC-14443-3-A
        /// @details reflected poly=0x8408 (bit-reflected 0x1021), init=0xC6C6, refin=true, refout=true, xorout=0x0000
        constexpr UInt16 ISO_IEC_14443_3_A(const UInt8* data, UIntSize len, UInt16 poly = 0x8408, UInt16 init = 0x6363) noexcept
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
        constexpr UInt16 T10_DIF(const UInt8* data, UIntSize len, UInt16 poly = 0x8bb7, UInt16 init = 0x0000) noexcept
        {
            UInt16 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= static_cast<UInt16>(data[i]) << 8;
                for (int j = 0; j < 8; ++j)
                    crc = (crc & 0x8000) ? (crc << 1) ^ poly : (crc << 1);
            }
            return crc;
        }

        /// @brief CRC-16/PROFIBUS
        /// @details poly=0x1dcf, init=0xFFFF, refin=false, refout=false, xorout=0xFFFF
        constexpr UInt16 PROFIBUS(const UInt8* data, UIntSize len, UInt16 poly = 0x1dcf, UInt16 init = 0xFFFF) noexcept
        {
            UInt16 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= static_cast<UInt16>(data[i]) << 8;
                for (int j = 0; j < 8; ++j)
                    crc = (crc & 0x8000) ? (crc << 1) ^ poly : (crc << 1);
            }
            return crc ^ 0xFFFF;
        }

        /// @brief CRC-16/LJ1200
        /// @details poly=0x6f63, init=0x0000, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 LJ1200(const UInt8* data, UIntSize len, UInt16 poly = 0x6f63, UInt16 init = 0x0000) noexcept
        {
            UInt16 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= static_cast<UInt16>(data[i]) << 8;
                for (int j = 0; j < 8; ++j)
                    crc = (crc & 0x8000) ? (crc << 1) ^ poly : (crc << 1);
            }
            return crc;
        }

        /// @brief CRC-16/OPENSAFETY-A
        /// @details poly=0x5935, init=0x0000, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 OPENSAFETY_A(const UInt8* data, UIntSize len, UInt16 poly = 0x5935, UInt16 init = 0x0000) noexcept
        {
            UInt16 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= static_cast<UInt16>(data[i]) << 8;
                for (int j = 0; j < 8; ++j)
                    crc = (crc & 0x8000) ? (crc << 1) ^ poly : (crc << 1);
            }
            return crc;
        }

        /// @brief CRC-16/OPENSAFETY-B
        /// @details poly=0x755b, init=0x0000, refin=false, refout=false, xorout=0x0000
        /// @brief CRC-16/OPENSAFETY-B
        /// @details reflected poly=0xDDAA (bit-reflected 0x755B), init=0x0000, refin=true, refout=true, xorout=0x0000
        constexpr UInt16 OPENSAFETY_B(const UInt8* data, UIntSize len, UInt16 poly = 0xDDAA, UInt16 init = 0x0000) noexcept
        {
            // Standard: poly=0x755B, init=0x0000, refin=false, refout=false, xorout=0x0000
            UInt16 crc = 0x0000;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= static_cast<UInt16>(data[i]) << 8;
                for (int j = 0; j < 8; ++j)
                {
                    crc = (crc & 0x8000) ? (crc << 1) ^ 0x755B : (crc << 1);
                }
            }
            return crc;
        }

        /// @brief CRC-16/NRSC-5 (reflected)
        /// @details poly=0x080B, init=0xFFFF, refin=true, refout=true, xorout=0x0000
        constexpr UInt16 NRSC_5(const UInt8* data, UIntSize len, UInt16 poly = 0xD010, UInt16 init = 0xFFFF) noexcept
        {
            // LSB-first/reflected CRC with reflected poly 0xD010
            UInt16 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= data[i];
                for (int j = 0; j < 8; ++j)
                {
                    crc = (crc & 0x0001) ? (crc >> 1) ^ poly : (crc >> 1);
                }
            }
            return crc;
        }

        /// @brief CRC-16/CMS
        /// @details poly=0x8005, init=0xFFFF, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 CMS(const UInt8* data, UIntSize len, UInt16 poly = 0x8005, UInt16 init = 0xFFFF) noexcept
        {
            UInt16 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= static_cast<UInt16>(data[i]) << 8;
                for (int j = 0; j < 8; ++j)
                    crc = (crc & 0x8000) ? (crc << 1) ^ poly : (crc << 1);
            }
            return crc;
        }

        /// @brief CRC-16/DDS-110
        /// @details poly=0x8005, init=0x800d, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 DDS_110(const UInt8* data, UIntSize len, UInt16 poly = 0x8005, UInt16 init = 0x800d) noexcept
        {
            UInt16 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= static_cast<UInt16>(data[i]) << 8;
                for (int j = 0; j < 8; ++j)
                    crc = (crc & 0x8000) ? (crc << 1) ^ poly : (crc << 1);
            }
            return crc;
        }

        /// @brief CRC-16/M17
        /// @details poly=0x5935, init=0xFFFF, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 M17(const UInt8* data, UIntSize len, UInt16 poly = 0x5935, UInt16 init = 0xFFFF) noexcept
        {
            UInt16 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= static_cast<UInt16>(data[i]) << 8;
                for (int j = 0; j < 8; ++j)
                    crc = (crc & 0x8000) ? (crc << 1) ^ poly : (crc << 1);
            }
            return crc;
        }

        /// @brief CRC-16/TELEDISK
        /// @details poly=0xa097, init=0x0000, refin=false, refout=false, xorout=0x0000
        constexpr UInt16 TELEDISK(const UInt8* data, UIntSize len, UInt16 poly = 0xa097, UInt16 init = 0x0000) noexcept
        {
            UInt16 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= static_cast<UInt16>(data[i]) << 8;
                for (int j = 0; j < 8; ++j)
                    crc = (crc & 0x8000) ? (crc << 1) ^ poly : (crc << 1);
            }
            return crc;
        }

        /// @brief CRC-16/TMS37157
        /// @details reflected poly=0x8408 (bit-reflected 0x1021), init=0x89EC, refin=true, refout=true, xorout=0x0000
        constexpr UInt16 TMS37157(const UInt8* data, UIntSize len, UInt16 poly = 0x8408, UInt16 init = 0x3791) noexcept
        {
            UInt16 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= data[i];
                for (int j = 0; j < 8; ++j)
                    crc = (crc & 0x0001) ? (crc >> 1) ^ poly : (crc >> 1);
            }
            return crc;
        }
    }// namespace CRC16

    namespace CRC32
    {
        /// @brief Compute CRC-32/IEEE-802.3 checksum.
        /// @details Implements CRC-32/IEEE-802.3 (reflected poly=0xEDB88320, init=0xFFFFFFFF,
        ///          input/output reflected, xorout=0xFFFFFFFF).
        /// @param data Pointer to input data buffer (as bytes).
        /// @param len Number of bytes to process.
        /// @param poly Reflected CRC polynomial (default 0xEDB88320).
        /// @param init Initial CRC value (default 0xFFFFFFFF).
        /// @return CRC-32 checksum.
        constexpr UInt32 IEEE_802_3(const UInt8* data, UIntSize len, UInt32 poly = 0xEDB88320, UInt32 init = 0xFFFFFFFF) noexcept
        {
            UInt32 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= data[i];
                for (int j = 0; j < 8; ++j)
                {
                    crc = (crc & 1) ? (crc >> 1) ^ poly : (crc >> 1);
                }
            }
            return crc ^ 0xFFFFFFFFu;
        }

        /// @brief Compute CRC-32/MPEG-2 checksum.
        /// @details Implements CRC-32/MPEG-2 (poly=0x04C11DB7, init=0xFFFFFFFF, no reflection, xorout=0x00000000).
        /// @param data Pointer to input data buffer (as bytes).
        /// @param len Number of bytes to process.
        /// @param poly CRC polynomial (default 0x04C11DB7).
        /// @param init Initial CRC value (default 0xFFFFFFFF).
        /// @return CRC-32 checksum.
        constexpr UInt32 MPEG_2(const UInt8* data, UIntSize len, UInt32 poly = 0x04C11DB7, UInt32 init = 0xFFFFFFFF) noexcept
        {
            UInt32 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= static_cast<UInt32>(data[i]) << 24;
                for (int j = 0; j < 8; ++j)
                {
                    crc = (crc & 0x80000000u) ? (crc << 1) ^ poly : (crc << 1);
                }
            }
            return crc;
        }
    }// namespace CRC32

    namespace CRC64
    {
        /// @brief Compute CRC-64/ISO-3309 checksum.
        /// @details Implements CRC-64/ISO-3309 (reflected poly=0xD800000000000000, init=0xFFFFFFFFFFFFFFFF,
        ///          input/output reflected, xorout=0xFFFFFFFFFFFFFFFF).
        /// @param data Pointer to input data buffer (as bytes).
        /// @param len Number of bytes to process.
        /// @param poly Reflected CRC polynomial (default 0xD800000000000000).
        /// @param init Initial CRC value (default 0xFFFFFFFFFFFFFFFF).
        /// @return CRC-64 checksum.
        constexpr UInt64 ISO_3309(const UInt8* data, UIntSize len, UInt64 poly = 0xD800000000000000ULL, UInt64 init = 0xFFFFFFFFFFFFFFFFULL) noexcept
        {
            UInt64 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= data[i];
                for (int j = 0; j < 8; ++j)
                {
                    crc = (crc & 1) ? (crc >> 1) ^ poly : (crc >> 1);
                }
            }
            return crc ^ 0xFFFFFFFFFFFFFFFFULL;
        }

        /// @brief Compute CRC-64/ECMA-182 checksum.
        /// @details Implements CRC-64/ECMA-182 (poly=0x42F0E1EBA9EA3693, init=0x0000000000000000, no reflection, xorout=0x0000000000000000).
        /// @param data Pointer to input data buffer (as bytes).
        /// @param len Number of bytes to process.
        /// @param poly CRC polynomial (default 0x42F0E1EBA9EA3693).
        /// @param init Initial CRC value (default 0x0000000000000000).
        /// @return CRC-64 checksum.
        constexpr UInt64 ECMA_182(const UInt8* data, UIntSize len, UInt64 poly = 0x42F0E1EBA9EA3693ULL, UInt64 init = 0x0000000000000000ULL) noexcept
        {
            UInt64 crc = init;
            for (UIntSize i = 0; i < len; ++i)
            {
                crc ^= static_cast<UInt64>(data[i]) << 56;
                for (int j = 0; j < 8; ++j)
                {
                    crc = (crc & 0x8000000000000000ULL) ? (crc << 1) ^ poly : (crc << 1);
                }
            }
            return crc;
        }
    }// namespace CRC64

}// namespace NGIN::Hashing
