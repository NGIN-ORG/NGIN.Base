#pragma once

#include <NGIN/Crypto/Algorithm.hpp>

#include <array>
#include <string_view>

namespace NGIN::Crypto::Tests::ProviderVectors
{
    struct HmacVector
    {
        MacAlgorithm     algorithm;
        std::string_view keyHex;
        std::string_view message;
        std::string_view expectedHex;
    };

    inline constexpr std::string_view RFC_4231_TEST_CASE_1_KEY_HEX {
            "0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b",
    };

    inline constexpr std::array HMAC_VECTORS {
            HmacVector {
                    .algorithm   = MacAlgorithm::HmacSha256,
                    .keyHex      = RFC_4231_TEST_CASE_1_KEY_HEX,
                    .message     = "Hi There",
                    .expectedHex = "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
            },
            HmacVector {
                    .algorithm = MacAlgorithm::HmacSha512,
                    .keyHex    = RFC_4231_TEST_CASE_1_KEY_HEX,
                    .message   = "Hi There",
                    .expectedHex =
                            "87aa7cdea5ef619d4ff0b4241a1d6cb"
                            "02379f4e2ce4ec2787ad0b30545e17cde"
                            "daa833b7d6b8a702038b274eaea3f4e4"
                            "be9d914eeb61f1702e696c203a126854",
            },
    };
}// namespace NGIN::Crypto::Tests::ProviderVectors
