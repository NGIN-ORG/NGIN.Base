#pragma once

#include <NGIN/Crypto/Algorithm.hpp>

#include <array>
#include <string_view>

namespace NGIN::Crypto::Tests::ProviderVectors
{
    struct AeadSealVector
    {
        AeadAlgorithm    algorithm;
        std::string_view keyHex;
        std::string_view nonceHex;
        std::string_view plaintextHex;
        std::string_view associatedDataHex;
        std::string_view ciphertextHex;
        std::string_view tagHex;
    };

    inline constexpr std::array AEAD_SEAL_VECTORS {
            AeadSealVector {
                    .algorithm         = AeadAlgorithm::Aes128Gcm,
                    .keyHex            = "00000000000000000000000000000000",
                    .nonceHex          = "000000000000000000000000",
                    .plaintextHex      = "00000000000000000000000000000000",
                    .associatedDataHex = "",
                    .ciphertextHex     = "0388dace60b6a392f328c2b971b2fe78",
                    .tagHex            = "ab6e47d42cec13bdf53a67b21257bddf",
            },
            AeadSealVector {
                    .algorithm         = AeadAlgorithm::Aes256Gcm,
                    .keyHex            = "0000000000000000000000000000000000000000000000000000000000000000",
                    .nonceHex          = "000000000000000000000000",
                    .plaintextHex      = "00000000000000000000000000000000",
                    .associatedDataHex = "",
                    .ciphertextHex     = "cea7403d4d606b6e074ec5d3baf39d18",
                    .tagHex            = "d0d1c8a799996bf0265b98b5d48ab919",
            },
            AeadSealVector {
                    .algorithm         = AeadAlgorithm::ChaCha20Poly1305,
                    .keyHex            = "1c9240a5eb55d38af333888604f6b5f0"
                                         "473917c1402b80099dca5cbc207075c0",
                    .nonceHex          = "000000000102030405060708",
                    .plaintextHex      = "496e7465726e65742d4472616674732061726520647261667420646f63756d656e747"
                                         "32076616c696420666f722061206d6178696d756d206f6620736978206d6f6e7468"
                                         "7320616e64206d617920626520757064617465642c207265706c616365642c206f"
                                         "72206f62736f6c65746564206279206f7468657220646f63756d656e74732061742"
                                         "0616e792074696d652e20497420697320696e617070726f70726961746520746f"
                                         "2075736520496e7465726e65742d447261667473206173207265666572656e6365"
                                         "206d6174657269616c206f7220746f2063697465207468656d206f746865722074"
                                         "68616e206173202fe2809c776f726b20696e2070726f67726573732e2fe2809d",
                    .associatedDataHex = "f33388860000000000004e91",
                    .ciphertextHex =
                            "64a0861575861af460f062c79be643bd"
                            "5e805cfd345cf389f108670ac76c8cb2"
                            "4c6cfc18755d43eea09ee94e382d26b0"
                            "bdb7b73c321b0100d4f03b7f355894cf"
                            "332f830e710b97ce98c8a84abd0b9481"
                            "14ad176e008d33bd60f982b1ff37c855"
                            "9797a06ef4f0ef61c186324e2b350638"
                            "3606907b6a7c02b0f9f6157b53c867e4"
                            "b9166c767b804d46a59b5216cde7a4e9"
                            "9040c5a40433225ee282a1b0a06c523e"
                            "af4534d7f83fa1155b0047718cbc546a"
                            "0d072b04b3564eea1b422273f548271a"
                            "0bb2316053fa76991955ebd63159434e"
                            "cebb4e466dae5a1073a6727627097a10"
                            "49e617d91d361094fa68f0ff77987130"
                            "305beaba2eda04df997b714d6c6f2c29"
                            "a6ad5cb4022b02709b",
                    .tagHex = "eead9d67890cbb22392336fea1851f38",
            },
            AeadSealVector {
                    .algorithm         = AeadAlgorithm::XChaCha20Poly1305,
                    .keyHex            = "00000000000000000000000000000000"
                                         "00000000000000000000000000000000",
                    .nonceHex          = "000000000000000000000000000000000000000000000000",
                    .plaintextHex      = "000102030405060708090a0b0c0d0e0f",
                    .associatedDataHex = "aabbccdd",
                    .ciphertextHex     = "789f948ae1258b78d1e8f9ceb9391147",
                    .tagHex            = "bbce05596cbf967cf383f72656ff1aaa",
            },
    };
}// namespace NGIN::Crypto::Tests::ProviderVectors
