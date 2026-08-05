/// @file KeyDerivation.hpp
/// @brief Algorithm-tagged HKDF, PBKDF2, and Argon2id derivation parameters.
#pragma once

#include <NGIN/Crypto/Backend/CryptoContext.hpp>
#include <NGIN/Crypto/ByteBuffer.hpp>
#include <NGIN/Crypto/Memory/Secret.hpp>
#include <NGIN/Crypto/Memory/SecretView.hpp>
#include <NGIN/Crypto/Types.hpp>

namespace NGIN::Crypto::Kdf
{
    /// @brief HKDF derivation inputs. `info` is context/application binding data and may be empty.
    struct HkdfParameters
    {
        NGIN::Crypto::Memory::SecretView inputKeyMaterial {};
        ConstByteSpan                    salt {};
        ConstByteSpan                    info {};
    };

    /// @brief PBKDF2 derivation inputs. PBKDF2 is for interoperability; prefer Argon2id for password storage.
    struct Pbkdf2Parameters
    {
        NGIN::Crypto::Memory::SecretView password {};
        ConstByteSpan                    salt {};
        NGIN::UInt32                     iterations {0};
    };

    /// @brief Argon2id derivation inputs.
    struct Argon2idParameters
    {
        NGIN::Crypto::Memory::SecretView password {};
        ConstByteSpan                    salt {};
        NGIN::UInt32                     memoryKiB {0};
        NGIN::UInt32                     iterations {0};
        NGIN::UInt32                     parallelism {1};
    };

    /// @brief Algorithm-tagged KDF parameter view.
    class KeyDerivationParameters
    {
    public:
        /// @brief Selects HKDF-SHA-256 with borrowed parameters.
        constexpr explicit KeyDerivationParameters(const HkdfParameters& parameters) noexcept
            : m_algorithm {KdfAlgorithm::HkdfSha256}, m_hkdf {&parameters}
        {
        }

        /// @brief Selects an HKDF algorithm with borrowed parameters.
        constexpr KeyDerivationParameters(KdfAlgorithm algorithm, const HkdfParameters& parameters) noexcept
            : m_algorithm {algorithm}, m_hkdf {&parameters}
        {
        }

        /// @brief Selects a PBKDF2 algorithm with borrowed parameters.
        constexpr KeyDerivationParameters(KdfAlgorithm algorithm, const Pbkdf2Parameters& parameters) noexcept
            : m_algorithm {algorithm}, m_pbkdf2 {&parameters}
        {
        }

        /// @brief Selects Argon2id with borrowed parameters.
        constexpr explicit KeyDerivationParameters(const Argon2idParameters& parameters) noexcept
            : m_algorithm {KdfAlgorithm::Argon2id}, m_argon2id {&parameters}
        {
        }

        /// @brief Returns the selected derivation algorithm.
        [[nodiscard]] constexpr KdfAlgorithm Algorithm() const noexcept
        {
            return m_algorithm;
        }

        /// @brief Returns HKDF parameters, or `nullptr` for another parameter kind.
        [[nodiscard]] constexpr const HkdfParameters* Hkdf() const noexcept
        {
            return m_hkdf;
        }

        /// @brief Returns PBKDF2 parameters, or `nullptr` for another parameter kind.
        [[nodiscard]] constexpr const Pbkdf2Parameters* Pbkdf2() const noexcept
        {
            return m_pbkdf2;
        }

        /// @brief Returns Argon2id parameters, or `nullptr` for another parameter kind.
        [[nodiscard]] constexpr const Argon2idParameters* Argon2id() const noexcept
        {
            return m_argon2id;
        }

    private:
        KdfAlgorithm              m_algorithm {KdfAlgorithm::HkdfSha256};
        const HkdfParameters*     m_hkdf {nullptr};
        const Pbkdf2Parameters*   m_pbkdf2 {nullptr};
        const Argon2idParameters* m_argon2id {nullptr};
    };

    /// @brief Derives key material into caller-provided output storage.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<void> DeriveKeyInto(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const KeyDerivationParameters&              parameters,
            ByteSpan                                    output) noexcept;

    /// @brief Derives key material into owned non-secret output storage.
    [[nodiscard]] NGIN_CRYPTO_API CryptoExpected<ByteBuffer> DeriveKey(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const KeyDerivationParameters&              parameters,
            NGIN::UIntSize                              outputSize);

    /// @brief Derives key material into fixed-size secret output storage.
    template<NGIN::UIntSize Size>
    [[nodiscard]] CryptoExpected<NGIN::Crypto::Memory::FixedSecret<Size>> DeriveFixedSecret(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const KeyDerivationParameters&              parameters)
    {
        NGIN::Crypto::Memory::FixedSecret<Size> output {};
        CryptoExpected<void>                    result = DeriveKeyInto(context, parameters, output.UnsafeMutableBytes());
        if (!result.HasValue())
        {
            return result.Error();
        }

        return output;
    }
}// namespace NGIN::Crypto::Kdf
