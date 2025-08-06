#include <boost/ut.hpp>
#include <NGIN/Hashing/Checksum.hpp>
#include <NGIN/Hashing/CRC.hpp>
#include <NGIN/Hashing/FNV.hpp>
#include <NGIN/Primitives.hpp>
#include <string_view>

using namespace boost::ut;
using namespace NGIN;
using namespace NGIN::Hashing;

suite<"NGIN::Hashing::HashCrcChecksum"> hashCrcChecksumTests = [] {
    // Test FNV-1a 32-bit overload consistency
    "FNV1a32_OverloadConsistency"_test = []() {
        constexpr std::string_view sv = "test";
        constexpr UInt32 h1           = FNV1a32(sv);
        constexpr UInt32 h2           = FNV1a32(sv.data(), sv.size());
        constexpr UInt32 h3           = FNV1a32(sv);
        expect(h1 == h2);
        expect(h2 == h3);
    };

    // Test FNV-1a 64-bit overload consistency
    "FNV1a64_OverloadConsistency"_test = []() {
        constexpr std::string_view sv = "test";
        constexpr UInt64 h1           = FNV1a64(sv);
        constexpr UInt64 h2           = FNV1a64(sv.data(), sv.size());
        constexpr UInt64 h3           = FNV1a64(sv);
        expect(h1 == h2);
        expect(h2 == h3);
    };

    // Test CRC empty inputs
    "CRC_EmptyInputs"_test = []() {
        // Empty data pointer or zero length yields initial folded value
        expect(CRC8::SMBUS(nullptr, 0) == 0_u8);
        expect(CRC8::MAXIM_DOW(nullptr, 0) == 0_u8);
        expect(CRC8::AUTOSAR(nullptr, 0) == 0_u8);
        expect(CRC8::SAE_J1850(nullptr, 0) == 0_u8);
        expect(CRC8::BLUETOOTH(nullptr, 0) == 0_u8);
        expect(CRC16::CCITT_FALSE(nullptr, 0) == static_cast<UInt16>(0xFFFF));
        expect(CRC16::ARC(nullptr, 0) == static_cast<UInt16>(0x0000));
        expect(CRC16::IBM_3740(nullptr, 0) == static_cast<UInt16>(0xFFFF));
        expect(CRC16::XMODEM(nullptr, 0) == static_cast<UInt16>(0x0000));
        expect(CRC16::KERMIT(nullptr, 0) == static_cast<UInt16>(0x0000));
        expect(CRC16::MODBUS(nullptr, 0) == static_cast<UInt16>(0xFFFF));
        expect(CRC16::IBM_SDLC(nullptr, 0) == static_cast<UInt16>(0xFFFF ^ 0xFFFF));
        expect(CRC16::GENIBUS(nullptr, 0) == static_cast<UInt16>(0xFFFF ^ 0xFFFF));
        expect(CRC16::USB(nullptr, 0) == static_cast<UInt16>(0xFFFF ^ 0xFFFF));
        expect(CRC16::MAXIM_DOW(nullptr, 0) == static_cast<UInt16>(0x0000 ^ 0xFFFF));
        expect(CRC16::MCRF4XX(nullptr, 0) == static_cast<UInt16>(0xFFFF));
        expect(CRC16::DNP(nullptr, 0) == static_cast<UInt16>(0x0000 ^ 0xFFFF));
        expect(CRC16::EN_13757(nullptr, 0) == static_cast<UInt16>(0x0000 ^ 0xFFFF));
        expect(CRC16::DECT_R(nullptr, 0) == static_cast<UInt16>(0x0000 ^ 0x0001));
        expect(CRC16::DECT_X(nullptr, 0) == static_cast<UInt16>(0x0000));
        expect(CRC16::UMTS(nullptr, 0) == static_cast<UInt16>(0x0000));
        expect(CRC16::ISO_IEC_14443_3_A(nullptr, 0) == static_cast<UInt16>(0x6363));
        expect(CRC16::T10_DIF(nullptr, 0) == static_cast<UInt16>(0x0000));
        expect(CRC16::PROFIBUS(nullptr, 0) == static_cast<UInt16>(0xFFFF ^ 0xFFFF));
        expect(CRC16::LJ1200(nullptr, 0) == static_cast<UInt16>(0x0000));
        expect(CRC16::OPENSAFETY_A(nullptr, 0) == static_cast<UInt16>(0x0000));
        expect(CRC16::OPENSAFETY_B(nullptr, 0) == static_cast<UInt16>(0x0000));
        expect(CRC16::NRSC_5(nullptr, 0) == static_cast<UInt16>(0xFFFF));
        expect(CRC16::CMS(nullptr, 0) == static_cast<UInt16>(0xFFFF));
        expect(CRC16::DDS_110(nullptr, 0) == static_cast<UInt16>(0x800d));
        expect(CRC16::M17(nullptr, 0) == static_cast<UInt16>(0xFFFF));
        expect(CRC16::TELEDISK(nullptr, 0) == static_cast<UInt16>(0x0000));
        expect(CRC16::TMS37157(nullptr, 0) == static_cast<UInt16>(0x3791));
        expect(CRC32::IEEE_802_3(nullptr, 0) == 0_u32);
        expect(CRC64::ISO_3309(nullptr, 0) == 0_u64);
    };

    // Test CRC against known vector "123456789"
    "CRC_KnownVector"_test = []() {
        // Use runtime pointer and length to avoid constexpr reinterpret_cast
        const char* s     = "123456789";
        const UInt8* data = reinterpret_cast<const UInt8*>(s);
        UIntSize len      = std::char_traits<char>::length(s);

        // CRC-8 variants
        expect(CRC8::SMBUS(data, len) == static_cast<UInt8>(0xF4));
        expect(CRC8::MAXIM_DOW(data, len) == static_cast<UInt8>(0xA1));
        expect(CRC8::AUTOSAR(data, len) == static_cast<UInt8>(0xDF));
        expect(CRC8::SAE_J1850(data, len) == static_cast<UInt8>(0x4B));
        expect(CRC8::BLUETOOTH(data, len) == static_cast<UInt8>(0x26));

        // CRC-16 variants (known check values for "123456789")
        expect(CRC16::CCITT_FALSE(data, len) == static_cast<UInt16>(0x29B1));
        expect(CRC16::ARC(data, len) == static_cast<UInt16>(0xBB3D));
        expect(CRC16::IBM_3740(data, len) == static_cast<UInt16>(0x29B1));
        expect(CRC16::XMODEM(data, len) == static_cast<UInt16>(0x31C3));
        expect(CRC16::KERMIT(data, len) == static_cast<UInt16>(0x2189));
        expect(CRC16::MODBUS(data, len) == static_cast<UInt16>(0x4B37));
        expect(CRC16::IBM_SDLC(data, len) == static_cast<UInt16>(0x906E));
        expect(CRC16::GENIBUS(data, len) == static_cast<UInt16>(0xD64E));
        expect(CRC16::USB(data, len) == static_cast<UInt16>(0xB4C8));
        expect(CRC16::MAXIM_DOW(data, len) == static_cast<UInt16>(0x44C2));
        expect(CRC16::MCRF4XX(data, len) == static_cast<UInt16>(0x6F91));
        expect(CRC16::DNP(data, len) == static_cast<UInt16>(0xEA82));
        expect(CRC16::EN_13757(data, len) == static_cast<UInt16>(0xC2B7));
        expect(CRC16::DECT_R(data, len) == static_cast<UInt16>(0x007E));
        expect(CRC16::DECT_X(data, len) == static_cast<UInt16>(0x007F));
        expect(CRC16::UMTS(data, len) == static_cast<UInt16>(0xFEE8));
        expect(CRC16::ISO_IEC_14443_3_A(data, len) == static_cast<UInt16>(0xBF05));
        expect(CRC16::T10_DIF(data, len) == static_cast<UInt16>(0xD0DB));
        expect(CRC16::PROFIBUS(data, len) == static_cast<UInt16>(0xA819));
        expect(CRC16::LJ1200(data, len) == static_cast<UInt16>(0xBDF4));
        expect(CRC16::OPENSAFETY_A(data, len) == static_cast<UInt16>(0x5D38));
        expect(CRC16::OPENSAFETY_B(data, len) == static_cast<UInt16>(0x20FE));
        expect(CRC16::NRSC_5(data, len) == static_cast<UInt16>(0xA066));
        expect(CRC16::CMS(data, len) == static_cast<UInt16>(0xAEE7));
        expect(CRC16::DDS_110(data, len) == static_cast<UInt16>(0x9ECF));
        expect(CRC16::M17(data, len) == static_cast<UInt16>(0x772B));
        expect(CRC16::TELEDISK(data, len) == static_cast<UInt16>(0x0FB3));
        expect(CRC16::TMS37157(data, len) == static_cast<UInt16>(0x26B1));

        // CRC-32/64
        expect(CRC32::IEEE_802_3(data, len) == static_cast<UInt32>(0xCBF43926));
        expect(CRC32::MPEG_2(data, len) == static_cast<UInt32>(0x0376E6E7));
        expect(CRC64::ISO_3309(data, len) == static_cast<UInt64>(0xB90956C775A41001ULL));
        expect(CRC64::ECMA_182(data, len) == static_cast<UInt64>(0x6C40DF5F0B497347ULL));
    };

    // Test checksum overload consistency
    "Checksum_OverloadConsistency"_test = []() {
        constexpr std::string_view sv = "checksum";
        // BSDChecksum
        expect(BSDChecksum(reinterpret_cast<const UInt8*>(sv.data()), sv.size()) == BSDChecksum(sv));
        expect(BSDChecksum(reinterpret_cast<const UInt8*>(sv.data()), sv.size()) == BSDChecksum(sv.data(), sv.size()));
        // SYSVChecksum
        expect(SYSVChecksum(reinterpret_cast<const UInt8*>(sv.data()), sv.size()) == SYSVChecksum(sv));
        expect(SYSVChecksum(reinterpret_cast<const UInt8*>(sv.data()), sv.size()) == SYSVChecksum(sv.data(), sv.size()));
        // Sum8, Sum24, Sum32
        expect(Sum8(sv) == Sum8(reinterpret_cast<const UInt8*>(sv.data()), sv.size()));
        expect(Sum24(sv) == Sum24(sv.data(), sv.size()));
        expect(Sum32(sv) == Sum32(reinterpret_cast<const UInt8*>(sv.data()), sv.size()));
        // InternetChecksum
        expect(InternetChecksum(sv) == InternetChecksum(sv.data(), sv.size()));
        // Fletcher
        expect(Fletcher4(sv) == Fletcher4(reinterpret_cast<const UInt8*>(sv.data()), sv.size()));
        expect(Fletcher8(sv) == Fletcher8(sv.data(), sv.size()));
        expect(Fletcher16(sv) == Fletcher16(reinterpret_cast<const UInt8*>(sv.data()), sv.size()));
        expect(Fletcher32(sv) == Fletcher32(sv.data(), sv.size()));
        // Adler32
        expect(Adler32(sv) == Adler32(reinterpret_cast<const UInt8*>(sv.data()), sv.size()));
        // Xor8
        expect(Xor8(sv) == Xor8(reinterpret_cast<const UInt8*>(sv.data()), sv.size()));
    };
};
// End HashCrcChecksum test suite
