#include <NGIN/Crypto/Certificates/CertificateStore.hpp>
#include <NGIN/Crypto/Certificates/X509.hpp>
#include <NGIN/Crypto/Encoding/Der.hpp>
#include <NGIN/Crypto/Keys/SubjectPublicKeyInfo.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <fstream>
#include <initializer_list>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    [[nodiscard]] NGIN::Crypto::ByteBuffer Bytes(std::initializer_list<NGIN::UInt32> values)
    {
        auto buffer = NGIN::Crypto::MakeByteBuffer(values.size());

        NGIN::UIntSize index = 0;
        for (auto value: values)
        {
            buffer[index++] = static_cast<NGIN::Byte>(value);
        }

        return buffer;
    }

    [[nodiscard]] NGIN::Crypto::ByteBuffer Text(std::string_view text)
    {
        auto buffer = NGIN::Crypto::MakeByteBuffer(text.size());
        for (NGIN::UIntSize i = 0; i < text.size(); ++i)
        {
            buffer[i] = static_cast<NGIN::Byte>(text[i]);
        }
        return buffer;
    }

    [[nodiscard]] NGIN::Crypto::ByteBuffer RepeatedByte(NGIN::UInt8 value, NGIN::UIntSize count)
    {
        auto buffer = NGIN::Crypto::MakeByteBuffer(count);
        for (NGIN::UIntSize i = 0; i < count; ++i)
        {
            buffer[i] = static_cast<NGIN::Byte>(value);
        }
        return buffer;
    }

    void Append(NGIN::Crypto::ByteBuffer& output, const NGIN::Crypto::ByteBuffer& input)
    {
        for (NGIN::Byte byte: input)
        {
            output.PushBack(byte);
        }
    }

    [[nodiscard]] NGIN::Crypto::ByteBuffer Concat(std::initializer_list<const NGIN::Crypto::ByteBuffer*> buffers)
    {
        NGIN::Crypto::ByteBuffer output;
        for (const auto* buffer: buffers)
        {
            output.Reserve(output.Size() + buffer->Size());
            Append(output, *buffer);
        }
        return output;
    }

    [[nodiscard]] NGIN::Crypto::ByteBuffer RequireValue(NGIN::Crypto::CryptoExpected<NGIN::Crypto::ByteBuffer> value)
    {
        REQUIRE(value.has_value());
        return std::move(value.value());
    }

    [[nodiscard]] NGIN::Crypto::ConstByteSpan View(const NGIN::Crypto::ByteBuffer& bytes)
    {
        return NGIN::Crypto::ConstByteSpan {bytes.data(), bytes.Size()};
    }

    [[nodiscard]] NGIN::Crypto::ByteBuffer Ed25519AlgorithmIdentifier()
    {
        return Bytes({0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70});
    }

    [[nodiscard]] bool HasReadableLinuxCaBundle()
    {
#if defined(__linux__)
        for (std::string_view path: {
                     "/etc/ssl/certs/ca-certificates.crt",
                     "/etc/pki/tls/certs/ca-bundle.crt",
                     "/etc/ssl/ca-bundle.pem",
                     "/etc/ssl/cert.pem",
             })
        {
            std::ifstream input {std::string {path}, std::ios::binary};
            if (input)
            {
                return true;
            }
        }
#endif
        return false;
    }

    [[nodiscard]] NGIN::Crypto::ByteBuffer UtcTime(std::string_view value)
    {
        const auto text = Text(value);
        return RequireValue(NGIN::Crypto::Encoding::EncodeDerElement(
                NGIN::Crypto::Encoding::DerTag {
                        .tagClass    = NGIN::Crypto::Encoding::DerTagClass::Universal,
                        .constructed = false,
                        .number      = 23,
                },
                View(text)));
    }

    [[nodiscard]] NGIN::Crypto::ByteBuffer GeneralName(NGIN::UInt32 tagNumber, const NGIN::Crypto::ByteBuffer& value)
    {
        return RequireValue(NGIN::Crypto::Encoding::EncodeDerElement(
                NGIN::Crypto::Encoding::DerTag {
                        .tagClass    = NGIN::Crypto::Encoding::DerTagClass::ContextSpecific,
                        .constructed = false,
                        .number      = tagNumber,
                },
                View(value)));
    }

    [[nodiscard]] NGIN::Crypto::ByteBuffer Utf8String(std::string_view value)
    {
        const auto text = Text(value);
        return RequireValue(NGIN::Crypto::Encoding::EncodeDerElement(
                NGIN::Crypto::Encoding::DerTag {
                        .tagClass    = NGIN::Crypto::Encoding::DerTagClass::Universal,
                        .constructed = false,
                        .number      = 12,
                },
                View(text)));
    }

    [[nodiscard]] NGIN::Crypto::ByteBuffer NameAttribute(
            std::initializer_list<NGIN::UInt32> oidArcs,
            std::string_view                    value)
    {
        const auto oid      = RequireValue(NGIN::Crypto::Encoding::EncodeDerObjectIdentifier(std::vector<NGIN::UInt32>(oidArcs)));
        const auto text     = Utf8String(value);
        const auto children = Concat({&oid, &text});
        const auto sequence = RequireValue(NGIN::Crypto::Encoding::EncodeDerSequence(View(children)));
        return RequireValue(NGIN::Crypto::Encoding::EncodeDerSet(View(sequence)));
    }

    [[nodiscard]] NGIN::Crypto::ByteBuffer Name(std::initializer_list<const NGIN::Crypto::ByteBuffer*> attributes)
    {
        const auto children = Concat(attributes);
        return RequireValue(NGIN::Crypto::Encoding::EncodeDerSequence(View(children)));
    }

    [[nodiscard]] NGIN::Crypto::ByteBuffer Extension(
            std::initializer_list<NGIN::UInt32> oidArcs, const NGIN::Crypto::ByteBuffer& derValue)
    {
        const auto oid      = RequireValue(NGIN::Crypto::Encoding::EncodeDerObjectIdentifier(std::vector<NGIN::UInt32>(oidArcs)));
        const auto value    = RequireValue(NGIN::Crypto::Encoding::EncodeDerOctetString(View(derValue)));
        const auto children = Concat({&oid, &value});
        return RequireValue(NGIN::Crypto::Encoding::EncodeDerSequence(View(children)));
    }

    [[nodiscard]] NGIN::Crypto::ByteBuffer ExtensionWithCritical(
            std::initializer_list<NGIN::UInt32> oidArcs, bool critical, const NGIN::Crypto::ByteBuffer& derValue)
    {
        const auto oid             = RequireValue(NGIN::Crypto::Encoding::EncodeDerObjectIdentifier(std::vector<NGIN::UInt32>(oidArcs)));
        const auto criticalValue   = Bytes({critical ? 0xffu : 0x00u});
        const auto criticalBoolean = RequireValue(NGIN::Crypto::Encoding::EncodeDerElement(
                NGIN::Crypto::Encoding::DerTag {
                        .tagClass    = NGIN::Crypto::Encoding::DerTagClass::Universal,
                        .constructed = false,
                        .number      = 1,
                },
                View(criticalValue)));
        const auto value           = RequireValue(NGIN::Crypto::Encoding::EncodeDerOctetString(View(derValue)));
        const auto children        = Concat({&oid, &criticalBoolean, &value});
        return RequireValue(NGIN::Crypto::Encoding::EncodeDerSequence(View(children)));
    }

    [[nodiscard]] NGIN::Crypto::ByteBuffer DerBoolean(bool value)
    {
        const auto encodedValue = Bytes({value ? 0xffu : 0x00u});
        return RequireValue(NGIN::Crypto::Encoding::EncodeDerElement(
                NGIN::Crypto::Encoding::DerTag {
                        .tagClass    = NGIN::Crypto::Encoding::DerTagClass::Universal,
                        .constructed = false,
                        .number      = 1,
                },
                View(encodedValue)));
    }

    [[nodiscard]] NGIN::Crypto::ByteBuffer CertificateWithExtensions(
            std::initializer_list<const NGIN::Crypto::ByteBuffer*> extensions)
    {
        const auto versionInteger = RequireValue(NGIN::Crypto::Encoding::EncodeDerInteger(View(Bytes({0x02}))));
        const auto version        = RequireValue(NGIN::Crypto::Encoding::EncodeDerElement(
                NGIN::Crypto::Encoding::DerTag {
                               .tagClass    = NGIN::Crypto::Encoding::DerTagClass::ContextSpecific,
                               .constructed = true,
                               .number      = 0,
                },
                View(versionInteger)));

        const auto serial             = RequireValue(NGIN::Crypto::Encoding::EncodeDerInteger(View(Bytes({0x01}))));
        const auto signatureAlgorithm = Ed25519AlgorithmIdentifier();
        const auto issuerCommonName   = NameAttribute({2, 5, 4, 3}, "Example Issuer");
        const auto subjectCommonName  = NameAttribute({2, 5, 4, 3}, "example.com");
        const auto issuerName         = Name({&issuerCommonName});
        const auto subjectName        = Name({&subjectCommonName});

        const auto notBefore        = UtcTime("260101000000Z");
        const auto notAfter         = UtcTime("270101000000Z");
        const auto validityChildren = Concat({&notBefore, &notAfter});
        const auto validity         = RequireValue(NGIN::Crypto::Encoding::EncodeDerSequence(View(validityChildren)));

        const auto publicKeyBytes = RepeatedByte(0x11, 32);
        const auto spki           = RequireValue(NGIN::Crypto::Keys::WriteSubjectPublicKeyInfo(
                NGIN::Crypto::Keys::KeyAlgorithm::Ed25519,
                View(publicKeyBytes)));

        const auto extensionsChildren = Concat(extensions);
        const auto extensionsSequence = RequireValue(NGIN::Crypto::Encoding::EncodeDerSequence(View(extensionsChildren)));
        const auto encodedExtensions  = RequireValue(NGIN::Crypto::Encoding::EncodeDerElement(
                NGIN::Crypto::Encoding::DerTag {
                         .tagClass    = NGIN::Crypto::Encoding::DerTagClass::ContextSpecific,
                         .constructed = true,
                         .number      = 3,
                },
                View(extensionsSequence)));

        const auto tbsChildren = Concat(
                {&version, &serial, &signatureAlgorithm, &issuerName, &validity, &subjectName, &spki, &encodedExtensions});
        const auto tbs = RequireValue(NGIN::Crypto::Encoding::EncodeDerSequence(View(tbsChildren)));

        const auto signature           = RequireValue(NGIN::Crypto::Encoding::EncodeDerBitString(0, View(RepeatedByte(0x44, 64))));
        const auto certificateChildren = Concat({&tbs, &signatureAlgorithm, &signature});
        return RequireValue(NGIN::Crypto::Encoding::EncodeDerSequence(View(certificateChildren)));
    }
}// namespace

TEST_CASE("X509 parser extracts certificate structure and selected extensions", "[Crypto][Certificate]")
{
    const auto versionInteger = RequireValue(NGIN::Crypto::Encoding::EncodeDerInteger(View(Bytes({0x02}))));
    const auto version        = RequireValue(NGIN::Crypto::Encoding::EncodeDerElement(
            NGIN::Crypto::Encoding::DerTag {
                           .tagClass    = NGIN::Crypto::Encoding::DerTagClass::ContextSpecific,
                           .constructed = true,
                           .number      = 0,
            },
            View(versionInteger)));

    const auto serial             = RequireValue(NGIN::Crypto::Encoding::EncodeDerInteger(View(Bytes({0x01}))));
    const auto signatureAlgorithm = Ed25519AlgorithmIdentifier();
    const auto issuerCommonName   = NameAttribute({2, 5, 4, 3}, "Example Issuer");
    const auto subjectCommonName  = NameAttribute({2, 5, 4, 3}, "example.com");
    const auto subjectOrg         = NameAttribute({2, 5, 4, 10}, "NGIN");
    const auto issuerName         = Name({&issuerCommonName});
    const auto subjectName        = Name({&subjectCommonName, &subjectOrg});

    const auto notBefore        = UtcTime("260101000000Z");
    const auto notAfter         = UtcTime("270101000000Z");
    const auto validityChildren = Concat({&notBefore, &notAfter});
    const auto validity         = RequireValue(NGIN::Crypto::Encoding::EncodeDerSequence(View(validityChildren)));

    const auto publicKeyBytes = RepeatedByte(0x11, 32);
    const auto spki           = RequireValue(NGIN::Crypto::Keys::WriteSubjectPublicKeyInfo(
            NGIN::Crypto::Keys::KeyAlgorithm::Ed25519,
            View(publicKeyBytes)));

    const auto dns          = GeneralName(2, Text("example.com"));
    const auto email        = GeneralName(1, Text("admin@example.com"));
    const auto ip           = GeneralName(7, Bytes({127, 0, 0, 1}));
    const auto sanChildren  = Concat({&dns, &email, &ip});
    const auto sanSequence  = RequireValue(NGIN::Crypto::Encoding::EncodeDerSequence(View(sanChildren)));
    const auto sanExtension = Extension({2, 5, 29, 17}, sanSequence);

    const auto keyUsageBits      = RequireValue(NGIN::Crypto::Encoding::EncodeDerBitString(5, View(Bytes({0xa0}))));
    const auto keyUsageExtension = Extension({2, 5, 29, 15}, keyUsageBits);

    const auto caTrue                    = DerBoolean(true);
    const auto pathLengthBytes           = Bytes({0x03});
    const auto pathLength                = RequireValue(NGIN::Crypto::Encoding::EncodeDerInteger(View(pathLengthBytes)));
    const auto basicConstraintsChildren  = Concat({&caTrue, &pathLength});
    const auto basicConstraintsValue     = RequireValue(NGIN::Crypto::Encoding::EncodeDerSequence(View(basicConstraintsChildren)));
    const auto basicConstraintsExtension = ExtensionWithCritical({2, 5, 29, 19}, true, basicConstraintsValue);

    const auto subjectKeyIdentifier          = Bytes({0x10, 0x20, 0x30, 0x40});
    const auto subjectKeyIdentifierValue     = RequireValue(NGIN::Crypto::Encoding::EncodeDerOctetString(View(subjectKeyIdentifier)));
    const auto subjectKeyIdentifierExtension = Extension({2, 5, 29, 14}, subjectKeyIdentifierValue);

    const auto authorityKeyIdentifierValue = GeneralName(0, Bytes({0x99, 0x88, 0x77}));
    const auto authorityKeyIdentifierSequence =
            RequireValue(NGIN::Crypto::Encoding::EncodeDerSequence(View(authorityKeyIdentifierValue)));
    const auto authorityKeyIdentifierExtension = Extension({2, 5, 29, 35}, authorityKeyIdentifierSequence);

    const std::array<NGIN::UInt32, 9> serverAuthOid {1, 3, 6, 1, 5, 5, 7, 3, 1};
    const auto                        serverAuth   = RequireValue(NGIN::Crypto::Encoding::EncodeDerObjectIdentifier(serverAuthOid));
    const auto                        ekuSequence  = RequireValue(NGIN::Crypto::Encoding::EncodeDerSequence(View(serverAuth)));
    const auto                        ekuExtension = Extension({2, 5, 29, 37}, ekuSequence);

    const auto extensionsChildren = Concat(
            {&sanExtension,
             &keyUsageExtension,
             &basicConstraintsExtension,
             &subjectKeyIdentifierExtension,
             &authorityKeyIdentifierExtension,
             &ekuExtension});
    const auto extensionsSequence = RequireValue(NGIN::Crypto::Encoding::EncodeDerSequence(View(extensionsChildren)));
    const auto extensions         = RequireValue(NGIN::Crypto::Encoding::EncodeDerElement(
            NGIN::Crypto::Encoding::DerTag {
                            .tagClass    = NGIN::Crypto::Encoding::DerTagClass::ContextSpecific,
                            .constructed = true,
                            .number      = 3,
            },
            View(extensionsSequence)));

    const auto tbsChildren = Concat(
            {&version, &serial, &signatureAlgorithm, &issuerName, &validity, &subjectName, &spki, &extensions});
    const auto tbs = RequireValue(NGIN::Crypto::Encoding::EncodeDerSequence(View(tbsChildren)));

    const auto signature           = RequireValue(NGIN::Crypto::Encoding::EncodeDerBitString(0, View(RepeatedByte(0x44, 64))));
    const auto certificateChildren = Concat({&tbs, &signatureAlgorithm, &signature});
    const auto certificateDer      = RequireValue(NGIN::Crypto::Encoding::EncodeDerSequence(View(certificateChildren)));

    auto parsed = NGIN::Crypto::Certificates::ParseX509Certificate(View(certificateDer));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed.value().version == 3);
    REQUIRE(parsed.value().serialNumber.Size() == 1);
    REQUIRE(parsed.value().serialNumber[0] == NGIN::Byte {1});
    REQUIRE(parsed.value().validity.notBefore == "260101000000Z");
    REQUIRE(parsed.value().validity.notAfter == "270101000000Z");
    REQUIRE(parsed.value().issuer.attributes.Size() == 1);
    REQUIRE(parsed.value().issuer.attributes[0].type ==
            NGIN::Crypto::Certificates::DistinguishedNameAttributeType::CommonName);
    REQUIRE(parsed.value().issuer.attributes[0].value == "Example Issuer");
    REQUIRE(parsed.value().subject.attributes.Size() == 2);
    REQUIRE(parsed.value().subject.attributes[0].type ==
            NGIN::Crypto::Certificates::DistinguishedNameAttributeType::CommonName);
    REQUIRE(parsed.value().subject.attributes[0].value == "example.com");
    REQUIRE(parsed.value().subject.attributes[1].type ==
            NGIN::Crypto::Certificates::DistinguishedNameAttributeType::OrganizationName);
    REQUIRE(parsed.value().subject.attributes[1].value == "NGIN");
    REQUIRE(parsed.value().hasKnownSignatureAlgorithm);
    REQUIRE(parsed.value().signatureAlgorithm == NGIN::Crypto::SignatureAlgorithm::Ed25519);
    REQUIRE(parsed.value().subjectPublicKeyInfo.algorithm.algorithm == NGIN::Crypto::Keys::KeyAlgorithm::Ed25519);
    REQUIRE(parsed.value().subjectPublicKeyInfo.publicKey.Size() == 32);
    REQUIRE(parsed.value().signatureValue.Size() == 64);

    REQUIRE(parsed.value().hasSubjectAltNames);
    REQUIRE(parsed.value().subjectAltNames.dnsNames.Size() == 1);
    REQUIRE(parsed.value().subjectAltNames.dnsNames[0] == "example.com");
    REQUIRE(parsed.value().subjectAltNames.emailAddresses.Size() == 1);
    REQUIRE(parsed.value().subjectAltNames.emailAddresses[0] == "admin@example.com");
    REQUIRE(parsed.value().subjectAltNames.ipAddresses.Size() == 1);
    REQUIRE(parsed.value().subjectAltNames.ipAddresses[0].Size() == 4);

    REQUIRE(parsed.value().hasKeyUsage);
    REQUIRE(parsed.value().keyUsage.unusedBitCount == 5);
    REQUIRE(parsed.value().keyUsage.bits.Size() == 1);
    REQUIRE(parsed.value().keyUsage.bits[0] == NGIN::Byte {0xa0});

    REQUIRE(parsed.value().hasBasicConstraints);
    REQUIRE(parsed.value().basicConstraints.certificateAuthority);
    REQUIRE(parsed.value().basicConstraints.hasPathLengthConstraint);
    REQUIRE(parsed.value().basicConstraints.pathLengthConstraint == 3);

    REQUIRE(parsed.value().hasSubjectKeyIdentifier);
    REQUIRE(parsed.value().subjectKeyIdentifier.Size() == subjectKeyIdentifier.Size());
    REQUIRE(parsed.value().subjectKeyIdentifier[0] == NGIN::Byte {0x10});
    REQUIRE(parsed.value().subjectKeyIdentifier[3] == NGIN::Byte {0x40});

    REQUIRE(parsed.value().hasAuthorityKeyIdentifier);
    REQUIRE(parsed.value().authorityKeyIdentifier.Size() == 3);
    REQUIRE(parsed.value().authorityKeyIdentifier[0] == NGIN::Byte {0x99});
    REQUIRE(parsed.value().authorityKeyIdentifier[2] == NGIN::Byte {0x77});

    REQUIRE(parsed.value().extendedKeyUsages.Size() == 1);
    REQUIRE(parsed.value().extendedKeyUsages[0].Size() == serverAuthOid.size());
}

TEST_CASE("X509 parser rejects malformed known extension schemas", "[Crypto][Certificate]")
{
    const auto badIpName    = GeneralName(7, Bytes({127, 0, 0}));
    const auto badSanNames  = RequireValue(NGIN::Crypto::Encoding::EncodeDerSequence(View(badIpName)));
    const auto badSan       = Extension({2, 5, 29, 17}, badSanNames);
    auto       parsedBadSan = NGIN::Crypto::Certificates::ParseX509Certificate(View(CertificateWithExtensions({&badSan})));
    REQUIRE_FALSE(parsedBadSan.has_value());
    REQUIRE(parsedBadSan.error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);

    const auto dns          = GeneralName(2, Text("example.com"));
    const auto sanNames     = RequireValue(NGIN::Crypto::Encoding::EncodeDerSequence(View(dns)));
    const auto sanExtension = Extension({2, 5, 29, 17}, sanNames);
    auto       duplicateSan = NGIN::Crypto::Certificates::ParseX509Certificate(
            View(CertificateWithExtensions({&sanExtension, &sanExtension})));
    REQUIRE_FALSE(duplicateSan.has_value());
    REQUIRE(duplicateSan.error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);

    const auto emptyEkuSequence = RequireValue(NGIN::Crypto::Encoding::EncodeDerSequence(NGIN::Crypto::ConstByteSpan {}));
    const auto emptyEku         = Extension({2, 5, 29, 37}, emptyEkuSequence);
    auto       parsedEmptyEku   = NGIN::Crypto::Certificates::ParseX509Certificate(View(CertificateWithExtensions({&emptyEku})));
    REQUIRE_FALSE(parsedEmptyEku.has_value());
    REQUIRE(parsedEmptyEku.error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);

    const auto criticalFalseSan = ExtensionWithCritical({2, 5, 29, 17}, false, sanNames);
    auto       parsedCriticalFalse =
            NGIN::Crypto::Certificates::ParseX509Certificate(View(CertificateWithExtensions({&criticalFalseSan})));
    REQUIRE_FALSE(parsedCriticalFalse.has_value());
    REQUIRE(parsedCriticalFalse.error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);

    const auto explicitFalseCa       = DerBoolean(false);
    const auto falseBasicConstraints = RequireValue(NGIN::Crypto::Encoding::EncodeDerSequence(View(explicitFalseCa)));
    const auto falseBasicExtension   = Extension({2, 5, 29, 19}, falseBasicConstraints);
    auto       parsedFalseBasic      = NGIN::Crypto::Certificates::ParseX509Certificate(
            View(CertificateWithExtensions({&falseBasicExtension})));
    REQUIRE_FALSE(parsedFalseBasic.has_value());
    REQUIRE(parsedFalseBasic.error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);

    const auto pathLengthOnlyInteger   = Bytes({0x02, 0x01, 0x00});
    const auto pathLengthOnlyBasic     = RequireValue(NGIN::Crypto::Encoding::EncodeDerSequence(View(pathLengthOnlyInteger)));
    const auto pathLengthOnlyExtension = Extension({2, 5, 29, 19}, pathLengthOnlyBasic);
    auto       parsedPathLengthOnly    = NGIN::Crypto::Certificates::ParseX509Certificate(
            View(CertificateWithExtensions({&pathLengthOnlyExtension})));
    REQUIRE_FALSE(parsedPathLengthOnly.has_value());
    REQUIRE(parsedPathLengthOnly.error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);

    const auto caTrue                = DerBoolean(true);
    const auto basicConstraintsValue = RequireValue(NGIN::Crypto::Encoding::EncodeDerSequence(View(caTrue)));
    const auto basicConstraints      = Extension({2, 5, 29, 19}, basicConstraintsValue);
    auto       duplicateBasic        = NGIN::Crypto::Certificates::ParseX509Certificate(
            View(CertificateWithExtensions({&basicConstraints, &basicConstraints})));
    REQUIRE_FALSE(duplicateBasic.has_value());
    REQUIRE(duplicateBasic.error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);
}

TEST_CASE("X509 parser rejects malformed top-level certificate input", "[Crypto][Certificate]")
{
    const auto notASequence = Bytes({0x04, 0x01, 0x00});
    auto       parsed       = NGIN::Crypto::Certificates::ParseX509Certificate(View(notASequence));

    REQUIRE_FALSE(parsed.has_value());
    REQUIRE(parsed.error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);
}

TEST_CASE("X509 parser malformed corpus rejects truncated certificate structures", "[Crypto][Certificate]")
{
    for (auto bytes: {
                 Bytes({}),
                 Bytes({0x30}),
                 Bytes({0x30, 0x00}),
                 Bytes({0x30, 0x03, 0x30, 0x01}),
                 Bytes({0x30, 0x06, 0x30, 0x00, 0x30, 0x00, 0x03, 0x00}),
         })
    {
        auto parsed = NGIN::Crypto::Certificates::ParseX509Certificate(View(bytes));
        REQUIRE_FALSE(parsed.has_value());
        REQUIRE(parsed.error().Code() == NGIN::Crypto::CryptoErrorCode::ParseError);
    }
}

TEST_CASE("CertificateStore supports custom lookup and platform root loading", "[Crypto][Certificate]")
{
    NGIN::Crypto::Certificates::Certificate certificate;
    certificate.subjectDer                = Bytes({0x30, 0x00});
    certificate.subjectKeyIdentifier      = Bytes({0x10, 0x20, 0x30});
    certificate.hasSubjectKeyIdentifier   = true;
    certificate.authorityKeyIdentifier    = Bytes({0x90, 0x80, 0x70});
    certificate.hasAuthorityKeyIdentifier = true;

    NGIN::Containers::Vector<NGIN::Crypto::Certificates::Certificate> certificates;
    certificates.PushBack(certificate);

    auto store = NGIN::Crypto::Certificates::CreateCustomCertificateStore(std::move(certificates));
    REQUIRE(store.has_value());
    REQUIRE(store.value().Size() == 1);
    REQUIRE(store.value().Info().kind == NGIN::Crypto::Certificates::CertificateStoreKind::Custom);
    REQUIRE(store.value().Info().source == "custom");
    REQUIRE(store.value().Info().certificatesLoaded == 1);
    REQUIRE(store.value().Info().certificatesSkipped == 0);

    auto matches = store.value().FindBySubjectDer(View(certificate.subjectDer));
    REQUIRE(matches.has_value());
    REQUIRE(matches.value().certificates.Size() == 1);

    auto misses = store.value().FindBySubjectDer(View(Bytes({0x30, 0x01, 0x00})));
    REQUIRE(misses.has_value());
    REQUIRE(misses.value().certificates.Size() == 0);

    auto skiMatches = store.value().FindBySubjectKeyIdentifier(View(certificate.subjectKeyIdentifier));
    REQUIRE(skiMatches.has_value());
    REQUIRE(skiMatches.value().certificates.Size() == 1);

    auto skiMisses = store.value().FindBySubjectKeyIdentifier(View(Bytes({0x10, 0x20})));
    REQUIRE(skiMisses.has_value());
    REQUIRE(skiMisses.value().certificates.Size() == 0);

    auto akiMatches = store.value().FindByAuthorityKeyIdentifier(View(certificate.authorityKeyIdentifier));
    REQUIRE(akiMatches.has_value());
    REQUIRE(akiMatches.value().certificates.Size() == 1);

    auto akiMisses = store.value().FindByAuthorityKeyIdentifier(View(Bytes({0x90, 0x80})));
    REQUIRE(akiMisses.has_value());
    REQUIRE(akiMisses.value().certificates.Size() == 0);

    auto platformSelection = NGIN::Crypto::Certificates::OpenPlatformRootCertificateStoreWithDiagnostics();
    REQUIRE(platformSelection.diagnostics.Size() > 0);

    auto platform = NGIN::Crypto::Certificates::OpenPlatformRootCertificateStore();
    if (platform.has_value())
    {
        REQUIRE(platformSelection.store.has_value());
        REQUIRE(platform.value().Info().kind == NGIN::Crypto::Certificates::CertificateStoreKind::PlatformRoot);
        REQUIRE(platform.value().Info().platformBacked);
        REQUIRE(platform.value().Info().available);
        REQUIRE(platform.value().Size() > 0);
        REQUIRE_FALSE(platform.value().Info().sourcePath.empty());
        REQUIRE(platform.value().Info().certificatesLoaded == platform.value().Size());
        REQUIRE_FALSE(platform.value().Info().diagnostic.empty());

#if defined(__linux__)
        REQUIRE(HasReadableLinuxCaBundle());
        REQUIRE(platform.value().Info().operatingSystem == "linux");
        REQUIRE(platform.value().Info().source == "system-ca-bundle");
#elif defined(_WIN32)
        REQUIRE(platform.value().Info().operatingSystem == "windows");
        REQUIRE(platform.value().Info().source == "native-windows-certificate-store");
#elif defined(__APPLE__)
        REQUIRE(platform.value().Info().operatingSystem == "macos");
        REQUIRE(platform.value().Info().source == "native-apple-security-trust-anchors");
#endif

        bool sawSuccessfulDiagnostic = false;
        for (const auto& diagnostic: platformSelection.diagnostics)
        {
            sawSuccessfulDiagnostic = sawSuccessfulDiagnostic ||
                                      (diagnostic.code == NGIN::Crypto::CryptoErrorCode::None &&
                                       diagnostic.info.available &&
                                       diagnostic.info.source == platform.value().Info().source);
        }
        REQUIRE(sawSuccessfulDiagnostic);
    }
    else
    {
        REQUIRE_FALSE(platformSelection.store.has_value());
        REQUIRE(platform.error().Code() != NGIN::Crypto::CryptoErrorCode::None);
        REQUIRE(platformSelection.store.error().Code() != NGIN::Crypto::CryptoErrorCode::None);
        REQUIRE_FALSE(platformSelection.diagnostics[0].info.available);
    }
}

TEST_CASE("TlsCredentialMaterial carries chain and private key handoff data", "[Crypto][Certificate]")
{
    NGIN::Crypto::Certificates::Certificate certificate;
    certificate.subjectDer = Bytes({0x30, 0x00});

    NGIN::Crypto::Certificates::TlsCredentialMaterial material;
    material.certificateChain.certificates.PushBack(certificate);
    material.privateKey.algorithm.algorithm = NGIN::Crypto::Keys::KeyAlgorithm::Ed25519;
    material.privateKey.privateKey          = Bytes({0x01, 0x02, 0x03});

    REQUIRE(material.certificateChain.certificates.Size() == 1);
    REQUIRE(material.privateKey.algorithm.algorithm == NGIN::Crypto::Keys::KeyAlgorithm::Ed25519);
    REQUIRE(material.privateKey.privateKey.Size() == 3);
}
