#include <NGIN/Crypto/Encoding/Base64Url.hpp>

#include <NGIN/Crypto/Errors/CryptoError.hpp>

namespace NGIN::Crypto::Encoding
{
    namespace
    {
        [[nodiscard]] constexpr CryptoError EncodingError() noexcept
        {
            return CryptoError {CryptoErrorCode::EncodingError};
        }

        [[nodiscard]] constexpr CryptoError OutputBufferTooSmall() noexcept
        {
            return CryptoError {CryptoErrorCode::OutputBufferTooSmall};
        }

        void ConvertStandardToUrl(std::span<char> text) noexcept
        {
            for (char& character: text)
            {
                if (character == '+')
                {
                    character = '-';
                }
                else if (character == '/')
                {
                    character = '_';
                }
            }
        }

        [[nodiscard]] CryptoExpected<std::string> NormalizeUrlInput(std::string_view text)
        {
            if ((text.size() % 4) == 1)
            {
                return std::unexpected(EncodingError());
            }

            std::string normalized;
            normalized.reserve(text.size() + 2);

            bool seenPadding = false;
            for (char character: text)
            {
                if (character == '=')
                {
                    seenPadding = true;
                    normalized.push_back(character);
                    continue;
                }

                if (seenPadding)
                {
                    return std::unexpected(EncodingError());
                }

                if (character == '-')
                {
                    normalized.push_back('+');
                }
                else if (character == '_')
                {
                    normalized.push_back('/');
                }
                else if (character == '+' || character == '/')
                {
                    return std::unexpected(EncodingError());
                }
                else
                {
                    normalized.push_back(character);
                }
            }

            if (normalized.find('=') == std::string::npos)
            {
                while ((normalized.size() % 4) != 0)
                {
                    normalized.push_back('=');
                }
            }
            else if ((normalized.size() % 4) != 0)
            {
                return std::unexpected(EncodingError());
            }

            return normalized;
        }
    }// namespace

    CryptoExpected<std::string> EncodeBase64Url(ConstByteSpan input, Base64Padding padding)
    {
        auto output = EncodeBase64(input, padding);
        if (!output.has_value())
        {
            return std::unexpected(std::move(output).error());
        }

        ConvertStandardToUrl(std::span<char> {output.value().data(), output.value().size()});
        return output;
    }

    CryptoExpected<void> EncodeBase64UrlInto(ConstByteSpan input, std::span<char> output, Base64Padding padding) noexcept
    {
        auto result = EncodeBase64Into(input, output, padding);
        if (!result.has_value())
        {
            return std::unexpected(std::move(result).error());
        }

        ConvertStandardToUrl(output);
        return {};
    }

    CryptoExpected<ByteBuffer> DecodeBase64Url(std::string_view text)
    {
        auto normalized = NormalizeUrlInput(text);
        if (!normalized.has_value())
        {
            return std::unexpected(std::move(normalized).error());
        }

        return DecodeBase64(normalized.value());
    }

    CryptoExpected<void> DecodeBase64UrlInto(std::string_view text, ByteSpan output)
    {
        auto normalized = NormalizeUrlInput(text);
        if (!normalized.has_value())
        {
            return std::unexpected(std::move(normalized).error());
        }

        auto decoded = DecodeBase64(normalized.value());
        if (!decoded.has_value())
        {
            return std::unexpected(std::move(decoded).error());
        }

        if (output.size() != decoded.value().Size())
        {
            return std::unexpected(OutputBufferTooSmall());
        }

        for (NGIN::UIntSize i = 0; i < output.size(); ++i)
        {
            output[i] = decoded.value()[i];
        }

        return {};
    }
}// namespace NGIN::Crypto::Encoding
