#include <NGIN/Crypto/Encoding/Hex.hpp>

#include <NGIN/Crypto/Errors/CryptoError.hpp>

#include <cstddef>

namespace NGIN::Crypto::Encoding
{
    namespace
    {
        [[nodiscard]] constexpr char HexDigit(NGIN::UInt8 value) noexcept
        {
            return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('a' + (value - 10));
        }

        [[nodiscard]] constexpr NGIN::Int32 DecodeHexDigit(char value) noexcept
        {
            if (value >= '0' && value <= '9')
            {
                return value - '0';
            }
            if (value >= 'a' && value <= 'f')
            {
                return 10 + value - 'a';
            }
            if (value >= 'A' && value <= 'F')
            {
                return 10 + value - 'A';
            }
            return -1;
        }

        [[nodiscard]] constexpr CryptoError EncodingError() noexcept
        {
            return CryptoError {CryptoErrorCode::EncodingError};
        }

        [[nodiscard]] constexpr CryptoError OutputBufferTooSmall() noexcept
        {
            return CryptoError {CryptoErrorCode::OutputBufferTooSmall};
        }
    }// namespace

    CryptoExpected<std::string> EncodeHex(ConstByteSpan input)
    {
        std::string output;
        output.resize(HexEncodedLength(input.size()));

        auto result = EncodeHexInto(input, std::span<char> {output.data(), output.size()});
        if (!result.HasValue())
        {
            return result.Error();
        }

        return output;
    }

    CryptoExpected<void> EncodeHexInto(ConstByteSpan input, std::span<char> output) noexcept
    {
        if (output.size() != HexEncodedLength(input.size()))
        {
            return OutputBufferTooSmall();
        }

        for (NGIN::UIntSize i = 0; i < input.size(); ++i)
        {
            const auto value    = std::to_integer<NGIN::UInt8>(input[i]);
            output[(i * 2)]     = HexDigit(static_cast<NGIN::UInt8>(value >> 4));
            output[(i * 2) + 1] = HexDigit(static_cast<NGIN::UInt8>(value & 0x0f));
        }

        return {};
    }

    CryptoExpected<ByteBuffer> DecodeHex(std::string_view text)
    {
        if ((text.size() % 2) != 0)
        {
            return EncodingError();
        }

        auto output = MakeByteBuffer(HexDecodedLength(text));
        auto result = DecodeHexInto(text, ByteSpan {output.data(), output.Size()});
        if (!result.HasValue())
        {
            return result.Error();
        }

        return output;
    }

    CryptoExpected<void> DecodeHexInto(std::string_view text, ByteSpan output) noexcept
    {
        if ((text.size() % 2) != 0)
        {
            return EncodingError();
        }
        if (output.size() != HexDecodedLength(text))
        {
            return OutputBufferTooSmall();
        }

        for (NGIN::UIntSize i = 0; i < output.size(); ++i)
        {
            const auto high = DecodeHexDigit(text[(i * 2)]);
            const auto low  = DecodeHexDigit(text[(i * 2) + 1]);
            if (high < 0 || low < 0)
            {
                return EncodingError();
            }

            output[i] = static_cast<NGIN::Byte>((high << 4) | low);
        }

        return {};
    }
}// namespace NGIN::Crypto::Encoding
