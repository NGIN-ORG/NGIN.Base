#pragma once

#include <NGIN/Crypto/Backend/CryptoContext.hpp>
#include <NGIN/Crypto/Signatures/Signature.hpp>

namespace NGIN::Crypto::Signatures
{
    struct VerifyInput
    {
        ConstByteSpan publicKey {};
        ConstByteSpan message {};
        ConstByteSpan signature {};
    };

    /// @brief Verifies a signature over a message.
    [[nodiscard]] CryptoExpected<void> Verify(
            const NGIN::Crypto::Backend::CryptoContext& context,
            SignatureAlgorithm                          algorithm,
            const VerifyInput&                          input) noexcept;
}// namespace NGIN::Crypto::Signatures
