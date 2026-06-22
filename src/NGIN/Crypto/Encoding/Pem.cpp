#include <NGIN/Crypto/Encoding/Pem.hpp>

#include <NGIN/Crypto/Encoding/Base64.hpp>
#include <NGIN/Crypto/Errors/CryptoError.hpp>

#include <string>

namespace NGIN::Crypto::Encoding
{
    namespace
    {
        constexpr std::string_view BEGIN_PREFIX {"-----BEGIN "};
        constexpr std::string_view END_PREFIX {"-----END "};
        constexpr std::string_view BOUNDARY_SUFFIX {"-----"};

        [[nodiscard]] constexpr CryptoError ParseError() noexcept
        {
            return CryptoError {CryptoErrorCode::ParseError};
        }

        [[nodiscard]] constexpr CryptoError EncodingError() noexcept
        {
            return CryptoError {CryptoErrorCode::EncodingError};
        }

        [[nodiscard]] constexpr bool StartsWith(std::string_view text, std::string_view prefix) noexcept
        {
            return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
        }

        [[nodiscard]] constexpr bool EndsWith(std::string_view text, std::string_view suffix) noexcept
        {
            return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
        }

        [[nodiscard]] bool ReadLine(std::string_view input, NGIN::UIntSize& offset, std::string_view& line) noexcept
        {
            if (offset >= input.size())
            {
                return false;
            }

            const auto lineStart = offset;
            while (offset < input.size() && input[offset] != '\r' && input[offset] != '\n')
            {
                ++offset;
            }

            line = input.substr(lineStart, offset - lineStart);

            if (offset < input.size())
            {
                if (input[offset] == '\r' && offset + 1 < input.size() && input[offset + 1] == '\n')
                {
                    offset += 2;
                }
                else
                {
                    ++offset;
                }
            }

            return true;
        }

        [[nodiscard]] bool ExtractBoundaryLabel(
                std::string_view line, std::string_view prefix, std::string_view& label) noexcept
        {
            if (!StartsWith(line, prefix) || !EndsWith(line, BOUNDARY_SUFFIX))
            {
                return false;
            }

            const auto labelStart = prefix.size();
            const auto labelSize  = line.size() - prefix.size() - BOUNDARY_SUFFIX.size();
            label                 = line.substr(labelStart, labelSize);
            return true;
        }

        [[nodiscard]] constexpr bool IsLabelCharacter(char character) noexcept
        {
            return (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') || character == ' ';
        }

        [[nodiscard]] bool IsValidLabel(std::string_view label) noexcept
        {
            if (label.empty() || label.front() == ' ' || label.back() == ' ')
            {
                return false;
            }

            bool previousWasSpace = false;
            for (char character: label)
            {
                if (!IsLabelCharacter(character))
                {
                    return false;
                }

                if (character == ' ')
                {
                    if (previousWasSpace)
                    {
                        return false;
                    }
                    previousWasSpace = true;
                }
                else
                {
                    previousWasSpace = false;
                }
            }

            return true;
        }

        [[nodiscard]] bool IsAllowedLabel(std::string_view label, PemParseOptions options) noexcept
        {
            if (options.allowedLabels.size() == 0)
            {
                return true;
            }

            for (std::string_view allowedLabel: options.allowedLabels)
            {
                if (label == allowedLabel)
                {
                    return true;
                }
            }

            return false;
        }

        [[nodiscard]] constexpr bool IsBase64Character(char character) noexcept
        {
            return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
                   (character >= '0' && character <= '9') || character == '+' || character == '/' || character == '=';
        }

        [[nodiscard]] bool CanFitEncodedBase64Length(NGIN::UIntSize encodedLength, NGIN::UIntSize maxDecodedBytes) noexcept
        {
            if (maxDecodedBytes > (static_cast<NGIN::UIntSize>(-1) - 2))
            {
                return true;
            }

            const auto maximumEncodedLength = ((maxDecodedBytes + 2) / 3) * 4 + 4;
            return encodedLength <= maximumEncodedLength;
        }
    }// namespace

    CryptoExpected<PemBlocks> ParsePem(std::string_view input, PemParseOptions options)
    {
        PemBlocks   blocks;
        std::string currentLabel;
        std::string encodedPayload;

        bool             insideBlock = false;
        NGIN::UIntSize   offset      = 0;
        std::string_view line;

        while (ReadLine(input, offset, line))
        {
            if (!insideBlock)
            {
                if (line.empty())
                {
                    continue;
                }

                std::string_view label;
                if (!ExtractBoundaryLabel(line, BEGIN_PREFIX, label) || !IsValidLabel(label) ||
                    !IsAllowedLabel(label, options))
                {
                    return ParseError();
                }

                if (!options.allowMultipleBlocks && blocks.Size() != 0)
                {
                    return ParseError();
                }

                currentLabel.assign(label);
                encodedPayload.clear();
                insideBlock = true;
                continue;
            }

            if (StartsWith(line, BEGIN_PREFIX))
            {
                return ParseError();
            }

            std::string_view endLabel;
            if (ExtractBoundaryLabel(line, END_PREFIX, endLabel))
            {
                if (endLabel != currentLabel || encodedPayload.empty())
                {
                    return ParseError();
                }
                if ((encodedPayload.size() % 4) != 0)
                {
                    return EncodingError();
                }

                auto decoded = DecodeBase64(encodedPayload);
                if (!decoded.HasValue())
                {
                    return decoded.Error();
                }
                if (decoded.Value().Size() > options.maxDecodedBytes)
                {
                    return ParseError();
                }

                blocks.PushBack(PemBlock {
                        .label   = currentLabel,
                        .decoded = std::move(decoded.Value()),
                });

                currentLabel.clear();
                encodedPayload.clear();
                insideBlock = false;
                continue;
            }

            if (line.empty())
            {
                return ParseError();
            }

            for (char character: line)
            {
                if (!IsBase64Character(character))
                {
                    return EncodingError();
                }
            }

            if (!CanFitEncodedBase64Length(encodedPayload.size() + line.size(), options.maxDecodedBytes))
            {
                return ParseError();
            }

            encodedPayload.append(line);
        }

        if (insideBlock || blocks.Size() == 0)
        {
            return ParseError();
        }

        return blocks;
    }
}// namespace NGIN::Crypto::Encoding
