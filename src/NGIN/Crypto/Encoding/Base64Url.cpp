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
            for (auto& character: text)
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
                return EncodingError();
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
                    return EncodingError();
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
                    return EncodingError();
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
                return EncodingError();
            }

            return normalized;
        }
    }// namespace

    CryptoExpected<std::string> EncodeBase64Url(ConstByteSpan input, Base64Padding padding)
    {
        auto output = EncodeBase64(input, padding);
        if (!output.HasValue())
        {
            return output.Error();
        }

        ConvertStandardToUrl(std::span<char> {output.Value().data(), output.Value().size()});
        return output;
    }

    CryptoExpected<void> EncodeBase64UrlInto(ConstByteSpan input, std::span<char> output, Base64Padding padding) noexcept
    {
        auto result = EncodeBase64Into(input, output, padding);
        if (!result.HasValue())
        {
            return result.Error();
        }

        ConvertStandardToUrl(output);
        return {};
    }

    CryptoExpected<ByteBuffer> DecodeBase64Url(std::string_view text)
    {
        auto normalized = NormalizeUrlInput(text);
        if (!normalized.HasValue())
        {
            return normalized.Error();
        }

        return DecodeBase64(normalized.Value());
    }

    CryptoExpected<void> DecodeBase64UrlInto(std::string_view text, ByteSpan output)
    {
        auto normalized = NormalizeUrlInput(text);
        if (!normalized.HasValue())
        {
            return normalized.Error();
        }

        auto decoded = DecodeBase64(normalized.Value());
        if (!decoded.HasValue())
        {
            return decoded.Error();
        }

        if (output.size() != decoded.Value().Size())
        {
            return OutputBufferTooSmall();
        }

        for (NGIN::UIntSize i = 0; i < output.size(); ++i)
        {
            output[i] = decoded.Value()[i];
        }

        return {};
    }
}// namespace NGIN::Crypto::Encoding
