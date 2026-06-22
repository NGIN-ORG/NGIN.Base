#include <NGIN/Crypto/Crypto.hpp>

#include <array>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    using NGIN::Byte;
    using NGIN::Crypto::AeadAlgorithm;
    using NGIN::Crypto::ByteBuffer;
    using NGIN::Crypto::ConstByteSpan;
    using NGIN::Crypto::CryptoError;
    using NGIN::Crypto::CryptoExpected;
    using NGIN::Crypto::HashAlgorithm;
    using NGIN::Crypto::KdfAlgorithm;
    using NGIN::Crypto::KeyAgreementAlgorithm;
    using NGIN::Crypto::MacAlgorithm;
    using NGIN::Crypto::MakeByteBuffer;
    using NGIN::Crypto::SignatureAlgorithm;
    using NGIN::Crypto::Backend::CryptoContext;

    [[nodiscard]] ByteBuffer Bytes(std::string_view text)
    {
        auto buffer = MakeByteBuffer(text.size());
        for (NGIN::UIntSize i = 0; i < text.size(); ++i)
        {
            buffer[i] = static_cast<Byte>(text[i]);
        }
        return buffer;
    }

    [[nodiscard]] ByteBuffer Bytes(std::initializer_list<NGIN::UInt32> values)
    {
        auto buffer = MakeByteBuffer(values.size());

        NGIN::UIntSize index = 0;
        for (const auto value: values)
        {
            buffer[index++] = static_cast<Byte>(value);
        }

        return buffer;
    }

    [[nodiscard]] ByteBuffer RepeatedByte(NGIN::UInt8 value, NGIN::UIntSize count)
    {
        auto buffer = MakeByteBuffer(count);
        for (NGIN::UIntSize i = 0; i < count; ++i)
        {
            buffer[i] = static_cast<Byte>(value);
        }
        return buffer;
    }

    [[nodiscard]] ConstByteSpan View(const ByteBuffer& bytes) noexcept
    {
        return ConstByteSpan {bytes.data(), bytes.Size()};
    }

    void Append(ByteBuffer& output, const ByteBuffer& input)
    {
        output.Reserve(output.Size() + input.Size());
        for (const auto byte: input)
        {
            output.PushBack(byte);
        }
    }

    [[nodiscard]] ByteBuffer Concat(std::initializer_list<const ByteBuffer*> buffers)
    {
        ByteBuffer output;
        for (const auto* buffer: buffers)
        {
            Append(output, *buffer);
        }
        return output;
    }

    void PrintFailure(std::string_view label, const CryptoError& error)
    {
        std::cout << label << ": skipped (" << error.Message() << ")\n";
    }

    void PrintSkipped(std::string_view label, std::string_view reason)
    {
        std::cout << label << ": skipped (" << reason << ")\n";
    }

    template<class T>
    [[nodiscard]] CryptoExpected<T> Require(CryptoExpected<T> value)
    {
        if (!value.HasValue())
        {
            return value.Error();
        }

        return std::move(value.Value());
    }

    [[nodiscard]] CryptoExpected<ByteBuffer> UtcTime(std::string_view value)
    {
        auto text = Bytes(value);
        return NGIN::Crypto::Encoding::EncodeDerElement(
                NGIN::Crypto::Encoding::DerTag {
                        .tagClass    = NGIN::Crypto::Encoding::DerTagClass::Universal,
                        .constructed = false,
                        .number      = 23,
                },
                View(text));
    }

    [[nodiscard]] CryptoExpected<ByteBuffer> Utf8String(std::string_view value)
    {
        auto text = Bytes(value);
        return NGIN::Crypto::Encoding::EncodeDerElement(
                NGIN::Crypto::Encoding::DerTag {
                        .tagClass    = NGIN::Crypto::Encoding::DerTagClass::Universal,
                        .constructed = false,
                        .number      = 12,
                },
                View(text));
    }

    [[nodiscard]] CryptoExpected<ByteBuffer> GeneralName(NGIN::UInt32 tagNumber, const ByteBuffer& value)
    {
        return NGIN::Crypto::Encoding::EncodeDerElement(
                NGIN::Crypto::Encoding::DerTag {
                        .tagClass    = NGIN::Crypto::Encoding::DerTagClass::ContextSpecific,
                        .constructed = false,
                        .number      = tagNumber,
                },
                View(value));
    }

    [[nodiscard]] CryptoExpected<ByteBuffer> NameAttribute(
            std::initializer_list<NGIN::UInt32> oidArcs,
            std::string_view                    value)
    {
        auto oid = Require(NGIN::Crypto::Encoding::EncodeDerObjectIdentifier(std::vector<NGIN::UInt32>(oidArcs)));
        if (!oid)
        {
            return oid.Error();
        }

        auto text = Utf8String(value);
        if (!text)
        {
            return text.Error();
        }

        auto children = Concat({&oid.Value(), &text.Value()});
        auto sequence = Require(NGIN::Crypto::Encoding::EncodeDerSequence(View(children)));
        if (!sequence)
        {
            return sequence.Error();
        }

        return NGIN::Crypto::Encoding::EncodeDerSet(View(sequence.Value()));
    }

    [[nodiscard]] CryptoExpected<ByteBuffer> Extension(
            std::initializer_list<NGIN::UInt32> oidArcs,
            const ByteBuffer&                   derValue)
    {
        auto oid = Require(NGIN::Crypto::Encoding::EncodeDerObjectIdentifier(std::vector<NGIN::UInt32>(oidArcs)));
        if (!oid)
        {
            return oid.Error();
        }

        auto value = Require(NGIN::Crypto::Encoding::EncodeDerOctetString(View(derValue)));
        if (!value)
        {
            return value.Error();
        }

        auto children = Concat({&oid.Value(), &value.Value()});
        return NGIN::Crypto::Encoding::EncodeDerSequence(View(children));
    }

    [[nodiscard]] CryptoExpected<ByteBuffer> BuildDemoCertificateDer()
    {
        auto versionInteger = Require(NGIN::Crypto::Encoding::EncodeDerInteger(View(Bytes({0x02}))));
        if (!versionInteger)
        {
            return versionInteger.Error();
        }

        auto version = Require(NGIN::Crypto::Encoding::EncodeDerElement(
                NGIN::Crypto::Encoding::DerTag {
                        .tagClass    = NGIN::Crypto::Encoding::DerTagClass::ContextSpecific,
                        .constructed = true,
                        .number      = 0,
                },
                View(versionInteger.Value())));
        if (!version)
        {
            return version.Error();
        }

        auto serial = Require(NGIN::Crypto::Encoding::EncodeDerInteger(View(Bytes({0x01}))));
        if (!serial)
        {
            return serial.Error();
        }

        auto signatureAlgorithm = Bytes({0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70});
        auto issuerCommonName   = NameAttribute({2, 5, 4, 3}, "Example Issuer");
        auto subjectCommonName  = NameAttribute({2, 5, 4, 3}, "example.com");
        auto subjectOrg         = NameAttribute({2, 5, 4, 10}, "NGIN");
        if (!issuerCommonName || !subjectCommonName || !subjectOrg)
        {
            return CryptoError {NGIN::Crypto::CryptoErrorCode::ParseError};
        }

        auto issuerName      = NGIN::Crypto::Encoding::EncodeDerSequence(View(issuerCommonName.Value()));
        auto subjectChildren = Concat({&subjectCommonName.Value(), &subjectOrg.Value()});
        auto subjectName     = NGIN::Crypto::Encoding::EncodeDerSequence(View(subjectChildren));
        if (!issuerName || !subjectName)
        {
            return CryptoError {NGIN::Crypto::CryptoErrorCode::ParseError};
        }

        auto notBefore = UtcTime("260101000000Z");
        auto notAfter  = UtcTime("270101000000Z");
        if (!notBefore || !notAfter)
        {
            return CryptoError {NGIN::Crypto::CryptoErrorCode::ParseError};
        }

        auto validityChildren = Concat({&notBefore.Value(), &notAfter.Value()});
        auto validity         = NGIN::Crypto::Encoding::EncodeDerSequence(View(validityChildren));
        if (!validity)
        {
            return validity.Error();
        }

        auto publicKeyBytes = RepeatedByte(0x11, 32);
        auto spki           = NGIN::Crypto::Keys::WriteSubjectPublicKeyInfo(
                NGIN::Crypto::Keys::KeyAlgorithm::Ed25519,
                View(publicKeyBytes));
        if (!spki)
        {
            return spki.Error();
        }

        auto dns   = GeneralName(2, Bytes("example.com"));
        auto email = GeneralName(1, Bytes("admin@example.com"));
        auto ip    = GeneralName(7, Bytes({127, 0, 0, 1}));
        if (!dns || !email || !ip)
        {
            return CryptoError {NGIN::Crypto::CryptoErrorCode::ParseError};
        }

        auto sanChildren = Concat({&dns.Value(), &email.Value(), &ip.Value()});
        auto sanSequence = NGIN::Crypto::Encoding::EncodeDerSequence(View(sanChildren));
        if (!sanSequence)
        {
            return sanSequence.Error();
        }

        auto sanExtension = Extension({2, 5, 29, 17}, sanSequence.Value());
        if (!sanExtension)
        {
            return sanExtension.Error();
        }

        auto extensionsSequence = NGIN::Crypto::Encoding::EncodeDerSequence(View(sanExtension.Value()));
        if (!extensionsSequence)
        {
            return extensionsSequence.Error();
        }

        auto extensions = NGIN::Crypto::Encoding::EncodeDerElement(
                NGIN::Crypto::Encoding::DerTag {
                        .tagClass    = NGIN::Crypto::Encoding::DerTagClass::ContextSpecific,
                        .constructed = true,
                        .number      = 3,
                },
                View(extensionsSequence.Value()));
        if (!extensions)
        {
            return extensions.Error();
        }

        auto tbsChildren = Concat(
                {&version.Value(),
                 &serial.Value(),
                 &signatureAlgorithm,
                 &issuerName.Value(),
                 &validity.Value(),
                 &subjectName.Value(),
                 &spki.Value(),
                 &extensions.Value()});
        auto tbs = NGIN::Crypto::Encoding::EncodeDerSequence(View(tbsChildren));
        if (!tbs)
        {
            return tbs.Error();
        }

        auto signature = NGIN::Crypto::Encoding::EncodeDerBitString(0, View(RepeatedByte(0x44, 64)));
        if (!signature)
        {
            return signature.Error();
        }

        auto certificateChildren = Concat({&tbs.Value(), &signatureAlgorithm, &signature.Value()});
        return NGIN::Crypto::Encoding::EncodeDerSequence(View(certificateChildren));
    }

    [[nodiscard]] std::string WrapPem(std::string_view label, ConstByteSpan der)
    {
        auto encoded = NGIN::Crypto::Encoding::EncodeBase64(der);
        if (!encoded)
        {
            return {};
        }

        std::string pem;
        pem.reserve(encoded.Value().size() + label.size() * 2 + 40);
        pem += "-----BEGIN ";
        pem += label;
        pem += "-----\n";
        for (NGIN::UIntSize offset = 0; offset < encoded.Value().size(); offset += 64)
        {
            pem.append(encoded.Value(), offset, 64);
            pem.push_back('\n');
        }
        pem += "-----END ";
        pem += label;
        pem += "-----\n";
        return pem;
    }

    void RunHashExample(const CryptoContext& context)
    {
        auto digest = NGIN::Crypto::Hashing::Sha256(context, View(Bytes("message to hash")));
        if (!digest)
        {
            PrintFailure("SHA-256", digest.Error());
            return;
        }

        std::cout << "SHA-256: " << digest.Value().size() << " bytes\n";
    }

    void RunAeadExample(const CryptoContext& context)
    {
        if (!context.Supports(AeadAlgorithm::Aes256Gcm))
        {
            PrintSkipped("AES-256-GCM", context.DescribeSupport(AeadAlgorithm::Aes256Gcm).reason);
            return;
        }

        auto key   = NGIN::Crypto::Symmetric::GenerateAes256GcmKey();
        auto nonce = NGIN::Crypto::Symmetric::GenerateAesGcmNonce();
        if (!key || !nonce)
        {
            PrintSkipped("AES-256-GCM", "secure random unavailable");
            return;
        }

        auto plaintext      = Bytes("authenticated plaintext");
        auto associatedData = Bytes("metadata");
        auto sealed         = NGIN::Crypto::Symmetric::SealAes256Gcm(
                context,
                key.Value(),
                nonce.Value(),
                View(plaintext),
                View(associatedData));
        if (!sealed)
        {
            PrintFailure("AES-256-GCM seal", sealed.Error());
            return;
        }

        auto opened = NGIN::Crypto::Symmetric::OpenAes256Gcm(
                context,
                key.Value(),
                nonce.Value(),
                View(sealed.Value().ciphertext),
                sealed.Value().tag,
                View(associatedData));
        if (!opened)
        {
            PrintFailure("AES-256-GCM open", opened.Error());
            return;
        }

        std::cout << "AES-256-GCM: sealed " << sealed.Value().ciphertext.Size() << " bytes and verified tag\n";
    }

    void RunEd25519Example(const CryptoContext& context)
    {
        if (!context.Supports(SignatureAlgorithm::Ed25519))
        {
            PrintSkipped("Ed25519", context.DescribeSupport(SignatureAlgorithm::Ed25519).reason);
            return;
        }

        auto keyPair = NGIN::Crypto::Asymmetric::GenerateEd25519KeyPair(context);
        if (!keyPair)
        {
            PrintFailure("Ed25519 key generation", keyPair.Error());
            return;
        }

        auto message   = Bytes("signed message");
        auto signature = NGIN::Crypto::Asymmetric::SignEd25519(context, keyPair.Value().privateKey, View(message));
        if (!signature)
        {
            PrintFailure("Ed25519 sign", signature.Error());
            return;
        }

        auto verified = NGIN::Crypto::Asymmetric::VerifyEd25519(
                context,
                keyPair.Value().publicKey,
                View(message),
                signature.Value());
        if (!verified)
        {
            PrintFailure("Ed25519 verify", verified.Error());
            return;
        }

        std::cout << "Ed25519: signed and verified " << message.Size() << " bytes\n";
    }

    void RunX25519Example(const CryptoContext& context)
    {
        if (!context.Supports(KeyAgreementAlgorithm::X25519) || !context.Supports(KdfAlgorithm::HkdfSha256))
        {
            PrintSkipped("X25519 + HKDF-SHA256", "key agreement or HKDF-SHA256 unsupported by selected backend");
            return;
        }

        auto alice = NGIN::Crypto::Asymmetric::GenerateX25519KeyPair(context);
        auto bob   = NGIN::Crypto::Asymmetric::GenerateX25519KeyPair(context);
        if (!alice || !bob)
        {
            PrintSkipped("X25519 + HKDF-SHA256", "key generation failed");
            return;
        }

        auto shared = NGIN::Crypto::Asymmetric::DeriveX25519SharedSecret(
                context,
                alice.Value().privateKey,
                bob.Value().publicKey);
        if (!shared)
        {
            PrintFailure("X25519 shared secret", shared.Error());
            return;
        }

        auto salt = Bytes("example salt");
        auto info = Bytes("ngin example key");
        auto key  = NGIN::Crypto::Kdf::HkdfSha256Secret<32>(
                context,
                NGIN::Crypto::Kdf::HkdfParameters {
                         .inputKeyMaterial = NGIN::Crypto::Memory::SecretView {shared.Value().Bytes()},
                         .salt             = View(salt),
                         .info             = View(info),
                });
        if (!key)
        {
            PrintFailure("HKDF-SHA256", key.Error());
            return;
        }

        std::cout << "X25519 + HKDF-SHA256: derived " << key.Value().Bytes().size() << " byte secret\n";
    }

    void RunCertificateExample()
    {
        auto der = BuildDemoCertificateDer();
        if (!der)
        {
            PrintFailure("PEM certificate fixture", der.Error());
            return;
        }

        auto pem    = WrapPem("CERTIFICATE", View(der.Value()));
        auto blocks = NGIN::Crypto::Encoding::ParsePem(
                pem,
                NGIN::Crypto::Encoding::PemParseOptions {
                        .allowedLabels       = {"CERTIFICATE"},
                        .maxDecodedBytes     = 4096,
                        .allowMultipleBlocks = false,
                });
        if (!blocks)
        {
            PrintFailure("PEM parse", blocks.Error());
            return;
        }

        auto certificate = NGIN::Crypto::Certificates::ParseX509Certificate(View(blocks.Value()[0].decoded));
        if (!certificate)
        {
            PrintFailure("X.509 parse", certificate.Error());
            return;
        }

        std::cout << "PEM/X.509: subjectAltName DNS count " << certificate.Value().subjectAltNames.dnsNames.Size();
        if (certificate.Value().hasSubjectAltNames && certificate.Value().subjectAltNames.dnsNames.Size() > 0)
        {
            std::cout << " (" << certificate.Value().subjectAltNames.dnsNames[0] << ')';
        }
        std::cout << '\n';
    }

    void RunJwtExample(const CryptoContext& context)
    {
        constexpr std::string_view token {
                "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
                "eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiaWF0IjoxNTE2MjM5MDIyfQ."
                "SflKxwRJSMeKKF2QT4fwpMeJf36POk6yJV_adQssw5c"};

        if (!context.Supports(MacAlgorithm::HmacSha256))
        {
            PrintSkipped("JWT HS256 validation", context.DescribeSupport(MacAlgorithm::HmacSha256).reason);
            return;
        }

        auto secret = Bytes("your-256-bit-secret");
        auto result = NGIN::Crypto::Tokens::ValidateJwt(
                context,
                token,
                NGIN::Crypto::Tokens::JwtValidationKey {
                        .algorithm = NGIN::Crypto::Tokens::JwtAlgorithm::Hs256,
                        .hmacKey   = NGIN::Crypto::Memory::SecretView {View(secret)},
                        .publicKey = {},
                },
                NGIN::Crypto::Tokens::JwtValidationPolicy {
                        .allowHs256              = true,
                        .allowEdDsa              = false,
                        .expectedIssuer          = {},
                        .expectedAudience        = {},
                        .currentUnixTimeSeconds  = 0,
                        .allowedClockSkewSeconds = 0,
                        .requireExpiration       = false,
                        .validateExpiration      = true,
                        .validateNotBefore       = true,
                        .requiredClaims          = {},
                        .parseOptions            = {},
                });
        if (!result)
        {
            PrintFailure("JWT HS256 validation", result.Error());
            return;
        }

        std::cout << "JWT HS256 validation: subject " << result.Value().claims.subject << '\n';
    }
}// namespace

int main()
{
    auto context = NGIN::Crypto::Backend::CreateBestAvailableContext();
    if (!context)
    {
        std::cerr << "Crypto backend selection failed: " << context.Error().Message() << '\n';
        return 1;
    }

    std::cout << "Selected backend: " << context.Value().Info().Name() << '\n';

    auto token = NGIN::Crypto::Tokens::GenerateBase64Url(
            context.Value(),
            NGIN::Crypto::Tokens::TokenOptions {
                    .byteLength          = 16,
                    .minimumEntropyBytes = 16,
                    .encoding            = NGIN::Crypto::Tokens::TokenEncoding::Base64Url,
            });
    if (token)
    {
        std::cout << "Random token: " << token.Value().Value() << '\n';
    }
    else
    {
        PrintFailure("Random token", token.Error());
    }

    RunHashExample(context.Value());
    RunAeadExample(context.Value());
    RunEd25519Example(context.Value());
    RunX25519Example(context.Value());
    RunCertificateExample();
    RunJwtExample(context.Value());

    return 0;
}
