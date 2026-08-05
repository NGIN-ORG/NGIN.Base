#include <NGIN/Net/Types/Endpoint.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <utility>

namespace NGIN::Net
{
    namespace
    {
        [[nodiscard]] AddressExpected<NGIN::UInt16> ParsePort(
                std::string_view text,
                NGIN::UIntSize   offset) noexcept
        {
            if (text.empty())
            {
                return NGIN::Utilities::Unexpected<AddressParseError>(
                        {AddressParseErrorCode::MissingPort, offset});
            }
            unsigned   value  = 0;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, 10);
            if (parsed.ec == std::errc::invalid_argument || parsed.ptr != text.data() + text.size())
            {
                return NGIN::Utilities::Unexpected<AddressParseError>(
                        {AddressParseErrorCode::InvalidCharacter,
                         offset + static_cast<NGIN::UIntSize>(parsed.ptr - text.data())});
            }
            if (parsed.ec == std::errc::result_out_of_range || value > 65535)
            {
                return NGIN::Utilities::Unexpected<AddressParseError>(
                        {AddressParseErrorCode::PortOutOfRange, offset});
            }
            return static_cast<NGIN::UInt16>(value);
        }

        [[nodiscard]] AddressExpected<NGIN::UInt32> ParseScope(
                std::string_view text,
                NGIN::UIntSize   offset) noexcept
        {
            if (text.empty())
            {
                return NGIN::Utilities::Unexpected<AddressParseError>(
                        {AddressParseErrorCode::InvalidScope, offset});
            }
            NGIN::UInt32 value  = 0;
            const auto   parsed = std::from_chars(text.data(), text.data() + text.size(), value, 10);
            if (parsed.ec != std::errc {} || parsed.ptr != text.data() + text.size())
            {
                return NGIN::Utilities::Unexpected<AddressParseError>(
                        {AddressParseErrorCode::InvalidScope,
                         offset + static_cast<NGIN::UIntSize>(parsed.ptr - text.data())});
            }
            return value;
        }
    }// namespace

    AddressExpected<Endpoint> Endpoint::Parse(std::string_view text)
    {
        if (text.empty())
        {
            return NGIN::Utilities::Unexpected<AddressParseError>({AddressParseErrorCode::Empty, 0});
        }

        std::string_view addressText;
        std::string_view portText;
        NGIN::UInt32     scopeId    = 0;
        NGIN::UIntSize   portOffset = 0;
        if (text.front() == '[')
        {
            const auto close = text.find(']');
            if (close == std::string_view::npos || close + 1 >= text.size() || text[close + 1] != ':')
            {
                return NGIN::Utilities::Unexpected<AddressParseError>(
                        {AddressParseErrorCode::InvalidFormat, close == std::string_view::npos ? text.size() : close + 1});
            }
            addressText = text.substr(1, close - 1);
            portText    = text.substr(close + 2);
            portOffset  = close + 2;
            if (const auto percent = addressText.find('%'); percent != std::string_view::npos)
            {
                auto scope = ParseScope(addressText.substr(percent + 1), percent + 2);
                if (!scope.HasValue())
                {
                    return NGIN::Utilities::Unexpected<AddressParseError>(std::move(scope).TakeError());
                }
                scopeId     = scope.Value();
                addressText = addressText.substr(0, percent);
            }
        }
        else
        {
            const auto separator = text.rfind(':');
            if (separator == std::string_view::npos)
            {
                return NGIN::Utilities::Unexpected<AddressParseError>(
                        {AddressParseErrorCode::MissingPort, text.size()});
            }
            if (text.find(':') != separator)
            {
                return NGIN::Utilities::Unexpected<AddressParseError>(
                        {AddressParseErrorCode::InvalidFormat, separator});
            }
            addressText = text.substr(0, separator);
            portText    = text.substr(separator + 1);
            portOffset  = separator + 1;
        }

        auto address = IpAddress::Parse(addressText);
        if (!address.HasValue())
        {
            auto error = std::move(address).TakeError();
            if (text.front() == '[')
            {
                ++error.offset;
            }
            return NGIN::Utilities::Unexpected<AddressParseError>(error);
        }
        if ((text.front() == '[' && !address.Value().IsV6()) ||
            (scopeId != 0 && !address.Value().IsV6()))
        {
            return NGIN::Utilities::Unexpected<AddressParseError>(
                    {AddressParseErrorCode::InvalidScope, 1});
        }
        auto port = ParsePort(portText, portOffset);
        if (!port.HasValue())
        {
            return NGIN::Utilities::Unexpected<AddressParseError>(std::move(port).TakeError());
        }
        return Endpoint {std::move(address).TakeValue(), port.Value(), scopeId};
    }

    bool Endpoint::TryFormat(std::span<char> destination, NGIN::UIntSize& written) const noexcept
    {
        written = 0;
        std::array<char, 96> output {};
        NGIN::UIntSize       size = 0;
        if (address.IsV6())
        {
            output[size++] = '[';
        }
        NGIN::UIntSize addressSize = 0;
        if (!address.TryFormat(std::span<char> {output}.subspan(size), addressSize))
        {
            return false;
        }
        size += addressSize;
        if (address.IsV6())
        {
            if (scopeId != 0)
            {
                output[size++]         = '%';
                const auto scopeResult = std::to_chars(output.data() + size, output.data() + output.size(), scopeId);
                size                   = static_cast<NGIN::UIntSize>(scopeResult.ptr - output.data());
            }
            output[size++] = ']';
        }
        output[size++]        = ':';
        const auto portResult = std::to_chars(output.data() + size, output.data() + output.size(), port);
        size                  = static_cast<NGIN::UIntSize>(portResult.ptr - output.data());
        if (destination.size() < size)
        {
            return false;
        }
        std::copy_n(output.begin(), size, destination.begin());
        written = size;
        return true;
    }

    std::string Endpoint::ToString() const
    {
        std::array<char, 96> output {};
        NGIN::UIntSize       written = 0;
        if (!TryFormat(output, written))
        {
            return {};
        }
        return std::string {output.data(), written};
    }

    std::size_t EndpointHash::operator()(const Endpoint& endpoint) const noexcept
    {
        auto hash = IpAddressHash {}(endpoint.address);
        hash ^= static_cast<std::size_t>(endpoint.port) + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
        hash ^= static_cast<std::size_t>(endpoint.scopeId) + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
        return hash;
    }
}// namespace NGIN::Net
