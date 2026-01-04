/// @file ByteStreamBuilder.hpp
/// @brief Builder for byte stream transports.
#pragma once

#include <memory>
#include <stdexcept>
#include <utility>

#include <NGIN/Net/Transport/TcpByteStream.hpp>

namespace NGIN::Net::Transport
{
    /// @brief Builder for IByteStream adapters.
    class ByteStreamBuilder final
    {
    public:
        ByteStreamBuilder() noexcept = default;

        ByteStreamBuilder& FromTcpSocket(TcpSocket&& socket, NetworkDriver& driver) noexcept
        {
            m_socket = std::move(socket);
            m_driver = &driver;
            m_hasSocket = true;
            return *this;
        }

        [[nodiscard]] std::unique_ptr<IByteStream> Build()
        {
            if (!m_hasSocket || !m_driver)
            {
                throw std::runtime_error("ByteStreamBuilder requires a TcpSocket and NetworkDriver");
            }
            return std::make_unique<TcpByteStream>(std::move(m_socket), *m_driver);
        }

    private:
        TcpSocket      m_socket {};
        NetworkDriver* m_driver {nullptr};
        bool           m_hasSocket {false};
    };
}// namespace NGIN::Net::Transport
