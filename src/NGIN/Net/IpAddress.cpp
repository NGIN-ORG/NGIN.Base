#include <NGIN/Net/Types/IpAddress.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <vector>

namespace NGIN::Net
{
    namespace
    {
        [[nodiscard]] AddressExpected<std::array<NGIN::Byte, IpAddress::V4Size>> ParseV4(
                std::string_view text,
                NGIN::UIntSize   baseOffset = 0) noexcept
        {
            std::array<NGIN::Byte, IpAddress::V4Size> bytes {};
            NGIN::UIntSize                            begin = 0;
            for (NGIN::UIntSize component = 0; component < bytes.size(); ++component)
            {
                const auto end      = text.find('.', begin);
                const auto tokenEnd = end == std::string_view::npos ? text.size() : end;
                const auto token    = text.substr(begin, tokenEnd - begin);
                if (token.empty() || (token.size() > 1 && token.front() == '0'))
                {
                    return NGIN::Utilities::Unexpected<AddressParseError>(
                            {AddressParseErrorCode::InvalidFormat, baseOffset + begin});
                }
                unsigned   value  = 0;
                const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value, 10);
                if (parsed.ec == std::errc::invalid_argument || parsed.ptr != token.data() + token.size())
                {
                    return NGIN::Utilities::Unexpected<AddressParseError>(
                            {AddressParseErrorCode::InvalidCharacter,
                             baseOffset + begin + static_cast<NGIN::UIntSize>(parsed.ptr - token.data())});
                }
                if (parsed.ec == std::errc::result_out_of_range || value > 255)
                {
                    return NGIN::Utilities::Unexpected<AddressParseError>(
                            {AddressParseErrorCode::ComponentOutOfRange, baseOffset + begin});
                }
                bytes[component] = static_cast<NGIN::Byte>(value);
                if (component != bytes.size() - 1 && end == std::string_view::npos)
                {
                    return NGIN::Utilities::Unexpected<AddressParseError>(
                            {AddressParseErrorCode::InvalidFormat, baseOffset + text.size()});
                }
                if (component == bytes.size() - 1 && end != std::string_view::npos)
                {
                    return NGIN::Utilities::Unexpected<AddressParseError>(
                            {AddressParseErrorCode::InvalidFormat, baseOffset + end});
                }
                begin = tokenEnd + 1;
            }
            return bytes;
        }

        [[nodiscard]] AddressExpected<std::vector<NGIN::UInt16>> ParseV6Side(
                std::string_view text,
                NGIN::UIntSize   baseOffset,
                bool             allowV4)
        {
            std::vector<NGIN::UInt16> words;
            if (text.empty())
            {
                return words;
            }

            NGIN::UIntSize begin = 0;
            while (begin <= text.size())
            {
                const auto end      = text.find(':', begin);
                const auto tokenEnd = end == std::string_view::npos ? text.size() : end;
                const auto token    = text.substr(begin, tokenEnd - begin);
                if (token.empty())
                {
                    return NGIN::Utilities::Unexpected<AddressParseError>(
                            {AddressParseErrorCode::InvalidFormat, baseOffset + begin});
                }

                if (token.find('.') != std::string_view::npos)
                {
                    if (!allowV4 || end != std::string_view::npos)
                    {
                        return NGIN::Utilities::Unexpected<AddressParseError>(
                                {AddressParseErrorCode::InvalidFormat, baseOffset + begin});
                    }
                    auto v4 = ParseV4(token, baseOffset + begin);
                    if (!v4.HasValue())
                    {
                        return NGIN::Utilities::Unexpected<AddressParseError>(std::move(v4).TakeError());
                    }
                    const auto& bytes = v4.Value();
                    words.push_back(static_cast<NGIN::UInt16>(
                            std::to_integer<NGIN::UInt8>(bytes[0]) << 8 | std::to_integer<NGIN::UInt8>(bytes[1])));
                    words.push_back(static_cast<NGIN::UInt16>(
                            std::to_integer<NGIN::UInt8>(bytes[2]) << 8 | std::to_integer<NGIN::UInt8>(bytes[3])));
                    break;
                }

                if (token.size() > 4)
                {
                    return NGIN::Utilities::Unexpected<AddressParseError>(
                            {AddressParseErrorCode::ComponentOutOfRange, baseOffset + begin});
                }
                unsigned   value  = 0;
                const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value, 16);
                if (parsed.ec != std::errc {} || parsed.ptr != token.data() + token.size())
                {
                    return NGIN::Utilities::Unexpected<AddressParseError>(
                            {AddressParseErrorCode::InvalidCharacter,
                             baseOffset + begin + static_cast<NGIN::UIntSize>(parsed.ptr - token.data())});
                }
                words.push_back(static_cast<NGIN::UInt16>(value));
                if (end == std::string_view::npos)
                {
                    break;
                }
                begin = end + 1;
            }
            return words;
        }

        [[nodiscard]] AddressExpected<IpAddress> ParseV6(std::string_view text)
        {
            const auto compression = text.find("::");
            if (compression != std::string_view::npos && text.find("::", compression + 2) != std::string_view::npos)
            {
                return NGIN::Utilities::Unexpected<AddressParseError>(
                        {AddressParseErrorCode::InvalidFormat, compression + 2});
            }

            const auto leftText  = compression == std::string_view::npos ? text : text.substr(0, compression);
            const auto rightText = compression == std::string_view::npos ? std::string_view {} : text.substr(compression + 2);
            auto       left      = ParseV6Side(leftText, 0, compression == std::string_view::npos && rightText.empty());
            if (!left.HasValue())
            {
                return NGIN::Utilities::Unexpected<AddressParseError>(std::move(left).TakeError());
            }
            auto right = ParseV6Side(rightText, compression == std::string_view::npos ? 0 : compression + 2, true);
            if (!right.HasValue())
            {
                return NGIN::Utilities::Unexpected<AddressParseError>(std::move(right).TakeError());
            }

            const auto wordCount = left.Value().size() + right.Value().size();
            if ((compression == std::string_view::npos && wordCount != 8) ||
                (compression != std::string_view::npos && wordCount >= 8))
            {
                return NGIN::Utilities::Unexpected<AddressParseError>(
                        {AddressParseErrorCode::InvalidFormat, text.size()});
            }

            std::array<NGIN::UInt16, 8> words {};
            std::copy(left.Value().begin(), left.Value().end(), words.begin());
            std::copy(right.Value().begin(), right.Value().end(), words.end() - right.Value().size());
            std::array<NGIN::Byte, IpAddress::V6Size> bytes {};
            for (NGIN::UIntSize index = 0; index < words.size(); ++index)
            {
                bytes[index * 2]     = static_cast<NGIN::Byte>(words[index] >> 8);
                bytes[index * 2 + 1] = static_cast<NGIN::Byte>(words[index] & 0xff);
            }
            return IpAddress {AddressFamily::V6, bytes};
        }

        void AppendDecimal(std::array<char, 64>& output, NGIN::UIntSize& size, unsigned value) noexcept
        {
            const auto result = std::to_chars(output.data() + size, output.data() + output.size(), value, 10);
            size              = static_cast<NGIN::UIntSize>(result.ptr - output.data());
        }

        void AppendHex(std::array<char, 64>& output, NGIN::UIntSize& size, unsigned value) noexcept
        {
            const auto result = std::to_chars(output.data() + size, output.data() + output.size(), value, 16);
            size              = static_cast<NGIN::UIntSize>(result.ptr - output.data());
        }
    }// namespace

    AddressExpected<IpAddress> IpAddress::Parse(std::string_view text)
    {
        if (text.empty())
        {
            return NGIN::Utilities::Unexpected<AddressParseError>({AddressParseErrorCode::Empty, 0});
        }
        if (text.find('%') != std::string_view::npos)
        {
            return NGIN::Utilities::Unexpected<AddressParseError>(
                    {AddressParseErrorCode::InvalidScope, text.find('%')});
        }
        if (text.find(':') != std::string_view::npos)
        {
            return ParseV6(text);
        }
        auto v4 = ParseV4(text);
        if (!v4.HasValue())
        {
            return NGIN::Utilities::Unexpected<AddressParseError>(std::move(v4).TakeError());
        }
        std::array<NGIN::Byte, V6Size> bytes {};
        std::copy(v4.Value().begin(), v4.Value().end(), bytes.begin());
        return IpAddress {AddressFamily::V4, bytes};
    }

    bool IpAddress::TryFormat(std::span<char> destination, NGIN::UIntSize& written) const noexcept
    {
        written = 0;
        if (!IsValid())
        {
            return false;
        }

        std::array<char, 64> output {};
        NGIN::UIntSize       size = 0;
        if (IsV4())
        {
            for (NGIN::UIntSize index = 0; index < V4Size; ++index)
            {
                if (index != 0)
                {
                    output[size++] = '.';
                }
                AppendDecimal(output, size, std::to_integer<unsigned>(m_bytes[index]));
            }
        }
        else
        {
            const bool mapped = std::all_of(m_bytes.begin(), m_bytes.begin() + 10, [](NGIN::Byte value) {
                                    return value == NGIN::Byte {0};
                                }) &&
                                m_bytes[10] == NGIN::Byte {0xff} && m_bytes[11] == NGIN::Byte {0xff};
            if (mapped)
            {
                constexpr std::string_view prefix = "::ffff:";
                std::copy(prefix.begin(), prefix.end(), output.begin());
                size = prefix.size();
                for (NGIN::UIntSize index = 12; index < V6Size; ++index)
                {
                    if (index != 12)
                    {
                        output[size++] = '.';
                    }
                    AppendDecimal(output, size, std::to_integer<unsigned>(m_bytes[index]));
                }
            }
            else
            {
                std::array<NGIN::UInt16, 8> words {};
                for (NGIN::UIntSize index = 0; index < words.size(); ++index)
                {
                    words[index] = static_cast<NGIN::UInt16>(
                            std::to_integer<NGIN::UInt8>(m_bytes[index * 2]) << 8 |
                            std::to_integer<NGIN::UInt8>(m_bytes[index * 2 + 1]));
                }
                NGIN::UIntSize bestBegin  = words.size();
                NGIN::UIntSize bestLength = 0;
                for (NGIN::UIntSize begin = 0; begin < words.size();)
                {
                    if (words[begin] != 0)
                    {
                        ++begin;
                        continue;
                    }
                    NGIN::UIntSize end = begin;
                    while (end < words.size() && words[end] == 0)
                    {
                        ++end;
                    }
                    if (end - begin > bestLength && end - begin >= 2)
                    {
                        bestBegin  = begin;
                        bestLength = end - begin;
                    }
                    begin = end;
                }

                for (NGIN::UIntSize index = 0; index < words.size();)
                {
                    if (index == bestBegin)
                    {
                        output[size++] = ':';
                        output[size++] = ':';
                        index += bestLength;
                        continue;
                    }
                    if (size != 0 && output[size - 1] != ':')
                    {
                        output[size++] = ':';
                    }
                    AppendHex(output, size, words[index]);
                    ++index;
                }
            }
        }

        if (destination.size() < size)
        {
            return false;
        }
        std::copy_n(output.begin(), size, destination.begin());
        written = size;
        return true;
    }

    std::string IpAddress::ToString() const
    {
        std::array<char, 64> output {};
        NGIN::UIntSize       written = 0;
        if (!TryFormat(output, written))
        {
            return {};
        }
        return std::string {output.data(), written};
    }

    std::size_t IpAddressHash::operator()(const IpAddress& address) const noexcept
    {
        std::size_t hash = 1469598103934665603ull;
        hash ^= static_cast<std::size_t>(address.GetFamily());
        hash *= 1099511628211ull;
        for (const auto byte: address.Bytes())
        {
            hash ^= std::to_integer<std::size_t>(byte);
            hash *= 1099511628211ull;
        }
        return hash;
    }
}// namespace NGIN::Net
