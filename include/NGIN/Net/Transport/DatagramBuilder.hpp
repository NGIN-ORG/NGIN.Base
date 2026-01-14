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
        DatagramBuilder() noexcept = default;

        DatagramBuilder& FromUdpSocket(UdpSocket&& socket, NetworkDriver& driver) noexcept
        {
            m_socket = std::move(socket);
            m_driver = &driver;
            m_hasSocket = true;
            return *this;
        }

        [[nodiscard]] NGIN::Net::NetExpected<std::unique_ptr<IDatagramChannel>> Build()
        {
            if (!m_hasSocket || !m_driver)
            {
                return std::unexpected(NGIN::Net::NetError {NGIN::Net::NetErrorCode::Unknown, 0});
            }
            auto channel = std::make_unique<UdpDatagramChannel>(std::move(m_socket), *m_driver);
            m_hasSocket = false;
            m_driver = nullptr;
            return channel;
        }

    private:
        UdpSocket      m_socket {};
        NetworkDriver* m_driver {nullptr};
        bool           m_hasSocket {false};
    };
}// namespace NGIN::Net::Transport

