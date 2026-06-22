#include <NGIN/Crypto/Encoding/Der.hpp>

#include <NGIN/Crypto/Errors/CryptoError.hpp>

#include <cstddef>
#include <limits>

namespace NGIN::Crypto::Encoding
{
    namespace
    {
        [[nodiscard]] constexpr CryptoError ParseError() noexcept
        {
            return CryptoError {CryptoErrorCode::ParseError};
        }

        [[nodiscard]] constexpr CryptoError InvalidArgument() noexcept
        {
            return CryptoError {CryptoErrorCode::InvalidArgument};
        }

        [[nodiscard]] constexpr NGIN::UInt8 ByteValue(NGIN::Byte byte) noexcept
        {
            return std::to_integer<NGIN::UInt8>(byte);
        }

        void AppendByte(ByteBuffer& output, NGIN::UInt8 value)
        {
            output.PushBack(static_cast<NGIN::Byte>(value));
        }

        void AppendBytes(ByteBuffer& output, ConstByteSpan bytes)
        {
            for (auto byte: bytes)
            {
                output.PushBack(byte);
            }
        }

        [[nodiscard]] constexpr NGIN::UInt8 EncodeTagClass(DerTagClass tagClass) noexcept
        {
            return static_cast<NGIN::UInt8>(static_cast<NGIN::UInt8>(tagClass) << 6u);
        }

        [[nodiscard]] constexpr DerTagClass DecodeTagClass(NGIN::UInt8 identifier) noexcept
        {
            return static_cast<DerTagClass>((identifier >> 6u) & 0x03u);
        }

        [[nodiscard]] bool IsMinimalInteger(ConstByteSpan value) noexcept
        {
            if (value.empty())
            {
                return false;
            }

            if (value.size() <= 1)
            {
                return true;
            }

            const auto first  = ByteValue(value[0]);
            const auto second = ByteValue(value[1]);
            if (first == 0x00u && (second & 0x80u) == 0)
            {
                return false;
            }
            if (first == 0xffu && (second & 0x80u) != 0)
            {
                return false;
            }

            return true;
        }

        [[nodiscard]] bool IsValidBitStringValue(NGIN::UInt8 unusedBitCount, ConstByteSpan bytes) noexcept
        {
            if (unusedBitCount > 7)
            {
                return false;
            }
            if (bytes.empty())
            {
                return unusedBitCount == 0;
            }
            if (unusedBitCount == 0)
            {
                return true;
            }

            const auto mask = static_cast<NGIN::UInt8>((1u << unusedBitCount) - 1u);
            return (ByteValue(bytes[bytes.size() - 1]) & mask) == 0;
        }

        [[nodiscard]] CryptoExpected<NGIN::UInt64> ReadBase128Integer(ConstByteSpan input, NGIN::UIntSize& offset) noexcept
        {
            if (offset >= input.size())
            {
                return ParseError();
            }

            NGIN::UInt64 value        = 0;
            bool         firstOctet   = true;
            bool         hasMoreBytes = true;
            while (offset < input.size() && hasMoreBytes)
            {
                const auto octet = ByteValue(input[offset++]);
                hasMoreBytes     = (octet & 0x80u) != 0;

                if (firstOctet && hasMoreBytes && (octet & 0x7fu) == 0)
                {
                    return ParseError();
                }
                firstOctet = false;

                if (value > ((std::numeric_limits<NGIN::UInt64>::max() >> 7u)))
                {
                    return ParseError();
                }

                value = (value << 7u) | static_cast<NGIN::UInt64>(octet & 0x7fu);
            }

            if (hasMoreBytes)
            {
                return ParseError();
            }

            return value;
        }

        [[nodiscard]] CryptoExpected<void> AppendBase128Integer(ByteBuffer& output, NGIN::UInt64 value)
        {
            NGIN::UInt8    encoded[10] {};
            NGIN::UIntSize count = 0;

            encoded[count++] = static_cast<NGIN::UInt8>(value & 0x7fu);
            value >>= 7u;
            while (value != 0)
            {
                encoded[count++] = static_cast<NGIN::UInt8>(0x80u | (value & 0x7fu));
                value >>= 7u;
            }

            for (NGIN::UIntSize i = 0; i < count; ++i)
            {
                AppendByte(output, encoded[count - i - 1]);
            }

            return {};
        }

        [[nodiscard]] CryptoExpected<void> AppendIdentifier(ByteBuffer& output, DerTag tag)
        {
            if (tag.number < 31)
            {
                AppendByte(output, EncodeTagClass(tag.tagClass) | (tag.constructed ? 0x20u : 0u) |
                                           static_cast<NGIN::UInt8>(tag.number));
                return {};
            }

            AppendByte(output, EncodeTagClass(tag.tagClass) | (tag.constructed ? 0x20u : 0u) | 0x1fu);
            return AppendBase128Integer(output, tag.number);
        }

        [[nodiscard]] CryptoExpected<void> AppendLength(ByteBuffer& output, NGIN::UIntSize length)
        {
            if (length <= 127)
            {
                AppendByte(output, static_cast<NGIN::UInt8>(length));
                return {};
            }

            NGIN::UInt8    encoded[sizeof(NGIN::UIntSize)] {};
            NGIN::UIntSize count     = 0;
            auto           remaining = length;
            while (remaining != 0)
            {
                encoded[count++] = static_cast<NGIN::UInt8>(remaining & 0xffu);
                remaining >>= 8u;
            }

            if (count > 126)
            {
                return InvalidArgument();
            }

            AppendByte(output, 0x80u | static_cast<NGIN::UInt8>(count));
            for (NGIN::UIntSize i = 0; i < count; ++i)
            {
                AppendByte(output, encoded[count - i - 1]);
            }

            return {};
        }
    }// namespace

    DerReader::DerReader(ConstByteSpan input, DerReadOptions options) noexcept : DerReader(input, options, 0) {}

    DerReader::DerReader(ConstByteSpan input, DerReadOptions options, NGIN::UIntSize depth) noexcept
        : m_input {input}, m_options {options}, m_depth {depth}
    {
    }

    bool DerReader::IsAtEnd() const noexcept
    {
        return m_offset == m_input.size();
    }

    NGIN::UIntSize DerReader::Remaining() const noexcept
    {
        return m_input.size() - m_offset;
    }

    CryptoExpected<DerElement> DerReader::ReadElement() noexcept
    {
        const auto elementStart = m_offset;
        if (m_offset >= m_input.size())
        {
            return ParseError();
        }

        const auto identifier = ByteValue(m_input[m_offset++]);

        DerTag tag {
                .tagClass    = DecodeTagClass(identifier),
                .constructed = (identifier & 0x20u) != 0,
                .number      = static_cast<NGIN::UInt32>(identifier & 0x1fu),
        };

        if (tag.number == 0x1fu)
        {
            auto highTagNumber = ReadBase128Integer(m_input, m_offset);
            if (!highTagNumber.HasValue() || highTagNumber.Value() < 31 ||
                highTagNumber.Value() > std::numeric_limits<NGIN::UInt32>::max())
            {
                return ParseError();
            }
            tag.number = static_cast<NGIN::UInt32>(highTagNumber.Value());
        }

        if (m_offset >= m_input.size())
        {
            return ParseError();
        }

        const auto     firstLengthOctet = ByteValue(m_input[m_offset++]);
        NGIN::UIntSize length           = 0;
        if ((firstLengthOctet & 0x80u) == 0)
        {
            length = firstLengthOctet;
        }
        else
        {
            const auto lengthOctets = static_cast<NGIN::UIntSize>(firstLengthOctet & 0x7fu);
            if (lengthOctets == 0 || lengthOctets > sizeof(NGIN::UIntSize) || lengthOctets > Remaining())
            {
                return ParseError();
            }
            if (ByteValue(m_input[m_offset]) == 0)
            {
                return ParseError();
            }

            for (NGIN::UIntSize i = 0; i < lengthOctets; ++i)
            {
                if (length > ((std::numeric_limits<NGIN::UIntSize>::max() >> 8u)))
                {
                    return ParseError();
                }
                length = (length << 8u) | ByteValue(m_input[m_offset++]);
            }

            if (length <= 127)
            {
                return ParseError();
            }
        }

        if (length > m_options.maxElementBytes || length > Remaining())
        {
            return ParseError();
        }

        const auto valueStart = m_offset;
        m_offset += length;

        return DerElement {
                .tag     = tag,
                .value   = m_input.subspan(valueStart, length),
                .encoded = m_input.subspan(elementStart, m_offset - elementStart),
        };
    }

    CryptoExpected<DerReader> DerReader::EnterConstructed(const DerElement& element) const noexcept
    {
        if (!element.tag.constructed || m_depth >= m_options.maxDepth)
        {
            return ParseError();
        }

        return DerReader {element.value, m_options, m_depth + 1};
    }

    bool IsDerUniversalElement(const DerElement& element, DerUniversalTag tag, bool constructed) noexcept
    {
        return element.tag == MakeDerUniversalTag(tag, constructed);
    }

    CryptoExpected<ConstByteSpan> ReadDerInteger(const DerElement& element) noexcept
    {
        if (!IsDerUniversalElement(element, DerUniversalTag::Integer) || !IsMinimalInteger(element.value))
        {
            return ParseError();
        }

        return element.value;
    }

    CryptoExpected<DerBitString> ReadDerBitString(const DerElement& element) noexcept
    {
        if (!IsDerUniversalElement(element, DerUniversalTag::BitString) || element.value.empty())
        {
            return ParseError();
        }

        const auto unusedBitCount = ByteValue(element.value[0]);
        const auto bytes          = element.value.subspan(1);
        if (!IsValidBitStringValue(unusedBitCount, bytes))
        {
            return ParseError();
        }

        return DerBitString {
                .unusedBitCount = unusedBitCount,
                .bytes          = bytes,
        };
    }

    CryptoExpected<ConstByteSpan> ReadDerOctetString(const DerElement& element) noexcept
    {
        if (!IsDerUniversalElement(element, DerUniversalTag::OctetString))
        {
            return ParseError();
        }

        return element.value;
    }

    CryptoExpected<NGIN::Containers::Vector<NGIN::UInt32>> ReadDerObjectIdentifier(const DerElement& element)
    {
        if (!IsDerUniversalElement(element, DerUniversalTag::ObjectIdentifier) || element.value.empty())
        {
            return ParseError();
        }

        NGIN::UIntSize offset = 0;
        auto           first  = ReadBase128Integer(element.value, offset);
        if (!first.HasValue())
        {
            return first.Error();
        }

        NGIN::Containers::Vector<NGIN::UInt32> arcs;
        const auto                             firstValue = first.Value();
        if (firstValue < 40)
        {
            arcs.PushBack(0);
            arcs.PushBack(static_cast<NGIN::UInt32>(firstValue));
        }
        else if (firstValue < 80)
        {
            arcs.PushBack(1);
            arcs.PushBack(static_cast<NGIN::UInt32>(firstValue - 40));
        }
        else
        {
            if (firstValue - 80 > std::numeric_limits<NGIN::UInt32>::max())
            {
                return ParseError();
            }
            arcs.PushBack(2);
            arcs.PushBack(static_cast<NGIN::UInt32>(firstValue - 80));
        }

        while (offset < element.value.size())
        {
            auto arc = ReadBase128Integer(element.value, offset);
            if (!arc.HasValue() || arc.Value() > std::numeric_limits<NGIN::UInt32>::max())
            {
                return ParseError();
            }
            arcs.PushBack(static_cast<NGIN::UInt32>(arc.Value()));
        }

        return arcs;
    }

    CryptoExpected<DerReader> ReadDerSequence(const DerReader& parent, const DerElement& element) noexcept
    {
        if (!IsDerUniversalElement(element, DerUniversalTag::Sequence, true))
        {
            return ParseError();
        }

        return parent.EnterConstructed(element);
    }

    CryptoExpected<DerReader> ReadDerSet(const DerReader& parent, const DerElement& element) noexcept
    {
        if (!IsDerUniversalElement(element, DerUniversalTag::Set, true))
        {
            return ParseError();
        }

        return parent.EnterConstructed(element);
    }

    CryptoExpected<ByteBuffer> EncodeDerElement(DerTag tag, ConstByteSpan value)
    {
        ByteBuffer output;
        output.Reserve(value.size() + 8);

        auto identifier = AppendIdentifier(output, tag);
        if (!identifier.HasValue())
        {
            return identifier.Error();
        }

        auto length = AppendLength(output, value.size());
        if (!length.HasValue())
        {
            return length.Error();
        }

        AppendBytes(output, value);
        return output;
    }

    CryptoExpected<ByteBuffer> EncodeDerInteger(ConstByteSpan value)
    {
        if (!IsMinimalInteger(value))
        {
            return InvalidArgument();
        }

        return EncodeDerElement(MakeDerUniversalTag(DerUniversalTag::Integer), value);
    }

    CryptoExpected<ByteBuffer> EncodeDerBitString(NGIN::UInt8 unusedBitCount, ConstByteSpan bytes)
    {
        if (!IsValidBitStringValue(unusedBitCount, bytes))
        {
            return InvalidArgument();
        }

        ByteBuffer value;
        value.Reserve(bytes.size() + 1);
        AppendByte(value, unusedBitCount);
        AppendBytes(value, bytes);
        return EncodeDerElement(MakeDerUniversalTag(DerUniversalTag::BitString), ConstByteSpan {value.data(), value.Size()});
    }

    CryptoExpected<ByteBuffer> EncodeDerOctetString(ConstByteSpan value)
    {
        return EncodeDerElement(MakeDerUniversalTag(DerUniversalTag::OctetString), value);
    }

    CryptoExpected<ByteBuffer> EncodeDerObjectIdentifier(std::span<const NGIN::UInt32> arcs)
    {
        if (arcs.size() < 2 || arcs[0] > 2 || (arcs[0] < 2 && arcs[1] > 39))
        {
            return InvalidArgument();
        }

        ByteBuffer value;
        const auto firstValue = static_cast<NGIN::UInt64>(arcs[0]) * 40u + arcs[1];
        auto       first      = AppendBase128Integer(value, firstValue);
        if (!first.HasValue())
        {
            return first.Error();
        }

        for (NGIN::UIntSize i = 2; i < arcs.size(); ++i)
        {
            auto arc = AppendBase128Integer(value, arcs[i]);
            if (!arc.HasValue())
            {
                return arc.Error();
            }
        }

        return EncodeDerElement(MakeDerUniversalTag(DerUniversalTag::ObjectIdentifier), ConstByteSpan {value.data(), value.Size()});
    }

    CryptoExpected<ByteBuffer> EncodeDerSequence(ConstByteSpan encodedChildren)
    {
        return EncodeDerElement(MakeDerUniversalTag(DerUniversalTag::Sequence, true), encodedChildren);
    }

    CryptoExpected<ByteBuffer> EncodeDerSet(ConstByteSpan encodedChildren)
    {
        return EncodeDerElement(MakeDerUniversalTag(DerUniversalTag::Set, true), encodedChildren);
    }
}// namespace NGIN::Crypto::Encoding
