/// @file DatagramBuilder.hpp
/// @brief Builder for datagram transports.
#pragma once

#include <memory>
#include <utility>

#include <NGIN/Net/Transport/UdpDatagramChannel.hpp>

namespace NGIN::Net::Transport
{
    /// @brief Builder for IDatagramChannel adapters.
    class DatagramBuilder final
    {
    public:
        /// @brief Constructs an empty datagram-channel builder.
        DatagramBuilder() noexcept = default;

        /// @brief Selects a UDP socket with its existing runtime binding.
        DatagramBuilder& FromUdpSocket(UdpSocket&& socket) noexcept
        {
            m_socket    = std::move(socket);
            m_hasSocket = true;
            return *this;
        }

        /// @brief Consumes the selected socket and builds a UDP datagram-channel adapter.
        [[nodiscard]] NGIN::Net::NetExpected<std::unique_ptr<IDatagramChannel>> Build()
        {
            if (!m_hasSocket)
            {
                return NGIN::Utilities::Unexpected(NGIN::Net::NetError {NGIN::Net::NetErrorCode::Unknown, 0});
            }
            std::unique_ptr<UdpDatagramChannel> channel =
                    std::make_unique<UdpDatagramChannel>(std::move(m_socket));
            m_hasSocket                           = false;
            std::unique_ptr<IDatagramChannel> out = std::move(channel);
            return out;
        }

    private:
        UdpSocket m_socket {};
        bool      m_hasSocket {false};
    };
}// namespace NGIN::Net::Transport
