#pragma once

#include <NGIN/Crypto/Backend/CryptoContext.hpp>

namespace NGIN::Crypto::Backend::detail
{
    [[nodiscard]] CryptoExpected<CryptoContext> CreateAppleContext(const BackendOptions& options) noexcept;

    [[nodiscard]] CryptoExpected<void> HashApple(
            HashAlgorithm algorithm,
            ConstByteSpan input,
            ByteSpan      output) noexcept;

    [[nodiscard]] CryptoExpected<void> MacApple(
            MacAlgorithm                     algorithm,
            NGIN::Crypto::Memory::SecretView key,
            ConstByteSpan                    input,
            ByteSpan                         output) noexcept;

    [[nodiscard]] CryptoExpected<void> Pbkdf2Apple(
            KdfAlgorithm                     algorithm,
            NGIN::Crypto::Memory::SecretView password,
            ConstByteSpan                    salt,
            NGIN::UInt32                     iterations,
            ByteSpan                         output) noexcept;
}// namespace NGIN::Crypto::Backend::detail
