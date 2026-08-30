#include <NGIN/Crypto/Keys/PrivateKeyInfo.hpp>
#include <NGIN/Crypto/Keys/SubjectPublicKeyInfo.hpp>

#include <NGIN/Crypto/Encoding/Der.hpp>
#include <NGIN/Crypto/Errors/CryptoError.hpp>
#include <NGIN/Crypto/Kdf/Pbkdf2.hpp>
#include <NGIN/Crypto/Memory/SecureBuffer.hpp>
#include <NGIN/Crypto/Symmetric/Aead.hpp>

#include <array>
#include <limits>
#include <optional>
#include <utility>

namespace NGIN::Crypto::Keys
{
    namespace
    {
        using NGIN::Crypto::Encoding::DerElement;
        using NGIN::Crypto::Encoding::DerReader;
        using NGIN::Crypto::Encoding::DerUniversalTag;

        constexpr std::array<NGIN::UInt32, 4> ED25519_OID {1, 3, 101, 112};
        constexpr std::array<NGIN::UInt32, 4> X25519_OID {1, 3, 101, 110};
        constexpr std::array<NGIN::UInt32, 6> EC_PUBLIC_KEY_OID {1, 2, 840, 10045, 2, 1};
        constexpr std::array<NGIN::UInt32, 7> SECP256R1_OID {1, 2, 840, 10045, 3, 1, 7};
        constexpr std::array<NGIN::UInt32, 7> RSA_ENCRYPTION_OID {1, 2, 840, 113549, 1, 1, 1};
        constexpr std::array<NGIN::UInt32, 7> PBES2_OID {1, 2, 840, 113549, 1, 5, 13};
        constexpr std::array<NGIN::UInt32, 7> PBKDF2_OID {1, 2, 840, 113549, 1, 5, 12};
        constexpr std::array<NGIN::UInt32, 6> HMAC_SHA256_OID {1, 2, 840, 113549, 2, 9};
        constexpr std::array<NGIN::UInt32, 9> AES_128_GCM_OID {2, 16, 840, 1, 101, 3, 4, 1, 6};
        constexpr std::array<NGIN::UInt32, 9> AES_256_GCM_OID {2, 16, 840, 1, 101, 3, 4, 1, 46};

        struct Pbkdf2PbesParameters
        {
            ByteBuffer     salt;
            NGIN::UInt32   iterations {0};
            NGIN::UIntSize keyLength {0};
        };

        struct AesGcmPbesParameters
        {
            AeadAlgorithm  algorithm {AeadAlgorithm::Aes256Gcm};
            ByteBuffer     nonce;
            NGIN::UIntSize keyLength {0};
            NGIN::UIntSize tagLength {0};
        };

        struct Pbes2Parameters
        {
            Pbkdf2PbesParameters kdf;
            AesGcmPbesParameters encryption;
        };

        [[nodiscard]] constexpr CryptoError ParseError() noexcept
        {
            return CryptoError {CryptoErrorCode::ParseError};
        }

        [[nodiscard]] constexpr CryptoError InvalidArgument() noexcept
        {
            return CryptoError {CryptoErrorCode::InvalidArgument};
        }

        [[nodiscard]] constexpr CryptoError InvalidKey() noexcept
        {
            return CryptoError {CryptoErrorCode::InvalidKey};
        }

        [[nodiscard]] constexpr CryptoError UnsupportedAlgorithm() noexcept
        {
            return CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

        [[nodiscard]] ByteBuffer CopyBytes(ConstByteSpan bytes)
        {
            auto buffer = MakeByteBuffer(bytes.size());
            for (NGIN::UIntSize i = 0; i < bytes.size(); ++i)
            {
                buffer[i] = bytes[i];
            }
            return buffer;
        }

        void AppendBytes(ByteBuffer& output, ConstByteSpan bytes)
        {
            for (NGIN::Byte byte: bytes)
            {
                output.PushBack(byte);
            }
        }

        [[nodiscard]] bool OidEquals(
                const NGIN::Containers::Vector<NGIN::UInt32>& actual, std::span<const NGIN::UInt32> expected) noexcept
        {
            if (actual.Size() != expected.size())
            {
                return false;
            }

            for (NGIN::UIntSize i = 0; i < expected.size(); ++i)
            {
                if (actual[i] != expected[i])
                {
                    return false;
                }
            }

            return true;
        }

        [[nodiscard]] bool IsDerNull(const DerElement& element) noexcept
        {
            return NGIN::Crypto::Encoding::IsDerUniversalElement(element, DerUniversalTag::Null) && element.value.empty();
        }

        [[nodiscard]] bool IsDerInteger(const DerElement& element) noexcept
        {
            return NGIN::Crypto::Encoding::IsDerUniversalElement(element, DerUniversalTag::Integer);
        }

        [[nodiscard]] CryptoExpected<DerElement> ReadSingleElement(ConstByteSpan der) noexcept
        {
            DerReader reader {der};
            auto      element = reader.ReadElement();
            if (!element.has_value())
            {
                return std::unexpected(std::move(element).error());
            }
            if (!reader.IsAtEnd())
            {
                return std::unexpected(ParseError());
            }

            return element.value();
        }

        [[nodiscard]] CryptoExpected<NGIN::UInt32> ReadDerPositiveUInt32(const DerElement& element) noexcept
        {
            auto integer = NGIN::Crypto::Encoding::ReadDerInteger(element);
            if (!integer.has_value())
            {
                return std::unexpected(std::move(integer).error());
            }
            if (integer.value().empty() || (std::to_integer<NGIN::UInt8>(integer.value()[0]) & 0x80u) != 0)
            {
                return std::unexpected(ParseError());
            }

            ConstByteSpan value = integer.value();
            if (value.size() > 1 && value[0] == NGIN::Byte {0x00})
            {
                value = value.subspan(1);
            }
            if (value.empty() || value.size() > sizeof(NGIN::UInt32))
            {
                return std::unexpected(ParseError());
            }

            auto output = NGIN::UInt32 {0};
            for (NGIN::Byte byte: value)
            {
                output = static_cast<NGIN::UInt32>((output << 8u) | std::to_integer<NGIN::UInt8>(byte));
            }

            return output;
        }

        [[nodiscard]] CryptoExpected<KeyAlgorithm> IdentifyAlgorithm(
                const NGIN::Containers::Vector<NGIN::UInt32>& oid,
                bool                                          hasParameters,
                ConstByteSpan                                 parameters)
        {
            if (OidEquals(oid, ED25519_OID))
            {
                if (hasParameters)
                {
                    return std::unexpected(ParseError());
                }
                return KeyAlgorithm::Ed25519;
            }
            if (OidEquals(oid, X25519_OID))
            {
                if (hasParameters)
                {
                    return std::unexpected(ParseError());
                }
                return KeyAlgorithm::X25519;
            }
            if (OidEquals(oid, EC_PUBLIC_KEY_OID))
            {
                if (!hasParameters)
                {
                    return std::unexpected(ParseError());
                }

                auto parameterElement = ReadSingleElement(parameters);
                if (!parameterElement.has_value())
                {
                    return std::unexpected(std::move(parameterElement).error());
                }

                auto curveOid = NGIN::Crypto::Encoding::ReadDerObjectIdentifier(parameterElement.value());
                if (!curveOid.has_value())
                {
                    return std::unexpected(std::move(curveOid).error());
                }

                return OidEquals(curveOid.value(), SECP256R1_OID) ? CryptoExpected<KeyAlgorithm> {KeyAlgorithm::EcdsaP256}
                                                                  : CryptoExpected<KeyAlgorithm> {KeyAlgorithm::Unknown};
            }
            if (OidEquals(oid, RSA_ENCRYPTION_OID))
            {
                if (hasParameters)
                {
                    auto parameterElement = ReadSingleElement(parameters);
                    if (!parameterElement.has_value())
                    {
                        return std::unexpected(std::move(parameterElement).error());
                    }
                    if (!IsDerNull(parameterElement.value()))
                    {
                        return std::unexpected(ParseError());
                    }
                }

                return KeyAlgorithm::Rsa;
            }

            return KeyAlgorithm::Unknown;
        }

        [[nodiscard]] KeyAlgorithmIdentifier MakeKeyAlgorithmIdentifier(KeyAlgorithm algorithm)
        {
            KeyAlgorithmIdentifier identifier {
                    .algorithm        = algorithm,
                    .objectIdentifier = {},
                    .parameters       = {},
                    .hasParameters    = false,
            };

            if (algorithm == KeyAlgorithm::EcdsaP256)
            {
                constexpr std::array<NGIN::Byte, 10> encodedCurveOid {
                        NGIN::Byte {0x06},
                        NGIN::Byte {0x08},
                        NGIN::Byte {0x2a},
                        NGIN::Byte {0x86},
                        NGIN::Byte {0x48},
                        NGIN::Byte {0xce},
                        NGIN::Byte {0x3d},
                        NGIN::Byte {0x03},
                        NGIN::Byte {0x01},
                        NGIN::Byte {0x07},
                };
                identifier.parameters    = CopyBytes(ConstByteSpan {encodedCurveOid.data(), encodedCurveOid.size()});
                identifier.hasParameters = true;
            }

            return identifier;
        }

        [[nodiscard]] CryptoExpected<EncryptedPrivateKeyAlgorithmIdentifier> ParseRawAlgorithmIdentifierElement(
                const DerElement& element);

        [[nodiscard]] CryptoExpected<void> ValidateAbsentOrNullParameters(
                const EncryptedPrivateKeyAlgorithmIdentifier& identifier) noexcept
        {
            if (!identifier.hasParameters)
            {
                return {};
            }

            auto parameterElement = ReadSingleElement(
                    ConstByteSpan {identifier.parameters.data(), identifier.parameters.Size()});
            if (!parameterElement.has_value())
            {
                return std::unexpected(std::move(parameterElement).error());
            }
            if (!IsDerNull(parameterElement.value()))
            {
                return std::unexpected(ParseError());
            }

            return {};
        }

        [[nodiscard]] CryptoExpected<Pbkdf2PbesParameters> ParsePbkdf2Parameters(ConstByteSpan parameters)
        {
            auto parameterElement = ReadSingleElement(parameters);
            if (!parameterElement.has_value())
            {
                return std::unexpected(std::move(parameterElement).error());
            }

            DerReader parent {parameterElement.value().encoded};
            auto      reader = NGIN::Crypto::Encoding::ReadDerSequence(parent, parameterElement.value());
            if (!reader.has_value())
            {
                return std::unexpected(std::move(reader).error());
            }

            auto saltElement = reader.value().ReadElement();
            if (!saltElement.has_value())
            {
                return std::unexpected(std::move(saltElement).error());
            }
            auto salt = NGIN::Crypto::Encoding::ReadDerOctetString(saltElement.value());
            if (!salt.has_value())
            {
                return std::unexpected(UnsupportedAlgorithm());
            }

            auto iterationsElement = reader.value().ReadElement();
            if (!iterationsElement.has_value())
            {
                return std::unexpected(std::move(iterationsElement).error());
            }
            auto iterations = ReadDerPositiveUInt32(iterationsElement.value());
            if (!iterations.has_value())
            {
                return std::unexpected(std::move(iterations).error());
            }
            if (iterations.value() == 0)
            {
                return std::unexpected(ParseError());
            }

            auto keyLength = std::optional<NGIN::UInt32> {};
            auto prf       = std::optional<EncryptedPrivateKeyAlgorithmIdentifier> {};

            if (!reader.value().IsAtEnd())
            {
                auto next = reader.value().ReadElement();
                if (!next.has_value())
                {
                    return std::unexpected(std::move(next).error());
                }

                if (IsDerInteger(next.value()))
                {
                    auto parsedKeyLength = ReadDerPositiveUInt32(next.value());
                    if (!parsedKeyLength.has_value())
                    {
                        return std::unexpected(std::move(parsedKeyLength).error());
                    }
                    if (parsedKeyLength.value() == 0)
                    {
                        return std::unexpected(ParseError());
                    }
                    keyLength = parsedKeyLength.value();

                    if (!reader.value().IsAtEnd())
                    {
                        auto prfElement = reader.value().ReadElement();
                        if (!prfElement.has_value())
                        {
                            return std::unexpected(std::move(prfElement).error());
                        }
                        auto parsedPrf = ParseRawAlgorithmIdentifierElement(prfElement.value());
                        if (!parsedPrf.has_value())
                        {
                            return std::unexpected(std::move(parsedPrf).error());
                        }
                        prf = std::move(parsedPrf.value());
                    }
                }
                else
                {
                    auto parsedPrf = ParseRawAlgorithmIdentifierElement(next.value());
                    if (!parsedPrf.has_value())
                    {
                        return std::unexpected(std::move(parsedPrf).error());
                    }
                    prf = std::move(parsedPrf.value());
                }
            }

            if (!reader.value().IsAtEnd())
            {
                return std::unexpected(ParseError());
            }
            if (!prf.has_value())
            {
                return std::unexpected(UnsupportedAlgorithm());
            }
            if (!OidEquals(prf->objectIdentifier, HMAC_SHA256_OID))
            {
                return std::unexpected(UnsupportedAlgorithm());
            }
            auto prfParameters = ValidateAbsentOrNullParameters(*prf);
            if (!prfParameters.has_value())
            {
                return std::unexpected(std::move(prfParameters).error());
            }

            return Pbkdf2PbesParameters {
                    .salt       = CopyBytes(salt.value()),
                    .iterations = iterations.value(),
                    .keyLength  = keyLength.value_or(0),
            };
        }

        [[nodiscard]] CryptoExpected<AesGcmPbesParameters> ParseAesGcmParameters(
                const EncryptedPrivateKeyAlgorithmIdentifier& encryptionScheme)
        {
            auto algorithm = AeadAlgorithm::Aes256Gcm;
            auto keyLength = NGIN::UIntSize {0};
            if (OidEquals(encryptionScheme.objectIdentifier, AES_128_GCM_OID))
            {
                algorithm = AeadAlgorithm::Aes128Gcm;
                keyLength = 16;
            }
            else if (OidEquals(encryptionScheme.objectIdentifier, AES_256_GCM_OID))
            {
                algorithm = AeadAlgorithm::Aes256Gcm;
                keyLength = 32;
            }
            else
            {
                return std::unexpected(UnsupportedAlgorithm());
            }
            if (!encryptionScheme.hasParameters)
            {
                return std::unexpected(ParseError());
            }

            auto parameterElement = ReadSingleElement(
                    ConstByteSpan {encryptionScheme.parameters.data(), encryptionScheme.parameters.Size()});
            if (!parameterElement.has_value())
            {
                return std::unexpected(std::move(parameterElement).error());
            }

            DerReader parent {parameterElement.value().encoded};
            auto      reader = NGIN::Crypto::Encoding::ReadDerSequence(parent, parameterElement.value());
            if (!reader.has_value())
            {
                return std::unexpected(std::move(reader).error());
            }

            auto nonceElement = reader.value().ReadElement();
            if (!nonceElement.has_value())
            {
                return std::unexpected(std::move(nonceElement).error());
            }
            auto nonce = NGIN::Crypto::Encoding::ReadDerOctetString(nonceElement.value());
            if (!nonce.has_value())
            {
                return std::unexpected(std::move(nonce).error());
            }

            auto tagLength = std::optional<NGIN::UInt32> {};
            if (!reader.value().IsAtEnd())
            {
                auto tagLengthElement = reader.value().ReadElement();
                if (!tagLengthElement.has_value())
                {
                    return std::unexpected(std::move(tagLengthElement).error());
                }
                auto parsedTagLength = ReadDerPositiveUInt32(tagLengthElement.value());
                if (!parsedTagLength.has_value())
                {
                    return std::unexpected(std::move(parsedTagLength).error());
                }
                if (parsedTagLength.value() == 0)
                {
                    return std::unexpected(ParseError());
                }
                tagLength = parsedTagLength.value();
            }
            if (!reader.value().IsAtEnd())
            {
                return std::unexpected(ParseError());
            }
            if (nonce.value().size() != Symmetric::AeadNonceSize(algorithm))
            {
                return std::unexpected(ParseError());
            }
            if (!tagLength.has_value() || tagLength.value() != Symmetric::AeadTagSize(algorithm))
            {
                return std::unexpected(UnsupportedAlgorithm());
            }

            return AesGcmPbesParameters {
                    .algorithm = algorithm,
                    .nonce     = CopyBytes(nonce.value()),
                    .keyLength = keyLength,
                    .tagLength = tagLength.value(),
            };
        }

        [[nodiscard]] CryptoExpected<Pbes2Parameters> ParsePbes2Parameters(
                const EncryptedPrivateKeyAlgorithmIdentifier& algorithm)
        {
            if (!OidEquals(algorithm.objectIdentifier, PBES2_OID))
            {
                return std::unexpected(UnsupportedAlgorithm());
            }
            if (!algorithm.hasParameters)
            {
                return std::unexpected(ParseError());
            }

            auto parameterElement = ReadSingleElement(ConstByteSpan {algorithm.parameters.data(), algorithm.parameters.Size()});
            if (!parameterElement.has_value())
            {
                return std::unexpected(std::move(parameterElement).error());
            }

            DerReader parent {parameterElement.value().encoded};
            auto      reader = NGIN::Crypto::Encoding::ReadDerSequence(parent, parameterElement.value());
            if (!reader.has_value())
            {
                return std::unexpected(std::move(reader).error());
            }

            auto kdfElement = reader.value().ReadElement();
            if (!kdfElement.has_value())
            {
                return std::unexpected(std::move(kdfElement).error());
            }
            auto kdf = ParseRawAlgorithmIdentifierElement(kdfElement.value());
            if (!kdf.has_value())
            {
                return std::unexpected(std::move(kdf).error());
            }
            if (!OidEquals(kdf.value().objectIdentifier, PBKDF2_OID) || !kdf.value().hasParameters)
            {
                return std::unexpected(UnsupportedAlgorithm());
            }

            auto encryptionElement = reader.value().ReadElement();
            if (!encryptionElement.has_value())
            {
                return std::unexpected(std::move(encryptionElement).error());
            }
            auto encryptionScheme = ParseRawAlgorithmIdentifierElement(encryptionElement.value());
            if (!encryptionScheme.has_value())
            {
                return std::unexpected(std::move(encryptionScheme).error());
            }
            if (!reader.value().IsAtEnd())
            {
                return std::unexpected(ParseError());
            }

            auto parsedEncryption = ParseAesGcmParameters(encryptionScheme.value());
            if (!parsedEncryption.has_value())
            {
                return std::unexpected(std::move(parsedEncryption).error());
            }
            auto parsedKdf =
                    ParsePbkdf2Parameters(ConstByteSpan {kdf.value().parameters.data(), kdf.value().parameters.Size()});
            if (!parsedKdf.has_value())
            {
                return std::unexpected(std::move(parsedKdf).error());
            }
            if (parsedKdf.value().keyLength != 0 && parsedKdf.value().keyLength != parsedEncryption.value().keyLength)
            {
                return std::unexpected(UnsupportedAlgorithm());
            }

            return Pbes2Parameters {
                    .kdf        = std::move(parsedKdf.value()),
                    .encryption = std::move(parsedEncryption.value()),
            };
        }

        [[nodiscard]] CryptoExpected<KeyAlgorithmIdentifier> ParseAlgorithmIdentifierElement(const DerElement& element)
        {
            DerReader parent {element.encoded};
            auto      reader = NGIN::Crypto::Encoding::ReadDerSequence(parent, element);
            if (!reader.has_value())
            {
                return std::unexpected(std::move(reader).error());
            }

            auto oidElement = reader.value().ReadElement();
            if (!oidElement.has_value())
            {
                return std::unexpected(std::move(oidElement).error());
            }

            auto oid = NGIN::Crypto::Encoding::ReadDerObjectIdentifier(oidElement.value());
            if (!oid.has_value())
            {
                return std::unexpected(std::move(oid).error());
            }

            KeyAlgorithmIdentifier identifier {
                    .objectIdentifier = std::move(oid.value()),
                    .parameters       = {},
                    .hasParameters    = false,
            };

            if (!reader.value().IsAtEnd())
            {
                auto parameters = reader.value().ReadElement();
                if (!parameters.has_value())
                {
                    return std::unexpected(std::move(parameters).error());
                }
                if (!reader.value().IsAtEnd())
                {
                    return std::unexpected(ParseError());
                }

                identifier.hasParameters = true;
                identifier.parameters    = CopyBytes(parameters.value().encoded);
            }

            auto algorithm = IdentifyAlgorithm(
                    identifier.objectIdentifier,
                    identifier.hasParameters,
                    ConstByteSpan {identifier.parameters.data(), identifier.parameters.Size()});
            if (!algorithm.has_value())
            {
                return std::unexpected(std::move(algorithm).error());
            }
            identifier.algorithm = algorithm.value();

            return identifier;
        }

        [[nodiscard]] CryptoExpected<EncryptedPrivateKeyAlgorithmIdentifier> ParseRawAlgorithmIdentifierElement(
                const DerElement& element)
        {
            DerReader parent {element.encoded};
            auto      reader = NGIN::Crypto::Encoding::ReadDerSequence(parent, element);
            if (!reader.has_value())
            {
                return std::unexpected(std::move(reader).error());
            }

            auto oidElement = reader.value().ReadElement();
            if (!oidElement.has_value())
            {
                return std::unexpected(std::move(oidElement).error());
            }

            auto oid = NGIN::Crypto::Encoding::ReadDerObjectIdentifier(oidElement.value());
            if (!oid.has_value())
            {
                return std::unexpected(std::move(oid).error());
            }

            EncryptedPrivateKeyAlgorithmIdentifier identifier {
                    .objectIdentifier = std::move(oid.value()),
                    .parameters       = {},
                    .hasParameters    = false,
            };

            if (!reader.value().IsAtEnd())
            {
                auto parameters = reader.value().ReadElement();
                if (!parameters.has_value())
                {
                    return std::unexpected(std::move(parameters).error());
                }
                if (!reader.value().IsAtEnd())
                {
                    return std::unexpected(ParseError());
                }

                identifier.hasParameters = true;
                identifier.parameters    = CopyBytes(parameters.value().encoded);
            }

            return identifier;
        }

        [[nodiscard]] CryptoExpected<ByteBuffer> EncodeRawAlgorithmIdentifier(
                const EncryptedPrivateKeyAlgorithmIdentifier& algorithm)
        {
            ByteBuffer children;

            auto oid = NGIN::Crypto::Encoding::EncodeDerObjectIdentifier(
                    std::span<const NGIN::UInt32> {algorithm.objectIdentifier.begin(), algorithm.objectIdentifier.Size()});
            if (!oid.has_value())
            {
                return std::unexpected(std::move(oid).error());
            }
            AppendBytes(children, ConstByteSpan {oid.value().data(), oid.value().Size()});

            if (algorithm.hasParameters)
            {
                auto parametersElement = ReadSingleElement(
                        ConstByteSpan {algorithm.parameters.data(), algorithm.parameters.Size()});
                if (!parametersElement.has_value())
                {
                    return std::unexpected(std::move(parametersElement).error());
                }
                AppendBytes(children, ConstByteSpan {algorithm.parameters.data(), algorithm.parameters.Size()});
            }
            else if (algorithm.parameters.Size() != 0)
            {
                return std::unexpected(InvalidArgument());
            }

            return NGIN::Crypto::Encoding::EncodeDerSequence(ConstByteSpan {children.data(), children.Size()});
        }

        [[nodiscard]] CryptoExpected<ByteBuffer> EncodeAlgorithmIdentifier(KeyAlgorithm algorithm)
        {
            ByteBuffer children;

            auto append = [&children](const ByteBuffer& bytes) {
                AppendBytes(children, ConstByteSpan {bytes.data(), bytes.Size()});
            };

            if (algorithm == KeyAlgorithm::Ed25519)
            {
                auto oid = NGIN::Crypto::Encoding::EncodeDerObjectIdentifier(ED25519_OID);
                if (!oid.has_value())
                {
                    return std::unexpected(std::move(oid).error());
                }
                append(oid.value());
                return NGIN::Crypto::Encoding::EncodeDerSequence(ConstByteSpan {children.data(), children.Size()});
            }

            if (algorithm == KeyAlgorithm::X25519)
            {
                auto oid = NGIN::Crypto::Encoding::EncodeDerObjectIdentifier(X25519_OID);
                if (!oid.has_value())
                {
                    return std::unexpected(std::move(oid).error());
                }
                append(oid.value());
                return NGIN::Crypto::Encoding::EncodeDerSequence(ConstByteSpan {children.data(), children.Size()});
            }

            if (algorithm == KeyAlgorithm::EcdsaP256)
            {
                auto oid = NGIN::Crypto::Encoding::EncodeDerObjectIdentifier(EC_PUBLIC_KEY_OID);
                if (!oid.has_value())
                {
                    return std::unexpected(std::move(oid).error());
                }
                auto curve = NGIN::Crypto::Encoding::EncodeDerObjectIdentifier(SECP256R1_OID);
                if (!curve.has_value())
                {
                    return std::unexpected(std::move(curve).error());
                }
                append(oid.value());
                append(curve.value());
                return NGIN::Crypto::Encoding::EncodeDerSequence(ConstByteSpan {children.data(), children.Size()});
            }

            if (algorithm == KeyAlgorithm::Rsa)
            {
                auto oid = NGIN::Crypto::Encoding::EncodeDerObjectIdentifier(RSA_ENCRYPTION_OID);
                if (!oid.has_value())
                {
                    return std::unexpected(std::move(oid).error());
                }
                auto nullParameters = NGIN::Crypto::Encoding::EncodeDerElement(
                        NGIN::Crypto::Encoding::MakeDerUniversalTag(DerUniversalTag::Null),
                        ConstByteSpan {});
                if (!nullParameters.has_value())
                {
                    return std::unexpected(std::move(nullParameters).error());
                }
                append(oid.value());
                append(nullParameters.value());
                return NGIN::Crypto::Encoding::EncodeDerSequence(ConstByteSpan {children.data(), children.Size()});
            }

            return std::unexpected(InvalidArgument());
        }

        [[nodiscard]] CryptoExpected<ByteBuffer> EncodeSequenceFromChildren(ConstByteSpan first, ConstByteSpan second)
        {
            ByteBuffer children;
            children.Reserve(first.size() + second.size());
            AppendBytes(children, first);
            AppendBytes(children, second);
            return NGIN::Crypto::Encoding::EncodeDerSequence(ConstByteSpan {children.data(), children.Size()});
        }

        [[nodiscard]] CryptoExpected<ByteBuffer> EncodeSequenceFromChildren(
                ConstByteSpan first, ConstByteSpan second, ConstByteSpan third)
        {
            ByteBuffer children;
            children.Reserve(first.size() + second.size() + third.size());
            AppendBytes(children, first);
            AppendBytes(children, second);
            AppendBytes(children, third);
            return NGIN::Crypto::Encoding::EncodeDerSequence(ConstByteSpan {children.data(), children.Size()});
        }
    }// namespace

    CryptoExpected<SignatureAlgorithm> ToSignatureAlgorithm(KeyAlgorithm algorithm) noexcept
    {
        switch (algorithm)
        {
            case KeyAlgorithm::Ed25519:
                return SignatureAlgorithm::Ed25519;
            case KeyAlgorithm::EcdsaP256:
                return SignatureAlgorithm::EcdsaP256Sha256;
            case KeyAlgorithm::Rsa:
                return SignatureAlgorithm::RsaPssSha256;
            case KeyAlgorithm::Unknown:
            case KeyAlgorithm::X25519:
                return std::unexpected(UnsupportedAlgorithm());
        }

        return std::unexpected(UnsupportedAlgorithm());
    }

    CryptoExpected<KeyAgreementAlgorithm> ToKeyAgreementAlgorithm(KeyAlgorithm algorithm) noexcept
    {
        if (algorithm == KeyAlgorithm::X25519)
        {
            return KeyAgreementAlgorithm::X25519;
        }

        return std::unexpected(UnsupportedAlgorithm());
    }

    CryptoExpected<KeyAlgorithm> FromSignatureAlgorithm(SignatureAlgorithm algorithm) noexcept
    {
        switch (algorithm)
        {
            case SignatureAlgorithm::Ed25519:
                return KeyAlgorithm::Ed25519;
            case SignatureAlgorithm::EcdsaP256Sha256:
                return KeyAlgorithm::EcdsaP256;
            case SignatureAlgorithm::RsaPssSha256:
                return KeyAlgorithm::Rsa;
        }

        return std::unexpected(UnsupportedAlgorithm());
    }

    CryptoExpected<KeyAlgorithm> FromKeyAgreementAlgorithm(KeyAgreementAlgorithm algorithm) noexcept
    {
        switch (algorithm)
        {
            case KeyAgreementAlgorithm::X25519:
                return KeyAlgorithm::X25519;
        }

        return std::unexpected(UnsupportedAlgorithm());
    }

    CryptoExpected<SubjectPublicKeyInfo> ParseSubjectPublicKeyInfo(ConstByteSpan der)
    {
        DerReader reader {der};
        auto      top = reader.ReadElement();
        if (!top.has_value())
        {
            return std::unexpected(std::move(top).error());
        }
        if (!reader.IsAtEnd())
        {
            return std::unexpected(ParseError());
        }

        auto sequence = NGIN::Crypto::Encoding::ReadDerSequence(reader, top.value());
        if (!sequence.has_value())
        {
            return std::unexpected(std::move(sequence).error());
        }

        auto algorithmElement = sequence.value().ReadElement();
        if (!algorithmElement.has_value())
        {
            return std::unexpected(std::move(algorithmElement).error());
        }

        auto algorithm = ParseAlgorithmIdentifierElement(algorithmElement.value());
        if (!algorithm.has_value())
        {
            return std::unexpected(std::move(algorithm).error());
        }

        auto publicKeyElement = sequence.value().ReadElement();
        if (!publicKeyElement.has_value())
        {
            return std::unexpected(std::move(publicKeyElement).error());
        }
        if (!sequence.value().IsAtEnd())
        {
            return std::unexpected(ParseError());
        }

        auto bitString = NGIN::Crypto::Encoding::ReadDerBitString(publicKeyElement.value());
        if (!bitString.has_value())
        {
            return std::unexpected(std::move(bitString).error());
        }
        if (bitString.value().unusedBitCount != 0)
        {
            return std::unexpected(ParseError());
        }

        return SubjectPublicKeyInfo {
                .algorithm = std::move(algorithm.value()),
                .publicKey = CopyBytes(bitString.value().bytes),
        };
    }

    CryptoExpected<ByteBuffer> WriteSubjectPublicKeyInfo(KeyAlgorithm algorithm, ConstByteSpan publicKey)
    {
        auto algorithmIdentifier = EncodeAlgorithmIdentifier(algorithm);
        if (!algorithmIdentifier.has_value())
        {
            return std::unexpected(std::move(algorithmIdentifier).error());
        }

        auto publicKeyBits = NGIN::Crypto::Encoding::EncodeDerBitString(0, publicKey);
        if (!publicKeyBits.has_value())
        {
            return std::unexpected(std::move(publicKeyBits).error());
        }

        return EncodeSequenceFromChildren(
                ConstByteSpan {algorithmIdentifier.value().data(), algorithmIdentifier.value().Size()},
                ConstByteSpan {publicKeyBits.value().data(), publicKeyBits.value().Size()});
    }

    CryptoExpected<NGIN::Crypto::Asymmetric::Ed25519PublicKey> ImportEd25519PublicKey(
            const SubjectPublicKeyInfo& publicKeyInfo) noexcept
    {
        if (publicKeyInfo.algorithm.algorithm != KeyAlgorithm::Ed25519)
        {
            return std::unexpected(InvalidKey());
        }

        return NGIN::Crypto::Asymmetric::Ed25519PublicKey::FromBytes(
                ConstByteSpan {publicKeyInfo.publicKey.data(), publicKeyInfo.publicKey.Size()});
    }

    CryptoExpected<NGIN::Crypto::Asymmetric::X25519PublicKey> ImportX25519PublicKey(
            const SubjectPublicKeyInfo& publicKeyInfo) noexcept
    {
        if (publicKeyInfo.algorithm.algorithm != KeyAlgorithm::X25519)
        {
            return std::unexpected(InvalidKey());
        }

        return NGIN::Crypto::Asymmetric::X25519PublicKey::FromBytes(
                ConstByteSpan {publicKeyInfo.publicKey.data(), publicKeyInfo.publicKey.Size()});
    }

    CryptoExpected<NGIN::Crypto::Asymmetric::EcdsaP256PublicKey> ImportEcdsaP256PublicKey(
            const SubjectPublicKeyInfo& publicKeyInfo) noexcept
    {
        if (publicKeyInfo.algorithm.algorithm != KeyAlgorithm::EcdsaP256 || publicKeyInfo.publicKey.Size() != 65 ||
            publicKeyInfo.publicKey[0] != NGIN::Byte {0x04})
        {
            return std::unexpected(InvalidKey());
        }

        return NGIN::Crypto::Asymmetric::EcdsaP256PublicKey::FromBytes(
                ConstByteSpan {publicKeyInfo.publicKey.data(), publicKeyInfo.publicKey.Size()});
    }

    SubjectPublicKeyInfo ExportSubjectPublicKeyInfo(const NGIN::Crypto::Asymmetric::Ed25519PublicKey& publicKey)
    {
        return SubjectPublicKeyInfo {
                .algorithm = MakeKeyAlgorithmIdentifier(KeyAlgorithm::Ed25519),
                .publicKey = CopyBytes(publicKey.Bytes()),
        };
    }

    SubjectPublicKeyInfo ExportSubjectPublicKeyInfo(const NGIN::Crypto::Asymmetric::X25519PublicKey& publicKey)
    {
        return SubjectPublicKeyInfo {
                .algorithm = MakeKeyAlgorithmIdentifier(KeyAlgorithm::X25519),
                .publicKey = CopyBytes(publicKey.Bytes()),
        };
    }

    SubjectPublicKeyInfo ExportSubjectPublicKeyInfo(const NGIN::Crypto::Asymmetric::EcdsaP256PublicKey& publicKey)
    {
        return SubjectPublicKeyInfo {
                .algorithm = MakeKeyAlgorithmIdentifier(KeyAlgorithm::EcdsaP256),
                .publicKey = CopyBytes(publicKey.Bytes()),
        };
    }

    CryptoExpected<PrivateKeyInfo> ParsePrivateKeyInfo(ConstByteSpan der)
    {
        DerReader reader {der};
        auto      top = reader.ReadElement();
        if (!top.has_value())
        {
            return std::unexpected(std::move(top).error());
        }
        if (!reader.IsAtEnd())
        {
            return std::unexpected(ParseError());
        }

        auto sequence = NGIN::Crypto::Encoding::ReadDerSequence(reader, top.value());
        if (!sequence.has_value())
        {
            return std::unexpected(std::move(sequence).error());
        }

        auto versionElement = sequence.value().ReadElement();
        if (!versionElement.has_value())
        {
            return std::unexpected(std::move(versionElement).error());
        }
        auto version = NGIN::Crypto::Encoding::ReadDerInteger(versionElement.value());
        if (!version.has_value())
        {
            return std::unexpected(std::move(version).error());
        }
        if (version.value().size() != 1 || version.value()[0] != NGIN::Byte {0})
        {
            return std::unexpected(ParseError());
        }

        auto algorithmElement = sequence.value().ReadElement();
        if (!algorithmElement.has_value())
        {
            return std::unexpected(std::move(algorithmElement).error());
        }
        auto algorithm = ParseAlgorithmIdentifierElement(algorithmElement.value());
        if (!algorithm.has_value())
        {
            return std::unexpected(std::move(algorithm).error());
        }

        auto privateKeyElement = sequence.value().ReadElement();
        if (!privateKeyElement.has_value())
        {
            return std::unexpected(std::move(privateKeyElement).error());
        }
        auto privateKey = NGIN::Crypto::Encoding::ReadDerOctetString(privateKeyElement.value());
        if (!privateKey.has_value())
        {
            return std::unexpected(std::move(privateKey).error());
        }
        if (!sequence.value().IsAtEnd())
        {
            return std::unexpected(ParseError());
        }

        return PrivateKeyInfo {
                .version    = 0,
                .algorithm  = std::move(algorithm.value()),
                .privateKey = CopyBytes(privateKey.value()),
        };
    }

    CryptoExpected<ByteBuffer> WritePrivateKeyInfo(KeyAlgorithm algorithm, ConstByteSpan privateKey)
    {
        const std::array<NGIN::Byte, 1> versionValue {NGIN::Byte {0}};
        auto                            version = NGIN::Crypto::Encoding::EncodeDerInteger(versionValue);
        if (!version.has_value())
        {
            return std::unexpected(std::move(version).error());
        }

        auto algorithmIdentifier = EncodeAlgorithmIdentifier(algorithm);
        if (!algorithmIdentifier.has_value())
        {
            return std::unexpected(std::move(algorithmIdentifier).error());
        }

        auto privateKeyOctets = NGIN::Crypto::Encoding::EncodeDerOctetString(privateKey);
        if (!privateKeyOctets.has_value())
        {
            return std::unexpected(std::move(privateKeyOctets).error());
        }

        return EncodeSequenceFromChildren(
                ConstByteSpan {version.value().data(), version.value().Size()},
                ConstByteSpan {algorithmIdentifier.value().data(), algorithmIdentifier.value().Size()},
                ConstByteSpan {privateKeyOctets.value().data(), privateKeyOctets.value().Size()});
    }

    CryptoExpected<EncryptedPrivateKeyInfo> ParseEncryptedPrivateKeyInfo(ConstByteSpan der)
    {
        DerReader reader {der};
        auto      top = reader.ReadElement();
        if (!top.has_value())
        {
            return std::unexpected(std::move(top).error());
        }
        if (!reader.IsAtEnd())
        {
            return std::unexpected(ParseError());
        }

        auto sequence = NGIN::Crypto::Encoding::ReadDerSequence(reader, top.value());
        if (!sequence.has_value())
        {
            return std::unexpected(std::move(sequence).error());
        }

        auto algorithmElement = sequence.value().ReadElement();
        if (!algorithmElement.has_value())
        {
            return std::unexpected(std::move(algorithmElement).error());
        }
        auto algorithm = ParseRawAlgorithmIdentifierElement(algorithmElement.value());
        if (!algorithm.has_value())
        {
            return std::unexpected(std::move(algorithm).error());
        }

        auto encryptedDataElement = sequence.value().ReadElement();
        if (!encryptedDataElement.has_value())
        {
            return std::unexpected(std::move(encryptedDataElement).error());
        }
        auto encryptedData = NGIN::Crypto::Encoding::ReadDerOctetString(encryptedDataElement.value());
        if (!encryptedData.has_value())
        {
            return std::unexpected(std::move(encryptedData).error());
        }
        if (!sequence.value().IsAtEnd())
        {
            return std::unexpected(ParseError());
        }

        return EncryptedPrivateKeyInfo {
                .encryptionAlgorithm = std::move(algorithm.value()),
                .encryptedData       = CopyBytes(encryptedData.value()),
        };
    }

    CryptoExpected<ByteBuffer> WriteEncryptedPrivateKeyInfo(
            const EncryptedPrivateKeyAlgorithmIdentifier& encryptionAlgorithm,
            ConstByteSpan                                 encryptedData)
    {
        auto algorithmIdentifier = EncodeRawAlgorithmIdentifier(encryptionAlgorithm);
        if (!algorithmIdentifier.has_value())
        {
            return std::unexpected(std::move(algorithmIdentifier).error());
        }

        auto encryptedDataOctets = NGIN::Crypto::Encoding::EncodeDerOctetString(encryptedData);
        if (!encryptedDataOctets.has_value())
        {
            return std::unexpected(std::move(encryptedDataOctets).error());
        }

        return EncodeSequenceFromChildren(
                ConstByteSpan {algorithmIdentifier.value().data(), algorithmIdentifier.value().Size()},
                ConstByteSpan {encryptedDataOctets.value().data(), encryptedDataOctets.value().Size()});
    }

    CryptoExpected<PrivateKeyInfo> DecryptEncryptedPrivateKeyInfo(
            const NGIN::Crypto::Backend::CryptoContext& context,
            const EncryptedPrivateKeyInfo&              encryptedPrivateKeyInfo,
            NGIN::Crypto::Memory::SecretView            password,
            const EncryptedPrivateKeyDecryptOptions&    options)
    {
        auto parameters = ParsePbes2Parameters(encryptedPrivateKeyInfo.encryptionAlgorithm);
        if (!parameters.has_value())
        {
            return std::unexpected(std::move(parameters).error());
        }

        const auto& kdf        = parameters.value().kdf;
        const auto& encryption = parameters.value().encryption;
        if (kdf.iterations < options.minimumPbkdf2Iterations)
        {
            return std::unexpected(InvalidArgument());
        }
        if (encryptedPrivateKeyInfo.encryptedData.Size() < encryption.tagLength)
        {
            return std::unexpected(ParseError());
        }

        const auto ciphertextSize = encryptedPrivateKeyInfo.encryptedData.Size() - encryption.tagLength;
        if (ciphertextSize > options.maxPlaintextBytes)
        {
            return std::unexpected(InvalidArgument());
        }

        auto                                derivedKey = NGIN::Crypto::Memory::SecureBuffer {encryption.keyLength};
        NGIN::Crypto::Kdf::Pbkdf2Parameters kdfParameters {
                .password   = password,
                .salt       = ConstByteSpan {kdf.salt.data(), kdf.salt.Size()},
                .iterations = kdf.iterations,
        };
        auto keyResult = NGIN::Crypto::Kdf::Pbkdf2Sha256Into(context, kdfParameters, derivedKey.AsBytes());
        if (!keyResult.has_value())
        {
            return std::unexpected(std::move(keyResult).error());
        }

        auto encryptedBytes = ConstByteSpan {
                encryptedPrivateKeyInfo.encryptedData.data(),
                encryptedPrivateKeyInfo.encryptedData.Size(),
        };
        auto ciphertext = encryptedBytes.subspan(0, ciphertextSize);
        auto tag        = encryptedBytes.subspan(ciphertextSize, encryption.tagLength);

        auto plaintext = NGIN::Crypto::Symmetric::Open(
                context,
                encryption.algorithm,
                NGIN::Crypto::Symmetric::AeadOpenInput {
                        .key            = NGIN::Crypto::Memory::SecretView {derivedKey.AsBytes()},
                        .nonce          = ConstByteSpan {encryption.nonce.data(), encryption.nonce.Size()},
                        .ciphertext     = ciphertext,
                        .associatedData = {},
                        .tag            = tag,
                });
        if (!plaintext.has_value())
        {
            return std::unexpected(std::move(plaintext).error());
        }

        auto privateKeyInfo = ParsePrivateKeyInfo(ConstByteSpan {plaintext.value().data(), plaintext.value().Size()});
        if (!privateKeyInfo.has_value())
        {
            NGIN::Crypto::Memory::SecureZero(ByteSpan {plaintext.value().data(), plaintext.value().Size()});
            return std::unexpected(std::move(privateKeyInfo).error());
        }

        NGIN::Crypto::Memory::SecureZero(ByteSpan {plaintext.value().data(), plaintext.value().Size()});
        return privateKeyInfo.value();
    }

    CryptoExpected<NGIN::Crypto::Asymmetric::Ed25519PrivateKey> ImportEd25519PrivateKey(
            const PrivateKeyInfo& privateKeyInfo) noexcept
    {
        if (privateKeyInfo.algorithm.algorithm != KeyAlgorithm::Ed25519)
        {
            return std::unexpected(InvalidKey());
        }

        return NGIN::Crypto::Asymmetric::Ed25519PrivateKey::FromSecretBytes(
                ConstByteSpan {privateKeyInfo.privateKey.data(), privateKeyInfo.privateKey.Size()});
    }

    CryptoExpected<NGIN::Crypto::Asymmetric::X25519PrivateKey> ImportX25519PrivateKey(
            const PrivateKeyInfo& privateKeyInfo) noexcept
    {
        if (privateKeyInfo.algorithm.algorithm != KeyAlgorithm::X25519)
        {
            return std::unexpected(InvalidKey());
        }

        return NGIN::Crypto::Asymmetric::X25519PrivateKey::FromSecretBytes(
                ConstByteSpan {privateKeyInfo.privateKey.data(), privateKeyInfo.privateKey.Size()});
    }

    CryptoExpected<NGIN::Crypto::Asymmetric::EcdsaP256PrivateKey> ImportEcdsaP256PrivateKey(
            const PrivateKeyInfo& privateKeyInfo) noexcept
    {
        if (privateKeyInfo.algorithm.algorithm != KeyAlgorithm::EcdsaP256)
        {
            return std::unexpected(InvalidKey());
        }

        return NGIN::Crypto::Asymmetric::EcdsaP256PrivateKey::FromSecretBytes(
                ConstByteSpan {privateKeyInfo.privateKey.data(), privateKeyInfo.privateKey.Size()});
    }

    PrivateKeyInfo ExportPrivateKeyInfo(const NGIN::Crypto::Asymmetric::Ed25519PrivateKey& privateKey)
    {
        return PrivateKeyInfo {
                .version    = 0,
                .algorithm  = MakeKeyAlgorithmIdentifier(KeyAlgorithm::Ed25519),
                .privateKey = CopyBytes(privateKey.Bytes()),
        };
    }

    PrivateKeyInfo ExportPrivateKeyInfo(const NGIN::Crypto::Asymmetric::X25519PrivateKey& privateKey)
    {
        return PrivateKeyInfo {
                .version    = 0,
                .algorithm  = MakeKeyAlgorithmIdentifier(KeyAlgorithm::X25519),
                .privateKey = CopyBytes(privateKey.Bytes()),
        };
    }

    PrivateKeyInfo ExportPrivateKeyInfo(const NGIN::Crypto::Asymmetric::EcdsaP256PrivateKey& privateKey)
    {
        return PrivateKeyInfo {
                .version    = 0,
                .algorithm  = MakeKeyAlgorithmIdentifier(KeyAlgorithm::EcdsaP256),
                .privateKey = CopyBytes(privateKey.Bytes()),
        };
    }
}// namespace NGIN::Crypto::Keys
