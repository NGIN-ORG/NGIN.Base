#pragma once

#include <string_view>

namespace NGIN::Crypto::Tests::ProviderVectors
{
    struct X25519Vector
    {
        std::string_view privateKeyHex;
        std::string_view peerPublicKeyHex;
        std::string_view sharedSecretHex;
    };

    inline constexpr X25519Vector X25519_RFC_7748_TEST_1 {
            .privateKeyHex    = "77076d0a7318a57d3c16c17251b26645"
                                "df4c2f87ebc0992ab177fba51db92c2a",
            .peerPublicKeyHex = "de9edb7d7b7dc1b4d35b61c2ece43537"
                                "3f8343c85b78674dadfc7e146f882b4f",
            .sharedSecretHex  = "4a5d9d5ba4ce2de1728e3bf480350f25"
                                "e07e21c947d19e3376f09b3c1e161742",
    };
}// namespace NGIN::Crypto::Tests::ProviderVectors
