#pragma once

#include <NGIN/Crypto/Backend/CryptoContext.hpp>
#include <NGIN/Crypto/ByteBuffer.hpp>
#include <NGIN/Crypto/Memory/SecretView.hpp>
#include <NGIN/Crypto/Signatures/Signature.hpp>

namespace NGIN::Crypto::Signatures
{
    struct SignInput
    {
        NGIN::Crypto::Memory::SecretView privateKey {};
        ConstByteSpan                    message {};
    };

    /// @brief Signs a message into caller-provided signature storage.
    [[nodiscard]] CryptoExpected<void> SignInto(
            const NGIN::Crypto::Backend::CryptoContext& context,
            SignatureAlgorithm                          algorithm,
            const SignInput&                            input,
            ByteSpan                                    signature) noexcept;

    /// @brief Signs a message into owned signature storage.
    [[nodiscard]] CryptoExpected<ByteBuffer> Sign(
            const NGIN::Crypto::Backend::CryptoContext& context,
            SignatureAlgorithm                          algorithm,
            const SignInput&                            input);
}// namespace NGIN::Crypto::Signatures
