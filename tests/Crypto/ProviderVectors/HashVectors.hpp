#pragma once

#include <NGIN/Crypto/Algorithm.hpp>

#include <array>
#include <string_view>

namespace NGIN::Crypto::Tests::ProviderVectors
{
    struct HashVector
    {
        HashAlgorithm    algorithm;
        std::string_view message;
        std::string_view expectedHex;
    };

    inline constexpr std::array HASH_VECTORS {
            HashVector {
                    .algorithm   = HashAlgorithm::Sha256,
                    .message     = "abc",
                    .expectedHex = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            },
            HashVector {
                    .algorithm = HashAlgorithm::Sha512,
                    .message   = "abc",
                    .expectedHex =
                            "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
                            "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
            },
    };
}// namespace NGIN::Crypto::Tests::ProviderVectors
