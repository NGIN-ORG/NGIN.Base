#include <NGIN/Crypto/Tokens/Paseto.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    auto input   = std::string_view {reinterpret_cast<const char*>(data), size};
    auto options = NGIN::Crypto::Tokens::PasetoParseOptions {
            .maxPayloadBytes  = 64u * 1024u,
            .maxFooterBytes   = 8192,
            .maxImplicitBytes = 8192,
    };

    (void) NGIN::Crypto::Tokens::ParsePasetoV4Public(
            input,
            options);

    auto context = NGIN::Crypto::Backend::CreateBestAvailableContext();
    if (context.HasValue())
    {
        constexpr std::array<NGIN::Byte, 32> key {
                NGIN::Byte {0x70},
                NGIN::Byte {0x71},
                NGIN::Byte {0x72},
                NGIN::Byte {0x73},
                NGIN::Byte {0x74},
                NGIN::Byte {0x75},
                NGIN::Byte {0x76},
                NGIN::Byte {0x77},
                NGIN::Byte {0x78},
                NGIN::Byte {0x79},
                NGIN::Byte {0x7a},
                NGIN::Byte {0x7b},
                NGIN::Byte {0x7c},
                NGIN::Byte {0x7d},
                NGIN::Byte {0x7e},
                NGIN::Byte {0x7f},
                NGIN::Byte {0x80},
                NGIN::Byte {0x81},
                NGIN::Byte {0x82},
                NGIN::Byte {0x83},
                NGIN::Byte {0x84},
                NGIN::Byte {0x85},
                NGIN::Byte {0x86},
                NGIN::Byte {0x87},
                NGIN::Byte {0x88},
                NGIN::Byte {0x89},
                NGIN::Byte {0x8a},
                NGIN::Byte {0x8b},
                NGIN::Byte {0x8c},
                NGIN::Byte {0x8d},
                NGIN::Byte {0x8e},
                NGIN::Byte {0x8f},
        };

        (void) NGIN::Crypto::Tokens::OpenPasetoV4Local(
                context.Value(),
                input,
                NGIN::Crypto::Memory::SecretView {NGIN::Crypto::ConstByteSpan {key.data(), key.size()}},
                NGIN::Crypto::Tokens::PasetoValidationPolicy {
                        .expectedFooter    = {},
                        .implicitAssertion = {},
                        .requiredClaims    = {},
                        .parseOptions      = options,
                });
    }

    return 0;
}
