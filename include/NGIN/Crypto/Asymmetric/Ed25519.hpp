/// @file Ed25519.hpp
/// @brief Strong Ed25519 keys, generation, signing, and verification.
#pragma once

#include <NGIN/Crypto/Asymmetric/KeyTypes.hpp>
#include <NGIN/Crypto/Signatures/Sign.hpp>
#include <NGIN/Crypto/Signatures/Verify.hpp>

namespace NGIN::Crypto::Asymmetric
{
    using Ed25519PublicKey  = PublicKey<Ed25519KeyTag, 32>;
    using Ed25519PrivateKey = PrivateKey<Ed25519KeyTag, 32>;
    using Ed25519KeyPair    = KeyPair<Ed25519PublicKey, Ed25519PrivateKey>;

    /// @brief Generates an Ed25519 key pair using a backend implementation.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<Ed25519KeyPair> GenerateEd25519KeyPair(
            const NGIN::Crypto::Backend::CryptoContext& context) noexcept;

    /// @brief Signs a message into caller-provided fixed Ed25519 signature storage.
    [[nodiscard]] inline CryptoExpected<void> SignEd25519Into(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const Ed25519PrivateKey&                    privateKey,
            ConstByteSpan                               message,
            NGIN::Crypto::Signatures::Ed25519Signature& signature) noexcept
    {
        return NGIN::Crypto::Signatures::SignInto(
                context,
                SignatureAlgorithm::Ed25519,
                NGIN::Crypto::Signatures::SignInput {
                        .privateKey = NGIN::Crypto::Memory::SecretView {privateKey.Bytes()},
                        .message    = message,
                },
                ByteSpan {signature.data(), signature.size()});
    }

    /// @brief Signs a message and returns an owned fixed Ed25519 signature.
    [[nodiscard]] inline CryptoExpected<NGIN::Crypto::Signatures::Ed25519Signature> SignEd25519(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const Ed25519PrivateKey&                    privateKey,
            ConstByteSpan                               message)
    {
        NGIN::Crypto::Signatures::Ed25519Signature signature {};
        CryptoExpected<void>                       result = SignEd25519Into(context, privateKey, message, signature);
        if (!result.HasValue())
        {
            return result.Error();
        }

        return signature;
    }

    /// @brief Verifies a fixed Ed25519 signature for a message.
    [[nodiscard]] inline CryptoExpected<void> VerifyEd25519(
            const NGIN::Crypto::Backend::CryptoContext&       context,
            const Ed25519PublicKey&                           publicKey,
            ConstByteSpan                                     message,
            const NGIN::Crypto::Signatures::Ed25519Signature& signature) noexcept
    {
        return NGIN::Crypto::Signatures::Verify(
                context,
                SignatureAlgorithm::Ed25519,
                NGIN::Crypto::Signatures::VerifyInput {
                        .publicKey = publicKey.Bytes(),
                        .message   = message,
                        .signature = signature,
                });
    }
}// namespace NGIN::Crypto::Asymmetric
