#include <NGIN/Hashing/CRC.hpp>
#include <NGIN/Hashing/Checksum.hpp>
#include <NGIN/Hashing/FNV.hpp>
#include <NGIN/Primitives.hpp>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <span>
#include <string_view>

using namespace NGIN;
using namespace NGIN::Hashing;

namespace
{
    constexpr std::array<UInt8, 9>                        kDigitsBytes {static_cast<UInt8>('1'),
                                                 static_cast<UInt8>('2'),
                                                 static_cast<UInt8>('3'),
                                                 static_cast<UInt8>('4'),
                                                 static_cast<UInt8>('5'),
                                                 static_cast<UInt8>('6'),
                                                 static_cast<UInt8>('7'),
                                                 static_cast<UInt8>('8'),
                                                 static_cast<UInt8>('9')};
    constexpr std::span<const UInt8, kDigitsBytes.size()> kDigitsSpan {kDigitsBytes};
    constexpr std::string_view                            kDigitsText = "123456789";

    using CRC8SMBUSBitwise    = BitwiseCRCEngine<CRC8::SMBUSModel>;
    using CRC16ARCBitwise     = BitwiseCRCEngine<CRC16::ARCModel>;
    using CRC16XMODEMBitwise  = BitwiseCRCEngine<CRC16::XMODEMModel>;
    using CRC32IEEEBitwise    = BitwiseCRCEngine<CRC32::IEEE_802_3Model>;
    using CRC32MPEG2Bitwise   = BitwiseCRCEngine<CRC32::MPEG_2Model>;
    using CRC64ISO3309Bitwise = BitwiseCRCEngine<CRC64::ISO_3309Model>;
    using CRC64ECMA182Bitwise = BitwiseCRCEngine<CRC64::ECMA_182Model>;

    static_assert(CRC8::SMBUS(kDigitsSpan) == static_cast<UInt8>(0xF4));
    static_assert(CRC8::BLUETOOTH(kDigitsText) == static_cast<UInt8>(0x26));

    static_assert(CRC16::XMODEM(kDigitsSpan) == static_cast<UInt16>(0x31C3));
    static_assert(CRC16::ARC(kDigitsText) == static_cast<UInt16>(0xBB3D));

    static_assert(CRC32::MPEG_2(kDigitsSpan) == static_cast<UInt32>(0x0376E6E7));
    static_assert(CRC32::IEEE_802_3(kDigitsText) == static_cast<UInt32>(0xCBF43926));

    static_assert(CRC64::ECMA_182(kDigitsSpan) == static_cast<UInt64>(0x6C40DF5F0B497347ULL));
    static_assert(CRC64::ISO_3309(kDigitsText) == static_cast<UInt64>(0xB90956C775A41001ULL));

    static_assert(CRC8::SMBUSEngine::Compute(kDigitsSpan) == CRC8SMBUSBitwise::Compute(kDigitsSpan));
    static_assert(CRC16::ARCEngine::Compute(kDigitsSpan) == CRC16ARCBitwise::Compute(kDigitsSpan));
    static_assert(CRC16::XMODEMEngine::Compute(kDigitsSpan) == CRC16XMODEMBitwise::Compute(kDigitsSpan));
    static_assert(CRC32::IEEE_802_3Engine::Compute(kDigitsSpan) == CRC32IEEEBitwise::Compute(kDigitsSpan));
    static_assert(CRC32::MPEG_2Engine::Compute(kDigitsSpan) == CRC32MPEG2Bitwise::Compute(kDigitsSpan));
    static_assert(CRC64::ISO_3309Engine::Compute(kDigitsSpan) == CRC64ISO3309Bitwise::Compute(kDigitsSpan));
    static_assert(CRC64::ECMA_182Engine::Compute(kDigitsSpan) == CRC64ECMA182Bitwise::Compute(kDigitsSpan));
}// namespace

TEST_CASE("FNV1a32 overloads produce consistent results", "[Hashing][Checksum]")
{
    constexpr std::string_view value         = "test";
    constexpr UInt32           hashFromView  = FNV1a32(value);
    constexpr UInt32           hashFromData  = FNV1a32(value.data(), value.size());
    constexpr UInt32           hashRoundTrip = FNV1a32(value);
    CHECK(hashFromView == hashFromData);
    CHECK(hashFromData == hashRoundTrip);
}

TEST_CASE("FNV1a64 overloads produce consistent results", "[Hashing][Checksum]")
{
    constexpr std::string_view value         = "test";
    constexpr UInt64           hashFromView  = FNV1a64(value);
    constexpr UInt64           hashFromData  = FNV1a64(value.data(), value.size());
    constexpr UInt64           hashRoundTrip = FNV1a64(value);
    CHECK(hashFromView == hashFromData);
    CHECK(hashFromData == hashRoundTrip);
}

TEST_CASE("CRC implementations handle empty input", "[Hashing][CRC]")
{
    CHECK(CRC8::SMBUS(nullptr, 0) == UInt8 {0});
    CHECK(CRC8::MAXIM_DOW(nullptr, 0) == UInt8 {0});
    CHECK(CRC8::AUTOSAR(nullptr, 0) == UInt8 {0});
    CHECK(CRC8::SAE_J1850(nullptr, 0) == UInt8 {0});
    CHECK(CRC8::BLUETOOTH(nullptr, 0) == UInt8 {0});

    CHECK(CRC16::CCITT_FALSE(nullptr, 0) == static_cast<UInt16>(0xFFFF));
    CHECK(CRC16::ARC(nullptr, 0) == static_cast<UInt16>(0x0000));
    CHECK(CRC16::IBM_3740(nullptr, 0) == static_cast<UInt16>(0xFFFF));
    CHECK(CRC16::XMODEM(nullptr, 0) == static_cast<UInt16>(0x0000));
    CHECK(CRC16::KERMIT(nullptr, 0) == static_cast<UInt16>(0x0000));
    CHECK(CRC16::MODBUS(nullptr, 0) == static_cast<UInt16>(0xFFFF));
    CHECK(CRC16::IBM_SDLC(nullptr, 0) == static_cast<UInt16>(0xFFFF ^ 0xFFFF));
    CHECK(CRC16::GENIBUS(nullptr, 0) == static_cast<UInt16>(0xFFFF ^ 0xFFFF));
    CHECK(CRC16::USB(nullptr, 0) == static_cast<UInt16>(0xFFFF ^ 0xFFFF));
    CHECK(CRC16::MAXIM_DOW(nullptr, 0) == static_cast<UInt16>(0x0000 ^ 0xFFFF));
    CHECK(CRC16::MCRF4XX(nullptr, 0) == static_cast<UInt16>(0xFFFF));
    CHECK(CRC16::DNP(nullptr, 0) == static_cast<UInt16>(0x0000 ^ 0xFFFF));
    CHECK(CRC16::EN_13757(nullptr, 0) == static_cast<UInt16>(0x0000 ^ 0xFFFF));
    CHECK(CRC16::DECT_R(nullptr, 0) == static_cast<UInt16>(0x0000 ^ 0x0001));
    CHECK(CRC16::DECT_X(nullptr, 0) == static_cast<UInt16>(0x0000));
    CHECK(CRC16::UMTS(nullptr, 0) == static_cast<UInt16>(0x0000));
    CHECK(CRC16::ISO_IEC_14443_3_A(nullptr, 0) == static_cast<UInt16>(0x6363));
    CHECK(CRC16::T10_DIF(nullptr, 0) == static_cast<UInt16>(0x0000));
    CHECK(CRC16::PROFIBUS(nullptr, 0) == static_cast<UInt16>(0xFFFF ^ 0xFFFF));
    CHECK(CRC16::LJ1200(nullptr, 0) == static_cast<UInt16>(0x0000));
    CHECK(CRC16::OPENSAFETY_A(nullptr, 0) == static_cast<UInt16>(0x0000));
    CHECK(CRC16::OPENSAFETY_B(nullptr, 0) == static_cast<UInt16>(0x0000));
    CHECK(CRC16::NRSC_5(nullptr, 0) == static_cast<UInt16>(0xFFFF));
    CHECK(CRC16::CMS(nullptr, 0) == static_cast<UInt16>(0xFFFF));
    CHECK(CRC16::DDS_110(nullptr, 0) == static_cast<UInt16>(0x800d));
    CHECK(CRC16::M17(nullptr, 0) == static_cast<UInt16>(0xFFFF));
    CHECK(CRC16::TELEDISK(nullptr, 0) == static_cast<UInt16>(0x0000));
    CHECK(CRC16::TMS37157(nullptr, 0) == static_cast<UInt16>(0x3791));

    CHECK(CRC32::IEEE_802_3(nullptr, 0) == UInt32 {0});
    CHECK(CRC32::MPEG_2(nullptr, 0) == static_cast<UInt32>(0xFFFFFFFFu));
    CHECK(CRC32::AIXM(nullptr, 0) == UInt32 {0});
    CHECK(CRC32::AUTOSAR(nullptr, 0) == UInt32 {0});
    CHECK(CRC32::BASE91_D(nullptr, 0) == UInt32 {0});
    CHECK(CRC32::BZIP2(nullptr, 0) == UInt32 {0});
    CHECK(CRC32::CD_ROM_EDC(nullptr, 0) == UInt32 {0});
    CHECK(CRC32::CKSUM(nullptr, 0) == static_cast<UInt32>(0xFFFFFFFFu));
    CHECK(CRC32::ISCSI(nullptr, 0) == UInt32 {0});
    CHECK(CRC32::ISO_HDLC(nullptr, 0) == UInt32 {0});
    CHECK(CRC32::JAMCRC(nullptr, 0) == static_cast<UInt32>(0xFFFFFFFFu));
    CHECK(CRC32::MEF(nullptr, 0) == static_cast<UInt32>(0xFFFFFFFFu));
    CHECK(CRC32::XFER(nullptr, 0) == UInt32 {0});
    CHECK(CRC64::ISO_3309(nullptr, 0) == UInt64 {0});
}

TEST_CASE("CRC implementations match known vectors", "[Hashing][CRC]")
{
    CHECK(CRC8::SMBUS(kDigitsSpan) == static_cast<UInt8>(0xF4));
    CHECK(CRC8::MAXIM_DOW(kDigitsSpan) == static_cast<UInt8>(0xA1));
    CHECK(CRC8::AUTOSAR(kDigitsSpan) == static_cast<UInt8>(0xDF));
    CHECK(CRC8::SAE_J1850(kDigitsSpan) == static_cast<UInt8>(0x4B));
    CHECK(CRC8::BLUETOOTH(kDigitsSpan) == static_cast<UInt8>(0x26));

    CHECK(CRC16::CCITT_FALSE(kDigitsSpan) == static_cast<UInt16>(0x29B1));
    CHECK(CRC16::ARC(kDigitsSpan) == static_cast<UInt16>(0xBB3D));
    CHECK(CRC16::IBM_3740(kDigitsSpan) == static_cast<UInt16>(0x29B1));
    CHECK(CRC16::XMODEM(kDigitsSpan) == static_cast<UInt16>(0x31C3));
    CHECK(CRC16::KERMIT(kDigitsSpan) == static_cast<UInt16>(0x2189));
    CHECK(CRC16::MODBUS(kDigitsSpan) == static_cast<UInt16>(0x4B37));
    CHECK(CRC16::IBM_SDLC(kDigitsSpan) == static_cast<UInt16>(0x906E));
    CHECK(CRC16::GENIBUS(kDigitsSpan) == static_cast<UInt16>(0xD64E));
    CHECK(CRC16::USB(kDigitsSpan) == static_cast<UInt16>(0xB4C8));
    CHECK(CRC16::MAXIM_DOW(kDigitsSpan) == static_cast<UInt16>(0x44C2));
    CHECK(CRC16::MCRF4XX(kDigitsSpan) == static_cast<UInt16>(0x6F91));
    CHECK(CRC16::DNP(kDigitsSpan) == static_cast<UInt16>(0xEA82));
    CHECK(CRC16::EN_13757(kDigitsSpan) == static_cast<UInt16>(0xC2B7));
    CHECK(CRC16::DECT_R(kDigitsSpan) == static_cast<UInt16>(0x007E));
    CHECK(CRC16::DECT_X(kDigitsSpan) == static_cast<UInt16>(0x007F));
    CHECK(CRC16::UMTS(kDigitsSpan) == static_cast<UInt16>(0xFEE8));
    CHECK(CRC16::ISO_IEC_14443_3_A(kDigitsSpan) == static_cast<UInt16>(0xBF05));
    CHECK(CRC16::T10_DIF(kDigitsSpan) == static_cast<UInt16>(0xD0DB));
    CHECK(CRC16::PROFIBUS(kDigitsSpan) == static_cast<UInt16>(0xA819));
    CHECK(CRC16::LJ1200(kDigitsSpan) == static_cast<UInt16>(0xBDF4));
    CHECK(CRC16::OPENSAFETY_A(kDigitsSpan) == static_cast<UInt16>(0x5D38));
    CHECK(CRC16::OPENSAFETY_B(kDigitsSpan) == static_cast<UInt16>(0x20FE));
    CHECK(CRC16::NRSC_5(kDigitsSpan) == static_cast<UInt16>(0xA066));
    CHECK(CRC16::CMS(kDigitsSpan) == static_cast<UInt16>(0xAEE7));
    CHECK(CRC16::DDS_110(kDigitsSpan) == static_cast<UInt16>(0x9ECF));
    CHECK(CRC16::M17(kDigitsSpan) == static_cast<UInt16>(0x772B));
    CHECK(CRC16::TELEDISK(kDigitsSpan) == static_cast<UInt16>(0x0FB3));
    CHECK(CRC16::TMS37157(kDigitsSpan) == static_cast<UInt16>(0x26B1));

    CHECK(CRC32::IEEE_802_3(kDigitsSpan) == static_cast<UInt32>(0xCBF43926));
    CHECK(CRC32::MPEG_2(kDigitsSpan) == static_cast<UInt32>(0x0376E6E7));
    CHECK(CRC32::AIXM(kDigitsSpan) == static_cast<UInt32>(0x3010BF7F));
    CHECK(CRC32::AUTOSAR(kDigitsSpan) == static_cast<UInt32>(0x1697D06A));
    CHECK(CRC32::BASE91_D(kDigitsSpan) == static_cast<UInt32>(0x87315576));
    CHECK(CRC32::BZIP2(kDigitsSpan) == static_cast<UInt32>(0xFC891918));
    CHECK(CRC32::CD_ROM_EDC(kDigitsSpan) == static_cast<UInt32>(0x6EC2EDC4));
    CHECK(CRC32::CKSUM(kDigitsSpan) == static_cast<UInt32>(0x765E7680));
    CHECK(CRC32::ISCSI(kDigitsSpan) == static_cast<UInt32>(0xE3069283));
    CHECK(CRC32::ISO_HDLC(kDigitsSpan) == static_cast<UInt32>(0xCBF43926));
    CHECK(CRC32::JAMCRC(kDigitsSpan) == static_cast<UInt32>(0x340BC6D9));
    CHECK(CRC32::MEF(kDigitsSpan) == static_cast<UInt32>(0xD2C22F51));
    CHECK(CRC32::XFER(kDigitsSpan) == static_cast<UInt32>(0xBD0BE338));

    CHECK(CRC64::ISO_3309(kDigitsSpan) == static_cast<UInt64>(0xB90956C775A41001ULL));
    CHECK(CRC64::ECMA_182(kDigitsSpan) == static_cast<UInt64>(0x6C40DF5F0B497347ULL));
}

TEST_CASE("CRC overloads remain consistent", "[Hashing][CRC]")
{
    CHECK(CRC8::SMBUS(kDigitsSpan) == CRC8::SMBUS(kDigitsBytes.data(), kDigitsBytes.size()));
    CHECK(CRC8::SMBUS(kDigitsSpan) == CRC8::SMBUS(kDigitsText));

    CHECK(CRC16::ARC(kDigitsSpan) == CRC16::ARC(kDigitsBytes.data(), kDigitsBytes.size()));
    CHECK(CRC16::ARC(kDigitsSpan) == CRC16::ARC(kDigitsText));

    CHECK(CRC32::IEEE_802_3(kDigitsSpan) == CRC32::IEEE_802_3(kDigitsBytes.data(), kDigitsBytes.size()));
    CHECK(CRC32::IEEE_802_3(kDigitsSpan) == CRC32::IEEE_802_3(kDigitsText));

    CHECK(CRC64::ECMA_182(kDigitsSpan) == CRC64::ECMA_182(kDigitsBytes.data(), kDigitsBytes.size()));
    CHECK(CRC64::ECMA_182(kDigitsSpan) == CRC64::ECMA_182(kDigitsText));
}

TEST_CASE("CRC table and bitwise engines agree", "[Hashing][CRC]")
{
    CHECK(CRC8::SMBUSEngine::Compute(kDigitsSpan) == CRC8SMBUSBitwise::Compute(kDigitsSpan));
    CHECK(CRC16::ARCEngine::Compute(kDigitsSpan) == CRC16ARCBitwise::Compute(kDigitsSpan));
    CHECK(CRC16::XMODEMEngine::Compute(kDigitsSpan) == CRC16XMODEMBitwise::Compute(kDigitsSpan));
    CHECK(CRC32::IEEE_802_3Engine::Compute(kDigitsSpan) == CRC32IEEEBitwise::Compute(kDigitsSpan));
    CHECK(CRC32::MPEG_2Engine::Compute(kDigitsSpan) == CRC32MPEG2Bitwise::Compute(kDigitsSpan));
    CHECK(CRC64::ISO_3309Engine::Compute(kDigitsSpan) == CRC64ISO3309Bitwise::Compute(kDigitsSpan));
    CHECK(CRC64::ECMA_182Engine::Compute(kDigitsSpan) == CRC64ECMA182Bitwise::Compute(kDigitsSpan));
}

TEST_CASE("CRC stateful updates match one-shot results", "[Hashing][CRC]")
{
    CRC8::MAXIM_DOWState crc8;
    crc8.Update(kDigitsSpan.first<3>());
    crc8.Update(kDigitsSpan.subspan(3, 3));
    crc8.Update(kDigitsText.substr(6));
    CHECK(crc8.Finalize() == CRC8::MAXIM_DOW(kDigitsSpan));

    CRC16::ARCState crc16;
    crc16.Update(kDigitsText.substr(0, 4));
    crc16.Update(kDigitsBytes.data() + 4, kDigitsBytes.size() - 4);
    CHECK(crc16.Finalize() == CRC16::ARC(kDigitsSpan));

    CRC32::IEEE_802_3State crc32;
    crc32.Update(kDigitsSpan.subspan(0, 5));
    crc32.Update(kDigitsSpan.subspan(5));
    CHECK(crc32.Finalize() == CRC32::IEEE_802_3(kDigitsSpan));
    crc32.Reset();
    crc32.Update(kDigitsText);
    CHECK(crc32.Finalize() == CRC32::IEEE_802_3(kDigitsText));

    CRC64::ISO_3309State crc64;
    crc64.Update(kDigitsBytes.data(), 2);
    crc64.Update(kDigitsSpan.subspan(2, 4));
    crc64.Update(kDigitsText.substr(6));
    CHECK(crc64.Finalize() == CRC64::ISO_3309(kDigitsSpan));
}

TEST_CASE("Checksum overloads remain consistent", "[Hashing][Checksum]")
{
    constexpr std::string_view value = "checksum";
    const auto*                data  = reinterpret_cast<const UInt8*>(value.data());
    const auto                 size  = value.size();

    CHECK(BSDChecksum(data, size) == BSDChecksum(value));
    CHECK(BSDChecksum(data, size) == BSDChecksum(value.data(), size));

    CHECK(SYSVChecksum(data, size) == SYSVChecksum(value));
    CHECK(SYSVChecksum(data, size) == SYSVChecksum(value.data(), size));

    CHECK(Sum8(value) == Sum8(data, size));
    CHECK(Sum24(value) == Sum24(value.data(), size));
    CHECK(Sum32(value) == Sum32(data, size));

    CHECK(InternetChecksum(value) == InternetChecksum(value.data(), size));

    CHECK(Fletcher4(value) == Fletcher4(data, size));
    CHECK(Fletcher8(value) == Fletcher8(value.data(), size));
    CHECK(Fletcher16(value) == Fletcher16(data, size));
    CHECK(Fletcher32(value) == Fletcher32(data, size));

    CHECK(Adler32(value) == Adler32(data, size));
    CHECK(Xor8(value) == Xor8(data, size));
}
