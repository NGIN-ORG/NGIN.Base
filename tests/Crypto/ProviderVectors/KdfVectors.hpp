#pragma once

#include <NGIN/Crypto/Algorithm.hpp>

#include <array>
#include <string_view>

namespace NGIN::Crypto::Tests::ProviderVectors
{
    struct HkdfVector
    {
        KdfAlgorithm     algorithm;
        std::string_view inputKeyMaterialHex;
        std::string_view saltHex;
        std::string_view infoHex;
        std::string_view expectedHex;
    };

    struct Pbkdf2Vector
    {
        KdfAlgorithm     algorithm;
        std::string_view password;
        std::string_view salt;
        NGIN::UInt32     iterations;
        std::string_view expectedHex;
    };

    struct Argon2idVector
    {
        std::string_view password;
        std::string_view saltHex;
        NGIN::UInt32     memoryKiB;
        NGIN::UInt32     iterations;
        NGIN::UInt32     parallelism;
        std::string_view expectedHex;
    };

    inline constexpr std::array HKDF_VECTORS {
            HkdfVector {
                    .algorithm           = KdfAlgorithm::HkdfSha256,
                    .inputKeyMaterialHex = "0b0b0b0b0b0b0b0b0b0b0b"
                                           "0b0b0b0b0b0b0b0b0b0b0b",
                    .saltHex             = "000102030405060708090a0b0c",
                    .infoHex             = "f0f1f2f3f4f5f6f7f8f9",
                    .expectedHex         = "3cb25f25faacd57a90434f64d0362f2a"
                                           "2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
                                           "34007208d5b887185865",
            },
    };

    inline constexpr std::array PBKDF2_VECTORS {
            Pbkdf2Vector {
                    .algorithm   = KdfAlgorithm::Pbkdf2Sha256,
                    .password    = "password",
                    .salt        = "salt",
                    .iterations  = 2,
                    .expectedHex = "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43",
            },
            Pbkdf2Vector {
                    .algorithm  = KdfAlgorithm::Pbkdf2Sha512,
                    .password   = "password",
                    .salt       = "salt",
                    .iterations = 2,
                    .expectedHex =
                            "e1d9c16aa681708a45f5c7c4e215ceb6"
                            "6e011a2e9f0040713f18aefdb866d53c"
                            "f76cab2868a39b9f7840edce4fef5a82"
                            "be67335c77a6068e04112754f27ccf4e",
            },
    };

    inline constexpr std::array ARGON2ID_VECTORS {
            Argon2idVector {
                    .password    = "password",
                    .saltHex     = "31323334353637383930616263646566",
                    .memoryKiB   = 32,
                    .iterations  = 2,
                    .parallelism = 1,
                    .expectedHex = "01c10a761c2e13a3215f130cb6b43232ec805481e2816dc0e850ab2b9602277e",
            },
    };
}// namespace NGIN::Crypto::Tests::ProviderVectors
