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
        /// @brief Constructs an empty byte-stream builder.
        ByteStreamBuilder() noexcept = default;

        /// @brief Selects a TCP socket and borrows the driver used by the resulting stream.
        ByteStreamBuilder& FromTcpSocket(TcpSocket&& socket, NetworkDriver& driver) noexcept
        {
            m_socket    = std::move(socket);
            m_driver    = &driver;
            m_hasSocket = true;
            return *this;
        }

        /// @brief Consumes the selected socket and builds a TCP byte-stream adapter.
        [[nodiscard]] NGIN::Net::NetExpected<std::unique_ptr<IByteStream>> Build()
        {
            if (!m_hasSocket || !m_driver)
            {
                return NGIN::Utilities::Unexpected(NGIN::Net::NetError {NGIN::Net::NetErrorCode::Unknown, 0});
            }
            std::unique_ptr<TcpByteStream> stream = std::make_unique<TcpByteStream>(std::move(m_socket), *m_driver);
            m_hasSocket                           = false;
            m_driver                              = nullptr;
            std::unique_ptr<IByteStream> out      = std::move(stream);
            return out;
        }

        /// @brief Consumes the selected socket and builds a length-prefixed message stream.
        [[nodiscard]] NGIN::Net::NetExpected<std::unique_ptr<Filters::LengthPrefixedMessageStream>> BuildLengthPrefixed()
        {
            if (!m_hasSocket || !m_driver)
            {
                return NGIN::Utilities::Unexpected(NGIN::Net::NetError {NGIN::Net::NetErrorCode::Unknown, 0});
            }
            std::unique_ptr<TcpByteStream>                        base = std::make_unique<TcpByteStream>(std::move(m_socket), *m_driver);
            std::unique_ptr<Filters::LengthPrefixedMessageStream> stream =
                    std::make_unique<Filters::LengthPrefixedMessageStream>(std::move(base));
            m_hasSocket                                               = false;
            m_driver                                                  = nullptr;
            std::unique_ptr<Filters::LengthPrefixedMessageStream> out = std::move(stream);
            return out;
        }

    private:
        TcpSocket      m_socket {};
        NetworkDriver* m_driver {nullptr};
        bool           m_hasSocket {false};
    };
}// namespace NGIN::Net::Transport
