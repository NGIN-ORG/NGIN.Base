#include <NGIN/Crypto/Keys/PrivateKeyInfo.hpp>
#include <NGIN/Crypto/Keys/SubjectPublicKeyInfo.hpp>

#include <NGIN/Crypto/Encoding/Der.hpp>
#include <NGIN/Crypto/Errors/CryptoError.hpp>

#include <array>
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
            for (auto byte: bytes)
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

        [[nodiscard]] CryptoExpected<DerElement> ReadSingleElement(ConstByteSpan der) noexcept
        {
            DerReader reader {der};
            auto      element = reader.ReadElement();
            if (!element.HasValue())
            {
                return element.Error();
            }
            if (!reader.IsAtEnd())
            {
                return ParseError();
            }

            return element.Value();
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
                    return ParseError();
                }
                return KeyAlgorithm::Ed25519;
            }
            if (OidEquals(oid, X25519_OID))
            {
                if (hasParameters)
                {
                    return ParseError();
                }
                return KeyAlgorithm::X25519;
            }
            if (OidEquals(oid, EC_PUBLIC_KEY_OID))
            {
                if (!hasParameters)
                {
                    return ParseError();
                }

                auto parameterElement = ReadSingleElement(parameters);
                if (!parameterElement.HasValue())
                {
                    return parameterElement.Error();
                }

                auto curveOid = NGIN::Crypto::Encoding::ReadDerObjectIdentifier(parameterElement.Value());
                if (!curveOid.HasValue())
                {
                    return curveOid.Error();
                }

                return OidEquals(curveOid.Value(), SECP256R1_OID) ? CryptoExpected<KeyAlgorithm> {KeyAlgorithm::EcdsaP256}
                                                                  : CryptoExpected<KeyAlgorithm> {KeyAlgorithm::Unknown};
            }
            if (OidEquals(oid, RSA_ENCRYPTION_OID))
            {
                if (hasParameters)
                {
                    auto parameterElement = ReadSingleElement(parameters);
                    if (!parameterElement.HasValue())
                    {
                        return parameterElement.Error();
                    }
                    if (!IsDerNull(parameterElement.Value()))
                    {
                        return ParseError();
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

        [[nodiscard]] CryptoExpected<KeyAlgorithmIdentifier> ParseAlgorithmIdentifierElement(const DerElement& element)
        {
            DerReader parent {element.encoded};
            auto      reader = NGIN::Crypto::Encoding::ReadDerSequence(parent, element);
            if (!reader.HasValue())
            {
                return reader.Error();
            }

            auto oidElement = reader.Value().ReadElement();
            if (!oidElement.HasValue())
            {
                return oidElement.Error();
            }

            auto oid = NGIN::Crypto::Encoding::ReadDerObjectIdentifier(oidElement.Value());
            if (!oid.HasValue())
            {
                return oid.Error();
            }

            KeyAlgorithmIdentifier identifier {
                    .objectIdentifier = std::move(oid.Value()),
                    .parameters       = {},
                    .hasParameters    = false,
            };

            if (!reader.Value().IsAtEnd())
            {
                auto parameters = reader.Value().ReadElement();
                if (!parameters.HasValue())
                {
                    return parameters.Error();
                }
                if (!reader.Value().IsAtEnd())
                {
                    return ParseError();
                }

                identifier.hasParameters = true;
                identifier.parameters    = CopyBytes(parameters.Value().encoded);
            }

            auto algorithm = IdentifyAlgorithm(
                    identifier.objectIdentifier,
                    identifier.hasParameters,
                    ConstByteSpan {identifier.parameters.data(), identifier.parameters.Size()});
            if (!algorithm.HasValue())
            {
                return algorithm.Error();
            }
            identifier.algorithm = algorithm.Value();

            return identifier;
        }

        [[nodiscard]] CryptoExpected<EncryptedPrivateKeyAlgorithmIdentifier> ParseRawAlgorithmIdentifierElement(
                const DerElement& element)
        {
            DerReader parent {element.encoded};
            auto      reader = NGIN::Crypto::Encoding::ReadDerSequence(parent, element);
            if (!reader.HasValue())
            {
                return reader.Error();
            }

            auto oidElement = reader.Value().ReadElement();
            if (!oidElement.HasValue())
            {
                return oidElement.Error();
            }

            auto oid = NGIN::Crypto::Encoding::ReadDerObjectIdentifier(oidElement.Value());
            if (!oid.HasValue())
            {
                return oid.Error();
            }

            EncryptedPrivateKeyAlgorithmIdentifier identifier {
                    .objectIdentifier = std::move(oid.Value()),
                    .parameters       = {},
                    .hasParameters    = false,
            };

            if (!reader.Value().IsAtEnd())
            {
                auto parameters = reader.Value().ReadElement();
                if (!parameters.HasValue())
                {
                    return parameters.Error();
                }
                if (!reader.Value().IsAtEnd())
                {
                    return ParseError();
                }

                identifier.hasParameters = true;
                identifier.parameters    = CopyBytes(parameters.Value().encoded);
            }

            return identifier;
        }

        [[nodiscard]] CryptoExpected<ByteBuffer> EncodeRawAlgorithmIdentifier(
                const EncryptedPrivateKeyAlgorithmIdentifier& algorithm)
        {
            ByteBuffer children;

            auto oid = NGIN::Crypto::Encoding::EncodeDerObjectIdentifier(
                    std::span<const NGIN::UInt32> {algorithm.objectIdentifier.begin(), algorithm.objectIdentifier.Size()});
            if (!oid.HasValue())
            {
                return oid.Error();
            }
            AppendBytes(children, ConstByteSpan {oid.Value().data(), oid.Value().Size()});

            if (algorithm.hasParameters)
            {
                auto parametersElement = ReadSingleElement(
                        ConstByteSpan {algorithm.parameters.data(), algorithm.parameters.Size()});
                if (!parametersElement.HasValue())
                {
                    return parametersElement.Error();
                }
                AppendBytes(children, ConstByteSpan {algorithm.parameters.data(), algorithm.parameters.Size()});
            }
            else if (algorithm.parameters.Size() != 0)
            {
                return InvalidArgument();
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
                if (!oid.HasValue())
                {
                    return oid.Error();
                }
                append(oid.Value());
                return NGIN::Crypto::Encoding::EncodeDerSequence(ConstByteSpan {children.data(), children.Size()});
            }

            if (algorithm == KeyAlgorithm::X25519)
            {
                auto oid = NGIN::Crypto::Encoding::EncodeDerObjectIdentifier(X25519_OID);
                if (!oid.HasValue())
                {
                    return oid.Error();
                }
                append(oid.Value());
                return NGIN::Crypto::Encoding::EncodeDerSequence(ConstByteSpan {children.data(), children.Size()});
            }

            if (algorithm == KeyAlgorithm::EcdsaP256)
            {
                auto oid = NGIN::Crypto::Encoding::EncodeDerObjectIdentifier(EC_PUBLIC_KEY_OID);
                if (!oid.HasValue())
                {
                    return oid.Error();
                }
                auto curve = NGIN::Crypto::Encoding::EncodeDerObjectIdentifier(SECP256R1_OID);
                if (!curve.HasValue())
                {
                    return curve.Error();
                }
                append(oid.Value());
                append(curve.Value());
                return NGIN::Crypto::Encoding::EncodeDerSequence(ConstByteSpan {children.data(), children.Size()});
            }

            if (algorithm == KeyAlgorithm::Rsa)
            {
                auto oid = NGIN::Crypto::Encoding::EncodeDerObjectIdentifier(RSA_ENCRYPTION_OID);
                if (!oid.HasValue())
                {
                    return oid.Error();
                }
                auto nullParameters = NGIN::Crypto::Encoding::EncodeDerElement(
                        NGIN::Crypto::Encoding::MakeDerUniversalTag(DerUniversalTag::Null),
                        ConstByteSpan {});
                if (!nullParameters.HasValue())
                {
                    return nullParameters.Error();
                }
                append(oid.Value());
                append(nullParameters.Value());
                return NGIN::Crypto::Encoding::EncodeDerSequence(ConstByteSpan {children.data(), children.Size()});
            }

            return InvalidArgument();
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
                return UnsupportedAlgorithm();
        }

        return UnsupportedAlgorithm();
    }

    CryptoExpected<KeyAgreementAlgorithm> ToKeyAgreementAlgorithm(KeyAlgorithm algorithm) noexcept
    {
        if (algorithm == KeyAlgorithm::X25519)
        {
            return KeyAgreementAlgorithm::X25519;
        }

        return UnsupportedAlgorithm();
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

        return UnsupportedAlgorithm();
    }

    CryptoExpected<KeyAlgorithm> FromKeyAgreementAlgorithm(KeyAgreementAlgorithm algorithm) noexcept
    {
        switch (algorithm)
        {
            case KeyAgreementAlgorithm::X25519:
                return KeyAlgorithm::X25519;
        }

        return UnsupportedAlgorithm();
    }

    CryptoExpected<SubjectPublicKeyInfo> ParseSubjectPublicKeyInfo(ConstByteSpan der)
    {
        DerReader reader {der};
        auto      top = reader.ReadElement();
        if (!top.HasValue())
        {
            return top.Error();
        }
        if (!reader.IsAtEnd())
        {
            return ParseError();
        }

        auto sequence = NGIN::Crypto::Encoding::ReadDerSequence(reader, top.Value());
        if (!sequence.HasValue())
        {
            return sequence.Error();
        }

        auto algorithmElement = sequence.Value().ReadElement();
        if (!algorithmElement.HasValue())
        {
            return algorithmElement.Error();
        }

        auto algorithm = ParseAlgorithmIdentifierElement(algorithmElement.Value());
        if (!algorithm.HasValue())
        {
            return algorithm.Error();
        }

        auto publicKeyElement = sequence.Value().ReadElement();
        if (!publicKeyElement.HasValue())
        {
            return publicKeyElement.Error();
        }
        if (!sequence.Value().IsAtEnd())
        {
            return ParseError();
        }

        auto bitString = NGIN::Crypto::Encoding::ReadDerBitString(publicKeyElement.Value());
        if (!bitString.HasValue())
        {
            return bitString.Error();
        }
        if (bitString.Value().unusedBitCount != 0)
        {
            return ParseError();
        }

        return SubjectPublicKeyInfo {
                .algorithm = std::move(algorithm.Value()),
                .publicKey = CopyBytes(bitString.Value().bytes),
        };
    }

    CryptoExpected<ByteBuffer> WriteSubjectPublicKeyInfo(KeyAlgorithm algorithm, ConstByteSpan publicKey)
    {
        auto algorithmIdentifier = EncodeAlgorithmIdentifier(algorithm);
        if (!algorithmIdentifier.HasValue())
        {
            return algorithmIdentifier.Error();
        }

        auto publicKeyBits = NGIN::Crypto::Encoding::EncodeDerBitString(0, publicKey);
        if (!publicKeyBits.HasValue())
        {
            return publicKeyBits.Error();
        }

        return EncodeSequenceFromChildren(
                ConstByteSpan {algorithmIdentifier.Value().data(), algorithmIdentifier.Value().Size()},
                ConstByteSpan {publicKeyBits.Value().data(), publicKeyBits.Value().Size()});
    }

    CryptoExpected<NGIN::Crypto::Asymmetric::Ed25519PublicKey> ImportEd25519PublicKey(
            const SubjectPublicKeyInfo& publicKeyInfo) noexcept
    {
        if (publicKeyInfo.algorithm.algorithm != KeyAlgorithm::Ed25519)
        {
            return InvalidKey();
        }

        return NGIN::Crypto::Asymmetric::Ed25519PublicKey::FromBytes(
                ConstByteSpan {publicKeyInfo.publicKey.data(), publicKeyInfo.publicKey.Size()});
    }

    CryptoExpected<NGIN::Crypto::Asymmetric::X25519PublicKey> ImportX25519PublicKey(
            const SubjectPublicKeyInfo& publicKeyInfo) noexcept
    {
        if (publicKeyInfo.algorithm.algorithm != KeyAlgorithm::X25519)
        {
            return InvalidKey();
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
            return InvalidKey();
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
        if (!top.HasValue())
        {
            return top.Error();
        }
        if (!reader.IsAtEnd())
        {
            return ParseError();
        }

        auto sequence = NGIN::Crypto::Encoding::ReadDerSequence(reader, top.Value());
        if (!sequence.HasValue())
        {
            return sequence.Error();
        }

        auto versionElement = sequence.Value().ReadElement();
        if (!versionElement.HasValue())
        {
            return versionElement.Error();
        }
        auto version = NGIN::Crypto::Encoding::ReadDerInteger(versionElement.Value());
        if (!version.HasValue())
        {
            return version.Error();
        }
        if (version.Value().size() != 1 || version.Value()[0] != NGIN::Byte {0})
        {
            return ParseError();
        }

        auto algorithmElement = sequence.Value().ReadElement();
        if (!algorithmElement.HasValue())
        {
            return algorithmElement.Error();
        }
        auto algorithm = ParseAlgorithmIdentifierElement(algorithmElement.Value());
        if (!algorithm.HasValue())
        {
            return algorithm.Error();
        }

        auto privateKeyElement = sequence.Value().ReadElement();
        if (!privateKeyElement.HasValue())
        {
            return privateKeyElement.Error();
        }
        auto privateKey = NGIN::Crypto::Encoding::ReadDerOctetString(privateKeyElement.Value());
        if (!privateKey.HasValue())
        {
            return privateKey.Error();
        }
        if (!sequence.Value().IsAtEnd())
        {
            return ParseError();
        }

        return PrivateKeyInfo {
                .version    = 0,
                .algorithm  = std::move(algorithm.Value()),
                .privateKey = CopyBytes(privateKey.Value()),
        };
    }

    CryptoExpected<ByteBuffer> WritePrivateKeyInfo(KeyAlgorithm algorithm, ConstByteSpan privateKey)
    {
        const std::array<NGIN::Byte, 1> versionValue {NGIN::Byte {0}};
        auto                            version = NGIN::Crypto::Encoding::EncodeDerInteger(versionValue);
        if (!version.HasValue())
        {
            return version.Error();
        }

        auto algorithmIdentifier = EncodeAlgorithmIdentifier(algorithm);
        if (!algorithmIdentifier.HasValue())
        {
            return algorithmIdentifier.Error();
        }

        auto privateKeyOctets = NGIN::Crypto::Encoding::EncodeDerOctetString(privateKey);
        if (!privateKeyOctets.HasValue())
        {
            return privateKeyOctets.Error();
        }

        return EncodeSequenceFromChildren(
                ConstByteSpan {version.Value().data(), version.Value().Size()},
                ConstByteSpan {algorithmIdentifier.Value().data(), algorithmIdentifier.Value().Size()},
                ConstByteSpan {privateKeyOctets.Value().data(), privateKeyOctets.Value().Size()});
    }

    CryptoExpected<EncryptedPrivateKeyInfo> ParseEncryptedPrivateKeyInfo(ConstByteSpan der)
    {
        DerReader reader {der};
        auto      top = reader.ReadElement();
        if (!top.HasValue())
        {
            return top.Error();
        }
        if (!reader.IsAtEnd())
        {
            return ParseError();
        }

        auto sequence = NGIN::Crypto::Encoding::ReadDerSequence(reader, top.Value());
        if (!sequence.HasValue())
        {
            return sequence.Error();
        }

        auto algorithmElement = sequence.Value().ReadElement();
        if (!algorithmElement.HasValue())
        {
            return algorithmElement.Error();
        }
        auto algorithm = ParseRawAlgorithmIdentifierElement(algorithmElement.Value());
        if (!algorithm.HasValue())
        {
            return algorithm.Error();
        }

        auto encryptedDataElement = sequence.Value().ReadElement();
        if (!encryptedDataElement.HasValue())
        {
            return encryptedDataElement.Error();
        }
        auto encryptedData = NGIN::Crypto::Encoding::ReadDerOctetString(encryptedDataElement.Value());
        if (!encryptedData.HasValue())
        {
            return encryptedData.Error();
        }
        if (!sequence.Value().IsAtEnd())
        {
            return ParseError();
        }

        return EncryptedPrivateKeyInfo {
                .encryptionAlgorithm = std::move(algorithm.Value()),
                .encryptedData        = CopyBytes(encryptedData.Value()),
        };
    }

    CryptoExpected<ByteBuffer> WriteEncryptedPrivateKeyInfo(
            const EncryptedPrivateKeyAlgorithmIdentifier& encryptionAlgorithm,
            ConstByteSpan                                 encryptedData)
    {
        auto algorithmIdentifier = EncodeRawAlgorithmIdentifier(encryptionAlgorithm);
        if (!algorithmIdentifier.HasValue())
        {
            return algorithmIdentifier.Error();
        }

        auto encryptedDataOctets = NGIN::Crypto::Encoding::EncodeDerOctetString(encryptedData);
        if (!encryptedDataOctets.HasValue())
        {
            return encryptedDataOctets.Error();
        }

        return EncodeSequenceFromChildren(
                ConstByteSpan {algorithmIdentifier.Value().data(), algorithmIdentifier.Value().Size()},
                ConstByteSpan {encryptedDataOctets.Value().data(), encryptedDataOctets.Value().Size()});
    }

    CryptoExpected<NGIN::Crypto::Asymmetric::Ed25519PrivateKey> ImportEd25519PrivateKey(
            const PrivateKeyInfo& privateKeyInfo) noexcept
    {
        if (privateKeyInfo.algorithm.algorithm != KeyAlgorithm::Ed25519)
        {
            return InvalidKey();
        }

        return NGIN::Crypto::Asymmetric::Ed25519PrivateKey::FromSecretBytes(
                ConstByteSpan {privateKeyInfo.privateKey.data(), privateKeyInfo.privateKey.Size()});
    }

    CryptoExpected<NGIN::Crypto::Asymmetric::X25519PrivateKey> ImportX25519PrivateKey(
            const PrivateKeyInfo& privateKeyInfo) noexcept
    {
        if (privateKeyInfo.algorithm.algorithm != KeyAlgorithm::X25519)
        {
            return InvalidKey();
        }

        return NGIN::Crypto::Asymmetric::X25519PrivateKey::FromSecretBytes(
                ConstByteSpan {privateKeyInfo.privateKey.data(), privateKeyInfo.privateKey.Size()});
    }

    CryptoExpected<NGIN::Crypto::Asymmetric::EcdsaP256PrivateKey> ImportEcdsaP256PrivateKey(
            const PrivateKeyInfo& privateKeyInfo) noexcept
    {
        if (privateKeyInfo.algorithm.algorithm != KeyAlgorithm::EcdsaP256)
        {
            return InvalidKey();
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
