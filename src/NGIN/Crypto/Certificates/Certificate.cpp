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

        [[nodiscard]] bool IsIa5StringValue(ConstByteSpan value) noexcept
        {
            for (auto byte: value)
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
            if (!integer.HasValue())
            {
                return integer.Error();
            }
            if (integer.Value().empty() || (ByteValue(integer.Value()[0]) & 0x80u) != 0)
            {
                return ParseError();
            }

            NGIN::UIntSize offset = 0;
            if (integer.Value().size() > 1 && ByteValue(integer.Value()[0]) == 0x00u)
            {
                offset = 1;
            }
            if (integer.Value().size() - offset > sizeof(NGIN::UInt32))
            {
                return ParseError();
            }

            NGIN::UInt32 value = 0;
            for (NGIN::UIntSize i = offset; i < integer.Value().size(); ++i)
            {
                value = static_cast<NGIN::UInt32>((value << 8u) | ByteValue(integer.Value()[i]));
            }
            return value;
        }

        [[nodiscard]] CryptoExpected<std::string> ReadTimeString(const DerElement& element)
        {
            if (!IsUniversal(element, static_cast<DerUniversalTag>(23)) && !IsUniversal(element, static_cast<DerUniversalTag>(24)))
            {
                return ParseError();
            }

            std::string value;
            value.reserve(element.value.size());
            for (auto byte: element.value)
            {
                value.push_back(static_cast<char>(ByteValue(byte)));
            }

            return value;
        }

        [[nodiscard]] CryptoExpected<CertificateValidity> ParseValidity(const DerElement& element)
        {
            DerReader parent {element.encoded};
            auto      reader = NGIN::Crypto::Encoding::ReadDerSequence(parent, element);
            if (!reader.HasValue())
            {
                return reader.Error();
            }

            auto notBeforeElement = reader.Value().ReadElement();
            if (!notBeforeElement.HasValue())
            {
                return notBeforeElement.Error();
            }
            auto notBefore = ReadTimeString(notBeforeElement.Value());
            if (!notBefore.HasValue())
            {
                return notBefore.Error();
            }

            auto notAfterElement = reader.Value().ReadElement();
            if (!notAfterElement.HasValue())
            {
                return notAfterElement.Error();
            }
            auto notAfter = ReadTimeString(notAfterElement.Value());
            if (!notAfter.HasValue())
            {
                return notAfter.Error();
            }
            if (!reader.Value().IsAtEnd())
            {
                return ParseError();
            }

            return CertificateValidity {
                    .notBefore = std::move(notBefore.Value()),
                    .notAfter  = std::move(notAfter.Value()),
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
                return ParseError();
            }

            std::string value;
            if (element.tag.number == 30)
            {
                if ((element.value.size() % 2) != 0)
                {
                    return ParseError();
                }
                value.reserve(element.value.size() / 2);
                for (NGIN::UIntSize i = 0; i < element.value.size(); i += 2)
                {
                    if (ByteValue(element.value[i]) != 0)
                    {
                        return ParseError();
                    }
                    value.push_back(static_cast<char>(ByteValue(element.value[i + 1])));
                }
                return value;
            }

            if (element.tag.number != 12 && element.tag.number != 19 && element.tag.number != 20 &&
                element.tag.number != 22)
            {
                return ParseError();
            }

            value.reserve(element.value.size());
            for (auto byte: element.value)
            {
                value.push_back(static_cast<char>(ByteValue(byte)));
            }
            return value;
        }

        [[nodiscard]] CryptoExpected<DistinguishedName> ParseDistinguishedName(const DerElement& element)
        {
            DerReader parent {element.encoded};
            auto      name = NGIN::Crypto::Encoding::ReadDerSequence(parent, element);
            if (!name.HasValue())
            {
                return name.Error();
            }

            DistinguishedName result;
            while (!name.Value().IsAtEnd())
            {
                auto rdnSetElement = name.Value().ReadElement();
                if (!rdnSetElement.HasValue())
                {
                    return rdnSetElement.Error();
                }
                if (!IsUniversal(rdnSetElement.Value(), DerUniversalTag::Set, true))
                {
                    return ParseError();
                }

                DerReader setParent {rdnSetElement.Value().encoded};
                auto      rdnSet = NGIN::Crypto::Encoding::ReadDerSet(setParent, rdnSetElement.Value());
                if (!rdnSet.HasValue())
                {
                    return rdnSet.Error();
                }

                while (!rdnSet.Value().IsAtEnd())
                {
                    auto attributeElement = rdnSet.Value().ReadElement();
                    if (!attributeElement.HasValue())
                    {
                        return attributeElement.Error();
                    }

                    DerReader attributeParent {attributeElement.Value().encoded};
                    auto      attribute = NGIN::Crypto::Encoding::ReadDerSequence(attributeParent, attributeElement.Value());
                    if (!attribute.HasValue())
                    {
                        return attribute.Error();
                    }

                    auto oidElement = attribute.Value().ReadElement();
                    if (!oidElement.HasValue())
                    {
                        return oidElement.Error();
                    }
                    auto oid = NGIN::Crypto::Encoding::ReadDerObjectIdentifier(oidElement.Value());
                    if (!oid.HasValue())
                    {
                        return oid.Error();
                    }

                    auto valueElement = attribute.Value().ReadElement();
                    if (!valueElement.HasValue())
                    {
                        return valueElement.Error();
                    }
                    if (!attribute.Value().IsAtEnd())
                    {
                        return ParseError();
                    }

                    auto value = ReadNameValueString(valueElement.Value());
                    if (!value.HasValue())
                    {
                        return value.Error();
                    }

                    result.attributes.PushBack(DistinguishedNameAttribute {
                            .type             = IdentifyNameAttribute(oid.Value()),
                            .objectIdentifier = std::move(oid.Value()),
                            .value            = std::move(value.Value()),
                            .valueTag         = valueElement.Value().tag.number,
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
            if (!reader.HasValue())
            {
                return reader.Error();
            }

            auto oidElement = reader.Value().ReadElement();
            if (!oidElement.HasValue())
            {
                return oidElement.Error();
            }
            auto oid = ReadOidElement(oidElement.Value());
            if (!oid.HasValue())
            {
                return oid.Error();
            }

            if (OidEquals(oid.Value(), ED25519_OID))
            {
                if (!reader.Value().IsAtEnd())
                {
                    return ParseError();
                }
                return NGIN::Crypto::SignatureAlgorithm::Ed25519;
            }

            if (OidEquals(oid.Value(), ECDSA_WITH_SHA256_OID))
            {
                if (!reader.Value().IsAtEnd())
                {
                    return ParseError();
                }
                return NGIN::Crypto::SignatureAlgorithm::EcdsaP256Sha256;
            }

            if (OidEquals(oid.Value(), RSA_PSS_OID))
            {
                return NGIN::Crypto::SignatureAlgorithm::RsaPssSha256;
            }

            return UnsupportedAlgorithm();
        }

        [[nodiscard]] CryptoExpected<void> ValidateGeneralNameSchema(const DerElement& generalName)
        {
            if (generalName.tag.tagClass != DerTagClass::ContextSpecific)
            {
                return ParseError();
            }

            switch (generalName.tag.number)
            {
                case 0:
                case 3:
                case 5:
                    return generalName.tag.constructed && !generalName.value.empty() ? CryptoExpected<void> {}
                                                                                     : ParseError();
                case 1:
                case 2:
                case 6:
                    return !generalName.tag.constructed && !generalName.value.empty() && IsIa5StringValue(generalName.value)
                                   ? CryptoExpected<void> {}
                                   : ParseError();
                case 4:
                {
                    if (!generalName.tag.constructed)
                    {
                        return ParseError();
                    }
                    auto nameElement = ReadSingleElement(generalName.value);
                    if (!nameElement.HasValue())
                    {
                        return nameElement.Error();
                    }
                    auto name = ParseDistinguishedName(nameElement.Value());
                    return name.HasValue() ? CryptoExpected<void> {} : name.Error();
                }
                case 7:
                    return !generalName.tag.constructed &&
                                           (generalName.value.size() == 4 || generalName.value.size() == 16)
                                   ? CryptoExpected<void> {}
                                   : ParseError();
                case 8:
                {
                    if (generalName.tag.constructed || generalName.value.empty())
                    {
                        return ParseError();
                    }
                    DerElement oidElement {
                            .tag     = NGIN::Crypto::Encoding::MakeDerUniversalTag(DerUniversalTag::ObjectIdentifier),
                            .value   = generalName.value,
                            .encoded = generalName.value,
                    };
                    auto oid = NGIN::Crypto::Encoding::ReadDerObjectIdentifier(oidElement);
                    return oid.HasValue() ? CryptoExpected<void> {} : oid.Error();
                }
                default:
                    return ParseError();
            }
        }

        [[nodiscard]] CryptoExpected<void> ParseSubjectAltNameExtension(ConstByteSpan encodedNames, SubjectAltNames& names)
        {
            DerReader reader {encodedNames};
            auto      top = reader.ReadElement();
            if (!top.HasValue())
            {
                return top.Error();
            }
            if (!reader.IsAtEnd())
            {
                return ParseError();
            }

            DerReader parent {top.Value().encoded};
            auto      generalNames = NGIN::Crypto::Encoding::ReadDerSequence(parent, top.Value());
            if (!generalNames.HasValue())
            {
                return generalNames.Error();
            }

            bool hasAnyGeneralName = false;
            while (!generalNames.Value().IsAtEnd())
            {
                auto generalName = generalNames.Value().ReadElement();
                if (!generalName.HasValue())
                {
                    return generalName.Error();
                }
                hasAnyGeneralName = true;
                auto schema = ValidateGeneralNameSchema(generalName.Value());
                if (!schema.HasValue())
                {
                    return schema.Error();
                }

                if (IsTag(generalName.Value(), DerTagClass::ContextSpecific, false, 1) ||
                    IsTag(generalName.Value(), DerTagClass::ContextSpecific, false, 2))
                {
                    std::string text;
                    text.reserve(generalName.Value().value.size());
                    for (auto byte: generalName.Value().value)
                    {
                        text.push_back(static_cast<char>(ByteValue(byte)));
                    }

                    if (generalName.Value().tag.number == 1)
                    {
                        names.emailAddresses.PushBack(std::move(text));
                    }
                    else
                    {
                        names.dnsNames.PushBack(std::move(text));
                    }
                }
                else if (IsTag(generalName.Value(), DerTagClass::ContextSpecific, false, 7))
                {
                    names.ipAddresses.PushBack(CopyBytes(generalName.Value().value));
                }
            }

            if (!hasAnyGeneralName)
            {
                return ParseError();
            }

            return {};
        }

        [[nodiscard]] CryptoExpected<void> ParseKeyUsageExtension(ConstByteSpan encodedKeyUsage, KeyUsage& keyUsage)
        {
            DerReader reader {encodedKeyUsage};
            auto      element = reader.ReadElement();
            if (!element.HasValue())
            {
                return element.Error();
            }
            if (!reader.IsAtEnd())
            {
                return ParseError();
            }

            auto bits = NGIN::Crypto::Encoding::ReadDerBitString(element.Value());
            if (!bits.HasValue())
            {
                return bits.Error();
            }
            if (bits.Value().bytes.empty() || bits.Value().bytes.size() > 2)
            {
                return ParseError();
            }
            const auto usedBits = (bits.Value().bytes.size() * 8u) - bits.Value().unusedBitCount;
            if (usedBits == 0 || usedBits > 9)
            {
                return ParseError();
            }

            keyUsage.unusedBitCount = bits.Value().unusedBitCount;
            keyUsage.bits           = CopyBytes(bits.Value().bytes);
            return {};
        }

        [[nodiscard]] CryptoExpected<void> ParseBasicConstraintsExtension(
                ConstByteSpan encodedBasicConstraints, BasicConstraints& basicConstraints)
        {
            DerReader reader {encodedBasicConstraints};
            auto      top = reader.ReadElement();
            if (!top.HasValue())
            {
                return top.Error();
            }
            if (!reader.IsAtEnd())
            {
                return ParseError();
            }

            DerReader parent {top.Value().encoded};
            auto      sequence = NGIN::Crypto::Encoding::ReadDerSequence(parent, top.Value());
            if (!sequence.HasValue())
            {
                return sequence.Error();
            }

            BasicConstraints parsed;
            if (!sequence.Value().IsAtEnd())
            {
                auto first = sequence.Value().ReadElement();
                if (!first.HasValue())
                {
                    return first.Error();
                }

                if (IsUniversal(first.Value(), DerUniversalTag::Boolean))
                {
                    if (first.Value().value.size() != 1 || first.Value().value[0] != NGIN::Byte {0xff})
                    {
                        return ParseError();
                    }
                    parsed.certificateAuthority = true;
                }
                else if (IsUniversal(first.Value(), DerUniversalTag::Integer))
                {
                    return ParseError();
                }
                else
                {
                    return ParseError();
                }
            }

            if (!sequence.Value().IsAtEnd())
            {
                auto pathLengthElement = sequence.Value().ReadElement();
                if (!pathLengthElement.HasValue())
                {
                    return pathLengthElement.Error();
                }
                auto pathLength = ReadUInt32Integer(pathLengthElement.Value());
                if (!pathLength.HasValue())
                {
                    return pathLength.Error();
                }
                if (!parsed.certificateAuthority)
                {
                    return ParseError();
                }
                parsed.hasPathLengthConstraint = true;
                parsed.pathLengthConstraint    = pathLength.Value();
            }

            if (!sequence.Value().IsAtEnd())
            {
                return ParseError();
            }

            basicConstraints = parsed;
            return {};
        }

        [[nodiscard]] CryptoExpected<void> ParseSubjectKeyIdentifierExtension(
                ConstByteSpan encodedKeyIdentifier, Certificate& certificate)
        {
            DerReader reader {encodedKeyIdentifier};
            auto      element = reader.ReadElement();
            if (!element.HasValue())
            {
                return element.Error();
            }
            if (!reader.IsAtEnd())
            {
                return ParseError();
            }

            auto keyIdentifier = NGIN::Crypto::Encoding::ReadDerOctetString(element.Value());
            if (!keyIdentifier.HasValue())
            {
                return keyIdentifier.Error();
            }
            if (keyIdentifier.Value().empty())
            {
                return ParseError();
            }

            certificate.subjectKeyIdentifier    = CopyBytes(keyIdentifier.Value());
            certificate.hasSubjectKeyIdentifier = true;
            return {};
        }

        [[nodiscard]] CryptoExpected<void> ParseAuthorityKeyIdentifierExtension(
                ConstByteSpan encodedAuthorityKeyIdentifier, Certificate& certificate)
        {
            DerReader reader {encodedAuthorityKeyIdentifier};
            auto      top = reader.ReadElement();
            if (!top.HasValue())
            {
                return top.Error();
            }
            if (!reader.IsAtEnd())
            {
                return ParseError();
            }

            DerReader parent {top.Value().encoded};
            auto      sequence = NGIN::Crypto::Encoding::ReadDerSequence(parent, top.Value());
            if (!sequence.HasValue())
            {
                return sequence.Error();
            }

            while (!sequence.Value().IsAtEnd())
            {
                auto field = sequence.Value().ReadElement();
                if (!field.HasValue())
                {
                    return field.Error();
                }

                if (IsTag(field.Value(), DerTagClass::ContextSpecific, false, 0))
                {
                    if (certificate.hasAuthorityKeyIdentifier || field.Value().value.empty())
                    {
                        return ParseError();
                    }
                    certificate.authorityKeyIdentifier    = CopyBytes(field.Value().value);
                    certificate.hasAuthorityKeyIdentifier = true;
                }
                else if (IsTag(field.Value(), DerTagClass::ContextSpecific, true, 1))
                {
                    DerReader namesReader {field.Value().value};
                    while (!namesReader.IsAtEnd())
                    {
                        auto generalName = namesReader.ReadElement();
                        if (!generalName.HasValue())
                        {
                            return generalName.Error();
                        }
                        auto schema = ValidateGeneralNameSchema(generalName.Value());
                        if (!schema.HasValue())
                        {
                            return schema.Error();
                        }
                    }
                }
                else if (IsTag(field.Value(), DerTagClass::ContextSpecific, false, 2))
                {
                    DerElement serialElement {
                            .tag     = NGIN::Crypto::Encoding::MakeDerUniversalTag(DerUniversalTag::Integer),
                            .value   = field.Value().value,
                            .encoded = field.Value().value,
                    };
                    auto serial = NGIN::Crypto::Encoding::ReadDerInteger(serialElement);
                    if (!serial.HasValue())
                    {
                        return serial.Error();
                    }
                }
                else
                {
                    return ParseError();
                }
            }

            return {};
        }

        [[nodiscard]] CryptoExpected<void> ParseExtendedKeyUsageExtension(
                ConstByteSpan encodedEku, NGIN::Containers::Vector<NGIN::Containers::Vector<NGIN::UInt32>>& usages)
        {
            DerReader reader {encodedEku};
            auto      top = reader.ReadElement();
            if (!top.HasValue())
            {
                return top.Error();
            }
            if (!reader.IsAtEnd())
            {
                return ParseError();
            }

            DerReader parent {top.Value().encoded};
            auto      sequence = NGIN::Crypto::Encoding::ReadDerSequence(parent, top.Value());
            if (!sequence.HasValue())
            {
                return sequence.Error();
            }

            while (!sequence.Value().IsAtEnd())
            {
                auto usage = sequence.Value().ReadElement();
                if (!usage.HasValue())
                {
                    return usage.Error();
                }
                auto oid = ReadOidElement(usage.Value());
                if (!oid.HasValue())
                {
                    return oid.Error();
                }
                usages.PushBack(std::move(oid.Value()));
            }
            if (usages.Size() == 0)
            {
                return ParseError();
            }

            return {};
        }

        [[nodiscard]] CryptoExpected<void> ParseExtensions(const DerElement& explicitExtensions, Certificate& certificate)
        {
            if (!IsTag(explicitExtensions, DerTagClass::ContextSpecific, true, 3))
            {
                return ParseError();
            }

            DerReader wrapper {explicitExtensions.encoded};
            auto      extensionsReader = wrapper.EnterConstructed(explicitExtensions);
            if (!extensionsReader.HasValue())
            {
                return extensionsReader.Error();
            }

            auto extensionsElement = extensionsReader.Value().ReadElement();
            if (!extensionsElement.HasValue())
            {
                return extensionsElement.Error();
            }
            if (!extensionsReader.Value().IsAtEnd())
            {
                return ParseError();
            }

            DerReader parent {extensionsElement.Value().encoded};
            auto      extensions = NGIN::Crypto::Encoding::ReadDerSequence(parent, extensionsElement.Value());
            if (!extensions.HasValue())
            {
                return extensions.Error();
            }

            bool sawSubjectAltName         = false;
            bool sawKeyUsage               = false;
            bool sawBasicConstraints        = false;
            bool sawSubjectKeyIdentifier   = false;
            bool sawAuthorityKeyIdentifier = false;
            bool sawExtendedKeyUsage       = false;

            while (!extensions.Value().IsAtEnd())
            {
                auto extensionElement = extensions.Value().ReadElement();
                if (!extensionElement.HasValue())
                {
                    return extensionElement.Error();
                }

                DerReader extensionParent {extensionElement.Value().encoded};
                auto      extensionReader = NGIN::Crypto::Encoding::ReadDerSequence(extensionParent, extensionElement.Value());
                if (!extensionReader.HasValue())
                {
                    return extensionReader.Error();
                }

                auto oidElement = extensionReader.Value().ReadElement();
                if (!oidElement.HasValue())
                {
                    return oidElement.Error();
                }
                auto oid = ReadOidElement(oidElement.Value());
                if (!oid.HasValue())
                {
                    return oid.Error();
                }

                bool critical = false;
                auto next     = extensionReader.Value().ReadElement();
                if (!next.HasValue())
                {
                    return next.Error();
                }
                if (IsUniversal(next.Value(), static_cast<DerUniversalTag>(1)))
                {
                    if (next.Value().value.size() != 1 || next.Value().value[0] != NGIN::Byte {0xff})
                    {
                        return ParseError();
                    }
                    critical = true;
                    next     = extensionReader.Value().ReadElement();
                    if (!next.HasValue())
                    {
                        return next.Error();
                    }
                }

                (void) critical;

                auto extensionValue = NGIN::Crypto::Encoding::ReadDerOctetString(next.Value());
                if (!extensionValue.HasValue())
                {
                    return extensionValue.Error();
                }
                if (!extensionReader.Value().IsAtEnd())
                {
                    return ParseError();
                }

                if (OidEquals(oid.Value(), SUBJECT_ALT_NAME_OID))
                {
                    if (sawSubjectAltName)
                    {
                        return ParseError();
                    }
                    sawSubjectAltName = true;
                    auto result = ParseSubjectAltNameExtension(extensionValue.Value(), certificate.subjectAltNames);
                    if (!result.HasValue())
                    {
                        return result.Error();
                    }
                    certificate.hasSubjectAltNames = true;
                }
                else if (OidEquals(oid.Value(), KEY_USAGE_OID))
                {
                    if (sawKeyUsage)
                    {
                        return ParseError();
                    }
                    sawKeyUsage = true;
                    auto result = ParseKeyUsageExtension(extensionValue.Value(), certificate.keyUsage);
                    if (!result.HasValue())
                    {
                        return result.Error();
                    }
                    certificate.hasKeyUsage = true;
                }
                else if (OidEquals(oid.Value(), BASIC_CONSTRAINTS_OID))
                {
                    if (sawBasicConstraints)
                    {
                        return ParseError();
                    }
                    sawBasicConstraints = true;
                    auto result = ParseBasicConstraintsExtension(extensionValue.Value(), certificate.basicConstraints);
                    if (!result.HasValue())
                    {
                        return result.Error();
                    }
                    certificate.hasBasicConstraints = true;
                }
                else if (OidEquals(oid.Value(), SUBJECT_KEY_IDENTIFIER_OID))
                {
                    if (sawSubjectKeyIdentifier)
                    {
                        return ParseError();
                    }
                    sawSubjectKeyIdentifier = true;
                    auto result = ParseSubjectKeyIdentifierExtension(extensionValue.Value(), certificate);
                    if (!result.HasValue())
                    {
                        return result.Error();
                    }
                }
                else if (OidEquals(oid.Value(), AUTHORITY_KEY_IDENTIFIER_OID))
                {
                    if (sawAuthorityKeyIdentifier)
                    {
                        return ParseError();
                    }
                    sawAuthorityKeyIdentifier = true;
                    auto result = ParseAuthorityKeyIdentifierExtension(extensionValue.Value(), certificate);
                    if (!result.HasValue())
                    {
                        return result.Error();
                    }
                }
                else if (OidEquals(oid.Value(), EXTENDED_KEY_USAGE_OID))
                {
                    if (sawExtendedKeyUsage)
                    {
                        return ParseError();
                    }
                    sawExtendedKeyUsage = true;
                    auto result = ParseExtendedKeyUsageExtension(extensionValue.Value(), certificate.extendedKeyUsages);
                    if (!result.HasValue())
                    {
                        return result.Error();
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
        if (!certificateElement.HasValue())
        {
            return certificateElement.Error();
        }
        if (!reader.IsAtEnd())
        {
            return ParseError();
        }

        DerReader certificateParent {certificateElement.Value().encoded};
        auto      certificateSequence = NGIN::Crypto::Encoding::ReadDerSequence(certificateParent, certificateElement.Value());
        if (!certificateSequence.HasValue())
        {
            return certificateSequence.Error();
        }

        auto tbsElement = certificateSequence.Value().ReadElement();
        if (!tbsElement.HasValue())
        {
            return tbsElement.Error();
        }

        auto signatureAlgorithmElement = certificateSequence.Value().ReadElement();
        if (!signatureAlgorithmElement.HasValue())
        {
            return signatureAlgorithmElement.Error();
        }

        auto signatureValueElement = certificateSequence.Value().ReadElement();
        if (!signatureValueElement.HasValue())
        {
            return signatureValueElement.Error();
        }
        if (!certificateSequence.Value().IsAtEnd())
        {
            return ParseError();
        }

        Certificate certificate;
        certificate.tbsCertificateDer     = CopyBytes(tbsElement.Value().encoded);
        certificate.signatureAlgorithmDer = CopyBytes(signatureAlgorithmElement.Value().encoded);

        auto signatureAlgorithm = IdentifySignatureAlgorithm(signatureAlgorithmElement.Value());
        if (signatureAlgorithm.HasValue())
        {
            certificate.signatureAlgorithm         = signatureAlgorithm.Value();
            certificate.hasKnownSignatureAlgorithm = true;
        }
        else if (signatureAlgorithm.Error().Code() != CryptoErrorCode::UnsupportedAlgorithm)
        {
            return signatureAlgorithm.Error();
        }

        auto signatureValue = NGIN::Crypto::Encoding::ReadDerBitString(signatureValueElement.Value());
        if (!signatureValue.HasValue())
        {
            return signatureValue.Error();
        }
        if (signatureValue.Value().unusedBitCount != 0)
        {
            return ParseError();
        }
        certificate.signatureValue = CopyBytes(signatureValue.Value().bytes);

        DerReader tbsParent {tbsElement.Value().encoded};
        auto      tbsReader = NGIN::Crypto::Encoding::ReadDerSequence(tbsParent, tbsElement.Value());
        if (!tbsReader.HasValue())
        {
            return tbsReader.Error();
        }

        auto first = tbsReader.Value().ReadElement();
        if (!first.HasValue())
        {
            return first.Error();
        }

        DerElement serialElement = first.Value();
        if (IsTag(first.Value(), DerTagClass::ContextSpecific, true, 0))
        {
            DerReader versionWrapper {first.Value().encoded};
            auto      versionReader = versionWrapper.EnterConstructed(first.Value());
            if (!versionReader.HasValue())
            {
                return versionReader.Error();
            }
            auto versionElement = versionReader.Value().ReadElement();
            if (!versionElement.HasValue())
            {
                return versionElement.Error();
            }
            if (!versionReader.Value().IsAtEnd())
            {
                return ParseError();
            }
            auto version = NGIN::Crypto::Encoding::ReadDerInteger(versionElement.Value());
            if (!version.HasValue())
            {
                return version.Error();
            }
            if (version.Value().size() != 1 || ByteValue(version.Value()[0]) > 2)
            {
                return ParseError();
            }
            certificate.version = static_cast<NGIN::UInt32>(ByteValue(version.Value()[0]) + 1);

            auto serial = tbsReader.Value().ReadElement();
            if (!serial.HasValue())
            {
                return serial.Error();
            }
            serialElement = serial.Value();
        }

        auto serial = NGIN::Crypto::Encoding::ReadDerInteger(serialElement);
        if (!serial.HasValue())
        {
            return serial.Error();
        }
        certificate.serialNumber = CopyBytes(serial.Value());

        auto tbsSignature = tbsReader.Value().ReadElement();
        if (!tbsSignature.HasValue())
        {
            return tbsSignature.Error();
        }

        auto issuer = tbsReader.Value().ReadElement();
        if (!issuer.HasValue())
        {
            return issuer.Error();
        }
        if (!IsUniversal(issuer.Value(), DerUniversalTag::Sequence, true))
        {
            return ParseError();
        }
        certificate.issuerDer = CopyBytes(issuer.Value().encoded);
        auto issuerName       = ParseDistinguishedName(issuer.Value());
        if (issuerName.HasValue())
        {
            certificate.issuer = std::move(issuerName.Value());
        }

        auto validityElement = tbsReader.Value().ReadElement();
        if (!validityElement.HasValue())
        {
            return validityElement.Error();
        }
        auto validity = ParseValidity(validityElement.Value());
        if (!validity.HasValue())
        {
            return validity.Error();
        }
        certificate.validity = std::move(validity.Value());

        auto subject = tbsReader.Value().ReadElement();
        if (!subject.HasValue())
        {
            return subject.Error();
        }
        if (!IsUniversal(subject.Value(), DerUniversalTag::Sequence, true))
        {
            return ParseError();
        }
        certificate.subjectDer = CopyBytes(subject.Value().encoded);
        auto subjectName       = ParseDistinguishedName(subject.Value());
        if (subjectName.HasValue())
        {
            certificate.subject = std::move(subjectName.Value());
        }

        auto spkiElement = tbsReader.Value().ReadElement();
        if (!spkiElement.HasValue())
        {
            return spkiElement.Error();
        }
        auto spki = NGIN::Crypto::Keys::ParseSubjectPublicKeyInfo(spkiElement.Value().encoded);
        if (!spki.HasValue())
        {
            return spki.Error();
        }
        certificate.subjectPublicKeyInfo = std::move(spki.Value());
        certificate.publicKeyAlgorithm   = certificate.subjectPublicKeyInfo.algorithm;

        while (!tbsReader.Value().IsAtEnd())
        {
            auto optional = tbsReader.Value().ReadElement();
            if (!optional.HasValue())
            {
                return optional.Error();
            }
            if (IsTag(optional.Value(), DerTagClass::ContextSpecific, true, 3))
            {
                auto extensions = ParseExtensions(optional.Value(), certificate);
                if (!extensions.HasValue())
                {
                    return extensions.Error();
                }
            }
            else if (!IsTag(optional.Value(), DerTagClass::ContextSpecific, false, 1) &&
                     !IsTag(optional.Value(), DerTagClass::ContextSpecific, false, 2))
            {
                return ParseError();
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
            return UnsupportedAlgorithm();
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
