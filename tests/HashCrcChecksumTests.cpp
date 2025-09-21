#include <NGIN/Hashing/CRC.hpp>
#include <NGIN/Hashing/Checksum.hpp>
#include <NGIN/Hashing/FNV.hpp>
#include <NGIN/Primitives.hpp>
#include <catch2/catch_test_macros.hpp>
#include <string_view>

using namespace NGIN;
using namespace NGIN::Hashing;

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
    const char*  text   = "123456789";
    const UInt8* data   = reinterpret_cast<const UInt8*>(text);
    UIntSize     length = std::char_traits<char>::length(text);

    CHECK(CRC8::SMBUS(data, length) == static_cast<UInt8>(0xF4));
    CHECK(CRC8::MAXIM_DOW(data, length) == static_cast<UInt8>(0xA1));
    CHECK(CRC8::AUTOSAR(data, length) == static_cast<UInt8>(0xDF));
    CHECK(CRC8::SAE_J1850(data, length) == static_cast<UInt8>(0x4B));
    CHECK(CRC8::BLUETOOTH(data, length) == static_cast<UInt8>(0x26));

    CHECK(CRC16::CCITT_FALSE(data, length) == static_cast<UInt16>(0x29B1));
    CHECK(CRC16::ARC(data, length) == static_cast<UInt16>(0xBB3D));
    CHECK(CRC16::IBM_3740(data, length) == static_cast<UInt16>(0x29B1));
    CHECK(CRC16::XMODEM(data, length) == static_cast<UInt16>(0x31C3));
    CHECK(CRC16::KERMIT(data, length) == static_cast<UInt16>(0x2189));
    CHECK(CRC16::MODBUS(data, length) == static_cast<UInt16>(0x4B37));
    CHECK(CRC16::IBM_SDLC(data, length) == static_cast<UInt16>(0x906E));
    CHECK(CRC16::GENIBUS(data, length) == static_cast<UInt16>(0xD64E));
    CHECK(CRC16::USB(data, length) == static_cast<UInt16>(0xB4C8));
    CHECK(CRC16::MAXIM_DOW(data, length) == static_cast<UInt16>(0x44C2));
    CHECK(CRC16::MCRF4XX(data, length) == static_cast<UInt16>(0x6F91));
    CHECK(CRC16::DNP(data, length) == static_cast<UInt16>(0xEA82));
    CHECK(CRC16::EN_13757(data, length) == static_cast<UInt16>(0xC2B7));
    CHECK(CRC16::DECT_R(data, length) == static_cast<UInt16>(0x007E));
    CHECK(CRC16::DECT_X(data, length) == static_cast<UInt16>(0x007F));
    CHECK(CRC16::UMTS(data, length) == static_cast<UInt16>(0xFEE8));
    CHECK(CRC16::ISO_IEC_14443_3_A(data, length) == static_cast<UInt16>(0xBF05));
    CHECK(CRC16::T10_DIF(data, length) == static_cast<UInt16>(0xD0DB));
    CHECK(CRC16::PROFIBUS(data, length) == static_cast<UInt16>(0xA819));
    CHECK(CRC16::LJ1200(data, length) == static_cast<UInt16>(0xBDF4));
    CHECK(CRC16::OPENSAFETY_A(data, length) == static_cast<UInt16>(0x5D38));
    CHECK(CRC16::OPENSAFETY_B(data, length) == static_cast<UInt16>(0x20FE));
    CHECK(CRC16::NRSC_5(data, length) == static_cast<UInt16>(0xA066));
    CHECK(CRC16::CMS(data, length) == static_cast<UInt16>(0xAEE7));
    CHECK(CRC16::DDS_110(data, length) == static_cast<UInt16>(0x9ECF));
    CHECK(CRC16::M17(data, length) == static_cast<UInt16>(0x772B));
    CHECK(CRC16::TELEDISK(data, length) == static_cast<UInt16>(0x0FB3));
    CHECK(CRC16::TMS37157(data, length) == static_cast<UInt16>(0x26B1));

    CHECK(CRC32::IEEE_802_3(data, length) == static_cast<UInt32>(0xCBF43926));
    CHECK(CRC32::MPEG_2(data, length) == static_cast<UInt32>(0x0376E6E7));
    CHECK(CRC32::AIXM(data, length) == static_cast<UInt32>(0x3010BF7F));
    CHECK(CRC32::AUTOSAR(data, length) == static_cast<UInt32>(0x1697D06A));
    CHECK(CRC32::BASE91_D(data, length) == static_cast<UInt32>(0x87315576));
    CHECK(CRC32::BZIP2(data, length) == static_cast<UInt32>(0xFC891918));
    CHECK(CRC32::CD_ROM_EDC(data, length) == static_cast<UInt32>(0x6EC2EDC4));
    CHECK(CRC32::CKSUM(data, length) == static_cast<UInt32>(0x765E7680));
    CHECK(CRC32::ISCSI(data, length) == static_cast<UInt32>(0xE3069283));
    CHECK(CRC32::ISO_HDLC(data, length) == static_cast<UInt32>(0xCBF43926));
    CHECK(CRC32::JAMCRC(data, length) == static_cast<UInt32>(0x340BC6D9));
    CHECK(CRC32::MEF(data, length) == static_cast<UInt32>(0xD2C22F51));
    CHECK(CRC32::XFER(data, length) == static_cast<UInt32>(0xBD0BE338));
    CHECK(CRC64::ISO_3309(data, length) == static_cast<UInt64>(0xB90956C775A41001ULL));
    CHECK(CRC64::ECMA_182(data, length) == static_cast<UInt64>(0x6C40DF5F0B497347ULL));
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
    CHECK(Fletcher32(value) == Fletcher32(value.data(), size));

    CHECK(Adler32(value) == Adler32(data, size));
    CHECK(Xor8(value) == Xor8(data, size));
}
