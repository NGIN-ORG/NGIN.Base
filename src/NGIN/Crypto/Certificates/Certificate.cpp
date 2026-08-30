#include <NGIN/Crypto/Certificates/Certificate.hpp>

#include <NGIN/Crypto/Encoding/Der.hpp>
#include <NGIN/Crypto/Errors/CryptoError.hpp>
#include <NGIN/Crypto/Signatures/Verify.hpp>

#include <array>
#include <cstddef>
#include <string_view>
#include <utility>

namespace NGIN::Crypto::Certificates
{
    namespace
    {
        using NGIN::Crypto::Encoding::DerElement;
        using NGIN::Crypto::Encoding::DerReader;
        using NGIN::Crypto::Encoding::DerTag;
        using NGIN::Crypto::Encoding::DerTagClass;
        using NGIN::Crypto::Encoding::DerUniversalTag;

        constexpr std::array<NGIN::UInt32, 4> ED25519_OID {1, 3, 101, 112};
        constexpr std::array<NGIN::UInt32, 7> ECDSA_WITH_SHA256_OID {1, 2, 840, 10045, 4, 3, 2};
        constexpr std::array<NGIN::UInt32, 7> RSA_PSS_OID {1, 2, 840, 113549, 1, 1, 10};
        constexpr std::array<NGIN::UInt32, 4> SUBJECT_ALT_NAME_OID {2, 5, 29, 17};
        constexpr std::array<NGIN::UInt32, 4> KEY_USAGE_OID {2, 5, 29, 15};
        constexpr std::array<NGIN::UInt32, 4> BASIC_CONSTRAINTS_OID {2, 5, 29, 19};
        constexpr std::array<NGIN::UInt32, 4> SUBJECT_KEY_IDENTIFIER_OID {2, 5, 29, 14};
        constexpr std::array<NGIN::UInt32, 4> AUTHORITY_KEY_IDENTIFIER_OID {2, 5, 29, 35};
        constexpr std::array<NGIN::UInt32, 4> EXTENDED_KEY_USAGE_OID {2, 5, 29, 37};
        constexpr std::array<NGIN::UInt32, 4> RDN_COMMON_NAME_OID {2, 5, 4, 3};
        constexpr std::array<NGIN::UInt32, 4> RDN_COUNTRY_NAME_OID {2, 5, 4, 6};
        constexpr std::array<NGIN::UInt32, 4> RDN_LOCALITY_NAME_OID {2, 5, 4, 7};
        constexpr std::array<NGIN::UInt32, 4> RDN_STATE_OR_PROVINCE_NAME_OID {2, 5, 4, 8};
        constexpr std::array<NGIN::UInt32, 4> RDN_ORGANIZATION_NAME_OID {2, 5, 4, 10};
        constexpr std::array<NGIN::UInt32, 4> RDN_ORGANIZATIONAL_UNIT_NAME_OID {2, 5, 4, 11};
        constexpr std::array<NGIN::UInt32, 4> RDN_SERIAL_NUMBER_OID {2, 5, 4, 5};
        constexpr std::array<NGIN::UInt32, 7> RDN_DOMAIN_COMPONENT_OID {0, 9, 2342, 19200300, 100, 1, 25};
        constexpr std::array<NGIN::UInt32, 7> RDN_EMAIL_ADDRESS_OID {1, 2, 840, 113549, 1, 9, 1};

        [[nodiscard]] constexpr CryptoError ParseError() noexcept
        {
            return CryptoError {CryptoErrorCode::ParseError};
        }

        [[nodiscard]] constexpr CryptoError UnsupportedAlgorithm() noexcept
        {
            return CryptoError {CryptoErrorCode::UnsupportedAlgorithm};
        }

        [[nodiscard]] constexpr NGIN::UInt8 ByteValue(NGIN::Byte byte) noexcept
        {
            return std::to_integer<NGIN::UInt8>(byte);
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

        [[nodiscard]] bool IsTag(const DerElement& element, DerTagClass tagClass, bool constructed, NGIN::UInt32 number) noexcept
        {
            return element.tag == DerTag {
                                          .tagClass    = tagClass,
                                          .constructed = constructed,
                                          .number      = number,
                                  };
        }

        [[nodiscard]] bool IsUniversal(const DerElement& element, DerUniversalTag tag, bool constructed = false) noexcept
        {
            return NGIN::Crypto::Encoding::IsDerUniversalElement(element, tag, constructed);
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

        [[nodiscard]] bool IsIa5StringValue(ConstByteSpan value) noexcept
        {
            for (NGIN::Byte byte: value)
            {
                if (ByteValue(byte) > 0x7fu)
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] CryptoExpected<NGIN::UInt32> ReadUInt32Integer(const DerElement& element) noexcept
        {
            auto integer = NGIN::Crypto::Encoding::ReadDerInteger(element);
            if (!integer.has_value())
            {
                return std::unexpected(std::move(integer).error());
            }
            if (integer.value().empty() || (ByteValue(integer.value()[0]) & 0x80u) != 0)
            {
                return std::unexpected(ParseError());
            }

            NGIN::UIntSize offset = 0;
            if (integer.value().size() > 1 && ByteValue(integer.value()[0]) == 0x00u)
            {
                offset = 1;
            }
            if (integer.value().size() - offset > sizeof(NGIN::UInt32))
            {
                return std::unexpected(ParseError());
            }

            NGIN::UInt32 value = 0;
            for (NGIN::UIntSize i = offset; i < integer.value().size(); ++i)
            {
                value = static_cast<NGIN::UInt32>((value << 8u) | ByteValue(integer.value()[i]));
            }
            return value;
        }

        [[nodiscard]] CryptoExpected<std::string> ReadTimeString(const DerElement& element)
        {
            if (!IsUniversal(element, static_cast<DerUniversalTag>(23)) && !IsUniversal(element, static_cast<DerUniversalTag>(24)))
            {
                return std::unexpected(ParseError());
            }

            std::string value;
            value.reserve(element.value.size());
            for (NGIN::Byte byte: element.value)
            {
                value.push_back(static_cast<char>(ByteValue(byte)));
            }

            return value;
        }

        [[nodiscard]] CryptoExpected<CertificateValidity> ParseValidity(const DerElement& element)
        {
            DerReader parent {element.encoded};
            auto      reader = NGIN::Crypto::Encoding::ReadDerSequence(parent, element);
            if (!reader.has_value())
            {
                return std::unexpected(std::move(reader).error());
            }

            auto notBeforeElement = reader.value().ReadElement();
            if (!notBeforeElement.has_value())
            {
                return std::unexpected(std::move(notBeforeElement).error());
            }
            auto notBefore = ReadTimeString(notBeforeElement.value());
            if (!notBefore.has_value())
            {
                return std::unexpected(std::move(notBefore).error());
            }

            auto notAfterElement = reader.value().ReadElement();
            if (!notAfterElement.has_value())
            {
                return std::unexpected(std::move(notAfterElement).error());
            }
            auto notAfter = ReadTimeString(notAfterElement.value());
            if (!notAfter.has_value())
            {
                return std::unexpected(std::move(notAfter).error());
            }
            if (!reader.value().IsAtEnd())
            {
                return std::unexpected(ParseError());
            }

            return CertificateValidity {
                    .notBefore = std::move(notBefore.value()),
                    .notAfter  = std::move(notAfter.value()),
            };
        }

        [[nodiscard]] DistinguishedNameAttributeType IdentifyNameAttribute(
                const NGIN::Containers::Vector<NGIN::UInt32>& oid) noexcept
        {
            if (OidEquals(oid, RDN_COMMON_NAME_OID))
            {
                return DistinguishedNameAttributeType::CommonName;
            }
            if (OidEquals(oid, RDN_COUNTRY_NAME_OID))
            {
                return DistinguishedNameAttributeType::CountryName;
            }
            if (OidEquals(oid, RDN_ORGANIZATION_NAME_OID))
            {
                return DistinguishedNameAttributeType::OrganizationName;
            }
            if (OidEquals(oid, RDN_ORGANIZATIONAL_UNIT_NAME_OID))
            {
                return DistinguishedNameAttributeType::OrganizationalUnitName;
            }
            if (OidEquals(oid, RDN_LOCALITY_NAME_OID))
            {
                return DistinguishedNameAttributeType::LocalityName;
            }
            if (OidEquals(oid, RDN_STATE_OR_PROVINCE_NAME_OID))
            {
                return DistinguishedNameAttributeType::StateOrProvinceName;
            }
            if (OidEquals(oid, RDN_SERIAL_NUMBER_OID))
            {
                return DistinguishedNameAttributeType::SerialNumber;
            }
            if (OidEquals(oid, RDN_DOMAIN_COMPONENT_OID))
            {
                return DistinguishedNameAttributeType::DomainComponent;
            }
            if (OidEquals(oid, RDN_EMAIL_ADDRESS_OID))
            {
                return DistinguishedNameAttributeType::EmailAddress;
            }

            return DistinguishedNameAttributeType::Unknown;
        }

        [[nodiscard]] CryptoExpected<std::string> ReadNameValueString(const DerElement& element)
        {
            if (element.tag.tagClass != DerTagClass::Universal || element.tag.constructed)
            {
                return std::unexpected(ParseError());
            }

            std::string value;
            if (element.tag.number == 30)
            {
                if ((element.value.size() % 2) != 0)
                {
                    return std::unexpected(ParseError());
                }
                value.reserve(element.value.size() / 2);
                for (NGIN::UIntSize i = 0; i < element.value.size(); i += 2)
                {
                    if (ByteValue(element.value[i]) != 0)
                    {
                        return std::unexpected(ParseError());
                    }
                    value.push_back(static_cast<char>(ByteValue(element.value[i + 1])));
                }
                return value;
            }

            if (element.tag.number != 12 && element.tag.number != 19 && element.tag.number != 20 &&
                element.tag.number != 22)
            {
                return std::unexpected(ParseError());
            }

            value.reserve(element.value.size());
            for (NGIN::Byte byte: element.value)
            {
                value.push_back(static_cast<char>(ByteValue(byte)));
            }
            return value;
        }

        [[nodiscard]] CryptoExpected<DistinguishedName> ParseDistinguishedName(const DerElement& element)
        {
            DerReader parent {element.encoded};
            auto      name = NGIN::Crypto::Encoding::ReadDerSequence(parent, element);
            if (!name.has_value())
            {
                return std::unexpected(std::move(name).error());
            }

            DistinguishedName result;
            while (!name.value().IsAtEnd())
            {
                auto rdnSetElement = name.value().ReadElement();
                if (!rdnSetElement.has_value())
                {
                    return std::unexpected(std::move(rdnSetElement).error());
                }
                if (!IsUniversal(rdnSetElement.value(), DerUniversalTag::Set, true))
                {
                    return std::unexpected(ParseError());
                }

                DerReader setParent {rdnSetElement.value().encoded};
                auto      rdnSet = NGIN::Crypto::Encoding::ReadDerSet(setParent, rdnSetElement.value());
                if (!rdnSet.has_value())
                {
                    return std::unexpected(std::move(rdnSet).error());
                }

                while (!rdnSet.value().IsAtEnd())
                {
                    auto attributeElement = rdnSet.value().ReadElement();
                    if (!attributeElement.has_value())
                    {
                        return std::unexpected(std::move(attributeElement).error());
                    }

                    DerReader attributeParent {attributeElement.value().encoded};
                    auto      attribute = NGIN::Crypto::Encoding::ReadDerSequence(attributeParent, attributeElement.value());
                    if (!attribute.has_value())
                    {
                        return std::unexpected(std::move(attribute).error());
                    }

                    auto oidElement = attribute.value().ReadElement();
                    if (!oidElement.has_value())
                    {
                        return std::unexpected(std::move(oidElement).error());
                    }
                    auto oid = NGIN::Crypto::Encoding::ReadDerObjectIdentifier(oidElement.value());
                    if (!oid.has_value())
                    {
                        return std::unexpected(std::move(oid).error());
                    }

                    auto valueElement = attribute.value().ReadElement();
                    if (!valueElement.has_value())
                    {
                        return std::unexpected(std::move(valueElement).error());
                    }
                    if (!attribute.value().IsAtEnd())
                    {
                        return std::unexpected(ParseError());
                    }

                    auto value = ReadNameValueString(valueElement.value());
                    if (!value.has_value())
                    {
                        return std::unexpected(std::move(value).error());
                    }

                    result.attributes.PushBack(DistinguishedNameAttribute {
                            .type             = IdentifyNameAttribute(oid.value()),
                            .objectIdentifier = std::move(oid.value()),
                            .value            = std::move(value.value()),
                            .valueTag         = valueElement.value().tag.number,
                    });
                }
            }

            return result;
        }

        [[nodiscard]] CryptoExpected<NGIN::Containers::Vector<NGIN::UInt32>> ReadOidElement(const DerElement& element)
        {
            return NGIN::Crypto::Encoding::ReadDerObjectIdentifier(element);
        }

        [[nodiscard]] CryptoExpected<NGIN::Crypto::SignatureAlgorithm> IdentifySignatureAlgorithm(const DerElement& element)
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
            auto oid = ReadOidElement(oidElement.value());
            if (!oid.has_value())
            {
                return std::unexpected(std::move(oid).error());
            }

            if (OidEquals(oid.value(), ED25519_OID))
            {
                if (!reader.value().IsAtEnd())
                {
                    return std::unexpected(ParseError());
                }
                return NGIN::Crypto::SignatureAlgorithm::Ed25519;
            }

            if (OidEquals(oid.value(), ECDSA_WITH_SHA256_OID))
            {
                if (!reader.value().IsAtEnd())
                {
                    return std::unexpected(ParseError());
                }
                return NGIN::Crypto::SignatureAlgorithm::EcdsaP256Sha256;
            }

            if (OidEquals(oid.value(), RSA_PSS_OID))
            {
                return NGIN::Crypto::SignatureAlgorithm::RsaPssSha256;
            }

            return std::unexpected(UnsupportedAlgorithm());
        }

        [[nodiscard]] CryptoExpected<void> ValidateGeneralNameSchema(const DerElement& generalName)
        {
            if (generalName.tag.tagClass != DerTagClass::ContextSpecific)
            {
                return std::unexpected(ParseError());
            }

            switch (generalName.tag.number)
            {
                case 0:
                case 3:
                case 5:
                    return generalName.tag.constructed && !generalName.value.empty()
                                   ? CryptoExpected<void> {}
                                   : CryptoExpected<void> {std::unexpected(ParseError())};
                case 1:
                case 2:
                case 6:
                    return !generalName.tag.constructed && !generalName.value.empty() && IsIa5StringValue(generalName.value)
                                   ? CryptoExpected<void> {}
                                   : CryptoExpected<void> {std::unexpected(ParseError())};
                case 4: {
                    if (!generalName.tag.constructed)
                    {
                        return std::unexpected(ParseError());
                    }
                    auto nameElement = ReadSingleElement(generalName.value);
                    if (!nameElement.has_value())
                    {
                        return std::unexpected(std::move(nameElement).error());
                    }
                    auto name = ParseDistinguishedName(nameElement.value());
                    return name.has_value()
                                   ? CryptoExpected<void> {}
                                   : CryptoExpected<void> {std::unexpected(std::move(name).error())};
                }
                case 7:
                    return !generalName.tag.constructed &&
                                           (generalName.value.size() == 4 || generalName.value.size() == 16)
                                   ? CryptoExpected<void> {}
                                   : CryptoExpected<void> {std::unexpected(ParseError())};
                case 8: {
                    if (generalName.tag.constructed || generalName.value.empty())
                    {
                        return std::unexpected(ParseError());
                    }
                    DerElement oidElement {
                            .tag     = NGIN::Crypto::Encoding::MakeDerUniversalTag(DerUniversalTag::ObjectIdentifier),
                            .value   = generalName.value,
                            .encoded = generalName.value,
                    };
                    auto oid = NGIN::Crypto::Encoding::ReadDerObjectIdentifier(oidElement);
                    return oid.has_value()
                                   ? CryptoExpected<void> {}
                                   : CryptoExpected<void> {std::unexpected(std::move(oid).error())};
                }
                default:
                    return std::unexpected(ParseError());
            }
        }

        [[nodiscard]] CryptoExpected<void> ParseSubjectAltNameExtension(ConstByteSpan encodedNames, SubjectAltNames& names)
        {
            DerReader reader {encodedNames};
            auto      top = reader.ReadElement();
            if (!top.has_value())
            {
                return std::unexpected(std::move(top).error());
            }
            if (!reader.IsAtEnd())
            {
                return std::unexpected(ParseError());
            }

            DerReader parent {top.value().encoded};
            auto      generalNames = NGIN::Crypto::Encoding::ReadDerSequence(parent, top.value());
            if (!generalNames.has_value())
            {
                return std::unexpected(std::move(generalNames).error());
            }

            bool hasAnyGeneralName = false;
            while (!generalNames.value().IsAtEnd())
            {
                auto generalName = generalNames.value().ReadElement();
                if (!generalName.has_value())
                {
                    return std::unexpected(std::move(generalName).error());
                }
                hasAnyGeneralName = true;
                auto schema       = ValidateGeneralNameSchema(generalName.value());
                if (!schema.has_value())
                {
                    return std::unexpected(std::move(schema).error());
                }

                if (IsTag(generalName.value(), DerTagClass::ContextSpecific, false, 1) ||
                    IsTag(generalName.value(), DerTagClass::ContextSpecific, false, 2))
                {
                    std::string text;
                    text.reserve(generalName.value().value.size());
                    for (NGIN::Byte byte: generalName.value().value)
                    {
                        text.push_back(static_cast<char>(ByteValue(byte)));
                    }

                    if (generalName.value().tag.number == 1)
                    {
                        names.emailAddresses.PushBack(std::move(text));
                    }
                    else
                    {
                        names.dnsNames.PushBack(std::move(text));
                    }
                }
                else if (IsTag(generalName.value(), DerTagClass::ContextSpecific, false, 7))
                {
                    names.ipAddresses.PushBack(CopyBytes(generalName.value().value));
                }
            }

            if (!hasAnyGeneralName)
            {
                return std::unexpected(ParseError());
            }

            return {};
        }

        [[nodiscard]] CryptoExpected<void> ParseKeyUsageExtension(ConstByteSpan encodedKeyUsage, KeyUsage& keyUsage)
        {
            DerReader reader {encodedKeyUsage};
            auto      element = reader.ReadElement();
            if (!element.has_value())
            {
                return std::unexpected(std::move(element).error());
            }
            if (!reader.IsAtEnd())
            {
                return std::unexpected(ParseError());
            }

            auto bits = NGIN::Crypto::Encoding::ReadDerBitString(element.value());
            if (!bits.has_value())
            {
                return std::unexpected(std::move(bits).error());
            }
            if (bits.value().bytes.empty() || bits.value().bytes.size() > 2)
            {
                return std::unexpected(ParseError());
            }
            const auto usedBits = (bits.value().bytes.size() * 8u) - bits.value().unusedBitCount;
            if (usedBits == 0 || usedBits > 9)
            {
                return std::unexpected(ParseError());
            }

            keyUsage.unusedBitCount = bits.value().unusedBitCount;
            keyUsage.bits           = CopyBytes(bits.value().bytes);
            return {};
        }

        [[nodiscard]] CryptoExpected<void> ParseBasicConstraintsExtension(
                ConstByteSpan encodedBasicConstraints, BasicConstraints& basicConstraints)
        {
            DerReader reader {encodedBasicConstraints};
            auto      top = reader.ReadElement();
            if (!top.has_value())
            {
                return std::unexpected(std::move(top).error());
            }
            if (!reader.IsAtEnd())
            {
                return std::unexpected(ParseError());
            }

            DerReader parent {top.value().encoded};
            auto      sequence = NGIN::Crypto::Encoding::ReadDerSequence(parent, top.value());
            if (!sequence.has_value())
            {
                return std::unexpected(std::move(sequence).error());
            }

            BasicConstraints parsed;
            if (!sequence.value().IsAtEnd())
            {
                auto first = sequence.value().ReadElement();
                if (!first.has_value())
                {
                    return std::unexpected(std::move(first).error());
                }

                if (IsUniversal(first.value(), DerUniversalTag::Boolean))
                {
                    if (first.value().value.size() != 1 || first.value().value[0] != NGIN::Byte {0xff})
                    {
                        return std::unexpected(ParseError());
                    }
                    parsed.certificateAuthority = true;
                }
                else if (IsUniversal(first.value(), DerUniversalTag::Integer))
                {
                    return std::unexpected(ParseError());
                }
                else
                {
                    return std::unexpected(ParseError());
                }
            }

            if (!sequence.value().IsAtEnd())
            {
                auto pathLengthElement = sequence.value().ReadElement();
                if (!pathLengthElement.has_value())
                {
                    return std::unexpected(std::move(pathLengthElement).error());
                }
                auto pathLength = ReadUInt32Integer(pathLengthElement.value());
                if (!pathLength.has_value())
                {
                    return std::unexpected(std::move(pathLength).error());
                }
                if (!parsed.certificateAuthority)
                {
                    return std::unexpected(ParseError());
                }
                parsed.hasPathLengthConstraint = true;
                parsed.pathLengthConstraint    = pathLength.value();
            }

            if (!sequence.value().IsAtEnd())
            {
                return std::unexpected(ParseError());
            }

            basicConstraints = parsed;
            return {};
        }

        [[nodiscard]] CryptoExpected<void> ParseSubjectKeyIdentifierExtension(
                ConstByteSpan encodedKeyIdentifier, Certificate& certificate)
        {
            DerReader reader {encodedKeyIdentifier};
            auto      element = reader.ReadElement();
            if (!element.has_value())
            {
                return std::unexpected(std::move(element).error());
            }
            if (!reader.IsAtEnd())
            {
                return std::unexpected(ParseError());
            }

            auto keyIdentifier = NGIN::Crypto::Encoding::ReadDerOctetString(element.value());
            if (!keyIdentifier.has_value())
            {
                return std::unexpected(std::move(keyIdentifier).error());
            }
            if (keyIdentifier.value().empty())
            {
                return std::unexpected(ParseError());
            }

            certificate.subjectKeyIdentifier    = CopyBytes(keyIdentifier.value());
            certificate.hasSubjectKeyIdentifier = true;
            return {};
        }

        [[nodiscard]] CryptoExpected<void> ParseAuthorityKeyIdentifierExtension(
                ConstByteSpan encodedAuthorityKeyIdentifier, Certificate& certificate)
        {
            DerReader reader {encodedAuthorityKeyIdentifier};
            auto      top = reader.ReadElement();
            if (!top.has_value())
            {
                return std::unexpected(std::move(top).error());
            }
            if (!reader.IsAtEnd())
            {
                return std::unexpected(ParseError());
            }

            DerReader parent {top.value().encoded};
            auto      sequence = NGIN::Crypto::Encoding::ReadDerSequence(parent, top.value());
            if (!sequence.has_value())
            {
                return std::unexpected(std::move(sequence).error());
            }

            while (!sequence.value().IsAtEnd())
            {
                auto field = sequence.value().ReadElement();
                if (!field.has_value())
                {
                    return std::unexpected(std::move(field).error());
                }

                if (IsTag(field.value(), DerTagClass::ContextSpecific, false, 0))
                {
                    if (certificate.hasAuthorityKeyIdentifier || field.value().value.empty())
                    {
                        return std::unexpected(ParseError());
                    }
                    certificate.authorityKeyIdentifier    = CopyBytes(field.value().value);
                    certificate.hasAuthorityKeyIdentifier = true;
                }
                else if (IsTag(field.value(), DerTagClass::ContextSpecific, true, 1))
                {
                    DerReader namesReader {field.value().value};
                    while (!namesReader.IsAtEnd())
                    {
                        auto generalName = namesReader.ReadElement();
                        if (!generalName.has_value())
                        {
                            return std::unexpected(std::move(generalName).error());
                        }
                        auto schema = ValidateGeneralNameSchema(generalName.value());
                        if (!schema.has_value())
                        {
                            return std::unexpected(std::move(schema).error());
                        }
                    }
                }
                else if (IsTag(field.value(), DerTagClass::ContextSpecific, false, 2))
                {
                    DerElement serialElement {
                            .tag     = NGIN::Crypto::Encoding::MakeDerUniversalTag(DerUniversalTag::Integer),
                            .value   = field.value().value,
                            .encoded = field.value().value,
                    };
                    auto serial = NGIN::Crypto::Encoding::ReadDerInteger(serialElement);
                    if (!serial.has_value())
                    {
                        return std::unexpected(std::move(serial).error());
                    }
                }
                else
                {
                    return std::unexpected(ParseError());
                }
            }

            return {};
        }

        [[nodiscard]] CryptoExpected<void> ParseExtendedKeyUsageExtension(
                ConstByteSpan encodedEku, NGIN::Containers::Vector<NGIN::Containers::Vector<NGIN::UInt32>>& usages)
        {
            DerReader reader {encodedEku};
            auto      top = reader.ReadElement();
            if (!top.has_value())
            {
                return std::unexpected(std::move(top).error());
            }
            if (!reader.IsAtEnd())
            {
                return std::unexpected(ParseError());
            }

            DerReader parent {top.value().encoded};
            auto      sequence = NGIN::Crypto::Encoding::ReadDerSequence(parent, top.value());
            if (!sequence.has_value())
            {
                return std::unexpected(std::move(sequence).error());
            }

            while (!sequence.value().IsAtEnd())
            {
                auto usage = sequence.value().ReadElement();
                if (!usage.has_value())
                {
                    return std::unexpected(std::move(usage).error());
                }
                auto oid = ReadOidElement(usage.value());
                if (!oid.has_value())
                {
                    return std::unexpected(std::move(oid).error());
                }
                usages.PushBack(std::move(oid.value()));
            }
            if (usages.Size() == 0)
            {
                return std::unexpected(ParseError());
            }

            return {};
        }

        [[nodiscard]] CryptoExpected<void> ParseExtensions(const DerElement& explicitExtensions, Certificate& certificate)
        {
            if (!IsTag(explicitExtensions, DerTagClass::ContextSpecific, true, 3))
            {
                return std::unexpected(ParseError());
            }

            DerReader wrapper {explicitExtensions.encoded};
            auto      extensionsReader = wrapper.EnterConstructed(explicitExtensions);
            if (!extensionsReader.has_value())
            {
                return std::unexpected(std::move(extensionsReader).error());
            }

            auto extensionsElement = extensionsReader.value().ReadElement();
            if (!extensionsElement.has_value())
            {
                return std::unexpected(std::move(extensionsElement).error());
            }
            if (!extensionsReader.value().IsAtEnd())
            {
                return std::unexpected(ParseError());
            }

            DerReader parent {extensionsElement.value().encoded};
            auto      extensions = NGIN::Crypto::Encoding::ReadDerSequence(parent, extensionsElement.value());
            if (!extensions.has_value())
            {
                return std::unexpected(std::move(extensions).error());
            }

            bool sawSubjectAltName         = false;
            bool sawKeyUsage               = false;
            bool sawBasicConstraints       = false;
            bool sawSubjectKeyIdentifier   = false;
            bool sawAuthorityKeyIdentifier = false;
            bool sawExtendedKeyUsage       = false;

            while (!extensions.value().IsAtEnd())
            {
                auto extensionElement = extensions.value().ReadElement();
                if (!extensionElement.has_value())
                {
                    return std::unexpected(std::move(extensionElement).error());
                }

                DerReader extensionParent {extensionElement.value().encoded};
                auto      extensionReader = NGIN::Crypto::Encoding::ReadDerSequence(extensionParent, extensionElement.value());
                if (!extensionReader.has_value())
                {
                    return std::unexpected(std::move(extensionReader).error());
                }

                auto oidElement = extensionReader.value().ReadElement();
                if (!oidElement.has_value())
                {
                    return std::unexpected(std::move(oidElement).error());
                }
                auto oid = ReadOidElement(oidElement.value());
                if (!oid.has_value())
                {
                    return std::unexpected(std::move(oid).error());
                }

                bool critical = false;
                auto next     = extensionReader.value().ReadElement();
                if (!next.has_value())
                {
                    return std::unexpected(std::move(next).error());
                }
                if (IsUniversal(next.value(), static_cast<DerUniversalTag>(1)))
                {
                    if (next.value().value.size() != 1 || next.value().value[0] != NGIN::Byte {0xff})
                    {
                        return std::unexpected(ParseError());
                    }
                    critical = true;
                    next     = extensionReader.value().ReadElement();
                    if (!next.has_value())
                    {
                        return std::unexpected(std::move(next).error());
                    }
                }

                (void) critical;

                auto extensionValue = NGIN::Crypto::Encoding::ReadDerOctetString(next.value());
                if (!extensionValue.has_value())
                {
                    return std::unexpected(std::move(extensionValue).error());
                }
                if (!extensionReader.value().IsAtEnd())
                {
                    return std::unexpected(ParseError());
                }

                if (OidEquals(oid.value(), SUBJECT_ALT_NAME_OID))
                {
                    if (sawSubjectAltName)
                    {
                        return std::unexpected(ParseError());
                    }
                    sawSubjectAltName = true;
                    auto result       = ParseSubjectAltNameExtension(extensionValue.value(), certificate.subjectAltNames);
                    if (!result.has_value())
                    {
                        return std::unexpected(std::move(result).error());
                    }
                    certificate.hasSubjectAltNames = true;
                }
                else if (OidEquals(oid.value(), KEY_USAGE_OID))
                {
                    if (sawKeyUsage)
                    {
                        return std::unexpected(ParseError());
                    }
                    sawKeyUsage = true;
                    auto result = ParseKeyUsageExtension(extensionValue.value(), certificate.keyUsage);
                    if (!result.has_value())
                    {
                        return std::unexpected(std::move(result).error());
                    }
                    certificate.hasKeyUsage = true;
                }
                else if (OidEquals(oid.value(), BASIC_CONSTRAINTS_OID))
                {
                    if (sawBasicConstraints)
                    {
                        return std::unexpected(ParseError());
                    }
                    sawBasicConstraints = true;
                    auto result         = ParseBasicConstraintsExtension(extensionValue.value(), certificate.basicConstraints);
                    if (!result.has_value())
                    {
                        return std::unexpected(std::move(result).error());
                    }
                    certificate.hasBasicConstraints = true;
                }
                else if (OidEquals(oid.value(), SUBJECT_KEY_IDENTIFIER_OID))
                {
                    if (sawSubjectKeyIdentifier)
                    {
                        return std::unexpected(ParseError());
                    }
                    sawSubjectKeyIdentifier = true;
                    auto result             = ParseSubjectKeyIdentifierExtension(extensionValue.value(), certificate);
                    if (!result.has_value())
                    {
                        return std::unexpected(std::move(result).error());
                    }
                }
                else if (OidEquals(oid.value(), AUTHORITY_KEY_IDENTIFIER_OID))
                {
                    if (sawAuthorityKeyIdentifier)
                    {
                        return std::unexpected(ParseError());
                    }
                    sawAuthorityKeyIdentifier = true;
                    auto result               = ParseAuthorityKeyIdentifierExtension(extensionValue.value(), certificate);
                    if (!result.has_value())
                    {
                        return std::unexpected(std::move(result).error());
                    }
                }
                else if (OidEquals(oid.value(), EXTENDED_KEY_USAGE_OID))
                {
                    if (sawExtendedKeyUsage)
                    {
                        return std::unexpected(ParseError());
                    }
                    sawExtendedKeyUsage = true;
                    auto result         = ParseExtendedKeyUsageExtension(extensionValue.value(), certificate.extendedKeyUsages);
                    if (!result.has_value())
                    {
                        return std::unexpected(std::move(result).error());
                    }
                }
            }

            return {};
        }
    }// namespace

    CryptoExpected<Certificate> ParseX509Certificate(ConstByteSpan der)
    {
        DerReader reader {der};
        auto      certificateElement = reader.ReadElement();
        if (!certificateElement.has_value())
        {
            return std::unexpected(std::move(certificateElement).error());
        }
        if (!reader.IsAtEnd())
        {
            return std::unexpected(ParseError());
        }

        DerReader certificateParent {certificateElement.value().encoded};
        auto      certificateSequence = NGIN::Crypto::Encoding::ReadDerSequence(certificateParent, certificateElement.value());
        if (!certificateSequence.has_value())
        {
            return std::unexpected(std::move(certificateSequence).error());
        }

        auto tbsElement = certificateSequence.value().ReadElement();
        if (!tbsElement.has_value())
        {
            return std::unexpected(std::move(tbsElement).error());
        }

        auto signatureAlgorithmElement = certificateSequence.value().ReadElement();
        if (!signatureAlgorithmElement.has_value())
        {
            return std::unexpected(std::move(signatureAlgorithmElement).error());
        }

        auto signatureValueElement = certificateSequence.value().ReadElement();
        if (!signatureValueElement.has_value())
        {
            return std::unexpected(std::move(signatureValueElement).error());
        }
        if (!certificateSequence.value().IsAtEnd())
        {
            return std::unexpected(ParseError());
        }

        Certificate certificate;
        certificate.certificateDer        = CopyBytes(certificateElement.value().encoded);
        certificate.tbsCertificateDer     = CopyBytes(tbsElement.value().encoded);
        certificate.signatureAlgorithmDer = CopyBytes(signatureAlgorithmElement.value().encoded);

        auto signatureAlgorithm = IdentifySignatureAlgorithm(signatureAlgorithmElement.value());
        if (signatureAlgorithm.has_value())
        {
            certificate.signatureAlgorithm         = signatureAlgorithm.value();
            certificate.hasKnownSignatureAlgorithm = true;
        }
        else if (signatureAlgorithm.error().Code() != CryptoErrorCode::UnsupportedAlgorithm)
        {
            return std::unexpected(std::move(signatureAlgorithm).error());
        }

        auto signatureValue = NGIN::Crypto::Encoding::ReadDerBitString(signatureValueElement.value());
        if (!signatureValue.has_value())
        {
            return std::unexpected(std::move(signatureValue).error());
        }
        if (signatureValue.value().unusedBitCount != 0)
        {
            return std::unexpected(ParseError());
        }
        certificate.signatureValue = CopyBytes(signatureValue.value().bytes);

        DerReader tbsParent {tbsElement.value().encoded};
        auto      tbsReader = NGIN::Crypto::Encoding::ReadDerSequence(tbsParent, tbsElement.value());
        if (!tbsReader.has_value())
        {
            return std::unexpected(std::move(tbsReader).error());
        }

        auto first = tbsReader.value().ReadElement();
        if (!first.has_value())
        {
            return std::unexpected(std::move(first).error());
        }

        DerElement serialElement = first.value();
        if (IsTag(first.value(), DerTagClass::ContextSpecific, true, 0))
        {
            DerReader versionWrapper {first.value().encoded};
            auto      versionReader = versionWrapper.EnterConstructed(first.value());
            if (!versionReader.has_value())
            {
                return std::unexpected(std::move(versionReader).error());
            }
            auto versionElement = versionReader.value().ReadElement();
            if (!versionElement.has_value())
            {
                return std::unexpected(std::move(versionElement).error());
            }
            if (!versionReader.value().IsAtEnd())
            {
                return std::unexpected(ParseError());
            }
            auto version = NGIN::Crypto::Encoding::ReadDerInteger(versionElement.value());
            if (!version.has_value())
            {
                return std::unexpected(std::move(version).error());
            }
            if (version.value().size() != 1 || ByteValue(version.value()[0]) > 2)
            {
                return std::unexpected(ParseError());
            }
            certificate.version = static_cast<NGIN::UInt32>(ByteValue(version.value()[0]) + 1);

            auto serial = tbsReader.value().ReadElement();
            if (!serial.has_value())
            {
                return std::unexpected(std::move(serial).error());
            }
            serialElement = serial.value();
        }

        auto serial = NGIN::Crypto::Encoding::ReadDerInteger(serialElement);
        if (!serial.has_value())
        {
            return std::unexpected(std::move(serial).error());
        }
        certificate.serialNumber = CopyBytes(serial.value());

        auto tbsSignature = tbsReader.value().ReadElement();
        if (!tbsSignature.has_value())
        {
            return std::unexpected(std::move(tbsSignature).error());
        }

        auto issuer = tbsReader.value().ReadElement();
        if (!issuer.has_value())
        {
            return std::unexpected(std::move(issuer).error());
        }
        if (!IsUniversal(issuer.value(), DerUniversalTag::Sequence, true))
        {
            return std::unexpected(ParseError());
        }
        certificate.issuerDer = CopyBytes(issuer.value().encoded);
        auto issuerName       = ParseDistinguishedName(issuer.value());
        if (issuerName.has_value())
        {
            certificate.issuer = std::move(issuerName.value());
        }

        auto validityElement = tbsReader.value().ReadElement();
        if (!validityElement.has_value())
        {
            return std::unexpected(std::move(validityElement).error());
        }
        auto validity = ParseValidity(validityElement.value());
        if (!validity.has_value())
        {
            return std::unexpected(std::move(validity).error());
        }
        certificate.validity = std::move(validity.value());

        auto subject = tbsReader.value().ReadElement();
        if (!subject.has_value())
        {
            return std::unexpected(std::move(subject).error());
        }
        if (!IsUniversal(subject.value(), DerUniversalTag::Sequence, true))
        {
            return std::unexpected(ParseError());
        }
        certificate.subjectDer = CopyBytes(subject.value().encoded);
        auto subjectName       = ParseDistinguishedName(subject.value());
        if (subjectName.has_value())
        {
            certificate.subject = std::move(subjectName.value());
        }

        auto spkiElement = tbsReader.value().ReadElement();
        if (!spkiElement.has_value())
        {
            return std::unexpected(std::move(spkiElement).error());
        }
        auto spki = NGIN::Crypto::Keys::ParseSubjectPublicKeyInfo(spkiElement.value().encoded);
        if (!spki.has_value())
        {
            return std::unexpected(std::move(spki).error());
        }
        certificate.subjectPublicKeyInfo = std::move(spki.value());
        certificate.publicKeyAlgorithm   = certificate.subjectPublicKeyInfo.algorithm;

        while (!tbsReader.value().IsAtEnd())
        {
            auto optional = tbsReader.value().ReadElement();
            if (!optional.has_value())
            {
                return std::unexpected(std::move(optional).error());
            }
            if (IsTag(optional.value(), DerTagClass::ContextSpecific, true, 3))
            {
                auto extensions = ParseExtensions(optional.value(), certificate);
                if (!extensions.has_value())
                {
                    return std::unexpected(std::move(extensions).error());
                }
            }
            else if (!IsTag(optional.value(), DerTagClass::ContextSpecific, false, 1) &&
                     !IsTag(optional.value(), DerTagClass::ContextSpecific, false, 2))
            {
                return std::unexpected(ParseError());
            }
        }

        (void) tbsSignature;

        return certificate;
    }

    CryptoExpected<void> VerifyCertificateSignature(
            const NGIN::Crypto::Backend::CryptoContext&     context,
            const Certificate&                              certificate,
            const NGIN::Crypto::Keys::SubjectPublicKeyInfo& issuerPublicKey) noexcept
    {
        if (!certificate.hasKnownSignatureAlgorithm)
        {
            return std::unexpected(UnsupportedAlgorithm());
        }

        return NGIN::Crypto::Signatures::Verify(
                context,
                certificate.signatureAlgorithm,
                NGIN::Crypto::Signatures::VerifyInput {
                        .publicKey = ConstByteSpan {issuerPublicKey.publicKey.data(), issuerPublicKey.publicKey.Size()},
                        .message   = ConstByteSpan {
                                certificate.tbsCertificateDer.data(),
                                certificate.tbsCertificateDer.Size(),
                        },
                        .signature = ConstByteSpan {certificate.signatureValue.data(), certificate.signatureValue.Size()},
                });
    }
}// namespace NGIN::Crypto::Certificates
