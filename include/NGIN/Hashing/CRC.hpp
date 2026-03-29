// CRC.hpp
// Implements CRC-8, CRC-16, CRC-32, CRC-64 in NGIN::Hashing
#pragma once

#include <NGIN/Primitives.hpp>
#include <array>
#include <span>
#include <string_view>
#include <type_traits>

namespace NGIN::Hashing
{
    /// @brief CRC model metadata.
    /// @details The polynomial must be supplied in the bit-processing form used by the engine:
    ///          reflected form when `RefIn=true`, normal form when `RefIn=false`.
    template<typename T, T Poly, T Init, bool RefIn, bool RefOut, T XorOut>
    struct CRCModel
    {
        using value_type                            = T;
        inline static constexpr T        polynomial = Poly;
        inline static constexpr T        init       = Init;
        inline static constexpr bool     refin      = RefIn;
        inline static constexpr bool     refout     = RefOut;
        inline static constexpr T        xorout     = XorOut;
        inline static constexpr unsigned Bits       = sizeof(T) * 8;

        static_assert(std::is_unsigned_v<T>, "CRC model value type must be unsigned");
        static_assert(Bits == 8 || Bits == 16 || Bits == 32 || Bits == 64,
                      "CRC engine supports only 8, 16, 32, and 64-bit widths");
    };

    namespace detail
    {
        template<typename T>
        struct CRCValueTraits
        {
            inline static constexpr unsigned Bits = sizeof(T) * 8;
            using array_type                      = std::array<T, 256>;

            [[nodiscard]] static constexpr T ShiftRight8(T value) noexcept
            {
                if constexpr (Bits > 8)
                    return static_cast<T>(value >> 8);
                else
                    return static_cast<T>(0);
            }

            [[nodiscard]] static constexpr UInt8 ByteAt(T value, unsigned shift) noexcept
            {
                return static_cast<UInt8>((value >> shift) & static_cast<T>(0xFF));
            }
        };

        template<typename T>
        [[nodiscard]] constexpr T BitReverse(T value) noexcept
        {
            constexpr unsigned Bits = sizeof(T) * 8;
            T                  reversed {};
            for (unsigned i = 0; i < Bits; ++i)
            {
                reversed <<= 1;
                reversed |= (value & static_cast<T>(1));
                value >>= 1;
            }
            return reversed;
        }

        template<typename Model>
        using ModelValue = typename Model::value_type;

        template<typename Model>
        using TableArray = typename CRCValueTraits<ModelValue<Model>>::array_type;

        template<typename Model>
        [[nodiscard]] constexpr auto MakeTable0() noexcept -> TableArray<Model>
        {
            using T                                     = ModelValue<Model>;
            constexpr unsigned                     Bits = Model::Bits;
            typename CRCValueTraits<T>::array_type table {};

            for (unsigned n = 0; n < 256; ++n)
            {
                T crc = static_cast<T>(n);
                if constexpr (!Model::refin && Bits > 8)
                    crc = static_cast<T>(crc << (Bits - 8));

                for (int bit = 0; bit < 8; ++bit)
                {
                    if constexpr (Model::refin)
                    {
                        crc = (crc & static_cast<T>(1)) ? static_cast<T>((crc >> 1) ^ Model::polynomial)
                                                        : static_cast<T>(crc >> 1);
                    }
                    else
                    {
                        crc = (crc & (static_cast<T>(1) << (Bits - 1)))
                                      ? static_cast<T>((crc << 1) ^ Model::polynomial)
                                      : static_cast<T>(crc << 1);
                    }
                }

                table[n] = crc;
            }

            return table;
        }

        template<typename Model, unsigned Slices>
        struct TableStorage
        {
            using value_type  = ModelValue<Model>;
            using traits_type = CRCValueTraits<value_type>;
            using array_type  = typename traits_type::array_type;

            inline static constexpr array_type table0 = MakeTable0<Model>();

            [[nodiscard]] static constexpr auto MakeTables() noexcept
            {
                if constexpr (Slices == 1)
                {
                    return std::array<array_type, 1> {table0};
                }
                else
                {
                    static_assert(Slices == 8, "TableCRCEngine currently supports single-table and slicing-by-8");
                    static_assert(Model::refin, "Slicing-by-8 is only supported for reflected CRC models");
                    static_assert(Model::Bits == 32 || Model::Bits == 64,
                                  "Slicing-by-8 is only supported for 32-bit and 64-bit CRC models");

                    std::array<array_type, Slices> tables {};
                    tables[0] = table0;

                    for (unsigned slice = 1; slice < Slices; ++slice)
                    {
                        for (unsigned n = 0; n < 256; ++n)
                        {
                            const auto previous = tables[slice - 1][n];
                            tables[slice][n] =
                                    static_cast<value_type>(tables[0][static_cast<UInt8>(previous & static_cast<value_type>(0xFF))] ^
                                                            traits_type::ShiftRight8(previous));
                        }
                    }

                    return tables;
                }
            }

            inline static constexpr auto tables = MakeTables();
        };

        template<typename Model>
        struct CRC8TableByteUpdateHelper
        {
            using value_type = ModelValue<Model>;

            template<typename Table>
            [[nodiscard]] static constexpr value_type Update(const Table& table, value_type crc, UInt8 byte) noexcept
            {
                return static_cast<value_type>(table[static_cast<UInt8>(crc ^ byte)]);
            }
        };

        template<typename Model>
        struct TableByteUpdateHelper
        {
            using value_type  = ModelValue<Model>;
            using traits_type = CRCValueTraits<value_type>;

            template<typename Table>
            [[nodiscard]] static constexpr value_type Update(const Table& table, value_type crc, UInt8 byte) noexcept
            {
                if constexpr (Model::Bits == 8)
                {
                    return CRC8TableByteUpdateHelper<Model>::Update(table, crc, byte);
                }
                else if constexpr (Model::refin)
                {
                    return static_cast<value_type>(table[static_cast<UInt8>(crc ^ byte)] ^ traits_type::ShiftRight8(crc));
                }
                else
                {
                    return static_cast<value_type>(table[static_cast<UInt8>((crc >> (Model::Bits - 8)) ^ byte)] ^ (crc << 8));
                }
            }
        };

        template<typename Model>
        struct CRC8BitwiseByteUpdateHelper
        {
            using value_type = ModelValue<Model>;

            [[nodiscard]] static constexpr value_type Update(value_type crc, UInt8 byte) noexcept
            {
                if constexpr (Model::refin)
                {
                    crc ^= byte;
                    for (int bit = 0; bit < 8; ++bit)
                        crc = (crc & static_cast<value_type>(1))
                                      ? static_cast<value_type>((crc >> 1) ^ Model::polynomial)
                                      : static_cast<value_type>(crc >> 1);
                }
                else
                {
                    crc ^= static_cast<value_type>(byte);
                    for (int bit = 0; bit < 8; ++bit)
                    {
                        crc = (crc & (static_cast<value_type>(1) << 7))
                                      ? static_cast<value_type>((crc << 1) ^ Model::polynomial)
                                      : static_cast<value_type>(crc << 1);
                    }
                }

                return crc;
            }
        };

        template<typename Model>
        struct BitwiseByteUpdateHelper
        {
            using value_type = ModelValue<Model>;

            [[nodiscard]] static constexpr value_type Update(value_type crc, UInt8 byte) noexcept
            {
                if constexpr (Model::Bits == 8)
                {
                    return CRC8BitwiseByteUpdateHelper<Model>::Update(crc, byte);
                }

                if constexpr (Model::refin)
                {
                    crc ^= byte;
                    for (int bit = 0; bit < 8; ++bit)
                        crc = (crc & static_cast<value_type>(1))
                                      ? static_cast<value_type>((crc >> 1) ^ Model::polynomial)
                                      : static_cast<value_type>(crc >> 1);
                }
                else
                {
                    crc ^= static_cast<value_type>(byte) << (Model::Bits - 8);
                    for (int bit = 0; bit < 8; ++bit)
                    {
                        crc = (crc & (static_cast<value_type>(1) << (Model::Bits - 1)))
                                      ? static_cast<value_type>((crc << 1) ^ Model::polynomial)
                                      : static_cast<value_type>(crc << 1);
                    }
                }

                return crc;
            }
        };

        [[nodiscard]] constexpr UInt32 LoadLittleEndian32(const UInt8* data) noexcept
        {
            return static_cast<UInt32>(data[0]) | (static_cast<UInt32>(data[1]) << 8) |
                   (static_cast<UInt32>(data[2]) << 16) | (static_cast<UInt32>(data[3]) << 24);
        }

        [[nodiscard]] constexpr UInt64 LoadLittleEndian64(const UInt8* data) noexcept
        {
            return static_cast<UInt64>(data[0]) | (static_cast<UInt64>(data[1]) << 8) |
                   (static_cast<UInt64>(data[2]) << 16) | (static_cast<UInt64>(data[3]) << 24) |
                   (static_cast<UInt64>(data[4]) << 32) | (static_cast<UInt64>(data[5]) << 40) |
                   (static_cast<UInt64>(data[6]) << 48) | (static_cast<UInt64>(data[7]) << 56);
        }

        [[nodiscard]] inline std::span<const UInt8> ByteSpan(std::string_view text) noexcept
        {
            return std::span<const UInt8> {reinterpret_cast<const UInt8*>(text.data()), text.size()};
        }

        template<typename Model, unsigned Slices, unsigned Bits = Model::Bits>
        struct ReflectedSlicingUpdateHelper
        {
            using value_type = ModelValue<Model>;

            template<typename Tables>
            [[nodiscard]] static constexpr value_type Update(value_type             crc,
                                                             const Tables&          tables,
                                                             std::span<const UInt8> data) noexcept
            {
                for (UInt8 byte: data)
                    crc = TableByteUpdateHelper<Model>::Update(tables[0], crc, byte);
                return crc;
            }
        };

        template<typename Model>
        struct ReflectedSlicingUpdateHelper<Model, 8, 32>
        {
            using value_type  = ModelValue<Model>;
            using traits_type = CRCValueTraits<value_type>;

            template<typename Tables>
            [[nodiscard]] static constexpr value_type Update(value_type             crc,
                                                             const Tables&          tables,
                                                             std::span<const UInt8> data) noexcept
            {
                while (data.size() >= 8)
                {
                    crc ^= LoadLittleEndian32(data.data());
                    crc = static_cast<value_type>(
                            tables[7][traits_type::ByteAt(crc, 0)] ^ tables[6][traits_type::ByteAt(crc, 8)] ^
                            tables[5][traits_type::ByteAt(crc, 16)] ^ tables[4][traits_type::ByteAt(crc, 24)] ^
                            tables[3][data[4]] ^ tables[2][data[5]] ^ tables[1][data[6]] ^ tables[0][data[7]]);
                    data = data.subspan(8);
                }

                for (UInt8 byte: data)
                    crc = TableByteUpdateHelper<Model>::Update(tables[0], crc, byte);

                return crc;
            }
        };

        template<typename Model>
        struct ReflectedSlicingUpdateHelper<Model, 8, 64>
        {
            using value_type  = ModelValue<Model>;
            using traits_type = CRCValueTraits<value_type>;

            template<typename Tables>
            [[nodiscard]] static constexpr value_type Update(value_type             crc,
                                                             const Tables&          tables,
                                                             std::span<const UInt8> data) noexcept
            {
                while (data.size() >= 8)
                {
                    crc ^= LoadLittleEndian64(data.data());
                    crc = static_cast<value_type>(
                            tables[7][traits_type::ByteAt(crc, 0)] ^ tables[6][traits_type::ByteAt(crc, 8)] ^
                            tables[5][traits_type::ByteAt(crc, 16)] ^ tables[4][traits_type::ByteAt(crc, 24)] ^
                            tables[3][traits_type::ByteAt(crc, 32)] ^ tables[2][traits_type::ByteAt(crc, 40)] ^
                            tables[1][traits_type::ByteAt(crc, 48)] ^ tables[0][traits_type::ByteAt(crc, 56)]);
                    data = data.subspan(8);
                }

                for (UInt8 byte: data)
                    crc = TableByteUpdateHelper<Model>::Update(tables[0], crc, byte);

                return crc;
            }
        };
    }// namespace detail

    template<typename Model, unsigned Slices = 1>
    struct TableCRCEngine
    {
        using model_type   = Model;
        using value_type   = typename Model::value_type;
        using storage_type = detail::TableStorage<Model, Slices>;

        inline static constexpr unsigned Bits = Model::Bits;

        static_assert(Slices >= 1, "TableCRCEngine requires at least one lookup table");

        [[nodiscard]] static constexpr value_type Initial() noexcept
        {
            return Model::init;
        }

        [[nodiscard]] static constexpr value_type Update(value_type crc, std::span<const UInt8> data) noexcept
        {
            if (data.empty())
                return crc;

            if constexpr (Slices == 1)
            {
                for (UInt8 byte: data)
                    crc = detail::TableByteUpdateHelper<Model>::Update(storage_type::table0, crc, byte);
                return crc;
            }
            else
            {
                return detail::ReflectedSlicingUpdateHelper<Model, Slices>::Update(crc, storage_type::tables, data);
            }
        }

        [[nodiscard]] static constexpr value_type Update(value_type crc, const UInt8* data, UIntSize len) noexcept
        {
            if (!data || len == 0)
                return crc;

            return Update(crc, std::span<const UInt8> {data, static_cast<std::size_t>(len)});
        }

        [[nodiscard]] static constexpr value_type Update(value_type crc, std::string_view text) noexcept
        {
            if consteval
            {
                for (unsigned char ch: text)
                    crc = detail::TableByteUpdateHelper<Model>::Update(storage_type::table0, crc, static_cast<UInt8>(ch));
                return crc;
            }
            else
            {
                return Update(crc, detail::ByteSpan(text));
            }
        }

        [[nodiscard]] static constexpr value_type Finalize(value_type crc) noexcept
        {
            if constexpr (Model::refin != Model::refout)
                crc = detail::BitReverse(crc);
            return static_cast<value_type>(crc ^ Model::xorout);
        }

        [[nodiscard]] static constexpr value_type Compute(std::span<const UInt8> data) noexcept
        {
            return Finalize(Update(Initial(), data));
        }

        [[nodiscard]] static constexpr value_type Compute(const UInt8* data, UIntSize len) noexcept
        {
            return Finalize(Update(Initial(), data, len));
        }

        [[nodiscard]] static constexpr value_type Compute(std::string_view text) noexcept
        {
            return Finalize(Update(Initial(), text));
        }
    };

    template<typename Model>
    struct BitwiseCRCEngine
    {
        using model_type = Model;
        using value_type = typename Model::value_type;

        inline static constexpr unsigned Bits = Model::Bits;

        [[nodiscard]] static constexpr value_type Initial() noexcept
        {
            return Model::init;
        }

        [[nodiscard]] static constexpr value_type Update(value_type crc, std::span<const UInt8> data) noexcept
        {
            for (UInt8 byte: data)
                crc = detail::BitwiseByteUpdateHelper<Model>::Update(crc, byte);
            return crc;
        }

        [[nodiscard]] static constexpr value_type Update(value_type crc, const UInt8* data, UIntSize len) noexcept
        {
            if (!data || len == 0)
                return crc;

            return Update(crc, std::span<const UInt8> {data, static_cast<std::size_t>(len)});
        }

        [[nodiscard]] static constexpr value_type Update(value_type crc, std::string_view text) noexcept
        {
            if consteval
            {
                for (unsigned char ch: text)
                    crc = detail::BitwiseByteUpdateHelper<Model>::Update(crc, static_cast<UInt8>(ch));
                return crc;
            }
            else
            {
                return Update(crc, detail::ByteSpan(text));
            }
        }

        [[nodiscard]] static constexpr value_type Finalize(value_type crc) noexcept
        {
            if constexpr (Model::refin != Model::refout)
                crc = detail::BitReverse(crc);
            return static_cast<value_type>(crc ^ Model::xorout);
        }

        [[nodiscard]] static constexpr value_type Compute(std::span<const UInt8> data) noexcept
        {
            return Finalize(Update(Initial(), data));
        }

        [[nodiscard]] static constexpr value_type Compute(const UInt8* data, UIntSize len) noexcept
        {
            return Finalize(Update(Initial(), data, len));
        }

        [[nodiscard]] static constexpr value_type Compute(std::string_view text) noexcept
        {
            return Finalize(Update(Initial(), text));
        }
    };

    template<typename Engine>
    class CRCState
    {
    public:
        using engine_type = Engine;
        using value_type  = typename Engine::value_type;

        constexpr CRCState() noexcept
            : m_crc(Engine::Initial())
        {
        }

        constexpr void Update(std::span<const UInt8> data) noexcept
        {
            m_crc = Engine::Update(m_crc, data);
        }

        constexpr void Update(const UInt8* data, UIntSize len) noexcept
        {
            m_crc = Engine::Update(m_crc, data, len);
        }

        constexpr void Update(std::string_view text) noexcept
        {
            m_crc = Engine::Update(m_crc, text);
        }

        [[nodiscard]] constexpr value_type Finalize() const noexcept
        {
            return Engine::Finalize(m_crc);
        }

        constexpr void Reset() noexcept
        {
            m_crc = Engine::Initial();
        }

        [[nodiscard]] constexpr value_type Raw() const noexcept
        {
            return m_crc;
        }

    private:
        value_type m_crc;
    };

    namespace CRC8
    {
        using SMBUSModel     = CRCModel<UInt8, 0x07, 0x00, false, false, 0x00>;
        using MAXIM_DOWModel = CRCModel<UInt8, 0x8C, 0x00, true, true, 0x00>;
        using AUTOSARModel   = CRCModel<UInt8, 0x2F, 0xFF, false, false, 0xFF>;
        using SAE_J1850Model = CRCModel<UInt8, 0x1D, 0xFF, false, false, 0xFF>;
        using BLUETOOTHModel = CRCModel<UInt8, 0xE5, 0x00, true, true, 0x00>;

        using SMBUSEngine     = TableCRCEngine<SMBUSModel>;
        using MAXIM_DOWEngine = TableCRCEngine<MAXIM_DOWModel>;
        using AUTOSAREngine   = TableCRCEngine<AUTOSARModel>;
        using SAE_J1850Engine = TableCRCEngine<SAE_J1850Model>;
        using BLUETOOTHEngine = TableCRCEngine<BLUETOOTHModel>;

        using SMBUSState     = CRCState<SMBUSEngine>;
        using MAXIM_DOWState = CRCState<MAXIM_DOWEngine>;
        using AUTOSARState   = CRCState<AUTOSAREngine>;
        using SAE_J1850State = CRCState<SAE_J1850Engine>;
        using BLUETOOTHState = CRCState<BLUETOOTHEngine>;

        /// @brief CRC-8/SMBUS (aka CRC-8).
        /// @details poly=0x07, init=0x00, refin=false, refout=false, xorout=0x00.
        [[nodiscard]] constexpr UInt8 SMBUS(std::span<const UInt8> data) noexcept
        {
            return SMBUSEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt8 SMBUS(const UInt8* data, UIntSize len) noexcept
        {
            return SMBUSEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt8 SMBUS(std::string_view text) noexcept
        {
            return SMBUSEngine::Compute(text);
        }

        /// @brief CRC-8/MAXIM-DOW (Dallas/Maxim).
        /// @details reflected poly=0x8C (bit-reflected 0x31), init=0x00, refin=true, refout=true, xorout=0x00.
        [[nodiscard]] constexpr UInt8 MAXIM_DOW(std::span<const UInt8> data) noexcept
        {
            return MAXIM_DOWEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt8 MAXIM_DOW(const UInt8* data, UIntSize len) noexcept
        {
            return MAXIM_DOWEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt8 MAXIM_DOW(std::string_view text) noexcept
        {
            return MAXIM_DOWEngine::Compute(text);
        }

        /// @brief CRC-8/AUTOSAR.
        /// @details poly=0x2F, init=0xFF, refin=false, refout=false, xorout=0xFF.
        [[nodiscard]] constexpr UInt8 AUTOSAR(std::span<const UInt8> data) noexcept
        {
            return AUTOSAREngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt8 AUTOSAR(const UInt8* data, UIntSize len) noexcept
        {
            return AUTOSAREngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt8 AUTOSAR(std::string_view text) noexcept
        {
            return AUTOSAREngine::Compute(text);
        }

        /// @brief CRC-8/SAE-J1850.
        /// @details poly=0x1D, init=0xFF, refin=false, refout=false, xorout=0xFF.
        [[nodiscard]] constexpr UInt8 SAE_J1850(std::span<const UInt8> data) noexcept
        {
            return SAE_J1850Engine::Compute(data);
        }

        [[nodiscard]] constexpr UInt8 SAE_J1850(const UInt8* data, UIntSize len) noexcept
        {
            return SAE_J1850Engine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt8 SAE_J1850(std::string_view text) noexcept
        {
            return SAE_J1850Engine::Compute(text);
        }

        /// @brief CRC-8/BLUETOOTH.
        /// @details reflected poly=0xE5 (bit-reflected 0xA7), init=0x00, refin=true, refout=true, xorout=0x00.
        [[nodiscard]] constexpr UInt8 BLUETOOTH(std::span<const UInt8> data) noexcept
        {
            return BLUETOOTHEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt8 BLUETOOTH(const UInt8* data, UIntSize len) noexcept
        {
            return BLUETOOTHEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt8 BLUETOOTH(std::string_view text) noexcept
        {
            return BLUETOOTHEngine::Compute(text);
        }
    }// namespace CRC8

    namespace CRC16
    {
        using CCITT_FALSEModel       = CRCModel<UInt16, 0x1021, 0xFFFF, false, false, 0x0000>;
        using ARCModel               = CRCModel<UInt16, 0xA001, 0x0000, true, true, 0x0000>;
        using IBM_3740Model          = CRCModel<UInt16, 0x1021, 0xFFFF, false, false, 0x0000>;
        using XMODEMModel            = CRCModel<UInt16, 0x1021, 0x0000, false, false, 0x0000>;
        using KERMITModel            = CRCModel<UInt16, 0x8408, 0x0000, true, true, 0x0000>;
        using MODBUSModel            = CRCModel<UInt16, 0xA001, 0xFFFF, true, true, 0x0000>;
        using IBM_SDLCModel          = CRCModel<UInt16, 0x8408, 0xFFFF, true, true, 0xFFFF>;
        using GENIBUSModel           = CRCModel<UInt16, 0x1021, 0xFFFF, false, false, 0xFFFF>;
        using USBModel               = CRCModel<UInt16, 0xA001, 0xFFFF, true, true, 0xFFFF>;
        using MAXIM_DOWModel         = CRCModel<UInt16, 0xA001, 0x0000, true, true, 0xFFFF>;
        using MCRF4XXModel           = CRCModel<UInt16, 0x8408, 0xFFFF, true, true, 0x0000>;
        using DNPModel               = CRCModel<UInt16, 0xA6BC, 0x0000, true, true, 0xFFFF>;
        using EN_13757Model          = CRCModel<UInt16, 0x3D65, 0x0000, false, false, 0xFFFF>;
        using DECT_RModel            = CRCModel<UInt16, 0x0589, 0x0000, false, false, 0x0001>;
        using DECT_XModel            = CRCModel<UInt16, 0x0589, 0x0000, false, false, 0x0000>;
        using UMTSModel              = CRCModel<UInt16, 0x8005, 0x0000, false, false, 0x0000>;
        using ISO_IEC_14443_3_AModel = CRCModel<UInt16, 0x8408, 0x6363, true, true, 0x0000>;
        using T10_DIFModel           = CRCModel<UInt16, 0x8BB7, 0x0000, false, false, 0x0000>;
        using PROFIBUSModel          = CRCModel<UInt16, 0x1DCF, 0xFFFF, false, false, 0xFFFF>;
        using LJ1200Model            = CRCModel<UInt16, 0x6F63, 0x0000, false, false, 0x0000>;
        using OPENSAFETY_AModel      = CRCModel<UInt16, 0x5935, 0x0000, false, false, 0x0000>;
        using OPENSAFETY_BModel      = CRCModel<UInt16, 0x755B, 0x0000, false, false, 0x0000>;
        using NRSC_5Model            = CRCModel<UInt16, 0xD010, 0xFFFF, true, true, 0x0000>;
        using CMSModel               = CRCModel<UInt16, 0x8005, 0xFFFF, false, false, 0x0000>;
        using DDS_110Model           = CRCModel<UInt16, 0x8005, 0x800D, false, false, 0x0000>;
        using M17Model               = CRCModel<UInt16, 0x5935, 0xFFFF, false, false, 0x0000>;
        using TELEDISKModel          = CRCModel<UInt16, 0xA097, 0x0000, false, false, 0x0000>;
        using TMS37157Model          = CRCModel<UInt16, 0x8408, 0x3791, true, true, 0x0000>;

        using CCITT_FALSEEngine       = TableCRCEngine<CCITT_FALSEModel>;
        using ARCEngine               = TableCRCEngine<ARCModel>;
        using IBM_3740Engine          = TableCRCEngine<IBM_3740Model>;
        using XMODEMEngine            = TableCRCEngine<XMODEMModel>;
        using KERMITEngine            = TableCRCEngine<KERMITModel>;
        using MODBUSEngine            = TableCRCEngine<MODBUSModel>;
        using IBM_SDLCEngine          = TableCRCEngine<IBM_SDLCModel>;
        using GENIBUSEngine           = TableCRCEngine<GENIBUSModel>;
        using USBEngine               = TableCRCEngine<USBModel>;
        using MAXIM_DOWEngine         = TableCRCEngine<MAXIM_DOWModel>;
        using MCRF4XXEngine           = TableCRCEngine<MCRF4XXModel>;
        using DNPEngine               = TableCRCEngine<DNPModel>;
        using EN_13757Engine          = TableCRCEngine<EN_13757Model>;
        using DECT_REngine            = TableCRCEngine<DECT_RModel>;
        using DECT_XEngine            = TableCRCEngine<DECT_XModel>;
        using UMTSEngine              = TableCRCEngine<UMTSModel>;
        using ISO_IEC_14443_3_AEngine = TableCRCEngine<ISO_IEC_14443_3_AModel>;
        using T10_DIFEngine           = TableCRCEngine<T10_DIFModel>;
        using PROFIBUSEngine          = TableCRCEngine<PROFIBUSModel>;
        using LJ1200Engine            = TableCRCEngine<LJ1200Model>;
        using OPENSAFETY_AEngine      = TableCRCEngine<OPENSAFETY_AModel>;
        using OPENSAFETY_BEngine      = TableCRCEngine<OPENSAFETY_BModel>;
        using NRSC_5Engine            = TableCRCEngine<NRSC_5Model>;
        using CMSEngine               = TableCRCEngine<CMSModel>;
        using DDS_110Engine           = TableCRCEngine<DDS_110Model>;
        using M17Engine               = TableCRCEngine<M17Model>;
        using TELEDISKEngine          = TableCRCEngine<TELEDISKModel>;
        using TMS37157Engine          = TableCRCEngine<TMS37157Model>;

        using CCITT_FALSEState       = CRCState<CCITT_FALSEEngine>;
        using ARCState               = CRCState<ARCEngine>;
        using IBM_3740State          = CRCState<IBM_3740Engine>;
        using XMODEMState            = CRCState<XMODEMEngine>;
        using KERMITState            = CRCState<KERMITEngine>;
        using MODBUSState            = CRCState<MODBUSEngine>;
        using IBM_SDLCState          = CRCState<IBM_SDLCEngine>;
        using GENIBUSState           = CRCState<GENIBUSEngine>;
        using USBState               = CRCState<USBEngine>;
        using MAXIM_DOWState         = CRCState<MAXIM_DOWEngine>;
        using MCRF4XXState           = CRCState<MCRF4XXEngine>;
        using DNPState               = CRCState<DNPEngine>;
        using EN_13757State          = CRCState<EN_13757Engine>;
        using DECT_RState            = CRCState<DECT_REngine>;
        using DECT_XState            = CRCState<DECT_XEngine>;
        using UMTSState              = CRCState<UMTSEngine>;
        using ISO_IEC_14443_3_AState = CRCState<ISO_IEC_14443_3_AEngine>;
        using T10_DIFState           = CRCState<T10_DIFEngine>;
        using PROFIBUSState          = CRCState<PROFIBUSEngine>;
        using LJ1200State            = CRCState<LJ1200Engine>;
        using OPENSAFETY_AState      = CRCState<OPENSAFETY_AEngine>;
        using OPENSAFETY_BState      = CRCState<OPENSAFETY_BEngine>;
        using NRSC_5State            = CRCState<NRSC_5Engine>;
        using CMSState               = CRCState<CMSEngine>;
        using DDS_110State           = CRCState<DDS_110Engine>;
        using M17State               = CRCState<M17Engine>;
        using TELEDISKState          = CRCState<TELEDISKEngine>;
        using TMS37157State          = CRCState<TMS37157Engine>;

        /// @brief Compute CRC-16/CCITT-FALSE checksum.
        /// @details Implements CRC-16/CCITT-FALSE (poly=0x1021, init=0xFFFF, no reflection, xorout=0x0000).
        [[nodiscard]] constexpr UInt16 CCITT_FALSE(std::span<const UInt8> data) noexcept
        {
            return CCITT_FALSEEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt16 CCITT_FALSE(const UInt8* data, UIntSize len) noexcept
        {
            return CCITT_FALSEEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt16 CCITT_FALSE(std::string_view text) noexcept
        {
            return CCITT_FALSEEngine::Compute(text);
        }

        /// @brief CRC-16/ARC (aka CRC-16, CRC-IBM, CRC-16/LHA).
        /// @details reflected poly=0xA001 (bit-reflected 0x8005), init=0x0000, refin=true, refout=true, xorout=0x0000.
        [[nodiscard]] constexpr UInt16 ARC(std::span<const UInt8> data) noexcept
        {
            return ARCEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt16 ARC(const UInt8* data, UIntSize len) noexcept
        {
            return ARCEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt16 ARC(std::string_view text) noexcept
        {
            return ARCEngine::Compute(text);
        }

        /// @brief CRC-16/IBM-3740 (aka CRC-16/AUTOSAR, CRC-16/CCITT-FALSE).
        /// @details poly=0x1021, init=0xFFFF, refin=false, refout=false, xorout=0x0000.
        [[nodiscard]] constexpr UInt16 IBM_3740(std::span<const UInt8> data) noexcept
        {
            return IBM_3740Engine::Compute(data);
        }

        [[nodiscard]] constexpr UInt16 IBM_3740(const UInt8* data, UIntSize len) noexcept
        {
            return IBM_3740Engine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt16 IBM_3740(std::string_view text) noexcept
        {
            return IBM_3740Engine::Compute(text);
        }

        /// @brief CRC-16/XMODEM (aka CRC-16/ACORN, CRC-16/LTE, CRC-16/V-41-MSB).
        /// @details poly=0x1021, init=0x0000, refin=false, refout=false, xorout=0x0000.
        [[nodiscard]] constexpr UInt16 XMODEM(std::span<const UInt8> data) noexcept
        {
            return XMODEMEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt16 XMODEM(const UInt8* data, UIntSize len) noexcept
        {
            return XMODEMEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt16 XMODEM(std::string_view text) noexcept
        {
            return XMODEMEngine::Compute(text);
        }

        /// @brief CRC-16/KERMIT (aka CRC-16/CCITT, CRC-16/BLUETOOTH).
        /// @details reflected poly=0x8408 (bit-reflected 0x1021), init=0x0000, refin=true, refout=true, xorout=0x0000.
        [[nodiscard]] constexpr UInt16 KERMIT(std::span<const UInt8> data) noexcept
        {
            return KERMITEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt16 KERMIT(const UInt8* data, UIntSize len) noexcept
        {
            return KERMITEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt16 KERMIT(std::string_view text) noexcept
        {
            return KERMITEngine::Compute(text);
        }

        /// @brief CRC-16/MODBUS.
        /// @details reflected poly=0xA001 (bit-reflected 0x8005), init=0xFFFF, refin=true, refout=true, xorout=0x0000.
        [[nodiscard]] constexpr UInt16 MODBUS(std::span<const UInt8> data) noexcept
        {
            return MODBUSEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt16 MODBUS(const UInt8* data, UIntSize len) noexcept
        {
            return MODBUSEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt16 MODBUS(std::string_view text) noexcept
        {
            return MODBUSEngine::Compute(text);
        }

        /// @brief CRC-16/IBM-SDLC (aka CRC-16/X-25, CRC-B).
        /// @details reflected poly=0x8408 (bit-reflected 0x1021), init=0xFFFF, refin=true, refout=true, xorout=0xFFFF.
        [[nodiscard]] constexpr UInt16 IBM_SDLC(std::span<const UInt8> data) noexcept
        {
            return IBM_SDLCEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt16 IBM_SDLC(const UInt8* data, UIntSize len) noexcept
        {
            return IBM_SDLCEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt16 IBM_SDLC(std::string_view text) noexcept
        {
            return IBM_SDLCEngine::Compute(text);
        }

        /// @brief CRC-16/GENIBUS (aka CRC-16/DARC, CRC-16/EPC, CRC-16/I-CODE).
        /// @details poly=0x1021, init=0xFFFF, refin=false, refout=false, xorout=0xFFFF.
        [[nodiscard]] constexpr UInt16 GENIBUS(std::span<const UInt8> data) noexcept
        {
            return GENIBUSEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt16 GENIBUS(const UInt8* data, UIntSize len) noexcept
        {
            return GENIBUSEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt16 GENIBUS(std::string_view text) noexcept
        {
            return GENIBUSEngine::Compute(text);
        }

        /// @brief CRC-16/USB.
        /// @details reflected poly=0xA001 (bit-reflected 0x8005), init=0xFFFF, refin=true, refout=true, xorout=0xFFFF.
        [[nodiscard]] constexpr UInt16 USB(std::span<const UInt8> data) noexcept
        {
            return USBEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt16 USB(const UInt8* data, UIntSize len) noexcept
        {
            return USBEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt16 USB(std::string_view text) noexcept
        {
            return USBEngine::Compute(text);
        }

        /// @brief CRC-16/MAXIM-DOW.
        /// @details reflected poly=0xA001 (bit-reflected 0x8005), init=0x0000, refin=true, refout=true, xorout=0xFFFF.
        [[nodiscard]] constexpr UInt16 MAXIM_DOW(std::span<const UInt8> data) noexcept
        {
            return MAXIM_DOWEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt16 MAXIM_DOW(const UInt8* data, UIntSize len) noexcept
        {
            return MAXIM_DOWEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt16 MAXIM_DOW(std::string_view text) noexcept
        {
            return MAXIM_DOWEngine::Compute(text);
        }

        /// @brief CRC-16/MCRF4XX.
        /// @details reflected poly=0x8408 (bit-reflected 0x1021), init=0xFFFF, refin=true, refout=true, xorout=0x0000.
        [[nodiscard]] constexpr UInt16 MCRF4XX(std::span<const UInt8> data) noexcept
        {
            return MCRF4XXEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt16 MCRF4XX(const UInt8* data, UIntSize len) noexcept
        {
            return MCRF4XXEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt16 MCRF4XX(std::string_view text) noexcept
        {
            return MCRF4XXEngine::Compute(text);
        }

        /// @brief CRC-16/DNP.
        /// @details reflected poly=0xA6BC (bit-reflected 0x3D65), init=0x0000, refin=true, refout=true, xorout=0xFFFF.
        [[nodiscard]] constexpr UInt16 DNP(std::span<const UInt8> data) noexcept
        {
            return DNPEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt16 DNP(const UInt8* data, UIntSize len) noexcept
        {
            return DNPEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt16 DNP(std::string_view text) noexcept
        {
            return DNPEngine::Compute(text);
        }

        /// @brief CRC-16/EN-13757.
        /// @details poly=0x3D65, init=0x0000, refin=false, refout=false, xorout=0xFFFF.
        [[nodiscard]] constexpr UInt16 EN_13757(std::span<const UInt8> data) noexcept
        {
            return EN_13757Engine::Compute(data);
        }

        [[nodiscard]] constexpr UInt16 EN_13757(const UInt8* data, UIntSize len) noexcept
        {
            return EN_13757Engine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt16 EN_13757(std::string_view text) noexcept
        {
            return EN_13757Engine::Compute(text);
        }

        /// @brief CRC-16/DECT-R.
        /// @details poly=0x0589, init=0x0000, refin=false, refout=false, xorout=0x0001.
        [[nodiscard]] constexpr UInt16 DECT_R(std::span<const UInt8> data) noexcept
        {
            return DECT_REngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt16 DECT_R(const UInt8* data, UIntSize len) noexcept
        {
            return DECT_REngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt16 DECT_R(std::string_view text) noexcept
        {
            return DECT_REngine::Compute(text);
        }

        /// @brief CRC-16/DECT-X.
        /// @details poly=0x0589, init=0x0000, refin=false, refout=false, xorout=0x0000.
        [[nodiscard]] constexpr UInt16 DECT_X(std::span<const UInt8> data) noexcept
        {
            return DECT_XEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt16 DECT_X(const UInt8* data, UIntSize len) noexcept
        {
            return DECT_XEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt16 DECT_X(std::string_view text) noexcept
        {
            return DECT_XEngine::Compute(text);
        }

        /// @brief CRC-16/UMTS.
        /// @details poly=0x8005, init=0x0000, refin=false, refout=false, xorout=0x0000.
        [[nodiscard]] constexpr UInt16 UMTS(std::span<const UInt8> data) noexcept
        {
            return UMTSEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt16 UMTS(const UInt8* data, UIntSize len) noexcept
        {
            return UMTSEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt16 UMTS(std::string_view text) noexcept
        {
            return UMTSEngine::Compute(text);
        }

        /// @brief CRC-16/ISO-IEC-14443-3-A (CRC-A).
        /// @details reflected poly=0x8408 (bit-reflected 0x1021), init=0x6363, refin=true, refout=true, xorout=0x0000.
        [[nodiscard]] constexpr UInt16 ISO_IEC_14443_3_A(std::span<const UInt8> data) noexcept
        {
            return ISO_IEC_14443_3_AEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt16 ISO_IEC_14443_3_A(const UInt8* data, UIntSize len) noexcept
        {
            return ISO_IEC_14443_3_AEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt16 ISO_IEC_14443_3_A(std::string_view text) noexcept
        {
            return ISO_IEC_14443_3_AEngine::Compute(text);
        }

        /// @brief CRC-16/T10-DIF.
        /// @details poly=0x8BB7, init=0x0000, refin=false, refout=false, xorout=0x0000.
        [[nodiscard]] constexpr UInt16 T10_DIF(std::span<const UInt8> data) noexcept
        {
            return T10_DIFEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt16 T10_DIF(const UInt8* data, UIntSize len) noexcept
        {
            return T10_DIFEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt16 T10_DIF(std::string_view text) noexcept
        {
            return T10_DIFEngine::Compute(text);
        }

        /// @brief CRC-16/PROFIBUS.
        /// @details poly=0x1DCF, init=0xFFFF, refin=false, refout=false, xorout=0xFFFF.
        [[nodiscard]] constexpr UInt16 PROFIBUS(std::span<const UInt8> data) noexcept
        {
            return PROFIBUSEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt16 PROFIBUS(const UInt8* data, UIntSize len) noexcept
        {
            return PROFIBUSEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt16 PROFIBUS(std::string_view text) noexcept
        {
            return PROFIBUSEngine::Compute(text);
        }

        /// @brief CRC-16/LJ1200.
        /// @details poly=0x6F63, init=0x0000, refin=false, refout=false, xorout=0x0000.
        [[nodiscard]] constexpr UInt16 LJ1200(std::span<const UInt8> data) noexcept
        {
            return LJ1200Engine::Compute(data);
        }

        [[nodiscard]] constexpr UInt16 LJ1200(const UInt8* data, UIntSize len) noexcept
        {
            return LJ1200Engine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt16 LJ1200(std::string_view text) noexcept
        {
            return LJ1200Engine::Compute(text);
        }

        /// @brief CRC-16/OPENSAFETY-A.
        /// @details poly=0x5935, init=0x0000, refin=false, refout=false, xorout=0x0000.
        [[nodiscard]] constexpr UInt16 OPENSAFETY_A(std::span<const UInt8> data) noexcept
        {
            return OPENSAFETY_AEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt16 OPENSAFETY_A(const UInt8* data, UIntSize len) noexcept
        {
            return OPENSAFETY_AEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt16 OPENSAFETY_A(std::string_view text) noexcept
        {
            return OPENSAFETY_AEngine::Compute(text);
        }

        /// @brief CRC-16/OPENSAFETY-B.
        /// @details poly=0x755B, init=0x0000, refin=false, refout=false, xorout=0x0000.
        [[nodiscard]] constexpr UInt16 OPENSAFETY_B(std::span<const UInt8> data) noexcept
        {
            return OPENSAFETY_BEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt16 OPENSAFETY_B(const UInt8* data, UIntSize len) noexcept
        {
            return OPENSAFETY_BEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt16 OPENSAFETY_B(std::string_view text) noexcept
        {
            return OPENSAFETY_BEngine::Compute(text);
        }

        /// @brief CRC-16/NRSC-5.
        /// @details reflected poly=0xD010 (bit-reflected 0x080B), init=0xFFFF, refin=true, refout=true, xorout=0x0000.
        [[nodiscard]] constexpr UInt16 NRSC_5(std::span<const UInt8> data) noexcept
        {
            return NRSC_5Engine::Compute(data);
        }

        [[nodiscard]] constexpr UInt16 NRSC_5(const UInt8* data, UIntSize len) noexcept
        {
            return NRSC_5Engine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt16 NRSC_5(std::string_view text) noexcept
        {
            return NRSC_5Engine::Compute(text);
        }

        /// @brief CRC-16/CMS.
        /// @details poly=0x8005, init=0xFFFF, refin=false, refout=false, xorout=0x0000.
        [[nodiscard]] constexpr UInt16 CMS(std::span<const UInt8> data) noexcept
        {
            return CMSEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt16 CMS(const UInt8* data, UIntSize len) noexcept
        {
            return CMSEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt16 CMS(std::string_view text) noexcept
        {
            return CMSEngine::Compute(text);
        }

        /// @brief CRC-16/DDS-110.
        /// @details poly=0x8005, init=0x800D, refin=false, refout=false, xorout=0x0000.
        [[nodiscard]] constexpr UInt16 DDS_110(std::span<const UInt8> data) noexcept
        {
            return DDS_110Engine::Compute(data);
        }

        [[nodiscard]] constexpr UInt16 DDS_110(const UInt8* data, UIntSize len) noexcept
        {
            return DDS_110Engine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt16 DDS_110(std::string_view text) noexcept
        {
            return DDS_110Engine::Compute(text);
        }

        /// @brief CRC-16/M17.
        /// @details poly=0x5935, init=0xFFFF, refin=false, refout=false, xorout=0x0000.
        [[nodiscard]] constexpr UInt16 M17(std::span<const UInt8> data) noexcept
        {
            return M17Engine::Compute(data);
        }

        [[nodiscard]] constexpr UInt16 M17(const UInt8* data, UIntSize len) noexcept
        {
            return M17Engine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt16 M17(std::string_view text) noexcept
        {
            return M17Engine::Compute(text);
        }

        /// @brief CRC-16/TELEDISK.
        /// @details poly=0xA097, init=0x0000, refin=false, refout=false, xorout=0x0000.
        [[nodiscard]] constexpr UInt16 TELEDISK(std::span<const UInt8> data) noexcept
        {
            return TELEDISKEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt16 TELEDISK(const UInt8* data, UIntSize len) noexcept
        {
            return TELEDISKEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt16 TELEDISK(std::string_view text) noexcept
        {
            return TELEDISKEngine::Compute(text);
        }

        /// @brief CRC-16/TMS37157.
        /// @details reflected poly=0x8408 (bit-reflected 0x1021), init=0x3791, refin=true, refout=true, xorout=0x0000.
        [[nodiscard]] constexpr UInt16 TMS37157(std::span<const UInt8> data) noexcept
        {
            return TMS37157Engine::Compute(data);
        }

        [[nodiscard]] constexpr UInt16 TMS37157(const UInt8* data, UIntSize len) noexcept
        {
            return TMS37157Engine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt16 TMS37157(std::string_view text) noexcept
        {
            return TMS37157Engine::Compute(text);
        }
    }// namespace CRC16

    namespace CRC32
    {
        using IEEE_802_3Model = CRCModel<UInt32, 0xEDB88320u, 0xFFFFFFFFu, true, true, 0xFFFFFFFFu>;
        using MPEG_2Model     = CRCModel<UInt32, 0x04C11DB7u, 0xFFFFFFFFu, false, false, 0x00000000u>;
        using AIXMModel       = CRCModel<UInt32, 0x814141ABu, 0x00000000u, false, false, 0x00000000u>;
        using AUTOSARModel    = CRCModel<UInt32, 0xC8DF352Fu, 0xFFFFFFFFu, true, true, 0xFFFFFFFFu>;
        using BASE91_DModel   = CRCModel<UInt32, 0xD419CC15u, 0xFFFFFFFFu, true, true, 0xFFFFFFFFu>;
        using BZIP2Model      = CRCModel<UInt32, 0x04C11DB7u, 0xFFFFFFFFu, false, false, 0xFFFFFFFFu>;
        using CD_ROM_EDCModel = CRCModel<UInt32, 0xD8018001u, 0x00000000u, true, true, 0x00000000u>;
        using CKSUMModel      = CRCModel<UInt32, 0x04C11DB7u, 0x00000000u, false, false, 0xFFFFFFFFu>;
        using ISCSIModel      = CRCModel<UInt32, 0x82F63B78u, 0xFFFFFFFFu, true, true, 0xFFFFFFFFu>;
        using JAMCRCModel     = CRCModel<UInt32, 0xEDB88320u, 0xFFFFFFFFu, true, true, 0x00000000u>;
        using MEFModel        = CRCModel<UInt32, 0xEB31D82Eu, 0xFFFFFFFFu, true, true, 0x00000000u>;
        using XFERModel       = CRCModel<UInt32, 0x000000AFu, 0x00000000u, false, false, 0x00000000u>;

        using IEEE_802_3Engine = TableCRCEngine<IEEE_802_3Model, 8>;
        using MPEG_2Engine     = TableCRCEngine<MPEG_2Model>;
        using AIXMEngine       = TableCRCEngine<AIXMModel>;
        using AUTOSAREngine    = TableCRCEngine<AUTOSARModel, 8>;
        using BASE91_DEngine   = TableCRCEngine<BASE91_DModel, 8>;
        using BZIP2Engine      = TableCRCEngine<BZIP2Model>;
        using CD_ROM_EDCEngine = TableCRCEngine<CD_ROM_EDCModel, 8>;
        using CKSUMEngine      = TableCRCEngine<CKSUMModel>;
        using ISCSIEngine      = TableCRCEngine<ISCSIModel, 8>;
        using ISO_HDLCModel    = IEEE_802_3Model;
        using ISO_HDLCEngine   = IEEE_802_3Engine;
        using JAMCRCEngine     = TableCRCEngine<JAMCRCModel, 8>;
        using MEFEngine        = TableCRCEngine<MEFModel, 8>;
        using XFEREngine       = TableCRCEngine<XFERModel>;

        using IEEE_802_3State = CRCState<IEEE_802_3Engine>;
        using MPEG_2State     = CRCState<MPEG_2Engine>;
        using AIXMState       = CRCState<AIXMEngine>;
        using AUTOSARState    = CRCState<AUTOSAREngine>;
        using BASE91_DState   = CRCState<BASE91_DEngine>;
        using BZIP2State      = CRCState<BZIP2Engine>;
        using CD_ROM_EDCState = CRCState<CD_ROM_EDCEngine>;
        using CKSUMState      = CRCState<CKSUMEngine>;
        using ISCSIState      = CRCState<ISCSIEngine>;
        using ISO_HDLCState   = CRCState<ISO_HDLCEngine>;
        using JAMCRCState     = CRCState<JAMCRCEngine>;
        using MEFState        = CRCState<MEFEngine>;
        using XFERState       = CRCState<XFEREngine>;

        /// @brief Compute CRC-32/IEEE-802.3 checksum.
        /// @details reflected poly=0xEDB88320, init=0xFFFFFFFF, refin=true, refout=true, xorout=0xFFFFFFFF.
        [[nodiscard]] constexpr UInt32 IEEE_802_3(std::span<const UInt8> data) noexcept
        {
            return IEEE_802_3Engine::Compute(data);
        }

        [[nodiscard]] constexpr UInt32 IEEE_802_3(const UInt8* data, UIntSize len) noexcept
        {
            return IEEE_802_3Engine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt32 IEEE_802_3(std::string_view text) noexcept
        {
            return IEEE_802_3Engine::Compute(text);
        }

        /// @brief Compute CRC-32/MPEG-2 checksum.
        /// @details poly=0x04C11DB7, init=0xFFFFFFFF, refin=false, refout=false, xorout=0x00000000.
        [[nodiscard]] constexpr UInt32 MPEG_2(std::span<const UInt8> data) noexcept
        {
            return MPEG_2Engine::Compute(data);
        }

        [[nodiscard]] constexpr UInt32 MPEG_2(const UInt8* data, UIntSize len) noexcept
        {
            return MPEG_2Engine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt32 MPEG_2(std::string_view text) noexcept
        {
            return MPEG_2Engine::Compute(text);
        }

        /// @brief Compute CRC-32/AIXM checksum.
        /// @details poly=0x814141AB, init=0x00000000, refin=false, refout=false, xorout=0x00000000.
        [[nodiscard]] constexpr UInt32 AIXM(std::span<const UInt8> data) noexcept
        {
            return AIXMEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt32 AIXM(const UInt8* data, UIntSize len) noexcept
        {
            return AIXMEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt32 AIXM(std::string_view text) noexcept
        {
            return AIXMEngine::Compute(text);
        }

        /// @brief Compute CRC-32/AUTOSAR checksum.
        /// @details reflected poly=0xC8DF352F, init=0xFFFFFFFF, refin=true, refout=true, xorout=0xFFFFFFFF.
        [[nodiscard]] constexpr UInt32 AUTOSAR(std::span<const UInt8> data) noexcept
        {
            return AUTOSAREngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt32 AUTOSAR(const UInt8* data, UIntSize len) noexcept
        {
            return AUTOSAREngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt32 AUTOSAR(std::string_view text) noexcept
        {
            return AUTOSAREngine::Compute(text);
        }

        /// @brief Compute CRC-32/BASE91-D checksum.
        /// @details reflected poly=0xD419CC15, init=0xFFFFFFFF, refin=true, refout=true, xorout=0xFFFFFFFF.
        [[nodiscard]] constexpr UInt32 BASE91_D(std::span<const UInt8> data) noexcept
        {
            return BASE91_DEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt32 BASE91_D(const UInt8* data, UIntSize len) noexcept
        {
            return BASE91_DEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt32 BASE91_D(std::string_view text) noexcept
        {
            return BASE91_DEngine::Compute(text);
        }

        /// @brief Compute CRC-32/BZIP2 checksum.
        /// @details poly=0x04C11DB7, init=0xFFFFFFFF, refin=false, refout=false, xorout=0xFFFFFFFF.
        [[nodiscard]] constexpr UInt32 BZIP2(std::span<const UInt8> data) noexcept
        {
            return BZIP2Engine::Compute(data);
        }

        [[nodiscard]] constexpr UInt32 BZIP2(const UInt8* data, UIntSize len) noexcept
        {
            return BZIP2Engine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt32 BZIP2(std::string_view text) noexcept
        {
            return BZIP2Engine::Compute(text);
        }

        /// @brief Compute CRC-32/CD-ROM-EDC checksum.
        /// @details reflected poly=0xD8018001, init=0x00000000, refin=true, refout=true, xorout=0x00000000.
        [[nodiscard]] constexpr UInt32 CD_ROM_EDC(std::span<const UInt8> data) noexcept
        {
            return CD_ROM_EDCEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt32 CD_ROM_EDC(const UInt8* data, UIntSize len) noexcept
        {
            return CD_ROM_EDCEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt32 CD_ROM_EDC(std::string_view text) noexcept
        {
            return CD_ROM_EDCEngine::Compute(text);
        }

        /// @brief Compute CRC-32/CKSUM checksum.
        /// @details poly=0x04C11DB7, init=0x00000000, refin=false, refout=false, xorout=0xFFFFFFFF.
        [[nodiscard]] constexpr UInt32 CKSUM(std::span<const UInt8> data) noexcept
        {
            return CKSUMEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt32 CKSUM(const UInt8* data, UIntSize len) noexcept
        {
            return CKSUMEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt32 CKSUM(std::string_view text) noexcept
        {
            return CKSUMEngine::Compute(text);
        }

        /// @brief Compute CRC-32/ISCSI checksum.
        /// @details reflected poly=0x82F63B78, init=0xFFFFFFFF, refin=true, refout=true, xorout=0xFFFFFFFF.
        [[nodiscard]] constexpr UInt32 ISCSI(std::span<const UInt8> data) noexcept
        {
            return ISCSIEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt32 ISCSI(const UInt8* data, UIntSize len) noexcept
        {
            return ISCSIEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt32 ISCSI(std::string_view text) noexcept
        {
            return ISCSIEngine::Compute(text);
        }

        /// @brief Compute CRC-32/ISO-HDLC checksum.
        /// @details Alias of CRC-32/IEEE-802.3.
        [[nodiscard]] constexpr UInt32 ISO_HDLC(std::span<const UInt8> data) noexcept
        {
            return ISO_HDLCEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt32 ISO_HDLC(const UInt8* data, UIntSize len) noexcept
        {
            return ISO_HDLCEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt32 ISO_HDLC(std::string_view text) noexcept
        {
            return ISO_HDLCEngine::Compute(text);
        }

        /// @brief Compute CRC-32/JAMCRC checksum.
        /// @details reflected poly=0xEDB88320, init=0xFFFFFFFF, refin=true, refout=true, xorout=0x00000000.
        [[nodiscard]] constexpr UInt32 JAMCRC(std::span<const UInt8> data) noexcept
        {
            return JAMCRCEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt32 JAMCRC(const UInt8* data, UIntSize len) noexcept
        {
            return JAMCRCEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt32 JAMCRC(std::string_view text) noexcept
        {
            return JAMCRCEngine::Compute(text);
        }

        /// @brief Compute CRC-32/MEF checksum.
        /// @details reflected poly=0xEB31D82E, init=0xFFFFFFFF, refin=true, refout=true, xorout=0x00000000.
        [[nodiscard]] constexpr UInt32 MEF(std::span<const UInt8> data) noexcept
        {
            return MEFEngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt32 MEF(const UInt8* data, UIntSize len) noexcept
        {
            return MEFEngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt32 MEF(std::string_view text) noexcept
        {
            return MEFEngine::Compute(text);
        }

        /// @brief Compute CRC-32/XFER checksum.
        /// @details poly=0x000000AF, init=0x00000000, refin=false, refout=false, xorout=0x00000000.
        [[nodiscard]] constexpr UInt32 XFER(std::span<const UInt8> data) noexcept
        {
            return XFEREngine::Compute(data);
        }

        [[nodiscard]] constexpr UInt32 XFER(const UInt8* data, UIntSize len) noexcept
        {
            return XFEREngine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt32 XFER(std::string_view text) noexcept
        {
            return XFEREngine::Compute(text);
        }
    }// namespace CRC32

    namespace CRC64
    {
        using ISO_3309Model = CRCModel<UInt64, 0xD800000000000000ULL, 0xFFFFFFFFFFFFFFFFULL, true, true, 0xFFFFFFFFFFFFFFFFULL>;
        using ECMA_182Model = CRCModel<UInt64, 0x42F0E1EBA9EA3693ULL, 0x0000000000000000ULL, false, false, 0x0000000000000000ULL>;

        using ISO_3309Engine = TableCRCEngine<ISO_3309Model, 8>;
        using ECMA_182Engine = TableCRCEngine<ECMA_182Model>;

        using ISO_3309State = CRCState<ISO_3309Engine>;
        using ECMA_182State = CRCState<ECMA_182Engine>;

        /// @brief Compute CRC-64/ISO-3309 checksum.
        /// @details reflected poly=0xD800000000000000, init=0xFFFFFFFFFFFFFFFF, refin=true, refout=true, xorout=0xFFFFFFFFFFFFFFFF.
        [[nodiscard]] constexpr UInt64 ISO_3309(std::span<const UInt8> data) noexcept
        {
            return ISO_3309Engine::Compute(data);
        }

        [[nodiscard]] constexpr UInt64 ISO_3309(const UInt8* data, UIntSize len) noexcept
        {
            return ISO_3309Engine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt64 ISO_3309(std::string_view text) noexcept
        {
            return ISO_3309Engine::Compute(text);
        }

        /// @brief Compute CRC-64/ECMA-182 checksum.
        /// @details poly=0x42F0E1EBA9EA3693, init=0x0000000000000000, refin=false, refout=false, xorout=0x0000000000000000.
        [[nodiscard]] constexpr UInt64 ECMA_182(std::span<const UInt8> data) noexcept
        {
            return ECMA_182Engine::Compute(data);
        }

        [[nodiscard]] constexpr UInt64 ECMA_182(const UInt8* data, UIntSize len) noexcept
        {
            return ECMA_182Engine::Compute(data, len);
        }

        [[nodiscard]] constexpr UInt64 ECMA_182(std::string_view text) noexcept
        {
            return ECMA_182Engine::Compute(text);
        }
    }// namespace CRC64
}// namespace NGIN::Hashing
