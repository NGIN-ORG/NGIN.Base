#include <NGIN/Crypto/Encoding/Base64.hpp>

#include <NGIN/Crypto/Errors/CryptoError.hpp>

#include <array>
#include <cstddef>

namespace NGIN::Crypto::Encoding
{
    namespace
    {
        constexpr std::string_view BASE64_ALPHABET {"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"};

        [[nodiscard]] constexpr CryptoError EncodingError() noexcept
        {
            return CryptoError {CryptoErrorCode::EncodingError};
        }

        [[nodiscard]] constexpr CryptoError OutputBufferTooSmall() noexcept
        {
            return CryptoError {CryptoErrorCode::OutputBufferTooSmall};
        }

        [[nodiscard]] constexpr NGIN::Int32 DecodeValue(char value) noexcept
        {
            if (value >= 'A' && value <= 'Z')
            {
                return value - 'A';
            }
            if (value >= 'a' && value <= 'z')
            {
                return 26 + value - 'a';
            }
            if (value >= '0' && value <= '9')
            {
                return 52 + value - '0';
            }
            if (value == '+')
            {
                return 62;
            }
            if (value == '/')
            {
                return 63;
            }
            return -1;
        }

        [[nodiscard]] CryptoExpected<NGIN::UIntSize> DecodedLength(std::string_view text) noexcept
        {
            if (text.empty())
            {
                return NGIN::UIntSize {0};
            }

            if ((text.size() % 4) == 1)
            {
                return EncodingError();
            }

            NGIN::UIntSize padding = 0;
            if (!text.empty() && text.back() == '=')
            {
                padding = 1;
                if (text.size() >= 2 && text[text.size() - 2] == '=')
                {
                    padding = 2;
                }
            }

            for (NGIN::UIntSize i = 0; i < text.size(); ++i)
            {
                if (text[i] == '=')
                {
                    if (i < text.size() - padding)
                    {
                        return EncodingError();
                    }
                    continue;
                }

                if (DecodeValue(text[i]) < 0)
                {
                    return EncodingError();
                }
            }

            const auto fullGroups = text.size() / 4;
            const auto remainder  = text.size() % 4;

            auto decoded = fullGroups * 3;
            if (remainder == 2)
            {
                decoded += 1;
            }
            else if (remainder == 3)
            {
                decoded += 2;
            }

            if (padding > 0)
            {
                if (remainder != 0 || padding > 2 || text.size() < 4)
                {
                    return EncodingError();
                }

                if (padding == 2 && (DecodeValue(text[text.size() - 3]) & 0x0f) != 0)
                {
                    return EncodingError();
                }
                if (padding == 1 && (DecodeValue(text[text.size() - 2]) & 0x03) != 0)
                {
                    return EncodingError();
                }

                decoded -= padding;
            }
            else if (remainder == 2 && (DecodeValue(text[text.size() - 1]) & 0x0f) != 0)
            {
                return EncodingError();
            }
            else if (remainder == 3 && (DecodeValue(text[text.size() - 1]) & 0x03) != 0)
            {
                return EncodingError();
            }

            return decoded;
        }
    }// namespace

    NGIN::UIntSize Base64EncodedLength(NGIN::UIntSize byteCount, Base64Padding padding) noexcept
    {
        const auto fullGroups = byteCount / 3;
        const auto remainder  = byteCount % 3;
        auto       length     = fullGroups * 4;

        if (remainder == 0)
        {
            return length;
        }

        return padding == Base64Padding::Required ? length + 4 : length + remainder + 1;
    }

    CryptoExpected<std::string> EncodeBase64(ConstByteSpan input, Base64Padding padding)
    {
        std::string output;
        output.resize(Base64EncodedLength(input.size(), padding));

        auto result = EncodeBase64Into(input, std::span<char> {output.data(), output.size()}, padding);
        if (!result.HasValue())
        {
            return result.Error();
        }

        return output;
    }

    CryptoExpected<void> EncodeBase64Into(ConstByteSpan input, std::span<char> output, Base64Padding padding) noexcept
    {
        if (output.size() != Base64EncodedLength(input.size(), padding))
        {
            return OutputBufferTooSmall();
        }

        NGIN::UIntSize inputIndex  = 0;
        NGIN::UIntSize outputIndex = 0;

        while (input.size() - inputIndex >= 3)
        {
            const auto b0 = std::to_integer<NGIN::UInt8>(input[inputIndex++]);
            const auto b1 = std::to_integer<NGIN::UInt8>(input[inputIndex++]);
            const auto b2 = std::to_integer<NGIN::UInt8>(input[inputIndex++]);

            output[outputIndex++] = BASE64_ALPHABET[static_cast<NGIN::UIntSize>((b0 >> 2) & 0x3f)];
            output[outputIndex++] = BASE64_ALPHABET[static_cast<NGIN::UIntSize>(((b0 & 0x03) << 4) | (b1 >> 4))];
            output[outputIndex++] = BASE64_ALPHABET[static_cast<NGIN::UIntSize>(((b1 & 0x0f) << 2) | (b2 >> 6))];
            output[outputIndex++] = BASE64_ALPHABET[static_cast<NGIN::UIntSize>(b2 & 0x3f)];
        }

        const auto remaining = input.size() - inputIndex;
        if (remaining == 1)
        {
            const auto b0 = std::to_integer<NGIN::UInt8>(input[inputIndex]);

            output[outputIndex++] = BASE64_ALPHABET[static_cast<NGIN::UIntSize>((b0 >> 2) & 0x3f)];
            output[outputIndex++] = BASE64_ALPHABET[static_cast<NGIN::UIntSize>((b0 & 0x03) << 4)];
            if (padding == Base64Padding::Required)
            {
                output[outputIndex++] = '=';
                output[outputIndex++] = '=';
            }
        }
        else if (remaining == 2)
        {
            const auto b0 = std::to_integer<NGIN::UInt8>(input[inputIndex]);
            const auto b1 = std::to_integer<NGIN::UInt8>(input[inputIndex + 1]);

            output[outputIndex++] = BASE64_ALPHABET[static_cast<NGIN::UIntSize>((b0 >> 2) & 0x3f)];
            output[outputIndex++] = BASE64_ALPHABET[static_cast<NGIN::UIntSize>(((b0 & 0x03) << 4) | (b1 >> 4))];
            output[outputIndex++] = BASE64_ALPHABET[static_cast<NGIN::UIntSize>((b1 & 0x0f) << 2)];
            if (padding == Base64Padding::Required)
            {
                output[outputIndex++] = '=';
            }
        }

        return {};
    }

    CryptoExpected<ByteBuffer> DecodeBase64(std::string_view text)
    {
        auto decodedLength = DecodedLength(text);
        if (!decodedLength.HasValue())
        {
            return decodedLength.Error();
        }

        auto output = MakeByteBuffer(decodedLength.Value());
        auto result = DecodeBase64Into(text, ByteSpan {output.data(), output.Size()});
        if (!result.HasValue())
        {
            return result.Error();
        }

        return output;
    }

    CryptoExpected<void> DecodeBase64Into(std::string_view text, ByteSpan output) noexcept
    {
        auto decodedLength = DecodedLength(text);
        if (!decodedLength.HasValue())
        {
            return decodedLength.Error();
        }
        if (output.size() != decodedLength.Value())
        {
            return OutputBufferTooSmall();
        }

        std::array<NGIN::UInt8, 4> quartet {};
        NGIN::UIntSize             quartetSize = 0;
        NGIN::UIntSize             outputIndex = 0;

        for (char character: text)
        {
            if (character == '=')
            {
                break;
            }

            const auto value = DecodeValue(character);
            if (value < 0)
            {
                return EncodingError();
            }

            quartet[quartetSize++] = static_cast<NGIN::UInt8>(value);
            if (quartetSize == 4)
            {
                output[outputIndex++] = static_cast<NGIN::Byte>((quartet[0] << 2) | (quartet[1] >> 4));
                output[outputIndex++] = static_cast<NGIN::Byte>((quartet[1] << 4) | (quartet[2] >> 2));
                output[outputIndex++] = static_cast<NGIN::Byte>((quartet[2] << 6) | quartet[3]);
                quartetSize           = 0;
            }
        }

        if (quartetSize == 2)
        {
            output[outputIndex++] = static_cast<NGIN::Byte>((quartet[0] << 2) | (quartet[1] >> 4));
        }
        else if (quartetSize == 3)
        {
            output[outputIndex++] = static_cast<NGIN::Byte>((quartet[0] << 2) | (quartet[1] >> 4));
            output[outputIndex++] = static_cast<NGIN::Byte>((quartet[1] << 4) | (quartet[2] >> 2));
        }
        else if (quartetSize == 1)
        {
            return EncodingError();
        }

        return {};
    }
}// namespace NGIN::Crypto::Encoding
