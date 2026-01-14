/// @file ByteStreamBuilder.hpp
/// @brief Builder for byte stream transports.
#pragma once

#include <memory>
#include <utility>

#include <NGIN/Net/Transport/Filters/LengthPrefixedMessageStream.hpp>
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

        [[nodiscard]] NGIN::Net::NetExpected<std::unique_ptr<IByteStream>> Build()
        {
            if (!m_hasSocket || !m_driver)
            {
                return std::unexpected(NGIN::Net::NetError {NGIN::Net::NetErrorCode::Unknown, 0});
            }
            auto stream = std::make_unique<TcpByteStream>(std::move(m_socket), *m_driver);
            m_hasSocket = false;
            m_driver = nullptr;
            return stream;
        }

        [[nodiscard]] NGIN::Net::NetExpected<std::unique_ptr<Filters::LengthPrefixedMessageStream>> BuildLengthPrefixed()
        {
            if (!m_hasSocket || !m_driver)
            {
                return std::unexpected(NGIN::Net::NetError {NGIN::Net::NetErrorCode::Unknown, 0});
            }
            auto base = std::make_unique<TcpByteStream>(std::move(m_socket), *m_driver);
            auto stream = std::make_unique<Filters::LengthPrefixedMessageStream>(std::move(base));
            m_hasSocket = false;
            m_driver = nullptr;
            return stream;
        }

    private:
        TcpSocket      m_socket {};
        NetworkDriver* m_driver {nullptr};
        bool           m_hasSocket {false};
    };
}// namespace NGIN::Net::Transport

