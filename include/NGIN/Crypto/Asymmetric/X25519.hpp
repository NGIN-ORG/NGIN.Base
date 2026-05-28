#pragma once

#include <NGIN/Crypto/Asymmetric/KeyTypes.hpp>
#include <NGIN/Crypto/Backend/CryptoContext.hpp>

namespace NGIN::Crypto::Asymmetric
{
    using X25519PublicKey    = PublicKey<X25519KeyTag, 32>;
    using X25519PrivateKey   = PrivateKey<X25519KeyTag, 32>;
    using X25519KeyPair      = KeyPair<X25519PublicKey, X25519PrivateKey>;
    using X25519SharedSecret = NGIN::Crypto::Memory::FixedSecret<32>;

    /// @brief Generates an X25519 key pair using a backend implementation.
    [[nodiscard]] CryptoExpected<X25519KeyPair> GenerateX25519KeyPair(
            const NGIN::Crypto::Backend::CryptoContext& context) noexcept;

    /// @brief Derives an X25519 shared secret into caller-provided secret storage.
    [[nodiscard]] CryptoExpected<void> DeriveX25519SharedSecretInto(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const X25519PrivateKey&                     privateKey,
            const X25519PublicKey&                      peerPublicKey,
            ByteSpan                                    output) noexcept;

    /// @brief Derives an X25519 shared secret into owned fixed-size secret storage.
    [[nodiscard]] CryptoExpected<X25519SharedSecret> DeriveX25519SharedSecret(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const X25519PrivateKey&                     privateKey,
            const X25519PublicKey&                      peerPublicKey) noexcept;
}// namespace NGIN::Crypto::Asymmetric
