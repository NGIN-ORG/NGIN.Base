#include <NGIN/Crypto/Tokens/Jwt.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    auto input = std::string_view {reinterpret_cast<const char*>(data), size};
    constexpr NGIN::Crypto::Tokens::JwtParseOptions options {
            .maxHeaderBytes    = 4096,
            .maxPayloadBytes   = 64u * 1024u,
            .maxSignatureBytes = 8192,
    };

    (void) NGIN::Crypto::Tokens::ParseJwtCompact(input, options);

    static const auto context = NGIN::Crypto::Backend::CreateBestAvailableContext();
    if (!context.HasValue())
    {
        return 0;
    }

    constexpr std::array<NGIN::Byte, 32> hmacKey {
            NGIN::Byte {0x68}, NGIN::Byte {0x6d}, NGIN::Byte {0x61}, NGIN::Byte {0x63},
            NGIN::Byte {0x2d}, NGIN::Byte {0x66}, NGIN::Byte {0x75}, NGIN::Byte {0x7a},
            NGIN::Byte {0x7a}, NGIN::Byte {0x65}, NGIN::Byte {0x72}, NGIN::Byte {0x2d},
            NGIN::Byte {0x6b}, NGIN::Byte {0x65}, NGIN::Byte {0x79}, NGIN::Byte {0x2d},
            NGIN::Byte {0x30}, NGIN::Byte {0x30}, NGIN::Byte {0x30}, NGIN::Byte {0x30},
            NGIN::Byte {0x30}, NGIN::Byte {0x30}, NGIN::Byte {0x30}, NGIN::Byte {0x30},
            NGIN::Byte {0x30}, NGIN::Byte {0x30}, NGIN::Byte {0x30}, NGIN::Byte {0x30},
            NGIN::Byte {0x30}, NGIN::Byte {0x30}, NGIN::Byte {0x30}, NGIN::Byte {0x30},
    };
    constexpr std::array<NGIN::Byte, 65> ecdsaPublicKey {
            NGIN::Byte {0x04}, NGIN::Byte {0x9f}, NGIN::Byte {0xc6}, NGIN::Byte {0xca},
            NGIN::Byte {0x17}, NGIN::Byte {0x27}, NGIN::Byte {0x84}, NGIN::Byte {0x94},
            NGIN::Byte {0x9a}, NGIN::Byte {0xa4}, NGIN::Byte {0x6a}, NGIN::Byte {0xd2},
            NGIN::Byte {0xe0}, NGIN::Byte {0x2e}, NGIN::Byte {0xe2}, NGIN::Byte {0x4a},
            NGIN::Byte {0xfa}, NGIN::Byte {0x44}, NGIN::Byte {0xf8}, NGIN::Byte {0xfe},
            NGIN::Byte {0xc4}, NGIN::Byte {0xef}, NGIN::Byte {0xf9}, NGIN::Byte {0x39},
            NGIN::Byte {0xbf}, NGIN::Byte {0xf2}, NGIN::Byte {0xf7}, NGIN::Byte {0x37},
            NGIN::Byte {0xbb}, NGIN::Byte {0x3a}, NGIN::Byte {0xc8}, NGIN::Byte {0xe1},
            NGIN::Byte {0x41}, NGIN::Byte {0x23}, NGIN::Byte {0x78}, NGIN::Byte {0xf7},
            NGIN::Byte {0xb3}, NGIN::Byte {0x76}, NGIN::Byte {0x34}, NGIN::Byte {0x39},
            NGIN::Byte {0x46}, NGIN::Byte {0x13}, NGIN::Byte {0x6c}, NGIN::Byte {0xc5},
            NGIN::Byte {0x35}, NGIN::Byte {0x7d}, NGIN::Byte {0x62}, NGIN::Byte {0x59},
            NGIN::Byte {0xe8}, NGIN::Byte {0x9e}, NGIN::Byte {0x3b}, NGIN::Byte {0x86},
            NGIN::Byte {0x7c}, NGIN::Byte {0xfe}, NGIN::Byte {0x5e}, NGIN::Byte {0x41},
            NGIN::Byte {0xa1}, NGIN::Byte {0x0c}, NGIN::Byte {0xf5}, NGIN::Byte {0xa8},
            NGIN::Byte {0x02}, NGIN::Byte {0x8e}, NGIN::Byte {0xde}, NGIN::Byte {0x78},
            NGIN::Byte {0x40},
    };

    (void) NGIN::Crypto::Tokens::ValidateJwt(
            context.Value(),
            input,
            NGIN::Crypto::Tokens::JwtValidationKey {
                    .algorithm = NGIN::Crypto::Tokens::JwtAlgorithm::Hs256,
                    .hmacKey   = NGIN::Crypto::Memory::SecretView {
                            NGIN::Crypto::ConstByteSpan {hmacKey.data(), hmacKey.size()},
                    },
                    .publicKey = {},
            },
            NGIN::Crypto::Tokens::JwtValidationPolicy {
                    .allowHs256         = true,
                    .allowPs256         = false,
                    .allowEs256         = false,
                    .allowEdDsa         = false,
                    .requireExpiration  = false,
                    .validateExpiration = false,
                    .validateNotBefore  = false,
                    .parseOptions       = options,
            });
    (void) NGIN::Crypto::Tokens::ValidateJwt(
            context.Value(),
            input,
            NGIN::Crypto::Tokens::JwtValidationKey {
                    .algorithm = NGIN::Crypto::Tokens::JwtAlgorithm::Es256,
                    .hmacKey   = {},
                    .publicKey = NGIN::Crypto::ConstByteSpan {ecdsaPublicKey.data(), ecdsaPublicKey.size()},
            },
            NGIN::Crypto::Tokens::JwtValidationPolicy {
                    .allowHs256         = false,
                    .allowPs256         = false,
                    .allowEs256         = true,
                    .allowEdDsa         = false,
                    .requireExpiration  = false,
                    .validateExpiration = false,
                    .validateNotBefore  = false,
                    .parseOptions       = options,
            });
    return 0;
}
