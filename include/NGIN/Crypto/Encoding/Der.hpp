#pragma once

#include <NGIN/Containers/Vector.hpp>
#include <NGIN/Crypto/ByteBuffer.hpp>
#include <NGIN/Crypto/Result.hpp>
#include <NGIN/Crypto/Types.hpp>

namespace NGIN::Crypto::Encoding
{
    /// @brief ASN.1 tag class used by DER TLV elements.
    enum class DerTagClass : NGIN::UInt8
    {
        Universal,
        Application,
        ContextSpecific,
        Private,
    };

    /// @brief Universal tag numbers commonly needed by crypto formats.
    enum class DerUniversalTag : NGIN::UInt32
    {
        Boolean          = 1,
        Integer          = 2,
        BitString        = 3,
        OctetString      = 4,
        Null             = 5,
        ObjectIdentifier = 6,
        Sequence         = 16,
        Set              = 17,
    };

    /// @brief Parsed DER tag identifier.
    struct DerTag
    {
        DerTagClass  tagClass {DerTagClass::Universal};
        bool         constructed {false};
        NGIN::UInt32 number {0};

        [[nodiscard]] constexpr bool operator==(const DerTag&) const noexcept = default;
    };

    /// @brief Non-owning view over a parsed DER element.
    struct DerElement
    {
        DerTag        tag;
        ConstByteSpan value;
        ConstByteSpan encoded;
    };

    /// @brief Parsed DER BIT STRING contents.
    struct DerBitString
    {
        NGIN::UInt8   unusedBitCount {0};
        ConstByteSpan bytes;
    };

    /// @brief Bounds for strict DER readers.
    struct DerReadOptions
    {
        NGIN::UIntSize maxElementBytes {1u << 20};
        NGIN::UIntSize maxDepth {16};
    };

    /// @brief Streaming reader over DER TLV elements. Returned element views borrow from the original input.
    class DerReader
    {
    public:
        explicit DerReader(ConstByteSpan input, DerReadOptions options = {}) noexcept;

        [[nodiscard]] bool           IsAtEnd() const noexcept;
        [[nodiscard]] NGIN::UIntSize Remaining() const noexcept;

        [[nodiscard]] CryptoExpected<DerElement> ReadElement() noexcept;
        [[nodiscard]] CryptoExpected<DerReader>  EnterConstructed(const DerElement& element) const noexcept;

    private:
        DerReader(ConstByteSpan input, DerReadOptions options, NGIN::UIntSize depth) noexcept;

        ConstByteSpan  m_input;
        DerReadOptions m_options;
        NGIN::UIntSize m_offset {0};
        NGIN::UIntSize m_depth {0};
    };

    [[nodiscard]] constexpr DerTag MakeDerUniversalTag(DerUniversalTag tag, bool constructed = false) noexcept
    {
        return DerTag {
                .tagClass    = DerTagClass::Universal,
                .constructed = constructed,
                .number      = static_cast<NGIN::UInt32>(tag),
        };
    }

    [[nodiscard]] bool IsDerUniversalElement(
            const DerElement& element, DerUniversalTag tag, bool constructed = false) noexcept;

    [[nodiscard]] CryptoExpected<ConstByteSpan>                          ReadDerInteger(const DerElement& element) noexcept;
    [[nodiscard]] CryptoExpected<DerBitString>                           ReadDerBitString(const DerElement& element) noexcept;
    [[nodiscard]] CryptoExpected<ConstByteSpan>                          ReadDerOctetString(const DerElement& element) noexcept;
    [[nodiscard]] CryptoExpected<NGIN::Containers::Vector<NGIN::UInt32>> ReadDerObjectIdentifier(
            const DerElement& element);
    [[nodiscard]] CryptoExpected<DerReader> ReadDerSequence(const DerReader& parent, const DerElement& element) noexcept;
    [[nodiscard]] CryptoExpected<DerReader> ReadDerSet(const DerReader& parent, const DerElement& element) noexcept;

    [[nodiscard]] CryptoExpected<ByteBuffer> EncodeDerElement(DerTag tag, ConstByteSpan value);
    [[nodiscard]] CryptoExpected<ByteBuffer> EncodeDerInteger(ConstByteSpan value);
    [[nodiscard]] CryptoExpected<ByteBuffer> EncodeDerBitString(NGIN::UInt8 unusedBitCount, ConstByteSpan bytes);
    [[nodiscard]] CryptoExpected<ByteBuffer> EncodeDerOctetString(ConstByteSpan value);
    [[nodiscard]] CryptoExpected<ByteBuffer> EncodeDerObjectIdentifier(std::span<const NGIN::UInt32> arcs);
    [[nodiscard]] CryptoExpected<ByteBuffer> EncodeDerSequence(ConstByteSpan encodedChildren);
    [[nodiscard]] CryptoExpected<ByteBuffer> EncodeDerSet(ConstByteSpan encodedChildren);
}// namespace NGIN::Crypto::Encoding
