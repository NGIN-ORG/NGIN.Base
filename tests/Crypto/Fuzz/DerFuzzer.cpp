#include <NGIN/Crypto/Asymmetric/Ecdsa.hpp>
#include <NGIN/Crypto/Encoding/Der.hpp>

#include <NGIN/Crypto/Types.hpp>

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    auto input = NGIN::Crypto::ConstByteSpan {reinterpret_cast<const NGIN::Byte*>(data), size};
    (void) NGIN::Crypto::Asymmetric::ParseEcdsaP256Sha256SignatureDer(input);

    auto reader = NGIN::Crypto::Encoding::DerReader {
            input,
            NGIN::Crypto::Encoding::DerReadOptions {
                    .maxElementBytes = 64u * 1024u,
                    .maxDepth        = 8,
            },
    };

    while (!reader.IsAtEnd())
    {
        auto element = reader.ReadElement();
        if (!element.has_value())
        {
            break;
        }

        (void) NGIN::Crypto::Encoding::ReadDerInteger(element.value());
        (void) NGIN::Crypto::Encoding::ReadDerBitString(element.value());
        (void) NGIN::Crypto::Encoding::ReadDerOctetString(element.value());
        (void) NGIN::Crypto::Encoding::ReadDerObjectIdentifier(element.value());

        if (element.value().tag.constructed)
        {
            (void) reader.EnterConstructed(element.value());
        }
    }

    return 0;
}
